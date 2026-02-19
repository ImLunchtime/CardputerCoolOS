#include "pictures_app.h"
#include <hal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include "utils/ui/simple_list.h"

PicturesApp::PicturesApp()
{
    setAppInfo().name = "Pictures";
}

void PicturesApp::onOpen()
{
    _mode = Mode::Browse;
    _dir_stack.clear();
    _dir_stack.emplace_back();
    _dir_stack.back().dir_path = "/sdcard";
    _view_entry_index = -1;
    resetViewTransform();
    refreshCurrentDir();
    hookKeyboard();
    draw();
}

void PicturesApp::onRunning()
{
    if (_mode == Mode::Browse && !_dir_stack.empty()) {
        auto& st = _dir_stack.back();
        st.list.update(GetHAL().millis());
        if (st.list.isAnimating()) {
            draw();
        }
    }
}

void PicturesApp::onClose()
{
    unhookKeyboard();
    _dir_stack.clear();
    _view_entry_index = -1;
}

void PicturesApp::hookKeyboard()
{
    if (_keyboard_slot_id != 0) {
        return;
    }

    _keyboard_slot_id = GetHAL().keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& e) {
        if (!e.state) {
            return;
        }

        if (_mode == Mode::View) {
            if (e.keyCode == KEY_ESC || e.keyCode == KEY_GRAVE) {
                _mode = Mode::Browse;
                draw();
                return;
            }

            if (e.keyCode == KEY_MINUS || e.keyCode == KEY_EQUAL) {
                float next = _view_scale;
                if (e.keyCode == KEY_MINUS) {
                    next *= 0.9f;
                } else {
                    next *= 1.1f;
                }
                if (next < 0.1f) next = 0.1f;
                if (next > 8.0f) next = 8.0f;
                _view_scale = next;
                draw();
                return;
            }

            if (e.keyCode == KEY_LEFTBRACE || e.keyCode == KEY_RIGHTBRACE) {
                stepImage(e.keyCode == KEY_LEFTBRACE ? -1 : 1);
                draw();
                return;
            }

            const auto is_up = [](KeScanCode_t code) {
                return code == KEY_UP || code == KEY_W || code == KEY_K || code == KEY_SEMICOLON;
            };
            const auto is_down = [](KeScanCode_t code) {
                return code == KEY_DOWN || code == KEY_S || code == KEY_J || code == KEY_DOT;
            };
            const auto is_left = [](KeScanCode_t code) {
                return code == KEY_LEFT || code == KEY_A || code == KEY_H || code == KEY_COMMA;
            };
            const auto is_right = [](KeScanCode_t code) {
                return code == KEY_RIGHT || code == KEY_D || code == KEY_L || code == KEY_SLASH;
            };

            if (is_up(e.keyCode) || is_down(e.keyCode) || is_left(e.keyCode) || is_right(e.keyCode)) {
                constexpr int step = 12;
                if (is_up(e.keyCode)) {
                    _view_pan_y -= step;
                } else if (is_down(e.keyCode)) {
                    _view_pan_y += step;
                } else if (is_left(e.keyCode)) {
                    _view_pan_x -= step;
                } else {
                    _view_pan_x += step;
                }
                draw();
                return;
            }

            return;
        }

        if (e.keyCode == KEY_R) {
            refreshCurrentDir();
            draw();
            return;
        }

        if (e.keyCode == KEY_ESC || e.keyCode == KEY_GRAVE) {
            goBackOrExit();
            return;
        }

        if (e.keyCode == KEY_ENTER || e.keyCode == KEY_SPACE) {
            enterSelected();
            return;
        }

        const auto is_up = [](KeScanCode_t code) {
            return code == KEY_UP || code == KEY_W || code == KEY_K || code == KEY_SEMICOLON;
        };
        const auto is_down = [](KeScanCode_t code) {
            return code == KEY_DOWN || code == KEY_S || code == KEY_J || code == KEY_DOT;
        };

        if (is_up(e.keyCode) || is_down(e.keyCode)) {
            auto& canvas = GetHAL().canvas;
            canvas.setFont(&fonts::efontCN_12);
            canvas.setTextSize(1);

            const int pad = 4;
            const int header_h = canvas.fontHeight() + 4;
            const int list_h = canvas.height() - header_h - pad * 2;
            const int row_h = SimpleList::rowHeight(canvas);
            const int visible_row = SimpleList::visibleRows(list_h, row_h);

            moveSelection(is_up(e.keyCode) ? -1 : 1, visible_row);
            draw();
            return;
        }
    });
}

