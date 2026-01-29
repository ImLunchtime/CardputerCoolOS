/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <vector>

namespace audio {

void play_tone(int frequency, double durationSec = 0.02);

void play_melody(const std::vector<int>& midiList, double durationSec = 0.02);

void play_tone_from_midi(int midi, double durationSec = 0.02);

void play_random_tone(int semitoneShift = 48, double durationSec = 0.02);

void set_keyboard_sfx_enable(bool enable);

}  // namespace audio