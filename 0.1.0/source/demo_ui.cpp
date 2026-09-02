#include "demo_ui.h"

#include <ncurses.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kSelectedPair = 6;
constexpr int kActiveTabPair = 7;
constexpr int kTextPair = 8;

struct DemoDrive {
    std::string path;
    std::string size;
    std::string model;
    std::string serial;
    std::string media;
    std::string state;
    std::string smart;
    std::string transport;
    std::string temperature;
    std::string power_hours;
    std::string reallocated;
    std::string pending;
    std::string offline_unc;
    std::string ata;
    std::string hpa;
    std::string security;
    std::string protection_reason;
};

struct DemoJob {
    int id = 0;
    std::string time;
    std::string target;
    std::string task;
    std::string state;
    int progress = 0;
    std::string eta;
};

enum class HitKind {
    None,
    SelectDrive,
    MainTab,
    BottomTab,
    Scan,
    Settings,
    Detach,
    DriveUp,
    DriveDown,
    BottomUp,
    BottomDown,
    SmartAction,
    AtaAction,
    BenchmarkProfile,
    SanitizeOption,
    OpenWorkflow,
    JobPrimary,
    JobStop,
    ConfirmWorkflow,
    CancelWorkflow,
    ConfirmDetach,
    CancelDetach,
    ConfirmTerminate,
    CancelTerminate,
    CloseSettings
};

enum class InteractionMode {
    DriveList,
    DriveTabs,
    FeaturePanel
};

struct HitRegion {
    int y = 0;
    int x = 0;
    int height = 1;
    int width = 1;
    HitKind kind = HitKind::None;
    int value = 0;
    bool disabled = false;
};

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

struct DemoState {
    std::vector<DemoDrive> drives;
    std::vector<std::string> events;
    std::vector<DemoJob> jobs;
    int selected_drive = 0;
    int drive_offset = 0;
    int main_tab = 0;
    int bottom_tab = 0;
    int event_offset = 0;
    int job_offset = 0;
    std::array<int, 5> content_offsets{};
    InteractionMode mode = InteractionMode::DriveList;
    HitKind focus_kind = HitKind::Scan;
    int focus_value = 0;
    HitKind hover_kind = HitKind::None;
    int hover_value = 0;
    int smart_option = 1;
    int ata_option = 0;
    int smart_test_progress = 38;
    std::string smart_test_name = "Extended Test";
    std::string smart_test_state = "RUNNING";
    std::string smart_test_target = "/dev/sdd";
    int smart_job_id = -1;
    int benchmark_profile = 0;
    int benchmark_progress = 58;
    std::string benchmark_state = "RUNNING";
    int benchmark_job_id = -1;
    int sanitize_option = 0;
    int next_job_id = 105;
    bool confirm_detach = false;
    bool confirm_terminate = false;
    bool show_settings = false;
    bool show_workflow = false;
    int workflow_choice = 1;
    int detach_choice = 1;
    int terminate_choice = 1;
    InteractionMode modal_return_mode = InteractionMode::DriveList;
    HitKind modal_return_focus_kind = HitKind::Scan;
    int modal_return_focus_value = 0;
    Clock::time_point last_job_tick = Clock::now();
};

const std::vector<std::string> kMainTabs = {
    "Overview", "S.M.A.R.T", "ATA / HPA", "Benchmark", "Sanitize"
};

const std::vector<std::string> kMainTabShortcutLabels = {
    "O . Overview", "S . S.M.A.R.T", "A . ATA / HPA", "B . Benchmark",
    "N . Sanitize"
};

const std::vector<std::string> kBenchmarkProfiles = {
    "Quick Sequential", "HDD Zone Characterization", "Random 4K QD1",
    "Random 4K QD32", "Latency / ioping"
};

const std::vector<std::string> kSmartOptions = {
    "Short Test", "Extended Test", "Conveyance Test", "Raw S.M.A.R.T Data"
};

const std::vector<std::string> kAtaOptions = {
    "Security State", "HPA Inspect", "DCO Inspect", "Secure Erase", "Enhanced Secure Erase"
};

const std::vector<std::string> kSanitizeOptions = {
    "ATA Enhanced Secure Erase", "ATA Secure Erase", "nwipe Zero Fill",
    "nwipe One Fill", "nwipe Random Fill"
};

const DemoState* g_render_state = nullptr;

std::string fit(std::string text, int width) {
    if (width <= 0) return "";
    if (static_cast<int>(text.size()) <= width) return text;
    if (width <= 3) return text.substr(0, width);
    return text.substr(0, width - 3) + "...";
}

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char value[16];
    std::strftime(value, sizeof(value), "%H:%M:%S", &local);
    return value;
}

void addEvent(DemoState& state, const std::string& message) {
    state.events.insert(state.events.begin(), timestamp() + " " + message);
    if (state.events.size() > 50) state.events.resize(50);
    state.event_offset = 0;
}

bool isProtected(const DemoDrive& drive) {
    return drive.state == "PROTECTED";
}

int statusPair(const std::string& state) {
    if (state == "READY" || state == "COMPLETED" || state == "PASSED") return 3;
    if (state == "FAILING" || state == "FAILED" || state == "CANCELLED") return 4;
    if (state == "PROTECTED" || state == "DEGRADED" || state == "PAUSED") return 5;
    return 1;
}

void printClipped(int y, int x, int width, const std::string& text, int attributes = 0) {
    if (y < 0 || y >= LINES || x < 0 || x >= COLS || width <= 0) return;
    int safe_width = std::min(width, COLS - x);
    attron(attributes);
    mvaddnstr(y, x, text.c_str(), safe_width);
    attroff(attributes);
}

void printRight(int y, int right, const std::string& text, int attributes = 0) {
    int x = std::max(1, right - static_cast<int>(text.size()) + 1);
    printClipped(y, x, right - x + 1, text, attributes);
}

int buttonWidth(const std::string& label) {
    return static_cast<int>(label.size()) + 4;
}

int drawButton(std::vector<HitRegion>& hits, int y, int x, const std::string& label,
               bool active, HitKind kind, int value = 0, bool disabled = false) {
    std::string text = "[ " + label + " ]";
    if (x < 1 || x + static_cast<int>(text.size()) >= COLS) return 0;
    bool focused = g_render_state && g_render_state->focus_kind == kind &&
                   g_render_state->focus_value == value;
    bool hovered = g_render_state && g_render_state->hover_kind == kind &&
                   g_render_state->hover_value == value;
    bool highlighted = active || focused;
    int attributes = A_BOLD | COLOR_PAIR(highlighted ? kActiveTabPair : 2);
    if (highlighted) attributes |= A_REVERSE;
    if (focused) attributes |= A_UNDERLINE;
    if (hovered && !focused) attributes |= A_DIM;
    if (disabled) attributes = A_DIM | COLOR_PAIR(2);
    printClipped(y, x, static_cast<int>(text.size()), text, attributes);
    if (kind != HitKind::None) {
        hits.push_back({y, x, 1, static_cast<int>(text.size()), kind, value, disabled});
    }
    return static_cast<int>(text.size());
}