void PicturesApp::unhookKeyboard()
{
    if (_keyboard_slot_id == 0) {
        return;
    }
    GetHAL().keyboard.onKeyEvent.disconnect(_keyboard_slot_id);
    _keyboard_slot_id = 0;
}

void PicturesApp::draw()
{
    if (_mode == Mode::View) {
        drawView();
    } else {
        drawBrowse();
    }
}

void PicturesApp::drawBrowse()
{
    auto& canvas = GetHAL().canvas;
    const uint16_t bg = lgfx::color565(0x18, 0x18, 0x18);
    const uint16_t header_bg = lgfx::color565(0x2D, 0x2D, 0x2D);

    canvas.fillScreen(bg);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1);

    const int pad = 4;
    const int header_h = canvas.fontHeight() + 4;
    canvas.fillRect(0, 0, canvas.width(), header_h, header_bg);
    canvas.setTextColor(TFT_WHITE, header_bg);
    canvas.setTextDatum(textdatum_t::middle_left);

    std::string title = "Pictures: ";
    if (!_dir_stack.empty()) {
        title += baseName(_dir_stack.back().dir_path);
    } else {
        title += "(none)";
    }
    canvas.drawString(title.c_str(), pad, header_h / 2);

    if (!GetHAL().isSdCardMounted()) {
        canvas.setTextColor(TFT_WHITE, bg);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("SD card not mounted", canvas.width() / 2, canvas.height() / 2);
        GetHAL().pushAppCanvas();
        return;
    }

    if (_dir_stack.empty()) {
        canvas.setTextColor(TFT_WHITE, bg);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("No directory", canvas.width() / 2, canvas.height() / 2);
        GetHAL().pushAppCanvas();
        return;
    }

    auto& st = _dir_stack.back();
    const int item_count = static_cast<int>(st.entries.size());
    if (item_count <= 0) {
        canvas.setTextColor(TFT_WHITE, bg);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("No folders or PNG files", canvas.width() / 2, canvas.height() / 2);
        GetHAL().pushAppCanvas();
        return;
    }

    SimpleListStyle style;
    style.bg_color = bg;
    style.text_color = TFT_WHITE;
    style.selected_bg_color = TFT_WHITE;
    style.selected_text_color = TFT_BLACK;
    style.padding_x = 2;

    const int list_x = pad;
    const int list_y = header_h + pad;
    const int list_w = canvas.width() - pad * 2;
    const int list_h = canvas.height() - list_y - pad;

    st.list.draw(
        canvas,
        list_x,
        list_y,
        list_w,
        list_h,
        item_count,
        [&st](int idx) {
            if (idx < 0 || idx >= static_cast<int>(st.entries.size())) {
                return std::string();
            }
            const auto& e = st.entries[idx];
            if (e.is_dir) {
                return std::string("[DIR] ") + e.name;
            }
            return stripPngExt(e.name);
        },
        style);

    GetHAL().pushAppCanvas();
}

void PicturesApp::drawView()
{
    auto& canvas = GetHAL().canvas;
    canvas.fillScreen(TFT_BLACK);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1);

    const int pad = 4;
    const int header_h = canvas.fontHeight() + 4;
    canvas.fillRect(0, 0, canvas.width(), header_h, TFT_BLACK);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextDatum(textdatum_t::middle_left);

    std::string label;
    if (!_dir_stack.empty() && _view_entry_index >= 0 && _view_entry_index < static_cast<int>(_dir_stack.back().entries.size())) {
        label = stripPngExt(_dir_stack.back().entries[_view_entry_index].name);
    } else {
        label = "Picture";
    }
    canvas.drawString(label.c_str(), pad, header_h / 2);

    if (!GetHAL().isSdCardMounted()) {
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("SD card not mounted", canvas.width() / 2, canvas.height() / 2);
        GetHAL().pushAppCanvas();
        return;
    }

    if (_dir_stack.empty() || _view_entry_index < 0 || _view_entry_index >= static_cast<int>(_dir_stack.back().entries.size())) {
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("No image", canvas.width() / 2, canvas.height() / 2);
        GetHAL().pushAppCanvas();
        return;
    }

    const auto& e = _dir_stack.back().entries[_view_entry_index];
    bool ok = false;
    if (!e.path.empty()) {
        const int view_x = 0;
        const int view_y = header_h;
        const int view_w = canvas.width();
        const int view_h = canvas.height() - header_h;
        ok = canvas.drawPngFile(e.path.c_str(), view_x, view_y, view_w, view_h, _view_pan_x, _view_pan_y, _view_scale, 0.0f, datum_t::middle_center);
    }

    if (!ok) {
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("Failed to load PNG", canvas.width() / 2, canvas.height() / 2);
    }

    GetHAL().pushAppCanvas();
}

