/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <hal.h>
#include <assets.h>
#include <mooncake.h>
#include <memory>
#include <cstdio>
#include <apps/app_desktop/desktop_app.h>
#include <apps/app_music/music_app.h>
#include <apps/app_pictures/pictures_app.h>

class StatusBarService {
public:
    void update()
    {
        auto now = GetHAL().millis();
        if (now - _last_tick < 1000) {
            return;
        }
        _last_tick = now;
        draw();
    }

private:
    uint32_t _last_tick = 0;

    void draw()
    {
        auto& bar = GetHAL().canvasSystemBar;
        bar.fillScreen(TFT_BLACK);
        bar.setFont(&fonts::efontCN_12);
        bar.setTextColor(TFT_WHITE);
        bar.setTextSize(1);
        bar.setTextDatum(textdatum_t::middle_left);

        const uint8_t level = GetHAL().getBatLevel();
        const int x         = 4;
        const int y         = 4;
        const int w         = 22;
        const int h         = 12;
        const int tip_w     = 3;
        const int tip_h     = 6;
        const int tip_y     = y + (h - tip_h) / 2;
        const int padding   = 2;

        bar.drawRect(x, y, w, h, TFT_WHITE);
        bar.fillRect(x + w, tip_y, tip_w, tip_h, TFT_WHITE);

        int inner_w = w - padding * 2;
        int fill_w  = (inner_w * level) / 100;
        if (fill_w < 0) {
            fill_w = 0;
        }
        if (fill_w > inner_w) {
            fill_w = inner_w;
        }
        if (fill_w > 0) {
            bar.fillRect(x + padding, y + padding, fill_w, h - padding * 2, TFT_GREEN);
        }

        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%u%%", level);
        bar.drawString(buffer, x + w + tip_w + 6, bar.height() / 2);

        if (GetHAL().isSdCardMounted()) {
            constexpr int icon_w = 16;
            constexpr int icon_h = 16;
            const int icon_x     = bar.width() - icon_w - 2;
            const int icon_y     = (bar.height() - icon_h) / 2;
            bar.drawPng(assets_sdcard_png_data(), assets_sdcard_png_size(), icon_x, icon_y);
        }

        GetHAL().pushStatusBar();
    }
};

class AppSystem {
public:
    AppSystem() : _mooncake(mooncake::GetMooncake()) {}

    void init()
    {
        _desktop_app_id = _mooncake.installApp(std::make_unique<DesktopApp>());
        _music_app_id = _mooncake.installApp(std::make_unique<MusicApp>());
        _pictures_app_id = _mooncake.installApp(std::make_unique<PicturesApp>());
        _mooncake.openApp(_desktop_app_id);
    }

    void update()
    {
        _mooncake.update();
    }

private:
    mooncake::Mooncake& _mooncake;
    int _desktop_app_id = -1;
    int _music_app_id = -1;
    int _pictures_app_id = -1;
};

static AppSystem g_app_system;
static StatusBarService g_status_bar;

extern "C" void app_main(void)
{
    GetHAL().init();

    GetHAL().display.setBrightness(128);
    g_app_system.init();

    while (1) {
        GetHAL().feedTheDog();
        GetHAL().update();
        g_status_bar.update();
        g_app_system.update();
    }
}
