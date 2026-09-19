#pragma once

#include <ncurses.h>
#include <algorithm>
#include <string>

namespace drivelab::tui {

struct Layout {
    int height = 0;
    int width = 0;
    int main_top = 3;
    int main_bottom = 0;
    int bottom_top = 0;
    int bottom_rows = 0;
    int bottom_entry_y = 0;
    int left_divider = 0;
    int right_start = 0;
    int right_end = 0;
    int identity_separator = 7;
    int drive_start = 6;
    int drive_row_height = 3;
    int drive_row_stride = 3;
    int drive_first_y = 6;
    int drive_viewport_height = 0;
    int visible_drives = 0;
    int drive_indicator_y = 0;
    int content_top = 0;
    bool valid = false;
};


inline void printClipped(int y, int x, int width, const std::string& text, int attributes = 0) {
    if (y < 0 || y >= LINES || x < 0 || x >= COLS || width <= 0) return;
    int safe_width = std::min(width, COLS - x);
    attron(attributes);
    mvaddnstr(y, x, text.c_str(), safe_width);
    attroff(attributes);
}

inline void printRight(int y, int right, const std::string& text, int attributes = 0) {
    int x = std::max(1, right - static_cast<int>(text.size()) + 1);
    printClipped(y, x, right - x + 1, text, attributes);
}


inline void drawVerticalScrollbar(int y, int x, int height, int total, int visible, int offset) {
    if (height <= 0 || y < 0 || x < 0 || x >= COLS) return;
    int safe_height = std::min(height, LINES - y);
    attron(A_DIM | COLOR_PAIR(1));
    for (int row = 0; row < safe_height; ++row) mvaddch(y + row, x, ACS_VLINE);
    attroff(A_DIM | COLOR_PAIR(1));

    int thumb_height = total <= 0 ? safe_height
                                  : std::max(1, safe_height * std::min(visible, total) / total);
    int max_offset = std::max(0, total - visible);
    int thumb_offset = max_offset == 0 ? 0 : (safe_height - thumb_height) * offset / max_offset;
    attron(A_BOLD | COLOR_PAIR(1));
    for (int row = 0; row < thumb_height; ++row) {
        mvaddch(y + thumb_offset + row, x, ACS_CKBOARD);
    }
    attroff(A_BOLD | COLOR_PAIR(1));
}

inline Layout makeLayout() {
    Layout layout;
    getmaxyx(stdscr, layout.height, layout.width);
    if (layout.height < 24 || layout.width < 70) return layout;

    layout.bottom_rows = 5;
    layout.bottom_top = layout.height - (layout.bottom_rows + 4);
    layout.bottom_entry_y = layout.bottom_top + 3;
    layout.main_bottom = layout.bottom_top - 1;
    layout.left_divider = std::clamp(layout.width / 3, 27, 34);
    layout.right_start = layout.left_divider + 2;
    layout.right_end = layout.width - 2;
    layout.identity_separator = std::min(7, layout.main_bottom - 6);
    layout.drive_row_height = layout.height >= 34 ? 3 : 2;
    int help_lines = layout.height >= 30 ? 1 : 0;
    layout.drive_indicator_y = layout.main_bottom - help_lines - 1;
    int drive_space = layout.drive_indicator_y - layout.drive_start;
    layout.visible_drives = std::clamp(drive_space / layout.drive_row_height, 1, 5);
    layout.drive_viewport_height = drive_space;
    int gaps = std::max(1, layout.visible_drives - 1);
    int spare_rows = std::max(0, drive_space - layout.visible_drives * layout.drive_row_height);
    int gap_rows = layout.visible_drives > 1 ? std::min(2, spare_rows / gaps) : 0;
    layout.drive_row_stride = layout.drive_row_height + gap_rows;
    int used_rows = layout.visible_drives * layout.drive_row_height +
                    std::max(0, layout.visible_drives - 1) * gap_rows;
    layout.drive_first_y = layout.drive_start + std::max(0, (drive_space - used_rows) / 2);
    layout.valid = layout.identity_separator > layout.main_top + 2 &&
                   layout.right_end - layout.right_start >= 30;
    return layout;
}


inline void drawFrame(const Layout& layout) {
    erase();
    box(stdscr, 0, 0);
    mvhline(2, 1, ACS_HLINE, layout.width - 2);
    mvhline(layout.bottom_top, 1, ACS_HLINE, layout.width - 2);
    mvvline(layout.main_top, layout.left_divider, ACS_VLINE,
            std::max(1, layout.bottom_top - layout.main_top));
    mvhline(layout.identity_separator, layout.left_divider + 1, ACS_HLINE,
            layout.width - layout.left_divider - 2);
    mvaddch(2, 0, ACS_LTEE);
    mvaddch(2, layout.width - 1, ACS_RTEE);
    mvaddch(2, layout.left_divider, ACS_TTEE);
    mvaddch(layout.bottom_top, 0, ACS_LTEE);
    mvaddch(layout.bottom_top, layout.width - 1, ACS_RTEE);
    mvaddch(layout.bottom_top, layout.left_divider, ACS_BTEE);
    mvaddch(layout.identity_separator, layout.left_divider, ACS_LTEE);
    mvaddch(layout.identity_separator, layout.width - 1, ACS_RTEE);
}


}  // namespace drivelab::tui
