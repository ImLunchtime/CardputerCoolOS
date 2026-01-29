#include "music_player.h"
#include <hal.h>

extern "C" {
#include "audio_player.h"
}

#include <array>
#include <atomic>
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

enum class PlayerCmdType : uint8_t {
    PlayFile = 0,
    TogglePause = 1,
    Stop = 2,
};

struct PlayerCmd {
    PlayerCmdType type = PlayerCmdType::Stop;
    char path[256]{};
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

    while (GetHAL().speaker.isPlaying(w->speaker_channel) == 2) {
        vTaskDelay(1);
    }

    bool ok = GetHAL().speaker.playRaw(
        buf.data(),
        sample_count,
        w->sample_rate,
        w->stereo,
        1,
        w->speaker_channel,
        false);
    if (!ok) {
        vTaskDelay(1);
    }

    *bytes_written = len;
    return ESP_OK;
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
                esp_err_t ret = audio_player_play(fp);
                if (ret != ESP_OK) {
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
    cfg.priority = 5;
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

    g_cmd_queue = xQueueCreate(4, sizeof(PlayerCmd));
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
    return xQueueSend(g_cmd_queue, &cmd, 0) == pdTRUE;
}

void MusicPlayer::togglePause()
{
    if (!g_inited.load()) {
        return;
    }
    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::TogglePause;
    (void)xQueueSend(g_cmd_queue, &cmd, 0);
}

void MusicPlayer::stop()
{
    if (!g_inited.load()) {
        return;
    }
    PlayerCmd cmd{};
    cmd.type = PlayerCmdType::Stop;
    (void)xQueueSend(g_cmd_queue, &cmd, 0);
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
