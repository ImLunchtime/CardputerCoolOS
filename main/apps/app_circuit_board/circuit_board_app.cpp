#include "circuit_board_app.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <cJSON.h>

extern "C" {
    extern const uint8_t _binary_controls_png_start[];
    extern const uint8_t _binary_controls_png_end[];
    extern const uint8_t _binary_blueprint_png_start[];
    extern const uint8_t _binary_blueprint_png_end[];
    
    // Components
    extern const uint8_t _binary_button_png_start[];
    extern const uint8_t _binary_button_png_end[];
    extern const uint8_t _binary_current_gauge_png_start[];
    extern const uint8_t _binary_current_gauge_png_end[];
    extern const uint8_t _binary_voltage_gauge_png_start[];
    extern const uint8_t _binary_voltage_gauge_png_end[];
    extern const uint8_t _binary_switch_off_png_start[];
    extern const uint8_t _binary_switch_off_png_end[];
}

CircuitBoardApp::CircuitBoardApp() {
    setAppInfo().name = "Circuit Board";
    initComponentTypes();
}

void CircuitBoardApp::initComponentTypes() {
    _component_types.clear();
    
    _component_types.push_back({
        "Button",
        _binary_button_png_start,
        _binary_button_png_end,
        16, 16,
        2, 2
    });
    
    _component_types.push_back({
        "Current Meter",
        _binary_current_gauge_png_start,
        _binary_current_gauge_png_end,
        24, 24,
        3, 3
    });
    
    _component_types.push_back({
        "Voltage Meter",
        _binary_voltage_gauge_png_start,
        _binary_voltage_gauge_png_end,
        24, 24,
        3, 3
    });
    
    _component_types.push_back({
        "Switch",
        _binary_switch_off_png_start,
        _binary_switch_off_png_end,
        16, 24,
        2, 3
    });
}

