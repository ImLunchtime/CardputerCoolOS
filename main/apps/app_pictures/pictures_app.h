#pragma once
#include <mooncake.h>
#include <cstddef>
#include <string>
#include <vector>

class PicturesApp : public mooncake::AppAbility {
public:
    PicturesApp();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class Mode : uint8_t {
        Browse = 0,
        View = 1,
    };

    struct Entry {
        std::string name;
        std::string path;
        bool is_dir = false;
    };

    struct FolderState {
        std::string dir_path;
        std::vector<Entry> entries;
        int selected_index = 0;
        int scroll_offset = 0;
    };

    void draw();
    void drawBrowse();
    void drawView();
    void hookKeyboard();
    void unhookKeyboard();
    void refreshCurrentDir();
    void enterSelected();
    void goBackOrExit();
    void moveSelection(int delta, int visible_rows);
    void openImageAtEntryIndex(int entry_index);
    void stepImage(int delta);
    void resetViewTransform();
    int findNextImageEntryIndex(int start_entry_index, int delta) const;
    int countImagesInCurrentDir() const;
    int getFirstImageEntryIndex() const;
    static bool isPngFileName(const std::string& name);
    static std::string stripPngExt(const std::string& name);
    static std::string baseName(const std::string& path);
    static std::string joinPath(const std::string& dir, const std::string& name);

    Mode _mode = Mode::Browse;
    std::vector<FolderState> _dir_stack;
    int _view_entry_index = -1;
    float _view_scale = 1.0f;
    int _view_pan_x = 0;
    int _view_pan_y = 0;
    size_t _keyboard_slot_id = 0;
};