void drawVerticalScrollbar(int y, int x, int height, int total, int visible, int offset) {
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

Layout makeLayout() {
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

DemoState makeDemoState() {
    DemoState state;
    state.drives = {
        {"/dev/sdd", "596.2 GiB", "WDC WD6400AAKS-75A7B0", "WD-WMASY1520445", "HDD", "READY", "PASSED",
         "SATA 3 Gb/s", "35 C", "37,256 h", "0", "0", "0", "Supported - Disabled", "Disabled - native max exposed", "Not Frozen", ""},
        {"/dev/sde", "3.64 TiB", "HGST HUS724040ALA640", "PK1331PAG7JBKS", "HDD", "READY", "PASSED",
         "SAS 6 Gb/s", "31 C", "18,204 h", "0", "0", "0", "Supported", "Not applicable", "Unlocked", ""},
        {"/dev/sda", "1.82 TiB", "ST2000DM001-1E6164", "Z1E6A91R", "HDD", "PROTECTED", "PASSED",
         "SATA 6 Gb/s", "33 C", "42,891 h", "0", "0", "0", "Status only", "Status only", "Not exposed",
         "System disk contains /, /boot, /home, and active swap"},
        {"/dev/sdb", "223.6 GiB", "KINGSTON SA400S37240G", "50026B7784A9D221", "SSD", "PROTECTED", "PASSED",
         "SATA 6 Gb/s", "29 C", "9,842 h", "0", "0", "0", "Status only", "Status only", "Not exposed",
         "Passed through as a raw disk to Proxmox VM 104"},
        {"/dev/sdc", "7.28 TiB", "WDC WD80EFZZ-68BTXN0", "VH01KJ9G", "HDD", "PROTECTED", "PASSED",
         "SATA 6 Gb/s", "36 C", "22,106 h", "0", "0", "0", "Status only", "Status only", "Not exposed",
         "Active member of imported ZFS pool tank"},
        {"/dev/nvme0n1", "1.82 TiB", "Samsung SSD 980 PRO", "S69ENF0R912345", "NVMe", "FAILING", "FAILED",
         "PCIe 4.0 x4", "57 C", "16,772 h", "18", "7", "3", "Not applicable", "Not applicable", "Not applicable", ""},
        {"/dev/sdf", "931.5 GiB", "Seagate BarraCuda ST1000DM010", "Z9A4DEMO", "HDD", "DEGRADED", "PASSED",
         "SATA 6 Gb/s", "42 C", "31,449 h", "12", "0", "0", "Supported - Disabled", "Disabled", "Not Frozen", ""}
    };

    std::string now = timestamp();
    state.events = {
        now + " Demo inventory loaded: 7 canned drives; no device commands executed",
        now + " Scheduler policy: one active job per physical drive; cross-drive concurrency allowed",
        now + " DriveLab mock backend initialized",
        now + " Protected-drive policy loaded: action tabs hidden",
        now + " Canned S.M.A.R.T, ATA/HPA and benchmark fixtures ready",
        now + " Mouse navigation and panel scrolling enabled",
        now + " Demo job history restored"
    };
    state.jobs = {
        {104, now, "/dev/sde", "Sequential read demo", "RUNNING", 63, "00:09"},
        {103, now, "/dev/sde", "S.M.A.R.T long demo", "QUEUED", 0, "--"},
        {102, now, "/dev/sdd", "Random read demo", "PAUSED", 41, "00:15"},
        {101, now, "/dev/sdf", "Surface scan demo", "COMPLETED", 100, "done"},
        {100, now, "/dev/nvme0n1", "S.M.A.R.T check demo", "FAILED", 18, "--"},
        {99, now, "/dev/sdc", "Sanitize demo", "CANCELLED", 8, "--"}
    };
    return state;
}

void normalizeDriveViewport(DemoState& state, int visible) {
    state.selected_drive = std::clamp(state.selected_drive, 0, static_cast<int>(state.drives.size()) - 1);
    int max_offset = std::max(0, static_cast<int>(state.drives.size()) - visible);
    state.drive_offset = std::clamp(state.drive_offset, 0, max_offset);
    if (state.mode == InteractionMode::DriveList && state.focus_kind == HitKind::SelectDrive) {
        state.focus_value = std::clamp(state.focus_value, 0,
                                       static_cast<int>(state.drives.size()) - 1);
        if (state.focus_value < state.drive_offset) state.drive_offset = state.focus_value;
        if (state.focus_value >= state.drive_offset + visible) {
            state.drive_offset = state.focus_value - visible + 1;
        }
    }
    if (isProtected(state.drives[state.selected_drive]) && state.main_tab > 1) state.main_tab = 0;
}

void drawFrame(const Layout& layout) {
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

void drawHeader(const Layout& layout, std::vector<HitRegion>& hits) {
    const std::string version = "v0.1.0";
    int version_x = (layout.width - static_cast<int>(version.size())) / 2;
    printClipped(1, 2, layout.width - 4, "DriveLab", A_BOLD | COLOR_PAIR(kTextPair));
    const std::string full_mode = "// storage diagnostics simulator";
    const std::string compact_mode = "// simulator";
    int mode_width = std::max(0, version_x - 13);
    const std::string& mode = static_cast<int>(full_mode.size()) <= mode_width
        ? full_mode : compact_mode;
    printClipped(1, 11, mode_width, mode, COLOR_PAIR(1));

    printClipped(1, version_x, static_cast<int>(version.size()), version,
                 A_BOLD | COLOR_PAIR(5));

    const std::string settings = "Settings";
    const std::string full_session = "tmux: drivelab-demo1  o persistent";
    const std::string compact_session = "demo1  o persistent";
    const std::string tiny_session = "tmux: demo1";
    const std::string full_detach = "Q . Detach UI";
    const std::string compact_detach = "Q . Detach";
    const std::string plain_detach = "Detach UI";
    int version_right = version_x + static_cast<int>(version.size()) + 2;
    auto leavesSessionRoom = [&](const std::string& detach_label,
                                 const std::string& session_label) {
        int candidate_detach_x = layout.width - buttonWidth(detach_label) - 2;
        int candidate_settings_x = candidate_detach_x - buttonWidth(settings) - 1;
        int session_right = candidate_settings_x - 2;
        return session_right - static_cast<int>(session_label.size()) + 1 > version_right;
    };

    std::string detach = compact_detach;
    if (leavesSessionRoom(full_detach, compact_session)) detach = full_detach;
    else if (leavesSessionRoom(compact_detach, compact_session)) detach = compact_detach;
    else if (leavesSessionRoom(plain_detach, compact_session)) detach = plain_detach;
    else if (leavesSessionRoom(full_detach, tiny_session)) detach = full_detach;
    else if (leavesSessionRoom(compact_detach, tiny_session)) detach = compact_detach;
    else if (leavesSessionRoom(plain_detach, tiny_session)) detach = plain_detach;

    int detach_x = layout.width - buttonWidth(detach) - 2;
    drawButton(hits, 1, detach_x, detach, false, HitKind::Detach);

    int settings_x = detach_x - buttonWidth(settings) - 1;
    drawButton(hits, 1, settings_x, settings, false, HitKind::Settings);

    int session_right = settings_x - 2;
    std::string session = full_session;
    int session_x = session_right - static_cast<int>(session.size()) + 1;
    if (session_x <= version_right) {
        session = compact_session;
        session_x = session_right - static_cast<int>(session.size()) + 1;
    }
    if (session_x <= version_right) {
        session = tiny_session;
        session_x = session_right - static_cast<int>(session.size()) + 1;
    }
    if (session_x > version_right) {
        printClipped(1, session_x, static_cast<int>(session.size()), session, A_BOLD | COLOR_PAIR(3));
    }
}

void drawDriveRow(const Layout& layout, std::vector<HitRegion>& hits, const DemoState& state,
                  int drive_index, int row) {
    const DemoDrive& drive = state.drives[drive_index];
    bool selected = drive_index == state.selected_drive;
    bool focused = state.focus_kind == HitKind::SelectDrive && state.focus_value == drive_index;
    bool hovered = state.hover_kind == HitKind::SelectDrive && state.hover_value == drive_index;
    bool highlighted = selected || focused;
    int scrollbar_x = layout.left_divider - 1;
    int content_right = scrollbar_x - 3;
    int content_width = std::max(1, content_right - 1);
    if (highlighted) {
        attron(COLOR_PAIR(kSelectedPair));
        for (int y = row; y < row + layout.drive_row_height; ++y) {
            mvhline(y, 1, ' ', layout.left_divider - 1);
        }
        attroff(COLOR_PAIR(kSelectedPair));
    }

    int selected_attr = highlighted ? COLOR_PAIR(kSelectedPair) : 0;
    if (focused) selected_attr |= A_UNDERLINE;
    if (hovered && !focused) selected_attr |= A_DIM;
    printClipped(row, 2, content_width, drive.path, A_BOLD | selected_attr);
    printRight(row, content_right, drive.size, A_BOLD | selected_attr);

    if (layout.drive_row_height == 3) {
        printClipped(row + 1, 2, content_width, fit(drive.model, content_width), selected_attr | A_DIM);
        printClipped(row + 2, 2, content_width, drive.state,
                     A_BOLD | (highlighted ? selected_attr : COLOR_PAIR(statusPair(drive.state))));
        printRight(row + 2, content_right, drive.media,
                   selected_attr | COLOR_PAIR(highlighted ? kSelectedPair : 1));
    } else {
        std::string detail = drive.state + "  " + drive.model;
        printClipped(row + 1, 2,
                     content_width - static_cast<int>(drive.media.size()) - 2, detail,
                     selected_attr | A_DIM);
        printRight(row + 1, content_right, drive.media, selected_attr);
    }
    hits.push_back({row, 1, layout.drive_row_height, layout.left_divider - 2,
                    HitKind::SelectDrive, drive_index});
}

void drawDrivePanel(const Layout& layout, std::vector<HitRegion>& hits, DemoState& state) {
    normalizeDriveViewport(state, layout.visible_drives);
    printClipped(layout.main_top + 1, 2, layout.left_divider - 3, "Drives", A_BOLD | COLOR_PAIR(kTextPair));
    const std::string scan = "R . Scan";
    int scan_x = layout.left_divider - buttonWidth(scan) - 1;
    drawButton(hits, layout.main_top + 1, scan_x, scan, false, HitKind::Scan);
    mvhline(layout.main_top + 2, 1, ACS_HLINE, layout.left_divider - 1);
    mvaddch(layout.main_top + 2, layout.left_divider, ACS_RTEE);

    for (int visible = 0; visible < layout.visible_drives; ++visible) {
        int drive_index = state.drive_offset + visible;
        if (drive_index >= static_cast<int>(state.drives.size())) break;
        int row = layout.drive_first_y + visible * layout.drive_row_stride;
        drawDriveRow(layout, hits, state, drive_index, row);
    }
    drawVerticalScrollbar(layout.drive_start, layout.left_divider - 1,
                          layout.drive_viewport_height, static_cast<int>(state.drives.size()),
                          layout.visible_drives, state.drive_offset);

    int first = state.drives.empty() ? 0 : state.drive_offset + 1;
    int last = std::min(static_cast<int>(state.drives.size()), state.drive_offset + layout.visible_drives);
    std::ostringstream indicator;
    indicator << first << '-' << last << " of " << state.drives.size() << " . scroll";
    printClipped(layout.drive_indicator_y, 2, layout.left_divider - 12, indicator.str(), COLOR_PAIR(1));
    int down_x = layout.left_divider - buttonWidth("v") - 1;
    int up_x = down_x - buttonWidth("^") - 1;
    drawButton(hits, layout.drive_indicator_y, up_x, "^", false, HitKind::DriveUp);
    drawButton(hits, layout.drive_indicator_y, down_x, "v", false, HitKind::DriveDown);

    if (layout.height >= 30) {
        printClipped(layout.main_bottom, 2, layout.left_divider - 3,
                     "Up/Down Navigate  Enter Select  Tab Controls", COLOR_PAIR(1));
    }
}

void drawIdentity(const Layout& layout, const DemoDrive& drive) {
    int right_width = layout.right_end - layout.right_start + 1;
    std::string smart = "S.M.A.R.T " + drive.smart;
    printClipped(layout.main_top + 1, layout.right_start,
                 right_width - static_cast<int>(smart.size()) - 2,
                 drive.model, A_BOLD | COLOR_PAIR(kTextPair));
    printRight(layout.main_top + 1, layout.right_end, smart,
               A_BOLD | COLOR_PAIR(statusPair(drive.smart)));

    std::string identity = drive.path + " . " + drive.size + " . " + drive.media;
    printClipped(layout.main_top + 2, layout.right_start,
                 right_width - static_cast<int>(drive.serial.size()) - 4,
                 identity, COLOR_PAIR(1));
    printRight(layout.main_top + 2, layout.right_end, drive.serial, A_DIM);

    if (isProtected(drive)) {
        printClipped(layout.main_top + 3, layout.right_start,
                     right_width - static_cast<int>(drive.state.size()) - 3,
                     fit(drive.protection_reason, right_width - 14), COLOR_PAIR(5));
    }
    printRight(layout.main_top + 3, layout.right_end, drive.state,
               A_BOLD | COLOR_PAIR(statusPair(drive.state)));
}

int drawMainTabs(const Layout& layout, std::vector<HitRegion>& hits, DemoState& state,
                 const DemoDrive& drive) {
    int tab_count = isProtected(drive) ? 2 : static_cast<int>(kMainTabs.size());
    state.main_tab = std::clamp(state.main_tab, 0, tab_count - 1);
    int available_width = layout.right_end - layout.right_start + 1;
    bool show_shortcuts = tab_count <= 2 || available_width >= 52;
    const std::vector<std::string>& labels = show_shortcuts
        ? kMainTabShortcutLabels : kMainTabs;
    int y = layout.identity_separator + 1;
    int x = layout.right_start;
    for (int i = 0; i < tab_count; ++i) {
        int width = buttonWidth(labels[i]);
        if (x + width > layout.right_end + 1) {
            ++y;
            x = layout.right_start;
        }
        bool active = state.mode != InteractionMode::DriveList && state.main_tab == i;
        int drawn = drawButton(hits, y, x, labels[i], active, HitKind::MainTab, i);
        x += drawn + 1;
    }
    return y + 2;
}

void printPanel(const Layout& layout, int y, int x, int width, const std::string& text,
                int attributes = 0) {
    if (y < layout.content_top || y > layout.main_bottom) return;
    printClipped(y, x, width, text, attributes);
}

void drawKeyValue(const Layout& layout, int y, int x, int width,
                  const std::string& key, const std::string& value) {
    if (y < layout.content_top || y > layout.main_bottom || width < 10) return;
    printClipped(y, x, width, key, COLOR_PAIR(1));
    printRight(y, x + width - 1, fit(value, std::max(1, width - static_cast<int>(key.size()) - 2)),
               A_BOLD | COLOR_PAIR(kTextPair));
    if (y + 1 >= layout.content_top && y + 1 <= layout.main_bottom) {
        mvhline(y + 1, x, ACS_HLINE, width);
    }
}

void drawOverview(const Layout& layout, const DemoDrive& drive, int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    if (isProtected(drive)) {
        printPanel(layout, content_y, layout.right_start, width, "Protected device", A_BOLD | COLOR_PAIR(5));
        printPanel(layout, content_y + 2, layout.right_start, width, drive.protection_reason, COLOR_PAIR(kTextPair));
        printPanel(layout, content_y + 4, layout.right_start, width,
                   "Status and safely cached identity/S.M.A.R.T data only. Action tabs are hidden.", A_DIM);
        drawKeyValue(layout, content_y + 6, layout.right_start, width, "S.M.A.R.T", drive.smart);
        return;
    }

    bool two_columns = width >= 52;
    int gap = two_columns ? 3 : 0;
    int column_width = two_columns ? (width - gap) / 2 : width;
    int right_x = layout.right_start + column_width + gap;
    drawKeyValue(layout, content_y, layout.right_start, column_width, "Transport", drive.transport);
    drawKeyValue(layout, content_y + 2, layout.right_start, column_width, "Temperature", drive.temperature);
    drawKeyValue(layout, content_y + 4, layout.right_start, column_width, "Pending", drive.pending);
    drawKeyValue(layout, content_y + 6, layout.right_start, column_width, "ATA", drive.ata);
    drawKeyValue(layout, content_y + 8, layout.right_start, column_width, "Security", drive.security);
    if (two_columns) {
        drawKeyValue(layout, content_y, right_x, column_width, "Power-on hours", drive.power_hours);
        drawKeyValue(layout, content_y + 2, right_x, column_width, "Reallocated", drive.reallocated);
        drawKeyValue(layout, content_y + 4, right_x, column_width, "Offline UNC", drive.offline_unc);
        drawKeyValue(layout, content_y + 6, right_x, column_width, "HPA", drive.hpa);
    }
}

struct PanelButton {
    std::string label;
    HitKind kind = HitKind::None;
    int value = 0;
    bool active = false;
    bool disabled = false;
};

int drawPanelButtons(const Layout& layout, std::vector<HitRegion>& hits, int y,
                     const std::vector<PanelButton>& buttons) {
    int x = layout.right_start;
    int row = y;
    for (const PanelButton& button : buttons) {
        int width = buttonWidth(button.label);
        if (x + width > layout.right_end + 1) {
            ++row;
            x = layout.right_start;
        }
        if (row >= layout.content_top && row <= layout.main_bottom) {
            int drawn = drawButton(hits, row, x, button.label, button.active,
                                   button.kind, button.value, button.disabled);
            x += drawn + 1;
        } else {
            x += width + 1;
        }
    }
    return row;
}

void drawProgress(const Layout& layout, int y, const std::string& label,
                  const std::string& state, int progress) {
    int width = layout.right_end - layout.right_start + 1;
    int bar_width = std::clamp(width - static_cast<int>(label.size()) -
                               static_cast<int>(state.size()) - 12, 8, 24);
    int filled = std::clamp(progress * bar_width / 100, 0, bar_width);
    std::ostringstream line;
    line << label << "  " << state << "  [" << std::string(filled, '#')
         << std::string(bar_width - filled, '-') << "] " << std::setw(3) << progress << '%';
    printPanel(layout, y, layout.right_start, width, line.str(),
               A_BOLD | COLOR_PAIR(statusPair(state)));
}

struct ContextInfo {
    std::string title;
    std::string what;
    std::string safety;
    std::string provider;
    std::string use;
    std::string risk;
};

int contextualOption(const DemoState& state, HitKind kind, int selected, int count) {
    int option = selected;
    if (state.hover_kind == kind) option = state.hover_value;
    else if (state.focus_kind == kind) option = state.focus_value;
    return std::clamp(option, 0, count - 1);
}

int drawContextPanel(const Layout& layout, int y, const ContextInfo& info) {
    int width = layout.right_end - layout.right_start + 1;
    if (y >= layout.content_top && y <= layout.main_bottom) {
        mvhline(y, layout.right_start, ACS_HLINE, width);
    }
    printPanel(layout, y + 1, layout.right_start, width, info.title,
               A_BOLD | COLOR_PAIR(kTextPair));
    printPanel(layout, y + 2, layout.right_start, width, "What: " + info.what, COLOR_PAIR(kTextPair));
    int safety_pair = info.safety.find("DESTRUCTIVE") != std::string::npos ? 4 : 3;
    printPanel(layout, y + 3, layout.right_start, width, "Safety: " + info.safety,
               A_BOLD | COLOR_PAIR(safety_pair));
    printPanel(layout, y + 4, layout.right_start, width, "Future provider: " + info.provider, COLOR_PAIR(1));
    printPanel(layout, y + 5, layout.right_start, width, "Typical use: " + info.use, A_DIM);
    printPanel(layout, y + 6, layout.right_start, width, "Risk / limit: " + info.risk, A_DIM);
    return y + 6;
}

ContextInfo smartContext(int option) {
    switch (option) {
        case 0: return {"Short Test", "Firmware electrical/mechanical self-check without scanning all media.",
                        "READ-ONLY", "smartctl self-test adapter", "Fast first-pass health triage.",
                        "May briefly affect latency; support varies by device."};
        case 1: return {"Extended Test", "Drive firmware self-test across the media; it may take hours.",
                        "READ-ONLY", "smartctl self-test adapter", "Deep health validation after quick checks.",
                        "Creates sustained device workload and can be aborted only if supported."};
        case 2: return {"Conveyance Test", "Short firmware test intended to detect shipping damage.",
                        "READ-ONLY", "smartctl self-test adapter", "Recently transported ATA drives.",
                        "Often unsupported on SSD, SAS and NVMe devices."};
        default: return {"Raw S.M.A.R.T Data", "Displays unprocessed attributes, logs and device counters.",
                         "READ-ONLY", "smartctl health adapter", "Vendor-specific diagnosis and support cases.",
                         "Attribute meanings and thresholds vary by model and firmware."};
    }
}

void drawSmartTab(const Layout& layout, std::vector<HitRegion>& hits,
                  const DemoDrive& drive, const DemoState& state, int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    std::ostringstream summary;
    summary << "Health " << drive.smart << " . Temp " << drive.temperature
            << " . Realloc " << drive.reallocated << " . Pending " << drive.pending
            << " . Offline UNC " << drive.offline_unc << " . Power-on " << drive.power_hours;
    printPanel(layout, content_y, layout.right_start, width,
               "S.M.A.R.T health summary", A_BOLD | COLOR_PAIR(kTextPair));
    printPanel(layout, content_y + 1, layout.right_start, width, summary.str(), COLOR_PAIR(1));

    if (isProtected(drive)) {
        ContextInfo info{"Protected device", "Shows safely cached S.M.A.R.T status only.", "STATUS ONLY",
                         "mock health provider", "Confirm why this device is protected.",
                         drive.protection_reason};
        drawContextPanel(layout, content_y + 3, info);
        return;
    }

    int option = contextualOption(state, HitKind::SmartAction, state.smart_option,
                                  static_cast<int>(kSmartOptions.size()));
    std::vector<PanelButton> buttons;
    for (int i = 0; i < static_cast<int>(kSmartOptions.size()); ++i) {
        bool disabled = i == 2 && drive.media != "HDD";
        buttons.push_back({kSmartOptions[i], HitKind::SmartAction, i,
                           state.smart_option == i, disabled});
    }
    int actions_end = drawPanelButtons(layout, hits, content_y + 3, buttons);
    int context_end = drawContextPanel(layout, actions_end + 1, smartContext(option));
    std::string test_label = state.smart_test_name + " on " + state.smart_test_target;
    drawProgress(layout, context_end + 1, test_label, state.smart_test_state,
                 state.smart_test_progress);
    if (context_end + 2 >= layout.content_top && context_end + 2 <= layout.main_bottom) {
        drawButton(hits, context_end + 2, layout.right_start, "Open Workflow",
                   false, HitKind::OpenWorkflow);
    }
}

ContextInfo ataContext(int option) {
    switch (option) {
        case 0: return {"Security State", "Inspects ATA security enabled, locked and frozen flags.", "READ-ONLY",
                        "hdparm security adapter", "Determine whether firmware erase can be authorized.",
                        "USB bridges may hide or misreport ATA security state."};
        case 1: return {"HPA Inspect", "Compares current visible capacity with the native maximum.", "READ-ONLY",
                        "hdparm HPA adapter", "Detect host-protected hidden sectors.",
                        "Capacity changes are intentionally unavailable in this baseline."};
        case 2: return {"DCO Inspect", "Reads Device Configuration Overlay limits and capabilities.", "READ-ONLY",
                        "hdparm DCO identify adapter", "Explain capacity or feature restrictions.",
                        "DCO modification is out of scope; identify only."};
        case 3: return {"Secure Erase", "Requests the drive firmware's standard security erase.", "DESTRUCTIVE",
                        "hdparm erase adapter", "Firmware-native sanitization of ATA media.",
                        "Non-cancellable after acceptance; power loss may leave security enabled."};
        default: return {"Enhanced Secure Erase", "Requests the firmware's enhanced erase procedure.", "DESTRUCTIVE",
                         "hdparm enhanced-erase adapter", "Preferred when supported and duration is acceptable.",
                         "Support and completion reporting are firmware-dependent."};
    }
}

void drawAtaTab(const Layout& layout, std::vector<HitRegion>& hits,
                const DemoDrive& drive, const DemoState& state, int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    std::string frozen = drive.security.find("Not Frozen") != std::string::npos ? "Not Frozen" : "Not Frozen (demo)";
    printPanel(layout, content_y, layout.right_start, width,
               "ATA Security / HPA / DCO", A_BOLD | COLOR_PAIR(kTextPair));
    printPanel(layout, content_y + 1, layout.right_start, width,
               "Security Disabled/Unlocked . " + frozen + " . HPA " + drive.size + " / " + drive.size,
               COLOR_PAIR(1));
    printPanel(layout, content_y + 2, layout.right_start, width,
               "DCO unrestricted . Secure Erase supported (1h42m) . Enhanced supported (1h18m)", A_DIM);

    int option = contextualOption(state, HitKind::AtaAction, state.ata_option,
                                  static_cast<int>(kAtaOptions.size()));
    std::vector<PanelButton> buttons;
    for (int i = 0; i < static_cast<int>(kAtaOptions.size()); ++i) {
        buttons.push_back({kAtaOptions[i], HitKind::AtaAction, i, state.ata_option == i});
    }
    int actions_end = drawPanelButtons(layout, hits, content_y + 4, buttons);
    int context_end = drawContextPanel(layout, actions_end + 1, ataContext(option));
    if (context_end + 1 >= layout.content_top && context_end + 1 <= layout.main_bottom) {
        drawButton(hits, context_end + 1, layout.right_start, "Open Workflow",
                   false, HitKind::OpenWorkflow);
    }
}

ContextInfo benchmarkContext(int option) {
    switch (option) {
        case 0: return {"Quick Sequential", "Samples sequential read throughput over a small safe range.", "READ-ONLY",
                        "fio benchmark adapter", "Fast throughput sanity check.",
                        "A short sample does not describe whole-drive performance."};
        case 1: return {"HDD Zone Characterization", "Reads outer, middle and inner zones separately.", "READ-ONLY",
                        "fio benchmark adapter", "Reveal rotational-media zone performance falloff.",
                        "Longer runtime and sustained workload; HDD-oriented."};
        case 2: return {"Random 4K QD1", "Measures single-queue small-block random reads.", "READ-ONLY",
                        "fio benchmark adapter", "Estimate desktop-like responsiveness.",
                        "Results are sensitive to cache and competing host I/O."};
        case 3: return {"Random 4K QD32", "Measures deep-queue small-block random reads.", "READ-ONLY",
                        "fio benchmark adapter", "Evaluate maximum queued random-read capability.",
                        "Not representative of most interactive workloads."};
        default: return {"Latency / ioping", "Samples request latency and distribution over time.", "READ-ONLY",
                         "ioping latency adapter", "Find stalls, jitter and tail-latency problems.",
                         "Host contention can dominate device latency."};
    }
}

std::string benchmarkResult(int profile) {
    switch (profile) {
        case 0: return "184.6 MiB/s avg . 192.3 MiB/s peak";
        case 1: return "Outer 189 . Middle 162 . Inner 108 MiB/s";
        case 2: return "183 IOPS . p99 24.1 ms";
        case 3: return "1,612 IOPS . p99 18.7 ms";
        case 4: return "avg 7.4 ms . p95 12.8 ms . p99 24.1 ms";
        default: return "No result";
    }
}

void drawBenchmarkTab(const Layout& layout, std::vector<HitRegion>& hits,
                      const DemoState& state, int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    printPanel(layout, content_y, layout.right_start, width,
               "Benchmark profiles", A_BOLD | COLOR_PAIR(kTextPair));
    int option = contextualOption(state, HitKind::BenchmarkProfile, state.benchmark_profile,
                                  static_cast<int>(kBenchmarkProfiles.size()));
    std::vector<PanelButton> profiles;
    for (int i = 0; i < static_cast<int>(kBenchmarkProfiles.size()); ++i) {
        profiles.push_back({kBenchmarkProfiles[i], HitKind::BenchmarkProfile, i,
                            state.benchmark_profile == i});
    }
    int actions_end = drawPanelButtons(layout, hits, content_y + 2, profiles);
    int context_end = drawContextPanel(layout, actions_end + 1, benchmarkContext(option));
    printPanel(layout, context_end + 1, layout.right_start, width,
               "Representative result: " + benchmarkResult(option), A_BOLD | COLOR_PAIR(1));
    drawProgress(layout, context_end + 2, "Simulated run", state.benchmark_state,
                 state.benchmark_progress);
    if (context_end + 3 >= layout.content_top && context_end + 3 <= layout.main_bottom) {
        drawButton(hits, context_end + 3, layout.right_start, "Open Workflow",
                   false, HitKind::OpenWorkflow);
    }
}

ContextInfo sanitizeContext(int option) {
    switch (option) {
        case 0: return {"ATA Enhanced Secure Erase", "Firmware performs its enhanced media sanitization routine.",
                        "DESTRUCTIVE", "hdparm enhanced-erase adapter", "Preferred ATA method when supported.",
                        "Non-cancellable after acceptance; estimated 1h18m on this demo drive."};
        case 1: return {"ATA Secure Erase", "Firmware performs the standard ATA security erase.", "DESTRUCTIVE",
                        "hdparm erase adapter", "Native erase when enhanced mode is unavailable.",
                        "Non-cancellable after acceptance; estimated 1h42m."};
        case 2: return {"nwipe Zero Fill", "Writes zero bytes across every addressable sector.", "DESTRUCTIVE",
                        "nwipe sanitize adapter", "Simple overwrite with easy post-write verification.",
                        "Slow on large disks and may not cover remapped sectors."};
        case 3: return {"nwipe One Fill", "Writes 0xFF bytes across every addressable sector.", "DESTRUCTIVE",
                        "nwipe sanitize adapter", "Alternative deterministic overwrite pattern.",
                        "No security advantage over zero fill for modern drives."};
        default: return {"nwipe Random Fill", "Writes pseudorandom data across every addressable sector.", "DESTRUCTIVE",
                         "nwipe sanitize adapter", "Policy-driven overwrite where random patterns are required.",
                         "Slower to generate/verify and may not cover remapped sectors."};
    }
}

void drawSanitizeTab(const Layout& layout, std::vector<HitRegion>& hits,
                     const DemoDrive&, const DemoState& state, int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    printPanel(layout, content_y, layout.right_start, width,
               "Sanitize methods . Recommended: ATA Enhanced Secure Erase",
               A_BOLD | COLOR_PAIR(kTextPair));
    int option = contextualOption(state, HitKind::SanitizeOption, state.sanitize_option,
                                  static_cast<int>(kSanitizeOptions.size()));
    std::vector<PanelButton> buttons;
    for (int i = 0; i < static_cast<int>(kSanitizeOptions.size()); ++i) {
        buttons.push_back({kSanitizeOptions[i], HitKind::SanitizeOption, i,
                           state.sanitize_option == i});
    }
    int actions_end = drawPanelButtons(layout, hits, content_y + 2, buttons);
    int context_end = drawContextPanel(layout, actions_end + 1, sanitizeContext(option));
    printPanel(layout, context_end + 1, layout.right_start, width,
               "DANGER: DESTRUCTIVE WORKFLOW - DEMO SIMULATION ONLY", A_BOLD | COLOR_PAIR(4));
    if (context_end + 2 >= layout.content_top && context_end + 2 <= layout.main_bottom) {
        drawButton(hits, context_end + 2, layout.right_start, "Open Workflow",
                   false, HitKind::OpenWorkflow);
    }
}

void drawMainContent(const Layout& layout, std::vector<HitRegion>& hits, const DemoState& state,
                     const DemoDrive& drive, int content_y) {
    if (content_y >= layout.main_bottom) return;
    switch (state.main_tab) {
        case 0: drawOverview(layout, drive, content_y); break;
        case 1: drawSmartTab(layout, hits, drive, state, content_y); break;
        case 2: drawAtaTab(layout, hits, drive, state, content_y); break;
        case 3: drawBenchmarkTab(layout, hits, state, content_y); break;
        case 4: drawSanitizeTab(layout, hits, drive, state, content_y); break;
        default: break;
    }
}

int buttonRows(const std::vector<std::string>& labels, int width) {
    int rows = 1;
    int used = 0;
    for (const std::string& label : labels) {
        int item_width = buttonWidth(label);
        if (used > 0 && used + item_width > width) {
            ++rows;
            used = 0;
        }
        used += item_width + 1;
    }
    return rows;
}

int mainContentHeight(const DemoState& state, const Layout& layout) {
    if (isProtected(state.drives[state.selected_drive])) return state.main_tab == 0 ? 8 : 10;
    int width = layout.right_end - layout.right_start + 1;
    switch (state.main_tab) {
        case 0: return 10;
        case 1: return buttonRows(kSmartOptions, width) + 12;
        case 2: return buttonRows(kAtaOptions, width) + 12;
        case 3: return buttonRows(kBenchmarkProfiles, width) + 12;
        case 4: return buttonRows(kSanitizeOptions, width) + 11;
        default: return 1;
    }
}

int maxContentOffset(const DemoState& state, const Layout& layout) {
    int viewport_height = std::max(1, layout.main_bottom - layout.content_top + 1);
    return std::max(0, mainContentHeight(state, layout) - viewport_height);
}

int activeJobCount(const DemoState& state) {
    return static_cast<int>(std::count_if(state.jobs.begin(), state.jobs.end(), [](const DemoJob& job) {
        return job.state == "RUNNING" || job.state == "PAUSED" || job.state == "QUEUED";
    }));
}

std::string progressBar(int progress, int width = 6) {
    int filled = std::clamp(progress * width / 100, 0, width);
    return "[" + std::string(filled, '#') + std::string(width - filled, '-') + "]";
}

std::vector<std::pair<std::string, HitKind>> jobControls(const DemoJob& job) {
    if (job.state == "RUNNING") return {{"Pause", HitKind::JobPrimary}, {"Stop", HitKind::JobStop}};
    if (job.state == "PAUSED") return {{"Resume", HitKind::JobPrimary}, {"Stop", HitKind::JobStop}};
    if (job.state == "QUEUED") return {{"Cancel", HitKind::JobPrimary}};
    return {};
}

struct JobColumn {
    int x = 0;
    int width = 0;
};

struct JobColumns {
    JobColumn id;
    JobColumn started;
    JobColumn drive;
    JobColumn task;
    JobColumn state;
    JobColumn progress;
    JobColumn eta;
    JobColumn control;
};

JobColumns makeJobColumns(const Layout& layout) {
    constexpr int separators = 7;
    int content_width = layout.width - 4;
    int available = std::max(8, content_width - separators);
    int id_width = 4;
    int started_width = 8;
    int drive_width = 10;
    int task_width = 8;
    int state_width = 9;
    int progress_width = 10;
    int eta_width = 5;
    int control_width = 15;
    int preferred = id_width + started_width + drive_width + task_width +
                    state_width + progress_width + eta_width + control_width;
    int deficit = std::max(0, preferred - available);
    auto shrink = [&](int& width, int minimum) {
        int amount = std::min(deficit, std::max(0, width - minimum));
        width -= amount;
        deficit -= amount;
    };
    shrink(task_width, 4);
    shrink(progress_width, 8);
    shrink(control_width, 11);
    shrink(drive_width, 8);
    shrink(eta_width, 4);
    shrink(id_width, 3);
    shrink(started_width, 5);
    shrink(state_width, 5);

    int allocated = id_width + started_width + drive_width + task_width +
                    state_width + progress_width + eta_width + control_width;
    int extra = std::max(0, available - allocated);
    int task_extra = (extra * 3 + 4) / 5;
    task_width += task_extra;
    progress_width += extra - task_extra;
    int x = 2;
    auto next = [&](int width) {
        JobColumn column{x, width};
        x += width + 1;
        return column;
    };
    JobColumns columns;
    columns.id = next(id_width);
    columns.started = next(started_width);
    columns.drive = next(drive_width);
    columns.task = next(task_width);
    columns.state = next(state_width);
    columns.progress = next(progress_width);
    columns.eta = next(eta_width);
    columns.control = next(control_width);
    return columns;
}

void drawJobCell(int y, const JobColumn& column, const std::string& value, int attributes) {
    printClipped(y, column.x, column.width, fit(value, column.width), attributes);
    int separator_x = column.x + column.width;
    if (separator_x < COLS - 2) mvaddch(y, separator_x, '|');
}

void drawJobHeader(const Layout& layout) {
    JobColumns columns = makeJobColumns(layout);
    int attributes = A_BOLD | COLOR_PAIR(1);
    drawJobCell(layout.bottom_top + 2, columns.id, "ID", attributes);
    drawJobCell(layout.bottom_top + 2, columns.started, "Started", attributes);
    drawJobCell(layout.bottom_top + 2, columns.drive, "Drive", attributes);
    drawJobCell(layout.bottom_top + 2, columns.task, "Task", attributes);
    drawJobCell(layout.bottom_top + 2, columns.state, "State", attributes);
    drawJobCell(layout.bottom_top + 2, columns.progress, "Progress", attributes);
    drawJobCell(layout.bottom_top + 2, columns.eta, "ETA", attributes);
    printClipped(layout.bottom_top + 2, columns.control.x, columns.control.width,
                 "Control", attributes);
}

int drawCompactJobButton(std::vector<HitRegion>& hits, int y, int x, int max_width,
                         const std::string& label, HitKind kind, int job_index) {
    std::string text = "[" + label + "]";
    if (static_cast<int>(text.size()) > max_width || x + static_cast<int>(text.size()) >= COLS - 1) return 0;
    bool focused = g_render_state && g_render_state->focus_kind == kind &&
                   g_render_state->focus_value == job_index;
    bool hovered = g_render_state && g_render_state->hover_kind == kind &&
                   g_render_state->hover_value == job_index;
    int attributes = A_BOLD | COLOR_PAIR(focused ? kActiveTabPair : 2);
    if (focused) attributes |= A_REVERSE | A_UNDERLINE;
    if (hovered && !focused) attributes |= A_DIM;
    printClipped(y, x, static_cast<int>(text.size()), text, attributes);
    hits.push_back({y, x, 1, static_cast<int>(text.size()), kind, job_index});
    return static_cast<int>(text.size());
}

void drawJobRow(const Layout& layout, std::vector<HitRegion>& hits, const DemoState& state,
                int job_index, int y) {
    const DemoJob& job = state.jobs[job_index];
    JobColumns columns = makeJobColumns(layout);
    int attributes = COLOR_PAIR(statusPair(job.state));
    drawJobCell(y, columns.id, "#" + std::to_string(job.id), attributes);
    drawJobCell(y, columns.started, job.time, attributes);
    drawJobCell(y, columns.drive, job.target, attributes);
    drawJobCell(y, columns.task, job.task, attributes);
    drawJobCell(y, columns.state, job.state, attributes);
    std::ostringstream progress;
    int bar_width = std::max(1, columns.progress.width - 7);
    progress << std::setw(3) << job.progress << "% " << progressBar(job.progress, bar_width);
    drawJobCell(y, columns.progress, progress.str(), attributes);
    drawJobCell(y, columns.eta, job.eta, attributes);

    auto controls = jobControls(job);
    int x = columns.control.x;
    int remaining = columns.control.width;
    for (const auto& control : controls) {
        std::string label = control.first;
        if (columns.control.width < 14) {
            label = control.second == HitKind::JobStop ? "X" : label.substr(0, 1);
        }
        int drawn = drawCompactJobButton(hits, y, x, remaining, label, control.second, job_index);
        if (drawn == 0) break;
        x += drawn + 1;
        remaining -= drawn + 1;
    }
}

void drawBottomPanel(const Layout& layout, std::vector<HitRegion>& hits, DemoState& state) {
    bool show_shortcuts = layout.width >= 96;
    const std::string event_label = show_shortcuts ? "E . Event Log" : "Event Log";
    int x = 2;
    x += drawButton(hits, layout.bottom_top + 1, x, event_label, state.bottom_tab == 0,
                    HitKind::BottomTab, 0) + 1;
    std::ostringstream queue_label;
    if (show_shortcuts) queue_label << "J . ";
    queue_label << "Job Queue " << activeJobCount(state);
    int queue_width = drawButton(hits, layout.bottom_top + 1, x, queue_label.str(), state.bottom_tab == 1,
                                 HitKind::BottomTab, 1);
    std::string quit_hint = layout.width >= 94
        ? "Shift+Q . Terminate session"
        : "Shift+Q . Terminate";
    int hint_x = layout.width - 2 - static_cast<int>(quit_hint.size()) + 1;
    if (hint_x > x + buttonWidth(queue_label.str()) + 1) {
        printRight(layout.bottom_top + 1, layout.width - 2, quit_hint, COLOR_PAIR(1));
    }

    int total = state.bottom_tab == 0 ? static_cast<int>(state.events.size()) : static_cast<int>(state.jobs.size());
    int& offset = state.bottom_tab == 0 ? state.event_offset : state.job_offset;
    offset = std::clamp(offset, 0, std::max(0, total - layout.bottom_rows));
    int first = total == 0 ? 0 : offset + 1;
    int last = std::min(total, offset + layout.bottom_rows);
    std::ostringstream indicator;
    indicator << first << '-' << last << " of " << total;
    int indicator_x = x + queue_width + 2;
    printClipped(layout.bottom_top + 1, indicator_x, 12, indicator.str(), A_DIM);
    int up_x = indicator_x + static_cast<int>(indicator.str().size()) + 1;
    int down_x = up_x + buttonWidth("^") + 1;
    if (down_x + buttonWidth("v") < hint_x - 1) {
        drawButton(hits, layout.bottom_top + 1, up_x, "^", false, HitKind::BottomUp);
        drawButton(hits, layout.bottom_top + 1, down_x, "v", false, HitKind::BottomDown);
    }

    if (state.bottom_tab == 0) {
        printClipped(layout.bottom_top + 2, 2, layout.width - 5,
                     "Timestamped events (newest first)", A_BOLD | COLOR_PAIR(1));
    } else {
        drawJobHeader(layout);
    }

    for (int visible = 0; visible < layout.bottom_rows; ++visible) {
        int index = offset + visible;
        int y = layout.bottom_entry_y + visible;
        if (index >= total || y >= layout.height - 1) break;
        if (state.bottom_tab == 0) {
            printClipped(y, 2, layout.width - 4, state.events[index], COLOR_PAIR(kTextPair));
        } else {
            drawJobRow(layout, hits, state, index, y);
        }
    }
    drawVerticalScrollbar(layout.bottom_entry_y, layout.width - 2, layout.bottom_rows,
                          total, layout.bottom_rows, offset);
}

ContextInfo selectedWorkflowContext(const DemoState& state) {
    switch (state.main_tab) {
        case 1: return smartContext(state.smart_option);
        case 2: return ataContext(state.ata_option);
        case 3: return benchmarkContext(state.benchmark_profile);
        case 4: return sanitizeContext(state.sanitize_option);
        default: return {"Overview", "No workflow is available.", "STATUS ONLY",
                         "mock provider", "Review drive status.", "None"};
    }
}

void drawWorkflowDialog(const Layout& layout, std::vector<HitRegion>& hits,
                        const DemoState& state) {
    hits.clear();
    int width = std::min(66, layout.width - 6);
    int height = 11;
    int x = (layout.width - width) / 2;
    int y = (layout.height - height) / 2;
    attron(COLOR_PAIR(kTextPair));
    for (int row = 0; row < height; ++row) mvhline(y + row, x, ' ', width);
    attroff(COLOR_PAIR(kTextPair));
    mvaddch(y, x, ACS_ULCORNER); mvhline(y, x + 1, ACS_HLINE, width - 2); mvaddch(y, x + width - 1, ACS_URCORNER);
    mvvline(y + 1, x, ACS_VLINE, height - 2); mvvline(y + 1, x + width - 1, ACS_VLINE, height - 2);
    mvaddch(y + height - 1, x, ACS_LLCORNER); mvhline(y + height - 1, x + 1, ACS_HLINE, width - 2);
    mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);

    ContextInfo info = selectedWorkflowContext(state);
    int safety_pair = info.safety.find("DESTRUCTIVE") != std::string::npos ? 4 : 3;
    printClipped(y + 1, x + 2, width - 4, info.title + " workflow",
                 A_BOLD | COLOR_PAIR(kTextPair));
    printClipped(y + 3, x + 2, width - 4, "What: " + info.what, COLOR_PAIR(kTextPair));
    printClipped(y + 4, x + 2, width - 4, "Safety: " + info.safety,
                 A_BOLD | COLOR_PAIR(safety_pair));
    printClipped(y + 5, x + 2, width - 4, "Future provider: " + info.provider, COLOR_PAIR(1));
    printClipped(y + 7, x + 2, width - 4,
                 "Demo only: no storage command will be executed.", A_BOLD | COLOR_PAIR(5));
    int run_width = drawButton(hits, y + 8, x + 2, "Run Simulation",
                               state.workflow_choice == 0, HitKind::ConfirmWorkflow);
    drawButton(hits, y + 8, x + 3 + run_width, "Cancel",
               state.workflow_choice == 1, HitKind::CancelWorkflow);
}

void drawDetachDialog(const Layout& layout, std::vector<HitRegion>& hits,
                      const DemoState& state) {
    hits.clear();
    int width = std::min(58, layout.width - 6);
    int height = 9;
    int x = (layout.width - width) / 2;
    int y = (layout.height - height) / 2;
    attron(COLOR_PAIR(kTextPair));
    for (int row = 0; row < height; ++row) mvhline(y + row, x, ' ', width);
    attroff(COLOR_PAIR(kTextPair));
    mvaddch(y, x, ACS_ULCORNER); mvhline(y, x + 1, ACS_HLINE, width - 2); mvaddch(y, x + width - 1, ACS_URCORNER);
    mvvline(y + 1, x, ACS_VLINE, height - 2); mvvline(y + 1, x + width - 1, ACS_VLINE, height - 2);
    mvaddch(y + height - 1, x, ACS_LLCORNER); mvhline(y + height - 1, x + 1, ACS_HLINE, width - 2);
    mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);
    printClipped(y + 1, x + 2, width - 4, "Detach DriveLab?", A_BOLD | COLOR_PAIR(5));
    printClipped(y + 3, x + 2, width - 4,
                 "Active jobs will continue in drivelab-demo1.", COLOR_PAIR(kTextPair));
    drawButton(hits, y + 6, x + 2, "Detach", state.detach_choice == 0,
               HitKind::ConfirmDetach);
    drawButton(hits, y + 6, x + 15, "Cancel", state.detach_choice == 1,
               HitKind::CancelDetach);
}

void drawTerminateDialog(const Layout& layout, std::vector<HitRegion>& hits,
                         const DemoState& state) {
    hits.clear();
    int width = std::min(58, layout.width - 6);
    int height = 7;
    int x = (layout.width - width) / 2;
    int y = (layout.height - height) / 2;
    attron(COLOR_PAIR(kTextPair));
    for (int row = 0; row < height; ++row) mvhline(y + row, x, ' ', width);
    attroff(COLOR_PAIR(kTextPair));
    mvaddch(y, x, ACS_ULCORNER); mvhline(y, x + 1, ACS_HLINE, width - 2); mvaddch(y, x + width - 1, ACS_URCORNER);
    mvvline(y + 1, x, ACS_VLINE, height - 2); mvvline(y + 1, x + width - 1, ACS_VLINE, height - 2);
    mvaddch(y + height - 1, x, ACS_LLCORNER); mvhline(y + height - 1, x + 1, ACS_HLINE, width - 2);
    mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);
    printClipped(y + 1, x + 2, width - 4, "Terminate demo session?", A_BOLD | COLOR_PAIR(5));
    printClipped(y + 2, x + 2, width - 4, "Simulated jobs and UI state will be discarded.", COLOR_PAIR(kTextPair));
    drawButton(hits, y + 4, x + 2, "Terminate", state.terminate_choice == 0,
               HitKind::ConfirmTerminate);
    drawButton(hits, y + 4, x + 17, "Cancel", state.terminate_choice == 1,
               HitKind::CancelTerminate);
}