void CircuitBoardApp::onOpen() {
    _cursor_x = 0;
    _cursor_y = 0;
    _cursor_mode = Mode::Component;
    _is_menu_open = false;
    _menu_selection = 0;
    _is_saving = false;
    _is_loading = false;
    _current_filename.clear();
    _save_filename_input.clear();
    
    auto& keyboard = GetHAL().keyboard;
    
    // Hook keyboard
    if (_keyboard_slot_id == 0) {
        _keyboard_slot_id = keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& e) {
            if (!e.state) return;
            
            // Saving dialog
            if (_is_saving) {
                if (e.keyCode == KEY_ESC || e.keyCode == KEY_GRAVE) {
                    closeSaveDialog();
                    return;
                }
                if (e.keyCode == KEY_ENTER) {
                    if (!_save_filename_input.empty()) {
                        saveToFile(_save_filename_input);
                        closeSaveDialog();
                    }
                    return;
                }
                if (e.keyCode == KEY_BACKSPACE) {
                    if (!_save_filename_input.empty()) {
                        _save_filename_input.pop_back();
                        draw();
                    }
                    return;
                }
                // Simple alphanumeric input handling
                // Map keycodes to chars (simplified)
                char c = 0;
                if (e.keyCode >= KEY_A && e.keyCode <= KEY_Z) {
                    c = 'a' + (e.keyCode - KEY_A);
                    if (e.isModifier) c = 'A' + (e.keyCode - KEY_A); // Assuming modifier is shift
                } else if (e.keyCode >= KEY_1 && e.keyCode <= KEY_9) {
                    c = '1' + (e.keyCode - KEY_1);
                } else if (e.keyCode == KEY_0) {
                    c = '0';
                } else if (e.keyCode == KEY_MINUS) {
                    c = '_';
                }
                
                if (c != 0) {
                    handleSaveInput(c);
                }
                return;
            }

            // Loading dialog
            if (_is_loading) {
                if (e.keyCode == KEY_ESC || e.keyCode == KEY_GRAVE) {
                    closeLoadDialog();
                    return;
                }
                if (e.keyCode == KEY_UP || e.keyCode == KEY_W || e.keyCode == KEY_SEMICOLON) {
                    moveLoadSelection(-1);
                } else if (e.keyCode == KEY_DOWN || e.keyCode == KEY_S || e.keyCode == KEY_DOT) {
                    moveLoadSelection(1);
                } else if (e.keyCode == KEY_ENTER || e.keyCode == KEY_SPACE) {
                    loadSelectedFile();
                }
                return;
            }
            
            if (_is_menu_open) {
                if (e.keyCode == KEY_ESC || e.keyCode == KEY_GRAVE) {
                    closeMenu();
                    return;
                }
                
                if (e.keyCode == KEY_COMMA || e.keyCode == KEY_LEFT || e.keyCode == KEY_A) {
                    moveMenuSelection(-1);
                } else if (e.keyCode == KEY_SLASH || e.keyCode == KEY_RIGHT || e.keyCode == KEY_D) {
                    moveMenuSelection(1);
                } else if (e.keyCode == KEY_ENTER || e.keyCode == KEY_SPACE) {
                    placeSelectedComponent();
                }
                return;
            }

            if (e.keyCode == KEY_ESC || e.keyCode == KEY_GRAVE) {
                goBackOrExit();
                return;
            }
            
            if (e.keyCode == KEY_SEMICOLON) {
                moveCursor(0, -1);
            } else if (e.keyCode == KEY_DOT) {
                moveCursor(0, 1);
            } else if (e.keyCode == KEY_COMMA) {
                moveCursor(-1, 0);
            } else if (e.keyCode == KEY_SLASH) {
                moveCursor(1, 0);
            } else if (e.keyCode == KEY_P) {
                _cursor_mode = Mode::Component;
                draw();
            } else if (e.keyCode == KEY_T) {
                _cursor_mode = Mode::Trace;
                draw();
            } else if (e.keyCode == KEY_D) {
                _cursor_mode = Mode::Remove;
                draw();
            } else if (e.keyCode == KEY_ENTER || e.keyCode == KEY_SPACE) {
                if (_cursor_mode == Mode::Component) {
                    openMenu();
                } else if (_cursor_mode == Mode::Remove) {
                    removeComponentAtCursor();
                }
            } else if (e.keyCode == KEY_S) {
                bool force_new = (GetHAL().keyboard.getModifierMask() & KEY_MOD_LALT) || (GetHAL().keyboard.getModifierMask() & KEY_MOD_RALT);
                openSaveDialog(force_new);
            } else if (e.keyCode == KEY_L) {
                openLoadDialog();
            }
        });
    }

    draw();
}

void CircuitBoardApp::onRunning() {
    if (GetHAL().homeButton.wasPressed()) {
        goBackOrExit();
    }
    
    // Refresh if message times out
    if (_message_timeout > 0 && GetHAL().millis() >= _message_timeout) {
        _message_timeout = 0;
        draw();
    }

    if (_is_loading) {
        _file_list.update(GetHAL().millis());
        draw();
    }
}

void CircuitBoardApp::onClose() {
    if (_keyboard_slot_id != 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_keyboard_slot_id);
        _keyboard_slot_id = 0;
    }
}

