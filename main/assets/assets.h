/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <cstddef>

extern "C" {
extern const std::uint8_t _binary_sdcard_png_start[];
extern const std::uint8_t _binary_sdcard_png_end[];
}

inline const std::uint8_t* assets_sdcard_png_data()
{
    return _binary_sdcard_png_start;
}

inline std::size_t assets_sdcard_png_size()
{
    return static_cast<std::size_t>(_binary_sdcard_png_end - _binary_sdcard_png_start);
}
