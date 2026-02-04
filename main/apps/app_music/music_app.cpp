#include "music_app.h"
#include "music_player.h"
#include <hal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <optional>
#include "utils/ui/simple_list.h"

MusicApp::MusicApp()
{
    setAppInfo().name = "Music";
}

void MusicApp::onOpen()
{
    MusicPlayer::instance().init();
    resetToRoot();
    refreshMp3List();
    _playing_path.clear();
    _playback_started_for_path = false;
    _last_volume = static_cast<int>(GetHAL().speaker.getVolume());
    hookKeyboard();
    draw();
}

void MusicApp::onRunning()
{
    const auto st = MusicPlayer::instance().state();
    const int st_int = static_cast<int>(st);
    bool need_redraw = MusicPlayer::instance().consumeDirty() || (st_int != _last_player_state);
    _last_player_state = st_int;

    if ((st == MusicPlayerState::Playing || st == MusicPlayerState::Paused) && !_playing_path.empty()) {
        _playback_started_for_path = true;
    }

    {
        const int vol = static_cast<int>(GetHAL().speaker.getVolume());
        if (vol != _last_volume) {
            _last_volume = vol;
            need_redraw = true;
        }
    }

    if (st == MusicPlayerState::Idle && !_playing_path.empty() && _playback_started_for_path) {
        _playing_path.clear();
        _playback_started_for_path = false;
        need_redraw = true;
    }

    {
        const std::string name = getInfoPanelFileNameNoExt();
        if (name != _panel_name_cache) {
            _panel_name_cache = name;
            _panel_scroll_x = 0;
            _panel_scroll_last_ms = GetHAL().millis();
            need_redraw = true;
        }
        if (!_panel_name_cache.empty()) {
            const uint32_t now = GetHAL().millis();
            if (now - _panel_scroll_last_ms >= 60) {
                _panel_scroll_last_ms = now;
                need_redraw = true;
            }
        }
    }

    if (need_redraw) {
        draw();
    }
}

void MusicApp::onClose()
{
    unhookKeyboard();
    MusicPlayer::instance().stop();
    _playing_path.clear();
    _playback_started_for_path = false;
}

void MusicApp::refreshMp3List()
{
    _all_tracks.clear();
    _album_to_tracks.clear();
    _artist_to_tracks.clear();
    _uncategorized_tracks.clear();
    _album_keys.clear();
    _artist_keys.clear();

    const char* root = "/sdcard";
    DIR* dir         = opendir(root);
    if (!dir) {
        resetToRoot();
        return;
    }

    const auto trim = [](std::string s) -> std::string {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
            start++;
        }
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
            end--;
        }
        return s.substr(start, end - start);
    };

    const auto parse_name = [&](const std::string& filename) -> std::optional<std::tuple<std::string, std::string, std::string>> {
        if (filename.size() < 4) {
            return std::nullopt;
        }
        const std::string base = filename.substr(0, filename.size() - 4);
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            size_t pos = base.find('-', start);
            if (pos == std::string::npos) {
                parts.push_back(base.substr(start));
                break;
            }
            parts.push_back(base.substr(start, pos - start));
            start = pos + 1;
        }
        if (parts.size() != 3) {
            return std::nullopt;
        }
        std::string artist = trim(parts[0]);
        std::string album = trim(parts[1]);
        std::string title = trim(parts[2]);
        if (artist.empty() || album.empty() || title.empty()) {
            return std::nullopt;
        }
        return std::make_tuple(artist, album, title);
    };

    while (true) {
        dirent* ent = readdir(dir);
        if (!ent) {
            break;
        }

        if (ent->d_name[0] == '.') {
            continue;
        }

        std::string name = ent->d_name;
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower.size() < 4 || lower.rfind(".mp3") != (lower.size() - 4)) {
            continue;
        }

        std::string path = std::string(root) + "/" + name;
        struct stat st {};
        if (stat(path.c_str(), &st) != 0) {
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            continue;
        }

        TrackInfo ti;
        ti.file_name = name;
        ti.path = path;
        if (auto parsed = parse_name(name)) {
            ti.categorized = true;
            ti.artist = std::get<0>(*parsed);
            ti.album = std::get<1>(*parsed);
            ti.title = std::get<2>(*parsed);
        }
        const int idx = static_cast<int>(_all_tracks.size());
        _all_tracks.push_back(std::move(ti));
        if (_all_tracks[idx].categorized) {
            _album_to_tracks[_all_tracks[idx].album].push_back(idx);
            _artist_to_tracks[_all_tracks[idx].artist].push_back(idx);
        } else {
            _uncategorized_tracks.push_back(idx);
        }
    }
    closedir(dir);

    for (const auto& kv : _album_to_tracks) {
        _album_keys.push_back(kv.first);
    }
    for (const auto& kv : _artist_to_tracks) {
        _artist_keys.push_back(kv.first);
    }

    const auto& tracks = _all_tracks;
    for (auto& kv : _album_to_tracks) {
        auto& list = kv.second;
        std::sort(list.begin(), list.end(), [&](int a, int b) {
            const auto& ta = tracks[a];
            const auto& tb = tracks[b];
            if (ta.artist != tb.artist) return ta.artist < tb.artist;
            if (ta.title != tb.title) return ta.title < tb.title;
            return ta.file_name < tb.file_name;
        });
    }
    for (auto& kv : _artist_to_tracks) {
        auto& list = kv.second;
        std::sort(list.begin(), list.end(), [&](int a, int b) {
            const auto& ta = tracks[a];
            const auto& tb = tracks[b];
            if (ta.album != tb.album) return ta.album < tb.album;
            if (ta.title != tb.title) return ta.title < tb.title;
            return ta.file_name < tb.file_name;
        });
    }
    std::sort(_uncategorized_tracks.begin(), _uncategorized_tracks.end(), [&](int a, int b) {
        return tracks[a].file_name < tracks[b].file_name;
    });

    if (_view_stack.empty()) {
        resetToRoot();
    } else {
        const int count = getCurrentItemCount();
        auto& v = _view_stack.back();
        if (count <= 0) {
            v.selected_index = 0;
            v.scroll_offset = 0;
        } else {
            if (v.selected_index < 0) v.selected_index = 0;
            if (v.selected_index >= count) v.selected_index = count - 1;
            if (v.scroll_offset < 0) v.scroll_offset = 0;
            if (v.scroll_offset > v.selected_index) v.scroll_offset = v.selected_index;
        }
    }
}

