#pragma once
#include <mooncake.h>
#include <atomic>
#include <cstdint>

class AudioLoopbackApp : public mooncake::AppAbility {
public:
    AudioLoopbackApp();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static void loopbackTaskMain(void* arg);

    bool initLoopbackEngine();
    void deinitLoopbackEngine();

    void draw();
    void hookKeyboard();
    void unhookKeyboard();
    void openDesktopAndCloseSelf();

    void startLoopbackTask();
    void stopLoopbackTask();

    size_t _keyboard_slot_id = 0;
    bool _needs_redraw       = true;

    std::atomic<bool> _loopback_enabled{false};
    std::atomic<uint8_t> _volume{0};

    uint8_t _prev_volume = 0;
    bool _prev_volume_valid = false;

    void* _task_handle = nullptr;
    std::atomic<bool> _task_running{false};

    void* _i2s_tx_handle = nullptr;
    void* _i2s_rx_handle = nullptr;
};
