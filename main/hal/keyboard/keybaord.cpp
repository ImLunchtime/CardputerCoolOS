/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "keyboard.h"
#include "../hal_config.h"
#include <mooncake_log.h>

static const std::string _tag = "Keyboard";

static volatile bool _isr_flag = false;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    _isr_flag = true;
}

bool Keyboard::init()
{
    mclog::tagInfo(_tag, "init");

    _tca8418 = new Adafruit_TCA8418();
    auto ret = _tca8418->begin();
    if (!ret) {
        mclog::tagError(_tag, "init tca8418 failed");
        return false;
    }

    _tca8418->matrix(7, 8);
    _tca8418->flush();

    // Attach interrupt
    gpio_config_t io_conf;
    io_conf.intr_type    = GPIO_INTR_ANYEDGE;
    io_conf.mode         = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << HAL_PIN_KEYBOARD_INT);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // gpio_install_isr_service(0);
    gpio_install_isr_service((int)ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(static_cast<gpio_num_t>(HAL_PIN_KEYBOARD_INT), gpio_isr_handler, NULL);

    _tca8418->enableInterrupts();

    return true;
}

void Keyboard::update()
{
    clearKeyEvent();

    if (!_isr_flag) {
        return;
    }

    _key_event_raw_buffer = get_key_event_raw(_tca8418->getEvent());

    //  try to clear the IRQ flag
    //  if there are pending events it is not cleared
    _tca8418->writeRegister8(TCA8418_REG_INT_STAT, 1);
    int intstat = _tca8418->readRegister8(TCA8418_REG_INT_STAT);
    if ((intstat & 0x01) == 0) {
        _isr_flag = false;
    }

    remap(_key_event_raw_buffer);
    // mclog::tagDebug(_tag, "key event raw: ({}, {}): {}", _key_event_raw_buffer.row, _key_event_raw_buffer.col,
    //                 _key_event_raw_buffer.state);
    onKeyEventRaw.emit(_key_event_raw_buffer);

    update_modifier_mask(_key_event_raw_buffer);
    _key_event_buffer = convertToKeyEvent(_key_event_raw_buffer);
    // mclog::tagDebug(_tag, "key event: ({}) {} {}", (int)_key_event_buffer.code, _key_event_buffer.name,
    //                 _key_event_buffer.state);
    onKeyEvent.emit(_key_event_buffer);
}

Keyboard::KeyEventRaw_t Keyboard::get_key_event_raw(const uint8_t& eventRaw)
{
    KeyEventRaw_t ret;
    ret.state       = eventRaw & 0x80;
    uint16_t buffer = eventRaw;
    buffer &= 0x7F;
    buffer--;
    ret.row = buffer / 10;
    ret.col = buffer % 10;
    return ret;
}

// Remap to the same as cardputer
void Keyboard::remap(KeyEventRaw_t& key)
{
    // Col
    uint8_t col = 0;
    col         = key.row * 2;
    if (key.col > 3) col++;

    // Row
    uint8_t row = 0;
    row         = (key.col + 4) % 4;

    key.row = row;
    key.col = col;
}

void Keyboard::update_modifier_mask(const KeyEventRaw_t& key)
{
    // Check control key (3, 0)
    if (key.row == 3 && key.col == 0) {
        if (key.state) {
            _modifier_mask |= KEY_MOD_LCTRL;
        } else {
            _modifier_mask &= ~KEY_MOD_LCTRL;
        }
    }

    // Check shift key (2, 0)
    if (key.row == 2 && key.col == 0) {
        if (key.state) {
            _modifier_mask |= KEY_MOD_LSHIFT;
        } else {
            _modifier_mask &= ~KEY_MOD_LSHIFT;
        }
    }

    // Check capslock key (2, 1)
    if (key.row == 2 && key.col == 1) {
        _capslock_state = key.state;
    }
}

struct KeyValue_t {
    const char* firstName;
    const KeScanCode_t firstKeyCode;
    const char* secondName;
    const KeScanCode_t secondKeyCode;
};