void CircuitBoardApp::draw() {
    auto& canvas = GetHAL().canvas;
    canvas.fillScreen(TFT_BLACK);

    // Draw controls.png at (0,0)
    // Note: Ensure the symbol name matches what CMake generates for controls.png
    canvas.drawPng(
        _binary_controls_png_start,
        _binary_controls_png_end - _binary_controls_png_start,
        0, 0
    );

    // Draw blueprint.png centered in remaining space
    // Left panel: 32px
    const int left_w = 32;
    const int blueprint_w = 100;
    const int blueprint_h = 100;
    
    // Remaining space width: canvas.width() - 32
    // Center X of remaining space relative to (0,0) = 32 + (remaining_w - 100) / 2
    int x = left_w + (canvas.width() - left_w - blueprint_w) / 2;
    int y = (canvas.height() - blueprint_h) / 2;

    canvas.drawPng(
        _binary_blueprint_png_start,
        _binary_blueprint_png_end - _binary_blueprint_png_start,
        x, y
    );

    // Draw placed components
    for (const auto& comp : _placed_components) {
        if (comp.type_index >= 0 && comp.type_index < static_cast<int>(_component_types.size())) {
            const auto& type = _component_types[comp.type_index];
            int comp_x = x + kGridOffsetX + comp.x * kGridSize;
            int comp_y = y + kGridOffsetY + comp.y * kGridSize;
            canvas.drawPng(type.png_start, type.png_end - type.png_start, comp_x, comp_y);
        }
    }

    // Draw cursor
    // Calculate cursor position relative to screen
    int cursor_screen_x = x + kGridOffsetX + _cursor_x * kGridSize;
    int cursor_screen_y = y + kGridOffsetY + _cursor_y * kGridSize;
    
    // Draw 2px white border
    // Outer rect
    canvas.drawRect(cursor_screen_x, cursor_screen_y, kGridSize, kGridSize, TFT_WHITE);
    // Inner rect
    canvas.drawRect(cursor_screen_x + 1, cursor_screen_y + 1, kGridSize - 2, kGridSize - 2, TFT_WHITE);

    // Draw status label
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextDatum(textdatum_t::bottom_right);

    std::string mode_str;
    switch (_cursor_mode) {
        case Mode::Component: mode_str = "Component"; break;
        case Mode::Trace: mode_str = "Trace"; break;
        case Mode::Remove: mode_str = "Remove"; break;
    }
    std::string status_text = "Mode: " + mode_str;
    canvas.drawString(status_text.c_str(), canvas.width() - 2, canvas.height() - 2);

    if (_is_menu_open) {
        drawMenu();
    }
    
    if (_is_saving) {
        drawSaveDialog();
    }
    
    if (_is_loading) {
        drawLoadDialog();
    }

    // Draw message
    if (GetHAL().millis() < _message_timeout) {
        canvas.setFont(&fonts::efontCN_12);
        canvas.setTextSize(1);
        canvas.setTextColor(_message_color, TFT_BLACK);
        canvas.setTextDatum(textdatum_t::bottom_center);
        canvas.drawString(_message_text.c_str(), canvas.width() / 2, canvas.height() - 2);
    }

    GetHAL().pushAppCanvas();
}

void CircuitBoardApp::goBackOrExit() {
    auto& mc = mooncake::GetMooncake();
    auto* app_mgr = mc.getAppAbilityManager();
    const auto app_instances = app_mgr ? app_mgr->getAllAbilityInstance() : std::vector<mooncake::AbilityBase*>{};
    for (auto* app : app_instances) {
        if (!app) continue;
        const int id = app->getId();
        const auto info = mc.getAppInfo(id);
        if (info.name == "Desktop") {
            mc.openApp(id);
            mc.closeApp(getId());
            return;
        }
    }
    mc.closeApp(getId());
}

void CircuitBoardApp::moveCursor(int dx, int dy) {
    int next_x = _cursor_x + dx;
    int next_y = _cursor_y + dy;

    if (next_x < 0) next_x = 0;
    if (next_x >= kGridCols) next_x = kGridCols - 1;
    
    if (next_y < 0) next_y = 0;
    if (next_y >= kGridRows) next_y = kGridRows - 1;

    if (next_x != _cursor_x || next_y != _cursor_y) {
        _cursor_x = next_x;
        _cursor_y = next_y;
        draw();
    }
}

void CircuitBoardApp::openMenu() {
    _is_menu_open = true;
    _menu_selection = 0;
    draw();
}

void CircuitBoardApp::closeMenu() {
    _is_menu_open = false;
    draw();
}

void CircuitBoardApp::moveMenuSelection(int delta) {
    if (_component_types.empty()) return;
    
    _menu_selection += delta;
    if (_menu_selection < 0) _menu_selection = _component_types.size() - 1;
    if (_menu_selection >= _component_types.size()) _menu_selection = 0;
    
    draw();
}