void drawSettingsDialog(const Layout& layout, std::vector<HitRegion>& hits) {
    hits.clear();
    int width = std::min(48, layout.width - 6);
    int height = 7;
    int x = (layout.width - width) / 2;
    int y = (layout.height - height) / 2;
    attron(COLOR_PAIR(kTextPair));
    for (int row = 0; row < height; ++row) mvhline(y + row, x, ' ', width);
    attroff(COLOR_PAIR(kTextPair));
    mvaddch(y, x, ACS_ULCORNER); mvhline(y, x + 1, ACS_HLINE, width - 2); mvaddch(y, x + width - 1, ACS_URCORNER);
    mvvline(y + 1, x, ACS_VLINE, height - 2); mvvline(y + 1, x + width - 1, ACS_VLINE, height - 2);
    mvaddch(y + height - 1, x, ACS_LLCORNER); mvhline(y + height - 1, x + 1, ACS_HLINE, width - 2);
    mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);
    printClipped(y + 1, x + 2, width - 4, "DriveLab Settings", A_BOLD | COLOR_PAIR(kTextPair));
    printClipped(y + 2, x + 2, width - 4, "Work in progress.", A_DIM);
    drawButton(hits, y + 4, x + 2, "Close", true, HitKind::CloseSettings);
}

std::vector<HitRegion> drawDemo(DemoState& state, Layout& layout) {
    std::vector<HitRegion> hits;
    g_render_state = &state;
    layout = makeLayout();
    if (!layout.valid) {
        erase();
        box(stdscr, 0, 0);
        printClipped(1, 2, layout.width - 4, "DriveLab // storage diagnostics simulator", A_BOLD | COLOR_PAIR(1));
        printClipped(std::max(3, layout.height / 2), 2, std::max(1, layout.width - 4),
                     "Terminal too small. Resize to at least 70x24.", COLOR_PAIR(5));
        printClipped(std::max(4, layout.height - 2), 2, std::max(1, layout.width - 4),
                     "Q . Detach UI  Shift+Q . Terminate", COLOR_PAIR(1));
        if (layout.width >= 24 && layout.height >= 10) {
            if (state.show_workflow) drawWorkflowDialog(layout, hits, state);
            if (state.confirm_detach) drawDetachDialog(layout, hits, state);
            if (state.confirm_terminate) drawTerminateDialog(layout, hits, state);
            if (state.show_settings) drawSettingsDialog(layout, hits);
        }
        wnoutrefresh(stdscr); doupdate();
        return hits;
    }

    drawFrame(layout);
    drawHeader(layout, hits);
    drawDrivePanel(layout, hits, state);
    const DemoDrive& drive = state.drives[state.selected_drive];
    drawIdentity(layout, drive);
    layout.content_top = drawMainTabs(layout, hits, state, drive);
    int& content_offset = state.content_offsets[state.main_tab];
    content_offset = std::clamp(content_offset, 0, maxContentOffset(state, layout));
    drawMainContent(layout, hits, state, drive, layout.content_top - content_offset);
    drawBottomPanel(layout, hits, state);
    if (state.show_workflow) drawWorkflowDialog(layout, hits, state);
    if (state.confirm_detach) drawDetachDialog(layout, hits, state);
    if (state.confirm_terminate) drawTerminateDialog(layout, hits, state);
    if (state.show_settings) drawSettingsDialog(layout, hits);
    wnoutrefresh(stdscr);
    doupdate();
    return hits;
}

