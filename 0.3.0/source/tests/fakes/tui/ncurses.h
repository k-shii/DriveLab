#pragma once

// Private terminal double. No native curses symbols or terminal/device access.
#include <algorithm>
#include <deque>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using chtype = unsigned long;
using mmask_t = unsigned long;
struct WINDOW { int height = 34, width = 100; };
struct MEVENT { short id = 0; int x = 0, y = 0, z = 0; mmask_t bstate = 0; };
inline WINDOW terminal_window;
inline WINDOW* stdscr = &terminal_window;
#define LINES (stdscr->height)
#define COLS (stdscr->width)
#define getmaxyx(w,y,x) ((y)=(w)->height,(x)=(w)->width)
#define COLOR_PAIR(n) ((n)<<8)
inline constexpr int OK=0, ERR=-1, TRUE=1;
inline constexpr int COLOR_BLACK=0, COLOR_RED=1, COLOR_GREEN=2, COLOR_YELLOW=3,
                     COLOR_BLUE=4, COLOR_CYAN=6, COLOR_WHITE=7;
inline constexpr int ACS_ULCORNER='+', ACS_URCORNER='+', ACS_LLCORNER='+', ACS_LRCORNER='+';
inline constexpr int A_BOLD=1,A_DIM=2,A_REVERSE=4,A_UNDERLINE=8;
inline constexpr int ACS_HLINE='-',ACS_VLINE='|',ACS_LTEE='+',ACS_RTEE='+',
                     ACS_TTEE='+',ACS_BTEE='+',ACS_CKBOARD='#';
inline constexpr int KEY_UP=257,KEY_DOWN=258,KEY_LEFT=259,KEY_RIGHT=260,
                     KEY_ENTER=261,KEY_BTAB=262,KEY_PPAGE=263,KEY_NPAGE=264,
                     KEY_RESIZE=265,KEY_MOUSE=266;
inline constexpr mmask_t BUTTON1_PRESSED=1, BUTTON1_RELEASED=2,
                         BUTTON4_PRESSED=4, REPORT_MOUSE_POSITION=16;
#define BUTTON5_PRESSED 8UL

namespace fake_tui {
inline std::vector<std::string> screen;
inline std::deque<std::function<int()>> inputs;
inline MEVENT mouse;
inline int frames = 0;
inline std::string text() {
    std::string out;
    for (const auto& row : screen) out += row + "\n";
    return out;
}
inline void reset(int height=34,int width=100) {
    terminal_window={height,width};
    screen.assign(static_cast<std::size_t>(height),std::string(static_cast<std::size_t>(width),' '));
    inputs.clear(); frames=0;
}
inline void key(int value) { inputs.push_back([value]{return value;}); }
inline void click(int y,int x,mmask_t mask) {
    inputs.push_back([=]{mouse={0,x,y,0,mask}; return KEY_MOUSE;});
}
}
inline int erase() {
    fake_tui::screen.assign(static_cast<std::size_t>(LINES),std::string(static_cast<std::size_t>(COLS),' '));
    return OK;
}
inline int attron(int) { return OK; }
inline int attroff(int) { return OK; }
inline int mvaddch(int y,int x,chtype c) {
    if (y<0 || y>=LINES || x<0 || x>=COLS) throw std::runtime_error("Out-of-bounds terminal write");
    fake_tui::screen[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]=static_cast<char>(c);
    return OK;
}
inline int mvaddnstr(int y,int x,const char* text,int width) {
    for (int i=0; i<width && text[i]; ++i) mvaddch(y,x+i,static_cast<chtype>(text[i]));
    return OK;
}
inline int mvhline(int y,int x,chtype c,int width) {
    for (int i=0;i<width;++i) mvaddch(y,x+i,c);
    return OK;
}
inline int mvvline(int y,int x,chtype c,int height) {
    for (int i=0;i<height;++i) mvaddch(y+i,x,c);
    return OK;
}
inline int box(WINDOW*,chtype,chtype) {
    mvhline(0,0,'-',COLS); mvhline(LINES-1,0,'-',COLS);
    mvvline(0,0,'|',LINES); mvvline(0,COLS-1,'|',LINES);
    return OK;
}
inline WINDOW* initscr() { return stdscr; }
inline int endwin() { return OK; }
inline int cbreak() { return OK; }
inline int noecho() { return OK; }
inline int keypad(WINDOW*,bool) { return OK; }
inline int curs_set(int) { return OK; }
inline bool has_colors() { return true; }
inline int start_color() { return OK; }
inline int use_default_colors() { return OK; }
inline int init_pair(short,short,short) { return OK; }
inline int refresh() { ++fake_tui::frames; return OK; }
inline int wnoutrefresh(WINDOW*) { return OK; }
inline int doupdate() { ++fake_tui::frames; return OK; }
inline mmask_t mousemask(mmask_t value,mmask_t*) { return value; }
inline int mouseinterval(int) { return OK; }
inline void timeout(int) {}
inline int getmouse(MEVENT* out) { *out=fake_tui::mouse; return OK; }
inline int getch() {
    if (fake_tui::inputs.empty()) throw std::runtime_error("Unexpected terminal input request");
    const auto result = fake_tui::inputs.front()();
    if (result != ERR) fake_tui::inputs.pop_front();
    return result;
}
