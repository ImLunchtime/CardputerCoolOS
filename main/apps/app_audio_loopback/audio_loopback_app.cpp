#include "audio_loopback_app.h"

#include <hal.h>
#include <mooncake.h>
#include <mooncake_log.h>

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include <driver/i2s_std.h>
#include <esp_err.h>

namespace {

static const std::string kTag = "AudioLoopback";

constexpr uint8_t kEs8311Addr = 0x18;

constexpr i2s_port_t kI2SPort = I2S_NUM_1;
constexpr gpio_num_t kI2SBclk = GPIO_NUM_41;
constexpr gpio_num_t kI2SWs   = GPIO_NUM_43;
constexpr gpio_num_t kI2SDout = GPIO_NUM_42;
constexpr gpio_num_t kI2SDin  = GPIO_NUM_46;

constexpr uint32_t kSampleRate = 16000;
constexpr size_t kChunkFrames = 128;

}  // namespace

AudioLoopbackApp::AudioLoopbackApp()
{
    setAppInfo().name = "Audio Loopback";
}

void AudioLoopbackApp::onOpen()
{
    mclog::tagInfo(kTag, "onOpen");
    _prev_volume = GetHAL().speaker.getVolume();
    _prev_volume_valid = true;

    _volume.store(0);
    _delay_ms.store(0);
    _loopback_enabled.store(false);
    _needs_redraw = true;

    GetHAL().speaker.stop();
    GetHAL().speaker.end();
    GetHAL().mic.end();

    const bool ok = initLoopbackEngine();
    mclog::tagInfo(kTag, "initLoopbackEngine: {}", ok);

    hookKeyboard();
    if (ok) {
        startLoopbackTask();
    }
    draw();
}