bool pointInside(const HitRegion& hit, int y, int x) {
    return y >= hit.y && y < hit.y + hit.height && x >= hit.x && x < hit.x + hit.width;
}

bool updateHover(DemoState& state, const std::vector<HitRegion>& hits, int y, int x) {
    HitKind kind = HitKind::None;
    int value = 0;
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        if (!it->disabled && pointInside(*it, y, x)) {
            kind = it->kind;
            value = it->value;
            break;
        }
    }
    if (state.hover_kind == kind && state.hover_value == value) return false;
    state.hover_kind = kind;
    state.hover_value = value;
    return true;
}

void selectDrive(DemoState& state, int index) {
    state.selected_drive = std::clamp(index, 0, static_cast<int>(state.drives.size()) - 1);
    if (isProtected(state.drives[state.selected_drive]) && state.main_tab > 1) state.main_tab = 0;
    if (state.drives[state.selected_drive].media != "HDD" && state.smart_option == 2) {
        state.smart_option = 1;
    }
}

bool driveOwned(const DemoState& state, const std::string& target) {
    return std::any_of(state.jobs.begin(), state.jobs.end(), [&](const DemoJob& job) {
        return job.target == target && (job.state == "RUNNING" || job.state == "PAUSED");
    });
}

int queueDemoJob(DemoState& state, const std::string& task) {
    const DemoDrive& drive = state.drives[state.selected_drive];
    if (isProtected(drive)) return -1;
    std::string job_state = driveOwned(state, drive.path) ? "QUEUED" : "RUNNING";
    int job_id = state.next_job_id++;
    state.jobs.insert(state.jobs.begin(),
                      {job_id, timestamp(), drive.path, task, job_state, 0,
                       job_state == "RUNNING" ? "00:25" : "--"});
    state.bottom_tab = 1;
    state.job_offset = 0;
    addEvent(state, task + " created for " + drive.path + " (simulation only)");
    return job_id;
}

