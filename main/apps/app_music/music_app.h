#pragma once
#include <mooncake.h>
#include <cstddef>
#include <string>
#include <vector>
#include <map>

class MusicApp : public mooncake::AppAbility {
public:
    MusicApp();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class ViewKind : uint8_t {
        Root = 0,
        Albums = 1,
        Artists = 2,
        Uncategorized = 3,
        AlbumTracks = 4,
        ArtistTracks = 5,
    };

    struct TrackInfo {
        std::string file_name;
        std::string path;
        bool categorized = false;
        std::string artist;
        std::string album;
        std::string title;
    };

    struct ViewState {
        ViewKind kind = ViewKind::Root;
        std::string key;
        int selected_index = 0;
        int scroll_offset = 0;
    };

    void draw();
    void refreshMp3List();
    void hookKeyboard();
    void unhookKeyboard();
    void resetToRoot();
    void navigateBackOrExit();
    void activateSelection();
    void moveSelection(int delta, int visible_rows);
    int getCurrentItemCount() const;
    std::string getCurrentItemLabel(int idx) const;
    bool isCurrentItemTrack(int idx) const;
    int getCurrentItemTrackIndex(int idx) const;
    std::string getViewTitle() const;
    std::string getInfoPanelFileNameNoExt() const;

    std::vector<TrackInfo> _all_tracks;
    std::map<std::string, std::vector<int>> _album_to_tracks;
    std::map<std::string, std::vector<int>> _artist_to_tracks;
    std::vector<int> _uncategorized_tracks;
    std::vector<std::string> _album_keys;
    std::vector<std::string> _artist_keys;

    std::vector<ViewState> _view_stack;
    std::string _playing_path;
    int _last_player_state = 0;
    size_t _keyboard_slot_id = 0;

    std::string _panel_name_cache;
    int _panel_scroll_x = 0;
    uint32_t _panel_scroll_last_ms = 0;
};
