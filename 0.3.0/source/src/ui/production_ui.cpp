#include "ui/production_ui.h"
#include "app/core_application.h"
#include "ui/production_read_model.h"
#include "ui/tui_layout.h"
#include "core/version.h"

#include <algorithm>
#include <ctime>
#include <optional>
#include <utility>
#include <vector>

namespace drivelab::tui {
namespace {

#ifdef BUTTON5_PRESSED
constexpr mmask_t kWheelDown = BUTTON5_PRESSED;
#else
constexpr mmask_t kWheelDown = 0;
#endif

enum class Focus { Scan, Drive, Overview, Details, Events, Jobs, Settings, Exit,
                   DriveUp, DriveDown, EventUp, EventDown, ContentUp, ContentDown,
                   Confirm, Cancel, Close };
enum class Mode { List, Tabs, Panel };
enum class Modal { None, Exit, Settings };

struct Hit {
    int y, x, height, width;
    Focus focus;
    int index = 0;
    friend bool operator==(const Hit&, const Hit&) = default;
};
struct State {
    ProductionReadModel model;
    Focus focus = Focus::Scan;
    Mode mode = Mode::List;
    Modal modal = Modal::None;
    Focus modal_return_focus = Focus::Scan;
    Mode modal_return_mode = Mode::List;
    bool cancel = true, detailed = false, jobs = false;
    int cursor = 0, drive_offset = 0, content_offset = 0, event_offset = 0;
    int content_lines = 0;
    std::vector<std::string> events;
    std::optional<Hit> hover;
    explicit State(CoreApplication& app) : model(app) {}
};

int statusColor(DriveStatus status) {
    if (status == DriveStatus::Ready) return 3;
    if (status == DriveStatus::Busy) return 1;
    return 5;
}
int count(const State& s) { return static_cast<int>(s.model.snapshot().drives.size()); }

std::vector<std::string> wrapped(const std::vector<std::string>& lines, int width) {
    std::vector<std::string> result;
    const auto limit = static_cast<std::size_t>(std::max(1, width));
    for (auto line : lines) {
        while (line.size() > limit) {
            auto cut = line.rfind(' ', limit);
            if (cut == std::string::npos || cut == 0) cut = limit;
            result.push_back(line.substr(0, cut));
            line.erase(0, cut);
            if (!line.empty() && line.front() == ' ') line.erase(0, 1);
        }
        result.push_back(std::move(line));
    }
    return result;
}

void event(State& s, const std::string& message) {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[16]{};
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
    s.events.insert(s.events.begin(), std::string(stamp) + "  " + presentationText(message));
    if (s.events.size() > 100) s.events.resize(100);
    s.event_offset = 0;
}

void rescan(State& s) {
    const auto result = s.model.startScan();
    s.cursor = static_cast<int>(s.model.selectedIndex().value_or(0));
    s.drive_offset = std::max(0, s.cursor - 1);
    s.content_offset = 0;
    if (!s.model.selected()) { s.mode = Mode::List; s.focus = Focus::Scan; }
    if (!result) event(s, "Scan failed: " + result.error().message);
    else if (s.model.snapshot().phase == ProductionScanPhase::Complete)
        event(s, "Scan complete: " + std::to_string(count(s)) + " physical devices.");
    else if (s.model.snapshot().scan_error) event(s, "Scan failed: " + s.model.snapshot().scan_error->message);
    else event(s, "Scanning inventory, then ownership and safety proof.");
    if (!s.model.selectionNotice().empty()) event(s, s.model.selectionNotice());
}

void scanWithNotice(State& s, const Layout& layout) {
    if (layout.valid) {
        printClipped(layout.bottom_entry_y, 2, layout.width - 4,
                     "Scanning current storage and ownership... ", A_BOLD | COLOR_PAIR(5));
        wnoutrefresh(stdscr);
        doupdate();
    }
    rescan(s);
}

void button(State& s, std::vector<Hit>& hits, int y, int x, const std::string& label,
            Focus focus, bool selected = false) {
    const std::string text = "[ " + label + " ]";
    if (x + static_cast<int>(text.size()) >= COLS) return;
    const bool focused = s.focus == focus;
    const bool hovered = s.hover && s.hover->focus == focus;
    int attributes = A_BOLD | COLOR_PAIR(1);
    if (selected || focused) attributes |= A_REVERSE;
    if (focused) attributes |= A_UNDERLINE;
    if (hovered && !focused) attributes |= A_DIM;
    printClipped(y, x, static_cast<int>(text.size()), text, attributes);
    hits.push_back({y,x,1,static_cast<int>(text.size()),focus,0});
}

std::vector<Hit> draw(State& s, Layout& layout) {
    layout = makeLayout();
    std::vector<Hit> hits;
    if (!layout.valid) {
        erase();
        printClipped(0,0,COLS,"DriveLab: resize to at least 70 columns x 24 rows.");
        printClipped(1,0,COLS,"R rescan. Q/q opens exit confirmation; Esc cancels.");
        if (s.modal == Modal::Exit)
            printClipped(2,0,COLS,s.cancel ? "[ Exit ]  [> Cancel <]" : "[> Exit <]  [ Cancel ]");
        if (s.modal == Modal::Settings)
            printClipped(2,0,COLS,"Settings unavailable. Enter/Esc closes.");
        return hits;
    }
    drawFrame(layout);
    const std::string version = "v" + std::string(kVersion);
    const int version_x = (layout.width - static_cast<int>(version.size())) / 2;
    printClipped(1,2,version_x-3,"DriveLab // live",A_BOLD | COLOR_PAIR(1));
    printClipped(1,version_x,static_cast<int>(version.size()),version,A_BOLD | COLOR_PAIR(5));
    button(s,hits,1,layout.width-24,"Settings",Focus::Settings);
    button(s,hits,1,layout.width-11,"q Exit",Focus::Exit);
    printClipped(4,2,8,"Drives",A_BOLD);
    button(s,hits,4,layout.left_divider-13,"R Scan",Focus::Scan);
    mvhline(5,1,ACS_HLINE,layout.left_divider-1);

    s.cursor = std::clamp(s.cursor,0,std::max(0,count(s)-1));
    s.drive_offset = std::clamp(s.drive_offset,0,std::max(0,count(s)-layout.visible_drives));
    for (int row = 0; row < layout.visible_drives && row+s.drive_offset < count(s); ++row) {
        const int index = row+s.drive_offset;
        const auto& drive = s.model.snapshot().drives[static_cast<std::size_t>(index)];
        const int y = layout.drive_first_y + row*layout.drive_row_stride;
        const int width = layout.left_divider-4;
        const bool selected = s.model.selectedIndex() == static_cast<std::size_t>(index);
        const bool focused = s.focus == Focus::Drive && s.cursor == index;
        int attributes = selected ? A_REVERSE : 0;
        if (focused) attributes |= A_UNDERLINE | A_BOLD;
        const auto size = productionCapacity(drive.observation.capacity_bytes);
        const auto path = presentationText(drive.current_path.value_or(drive.observation.current_path.value_or("Path unavailable")));
        printClipped(y,2,std::max(1,width-static_cast<int>(size.size())-1),path,attributes);
        printRight(y,layout.left_divider-3,size,attributes);
        const auto status = productionStatusLabel(drive);
        printClipped(y+1,2,width,status,attributes | A_BOLD | COLOR_PAIR(statusColor(drive.status)));
        const auto model = presentationText(drive.observation.model.value_or("Unknown model"));
        if (layout.drive_row_height == 3) {
            printClipped(y+2,2,width,model,attributes);
            const auto media = productionMedia(drive.observation);
            if (static_cast<int>(status.size()+media.size())+2 <= width)
                printRight(y+1,layout.left_divider-3,media,attributes | A_DIM);
        } else {
            const int model_x = 3 + static_cast<int>(status.size());
            printClipped(y+1,model_x,layout.left_divider-model_x-3,model,attributes | A_DIM);
        }
        hits.push_back({y,1,layout.drive_row_height,layout.left_divider-2,Focus::Drive,index});
    }
    if (count(s) == 0)
        printClipped(layout.drive_first_y,2,layout.left_divider-4,
                     s.model.snapshot().scan_error ? "Scan unavailable" :
                     s.model.snapshot().phase == ProductionScanPhase::Inventory ? "Scanning inventory..." : "No physical drives",COLOR_PAIR(5));
    const auto range = count(s) == 0 ? "0 drives" :
        std::to_string(s.drive_offset+1)+"-"+std::to_string(std::min(count(s),s.drive_offset+layout.visible_drives))+
        " of "+std::to_string(count(s));
    printClipped(layout.drive_indicator_y,2,layout.left_divider-13,range);
    button(s,hits,layout.drive_indicator_y,layout.left_divider-12,"^",Focus::DriveUp);
    button(s,hits,layout.drive_indicator_y,layout.left_divider-6,"v",Focus::DriveDown);
    drawVerticalScrollbar(layout.drive_first_y,layout.left_divider-1,
                          layout.drive_viewport_height,count(s),layout.visible_drives,s.drive_offset);

    const int x = layout.right_start, width = layout.right_end-x+1;
    if (const auto* drive = s.model.selected()) {
        const auto status = productionStatusLabel(*drive);
        printClipped(4,x,width-static_cast<int>(status.size())-1,
                     presentationText(drive->observation.model.value_or("Unknown model")),A_BOLD);
        printRight(4,layout.right_end,status,A_BOLD | COLOR_PAIR(statusColor(drive->status)));
        printClipped(5,x,width,presentationText(drive->current_path.value_or("Current path unavailable"))+
                     "  "+productionCapacity(drive->observation.capacity_bytes),COLOR_PAIR(1));
        printClipped(6,x,width,"Serial: "+presentationText(drive->observation.serial.value_or("Unavailable")));
        button(s,hits,layout.identity_separator+1,x,"O Overview",Focus::Overview,!s.detailed);
        button(s,hits,layout.identity_separator+1,x+17,"D Evidence",Focus::Details,s.detailed);
    } else {
        printClipped(4,x,width,"Select a physical drive",A_BOLD);
        printClipped(5,x,width,"Live discovery / read-only",COLOR_PAIR(1));
    }
    const int top = layout.identity_separator+3;
    const int visible = std::max(1,layout.main_bottom-top);
    auto lines = s.model.overview(s.detailed);
    if (s.model.snapshot().scan_error)
        lines = {"Scan failed: "+presentationText(s.model.snapshot().scan_error->message),
                 "Previous drive observations were cleared. Press R to retry."};
    lines = wrapped(lines,width-2);
    s.content_lines = static_cast<int>(lines.size());
    s.content_offset = std::clamp(s.content_offset,0,std::max(0,s.content_lines-visible));
    for (int row = 0; row < visible && row+s.content_offset < s.content_lines; ++row)
        printClipped(top+row,x,width-2,lines[static_cast<std::size_t>(row+s.content_offset)]);
    drawVerticalScrollbar(top,layout.right_end,visible,s.content_lines,visible,s.content_offset);
    printClipped(layout.main_bottom,x,width-14,"PgUp/PgDn scroll; Esc back",A_DIM);
    button(s,hits,layout.main_bottom,layout.right_end-11,"^",Focus::ContentUp);
    button(s,hits,layout.main_bottom,layout.right_end-5,"v",Focus::ContentDown);

    button(s,hits,layout.bottom_top+1,2,"E Event Log",Focus::Events,!s.jobs);
    button(s,hits,layout.bottom_top+1,20,"J Job Queue (0)",Focus::Jobs,s.jobs);
    button(s,hits,layout.bottom_top+1,layout.width-14,"^",Focus::EventUp);
    button(s,hits,layout.bottom_top+1,layout.width-8,"v",Focus::EventDown);
    if (s.jobs) printClipped(layout.bottom_entry_y,2,layout.width-4,"No jobs. Storage operations are unavailable in 0.3.");
    else {
        s.event_offset = std::clamp(s.event_offset,0,std::max(0,static_cast<int>(s.events.size())-5));
        for (int row = 0; row < 5 && row+s.event_offset < static_cast<int>(s.events.size()); ++row)
            printClipped(layout.bottom_entry_y+row,2,layout.width-4,s.events[static_cast<std::size_t>(row+s.event_offset)]);
    }
    if (s.modal != Modal::None) {
        hits.clear();
        const int modal_width = std::min(64,layout.width-4);
        const int left = (layout.width-modal_width)/2, y = (layout.height-8)/2;
        for (int row = 0; row < 8; ++row) mvhline(y+row,left,' ',modal_width);
        mvhline(y,left,ACS_HLINE,modal_width);
        mvhline(y+7,left,ACS_HLINE,modal_width);
        const bool exiting = s.modal == Modal::Exit;
        printClipped(y+1,left+2,modal_width-4,exiting ? "Exit DriveLab?" : "Settings",A_BOLD | COLOR_PAIR(1));
        printClipped(y+3,left+2,modal_width-4,exiting
            ? "This closes the local read-only application."
            : "No configurable production options in this checkpoint.");
        s.focus = exiting ? (s.cancel ? Focus::Cancel : Focus::Confirm) : Focus::Close;
        if (exiting) {
            button(s,hits,y+5,left+modal_width-24,"Exit",Focus::Confirm);
            button(s,hits,y+5,left+modal_width-13,"Cancel",Focus::Cancel);
        } else button(s,hits,y+5,left+modal_width-12,"Close",Focus::Close);
    }
    return hits;
}

void revealCursor(State& s, const Layout& layout) {
    s.cursor = std::clamp(s.cursor,0,std::max(0,count(s)-1));
    if (s.cursor < s.drive_offset) s.drive_offset = s.cursor;
    if (s.cursor >= s.drive_offset+layout.visible_drives)
        s.drive_offset = std::max(0,s.cursor-layout.visible_drives+1);
}

bool activate(State& s, Focus focus, int index, const Layout& layout) {
    switch (focus) {
        case Focus::Scan: scanWithNotice(s,layout); break;
        case Focus::Drive:
            if (s.model.select(static_cast<std::size_t>(index))) {
                s.cursor=index; s.mode=Mode::Tabs; s.focus=Focus::Overview;
                s.detailed=false; s.content_offset=0;
            }
            break;
        case Focus::Overview: case Focus::Details:
            s.detailed=focus==Focus::Details; s.content_offset=0;
            s.mode=Mode::Panel; s.focus=focus; break;
        case Focus::Events: s.jobs=false; s.focus=focus; s.mode=Mode::List; break;
        case Focus::Jobs: s.jobs=true; s.focus=focus; s.mode=Mode::List; break;
        case Focus::Settings: case Focus::Exit:
            s.modal_return_focus=s.focus; s.modal_return_mode=s.mode;
            s.modal=focus==Focus::Exit?Modal::Exit:Modal::Settings;
            s.cancel=true; break;
        case Focus::Confirm: return true;
        case Focus::Cancel: case Focus::Close:
            s.modal=Modal::None; s.focus=s.modal_return_focus; s.mode=s.modal_return_mode; break;
        case Focus::DriveUp: case Focus::DriveDown:
            s.drive_offset += focus==Focus::DriveUp ? -1 : 1; break;
        case Focus::EventUp: case Focus::EventDown:
            s.event_offset += focus==Focus::EventUp ? -1 : 1; break;
        case Focus::ContentUp: case Focus::ContentDown:
            s.content_offset += focus==Focus::ContentUp ? -1 : 1; break;
    }
    return false;
}

void cycleFocus(State& s, int delta, const Layout& layout) {
    if (s.mode != Mode::List && s.model.selected()) {
        if (s.mode == Mode::Panel) s.content_offset += delta;
        else { s.detailed=!s.detailed; s.focus=s.detailed?Focus::Details:Focus::Overview; s.content_offset=0; }
        return;
    }
    std::vector<std::pair<Focus,int>> targets{{Focus::Scan,0}};
    for (int i=s.drive_offset; i<std::min(count(s),s.drive_offset+layout.visible_drives); ++i)
        targets.emplace_back(Focus::Drive,i);
    for (const auto focus : {Focus::Events,Focus::Jobs,Focus::Settings,Focus::Exit})
        targets.emplace_back(focus,0);
    auto it = std::find(targets.begin(),targets.end(),std::pair{s.focus,s.focus==Focus::Drive?s.cursor:0});
    int index = it==targets.end() ? 0 : static_cast<int>(it-targets.begin());
    index=(index+delta+static_cast<int>(targets.size()))%static_cast<int>(targets.size());
    s.focus=targets[static_cast<std::size_t>(index)].first;
    if (s.focus==Focus::Drive) s.cursor=targets[static_cast<std::size_t>(index)].second;
}

bool key(State& s, int input, const Layout& layout) {
    if (s.modal != Modal::None) {
        if (input==27) return activate(s,Focus::Cancel,0,layout);
        if (s.modal==Modal::Settings)
            return input=='\n' || input==KEY_ENTER ? activate(s,Focus::Close,0,layout) : false;
        if (input=='\t' || input==KEY_BTAB || input==KEY_LEFT || input==KEY_RIGHT) s.cancel=!s.cancel;
        if (input=='\n' || input==KEY_ENTER) return activate(s,s.cancel?Focus::Cancel:Focus::Confirm,0,layout);
        return false;
    }
    if (input=='q' || input=='Q') return activate(s,Focus::Exit,0,layout);
    if (input=='r' || input=='R') return activate(s,Focus::Scan,0,layout);
    if (input=='e' || input=='E') return activate(s,Focus::Events,0,layout);
    if (input=='j' || input=='J') return activate(s,Focus::Jobs,0,layout);
    if ((input=='o' || input=='O' || input=='d' || input=='D') && s.model.selected())
        return activate(s,(input=='d' || input=='D')?Focus::Details:Focus::Overview,0,layout);
    if (input=='\t' || input==KEY_BTAB) cycleFocus(s,input=='\t'?1:-1,layout);
    else if (input==27) {
        if (s.mode==Mode::Panel) { s.mode=Mode::Tabs; s.focus=s.detailed?Focus::Details:Focus::Overview; }
        else { s.mode=Mode::List; s.focus=s.model.selected()?Focus::Drive:Focus::Scan;
               s.cursor=static_cast<int>(s.model.selectedIndex().value_or(0)); revealCursor(s,layout); }
    } else if (input==KEY_PPAGE || input==KEY_NPAGE) s.content_offset += input==KEY_PPAGE?-5:5;
    else if (input==KEY_UP || input==KEY_DOWN) {
        const int delta=input==KEY_UP?-1:1;
        if (s.mode==Mode::Panel) s.content_offset+=delta;
        else if (s.focus==Focus::Events) s.event_offset+=delta;
        else if (s.mode==Mode::List && count(s)>0) {
            if (s.focus==Focus::Drive) s.cursor+=delta;
            s.focus=Focus::Drive; revealCursor(s,layout);
        }
    } else if ((input==KEY_LEFT || input==KEY_RIGHT) && s.mode==Mode::Tabs)
        cycleFocus(s,input==KEY_LEFT?-1:1,layout);
    else if (input=='\n' || input==KEY_ENTER) {
        if (s.mode==Mode::Panel) return false;
        return activate(s,s.focus,s.cursor,layout);
    }
    return false;
}

std::optional<Hit> hitAt(const std::vector<Hit>& hits, int y, int x) {
    for (const auto& hit : hits)
        if (y>=hit.y && y<hit.y+hit.height && x>=hit.x && x<hit.x+hit.width) return hit;
    return std::nullopt;
}
}  // namespace

int runProductionUi(CoreApplication& application) {
    State state(application);
    Layout layout;
    auto hits=draw(state,layout);
    wnoutrefresh(stdscr); doupdate();
    scanWithNotice(state,layout);
    mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON4_PRESSED | kWheelDown | REPORT_MOUSE_POSITION, nullptr);
    mouseinterval(0);
    timeout(100);
    // Ncurses stays on this thread; the Core worker publishes immutable snapshots.
    struct CancelOnExit { CoreApplication& app; ~CancelOnExit() { app.cancelProductionScan(); } } cancel{application};
    std::optional<Hit> pressed;
    bool done=false;
    while (!done) {
        const auto previous = state.model.snapshot().phase;
        (void)state.model.refresh();
        const auto phase = state.model.snapshot().phase;
        if (phase != previous) {
            if (phase == ProductionScanPhase::Complete)
                event(state, "Scan complete: " + std::to_string(count(state)) + " physical devices.");
            if (phase == ProductionScanPhase::Failed)
                event(state, "Scan failed: " + state.model.snapshot().scan_error->message);
            if (!state.model.selectionNotice().empty()) event(state, state.model.selectionNotice());
            if (phase == ProductionScanPhase::Failed || !state.model.selectionNotice().empty()) {
                state.mode=Mode::List; state.focus=Focus::Scan;
            }
        }
        hits=draw(state,layout);
        wnoutrefresh(stdscr); doupdate();
        const int input=getch();
        if (input==ERR) continue;
        if (input==KEY_RESIZE) { pressed.reset(); state.hover.reset(); continue; }
        if (input!=KEY_MOUSE) { pressed.reset(); done=key(state,input,layout); continue; }
        MEVENT mouse{};
        if (getmouse(&mouse)!=OK) continue;
        const auto hit=hitAt(hits,mouse.y,mouse.x);
        state.hover=hit;
        if (mouse.bstate & BUTTON1_PRESSED) pressed=hit;
        if (mouse.bstate & BUTTON1_RELEASED) {
            if (pressed && hit && *pressed==*hit) done=activate(state,hit->focus,hit->index,layout);
            pressed.reset();
        }
        if (state.modal==Modal::None && (mouse.bstate & (BUTTON4_PRESSED | kWheelDown))) {
            const int delta=mouse.bstate & BUTTON4_PRESSED ? -1 : 1;
            if (mouse.y>=layout.bottom_top) state.event_offset+=delta;
            else if (mouse.x<layout.left_divider) state.drive_offset+=delta;
            else state.content_offset+=delta;
        }
    }
    return 0;
}

}  // namespace drivelab::tui
