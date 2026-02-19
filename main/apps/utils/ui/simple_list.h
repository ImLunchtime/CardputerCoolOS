#pragma once
#include <M5GFX.h>
#include <cstdint>
#include <functional>
#include <string>
#include <cmath>
#include <smooth_ui_toolkit.h>

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

class SmoothSimpleList {
public:
    SmoothSimpleList()
    {
        _anim_idx.init();
        _anim_scroll.init();

        auto& opt = _anim_idx.easingOptions();
        opt.duration = 0.05f;
        opt.easingFunction = smooth_ui_toolkit::ease::ease_out_expo;

        auto& opt_s = _anim_scroll.easingOptions();
        opt_s.duration = 0.06f;
        opt_s.easingFunction = smooth_ui_toolkit::ease::ease_out_expo;
    }

    void update(uint32_t time_ms)
    {
        float t = time_ms / 1000.0f;
        _anim_idx.update(t);
        _anim_scroll.update(t);
    }

    bool isAnimating()
    {
        return !_anim_idx.done() || !_anim_scroll.done();
    }

    void go(int index, int item_count, int visible_rows)
    {
        if (item_count <= 0) return;
        if (index < 0) index = 0;
        if (index >= item_count) index = item_count - 1;

        _target_idx = index;

        int max_scroll = item_count - visible_rows;
        if (max_scroll < 0) max_scroll = 0;

        int target_scroll = 0;
        
        // Logic: Try to keep selection in middle
        if (item_count > visible_rows) {
            target_scroll = index - visible_rows / 2;
            if (target_scroll < 0) target_scroll = 0;
            if (target_scroll > max_scroll) target_scroll = max_scroll;
        }

        _anim_idx.retarget(_anim_idx.value(), (float)index);
        _anim_scroll.retarget(_anim_scroll.value(), (float)target_scroll);

        _anim_idx.play();
        _anim_scroll.play();
    }
    
    void jumpTo(int index, int item_count, int visible_rows) {
        go(index, item_count, visible_rows);
        
        // Calculate target scroll again to snap
        int max_scroll = item_count - visible_rows;
        if (max_scroll < 0) max_scroll = 0;
        int target_scroll = 0;
        if (item_count > visible_rows) {
            target_scroll = _target_idx - visible_rows / 2;
            if (target_scroll < 0) target_scroll = 0;
            if (target_scroll > max_scroll) target_scroll = max_scroll;
        }
        float scroll_f = (float)target_scroll;
        float idx_f = (float)_target_idx;
        
        _anim_idx.retarget(idx_f, idx_f);
        _anim_scroll.retarget(scroll_f, scroll_f);
        _anim_idx.complete();
        _anim_scroll.complete();
    }

    int getSelectedIndex() const { return _target_idx; }

    void draw(
        LGFX_Sprite& canvas,
        int x,
        int y,
        int w,
        int h,
        int item_count,
        const std::function<std::string(int)>& label_fn,
        const SimpleListStyle& style)
    {
        canvas.fillRect(x, y, w, h, style.bg_color);
        if (item_count <= 0) return;

        const int row_h = SimpleList::rowHeight(canvas);
        const int visible_rows_count = h / row_h + 2; 

        float cur_scroll = _anim_scroll.value();
        float cur_idx = _anim_idx.value();

        // 1. Draw all items in normal color
        int start_idx = (int)std::floor(cur_scroll);
        if (start_idx < 0) start_idx = 0;
        int end_idx = start_idx + visible_rows_count;
        if (end_idx > item_count) end_idx = item_count;

        canvas.setTextDatum(textdatum_t::middle_left);
        canvas.setTextColor(style.text_color, style.bg_color);
        
        canvas.setClipRect(x, y, w, h);

        for (int i = start_idx; i < end_idx; ++i) {
            float item_y = y + (i - cur_scroll) * row_h;
            if (item_y + row_h <= y || item_y >= y + h) continue;

            std::string label = label_fn(i);
            canvas.drawString(label.c_str(), x + style.padding_x, (int)(item_y + row_h / 2));
        }

        // 2. Draw Highlight and selected text
        int hl_y = (int)(y + (cur_idx - cur_scroll) * row_h);
        int hl_h = row_h;

        int clip_y = std::max(y, hl_y);
        int clip_bottom = std::min(y + h, hl_y + hl_h);
        int clip_h = clip_bottom - clip_y;

        if (clip_h > 0) {
            canvas.setClipRect(x, clip_y, w, clip_h);
            canvas.fillRect(x, hl_y, w, hl_h, style.selected_bg_color);

            canvas.setTextColor(style.selected_text_color, style.selected_bg_color);
            
            int hl_start_item = (int)std::floor(cur_idx);
            for (int i = hl_start_item - 1; i <= hl_start_item + 1; ++i) {
                if (i < start_idx || i >= end_idx) continue;
                
                float item_y = y + (i - cur_scroll) * row_h;
                if (item_y + row_h < hl_y || item_y > hl_y + hl_h) continue;

                std::string label = label_fn(i);
                canvas.drawString(label.c_str(), x + style.padding_x, (int)(item_y + row_h / 2));
            }
        }
        canvas.clearClipRect();
    }

private:
    smooth_ui_toolkit::Animate _anim_idx;
    smooth_ui_toolkit::Animate _anim_scroll;
    int _target_idx = 0;
};