void AudioLoopbackApp::loopbackTaskMain(void* arg)
{
    auto* app = static_cast<AudioLoopbackApp*>(arg);
    if (app == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    mclog::tagInfo(kTag, "loopback read task start");
    uint8_t last_vol = 0xFF;
    static int16_t buf[kChunkFrames * 2];
    auto* rb = static_cast<RingbufHandle_t>(app->_ring_buffer_handle);

    while (app->_task_running.load()) {
        auto* rx = static_cast<i2s_chan_handle_t>(app->_i2s_rx_handle);
        if (rx == nullptr || rb == nullptr) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        size_t bytes_read = 0;
        esp_err_t rr = i2s_channel_read(rx, buf, sizeof(buf), &bytes_read, 100 / portTICK_PERIOD_MS);
        if (rr != ESP_OK || bytes_read == 0) {
            continue;
        }

        const bool enabled = app->_loopback_enabled.load();
        const uint8_t vol = app->_volume.load();
        const bool audible = enabled && vol > 0;
        const uint8_t dac_vol = audible ? 0xBF : 0;
        constexpr uint32_t kMaxDigitalGainQ8 = 64u * 256u;
        const uint32_t gain_q8 = audible ? (static_cast<uint32_t>(vol) * kMaxDigitalGainQ8) / 255u : 0u;

        if (dac_vol != last_vol) {
            last_vol = dac_vol;
            bool ok = M5.In_I2C.writeRegister8(kEs8311Addr, 0x32, dac_vol, 400000);
            if (!ok) {
                mclog::tagWarn(kTag, "i2c write fail: ES8311 reg 0x32");
            }
        }

        const size_t frames = bytes_read / (sizeof(int16_t) * 2);
        if (!audible || frames == 0) {
            std::memset(buf, 0, frames * sizeof(int16_t) * 2);
        } else {
            for (size_t i = 0; i < frames; ++i) {
                const int32_t l = buf[i * 2];
                const int32_t r = buf[i * 2 + 1];
                int32_t s = (l + r) / 2;
                int32_t y = (s * static_cast<int32_t>(gain_q8)) >> 8;
                if (y > 32767) y = 32767;
                if (y < -32768) y = -32768;
                const int16_t out = static_cast<int16_t>(y);
                buf[i * 2] = out;
                buf[i * 2 + 1] = out;
            }
        }

        if (xRingbufferSend(rb, buf, bytes_read, 0) != pdTRUE) {
            // buffer full, maybe drop or overwrite? 
            // For now just drop to keep latest data flow
        }
    }

    mclog::tagInfo(kTag, "loopback read task stop");
    app->_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void AudioLoopbackApp::writeTaskMain(void* arg)
{
    auto* app = static_cast<AudioLoopbackApp*>(arg);
    if (app == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    mclog::tagInfo(kTag, "loopback write task start");
    auto* rb = static_cast<RingbufHandle_t>(app->_ring_buffer_handle);
    static const uint8_t zeros[512] = {0}; 

    while (app->_task_running.load()) {
        auto* tx = static_cast<i2s_chan_handle_t>(app->_i2s_tx_handle);
        if (tx == nullptr || rb == nullptr) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        const int delay_ms = app->_delay_ms.load();
        const size_t target_delay_bytes = delay_ms * 64; // 16kHz * 4 bytes/sample * ms
        const size_t total_size = 80 * 1024;
        const size_t free_size = xRingbufferGetCurFreeSize(rb);
        const size_t used_size = total_size - free_size;

        // If buffer has less data than target delay, write silence to wait
        if (used_size < target_delay_bytes) {
            size_t bytes_written = 0;
            i2s_channel_write(tx, zeros, sizeof(zeros), &bytes_written, 100 / portTICK_PERIOD_MS);
            continue;
        }

        // If buffer has too much data (lag > target + 100ms), drop data to catch up
        if (used_size > target_delay_bytes + 6400) {
            size_t size = 0;
            void* data = xRingbufferReceive(rb, &size, 0);
            if (data) {
                vRingbufferReturnItem(rb, data);
            }
            continue;
        }

        size_t size = 0;
        void* data = xRingbufferReceive(rb, &size, 100 / portTICK_PERIOD_MS);
        if (data != nullptr && size > 0) {
            size_t bytes_written = 0;
            i2s_channel_write(tx, data, size, &bytes_written, 100 / portTICK_PERIOD_MS);
            vRingbufferReturnItem(rb, data);
        }
    }

    mclog::tagInfo(kTag, "loopback write task stop");
    app->_write_task_handle = nullptr;
    vTaskDelete(nullptr);
}

void AudioLoopbackApp::onRunning()
{
    if (_needs_redraw) {
        _needs_redraw = false;
        draw();
    }
}

void AudioLoopbackApp::onClose()
{
    mclog::tagInfo(kTag, "onClose");
    stopLoopbackTask();
    unhookKeyboard();
    deinitLoopbackEngine();

    if (_prev_volume_valid) {
        GetHAL().speaker.begin();
        GetHAL().speaker.setVolume(_prev_volume);
    } else {
        GetHAL().speaker.begin();
    }
}

void AudioLoopbackApp::startLoopbackTask()
{
    if (_task_handle != nullptr) {
        return;
    }

    // 80KB ring buffer (approx 1250ms at 16kHz stereo 16bit)
    _ring_buffer_handle = xRingbufferCreate(80 * 1024, RINGBUF_TYPE_BYTEBUF);
    if (_ring_buffer_handle == nullptr) {
        mclog::tagError(kTag, "create ring buffer failed");
        return;
    }

    _task_running.store(true);
    
    // Create Read Task (Producer)
    TaskHandle_t read_handle = nullptr;
    BaseType_t ok_read = xTaskCreatePinnedToCore(
        AudioLoopbackApp::loopbackTaskMain,
        "loop_read",
        8192,
        this,
        4,
        &read_handle,
        1);

    if (ok_read != pdPASS) {
        mclog::tagError(kTag, "create read task failed");
        _task_running.store(false);
        vRingbufferDelete(static_cast<RingbufHandle_t>(_ring_buffer_handle));
        _ring_buffer_handle = nullptr;
        return;
    }
    _task_handle = read_handle;

    // Create Write Task (Consumer)
    TaskHandle_t write_handle = nullptr;
    BaseType_t ok_write = xTaskCreatePinnedToCore(
        AudioLoopbackApp::writeTaskMain,
        "loop_write",
        8192,
        this,
        4,
        &write_handle,
        1);

    if (ok_write != pdPASS) {
        mclog::tagError(kTag, "create write task failed");
        // Clean up read task (it will stop because _task_running is set to false, but we should be careful)
        _task_running.store(false);
        // We rely on stopLoopbackTask logic or manual cleanup, but here just set null
        // Read task will exit soon
        _write_task_handle = nullptr; 
        return;
    }
    _write_task_handle = write_handle;
}

void AudioLoopbackApp::stopLoopbackTask()
{
    _task_running.store(false);
    
    // Wait for tasks to stop
    while (_task_handle != nullptr || _write_task_handle != nullptr) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (_ring_buffer_handle != nullptr) {
        vRingbufferDelete(static_cast<RingbufHandle_t>(_ring_buffer_handle));
        _ring_buffer_handle = nullptr;
    }
}

bool AudioLoopbackApp::initLoopbackEngine()
{
    if (_i2s_tx_handle != nullptr || _i2s_rx_handle != nullptr) {
        return true;
    }

    mclog::tagInfo(
        kTag,
        "I2C enabled={} port={} sda={} scl={}",
        M5.In_I2C.isEnabled(),
        static_cast<int>(M5.In_I2C.getPort()),
        static_cast<int>(M5.In_I2C.getSDA()),
        static_cast<int>(M5.In_I2C.getSCL()));

    if (!M5.In_I2C.isEnabled()) {
        mclog::tagError(kTag, "I2C not enabled");
        return false;
    }

    const bool codec_found = M5.In_I2C.scanID(kEs8311Addr, 400000);
    mclog::tagInfo(kTag, "ES8311 found={} addr=0x{:02X}", codec_found, static_cast<unsigned>(kEs8311Addr));
    if (!codec_found) {
        return false;
    }

    GetHAL().speaker.stop();
    GetHAL().speaker.end();
    GetHAL().mic.end();

    auto wr = [&](uint8_t reg, uint8_t val) -> bool {
        bool ok = M5.In_I2C.writeRegister8(kEs8311Addr, reg, val, 400000);
        if (!ok) {
            mclog::tagError(kTag, "i2c write fail: reg=0x{:02X} val=0x{:02X}", (unsigned)reg, (unsigned)val);
        }
        return ok;
    };
    auto rd = [&](uint8_t reg, uint8_t* out) -> bool {
        return M5.In_I2C.readRegister(kEs8311Addr, reg, out, 1, 400000);
    };

    uint8_t reg00 = 0;
    if (rd(0x00, &reg00)) {
        mclog::tagInfo(kTag, "ES8311 reg00(before)=0x{:02X}", (unsigned)reg00);
    } else {
        mclog::tagWarn(kTag, "i2c read fail: reg00(before)");
    }

    bool ok_i2c = true;
    ok_i2c &= wr(0x00, 0x80);
    ok_i2c &= wr(0x01, 0xBF);
    ok_i2c &= wr(0x02, 0x18);
    ok_i2c &= wr(0x0D, 0x01);
    ok_i2c &= wr(0x0E, 0x02);
    ok_i2c &= wr(0x14, 0x10);
    ok_i2c &= wr(0x17, 0xBF);
    ok_i2c &= wr(0x1C, 0x6A);
    ok_i2c &= wr(0x12, 0x00);
    ok_i2c &= wr(0x13, 0x10);
    ok_i2c &= wr(0x32, 0x00);
    ok_i2c &= wr(0x37, 0x08);

    if (!ok_i2c) {
        return false;
    }

    if (ok_i2c) {
        uint8_t r1 = 0, r0d = 0, r0e = 0, r12 = 0, r13 = 0, r32 = 0;
        rd(0x01, &r1);
        rd(0x0D, &r0d);
        rd(0x0E, &r0e);
        rd(0x12, &r12);
        rd(0x13, &r13);
        rd(0x32, &r32);
        mclog::tagInfo(
            kTag,
            "ES8311 regs: 01=0x{:02X} 0D=0x{:02X} 0E=0x{:02X} 12=0x{:02X} 13=0x{:02X} 32=0x{:02X}",
            (unsigned)r1,
            (unsigned)r0d,
            (unsigned)r0e,
            (unsigned)r12,
            (unsigned)r13,
            (unsigned)r32);
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(kI2SPort, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 128;
    chan_cfg.auto_clear = true;

    i2s_chan_handle_t tx_handle = nullptr;
    i2s_chan_handle_t rx_handle = nullptr;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    if (err != ESP_OK) {
        mclog::tagError(kTag, "i2s_new_channel failed: {}", esp_err_to_name(err));
        return false;
    }

    i2s_std_config_t tx_cfg;
    std::memset(&tx_cfg, 0, sizeof(tx_cfg));
    tx_cfg.clk_cfg.clk_src = i2s_clock_src_t::I2S_CLK_SRC_PLL_160M;
    tx_cfg.clk_cfg.sample_rate_hz = kSampleRate;
    tx_cfg.clk_cfg.mclk_multiple = i2s_mclk_multiple_t::I2S_MCLK_MULTIPLE_128;
    tx_cfg.slot_cfg.data_bit_width = i2s_data_bit_width_t::I2S_DATA_BIT_WIDTH_16BIT;
    tx_cfg.slot_cfg.slot_bit_width = i2s_slot_bit_width_t::I2S_SLOT_BIT_WIDTH_16BIT;
    tx_cfg.slot_cfg.slot_mode = i2s_slot_mode_t::I2S_SLOT_MODE_STEREO;
    tx_cfg.slot_cfg.slot_mask = i2s_std_slot_mask_t::I2S_STD_SLOT_BOTH;
    tx_cfg.slot_cfg.ws_width = 16;
    tx_cfg.slot_cfg.ws_pol = false;
    tx_cfg.slot_cfg.bit_shift = true;
    tx_cfg.slot_cfg.left_align = true;
    tx_cfg.slot_cfg.big_endian = false;
    tx_cfg.slot_cfg.bit_order_lsb = false;
    tx_cfg.gpio_cfg.bclk = kI2SBclk;
    tx_cfg.gpio_cfg.ws = kI2SWs;
    tx_cfg.gpio_cfg.dout = kI2SDout;
    tx_cfg.gpio_cfg.din = GPIO_NUM_NC;
    tx_cfg.gpio_cfg.mclk = GPIO_NUM_NC;

    i2s_std_config_t rx_cfg = tx_cfg;
    rx_cfg.gpio_cfg.dout = GPIO_NUM_NC;
    rx_cfg.gpio_cfg.din = kI2SDin;

    err = i2s_channel_init_std_mode(tx_handle, &tx_cfg);
    if (err != ESP_OK) {
        mclog::tagError(kTag, "i2s init tx failed: {}", esp_err_to_name(err));
        i2s_del_channel(tx_handle);
        i2s_del_channel(rx_handle);
        return false;
    }
    err = i2s_channel_init_std_mode(rx_handle, &rx_cfg);
    if (err != ESP_OK) {
        mclog::tagError(kTag, "i2s init rx failed: {}", esp_err_to_name(err));
        i2s_del_channel(tx_handle);
        i2s_del_channel(rx_handle);
        return false;
    }

    err = i2s_channel_enable(tx_handle);
    if (err != ESP_OK) {
        mclog::tagError(kTag, "i2s enable tx failed: {}", esp_err_to_name(err));
        i2s_del_channel(tx_handle);
        i2s_del_channel(rx_handle);
        return false;
    }
    err = i2s_channel_enable(rx_handle);
    if (err != ESP_OK) {
        mclog::tagError(kTag, "i2s enable rx failed: {}", esp_err_to_name(err));
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        i2s_del_channel(rx_handle);
        return false;
    }

    _i2s_tx_handle = tx_handle;
    _i2s_rx_handle = rx_handle;
    mclog::tagInfo(kTag, "i2s enabled ok");
    return true;
}

void AudioLoopbackApp::deinitLoopbackEngine()
{
    auto* tx = static_cast<i2s_chan_handle_t>(_i2s_tx_handle);
    auto* rx = static_cast<i2s_chan_handle_t>(_i2s_rx_handle);
    _i2s_tx_handle = nullptr;
    _i2s_rx_handle = nullptr;

    if (tx) {
        i2s_channel_disable(tx);
        i2s_del_channel(tx);
    }
    if (rx) {
        i2s_channel_disable(rx);
        i2s_del_channel(rx);
    }

    M5.In_I2C.writeRegister8(kEs8311Addr, 0x0D, 0xFC, 400000);
    M5.In_I2C.writeRegister8(kEs8311Addr, 0x0E, 0x6A, 400000);
    M5.In_I2C.writeRegister8(kEs8311Addr, 0x00, 0x00, 400000);
}

void AudioLoopbackApp::hookKeyboard()
{
    if (_keyboard_slot_id != 0) {
        return;
    }

    _keyboard_slot_id = GetHAL().keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& e) {
        if (!e.state) {
            return;
        }

        if (e.keyCode == KEY_BACKSPACE || e.keyCode == KEY_DELETE) {
            openDesktopAndCloseSelf();
            return;
        }

        if (e.keyCode == KEY_ENTER || e.keyCode == KEY_SPACE) {
            const bool next = !_loopback_enabled.load();
            _loopback_enabled.store(next);
            _needs_redraw = true;
            return;
        }

        if (e.keyCode == KEY_LEFTBRACE || e.keyCode == KEY_RIGHTBRACE) {
            int d = _delay_ms.load();
            const int step = 50;
            if (e.keyCode == KEY_LEFTBRACE) {
                d -= step;
            } else {
                d += step;
            }
            if (d < 0) d = 0;
            if (d > kMaxDelayMs) d = kMaxDelayMs;
            _delay_ms.store(d);
            _needs_redraw = true;
            return;
        }

        if (e.keyCode == KEY_MINUS || e.keyCode == KEY_EQUAL) {
            int v = static_cast<int>(_volume.load());
            const int step = 5;
            if (e.keyCode == KEY_MINUS) {
                v -= step;
            } else {
                v += step;
            }
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            _volume.store(static_cast<uint8_t>(v));
            _needs_redraw = true;
            return;
        }
    });
}

void AudioLoopbackApp::unhookKeyboard()
{
    if (_keyboard_slot_id == 0) {
        return;
    }
    GetHAL().keyboard.onKeyEvent.disconnect(_keyboard_slot_id);
    _keyboard_slot_id = 0;
}

void AudioLoopbackApp::openDesktopAndCloseSelf()
{
    auto& mc = mooncake::GetMooncake();
    auto* app_mgr = mc.getAppAbilityManager();
    const auto app_instances = app_mgr ? app_mgr->getAllAbilityInstance() : std::vector<mooncake::AbilityBase*>{};

    for (auto* app : app_instances) {
        if (app == nullptr) {
            continue;
        }
        const int id = app->getId();
        const auto info = mc.getAppInfo(id);
        if (info.name == "Desktop") {
            mc.openApp(id);
            mc.closeApp(getId());
            return;
        }
    }
}

void AudioLoopbackApp::draw()
{
    auto& canvas = GetHAL().canvas;
    const uint16_t bg = lgfx::color565(0x22, 0x22, 0x22);
    const uint16_t fg = lgfx::color565(0xEE, 0xEE, 0xEE);
    const uint16_t accent = lgfx::color565(0xFF, 0x8D, 0x1A);

    canvas.fillScreen(bg);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1);
    canvas.setTextColor(fg);
    canvas.setTextDatum(textdatum_t::top_left);

    canvas.drawString("Audio Loopback", 6, 0);

    const bool enabled = _loopback_enabled.load();
    canvas.setTextColor(enabled ? accent : fg);
    canvas.drawString(enabled ? "Loop:ON" : "Loop:OFF", 6, 14);
    canvas.setTextColor(fg);

    char volbuf[32];
    std::snprintf(volbuf, sizeof(volbuf), "Vol:%u  Delay:%dms", static_cast<unsigned>(_volume.load()), _delay_ms.load());
    canvas.drawString(volbuf, 6, 28);

    canvas.drawString("Ent/Spc:Toggle  +/-:Vol", 6, 42);
    canvas.drawString("[ ]:Delay  Bksp:Exit", 6, 56);

    GetHAL().pushCanvas();
}