void MusicApp::hookKeyboard()
{
    if (_keyboard_slot_id != 0) {
        return;
    }

    _keyboard_slot_id = GetHAL().keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& e) {
        if (!e.state) {
            return;
        }

        if (e.keyCode == KEY_MINUS || e.keyCode == KEY_EQUAL) {
            constexpr int step = 5;
            int vol = static_cast<int>(GetHAL().speaker.getVolume());
            if (e.keyCode == KEY_MINUS) {
                vol -= step;
            } else {
                vol += step;
            }
            if (vol < 0) {
                vol = 0;
            } else if (vol > 255) {
                vol = 255;
            }
            GetHAL().speaker.setVolume(static_cast<uint8_t>(vol));
            draw();
            return;
        }

        if (e.keyCode == KEY_ENTER || e.keyCode == KEY_SPACE) {
            activateSelection();
            return;
        }

        if (e.keyCode == KEY_BACKSPACE || e.keyCode == KEY_DELETE) {
            MusicPlayer::instance().stop();
            _playing_path.clear();
            _playback_started_for_path = false;
            draw();
            return;
        }

        if (e.keyCode == KEY_R) {
            refreshMp3List();
            resetToRoot();
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

        if (is_left(e.keyCode) || is_right(e.keyCode)) {
            const auto st = MusicPlayer::instance().state();
            if ((st == MusicPlayerState::Playing || st == MusicPlayerState::Paused) && !_playing_path.empty()) {
                MusicPlayer::instance().seekBySeconds(is_right(e.keyCode) ? 5 : -5);
                return;
            }
        }

        if (is_up(e.keyCode) || is_down(e.keyCode)) {
            auto& canvas = GetHAL().canvas;
            canvas.setFont(&fonts::efontCN_12);
            canvas.setTextSize(1);
            const int list_h = canvas.height() - 8;
            const int row_h = canvas.fontHeight() + 4;
            int visible_row = list_h / row_h;
            if (visible_row < 1) visible_row = 1;
            moveSelection(is_up(e.keyCode) ? -1 : 1, visible_row);
            draw();
            return;
        }

        if (e.keyCode == KEY_ESC || e.keyCode == KEY_GRAVE) {
            navigateBackOrExit();
            return;
        }

    });
}

void MusicApp::unhookKeyboard()
{
    if (_keyboard_slot_id == 0) {
        return;
    }
    GetHAL().keyboard.onKeyEvent.disconnect(_keyboard_slot_id);
    _keyboard_slot_id = 0;
}

