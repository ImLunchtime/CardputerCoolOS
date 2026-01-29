/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <random>

namespace audio {

static std::vector<int> c_major_scale = {60, 62, 64, 65, 67, 69, 71};  // C major scale (C D E F G A B)

void play_tone(int frequency, double durationSec)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    const int sample_rate = GetHAL().speaker.config().sample_rate;
    const int samples     = static_cast<int>(sample_rate * durationSec);
    std::vector<int16_t> buffer(samples * 2);  // stereo

    const int fade_len    = 200;  // fade-out length (samples)
    const float amplitude = 32767.0f / 5;

    for (int i = 0; i < samples; ++i) {
        float amp = amplitude;

        // Apply fade-out at the end
        if (i >= samples - fade_len) {
            float fade_factor = static_cast<float>(samples - i) / fade_len;
            amp *= fade_factor;
        }

        int16_t value     = static_cast<int16_t>(amp * sin(2.0 * M_PI * frequency * i / sample_rate));
        buffer[i * 2]     = value;  // left channel
        buffer[i * 2 + 1] = value;  // right channel
    }

    GetHAL().speaker.playRaw(buffer.data(), buffer.size());
}

void play_melody(const std::vector<int>& midiList, double durationSec = 0.1)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    const int sample_rate      = GetHAL().speaker.config().sample_rate;
    const int samples_per_note = static_cast<int>(sample_rate * durationSec);
    const int fade_len         = 200;  // fade-out length per note (samples)
    const float amplitude      = 32767.0f / 5;

    std::vector<int16_t> buffer;                             // store the whole melody
    buffer.reserve(midiList.size() * samples_per_note * 2);  // stereo capacity

    for (int midiNote : midiList) {
        for (int i = 0; i < samples_per_note; ++i) {
            float amp = amplitude;

            // Apply fade-out at the end of each note
            if (i >= samples_per_note - fade_len) {
                float fade_factor = static_cast<float>(samples_per_note - i) / fade_len;
                amp *= fade_factor;
            }

            int16_t sample = 0;
            if (midiNote >= 0) {
                double freq = 440.0 * pow(2.0, (midiNote - 69) / 12.0);
                sample      = static_cast<int16_t>(amp * sin(2.0 * M_PI * freq * i / sample_rate));
            }

            buffer.push_back(sample);  // left channel
            buffer.push_back(sample);  // right channel
        }
    }

    GetHAL().speaker.playRaw(buffer.data(), buffer.size());
}

void play_tone_from_midi(int midi, double durationSec)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    double freq = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    play_tone(static_cast<int>(freq), durationSec);
}

void play_random_tone(int semitoneShift = 0, double durationSec = 0.15)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(c_major_scale.size()) - 1);

    int index = dist(gen);
    int midi  = c_major_scale[index] + semitoneShift;

    play_tone_from_midi(midi, durationSec);
}

/* -------------------------------------------------------------------------- */
/*                                  Keyboard                                  */
/* -------------------------------------------------------------------------- */
static void _keyboard_sfx_on_key_event(const Keyboard::KeyEvent_t& event)
{
    if (!event.state) {
        return;
    }

    GetHAL().speaker.setVolume(90);

    int semitoneShift = 48;
    switch (event.keyCode) {
        case KEY_1:
            play_tone_from_midi(c_major_scale[0] + semitoneShift, 0.02);
            return;
        case KEY_2:
            play_tone_from_midi(c_major_scale[1] + semitoneShift, 0.02);
            return;
        case KEY_3:
            play_tone_from_midi(c_major_scale[2] + semitoneShift, 0.02);
            return;
        case KEY_4:
            play_tone_from_midi(c_major_scale[3] + semitoneShift, 0.02);
            return;
        case KEY_5:
            play_tone_from_midi(c_major_scale[4] + semitoneShift, 0.02);
            return;
        case KEY_6:
            play_tone_from_midi(c_major_scale[5] + semitoneShift, 0.02);
            return;
        case KEY_7:
            play_tone_from_midi(c_major_scale[6] + semitoneShift, 0.02);
            return;
        default:
            play_random_tone(semitoneShift, 0.02);
    }
}

void set_keyboard_sfx_enable(bool enable)
{
    static size_t slot_id  = 0;
    static bool is_enabled = false;

    mclog::tagInfo("audio", "set keyboard sfx enable: {}", enable);
    if (enable) {
        if (is_enabled) {
            return;
        }
        is_enabled = true;
        slot_id    = GetHAL().keyboard.onKeyEvent.connect(_keyboard_sfx_on_key_event);
    } else {
        if (!is_enabled) {
            return;
        }
        is_enabled = false;
        GetHAL().keyboard.onKeyEvent.disconnect(slot_id);
    }
}

}  // namespace audio
