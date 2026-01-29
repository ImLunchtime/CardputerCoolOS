#include "desktop_app.h"
#include <hal.h>
#include "utils/ui/simple_list.h"

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
}

void DesktopApp::onClose()
{
    unhookKeyboard();
}

void DesktopApp::refreshAppList()
{
    _apps.clear();

    auto& mc = mooncake::GetMooncake();
    auto ids = mc.getAllAppIDs();
    _apps.reserve(ids.size());
    for (const auto id : ids) {
        if (id == getId()) {
            continue;
        }
        auto info = mc.getAppInfo(id);
        if (info.name.empty()) {
            continue;
        }
        _apps.push_back({id, info.name});
    }

    if (_apps.empty()) {
        _selected_index = 0;
        _scroll_offset  = 0;
        return;
    }

    if (_selected_index < 0) {
        _selected_index = 0;
    }
    if (_selected_index >= static_cast<int>(_apps.size())) {
        _selected_index = static_cast<int>(_apps.size()) - 1;
    }
    if (_scroll_offset < 0) {
        _scroll_offset = 0;
    }
    if (_scroll_offset > _selected_index) {
        _scroll_offset = _selected_index;
    }
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
            SimpleListState s{_selected_index, _scroll_offset};
            SimpleList::move(s, -1, static_cast<int>(_apps.size()), visible_row);
            _selected_index = s.selected_index;
            _scroll_offset = s.scroll_offset;
            draw();
            return;
        }

        if (is_down(e.keyCode)) {
            SimpleListState s{_selected_index, _scroll_offset};
            SimpleList::move(s, 1, static_cast<int>(_apps.size()), visible_row);
            _selected_index = s.selected_index;
            _scroll_offset = s.scroll_offset;
            draw();
            return;
        }

        if (e.keyCode == KEY_ENTER) {
            if (_apps.empty()) {
                return;
            }
            const int target_id = _apps[_selected_index].id;
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

    SimpleListState s{_selected_index, _scroll_offset};
    SimpleList::clamp(s, static_cast<int>(_apps.size()));
    _selected_index = s.selected_index;
    _scroll_offset = s.scroll_offset;

    SimpleListStyle style;
    style.bg_color = bg_color;
    style.text_color = TFT_WHITE;
    style.selected_bg_color = selected_color;
    style.selected_text_color = TFT_BLACK;
    style.padding_x = 2;

    SimpleList::draw(
        canvas,
        list_x,
        list_y,
        list_w,
        list_h,
        s,
        static_cast<int>(_apps.size()),
        [this](int idx) { return _apps[idx].name; },
        style);

    GetHAL().pushAppCanvas();
}
