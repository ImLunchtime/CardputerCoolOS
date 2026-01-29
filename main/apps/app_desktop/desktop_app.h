#pragma once
#include <mooncake.h>
#include <cstddef>
#include <string>
#include <vector>

class DesktopApp : public mooncake::AppAbility {
public:
    DesktopApp();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    struct AppEntry {
        int id = -1;
        std::string name;
    };

    void draw();
    void refreshAppList();
    void hookKeyboard();
    void unhookKeyboard();

    std::vector<AppEntry> _apps;
    int _selected_index = 0;
    int _scroll_offset  = 0;
    size_t _keyboard_slot_id = 0;
};