void CircuitBoardApp::drawMenu() {
    auto& canvas = GetHAL().canvas;
    int w = canvas.width();
    int h = canvas.height();
    
    // Draw semi-transparent background (simulate by darkening or just solid rect)
    // Here we use a solid rect for simplicity
    int menu_h = 40;
    int menu_y = (h - menu_h) / 2;
    canvas.fillRect(0, menu_y, w, menu_h, TFT_DARKGREY);
    canvas.drawRect(0, menu_y, w, menu_h, TFT_WHITE);
    
    // Draw components
    int start_x = 10;
    int item_spacing = 40;
    
    for (int i = 0; i < _component_types.size(); ++i) {
        const auto& type = _component_types[i];
        int item_x = start_x + i * item_spacing;
        int item_y = menu_y + (menu_h - type.height) / 2;
        
        // Draw selection box
        if (i == _menu_selection) {
            canvas.fillRect(item_x - 2, item_y - 2, type.width + 4, type.height + 4, TFT_YELLOW);
        }
        
        canvas.drawPng(type.png_start, type.png_end - type.png_start, item_x, item_y);
    }
}

bool CircuitBoardApp::checkOverlap(int x, int y, int w, int h, int exclude_index) {
    for (int i = 0; i < static_cast<int>(_placed_components.size()); ++i) {
        if (i == exclude_index) continue;
        
        const auto& placed = _placed_components[i];
        if (placed.type_index < 0 || placed.type_index >= static_cast<int>(_component_types.size())) continue;
        
        const auto& type = _component_types[placed.type_index];
        
        // Rect overlap check
        // Rect 1: x, y, w, h
        // Rect 2: placed.x, placed.y, type.grid_w, type.grid_h
        
        bool no_overlap = (x + w <= placed.x) ||
                          (placed.x + type.grid_w <= x) ||
                          (y + h <= placed.y) ||
                          (placed.y + type.grid_h <= y);
                          
        if (!no_overlap) return true;
    }
    return false;
}

void CircuitBoardApp::placeSelectedComponent() {
    if (_menu_selection < 0 || _menu_selection >= _component_types.size()) return;
    
    const auto& type = _component_types[_menu_selection];
    
    // Check bounds and adjust if necessary
    int x = _cursor_x;
    int y = _cursor_y;
    
    if (x + type.grid_w > kGridCols) {
        x = kGridCols - type.grid_w;
    }
    
    if (y + type.grid_h > kGridRows) {
        y = kGridRows - type.grid_h;
    }
    
    // Check overlap
    if (checkOverlap(x, y, type.grid_w, type.grid_h)) {
        showMessage("Cannot place: Overlap!", TFT_RED);
        closeMenu();
        return;
    }
    
    // Place component
    _placed_components.push_back({x, y, _menu_selection});
    
    closeMenu();
}

void CircuitBoardApp::removeComponentAtCursor() {
    for (auto it = _placed_components.begin(); it != _placed_components.end(); ++it) {
        const auto& placed = *it;
        if (placed.type_index < 0 || placed.type_index >= static_cast<int>(_component_types.size())) continue;
        const auto& type = _component_types[placed.type_index];
        
        // Check if cursor is inside this component
        // Component rect: placed.x, placed.y, type.grid_w, type.grid_h
        bool inside = (_cursor_x >= placed.x) && (_cursor_x < placed.x + type.grid_w) &&
                      (_cursor_y >= placed.y) && (_cursor_y < placed.y + type.grid_h);
                      
        if (inside) {
            _placed_components.erase(it);
            draw();
            return;
        }
    }
    
    showMessage("Nothing to remove", TFT_YELLOW);
    draw();
}

void CircuitBoardApp::showMessage(const std::string& text, uint16_t color) {
    _message_text = text;
    _message_color = color;
    _message_timeout = GetHAL().millis() + 2000;
    draw();
}

