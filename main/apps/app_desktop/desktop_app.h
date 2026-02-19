#pragma once
#include <mooncake.h>
#include <cstddef>
#include <string>
#include <vector>
#include "utils/ui/simple_list.h"

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
    SmoothSimpleList _list;
    size_t _keyboard_slot_id = 0;
};
