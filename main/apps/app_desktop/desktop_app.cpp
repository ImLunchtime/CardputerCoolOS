#include "desktop_app.h"
#include <hal.h>

DesktopApp::DesktopApp()
{
    setAppInfo().name = "Desktop";
}

void DesktopApp::onOpen()
{
    refreshAppList();
    hookKeyboard();
    draw();
}

void DesktopApp::onRunning()
{
    _list.update(GetHAL().millis());
    if (_list.isAnimating()) {
        draw();
    }
}

void DesktopApp::onClose()
{
    unhookKeyboard();
}

void DesktopApp::refreshAppList()
{
    _apps.clear();

    auto& mc = mooncake::GetMooncake();
    auto* app_mgr = mc.getAppAbilityManager();
    const auto app_instances = app_mgr ? app_mgr->getAllAbilityInstance() : std::vector<mooncake::AbilityBase*>{};

    _apps.reserve(app_instances.size());
    for (auto* app : app_instances) {
        if (app == nullptr) {
            continue;
        }
        const int id = app->getId();
        if (id == getId()) {
            continue;
        }
        auto info = mc.getAppInfo(id);
        if (info.name.empty()) {
            continue;
        }
        _apps.emplace_back();
        _apps.back().id = id;
        _apps.back().name = info.name;
    }

    if (_apps.empty()) {
        _list.jumpTo(0, 0, 1);
        return;
    }

    auto& canvas = GetHAL().canvas;
    canvas.setFont(&fonts::efontCN_12);
    const int list_h = 108;
    const int row_h = SimpleList::rowHeight(canvas);
    const int visible_rows = SimpleList::visibleRows(list_h, row_h);

    int idx = _list.getSelectedIndex();
    if (idx >= static_cast<int>(_apps.size())) {
        idx = static_cast<int>(_apps.size()) - 1;
    }
    _list.jumpTo(idx, static_cast<int>(_apps.size()), visible_rows);
}

void DesktopApp::hookKeyboard()
{
    if (_keyboard_slot_id != 0) {
        return;
    }

    _keyboard_slot_id = GetHAL().keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& e) {
        if (!e.state) {
            return;
        }

        const int list_h      = 108;
        auto& canvas          = GetHAL().canvas;
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextSize(1);
        const int row_h = SimpleList::rowHeight(canvas);
        const int visible_row = SimpleList::visibleRows(list_h, row_h);

        const auto is_up = [](KeScanCode_t code) {
            return code == KEY_UP || code == KEY_W || code == KEY_K || code == KEY_SEMICOLON;
        };
        const auto is_down = [](KeScanCode_t code) {
            return code == KEY_DOWN || code == KEY_S || code == KEY_J || code == KEY_DOT;
        };

        if (is_up(e.keyCode)) {
            _list.go(_list.getSelectedIndex() - 1, static_cast<int>(_apps.size()), visible_row);
            draw();
            return;
        }

        if (is_down(e.keyCode)) {
            _list.go(_list.getSelectedIndex() + 1, static_cast<int>(_apps.size()), visible_row);
            draw();
            return;
        }

        if (e.keyCode == KEY_ENTER) {
            if (_apps.empty()) {
                return;
            }
            const int target_id = _apps[_list.getSelectedIndex()].id;
            auto& mc            = mooncake::GetMooncake();
            mc.openApp(target_id);
            mc.closeApp(getId());
            return;
        }
    });
}

void DesktopApp::unhookKeyboard()
{
    if (_keyboard_slot_id == 0) {
        return;
    }
    GetHAL().keyboard.onKeyEvent.disconnect(_keyboard_slot_id);
    _keyboard_slot_id = 0;
}

void DesktopApp::draw()
{
    auto& canvas = GetHAL().canvas;
    const uint16_t bg_color = lgfx::color565(0x33, 0x33, 0x33);
    const uint16_t container_2_color = lgfx::color565(0xFF, 0x8D, 0x1A);
    const uint16_t container_3_color = lgfx::color565(0x61, 0x61, 0x61);
    const uint16_t selected_color = lgfx::color565(0xEE, 0xEE, 0xEE);

    canvas.fillScreen(bg_color);

    canvas.fillRoundRect(165, 3, 69, 69, 7, container_2_color);
    canvas.fillRoundRect(165, 75, 69, 36, 7, container_3_color);

    const int list_x = 3;
    const int list_y = 3;
    const int list_w = 159;
    const int list_h = 108;

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::middle_left);

    SimpleListStyle style;
    style.bg_color = bg_color;
    style.text_color = TFT_WHITE;
    style.selected_bg_color = selected_color;
    style.selected_text_color = TFT_BLACK;
    style.padding_x = 2;

    _list.draw(
        canvas,
        list_x,
        list_y,
        list_w,
        list_h,
        static_cast<int>(_apps.size()),
        [this](int idx) { return _apps[idx].name; },
        style);

    GetHAL().pushAppCanvas();
}