void PicturesApp::refreshCurrentDir()
{
    if (_dir_stack.empty()) {
        return;
    }

    auto& st = _dir_stack.back();
    st.entries.clear();

    if (!GetHAL().isSdCardMounted()) {
        st.list.jumpTo(0, 0, 1);
        return;
    }

    DIR* dir = opendir(st.dir_path.c_str());
    if (!dir) {
        st.list.jumpTo(0, 0, 1);
        return;
    }

    while (true) {
        dirent* ent = readdir(dir);
        if (!ent) {
            break;
        }

        if (ent->d_name[0] == '.') {
            continue;
        }

        std::string name = ent->d_name;
        std::string path = joinPath(st.dir_path, name);

        struct stat s {};
        if (stat(path.c_str(), &s) != 0) {
            continue;
        }

        if (S_ISDIR(s.st_mode)) {
            st.entries.push_back(Entry{name, path, true});
            continue;
        }

        if (S_ISREG(s.st_mode) && isPngFileName(name)) {
            st.entries.push_back(Entry{name, path, false});
        }
    }
    closedir(dir);

    std::sort(st.entries.begin(), st.entries.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir && !b.is_dir;
        }
        std::string an = a.name;
        std::string bn = b.name;
        std::transform(an.begin(), an.end(), an.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(bn.begin(), bn.end(), bn.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return an < bn;
    });

    const int item_count = static_cast<int>(st.entries.size());
    if (item_count <= 0) {
        st.list.jumpTo(0, 0, 1);
        return;
    }

    auto& canvas = GetHAL().canvas;
    canvas.setFont(&fonts::efontCN_12);
    const int pad = 4;
    const int header_h = canvas.fontHeight() + 4;
    const int list_h = canvas.height() - header_h - pad * 2;
    const int row_h = SimpleList::rowHeight(canvas);
    const int visible_rows = SimpleList::visibleRows(list_h, row_h);

    int idx = st.list.getSelectedIndex();
    if (idx >= item_count) idx = item_count - 1;
    st.list.jumpTo(idx, item_count, visible_rows);

    if (_mode == Mode::View) {
        if (_view_entry_index < 0 || _view_entry_index >= item_count || st.entries[_view_entry_index].is_dir) {
            _mode = Mode::Browse;
            _view_entry_index = -1;
        }
    }
}

void PicturesApp::enterSelected()
{
    if (_dir_stack.empty()) {
        return;
    }

    auto& st = _dir_stack.back();
    if (st.entries.empty()) {
        return;
    }

    int idx = st.list.getSelectedIndex();
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(st.entries.size())) idx = static_cast<int>(st.entries.size()) - 1;

    const auto& e = st.entries[idx];
    if (e.is_dir) {
        _dir_stack.emplace_back();
        _dir_stack.back().dir_path = e.path;
        refreshCurrentDir();
        draw();
        return;
    }

    openImageAtEntryIndex(idx);
    draw();
}

void PicturesApp::openImageAtEntryIndex(int entry_index)
{
    if (_dir_stack.empty()) {
        return;
    }

    auto& st = _dir_stack.back();
    if (entry_index < 0 || entry_index >= static_cast<int>(st.entries.size())) {
        return;
    }
    if (st.entries[entry_index].is_dir) {
        return;
    }
    if (!isPngFileName(st.entries[entry_index].name)) {
        return;
    }

    resetViewTransform();
    _mode = Mode::View;
    _view_entry_index = entry_index;
}