void applySmartAction(DemoState& state, int action) {
    const DemoDrive& drive = state.drives[state.selected_drive];
    if (isProtected(drive)) return;
    state.smart_option = std::clamp(action, 0, static_cast<int>(kSmartOptions.size()) - 1);
    if (state.smart_option == 3) {
        addEvent(state, "Raw S.M.A.R.T demo opened for " + drive.path + "; no command executed");
        state.bottom_tab = 0;
        return;
    }
    state.smart_test_name = kSmartOptions[state.smart_option];
    state.smart_test_state = "RUNNING";
    state.smart_test_progress = 0;
    state.smart_test_target = drive.path;
    state.smart_job_id = queueDemoJob(state, "S.M.A.R.T " + state.smart_test_name + " demo");
    if (!state.jobs.empty()) state.smart_test_state = state.jobs.front().state;
}

void applyAtaAction(DemoState& state, int action) {
    const DemoDrive& drive = state.drives[state.selected_drive];
    if (isProtected(drive)) return;
    state.ata_option = std::clamp(action, 0, static_cast<int>(kAtaOptions.size()) - 1);
    if (state.ata_option == 0) {
        addEvent(state, "ATA Security inspection simulated for " + drive.path);
        state.bottom_tab = 0;
    } else if (state.ata_option == 1) {
        addEvent(state, "HPA inspection simulated for " + drive.path);
        state.bottom_tab = 0;
    } else if (state.ata_option == 2) {
        addEvent(state, "DCO identify simulated for " + drive.path);
        state.bottom_tab = 0;
    } else {
        queueDemoJob(state, kAtaOptions[state.ata_option] + " workflow demo");
    }
}

void startBenchmark(DemoState& state) {
    const DemoDrive& drive = state.drives[state.selected_drive];
    if (isProtected(drive)) return;
    state.benchmark_progress = 0;
    state.benchmark_job_id = queueDemoJob(state, kBenchmarkProfiles[state.benchmark_profile] + " demo");
    if (!state.jobs.empty()) state.benchmark_state = state.jobs.front().state;
}