void CircuitBoardApp::openSaveDialog(bool force_new) {
    if (!GetHAL().isSdCardMounted()) {
        showMessage("SD Card not mounted!", TFT_RED);
        return;
    }

    if (!force_new && !_current_filename.empty()) {
        saveToFile(_current_filename);
        return;
    }

    _is_saving = true;
    _is_loading = false;
    _is_menu_open = false;
    _save_filename_input.clear();
    draw();
}

void CircuitBoardApp::closeSaveDialog() {
    _is_saving = false;
    draw();
}

void CircuitBoardApp::handleSaveInput(char c) {
    if (_save_filename_input.length() < 20) {
        _save_filename_input.push_back(c);
        draw();
    }
}

void CircuitBoardApp::saveToFile(const std::string& filename) {
    if (!GetHAL().isSdCardMounted()) {
        showMessage("SD Card not mounted!", TFT_RED);
        return;
    }

    std::string path = "/sdcard/" + filename;
    if (path.length() < 11 || path.substr(path.length() - 11) != ".coscircuit") {
        path += ".coscircuit";
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* components = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "components", components);

    for (const auto& comp : _placed_components) {
        cJSON* item = cJSON_CreateObject();
        if (comp.type_index >= 0 && comp.type_index < _component_types.size()) {
            cJSON_AddStringToObject(item, "type", _component_types[comp.type_index].name);
        } else {
            cJSON_AddStringToObject(item, "type", "Unknown");
        }
        cJSON_AddNumberToObject(item, "x", comp.x);
        cJSON_AddNumberToObject(item, "y", comp.y);
        cJSON_AddItemToArray(components, item);
    }

    char* json_str = cJSON_Print(root);
    std::ofstream ofs(path);
    if (ofs.is_open()) {
        ofs << json_str;
        ofs.close();
        _current_filename = filename;
        if (_current_filename.length() < 11 || _current_filename.substr(_current_filename.length() - 11) != ".coscircuit") {
             // Keep filename without extension or with extension? Usually user inputs name without extension.
             // Let's store what we used to save.
             // Actually, let's just store the basename.
        }
        // Normalize _current_filename to be the one we can reuse
        // If user typed "foo", we saved to "/sdcard/foo.coscircuit".
        // Next time we want to save to "foo.coscircuit" or just reuse the path.
        // Let's store the full filename (basename + ext)
        size_t last_slash = path.find_last_of('/');
        if (last_slash != std::string::npos) {
            _current_filename = path.substr(last_slash + 1);
        } else {
            _current_filename = path;
        }

        showMessage("Saved: " + _current_filename, TFT_GREEN);
    } else {
        showMessage("Save Failed!", TFT_RED);
    }

    free(json_str);
    cJSON_Delete(root);
}

void CircuitBoardApp::openLoadDialog() {
    if (!GetHAL().isSdCardMounted()) {
        showMessage("SD Card not mounted!", TFT_RED);
        return;
    }

    _is_loading = true;
    _is_saving = false;
    _is_menu_open = false;
    refreshFileList();
    draw();
}

void CircuitBoardApp::closeLoadDialog() {
    _is_loading = false;
    draw();
}

void CircuitBoardApp::refreshFileList() {
    _file_list_entries.clear();
    DIR* dir = opendir("/sdcard");
    if (!dir) return;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        std::string name = ent->d_name;
        if (name.length() > 11 && name.substr(name.length() - 11) == ".coscircuit") {
            _file_list_entries.push_back({name, "/sdcard/" + name});
        }
    }
    closedir(dir);
    
    // Reset list
    _file_list.jumpTo(0, _file_list_entries.size(), 5); // 5 visible rows
}

void CircuitBoardApp::loadSelectedFile() {
    int idx = _file_list.getSelectedIndex();
    if (idx >= 0 && idx < _file_list_entries.size()) {
        loadFromFile(_file_list_entries[idx].path);
        closeLoadDialog();
    }
}