void PicturesApp::resetViewTransform()
{
    _view_scale = 1.0f;
    _view_pan_x = 0;
    _view_pan_y = 0;
}

void PicturesApp::goBackOrExit()
{
    if (_mode == Mode::View) {
        _mode = Mode::Browse;
        draw();
        return;
    }

    if (_dir_stack.size() > 1) {
        _dir_stack.pop_back();
        refreshCurrentDir();
        draw();
        return;
    }

    auto& mc = mooncake::GetMooncake();
    auto* app_mgr = mc.getAppAbilityManager();
    const auto app_instances = app_mgr ? app_mgr->getAllAbilityInstance() : std::vector<mooncake::AbilityBase*>{};
    for (auto* app : app_instances) {
        if (!app) {
            continue;
        }
        const int id = app->getId();
        const auto info = mc.getAppInfo(id);
        if (info.name == "Desktop") {
            mc.openApp(id);
            mc.closeApp(getId());
            return;
        }
    }
}

void PicturesApp::moveSelection(int delta, int visible_rows)
{
    if (_dir_stack.empty()) {
        return;
    }
    auto& st = _dir_stack.back();
    int idx = st.list.getSelectedIndex();
    st.list.go(idx + delta, static_cast<int>(st.entries.size()), visible_rows);
}

void PicturesApp::stepImage(int delta)
{
    if (_dir_stack.empty()) {
        return;
    }
    if (_view_entry_index < 0) {
        _view_entry_index = getFirstImageEntryIndex();
        return;
    }
    const int next = findNextImageEntryIndex(_view_entry_index, delta);
    if (next >= 0) {
        _view_entry_index = next;
        
        // Calculate visible rows for jumpTo
        auto& canvas = GetHAL().canvas;
        canvas.setFont(&fonts::efontCN_12);
        const int pad = 4;
        const int header_h = canvas.fontHeight() + 4;
        const int list_h = canvas.height() - header_h - pad * 2;
        const int row_h = SimpleList::rowHeight(canvas);
        const int visible_rows = SimpleList::visibleRows(list_h, row_h);

        _dir_stack.back().list.jumpTo(next, static_cast<int>(_dir_stack.back().entries.size()), visible_rows);
        resetViewTransform();
    }
}

int PicturesApp::findNextImageEntryIndex(int start_entry_index, int delta) const
{
    if (_dir_stack.empty()) {
        return -1;
    }
    const auto& entries = _dir_stack.back().entries;
    const int n = static_cast<int>(entries.size());
    if (n <= 0) {
        return -1;
    }
    if (delta == 0) {
        return start_entry_index;
    }

    int idx = start_entry_index;
    for (int step = 0; step < n; ++step) {
        idx += (delta > 0) ? 1 : -1;
        if (idx < 0) idx = n - 1;
        if (idx >= n) idx = 0;
        if (!entries[idx].is_dir && isPngFileName(entries[idx].name)) {
            return idx;
        }
    }
    return -1;
}

int PicturesApp::countImagesInCurrentDir() const
{
    if (_dir_stack.empty()) {
        return 0;
    }
    int c = 0;
    for (const auto& e : _dir_stack.back().entries) {
        if (!e.is_dir && isPngFileName(e.name)) {
            ++c;
        }
    }
    return c;
}

int PicturesApp::getFirstImageEntryIndex() const
{
    if (_dir_stack.empty()) {
        return -1;
    }
    const auto& entries = _dir_stack.back().entries;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (!entries[i].is_dir && isPngFileName(entries[i].name)) {
            return i;
        }
    }
    return -1;
}

bool PicturesApp::isPngFileName(const std::string& name)
{
    if (name.size() < 4) {
        return false;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.rfind(".png") == lower.size() - 4;
}

std::string PicturesApp::stripPngExt(const std::string& name)
{
    if (isPngFileName(name) && name.size() >= 4) {
        return name.substr(0, name.size() - 4);
    }
    return name;
}

std::string PicturesApp::baseName(const std::string& path)
{
    if (path.empty()) {
        return "";
    }
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    if (pos + 1 >= path.size()) {
        return path;
    }
    return path.substr(pos + 1);
}

std::string PicturesApp::joinPath(const std::string& dir, const std::string& name)
{
    if (dir.empty()) {
        return name;
    }
    if (dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}
