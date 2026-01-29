#pragma once
#include <M5GFX.h>
#include <cstdint>
#include <functional>
#include <string>

struct SimpleListState {
    int selected_index = 0;
    int scroll_offset = 0;
};

struct SimpleListStyle {
    uint16_t bg_color = TFT_BLACK;
    uint16_t text_color = TFT_WHITE;
    uint16_t selected_bg_color = TFT_WHITE;
    uint16_t selected_text_color = TFT_BLACK;
    int padding_x = 2;
};

class SimpleList {
public:
    static int rowHeight(LGFX_Sprite& canvas)
    {
        return canvas.fontHeight() + 4;
    }

    static int visibleRows(int list_h, int row_h)
    {
        if (row_h <= 0) {
            return 1;
        }
        int v = list_h / row_h;
        if (v < 1) {
            v = 1;
        }
        return v;
    }

    static void clamp(SimpleListState& s, int item_count)
    {
        if (item_count <= 0) {
            s.selected_index = 0;
            s.scroll_offset = 0;
            return;
        }
        if (s.selected_index < 0) {
            s.selected_index = 0;
        }
        if (s.selected_index >= item_count) {
            s.selected_index = item_count - 1;
        }
        if (s.scroll_offset < 0) {
            s.scroll_offset = 0;
        }
        if (s.scroll_offset > s.selected_index) {
            s.scroll_offset = s.selected_index;
        }
    }

    static void move(SimpleListState& s, int delta, int item_count, int visible_rows)
    {
        clamp(s, item_count);
        if (item_count <= 0) {
            return;
        }
        if (visible_rows < 1) {
            visible_rows = 1;
        }
        s.selected_index += delta;
        if (s.selected_index < 0) {
            s.selected_index = 0;
        } else if (s.selected_index >= item_count) {
            s.selected_index = item_count - 1;
        }
        if (s.scroll_offset > s.selected_index) {
            s.scroll_offset = s.selected_index;
        } else if (s.selected_index >= s.scroll_offset + visible_rows) {
            s.scroll_offset = s.selected_index - visible_rows + 1;
        }
        if (s.scroll_offset < 0) {
            s.scroll_offset = 0;
        }
    }

    static void draw(
        LGFX_Sprite& canvas,
        int x,
        int y,
        int w,
        int h,
        const SimpleListState& s,
        int item_count,
        const std::function<std::string(int)>& label_fn,
        const SimpleListStyle& style)
    {
        canvas.fillRect(x, y, w, h, style.bg_color);

        const int row_h = rowHeight(canvas);
        const int visible_rows = visibleRows(h, row_h);

        canvas.setTextDatum(textdatum_t::middle_left);

        for (int row = 0; row < visible_rows; ++row) {
            const int idx = s.scroll_offset + row;
            if (idx >= item_count) {
                break;
            }

            const int row_y = y + row * row_h;
            const bool selected = (idx == s.selected_index);
            const uint16_t row_bg = selected ? style.selected_bg_color : style.bg_color;
            const uint16_t row_fg = selected ? style.selected_text_color : style.text_color;

            if (selected) {
                canvas.fillRect(x, row_y, w, row_h, row_bg);
            }

            canvas.setTextColor(row_fg, row_bg);
            std::string label = label_fn ? label_fn(idx) : std::string();

            canvas.setClipRect(x, row_y, w, row_h);
            canvas.drawString(label.c_str(), x + style.padding_x, row_y + row_h / 2);
            canvas.clearClipRect();
        }
    }
};