void MusicApp::draw()
{
    auto& canvas = GetHAL().canvas;
    const uint16_t bg_color = TFT_NAVY;
    const uint16_t border_color = lgfx::color565(0xAA, 0xAA, 0xAA);
    const uint16_t panel_bg = lgfx::color565(0x44, 0x44, 0x44);
    const uint16_t panel_border = lgfx::color565(0xAA, 0xAA, 0xAA);

    canvas.fillScreen(bg_color);
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::middle_left);

    const int split_x = (canvas.width() * 2) / 3 - 16;
    const int pad = 4;

    const int list_x = pad;
    const int list_y = pad;
    const int list_w = split_x - pad * 2;
    const int list_h = canvas.height() - pad * 2;

    const int panel_x = split_x + pad;
    const int panel_y = pad;
    const int panel_w = canvas.width() - panel_x - pad;
    const int panel_h = canvas.height() - pad * 2;

    const int row_h = canvas.fontHeight() + 4;
    int visible_row = list_h / row_h;
    if (visible_row < 1) {
        visible_row = 1;
    }

    const int item_count = getCurrentItemCount();
    if (item_count <= 0) {
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString("No MP3 files in /sdcard", canvas.width() / 2, canvas.height() / 2);
        GetHAL().pushAppCanvas();
        return;
    }

    SimpleListState list_state;
    list_state.selected_index = _view_stack.back().selected_index;
    list_state.scroll_offset = _view_stack.back().scroll_offset;
    SimpleList::clamp(list_state, item_count);
    _view_stack.back().selected_index = list_state.selected_index;
    _view_stack.back().scroll_offset = list_state.scroll_offset;

    SimpleListStyle style;
    style.bg_color = bg_color;
    style.text_color = TFT_WHITE;
    style.selected_bg_color = TFT_WHITE;
    style.selected_text_color = TFT_BLACK;
    style.padding_x = 2;

    SimpleList::draw(
        canvas,
        list_x,
        list_y,
        list_w,
        list_h,
        list_state,
        item_count,
        [this](int idx) {
            std::string label = getCurrentItemLabel(idx);
            if (isCurrentItemTrack(idx)) {
                const int ti = getCurrentItemTrackIndex(idx);
                if (ti >= 0 && ti < static_cast<int>(_all_tracks.size()) && _all_tracks[ti].path == _playing_path) {
                    return std::string(">> ") + label;
                }
            }
            return std::string("   ") + label;
        },
        style);

    canvas.drawFastVLine(split_x, 0, canvas.height(), border_color);

    canvas.drawRect(panel_x, panel_y, panel_w, panel_h, panel_border);
    canvas.fillRect(panel_x + 1, panel_y + 1, panel_w - 2, panel_h - 2, panel_bg);

    std::string status = "Vol " + std::to_string(GetHAL().speaker.getVolume());
    const auto st = MusicPlayer::instance().state();
    if (st == MusicPlayerState::Playing) {
        status += " >";
    } else if (st == MusicPlayerState::Paused) {
        status += " ||";
    }

    const int info_pad = 6;
    const int info_x0 = panel_x + info_pad;
    const int info_y0 = panel_y + info_pad;
    const int info_w = panel_w - info_pad * 2;

    canvas.setTextColor(TFT_WHITE, panel_bg);
    canvas.setTextDatum(textdatum_t::top_left);

    canvas.drawString(status.c_str(), info_x0, info_y0);

    const std::string name = getInfoPanelFileNameNoExt();
    if (!name.empty()) {
        const int box_y = info_y0 + canvas.fontHeight() + 4;
        const int box_h = canvas.fontHeight() + 6;
        const int box_x = info_x0;
        const int box_w = info_w;

        canvas.drawRect(box_x, box_y, box_w, box_h, border_color);
        canvas.fillRect(box_x + 1, box_y + 1, box_w - 2, box_h - 2, panel_bg);

        canvas.setClipRect(box_x + 2, box_y + 1, box_w - 4, box_h - 2);
        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(TFT_WHITE, panel_bg);

        int text_x = box_x + 3 - _panel_scroll_x;
        const int text_y = box_y + box_h / 2;
        canvas.drawString(name.c_str(), text_x, text_y);

        const int text_w = canvas.textWidth(name.c_str());
        const int avail_w = box_w - 6;
        if (text_w > avail_w) {
            const int gap = 18;
            canvas.drawString(name.c_str(), text_x + text_w + gap, text_y);
            _panel_scroll_x += 2;
            const int period = text_w + gap;
            if (_panel_scroll_x >= period) {
                _panel_scroll_x = 0;
            }
        } else {
            _panel_scroll_x = 0;
        }
        canvas.clearClipRect();
        canvas.setTextDatum(textdatum_t::top_left);

        const int vol_bar_y = box_y + box_h + 6;
        const int vol_bar_h = 10;
        if (vol_bar_y + vol_bar_h <= panel_y + panel_h - info_pad) {
            canvas.drawRect(box_x, vol_bar_y, box_w, vol_bar_h, border_color);
            canvas.fillRect(box_x + 1, vol_bar_y + 1, box_w - 2, vol_bar_h - 2, panel_bg);

            const int vol = static_cast<int>(GetHAL().speaker.getVolume());
            const int inner_w = box_w - 4;
            int fill_w = (inner_w * vol) / 255;
            if (fill_w < 0) fill_w = 0;
            if (fill_w > inner_w) fill_w = inner_w;
            if (fill_w > 0) {
                const uint16_t fill_color = lgfx::color565(0x22, 0xC5, 0x5E);
                canvas.fillRect(box_x + 2, vol_bar_y + 2, fill_w, vol_bar_h - 4, fill_color);
            }
        }
    }

    GetHAL().pushAppCanvas();
}