const KeyValue_t _key_value_map[4][14] = {{{"`", KEY_GRAVE, "~", KEY_GRAVE},
                                           {"1", KEY_1, "!", KEY_1},
                                           {"2", KEY_2, "@", KEY_2},
                                           {"3", KEY_3, "#", KEY_3},
                                           {"4", KEY_4, "$", KEY_4},
                                           {"5", KEY_5, "%", KEY_5},
                                           {"6", KEY_6, "^", KEY_6},
                                           {"7", KEY_7, "&", KEY_7},
                                           {"8", KEY_8, "*", KEY_8},
                                           {"9", KEY_9, "(", KEY_9},
                                           {"0", KEY_0, ")", KEY_0},
                                           {"-", KEY_MINUS, "_", KEY_MINUS},
                                           {"=", KEY_EQUAL, "+", KEY_EQUAL},
                                           {"del", KEY_BACKSPACE, "del", KEY_BACKSPACE}},
                                          {{"tab", KEY_TAB, "tab", KEY_TAB},
                                           {"q", KEY_Q, "Q", KEY_Q},
                                           {"w", KEY_W, "W", KEY_W},
                                           {"e", KEY_E, "E", KEY_E},
                                           {"r", KEY_R, "R", KEY_R},
                                           {"t", KEY_T, "T", KEY_T},
                                           {"y", KEY_Y, "Y", KEY_Y},
                                           {"u", KEY_U, "U", KEY_U},
                                           {"i", KEY_I, "I", KEY_I},
                                           {"o", KEY_O, "O", KEY_O},
                                           {"p", KEY_P, "P", KEY_P},
                                           {"[", KEY_LEFTBRACE, "{", KEY_LEFTBRACE},
                                           {"]", KEY_RIGHTBRACE, "}", KEY_RIGHTBRACE},
                                           {"\\", KEY_BACKSLASH, "|", KEY_BACKSLASH}},
                                          {{"shift", KEY_LEFTSHIFT, "shift", KEY_LEFTSHIFT},
                                           {"capslock", KEY_CAPSLOCK, "capslock", KEY_CAPSLOCK},
                                           {"a", KEY_A, "A", KEY_A},
                                           {"s", KEY_S, "S", KEY_S},
                                           {"d", KEY_D, "D", KEY_D},
                                           {"f", KEY_F, "F", KEY_F},
                                           {"g", KEY_G, "G", KEY_G},
                                           {"h", KEY_H, "H", KEY_H},
                                           {"j", KEY_J, "J", KEY_J},
                                           {"k", KEY_K, "K", KEY_K},
                                           {"l", KEY_L, "L", KEY_L},
                                           {";", KEY_SEMICOLON, ":", KEY_SEMICOLON},
                                           {"'", KEY_APOSTROPHE, "\"", KEY_APOSTROPHE},
                                           {"enter", KEY_ENTER, "enter", KEY_ENTER}},
                                          {{"ctrl", KEY_LEFTCTRL, "ctrl", KEY_LEFTCTRL},
                                           {"opt", KEY_LEFTMETA, "opt", KEY_LEFTMETA},
                                           {"alt", KEY_LEFTALT, "alt", KEY_LEFTALT},
                                           {"z", KEY_Z, "Z", KEY_Z},
                                           {"x", KEY_X, "X", KEY_X},
                                           {"c", KEY_C, "C", KEY_C},
                                           {"v", KEY_V, "V", KEY_V},
                                           {"b", KEY_B, "B", KEY_B},
                                           {"n", KEY_N, "N", KEY_N},
                                           {"m", KEY_M, "M", KEY_M},
                                           {",", KEY_COMMA, "<", KEY_COMMA},
                                           {".", KEY_DOT, ">", KEY_DOT},
                                           {"/", KEY_SLASH, "?", KEY_SLASH},
                                           {" ", KEY_SPACE, " ", KEY_SPACE}}};

Keyboard::KeyEvent_t Keyboard::convertToKeyEvent(const KeyEventRaw_t& key)
{
    KeyEvent_t ret;

    ret.state = key.state;

    // For letters: use shift/caps lock to determine case
    // For special characters: use shift to determine which symbol
    bool use_shifted_version = false;

    // Check if this is a letter key (a-z)
    KeScanCode_t baseKeyCode = _key_value_map[key.row][key.col].firstKeyCode;
    bool isLetter            = (baseKeyCode >= KEY_A && baseKeyCode <= KEY_Z);

    if (isLetter) {
        // For letters, use shift OR caps lock
        use_shifted_version = (_modifier_mask & KEY_MOD_LSHIFT) || _capslock_state || _is_capslock_locked;
    } else {
        // For non-letters (numbers, symbols), only use shift
        use_shifted_version = (_modifier_mask & KEY_MOD_LSHIFT);
    }

    // mclog::tagDebug(_tag, "modifier mask: {:08b} {}", _modifier_mask, use_shifted_version);

    if (use_shifted_version) {
        ret.keyCode = _key_value_map[key.row][key.col].secondKeyCode;
        ret.keyName = _key_value_map[key.row][key.col].secondName;
    } else {
        ret.keyCode = _key_value_map[key.row][key.col].firstKeyCode;
        ret.keyName = _key_value_map[key.row][key.col].firstName;
    }

    if (ret.keyCode == KEY_LEFTSHIFT || ret.keyCode == KEY_LEFTCTRL || ret.keyCode == KEY_CAPSLOCK) {
        ret.isModifier = true;
    } else {
        ret.isModifier = false;
    }

    return ret;
}

void Keyboard::clearKeyEvent()
{
    _key_event_raw_buffer.state = false;
    _key_event_raw_buffer.row   = 233;
    _key_event_raw_buffer.col   = 233;
    _key_event_buffer.keyCode   = KEY_NONE;
}
