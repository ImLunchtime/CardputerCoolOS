#pragma once
#include <cstdint>
#include <string>

enum class MusicPlayerState : uint8_t {
    Idle = 0,
    Playing = 1,
    Paused = 2,
};

class MusicPlayer {
public:
    static MusicPlayer& instance();

    bool init();
    bool playFile(const std::string& path);
    void togglePause();
    void stop();
    void seekBySeconds(int delta_seconds);

    MusicPlayerState state() const;
    bool consumeDirty();

private:
    MusicPlayer() = default;
};