std::string MusicApp::getInfoPanelFileNameNoExt() const
{
    auto strip_ext = [](const std::string& s) -> std::string {
        if (s.size() >= 4 && s.rfind(".mp3") == s.size() - 4) {
            return s.substr(0, s.size() - 4);
        }
        return s;
    };

    if (_playing_path.empty()) {
        return "";
    }

    const auto it = std::find_if(_all_tracks.begin(), _all_tracks.end(), [&](const TrackInfo& t) { return t.path == _playing_path; });
    if (it != _all_tracks.end()) {
        return strip_ext(it->file_name);
    }

    const auto pos = _playing_path.find_last_of('/');
    if (pos == std::string::npos || pos + 1 >= _playing_path.size()) {
        return strip_ext(_playing_path);
    }
    return strip_ext(_playing_path.substr(pos + 1));
}

void MusicApp::resetToRoot()
{
    _view_stack.clear();
    _view_stack.push_back(ViewState{ViewKind::Root, "", 0, 0});
}

void MusicApp::navigateBackOrExit()
{
    if (_view_stack.size() > 1) {
        _view_stack.pop_back();
        draw();
        return;
    }

    auto& mc = mooncake::GetMooncake();
    auto* app_mgr = mc.getAppAbilityManager();
    const auto app_instances = app_mgr ? app_mgr->getAllAbilityInstance() : std::vector<mooncake::AbilityBase*>{};

    for (auto* app : app_instances) {
        if (app == nullptr) {
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

void MusicApp::activateSelection()
{
    if (_view_stack.empty()) {
        resetToRoot();
    }
    auto& v = _view_stack.back();
    const int count = getCurrentItemCount();
    if (count <= 0) {
        return;
    }
    if (v.selected_index < 0) v.selected_index = 0;
    if (v.selected_index >= count) v.selected_index = count - 1;

    if (v.kind == ViewKind::Root) {
        if (v.selected_index == 0) {
            _view_stack.push_back(ViewState{ViewKind::Albums, "", 0, 0});
        } else if (v.selected_index == 1) {
            _view_stack.push_back(ViewState{ViewKind::Artists, "", 0, 0});
        } else {
            _view_stack.push_back(ViewState{ViewKind::Uncategorized, "", 0, 0});
        }
        draw();
        return;
    }

    if (v.kind == ViewKind::Albums) {
        if (v.selected_index >= 0 && v.selected_index < static_cast<int>(_album_keys.size())) {
            _view_stack.push_back(ViewState{ViewKind::AlbumTracks, _album_keys[v.selected_index], 0, 0});
            draw();
        }
        return;
    }

    if (v.kind == ViewKind::Artists) {
        if (v.selected_index >= 0 && v.selected_index < static_cast<int>(_artist_keys.size())) {
            _view_stack.push_back(ViewState{ViewKind::ArtistTracks, _artist_keys[v.selected_index], 0, 0});
            draw();
        }
        return;
    }

    if (isCurrentItemTrack(v.selected_index)) {
        const int ti = getCurrentItemTrackIndex(v.selected_index);
        if (ti < 0 || ti >= static_cast<int>(_all_tracks.size())) {
            return;
        }
        auto& player = MusicPlayer::instance();
        if (_all_tracks[ti].path == _playing_path) {
            player.togglePause();
        } else {
            if (player.playFile(_all_tracks[ti].path)) {
                _playing_path = _all_tracks[ti].path;
                _playback_started_for_path = false;
            }
        }
        draw();
        return;
    }
}

void MusicApp::moveSelection(int delta, int visible_rows)
{
    if (_view_stack.empty()) {
        resetToRoot();
    }
    auto& v = _view_stack.back();
    const int count = getCurrentItemCount();
    SimpleListState s{v.selected_index, v.scroll_offset};
    SimpleList::move(s, delta, count, visible_rows);
    v.selected_index = s.selected_index;
    v.scroll_offset = s.scroll_offset;
}

int MusicApp::getCurrentItemCount() const
{
    if (_view_stack.empty()) {
        return 0;
    }
    const auto& v = _view_stack.back();
    switch (v.kind) {
        case ViewKind::Root:
            return 3;
        case ViewKind::Albums:
            return static_cast<int>(_album_keys.size());
        case ViewKind::Artists:
            return static_cast<int>(_artist_keys.size());
        case ViewKind::Uncategorized:
            return static_cast<int>(_uncategorized_tracks.size());
        case ViewKind::AlbumTracks: {
            const auto it = _album_to_tracks.find(v.key);
            return (it == _album_to_tracks.end()) ? 0 : static_cast<int>(it->second.size());
        }
        case ViewKind::ArtistTracks: {
            const auto it = _artist_to_tracks.find(v.key);
            return (it == _artist_to_tracks.end()) ? 0 : static_cast<int>(it->second.size());
        }
        default:
            return 0;
    }
}

std::string MusicApp::getCurrentItemLabel(int idx) const
{
    if (_view_stack.empty()) {
        return "";
    }
    const auto& v = _view_stack.back();
    if (v.kind == ViewKind::Root) {
        if (idx == 0) return "Albums";
        if (idx == 1) return "Artists";
        return "Uncategorized";
    }
    if (v.kind == ViewKind::Albums) {
        if (idx >= 0 && idx < static_cast<int>(_album_keys.size())) return _album_keys[idx];
        return "";
    }
    if (v.kind == ViewKind::Artists) {
        if (idx >= 0 && idx < static_cast<int>(_artist_keys.size())) return _artist_keys[idx];
        return "";
    }
    if (isCurrentItemTrack(idx)) {
        const int ti = getCurrentItemTrackIndex(idx);
        if (ti < 0 || ti >= static_cast<int>(_all_tracks.size())) return "";
        const auto& t = _all_tracks[ti];
        if (v.kind == ViewKind::Uncategorized) {
            if (t.file_name.size() >= 4 && t.file_name.rfind(".mp3") == t.file_name.size() - 4) {
                return t.file_name.substr(0, t.file_name.size() - 4);
            }
            return t.file_name;
        }
        if (v.kind == ViewKind::AlbumTracks) {
            return t.title;
        }
        if (v.kind == ViewKind::ArtistTracks) {
            return t.title;
        }
        return t.file_name;
    }
    return "";
}

bool MusicApp::isCurrentItemTrack(int idx) const
{
    if (_view_stack.empty()) {
        return false;
    }
    const auto& v = _view_stack.back();
    return v.kind == ViewKind::Uncategorized || v.kind == ViewKind::AlbumTracks || v.kind == ViewKind::ArtistTracks;
}

int MusicApp::getCurrentItemTrackIndex(int idx) const
{
    if (_view_stack.empty()) {
        return -1;
    }
    const auto& v = _view_stack.back();
    if (idx < 0) {
        return -1;
    }
    if (v.kind == ViewKind::Uncategorized) {
        if (idx >= static_cast<int>(_uncategorized_tracks.size())) return -1;
        return _uncategorized_tracks[idx];
    }
    if (v.kind == ViewKind::AlbumTracks) {
        const auto it = _album_to_tracks.find(v.key);
        if (it == _album_to_tracks.end()) return -1;
        if (idx >= static_cast<int>(it->second.size())) return -1;
        return it->second[idx];
    }
    if (v.kind == ViewKind::ArtistTracks) {
        const auto it = _artist_to_tracks.find(v.key);
        if (it == _artist_to_tracks.end()) return -1;
        if (idx >= static_cast<int>(it->second.size())) return -1;
        return it->second[idx];
    }
    return -1;
}

std::string MusicApp::getViewTitle() const
{
    if (_view_stack.empty()) {
        return "";
    }
    const auto& v = _view_stack.back();
    switch (v.kind) {
        case ViewKind::Root:
            return "Root";
        case ViewKind::Albums:
            return "Albums";
        case ViewKind::Artists:
            return "Artists";
        case ViewKind::Uncategorized:
            return "Uncategorized";
        case ViewKind::AlbumTracks:
            return "Album: " + v.key;
        case ViewKind::ArtistTracks:
            return "Artist: " + v.key;
        default:
            return "";
    }
}