void startSanitize(DemoState& state) {
    if (isProtected(state.drives[state.selected_drive])) return;
    queueDemoJob(state, kSanitizeOptions[state.sanitize_option] + " workflow demo");
}

void runSelectedWorkflowSimulation(DemoState& state) {
    switch (state.main_tab) {
        case 1: applySmartAction(state, state.smart_option); break;
        case 2: applyAtaAction(state, state.ata_option); break;
        case 3: startBenchmark(state); break;
        case 4: startSanitize(state); break;
        default: break;
    }
}

void applyJobPrimary(DemoState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.jobs.size())) return;
    DemoJob& job = state.jobs[index];
    if (job.state == "RUNNING") {
        job.state = "PAUSED";
        addEvent(state, "Job #" + std::to_string(job.id) + " paused");
    } else if (job.state == "PAUSED") {
        job.state = "RUNNING";
        addEvent(state, "Job #" + std::to_string(job.id) + " resumed");
    } else if (job.state == "QUEUED") {
        job.state = "CANCELLED";
        addEvent(state, "Queued job #" + std::to_string(job.id) + " cancelled");
    }
}

void applyJobStop(DemoState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.jobs.size())) return;
    DemoJob& job = state.jobs[index];
    if (job.state == "RUNNING" || job.state == "PAUSED") {
        job.state = "CANCELLED";
        job.eta = "--";
        addEvent(state, "Job #" + std::to_string(job.id) + " stopped");
    }
}

void scanDemoInventory(DemoState& state) {
    addEvent(state, "Demo scan complete: 7 canned drives; no commands executed");
}

int visibleTabCount(const DemoState& state) {
    return isProtected(state.drives[state.selected_drive])
        ? 2 : static_cast<int>(kMainTabs.size());
}

bool hasNonCancellableJob(const DemoState& state);

void rememberModalReturn(DemoState& state) {
    state.modal_return_mode = state.mode;
    state.modal_return_focus_kind = state.focus_kind;
    state.modal_return_focus_value = state.focus_value;
}

void restoreModalReturn(DemoState& state) {
    state.mode = state.modal_return_mode;
    state.focus_kind = state.modal_return_focus_kind;
    state.focus_value = state.modal_return_focus_value;
}

void openSettings(DemoState& state) {
    rememberModalReturn(state);
    state.show_settings = true;
    state.focus_kind = HitKind::CloseSettings;
    state.focus_value = 0;
}

void openDetachDialog(DemoState& state) {
    rememberModalReturn(state);
    state.confirm_detach = true;
    state.detach_choice = 1;
    state.focus_kind = HitKind::CancelDetach;
    state.focus_value = 0;
}

void openTerminateDialog(DemoState& state) {
    if (hasNonCancellableJob(state)) {
        addEvent(state, "Session termination blocked by non-cancellable job");
        state.bottom_tab = 0;
        return;
    }
    rememberModalReturn(state);
    state.confirm_terminate = true;
    state.terminate_choice = 1;
    state.focus_kind = HitKind::CancelTerminate;
    state.focus_value = 0;
}

void openWorkflowDialog(DemoState& state) {
    if (state.main_tab < 1 || isProtected(state.drives[state.selected_drive])) return;
    rememberModalReturn(state);
    state.show_workflow = true;
    state.workflow_choice = 1;
    state.focus_kind = HitKind::CancelWorkflow;
    state.focus_value = 0;
}

void closeWorkflowDialog(DemoState& state) {
    state.show_workflow = false;
    restoreModalReturn(state);
}

void enterDriveMode(DemoState& state, int drive_index) {
    selectDrive(state, drive_index);
    state.main_tab = 0;
    state.mode = InteractionMode::DriveTabs;
    state.focus_kind = HitKind::MainTab;
    state.focus_value = 0;
}

void scrollBottom(DemoState& state, int delta, int rows) {
    int total = state.bottom_tab == 0 ? static_cast<int>(state.events.size()) : static_cast<int>(state.jobs.size());
    int& offset = state.bottom_tab == 0 ? state.event_offset : state.job_offset;
    offset = std::clamp(offset + delta, 0, std::max(0, total - rows));
    if ((state.focus_kind == HitKind::JobPrimary || state.focus_kind == HitKind::JobStop) &&
        (state.focus_value < offset || state.focus_value >= offset + rows)) {
        state.focus_kind = HitKind::BottomTab;
        state.focus_value = state.bottom_tab;
    }
}

void scrollDriveViewport(DemoState& state, int delta, const Layout& layout) {
    int max_offset = std::max(0, static_cast<int>(state.drives.size()) - layout.visible_drives);
    state.drive_offset = std::clamp(state.drive_offset + delta, 0, max_offset);
    if (state.mode == InteractionMode::DriveList && state.focus_kind == HitKind::SelectDrive) {
        state.focus_value = std::clamp(state.focus_value, state.drive_offset,
                                       state.drive_offset + layout.visible_drives - 1);
    }
}

void scrollMainContent(DemoState& state, int delta, const Layout& layout) {
    int& offset = state.content_offsets[state.main_tab];
    offset = std::clamp(offset + delta, 0, maxContentOffset(state, layout));
}

std::string handleMouseWheel(DemoState& state, int y, int x, int delta, const Layout& layout) {
    if (!layout.valid) return "none";
    if (y >= layout.bottom_top && y < layout.height - 1) {
        scrollBottom(state, delta, layout.bottom_rows);
        return "scroll-bottom";
    } else if (x > 0 && x < layout.left_divider &&
               y >= layout.drive_start && y < layout.drive_indicator_y) {
        scrollDriveViewport(state, delta, layout);
        return "scroll-drives";
    } else if (x > layout.left_divider && x < layout.width - 1 &&
               y >= layout.content_top && y <= layout.main_bottom) {
        scrollMainContent(state, delta, layout);
        return "scroll-feature";
    }
    return "none";
}

void normalizeJobControlFocus(DemoState& state) {
    if (state.focus_kind != HitKind::JobPrimary && state.focus_kind != HitKind::JobStop) return;
    int index = state.focus_value;
    if (index < 0 || index >= static_cast<int>(state.jobs.size()) ||
        jobControls(state.jobs[index]).empty()) {
        state.mode = InteractionMode::DriveList;
        state.focus_kind = HitKind::BottomTab;
        state.focus_value = state.bottom_tab;
    }
}

enum class UiSignal { None, Detach, Terminate };

UiSignal applyHit(DemoState& state, const HitRegion& hit, const Layout& layout) {
    if (hit.disabled) return UiSignal::None;
    if (hit.kind == HitKind::Settings) {
        openSettings(state);
        return UiSignal::None;
    }
    if (hit.kind == HitKind::Detach) {
        openDetachDialog(state);
        return UiSignal::None;
    }
    state.focus_kind = hit.kind;
    state.focus_value = hit.value;
    switch (hit.kind) {
        case HitKind::SelectDrive: enterDriveMode(state, hit.value); break;
        case HitKind::MainTab:
            state.main_tab = std::clamp(hit.value, 0, visibleTabCount(state) - 1);
            state.mode = InteractionMode::DriveTabs;
            state.focus_value = state.main_tab;
            break;
        case HitKind::BottomTab:
            state.bottom_tab = hit.value;
            state.mode = InteractionMode::DriveList;
            break;
        case HitKind::Scan:
            state.mode = InteractionMode::DriveList;
            scanDemoInventory(state);
            break;
        case HitKind::Settings:
        case HitKind::Detach: break;
        case HitKind::DriveUp:
            state.mode = InteractionMode::DriveList;
            state.focus_kind = HitKind::SelectDrive;
            state.focus_value = std::max(0, state.drive_offset - 1);
            break;
        case HitKind::DriveDown:
            state.mode = InteractionMode::DriveList;
            state.focus_kind = HitKind::SelectDrive;
            state.focus_value = std::min(static_cast<int>(state.drives.size()) - 1,
                                         state.drive_offset + layout.visible_drives);
            break;
        case HitKind::BottomUp:
            state.mode = InteractionMode::DriveList;
            scrollBottom(state, -1, layout.bottom_rows);
            break;
        case HitKind::BottomDown:
            state.mode = InteractionMode::DriveList;
            scrollBottom(state, 1, layout.bottom_rows);
            break;
        case HitKind::SmartAction:
            state.mode = InteractionMode::FeaturePanel;
            state.smart_option = std::clamp(hit.value, 0,
                                            static_cast<int>(kSmartOptions.size()) - 1);
            break;
        case HitKind::AtaAction:
            state.mode = InteractionMode::FeaturePanel;
            state.ata_option = std::clamp(hit.value, 0,
                                          static_cast<int>(kAtaOptions.size()) - 1);
            break;
        case HitKind::BenchmarkProfile:
            state.mode = InteractionMode::FeaturePanel;
            state.benchmark_profile = std::clamp(hit.value, 0,
                                                 static_cast<int>(kBenchmarkProfiles.size()) - 1);
            break;
        case HitKind::SanitizeOption:
            state.mode = InteractionMode::FeaturePanel;
            state.sanitize_option = std::clamp(hit.value, 0,
                                               static_cast<int>(kSanitizeOptions.size()) - 1);
            break;
        case HitKind::OpenWorkflow:
            state.mode = InteractionMode::FeaturePanel;
            openWorkflowDialog(state);
            break;
        case HitKind::JobPrimary:
            state.mode = InteractionMode::DriveList;
            applyJobPrimary(state, hit.value);
            normalizeJobControlFocus(state);
            break;
        case HitKind::JobStop:
            state.mode = InteractionMode::DriveList;
            applyJobStop(state, hit.value);
            normalizeJobControlFocus(state);
            break;
        case HitKind::ConfirmWorkflow:
            closeWorkflowDialog(state);
            runSelectedWorkflowSimulation(state);
            break;
        case HitKind::CancelWorkflow:
            closeWorkflowDialog(state);
            break;
        case HitKind::ConfirmDetach: return UiSignal::Detach;
        case HitKind::CancelDetach:
            state.confirm_detach = false;
            restoreModalReturn(state);
            break;
        case HitKind::ConfirmTerminate: return UiSignal::Terminate;
        case HitKind::CancelTerminate:
            state.confirm_terminate = false;
            restoreModalReturn(state);
            break;
        case HitKind::CloseSettings:
            state.show_settings = false;
            restoreModalReturn(state);
            break;
        case HitKind::None: break;
    }
    return UiSignal::None;
}

bool startNextQueuedJobs(DemoState& state) {
    bool changed = false;
    for (DemoJob& candidate : state.jobs) {
        if (candidate.state != "QUEUED") continue;
        if (!driveOwned(state, candidate.target)) {
            candidate.state = "RUNNING";
            candidate.eta = "00:25";
            addEvent(state, "Queued job #" + std::to_string(candidate.id) + " started");
            changed = true;
        }
    }
    return changed;
}

std::string etaForProgress(int progress) {
    int seconds = std::max(0, (100 - progress) / 4);
    std::ostringstream eta;
    eta << "00:" << std::setw(2) << std::setfill('0') << seconds;
    return eta.str();
}

bool tickJobs(DemoState& state) {
    auto now = Clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - state.last_job_tick).count() < 500) {
        return false;
    }
    state.last_job_tick = now;
    bool changed = false;
    for (DemoJob& job : state.jobs) {
        if (job.state != "RUNNING") continue;
        job.progress = std::min(100, job.progress + 2);
        job.eta = etaForProgress(job.progress);
        changed = true;
        if (job.progress == 100) {
            job.state = "COMPLETED";
            job.eta = "done";
            addEvent(state, "Job #" + std::to_string(job.id) + " completed (simulated)");
        }
    }
    changed = startNextQueuedJobs(state) || changed;

    if (state.smart_job_id < 0 && state.smart_test_state == "RUNNING") {
        state.smart_test_progress = std::min(100, state.smart_test_progress + 1);
        if (state.smart_test_progress == 100) state.smart_test_state = "COMPLETED";
        changed = true;
    }
    if (state.benchmark_job_id < 0 && state.benchmark_state == "RUNNING") {
        state.benchmark_progress = std::min(100, state.benchmark_progress + 1);
        if (state.benchmark_progress == 100) state.benchmark_state = "COMPLETED";
        changed = true;
    }
    for (const DemoJob& job : state.jobs) {
        if (job.id == state.smart_job_id) {
            changed = changed || state.smart_test_state != job.state ||
                      state.smart_test_progress != job.progress;
            state.smart_test_state = job.state;
            state.smart_test_progress = job.progress;
        }
        if (job.id == state.benchmark_job_id) {
            changed = changed || state.benchmark_state != job.state ||
                      state.benchmark_progress != job.progress;
            state.benchmark_state = job.state;
            state.benchmark_progress = job.progress;
        }
    }
    normalizeJobControlFocus(state);
    return changed;
}

