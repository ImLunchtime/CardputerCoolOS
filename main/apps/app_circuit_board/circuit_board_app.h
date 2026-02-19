#pragma once
#include <mooncake.h>
#include <cstdint>
#include <hal.h>

#include <string>
#include <vector>
#include "utils/ui/simple_list.h"

class CircuitBoardApp : public mooncake::AppAbility {
public:
    CircuitBoardApp();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
    
private:
    void draw();
    void goBackOrExit();
    void moveCursor(int dx, int dy);

    size_t _keyboard_slot_id = 0;
    int _cursor_x = 0;
    int _cursor_y = 0;
    
    // Grid configuration
    static constexpr int kGridOffsetX = 6;
    static constexpr int kGridOffsetY = 6;
    static constexpr int kGridSize = 8;
    static constexpr int kGridCols = 11; // (100 - 6) / 8 = 11
    static constexpr int kGridRows = 11; // (100 - 6) / 8 = 11

    enum class Mode {
        Component,
        Trace,
        Remove
    };
    Mode _cursor_mode = Mode::Component;

    // Components
    struct ComponentType {
        const char* name;
        const uint8_t* png_start;
        const uint8_t* png_end;
        int width;
        int height;
        int grid_w; // width in grids
        int grid_h; // height in grids
    };

    struct PlacedComponent {
        int x; // grid x
        int y; // grid y
        int type_index;
    };

    std::vector<ComponentType> _component_types;
    std::vector<PlacedComponent> _placed_components;
    
    bool _is_menu_open = false;
    int _menu_selection = 0;

    void initComponentTypes();
    void openMenu();
    void closeMenu();
    void drawMenu();
    void placeSelectedComponent();
    void moveMenuSelection(int delta);
    bool checkOverlap(int x, int y, int w, int h, int exclude_index = -1);
    void removeComponentAtCursor();

    uint32_t _message_timeout = 0;
    std::string _message_text;
    uint16_t _message_color = TFT_WHITE;
    void showMessage(const std::string& text, uint16_t color);

    // Save/Load
    bool _is_saving = false;
    bool _is_loading = false;
    std::string _current_filename; // Empty if new file
    std::string _save_filename_input;
    
    struct FileEntry {
        std::string name;
        std::string path;
    };
    std::vector<FileEntry> _file_list_entries;
    SmoothSimpleList _file_list;

    void openSaveDialog(bool force_new = false);
    void closeSaveDialog();
    void handleSaveInput(char c);
    void saveToFile(const std::string& filename);
    
    void openLoadDialog();
    void closeLoadDialog();
    void refreshFileList();
    void loadSelectedFile();
    void loadFromFile(const std::string& path);
    void moveLoadSelection(int delta);
    void drawSaveDialog();
    void drawLoadDialog();
};