void CircuitBoardApp::loadFromFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        showMessage("Open Failed!", TFT_RED);
        return;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string json_str = buffer.str();

    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) {
        showMessage("Parse Failed!", TFT_RED);
        return;
    }

    cJSON* components = cJSON_GetObjectItem(root, "components");
    if (components && cJSON_IsArray(components)) {
        _placed_components.clear();
        int count = cJSON_GetArraySize(components);
        for (int i = 0; i < count; ++i) {
            cJSON* item = cJSON_GetArrayItem(components, i);
            cJSON* type_item = cJSON_GetObjectItem(item, "type");
            cJSON* x_item = cJSON_GetObjectItem(item, "x");
            cJSON* y_item = cJSON_GetObjectItem(item, "y");

            if (type_item && x_item && y_item) {
                std::string type_name = type_item->valuestring;
                int type_idx = -1;
                for (int k = 0; k < _component_types.size(); ++k) {
                    if (_component_types[k].name == type_name) {
                        type_idx = k;
                        break;
                    }
                }

                if (type_idx >= 0) {
                    _placed_components.push_back({x_item->valueint, y_item->valueint, type_idx});
                }
            }
        }
        
        size_t last_slash = path.find_last_of('/');
        if (last_slash != std::string::npos) {
            _current_filename = path.substr(last_slash + 1);
        } else {
            _current_filename = path;
        }
        showMessage("Loaded: " + _current_filename, TFT_GREEN);
    } else {
        showMessage("Invalid Format!", TFT_RED);
    }

    cJSON_Delete(root);
}

void CircuitBoardApp::moveLoadSelection(int delta) {
    if (_file_list_entries.empty()) return;
    int idx = _file_list.getSelectedIndex();
    _file_list.go(idx + delta, _file_list_entries.size(), 5); // 5 visible rows
    draw();
}

void CircuitBoardApp::drawSaveDialog() {
    auto& canvas = GetHAL().canvas;
    int w = canvas.width();
    int h = canvas.height();
    
    int dlg_w = 200;
    int dlg_h = 60;
    int dlg_x = (w - dlg_w) / 2;
    int dlg_y = (h - dlg_h) / 2;
    
    canvas.fillRect(dlg_x, dlg_y, dlg_w, dlg_h, TFT_DARKGREY);
    canvas.drawRect(dlg_x, dlg_y, dlg_w, dlg_h, TFT_WHITE);
    
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_DARKGREY);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.drawString("Save As:", dlg_x + dlg_w / 2, dlg_y + 5);
    
    canvas.setTextDatum(textdatum_t::middle_center);
    std::string display_name = _save_filename_input + "_";
    canvas.drawString(display_name.c_str(), dlg_x + dlg_w / 2, dlg_y + 35);
}

void CircuitBoardApp::drawLoadDialog() {
    auto& canvas = GetHAL().canvas;
    int w = canvas.width();
    int h = canvas.height();
    
    int dlg_w = 220;
    int dlg_h = 100;
    int dlg_x = (w - dlg_w) / 2;
    int dlg_y = (h - dlg_h) / 2;
    
    canvas.fillRect(dlg_x, dlg_y, dlg_w, dlg_h, TFT_DARKGREY);
    canvas.drawRect(dlg_x, dlg_y, dlg_w, dlg_h, TFT_WHITE);
    
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_DARKGREY);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.drawString("Load Circuit:", dlg_x + dlg_w / 2, dlg_y + 5);
    
    if (_file_list_entries.empty()) {
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("(No .coscircuit files)", dlg_x + dlg_w / 2, dlg_y + dlg_h / 2);
    } else {
        SimpleListStyle style;
        style.bg_color = TFT_DARKGREY;
        style.text_color = TFT_WHITE;
        style.selected_bg_color = TFT_YELLOW;
        style.selected_text_color = TFT_BLACK;
        style.padding_x = 2;
        
        _file_list.draw(
            canvas,
            dlg_x + 5,
            dlg_y + 25,
            dlg_w - 10,
            dlg_h - 30,
            _file_list_entries.size(),
            [this](int idx) {
                if (idx < 0 || idx >= static_cast<int>(_file_list_entries.size())) return std::string("");
                return _file_list_entries[idx].name;
            },
            style
        );
    }
}