bool hasNonCancellableJob(const DemoState& state) {
    return std::any_of(state.jobs.begin(), state.jobs.end(), [](const DemoJob& job) {
        return job.state == "LOCKED";
    });
}

void focusHit(DemoState& state, const HitRegion& hit) {
    state.focus_kind = hit.kind;
    state.focus_value = hit.value;
    switch (hit.kind) {
        case HitKind::MainTab: state.main_tab = hit.value; break;
        case HitKind::SmartAction: state.smart_option = hit.value; break;
        case HitKind::AtaAction: state.ata_option = hit.value; break;
        case HitKind::BenchmarkProfile: state.benchmark_profile = hit.value; break;
        case HitKind::SanitizeOption: state.sanitize_option = hit.value; break;
        case HitKind::ConfirmWorkflow: state.workflow_choice = 0; break;
        case HitKind::CancelWorkflow: state.workflow_choice = 1; break;
        case HitKind::ConfirmDetach: state.detach_choice = 0; break;
        case HitKind::CancelDetach: state.detach_choice = 1; break;
        case HitKind::ConfirmTerminate: state.terminate_choice = 0; break;
        case HitKind::CancelTerminate: state.terminate_choice = 1; break;
        default: break;
    }
}

void appendHitKind(std::vector<int>& focusable, const std::vector<HitRegion>& hits,
                   HitKind kind) {
    for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
        if (!hits[index].disabled && hits[index].kind == kind) focusable.push_back(index);
    }
}

std::vector<int> focusableHits(const DemoState& state,
                               const std::vector<HitRegion>& hits) {
    std::vector<int> focusable;
    if (state.mode == InteractionMode::DriveList) {
        appendHitKind(focusable, hits, HitKind::Scan);
        appendHitKind(focusable, hits, HitKind::SelectDrive);
        appendHitKind(focusable, hits, HitKind::BottomTab);
        appendHitKind(focusable, hits, HitKind::BottomUp);
        appendHitKind(focusable, hits, HitKind::BottomDown);
        for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
            HitKind kind = hits[index].kind;
            if (!hits[index].disabled &&
                (kind == HitKind::JobPrimary || kind == HitKind::JobStop)) {
                focusable.push_back(index);
            }
        }
        appendHitKind(focusable, hits, HitKind::Settings);
        appendHitKind(focusable, hits, HitKind::Detach);
        return focusable;
    }

    for (int index = 0; index < static_cast<int>(hits.size()); ++index) {
        const HitRegion& hit = hits[index];
        if (hit.disabled) continue;
        if (state.mode == InteractionMode::DriveTabs && hit.kind == HitKind::MainTab) {
            focusable.push_back(index);
        }
    }
    return focusable;
}

void cycleFocus(DemoState& state, const std::vector<HitRegion>& hits, int direction) {
    std::vector<int> focusable = focusableHits(state, hits);
    if (focusable.empty()) return;

    int current = -1;
    for (int index = 0; index < static_cast<int>(focusable.size()); ++index) {
        const HitRegion& hit = hits[focusable[index]];
        if (hit.kind == state.focus_kind && hit.value == state.focus_value) {
            current = index;
            break;
        }
    }
    int next = current < 0
        ? (direction > 0 ? 0 : static_cast<int>(focusable.size()) - 1)
        : (current + direction + static_cast<int>(focusable.size())) %
              static_cast<int>(focusable.size());
    focusHit(state, hits[focusable[next]]);
}

UiSignal activateFocused(DemoState& state, const std::vector<HitRegion>& hits,
                         const Layout& layout) {
    for (const HitRegion& hit : hits) {
        if (!hit.disabled && hit.kind == state.focus_kind && hit.value == state.focus_value) {
            return applyHit(state, hit, layout);
        }
    }
    return UiSignal::None;
}

void returnToDriveList(DemoState& state) {
    state.mode = InteractionMode::DriveList;
    state.focus_kind = HitKind::SelectDrive;
    state.focus_value = state.selected_drive;
}

void moveDriveFocus(DemoState& state, int delta, const Layout& layout) {
    int first = state.drive_offset;
    int last = std::min(static_cast<int>(state.drives.size()) - 1,
                        state.drive_offset + layout.visible_drives - 1);
    int index = state.focus_kind == HitKind::SelectDrive
        ? state.focus_value : (delta > 0 ? first : last);
    if (state.focus_kind == HitKind::SelectDrive) index += delta;
    state.focus_kind = HitKind::SelectDrive;
    state.focus_value = std::clamp(index, 0, static_cast<int>(state.drives.size()) - 1);
}

void moveFeatureTab(DemoState& state, int delta) {
    int count = visibleTabCount(state);
    state.main_tab = (state.main_tab + delta + count) % count;
    state.focus_kind = HitKind::MainTab;
    state.focus_value = state.main_tab;
}

struct FocusTarget {
    HitKind kind = HitKind::None;
    int value = 0;
};

std::vector<FocusTarget> featureTargets(const DemoState& state) {
    std::vector<FocusTarget> targets;
    if (state.main_tab == 0 || isProtected(state.drives[state.selected_drive])) {
        targets.push_back({HitKind::MainTab, state.main_tab});
    } else if (state.main_tab == 1) {
        for (int index = 0; index < static_cast<int>(kSmartOptions.size()); ++index) {
            if (index != 2 || state.drives[state.selected_drive].media == "HDD") {
                targets.push_back({HitKind::SmartAction, index});
            }
        }
        targets.push_back({HitKind::OpenWorkflow, 0});
    } else if (state.main_tab == 2) {
        for (int index = 0; index < static_cast<int>(kAtaOptions.size()); ++index) {
            targets.push_back({HitKind::AtaAction, index});
        }
        targets.push_back({HitKind::OpenWorkflow, 0});
    } else if (state.main_tab == 3) {
        for (int index = 0; index < static_cast<int>(kBenchmarkProfiles.size()); ++index) {
            targets.push_back({HitKind::BenchmarkProfile, index});
        }
        targets.push_back({HitKind::OpenWorkflow, 0});
    } else if (state.main_tab == 4) {
        for (int index = 0; index < static_cast<int>(kSanitizeOptions.size()); ++index) {
            targets.push_back({HitKind::SanitizeOption, index});
        }
        targets.push_back({HitKind::OpenWorkflow, 0});
    }
    return targets;
}

int optionRow(const std::vector<std::string>& labels, int option, int width) {
    int row = 0;
    int used = 0;
    for (int index = 0; index <= option && index < static_cast<int>(labels.size()); ++index) {
        int item_width = buttonWidth(labels[index]);
        if (used > 0 && used + item_width > width) {
            ++row;
            used = 0;
        }
        used += item_width + 1;
    }
    return row;
}

int featureControlRow(const DemoState& state, const Layout& layout,
                      const FocusTarget& target) {
    int width = layout.right_end - layout.right_start + 1;
    switch (target.kind) {
        case HitKind::SmartAction: return 3 + optionRow(kSmartOptions, target.value, width);
        case HitKind::AtaAction: return 4 + optionRow(kAtaOptions, target.value, width);
        case HitKind::BenchmarkProfile:
            return 2 + optionRow(kBenchmarkProfiles, target.value, width);
        case HitKind::SanitizeOption:
            return 2 + optionRow(kSanitizeOptions, target.value, width);
        case HitKind::OpenWorkflow:
            if (state.main_tab == 1) return buttonRows(kSmartOptions, width) + 11;
            if (state.main_tab == 2) return buttonRows(kAtaOptions, width) + 11;
            if (state.main_tab == 3) return buttonRows(kBenchmarkProfiles, width) + 11;
            if (state.main_tab == 4) return buttonRows(kSanitizeOptions, width) + 10;
            return 0;
        default: return 0;
    }
}

void focusTarget(DemoState& state, const FocusTarget& target) {
    HitRegion synthetic{0, 0, 1, 1, target.kind, target.value};
    focusHit(state, synthetic);
}

void ensureFeatureFocusVisible(DemoState& state, const Layout& layout,
                               const FocusTarget& target) {
    if (target.kind == HitKind::MainTab) return;
    int viewport_height = std::max(1, layout.main_bottom - layout.content_top + 1);
    int row = featureControlRow(state, layout, target);
    int& offset = state.content_offsets[state.main_tab];
    if (row < offset) offset = row;
    if (row >= offset + viewport_height) offset = row - viewport_height + 1;
    offset = std::clamp(offset, 0, maxContentOffset(state, layout));
}

void enterFeaturePanel(DemoState& state, const Layout& layout) {
    state.mode = InteractionMode::FeaturePanel;
    state.content_offsets[state.main_tab] = 0;
    std::vector<FocusTarget> targets = featureTargets(state);
    if (targets.empty()) return;

    FocusTarget preferred = targets.front();
    if (state.main_tab == 1) preferred = {HitKind::SmartAction, state.smart_option};
    else if (state.main_tab == 2) preferred = {HitKind::AtaAction, state.ata_option};
    else if (state.main_tab == 3) preferred = {HitKind::BenchmarkProfile, state.benchmark_profile};
    else if (state.main_tab == 4) preferred = {HitKind::SanitizeOption, state.sanitize_option};
    auto found = std::find_if(targets.begin(), targets.end(), [&](const FocusTarget& target) {
        return target.kind == preferred.kind && target.value == preferred.value;
    });
    FocusTarget target = found == targets.end() ? targets.front() : *found;
    focusTarget(state, target);
    ensureFeatureFocusVisible(state, layout, target);
}

void cycleFeatureFocus(DemoState& state, int direction, const Layout& layout) {
    std::vector<FocusTarget> targets = featureTargets(state);
    if (targets.empty()) return;
    int current = -1;
    for (int index = 0; index < static_cast<int>(targets.size()); ++index) {
        if (targets[index].kind == state.focus_kind && targets[index].value == state.focus_value) {
            current = index;
            break;
        }
    }
    int next = current < 0
        ? (direction > 0 ? 0 : static_cast<int>(targets.size()) - 1)
        : (current + direction + static_cast<int>(targets.size())) %
              static_cast<int>(targets.size());
    focusTarget(state, targets[next]);
    ensureFeatureFocusVisible(state, layout, targets[next]);
}

UiSignal activateFeatureControl(DemoState& state) {
    switch (state.focus_kind) {
        case HitKind::SmartAction:
        case HitKind::AtaAction:
        case HitKind::BenchmarkProfile:
        case HitKind::SanitizeOption:
        case HitKind::OpenWorkflow:
            openWorkflowDialog(state);
            break;
        default: break;
    }
    return UiSignal::None;
}

void selectFeatureShortcut(DemoState& state, int tab) {
    if (tab < 0 || tab >= visibleTabCount(state)) return;
    state.mode = InteractionMode::DriveTabs;
    state.main_tab = tab;
    state.focus_kind = HitKind::MainTab;
    state.focus_value = tab;
}

void setWorkflowChoice(DemoState& state, int choice) {
    state.workflow_choice = std::clamp(choice, 0, 1);
    state.focus_kind = state.workflow_choice == 0
        ? HitKind::ConfirmWorkflow : HitKind::CancelWorkflow;
    state.focus_value = 0;
}

void setDetachChoice(DemoState& state, int choice) {
    state.detach_choice = std::clamp(choice, 0, 1);
    state.focus_kind = state.detach_choice == 0
        ? HitKind::ConfirmDetach : HitKind::CancelDetach;
    state.focus_value = 0;
}

void setTerminateChoice(DemoState& state, int choice) {
    state.terminate_choice = std::clamp(choice, 0, 1);
    state.focus_kind = state.terminate_choice == 0
        ? HitKind::ConfirmTerminate : HitKind::CancelTerminate;
    state.focus_value = 0;
}

