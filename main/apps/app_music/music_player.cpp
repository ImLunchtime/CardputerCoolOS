#include "music_player.h"
#include <hal.h>

extern "C" {
#include "audio_player.h"
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

struct SpeakerWriteCtx {
    int speaker_channel = 0;
    uint32_t sample_rate = 44100;
    bool stereo = true;
    size_t buf_index = 0;
    std::array<std::vector<int16_t>, 3> buffers;
};

static std::atomic<bool> g_inited = false;
static std::atomic<bool> g_dirty = false;
static std::atomic<audio_player_state_t> g_state_cache = AUDIO_PLAYER_STATE_IDLE;
static SpeakerWriteCtx g_write_ctx;
static std::atomic<uint64_t> g_pcm_frames_written = 0;
static std::atomic<uint32_t> g_base_ms = 0;

struct Mp3CbrInfo {
    bool valid = false;
    uint32_t data_start = 0;
    uint32_t frame_len = 0;
    uint32_t sample_rate = 0;
    uint16_t samples_per_frame = 0;
    uint32_t file_size = 0;
    char path[512]{};
};

static Mp3CbrInfo g_track;

enum class PlayerCmdType : uint8_t {
    PlayFile = 0,
    TogglePause = 1,
    Stop = 2,
    SeekBySeconds = 3,
};

struct PlayerCmd {
    PlayerCmdType type = PlayerCmdType::Stop;
    char path[512]{};
    int32_t seek_delta_seconds = 0;
};

static QueueHandle_t g_cmd_queue = nullptr;
static TaskHandle_t g_cmd_task = nullptr;
static SemaphoreHandle_t g_player_mutex = nullptr;

static esp_err_t clk_set(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    if (bits_cfg != 16) {
        return ESP_ERR_INVALID_ARG;
    }
    g_write_ctx.sample_rate = rate;
    g_write_ctx.stereo = (ch == I2S_SLOT_MODE_STEREO);
    return ESP_OK;
}

static esp_err_t write_pcm(void* audio_buffer, size_t len, size_t* bytes_written, uint32_t timeout_ms, void* ctx)
{
    (void)timeout_ms;
    auto* w = static_cast<SpeakerWriteCtx*>(ctx);
    if (w == nullptr || bytes_written == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((len % 2) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t sample_count = len / 2;
    auto& buf = w->buffers[w->buf_index++ % w->buffers.size()];
    buf.resize(sample_count);
    std::memcpy(buf.data(), audio_buffer, len);

    if (w->sample_rate > 0) {
        const uint32_t ch = w->stereo ? 2u : 1u;
        g_pcm_frames_written.fetch_add(sample_count / ch);
    }

    while (GetHAL().speaker.isPlaying(w->speaker_channel) == 2) {
        vTaskDelay(1);
    }

    while (!GetHAL().speaker.playRaw(
        buf.data(),
        sample_count,
        w->sample_rate,
        w->stereo,
        1,
        w->speaker_channel,
        false)) {
        vTaskDelay(1);
    }

    *bytes_written = len;
    return ESP_OK;
}

static uint32_t syncsafe_u32(const uint8_t b0, const uint8_t b1, const uint8_t b2, const uint8_t b3)
{
    return (static_cast<uint32_t>(b0 & 0x7F) << 21) | (static_cast<uint32_t>(b1 & 0x7F) << 14) | (static_cast<uint32_t>(b2 & 0x7F) << 7) |
           static_cast<uint32_t>(b3 & 0x7F);
}

static bool parse_mp3_cbr_info(FILE* fp, Mp3CbrInfo& out)
{
    if (fp == nullptr) {
        return false;
    }

    out = Mp3CbrInfo{};

    if (fseek(fp, 0, SEEK_END) != 0) {
        return false;
    }
    const long size = ftell(fp);
    if (size <= 0) {
        return false;
    }
    out.file_size = static_cast<uint32_t>(size);
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return false;
    }

    uint8_t head10[10]{};
    size_t n = fread(head10, 1, sizeof(head10), fp);
    if (n < sizeof(head10)) {
        return false;
    }

    uint32_t start = 0;
    if (head10[0] == 'I' && head10[1] == 'D' && head10[2] == '3') {
        const uint32_t tag_size = syncsafe_u32(head10[6], head10[7], head10[8], head10[9]);
        start = 10u + tag_size;
        if (start >= out.file_size) {
            return false;
        }
    }

    if (fseek(fp, static_cast<long>(start), SEEK_SET) != 0) {
        return false;
    }

    static uint8_t buf[4096];
    uint32_t offset = start;
    uint8_t carry[3]{};
    size_t carry_len = 0;

    while (offset < out.file_size) {
        const size_t to_read = std::min<size_t>(sizeof(buf), out.file_size - offset);
        const size_t got = fread(buf, 1, to_read, fp);
        if (got < 4) {
            return false;
        }

        static uint8_t scan[4096 + 3];
        size_t scan_len = 0;
        if (carry_len > 0) {
            std::memcpy(scan, carry, carry_len);
            scan_len += carry_len;
        }
        std::memcpy(scan + scan_len, buf, got);
        scan_len += got;

        for (size_t i = 0; i + 4 <= scan_len; ++i) {
            const uint8_t b0 = scan[i];
            const uint8_t b1 = scan[i + 1];
            if (b0 != 0xFF || (b1 & 0xE0) != 0xE0) {
                continue;
            }

            const uint8_t b2 = scan[i + 2];

            const uint8_t ver = (b1 >> 3) & 0x03;
            const uint8_t layer = (b1 >> 1) & 0x03;
            const uint8_t bitrate_index = (b2 >> 4) & 0x0F;
            const uint8_t sr_index = (b2 >> 2) & 0x03;
            const uint8_t padding = (b2 >> 1) & 0x01;

            if (layer != 0x01) {
                continue;
            }
            if (ver == 0x01) {
                continue;
            }
            if (sr_index == 0x03 || bitrate_index == 0x00 || bitrate_index == 0x0F) {
                continue;
            }

            uint32_t sample_rate = 0;
            if (ver == 0x03) {
                static constexpr uint32_t t[3] = {44100, 48000, 32000};
                sample_rate = t[sr_index];
            } else if (ver == 0x02) {
                static constexpr uint32_t t[3] = {22050, 24000, 16000};
                sample_rate = t[sr_index];
            } else {
                static constexpr uint32_t t[3] = {11025, 12000, 8000};
                sample_rate = t[sr_index];
            }
            if (sample_rate == 0) {
                continue;
            }

            uint32_t bitrate_kbps = 0;
            if (ver == 0x03) {
                static constexpr uint16_t t[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
                bitrate_kbps = t[bitrate_index];
            } else {
                static constexpr uint16_t t[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
                bitrate_kbps = t[bitrate_index];
            }
            if (bitrate_kbps == 0) {
                continue;
            }

            const uint32_t coef = (ver == 0x03) ? 144000u : 72000u;
            const uint32_t frame_len = (coef * bitrate_kbps) / sample_rate + padding;
            if (frame_len < 24 || frame_len > 5000) {
                continue;
            }

            const int64_t found_offset_signed = static_cast<int64_t>(offset) + static_cast<int64_t>(i) - static_cast<int64_t>(carry_len);
            if (found_offset_signed < 0 || static_cast<uint64_t>(found_offset_signed) >= out.file_size) {
                return false;
            }
            const uint32_t found_offset = static_cast<uint32_t>(found_offset_signed);

            out.valid = true;
            out.data_start = found_offset;
            out.frame_len = frame_len;
            out.sample_rate = sample_rate;
            out.samples_per_frame = (ver == 0x03) ? 1152 : 576;
            return true;
        }

        carry_len = std::min<size_t>(3, scan_len);
        std::memcpy(carry, scan + scan_len - carry_len, carry_len);
        offset += static_cast<uint32_t>(got);
    }

    return false;
}

static uint32_t get_position_ms()
{
    const uint32_t sr = (g_track.valid && g_track.sample_rate > 0) ? g_track.sample_rate : g_write_ctx.sample_rate;
    if (sr == 0) {
        return g_base_ms.load();
    }
    const uint64_t frames = g_pcm_frames_written.load();
    const uint64_t delta_ms = (frames * 1000u) / sr;
    return g_base_ms.load() + static_cast<uint32_t>(delta_ms);
}

static void player_cb(audio_player_cb_ctx_t*)
{
    g_dirty.store(true);
    g_state_cache.store(audio_player_get_state());
}

static MusicPlayerState map_state(audio_player_state_t st)
{
    switch (st) {
        case AUDIO_PLAYER_STATE_PLAYING:
            return MusicPlayerState::Playing;
        case AUDIO_PLAYER_STATE_PAUSE:
            return MusicPlayerState::Paused;
        case AUDIO_PLAYER_STATE_IDLE:
        case AUDIO_PLAYER_STATE_SHUTDOWN:
        default:
            return MusicPlayerState::Idle;
    }
}

static void player_lock()
{
    if (g_player_mutex) {
        xSemaphoreTake(g_player_mutex, portMAX_DELAY);
    }
}

static void player_unlock()
{
    if (g_player_mutex) {
        xSemaphoreGive(g_player_mutex);
    }
}

static void cmd_task_main(void*)
{
    PlayerCmd cmd{};
    while (true) {
        if (xQueueReceive(g_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const auto play_with_retry = [](FILE* fp) -> esp_err_t {
            esp_err_t ret = ESP_FAIL;
            for (int i = 0; i < 30; ++i) {
                ret = audio_player_play(fp);
                if (ret == ESP_OK) {
                    return ret;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            return ret;
        };

        const auto pause_with_retry = []() -> esp_err_t {
            esp_err_t ret = ESP_FAIL;
            for (int i = 0; i < 30; ++i) {
                ret = audio_player_pause();
                if (ret == ESP_OK) {
                    return ret;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            return ret;
        };

        if (cmd.type == PlayerCmdType::Stop) {
            player_lock();
            audio_player_stop();
            GetHAL().speaker.stop(g_write_ctx.speaker_channel);
            g_state_cache.store(audio_player_get_state());
            player_unlock();
            g_dirty.store(true);
            continue;
        }

        if (cmd.type == PlayerCmdType::TogglePause) {
            player_lock();
            const auto st = audio_player_get_state();
            if (st == AUDIO_PLAYER_STATE_PLAYING) {
                audio_player_pause();
            } else if (st == AUDIO_PLAYER_STATE_PAUSE) {
                audio_player_resume();
            }
            g_state_cache.store(audio_player_get_state());
            player_unlock();
            g_dirty.store(true);
            continue;
        }

        if (cmd.type == PlayerCmdType::PlayFile) {
            player_lock();
            audio_player_stop();
            GetHAL().speaker.stop(g_write_ctx.speaker_channel);
            FILE* fp = fopen(cmd.path, "rb");
            if (fp) {
                Mp3CbrInfo info{};
                (void)parse_mp3_cbr_info(fp, info);
                if (fseek(fp, 0, SEEK_SET) == 0 && info.valid) {
                    std::memcpy(info.path, cmd.path, sizeof(info.path));
                }

                esp_err_t ret = play_with_retry(fp);
                if (ret == ESP_OK) {
                    g_track = info.valid ? info : Mp3CbrInfo{};
                    g_base_ms.store(0);
                    g_pcm_frames_written.store(0);
                } else {
                    fclose(fp);
                }
            }

            g_state_cache.store(audio_player_get_state());
            player_unlock();
            g_dirty.store(true);
            continue;
        }

        if (cmd.type == PlayerCmdType::SeekBySeconds) {
            if (!g_track.valid || g_track.frame_len == 0 || g_track.sample_rate == 0 || g_track.samples_per_frame == 0) {
                continue;
            }

            const uint32_t pos_ms = get_position_ms();
            const int64_t target_ms_signed = static_cast<int64_t>(pos_ms) + static_cast<int64_t>(cmd.seek_delta_seconds) * 1000;
            const uint64_t max_frames = (g_track.file_size > g_track.data_start) ? ((g_track.file_size - g_track.data_start) / g_track.frame_len) : 0;
            const uint64_t max_ms = (max_frames * static_cast<uint64_t>(g_track.samples_per_frame) * 1000u) / g_track.sample_rate;

            uint64_t target_ms = 0;
            if (target_ms_signed <= 0) {
                target_ms = 0;
            } else {
                target_ms = static_cast<uint64_t>(target_ms_signed);
                if (target_ms > max_ms) {
                    target_ms = max_ms;
                }
            }

            const uint64_t target_frames = (target_ms * g_track.sample_rate) / 1000u;
            const uint64_t target_mp3_frames = target_frames / g_track.samples_per_frame;
            uint64_t seek_offset = g_track.data_start + target_mp3_frames * g_track.frame_len;
            if (seek_offset >= g_track.file_size) {
                seek_offset = (g_track.file_size > 4) ? (g_track.file_size - 4) : 0;
            }
            if (seek_offset < g_track.data_start) {
                seek_offset = g_track.data_start;
            }

            const uint32_t new_base_ms = static_cast<uint32_t>((target_mp3_frames * static_cast<uint64_t>(g_track.samples_per_frame) * 1000u) / g_track.sample_rate);

            player_lock();
            const bool want_paused = (audio_player_get_state() == AUDIO_PLAYER_STATE_PAUSE);
            audio_player_stop();
            GetHAL().speaker.stop(g_write_ctx.speaker_channel);

            FILE* fp = fopen(g_track.path, "rb");
            if (fp) {
                if (fseek(fp, static_cast<long>(seek_offset), SEEK_SET) == 0) {
                    esp_err_t ret = play_with_retry(fp);
                    if (ret == ESP_OK) {
                        g_base_ms.store(new_base_ms);
                        g_pcm_frames_written.store(0);
                        if (want_paused) {
                            (void)pause_with_retry();
                        }
                    } else {
                        fclose(fp);
                    }
                } else {
                    fclose(fp);
                }
            }

            g_state_cache.store(audio_player_get_state());
            player_unlock();
            g_dirty.store(true);
            continue;
        }
    }
}

}  // namespace

MusicPlayer& MusicPlayer::instance()
{
    static MusicPlayer inst;
    return inst;
}

bool MusicPlayer::init()
{
    bool expected = false;
    if (!g_inited.compare_exchange_strong(expected, true)) {
        return true;
    }

    g_player_mutex = xSemaphoreCreateMutex();
    if (g_player_mutex == nullptr) {
        g_inited.store(false);
        return false;
    }

    audio_player_config_t cfg{};
    cfg.mute_fn = nullptr;
    cfg.clk_set_fn = clk_set;
    cfg.write_fn = nullptr;
    cfg.priority = 6;
    cfg.coreID = 1;
    cfg.force_stereo = true;
    cfg.write_fn2 = write_pcm;
    cfg.write_ctx = &g_write_ctx;

    esp_err_t ret = audio_player_new(cfg);
    if (ret != ESP_OK) {
        g_inited.store(false);
        return false;
    }

    audio_player_callback_register(player_cb, nullptr);
    GetHAL().speaker.setVolume(20);
    g_state_cache.store(audio_player_get_state());

    g_cmd_queue = xQueueCreate(8, sizeof(PlayerCmd));
    if (g_cmd_queue == nullptr) {
        g_inited.store(false);
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        cmd_task_main,
        "music_player_cmd",
        4096,
        nullptr,
        4,
        &g_cmd_task,
        1);
    if (ok != pdPASS) {
        g_inited.store(false);
        return false;
    }

    return true;
}

bool MusicPlayer::playFile(const std::string& path)
{
    if (!init()) {
        return false;
    }
    if (path.size() >= sizeof(PlayerCmd::path)) {
        return false;
    }

    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::PlayFile;
    std::memcpy(cmd.path, path.c_str(), path.size() + 1);
    return xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE;
}

void MusicPlayer::togglePause()
{
    if (!g_inited.load()) {
        return;
    }
    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::TogglePause;
    (void)xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void MusicPlayer::stop()
{
    if (!g_inited.load()) {
        return;
    }
    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::Stop;
    (void)xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void MusicPlayer::seekBySeconds(int delta_seconds)
{
    if (!g_inited.load()) {
        return;
    }
    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::SeekBySeconds;
    cmd.seek_delta_seconds = delta_seconds;
    (void)xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

MusicPlayerState MusicPlayer::state() const
{
    if (!g_inited.load()) {
        return MusicPlayerState::Idle;
    }
    return map_state(g_state_cache.load());
}

bool MusicPlayer::consumeDirty()
{
    return g_dirty.exchange(false);
}