UiSignal handleKey(DemoState& state, int key, const Layout& layout,
                   const std::vector<HitRegion>& hits) {
    state.hover_kind = HitKind::None;
    state.hover_value = 0;

    if (state.show_workflow) {
        if (key == 27) {
            closeWorkflowDialog(state);
        } else if (key == KEY_LEFT || key == KEY_RIGHT || key == '\t' || key == KEY_BTAB) {
            setWorkflowChoice(state, 1 - state.workflow_choice);
        } else if (key == '\n' || key == KEY_ENTER) {
            if (state.workflow_choice == 0) {
                closeWorkflowDialog(state);
                runSelectedWorkflowSimulation(state);
            } else {
                closeWorkflowDialog(state);
            }
        }
        return UiSignal::None;
    }

    if (state.show_settings) {
        if (key == 27 || key == '\n' || key == KEY_ENTER) {
            state.show_settings = false;
            restoreModalReturn(state);
        }
        return UiSignal::None;
    }

    if (state.confirm_detach) {
        if (key == 27) {
            state.confirm_detach = false;
            restoreModalReturn(state);
        } else if (key == KEY_LEFT || key == KEY_RIGHT || key == '\t' || key == KEY_BTAB) {
            setDetachChoice(state, 1 - state.detach_choice);
        } else if (key == '\n' || key == KEY_ENTER) {
            if (state.detach_choice == 0) return UiSignal::Detach;
            state.confirm_detach = false;
            restoreModalReturn(state);
        }
        return UiSignal::None;
    }

    if (state.confirm_terminate) {
        if (key == 27) {
            state.confirm_terminate = false;
            restoreModalReturn(state);
        } else if (key == KEY_LEFT || key == KEY_RIGHT || key == '\t' || key == KEY_BTAB) {
            setTerminateChoice(state, 1 - state.terminate_choice);
        } else if (key == '\n' || key == KEY_ENTER) {
            if (state.terminate_choice == 0) return UiSignal::Terminate;
            state.confirm_terminate = false;
            restoreModalReturn(state);
        }
        return UiSignal::None;
    }

    if (key == 'q') {
        openDetachDialog(state);
        return UiSignal::None;
    }
    if (key == 'Q') {
        openTerminateDialog(state);
        return UiSignal::None;
    }
    if (key == KEY_PPAGE || key == KEY_NPAGE) {
        scrollBottom(state, key == KEY_PPAGE ? -1 : 1, layout.bottom_rows);
        return UiSignal::None;
    }
    if (key == 'r' || key == 'R') {
        state.mode = InteractionMode::DriveList;
        state.focus_kind = HitKind::Scan;
        state.focus_value = 0;
        scanDemoInventory(state);
        return UiSignal::None;
    }
    if (key == 'e' || key == 'E' || key == 'j' || key == 'J') {
        state.mode = InteractionMode::DriveList;
        state.bottom_tab = key == 'e' || key == 'E' ? 0 : 1;
        state.focus_kind = HitKind::BottomTab;
        state.focus_value = state.bottom_tab;
        return UiSignal::None;
    }
    bool feature_shortcut = true;
    if (key == 'o' || key == 'O') selectFeatureShortcut(state, 0);
    else if (key == 's' || key == 'S') selectFeatureShortcut(state, 1);
    else if (key == 'a' || key == 'A') selectFeatureShortcut(state, 2);
    else if (key == 'b' || key == 'B') selectFeatureShortcut(state, 3);
    else if (key == 'n' || key == 'N') selectFeatureShortcut(state, 4);
    else feature_shortcut = false;
    if (feature_shortcut) return UiSignal::None;

    if (state.mode == InteractionMode::DriveList) {
        if (key == '\t') cycleFocus(state, hits, 1);
        else if (key == KEY_BTAB) cycleFocus(state, hits, -1);
        else if ((key == KEY_UP || key == KEY_DOWN) &&
                 (state.focus_kind == HitKind::Scan || state.focus_kind == HitKind::SelectDrive ||
                  state.focus_kind == HitKind::DriveUp || state.focus_kind == HitKind::DriveDown)) {
            moveDriveFocus(state, key == KEY_UP ? -1 : 1, layout);
        } else if ((key == KEY_UP || key == KEY_DOWN) &&
                   (state.focus_kind == HitKind::BottomTab || state.focus_kind == HitKind::BottomUp ||
                    state.focus_kind == HitKind::BottomDown || state.focus_kind == HitKind::JobPrimary ||
                    state.focus_kind == HitKind::JobStop)) {
            scrollBottom(state, key == KEY_UP ? -1 : 1, layout.bottom_rows);
        } else if (key == '\n' || key == KEY_ENTER) {
            return activateFocused(state, hits, layout);
        }
    } else if (state.mode == InteractionMode::DriveTabs) {
        if (key == '\t') cycleFocus(state, hits, 1);
        else if (key == KEY_BTAB) cycleFocus(state, hits, -1);
        else if (key == KEY_LEFT) moveFeatureTab(state, -1);
        else if (key == KEY_RIGHT) moveFeatureTab(state, 1);
        else if (key == '\n' || key == KEY_ENTER) enterFeaturePanel(state, layout);
        else if (key == 27) returnToDriveList(state);
    } else if (state.mode == InteractionMode::FeaturePanel) {
        if (key == '\t' || key == KEY_DOWN) cycleFeatureFocus(state, 1, layout);
        else if (key == KEY_BTAB || key == KEY_UP) cycleFeatureFocus(state, -1, layout);
        else if (key == '\n' || key == KEY_ENTER) {
            return activateFeatureControl(state);
        } else if (key == 27) {
            state.mode = InteractionMode::DriveTabs;
            state.focus_kind = HitKind::MainTab;
            state.focus_value = state.main_tab;
        }
    }
    return UiSignal::None;
}

bool hasVisibleFocus(const DemoState& state, const std::vector<HitRegion>& hits) {
    return std::any_of(hits.begin(), hits.end(), [&](const HitRegion& hit) {
        return !hit.disabled && hit.kind == state.focus_kind && hit.value == state.focus_value;
    });
}

bool normalizeFocusAfterDraw(DemoState& state, const std::vector<HitRegion>& hits,
                             const Layout& layout) {
    if (!layout.valid || hasVisibleFocus(state, hits)) return false;
    if (state.show_workflow || state.confirm_detach || state.confirm_terminate ||
        state.show_settings) return false;

    if (state.mode == InteractionMode::FeaturePanel) {
        FocusTarget target{state.focus_kind, state.focus_value};
        int previous_offset = state.content_offsets[state.main_tab];
        ensureFeatureFocusVisible(state, layout, target);
        if (state.content_offsets[state.main_tab] != previous_offset) return true;
        state.focus_kind = HitKind::MainTab;
        state.focus_value = state.main_tab;
        return true;
    }
    if (state.mode == InteractionMode::DriveTabs) {
        state.focus_kind = HitKind::MainTab;
        state.focus_value = state.main_tab;
        return true;
    }
    state.focus_kind = HitKind::Scan;
    state.focus_value = 0;
    return true;
}

const char* hitKindName(HitKind kind) {
    switch (kind) {
        case HitKind::None: return "None";
        case HitKind::SelectDrive: return "SelectDrive";
        case HitKind::MainTab: return "MainTab";
        case HitKind::BottomTab: return "BottomTab";
        case HitKind::Scan: return "Scan";
        case HitKind::Settings: return "Settings";
        case HitKind::Detach: return "Detach";
        case HitKind::DriveUp: return "DriveUp";
        case HitKind::DriveDown: return "DriveDown";
        case HitKind::BottomUp: return "BottomUp";
        case HitKind::BottomDown: return "BottomDown";
        case HitKind::SmartAction: return "SmartAction";
        case HitKind::AtaAction: return "AtaAction";
        case HitKind::BenchmarkProfile: return "BenchmarkProfile";
        case HitKind::SanitizeOption: return "SanitizeOption";
        case HitKind::OpenWorkflow: return "OpenWorkflow";
        case HitKind::JobPrimary: return "JobPrimary";
        case HitKind::JobStop: return "JobStop";
        case HitKind::ConfirmWorkflow: return "ConfirmWorkflow";
        case HitKind::CancelWorkflow: return "CancelWorkflow";
        case HitKind::ConfirmDetach: return "ConfirmDetach";
        case HitKind::CancelDetach: return "CancelDetach";
        case HitKind::ConfirmTerminate: return "ConfirmTerminate";
        case HitKind::CancelTerminate: return "CancelTerminate";
        case HitKind::CloseSettings: return "CloseSettings";
    }
    return "Unknown";
}

const HitRegion* resolveHit(const std::vector<HitRegion>& hits, int y, int x) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        if (pointInside(*it, y, x)) return &*it;
    }
    return nullptr;
}

std::string describeHit(const HitRegion* hit) {
    if (!hit) return "none";
    std::ostringstream description;
    description << hitKindName(hit->kind) << '(' << hit->value << ')';
    if (hit->disabled) description << " disabled";
    return description.str();
}

bool isFeatureOptionHit(HitKind kind) {
    return kind == HitKind::SmartAction || kind == HitKind::AtaAction ||
           kind == HitKind::BenchmarkProfile || kind == HitKind::SanitizeOption;
}

void logMouseEvent(std::ofstream& log, const MEVENT& event,
                   const HitRegion* resolved, const std::string& action) {
    if (!log) return;
    log << timestamp() << " mouse x=" << event.x << " y=" << event.y
        << " raw_bstate=" << static_cast<unsigned long long>(event.bstate)
        << " resolved=" << describeHit(resolved)
        << " action=" << action << '\n';
    if (action != "none" ||
        (event.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED)) != 0) {
        log.flush();
    }
}

}  // namespace

int runDemoUi() {
    if (has_colors()) {
        init_pair(kSelectedPair, COLOR_WHITE, COLOR_BLUE);
        init_pair(kActiveTabPair, COLOR_BLACK, COLOR_CYAN);
        init_pair(kTextPair, COLOR_WHITE, -1);
    }
    mmask_t mouse_events = BUTTON1_PRESSED | BUTTON1_RELEASED |
                           BUTTON4_PRESSED | REPORT_MOUSE_POSITION;
#ifdef BUTTON5_PRESSED
    mouse_events |= BUTTON5_PRESSED;
#endif
    mousemask(mouse_events, nullptr);
    mouseinterval(0);
    timeout(30);

    DemoState state = makeDemoState();
    std::ofstream mouse_debug("/tmp/drivelab-demo-mouse.log",
                              std::ios::out | std::ios::trunc);
    addEvent(state, mouse_debug
        ? "Mouse debug logging enabled: /tmp/drivelab-demo-mouse.log"
        : "Mouse debug logging unavailable: /tmp/drivelab-demo-mouse.log");
    bool running = true;
    bool dirty = true;
    Layout layout;
    std::vector<HitRegion> hits;
    bool button1_down = false;
    HitKind pressed_kind = HitKind::None;
    int pressed_value = 0;
    while (running) {
        dirty = tickJobs(state) || dirty;
        if (dirty) {
            hits = drawDemo(state, layout);
            if (normalizeFocusAfterDraw(state, hits, layout)) hits = drawDemo(state, layout);
            dirty = false;
        }

        int key = getch();
        if (key == ERR) continue;
        if (key == KEY_RESIZE) {
            button1_down = false;
            dirty = true;
            continue;
        }

        UiSignal signal = UiSignal::None;
        if (key == KEY_MOUSE) {
            MEVENT event{};
            if (getmouse(&event) == OK) {
                const HitRegion* resolved = resolveHit(hits, event.y, event.x);
                dirty = updateHover(state, hits, event.y, event.x) || dirty;
                std::string action = "none";
                bool wheel_up = (event.bstate & BUTTON4_PRESSED) != 0;
                bool wheel_down = false;
#ifdef BUTTON5_PRESSED
                wheel_down = (event.bstate & BUTTON5_PRESSED) != 0;
#endif
                if (wheel_up || wheel_down) {
                    if (!state.show_workflow && !state.confirm_detach &&
                        !state.confirm_terminate && !state.show_settings) {
                        action = handleMouseWheel(state, event.y, event.x,
                                                  wheel_up ? -1 : 1, layout);
                        dirty = true;
                    }
                } else {
                    if ((event.bstate & BUTTON1_PRESSED) != 0) {
                        button1_down = resolved && !resolved->disabled;
                        if (button1_down) {
                            pressed_kind = resolved->kind;
                            pressed_value = resolved->value;
                        } else {
                            pressed_kind = HitKind::None;
                            pressed_value = 0;
                        }
                    }
                    if ((event.bstate & BUTTON1_RELEASED) != 0) {
                        bool same_hit = button1_down && resolved && !resolved->disabled &&
                                        resolved->kind == pressed_kind &&
                                        resolved->value == pressed_value;
                        button1_down = false;
                        pressed_kind = HitKind::None;
                        pressed_value = 0;
                        if (same_hit) {
                            action = std::string(isFeatureOptionHit(resolved->kind)
                                ? "select:" : "activate:") + describeHit(resolved);
                            signal = applyHit(state, *resolved, layout);
                            dirty = true;
                        }
                    }
                }
                // A release activates only the same control recorded on press.
                logMouseEvent(mouse_debug, event, resolved, action);
            }
        } else {
            button1_down = false;
            signal = handleKey(state, key, layout, hits);
            dirty = true;
        }
        if (signal == UiSignal::Detach || signal == UiSignal::Terminate) running = false;
    }
    timeout(-1);
    return 0;
}
