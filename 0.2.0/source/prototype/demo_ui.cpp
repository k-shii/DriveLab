#include "demo_ui.h"

#include "app/core_application.h"
#include "config/app_config.h"
#include "core/version.h"
#include "logging/logger.h"
#include "ui/demo_read_model.h"

#include <ncurses.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using drivelab::DemoDriveDetails;
using drivelab::DemoFeature;
using drivelab::DemoFeatureProgress;
using drivelab::DemoSnapshot;
using drivelab::DemoWorkflowDefinition;
using drivelab::DemoWorkflowId;
using drivelab::JobRecord;
using drivelab::JobState;

constexpr int kSelectedPair = 6;
constexpr int kActiveTabPair = 7;
constexpr int kTextPair = 8;

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
    drivelab::CoreApplication* application = nullptr;
    drivelab::DemoReadModel* read_model = nullptr;
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
    int benchmark_profile = 0;
    int sanitize_option = 0;
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
    Clock::time_point last_core_tick = Clock::now();

    [[nodiscard]] const std::vector<DemoDriveDetails>& drives() const {
        return read_model->drives();
    }

    [[nodiscard]] const DemoSnapshot& snapshot() const {
        return read_model->snapshot();
    }
};

const DemoState* g_render_state = nullptr;

const DemoDriveDetails& selectedDrive(const DemoState& state) {
    return state.drives()[state.selected_drive];
}

std::string drivePath(const DemoDriveDetails& details) {
    return details.drive.current_path;
}

std::string driveModel(const DemoDriveDetails& details) {
    return details.drive.identity.model;
}

std::string driveSerial(const DemoDriveDetails& details) {
    return details.drive.identity.serial;
}

std::string driveMedia(const DemoDriveDetails& details) {
    return drivelab::mediaKindName(details.drive.media);
}

std::string driveState(const DemoDriveDetails& details) {
    return drivelab::driveStatusName(details.drive.status);
}

std::string protectionReason(const DemoDriveDetails& details) {
    if (details.drive.protection_reasons.empty()) return "";
    std::ostringstream reason;
    for (std::size_t index = 0; index < details.drive.protection_reasons.size(); ++index) {
        if (index > 0) reason << "; ";
        reason << details.drive.protection_reasons[index];
    }
    return reason.str();
}

std::string formatCount(std::uint64_t value) {
    std::string text = std::to_string(value);
    for (int position = static_cast<int>(text.size()) - 3; position > 0; position -= 3) {
        text.insert(static_cast<std::size_t>(position), ",");
    }
    return text;
}

std::string formatTemperature(const DemoDriveDetails& details) {
    if (!details.smart.temperature_celsius) return "N/A";
    return std::to_string(*details.smart.temperature_celsius) + " C";
}

std::string formatPowerHours(const DemoDriveDetails& details) {
    return formatCount(details.smart.power_on_hours) + " h";
}

std::string formatDuration(std::chrono::minutes duration) {
    const auto total = duration.count();
    const auto hours = total / 60;
    const auto minutes = total % 60;
    std::ostringstream text;
    if (hours > 0) text << hours << 'h';
    if (minutes > 0 || hours == 0) text << minutes << 'm';
    return text.str();
}

std::vector<DemoFeature> availableFeatures(const DemoState& state) {
    auto features = state.read_model->features(
        static_cast<std::size_t>(state.selected_drive));
    if (!features || features.value().empty()) return {DemoFeature::Overview};
    return features.value();
}

DemoFeature selectedFeature(const DemoState& state) {
    std::vector<DemoFeature> features = availableFeatures(state);
    int index = std::clamp(state.main_tab, 0, static_cast<int>(features.size()) - 1);
    return features[static_cast<std::size_t>(index)];
}

std::vector<DemoWorkflowDefinition> featureWorkflows(
    const DemoState& state,
    DemoFeature feature) {
    auto workflows = state.read_model->workflows(
        static_cast<std::size_t>(state.selected_drive), feature);
    return workflows ? workflows.value() : std::vector<DemoWorkflowDefinition>{};
}

std::vector<std::string> workflowLabels(
    const std::vector<DemoWorkflowDefinition>& workflows) {
    std::vector<std::string> labels;
    labels.reserve(workflows.size());
    for (const DemoWorkflowDefinition& workflow : workflows) {
        labels.push_back(workflow.label);
    }
    return labels;
}

char featureShortcut(DemoFeature feature) {
    switch (feature) {
        case DemoFeature::Overview: return 'O';
        case DemoFeature::Smart: return 'S';
        case DemoFeature::AtaHpa: return 'A';
        case DemoFeature::Benchmark: return 'B';
        case DemoFeature::Sanitize: return 'N';
    }
    return '?';
}

std::string featureLabel(DemoFeature feature, bool show_shortcut) {
    std::string label = drivelab::demoFeatureName(feature);
    if (!show_shortcut) return label;
    return std::string(1, featureShortcut(feature)) + " . " + label;
}

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

std::string timestamp(std::chrono::system_clock::time_point time) {
    std::time_t value_time = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
    localtime_r(&value_time, &local);
    char value[16];
    std::strftime(value, sizeof(value), "%H:%M:%S", &local);
    return value;
}

bool isProtected(const DemoDriveDetails& drive) {
    return drive.drive.isProtected();
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

DemoState makeDemoState(drivelab::CoreApplication& application,
                        drivelab::DemoReadModel& read_model) {
    DemoState state;
    state.application = &application;
    state.read_model = &read_model;
    return state;
}

void normalizeDriveViewport(DemoState& state, int visible) {
    state.selected_drive = std::clamp(
        state.selected_drive, 0, static_cast<int>(state.drives().size()) - 1);
    int max_offset = std::max(0, static_cast<int>(state.drives().size()) - visible);
    state.drive_offset = std::clamp(state.drive_offset, 0, max_offset);
    if (state.mode == InteractionMode::DriveList && state.focus_kind == HitKind::SelectDrive) {
        state.focus_value = std::clamp(state.focus_value, 0,
                                       static_cast<int>(state.drives().size()) - 1);
        if (state.focus_value < state.drive_offset) state.drive_offset = state.focus_value;
        if (state.focus_value >= state.drive_offset + visible) {
            state.drive_offset = state.focus_value - visible + 1;
        }
    }
    if (isProtected(selectedDrive(state)) && state.main_tab > 1) state.main_tab = 0;
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
    const std::string version = "v" + std::string(drivelab::kVersion);
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
    const DemoDriveDetails& drive = state.drives()[drive_index];
    const std::string state_text = driveState(drive);
    const std::string media = driveMedia(drive);
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
    printClipped(row, 2, content_width, drivePath(drive), A_BOLD | selected_attr);
    printRight(row, content_right, drive.display_capacity, A_BOLD | selected_attr);

    if (layout.drive_row_height == 3) {
        printClipped(row + 1, 2, content_width, fit(driveModel(drive), content_width), selected_attr | A_DIM);
        printClipped(row + 2, 2, content_width, state_text,
                     A_BOLD | (highlighted ? selected_attr : COLOR_PAIR(statusPair(state_text))));
        printRight(row + 2, content_right, media,
                   selected_attr | COLOR_PAIR(highlighted ? kSelectedPair : 1));
    } else {
        std::string detail = state_text + "  " + driveModel(drive);
        printClipped(row + 1, 2,
                     content_width - static_cast<int>(media.size()) - 2, detail,
                     selected_attr | A_DIM);
        printRight(row + 1, content_right, media, selected_attr);
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
        if (drive_index >= static_cast<int>(state.drives().size())) break;
        int row = layout.drive_first_y + visible * layout.drive_row_stride;
        drawDriveRow(layout, hits, state, drive_index, row);
    }
    drawVerticalScrollbar(layout.drive_start, layout.left_divider - 1,
                          layout.drive_viewport_height, static_cast<int>(state.drives().size()),
                          layout.visible_drives, state.drive_offset);

    int first = state.drives().empty() ? 0 : state.drive_offset + 1;
    int last = std::min(static_cast<int>(state.drives().size()),
                        state.drive_offset + layout.visible_drives);
    std::ostringstream indicator;
    indicator << first << '-' << last << " of " << state.drives().size() << " . scroll";
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

void drawIdentity(const Layout& layout, const DemoDriveDetails& drive) {
    int right_width = layout.right_end - layout.right_start + 1;
    const std::string state_text = driveState(drive);
    const std::string serial = driveSerial(drive);
    std::string smart = "S.M.A.R.T " + drive.smart.health_summary;
    printClipped(layout.main_top + 1, layout.right_start,
                 right_width - static_cast<int>(smart.size()) - 2,
                 driveModel(drive), A_BOLD | COLOR_PAIR(kTextPair));
    printRight(layout.main_top + 1, layout.right_end, smart,
               A_BOLD | COLOR_PAIR(statusPair(drive.smart.health_summary)));

    std::string identity = drivePath(drive) + " . " + drive.display_capacity +
                           " . " + driveMedia(drive);
    printClipped(layout.main_top + 2, layout.right_start,
                 right_width - static_cast<int>(serial.size()) - 4,
                 identity, COLOR_PAIR(1));
    printRight(layout.main_top + 2, layout.right_end, serial, A_DIM);

    if (isProtected(drive)) {
        const std::string reason = protectionReason(drive);
        printClipped(layout.main_top + 3, layout.right_start,
                     right_width - static_cast<int>(state_text.size()) - 3,
                     fit(reason, right_width - 14), COLOR_PAIR(5));
    }
    printRight(layout.main_top + 3, layout.right_end, state_text,
               A_BOLD | COLOR_PAIR(statusPair(state_text)));
}

int drawMainTabs(const Layout& layout, std::vector<HitRegion>& hits, DemoState& state) {
    const std::vector<DemoFeature> features = availableFeatures(state);
    int tab_count = static_cast<int>(features.size());
    state.main_tab = std::clamp(state.main_tab, 0, tab_count - 1);
    int available_width = layout.right_end - layout.right_start + 1;
    bool show_shortcuts = tab_count <= 2 || available_width >= 52;
    int y = layout.identity_separator + 1;
    int x = layout.right_start;
    for (int i = 0; i < tab_count; ++i) {
        std::string label = featureLabel(features[static_cast<std::size_t>(i)], show_shortcuts);
        int width = buttonWidth(label);
        if (x + width > layout.right_end + 1) {
            ++y;
            x = layout.right_start;
        }
        bool active = state.mode != InteractionMode::DriveList && state.main_tab == i;
        int drawn = drawButton(hits, y, x, label, active, HitKind::MainTab, i);
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

void drawOverview(const Layout& layout, const DemoDriveDetails& drive, int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    if (isProtected(drive)) {
        printPanel(layout, content_y, layout.right_start, width, "Protected device", A_BOLD | COLOR_PAIR(5));
        printPanel(layout, content_y + 2, layout.right_start, width,
                   protectionReason(drive), COLOR_PAIR(kTextPair));
        printPanel(layout, content_y + 4, layout.right_start, width,
                   "Status and safely cached identity/S.M.A.R.T data only. Action tabs are hidden.", A_DIM);
        drawKeyValue(layout, content_y + 6, layout.right_start, width,
                     "S.M.A.R.T", drive.smart.health_summary);
        return;
    }

    bool two_columns = width >= 52;
    int gap = two_columns ? 3 : 0;
    int column_width = two_columns ? (width - gap) / 2 : width;
    int right_x = layout.right_start + column_width + gap;
    drawKeyValue(layout, content_y, layout.right_start, column_width,
                 "Transport", drive.transport);
    drawKeyValue(layout, content_y + 2, layout.right_start, column_width,
                 "Temperature", formatTemperature(drive));
    drawKeyValue(layout, content_y + 4, layout.right_start, column_width,
                 "Pending", std::to_string(drive.smart.pending));
    drawKeyValue(layout, content_y + 6, layout.right_start, column_width,
                 "ATA", drive.ata.ata_security_state);
    drawKeyValue(layout, content_y + 8, layout.right_start, column_width,
                 "Security", drive.ata.security_state);
    if (two_columns) {
        drawKeyValue(layout, content_y, right_x, column_width,
                     "Power-on hours", formatPowerHours(drive));
        drawKeyValue(layout, content_y + 2, right_x, column_width,
                     "Reallocated", std::to_string(drive.smart.reallocated));
        drawKeyValue(layout, content_y + 4, right_x, column_width,
                     "Offline UNC", std::to_string(drive.smart.offline_uncorrectable));
        drawKeyValue(layout, content_y + 6, right_x, column_width,
                     "HPA", drive.ata.hpa_state);
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

std::string progressState(const DemoFeatureProgress& progress) {
    return progress.job_id ? drivelab::jobStateName(progress.state) : "IDLE";
}

std::string progressLabel(const DemoFeatureProgress& progress,
                          const std::string& idle_label) {
    if (!progress.job_id) return idle_label;
    const DemoWorkflowDefinition* workflow =
        drivelab::findDemoWorkflow(progress.workflow);
    const std::string label = workflow ? workflow->label : "Simulated workflow";
    return progress.observed_path.empty()
        ? label
        : label + " on " + progress.observed_path;
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
    if (count <= 0) return 0;
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

ContextInfo workflowContext(const DemoWorkflowDefinition& workflow) {
    return {
        workflow.label,
        workflow.help.what,
        drivelab::demoWorkflowSafetyLabel(workflow),
        workflow.help.future_provider,
        workflow.help.typical_use,
        workflow.help.risk_or_limit
    };
}

ContextInfo workflowContext(
    const std::vector<DemoWorkflowDefinition>& workflows,
    int option) {
    if (workflows.empty()) {
        return {"Unavailable", "No workflow is available.", "STATUS ONLY",
                "mock provider", "Review drive status.", "None"};
    }
    int index = std::clamp(option, 0, static_cast<int>(workflows.size()) - 1);
    return workflowContext(workflows[static_cast<std::size_t>(index)]);
}

void drawSmartTab(const Layout& layout, std::vector<HitRegion>& hits,
                  const DemoDriveDetails& drive, const DemoState& state, int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    std::ostringstream summary;
    summary << "Health " << drive.smart.health_summary
            << " . Temp " << formatTemperature(drive)
            << " . Realloc " << drive.smart.reallocated
            << " . Pending " << drive.smart.pending
            << " . Offline UNC " << drive.smart.offline_uncorrectable
            << " . Power-on " << formatPowerHours(drive);
    printPanel(layout, content_y, layout.right_start, width,
               "S.M.A.R.T health summary", A_BOLD | COLOR_PAIR(kTextPair));
    printPanel(layout, content_y + 1, layout.right_start, width, summary.str(), COLOR_PAIR(1));

    if (isProtected(drive)) {
        ContextInfo info{"Protected device", "Shows safely cached S.M.A.R.T status only.", "STATUS ONLY",
                         "mock health provider", "Confirm why this device is protected.",
                         protectionReason(drive)};
        drawContextPanel(layout, content_y + 3, info);
        return;
    }

    const std::vector<DemoWorkflowDefinition> workflows =
        featureWorkflows(state, DemoFeature::Smart);
    int option = contextualOption(state, HitKind::SmartAction, state.smart_option,
                                  static_cast<int>(workflows.size()));
    std::vector<PanelButton> buttons;
    for (int i = 0; i < static_cast<int>(workflows.size()); ++i) {
        const DemoWorkflowDefinition& workflow = workflows[static_cast<std::size_t>(i)];
        buttons.push_back({workflow.label, HitKind::SmartAction, i,
                           state.smart_option == i, !workflow.supported});
    }
    int actions_end = drawPanelButtons(layout, hits, content_y + 3, buttons);
    int context_end = drawContextPanel(
        layout, actions_end + 1, workflowContext(workflows, option));
    const DemoFeatureProgress& progress = state.snapshot().smart_progress;
    drawProgress(layout, context_end + 1,
                 progressLabel(progress, "No simulated test started"),
                 progressState(progress), progress.progress_percent);
    if (context_end + 2 >= layout.content_top && context_end + 2 <= layout.main_bottom) {
        drawButton(hits, context_end + 2, layout.right_start, "Open Workflow",
                   false, HitKind::OpenWorkflow);
    }
}

std::string formatCapacityBytes(std::uint64_t bytes) {
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    constexpr double tib = gib * 1024.0;
    std::ostringstream text;
    if (static_cast<double>(bytes) >= tib) {
        text << std::fixed << std::setprecision(2)
             << static_cast<double>(bytes) / tib << " TiB";
    } else {
        text << std::fixed << std::setprecision(1)
             << static_cast<double>(bytes) / gib << " GiB";
    }
    return text.str();
}

std::string hpaCapacitySummary(const DemoDriveDetails& drive) {
    if (!drive.ata.hpa_current_capacity_bytes || !drive.ata.hpa_native_capacity_bytes) {
        return drive.ata.hpa_state;
    }
    return formatCapacityBytes(*drive.ata.hpa_current_capacity_bytes) + " / " +
           formatCapacityBytes(*drive.ata.hpa_native_capacity_bytes);
}

std::string lowerFirst(std::string text) {
    if (!text.empty()) {
        text.front() = static_cast<char>(
            std::tolower(static_cast<unsigned char>(text.front())));
    }
    return text;
}

std::string supportSummary(
    const std::string& label,
    bool supported,
    const std::optional<std::chrono::minutes>& duration) {
    std::string summary = label + (supported ? " supported" : " not supported");
    if (supported && duration) summary += " (" + formatDuration(*duration) + ")";
    return summary;
}

void drawAtaTab(const Layout& layout, std::vector<HitRegion>& hits,
                const DemoDriveDetails& drive, const DemoState& state, int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    std::string security = drive.ata.ata_security_state == "Supported - Disabled"
        ? "Disabled/Unlocked" : drive.ata.ata_security_state;
    std::string frozen = drive.ata.security_state.find("Not Frozen") != std::string::npos
        ? "Not Frozen" : "Not Frozen (demo)";
    printPanel(layout, content_y, layout.right_start, width,
               "ATA Security / HPA / DCO", A_BOLD | COLOR_PAIR(kTextPair));
    printPanel(layout, content_y + 1, layout.right_start, width,
               "Security " + security + " . " + frozen + " . HPA " +
                   hpaCapacitySummary(drive),
               COLOR_PAIR(1));
    printPanel(layout, content_y + 2, layout.right_start, width,
               "DCO " + lowerFirst(drive.ata.dco_status) + " . " +
                   supportSummary("Secure Erase", drive.ata.secure_erase_supported,
                                  drive.ata.secure_erase_duration) + " . " +
                   supportSummary("Enhanced", drive.ata.enhanced_secure_erase_supported,
                                  drive.ata.enhanced_secure_erase_duration),
               A_DIM);

    const std::vector<DemoWorkflowDefinition> workflows =
        featureWorkflows(state, DemoFeature::AtaHpa);
    int option = contextualOption(state, HitKind::AtaAction, state.ata_option,
                                  static_cast<int>(workflows.size()));
    std::vector<PanelButton> buttons;
    for (int i = 0; i < static_cast<int>(workflows.size()); ++i) {
        const DemoWorkflowDefinition& workflow = workflows[static_cast<std::size_t>(i)];
        buttons.push_back({workflow.label, HitKind::AtaAction, i,
                           state.ata_option == i, !workflow.supported});
    }
    int actions_end = drawPanelButtons(layout, hits, content_y + 4, buttons);
    int context_end = drawContextPanel(
        layout, actions_end + 1, workflowContext(workflows, option));
    if (context_end + 1 >= layout.content_top && context_end + 1 <= layout.main_bottom) {
        drawButton(hits, context_end + 1, layout.right_start, "Open Workflow",
                   false, HitKind::OpenWorkflow);
    }
}

std::string benchmarkResult(
    const DemoDriveDetails& drive,
    DemoWorkflowId workflow_id) {
    auto result = std::find_if(
        drive.benchmark_results.begin(), drive.benchmark_results.end(),
        [&](const drivelab::DemoBenchmarkResult& value) {
            return value.workflow == workflow_id;
        });
    if (result == drive.benchmark_results.end()) return "No result";
    return result->result;
}

void drawBenchmarkTab(const Layout& layout, std::vector<HitRegion>& hits,
                      const DemoDriveDetails& drive, const DemoState& state,
                      int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    printPanel(layout, content_y, layout.right_start, width,
               "Benchmark profiles", A_BOLD | COLOR_PAIR(kTextPair));
    const std::vector<DemoWorkflowDefinition> workflows =
        featureWorkflows(state, DemoFeature::Benchmark);
    int option = contextualOption(state, HitKind::BenchmarkProfile,
                                   state.benchmark_profile,
                                   static_cast<int>(workflows.size()));
    std::vector<PanelButton> profiles;
    for (int i = 0; i < static_cast<int>(workflows.size()); ++i) {
        const DemoWorkflowDefinition& workflow = workflows[static_cast<std::size_t>(i)];
        profiles.push_back({workflow.label, HitKind::BenchmarkProfile, i,
                            state.benchmark_profile == i, !workflow.supported});
    }
    int actions_end = drawPanelButtons(layout, hits, content_y + 2, profiles);
    int context_end = drawContextPanel(
        layout, actions_end + 1, workflowContext(workflows, option));
    std::string result = workflows.empty()
        ? "No result"
        : benchmarkResult(drive, workflows[static_cast<std::size_t>(option)].id);
    printPanel(layout, context_end + 1, layout.right_start, width,
               "Representative result: " + result, A_BOLD | COLOR_PAIR(1));
    const DemoFeatureProgress& progress = state.snapshot().benchmark_progress;
    drawProgress(layout, context_end + 2,
                 progressLabel(progress, "No simulated run started"),
                 progressState(progress), progress.progress_percent);
    if (context_end + 3 >= layout.content_top && context_end + 3 <= layout.main_bottom) {
        drawButton(hits, context_end + 3, layout.right_start, "Open Workflow",
                   false, HitKind::OpenWorkflow);
    }
}

void drawSanitizeTab(const Layout& layout, std::vector<HitRegion>& hits,
                     const DemoDriveDetails&, const DemoState& state, int content_y) {
    int width = layout.right_end - layout.right_start + 1;
    const std::vector<DemoWorkflowDefinition> workflows =
        featureWorkflows(state, DemoFeature::Sanitize);
    auto recommended = std::find_if(
        workflows.begin(), workflows.end(),
        [](const DemoWorkflowDefinition& workflow) { return workflow.recommended; });
    const std::string recommended_label = recommended == workflows.end()
        ? "None" : recommended->label;
    printPanel(layout, content_y, layout.right_start, width,
               "Sanitize methods . Recommended: " + recommended_label,
               A_BOLD | COLOR_PAIR(kTextPair));
    int option = contextualOption(state, HitKind::SanitizeOption, state.sanitize_option,
                                  static_cast<int>(workflows.size()));
    std::vector<PanelButton> buttons;
    for (int i = 0; i < static_cast<int>(workflows.size()); ++i) {
        const DemoWorkflowDefinition& workflow = workflows[static_cast<std::size_t>(i)];
        buttons.push_back({workflow.label, HitKind::SanitizeOption, i,
                           state.sanitize_option == i, !workflow.supported});
    }
    int actions_end = drawPanelButtons(layout, hits, content_y + 2, buttons);
    int context_end = drawContextPanel(
        layout, actions_end + 1, workflowContext(workflows, option));
    printPanel(layout, context_end + 1, layout.right_start, width,
               "DANGER: DESTRUCTIVE WORKFLOW - DEMO SIMULATION ONLY", A_BOLD | COLOR_PAIR(4));
    if (context_end + 2 >= layout.content_top && context_end + 2 <= layout.main_bottom) {
        drawButton(hits, context_end + 2, layout.right_start, "Open Workflow",
                   false, HitKind::OpenWorkflow);
    }
}

void drawMainContent(const Layout& layout, std::vector<HitRegion>& hits, const DemoState& state,
                     const DemoDriveDetails& drive, int content_y) {
    if (content_y >= layout.main_bottom) return;
    switch (selectedFeature(state)) {
        case DemoFeature::Overview: drawOverview(layout, drive, content_y); break;
        case DemoFeature::Smart: drawSmartTab(layout, hits, drive, state, content_y); break;
        case DemoFeature::AtaHpa: drawAtaTab(layout, hits, drive, state, content_y); break;
        case DemoFeature::Benchmark:
            drawBenchmarkTab(layout, hits, drive, state, content_y);
            break;
        case DemoFeature::Sanitize: drawSanitizeTab(layout, hits, drive, state, content_y); break;
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
    if (isProtected(selectedDrive(state))) {
        return selectedFeature(state) == DemoFeature::Overview ? 8 : 10;
    }
    int width = layout.right_end - layout.right_start + 1;
    const DemoFeature feature = selectedFeature(state);
    const std::vector<std::string> labels = workflowLabels(
        featureWorkflows(state, feature));
    switch (feature) {
        case DemoFeature::Overview: return 10;
        case DemoFeature::Smart: return buttonRows(labels, width) + 12;
        case DemoFeature::AtaHpa: return buttonRows(labels, width) + 12;
        case DemoFeature::Benchmark: return buttonRows(labels, width) + 12;
        case DemoFeature::Sanitize: return buttonRows(labels, width) + 11;
    }
    return 1;
}

int maxContentOffset(const DemoState& state, const Layout& layout) {
    int viewport_height = std::max(1, layout.main_bottom - layout.content_top + 1);
    return std::max(0, mainContentHeight(state, layout) - viewport_height);
}

int activeJobCount(const DemoState& state) {
    const std::vector<JobRecord>& jobs = state.snapshot().jobs;
    return static_cast<int>(std::count_if(jobs.begin(), jobs.end(), [](const JobRecord& job) {
        return job.state == JobState::Starting || job.state == JobState::Running ||
               job.state == JobState::Paused || job.state == JobState::Queued;
    }));
}

std::string progressBar(int progress, int width = 6) {
    int filled = std::clamp(progress * width / 100, 0, width);
    return "[" + std::string(filled, '#') + std::string(width - filled, '-') + "]";
}

std::vector<std::pair<std::string, HitKind>> jobControls(const JobRecord& job) {
    std::vector<std::pair<std::string, HitKind>> controls;
    if (job.state == JobState::Queued) {
        controls.push_back({"Cancel", HitKind::JobPrimary});
        return controls;
    }
    if (job.state == JobState::Running && job.operation.controls.can_pause) {
        controls.push_back({"Pause", HitKind::JobPrimary});
    } else if (job.state == JobState::Paused && job.operation.controls.can_resume) {
        controls.push_back({"Resume", HitKind::JobPrimary});
    }
    if ((job.state == JobState::Running || job.state == JobState::Paused) &&
        job.operation.controls.can_cancel) {
        controls.push_back({"Stop", HitKind::JobStop});
    }
    return controls;
}

std::string jobStarted(const JobRecord& job) {
    return timestamp(job.started_at.value_or(job.queued_at));
}

std::string jobEta(const JobRecord& job) {
    if (job.state == JobState::Completed) return "done";
    if (!job.estimated_remaining) return "--";
    const auto seconds = std::max<std::int64_t>(0, job.estimated_remaining->count());
    std::ostringstream eta;
    eta << std::setfill('0') << std::setw(2) << seconds / 60 << ':'
        << std::setw(2) << seconds % 60;
    return eta.str();
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
    const JobRecord& job = state.snapshot().jobs[static_cast<std::size_t>(job_index)];
    const std::string state_name = drivelab::jobStateName(job.state);
    JobColumns columns = makeJobColumns(layout);
    int attributes = COLOR_PAIR(statusPair(state_name));
    drawJobCell(y, columns.id, "#" + std::to_string(job.id), attributes);
    drawJobCell(y, columns.started, jobStarted(job), attributes);
    drawJobCell(y, columns.drive, job.observed_path, attributes);
    drawJobCell(y, columns.task, job.operation.name, attributes);
    drawJobCell(y, columns.state, state_name, attributes);
    std::ostringstream progress;
    int bar_width = std::max(1, columns.progress.width - 7);
    progress << std::setw(3) << job.progress_percent << "% "
             << progressBar(job.progress_percent, bar_width);
    drawJobCell(y, columns.progress, progress.str(), attributes);
    drawJobCell(y, columns.eta, jobEta(job), attributes);

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

    const DemoSnapshot& snapshot = state.snapshot();
    int total = state.bottom_tab == 0 ? static_cast<int>(snapshot.events.size())
                                      : static_cast<int>(snapshot.jobs.size());
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
            const drivelab::DemoEvent& event =
                snapshot.events[static_cast<std::size_t>(index)];
            printClipped(y, 2, layout.width - 4,
                         timestamp(event.timestamp) + " " + event.message,
                         COLOR_PAIR(kTextPair));
        } else {
            drawJobRow(layout, hits, state, index, y);
        }
    }
    drawVerticalScrollbar(layout.bottom_entry_y, layout.width - 2, layout.bottom_rows,
                          total, layout.bottom_rows, offset);
}

ContextInfo selectedWorkflowContext(const DemoState& state) {
    const DemoFeature feature = selectedFeature(state);
    int option = 0;
    switch (feature) {
        case DemoFeature::Smart: option = state.smart_option; break;
        case DemoFeature::AtaHpa: option = state.ata_option; break;
        case DemoFeature::Benchmark: option = state.benchmark_profile; break;
        case DemoFeature::Sanitize: option = state.sanitize_option; break;
        case DemoFeature::Overview:
            return {"Overview", "No workflow is available.", "STATUS ONLY",
                    "mock provider", "Review drive status.", "None"};
    }
    return workflowContext(featureWorkflows(state, feature), option);
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
    const DemoDriveDetails& drive = selectedDrive(state);
    drawIdentity(layout, drive);
    layout.content_top = drawMainTabs(layout, hits, state);
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
    state.selected_drive = std::clamp(
        index, 0, static_cast<int>(state.drives().size()) - 1);
    if (isProtected(selectedDrive(state))) {
        if (state.main_tab > 1) state.main_tab = 0;
        return;
    }
    std::vector<DemoWorkflowDefinition> smart =
        featureWorkflows(state, DemoFeature::Smart);
    if (state.smart_option >= static_cast<int>(smart.size()) ||
        (state.smart_option >= 0 && !smart.empty() &&
         !smart[static_cast<std::size_t>(state.smart_option)].supported)) {
        state.smart_option = smart.size() > 1 ? 1 : 0;
    }
}

bool refreshCoreState(DemoState& state) {
    drivelab::Result<void> refreshed = state.read_model->refresh();
    if (!refreshed) return false;
    state.event_offset = std::clamp(
        state.event_offset, 0,
        std::max(0, static_cast<int>(state.snapshot().events.size()) - 1));
    state.job_offset = std::clamp(
        state.job_offset, 0,
        std::max(0, static_cast<int>(state.snapshot().jobs.size()) - 1));
    return true;
}

int selectedWorkflowOption(const DemoState& state, DemoFeature feature) {
    switch (feature) {
        case DemoFeature::Smart: return state.smart_option;
        case DemoFeature::AtaHpa: return state.ata_option;
        case DemoFeature::Benchmark: return state.benchmark_profile;
        case DemoFeature::Sanitize: return state.sanitize_option;
        case DemoFeature::Overview: return -1;
    }
    return -1;
}

void runSelectedWorkflowSimulation(DemoState& state) {
    const DemoFeature feature = selectedFeature(state);
    const std::vector<DemoWorkflowDefinition> workflows =
        featureWorkflows(state, feature);
    const int option = selectedWorkflowOption(state, feature);
    if (option < 0 || option >= static_cast<int>(workflows.size()) ||
        !workflows[static_cast<std::size_t>(option)].supported) return;

    drivelab::Result<drivelab::DriveId> drive_id =
        state.read_model->driveId(static_cast<std::size_t>(state.selected_drive));
    if (!drive_id) return;
    drivelab::Result<drivelab::DemoWorkflowResult> result =
        state.application->runDemoWorkflow({
            drive_id.value(), workflows[static_cast<std::size_t>(option)].id
        });
    if (!result) return;
    refreshCoreState(state);
    state.event_offset = 0;
    if (result.value().outcome == drivelab::DemoWorkflowOutcome::JobSubmitted) {
        state.bottom_tab = 1;
        state.job_offset = 0;
    } else {
        state.bottom_tab = 0;
    }
}

void applyJobPrimary(DemoState& state, int index) {
    const std::vector<JobRecord>& jobs = state.snapshot().jobs;
    if (index < 0 || index >= static_cast<int>(jobs.size())) return;
    const drivelab::JobId id = jobs[static_cast<std::size_t>(index)].id;
    const JobState job_state = jobs[static_cast<std::size_t>(index)].state;
    bool changed = false;
    if (job_state == JobState::Running) {
        changed = static_cast<bool>(state.application->pauseDemoJob(id));
    } else if (job_state == JobState::Paused) {
        changed = static_cast<bool>(state.application->resumeDemoJob(id));
    } else if (job_state == JobState::Queued) {
        changed = static_cast<bool>(state.application->cancelDemoJob(id));
    }
    if (changed) {
        refreshCoreState(state);
        state.event_offset = 0;
    }
}

void applyJobStop(DemoState& state, int index) {
    const std::vector<JobRecord>& jobs = state.snapshot().jobs;
    if (index < 0 || index >= static_cast<int>(jobs.size())) return;
    const drivelab::JobId id = jobs[static_cast<std::size_t>(index)].id;
    drivelab::Result<void> result = state.application->cancelDemoJob(id);
    if (result) {
        refreshCoreState(state);
        state.event_offset = 0;
    }
}

void scanDemoInventory(DemoState& state) {
    drivelab::Result<void> result = state.application->scanDemoInventory();
    if (!result) return;
    refreshCoreState(state);
    state.event_offset = 0;
}

int visibleTabCount(const DemoState& state) {
    return static_cast<int>(availableFeatures(state).size());
}

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
    drivelab::Result<bool> allowed = state.application->requestDemoTermination();
    refreshCoreState(state);
    if (!allowed || !allowed.value()) {
        state.bottom_tab = 0;
        state.event_offset = 0;
        return;
    }
    rememberModalReturn(state);
    state.confirm_terminate = true;
    state.terminate_choice = 1;
    state.focus_kind = HitKind::CancelTerminate;
    state.focus_value = 0;
}

void openWorkflowDialog(DemoState& state) {
    if (selectedFeature(state) == DemoFeature::Overview ||
        isProtected(selectedDrive(state))) return;
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
    int total = state.bottom_tab == 0
        ? static_cast<int>(state.snapshot().events.size())
        : static_cast<int>(state.snapshot().jobs.size());
    int& offset = state.bottom_tab == 0 ? state.event_offset : state.job_offset;
    offset = std::clamp(offset + delta, 0, std::max(0, total - rows));
    if ((state.focus_kind == HitKind::JobPrimary || state.focus_kind == HitKind::JobStop) &&
        (state.focus_value < offset || state.focus_value >= offset + rows)) {
        state.focus_kind = HitKind::BottomTab;
        state.focus_value = state.bottom_tab;
    }
}

void scrollDriveViewport(DemoState& state, int delta, const Layout& layout) {
    int max_offset = std::max(
        0, static_cast<int>(state.drives().size()) - layout.visible_drives);
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
    const std::vector<JobRecord>& jobs = state.snapshot().jobs;
    if (index < 0 || index >= static_cast<int>(jobs.size()) ||
        jobControls(jobs[static_cast<std::size_t>(index)]).empty()) {
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
            state.focus_value = std::min(static_cast<int>(state.drives().size()) - 1,
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
            state.smart_option = hit.value;
            break;
        case HitKind::AtaAction:
            state.mode = InteractionMode::FeaturePanel;
            state.ata_option = hit.value;
            break;
        case HitKind::BenchmarkProfile:
            state.mode = InteractionMode::FeaturePanel;
            state.benchmark_profile = hit.value;
            break;
        case HitKind::SanitizeOption:
            state.mode = InteractionMode::FeaturePanel;
            state.sanitize_option = hit.value;
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

bool tickCore(DemoState& state) {
    auto now = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.last_core_tick);
    if (elapsed < std::chrono::milliseconds{500}) return false;
    state.last_core_tick = now;
    drivelab::Result<void> advanced = state.application->advanceDemo(elapsed);
    if (!advanced || !refreshCoreState(state)) return false;
    normalizeJobControlFocus(state);
    return true;
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
    int last = std::min(static_cast<int>(state.drives().size()) - 1,
                        state.drive_offset + layout.visible_drives - 1);
    int index = state.focus_kind == HitKind::SelectDrive
        ? state.focus_value : (delta > 0 ? first : last);
    if (state.focus_kind == HitKind::SelectDrive) index += delta;
    state.focus_kind = HitKind::SelectDrive;
    state.focus_value = std::clamp(
        index, 0, static_cast<int>(state.drives().size()) - 1);
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

HitKind optionHitKind(DemoFeature feature) {
    switch (feature) {
        case DemoFeature::Smart: return HitKind::SmartAction;
        case DemoFeature::AtaHpa: return HitKind::AtaAction;
        case DemoFeature::Benchmark: return HitKind::BenchmarkProfile;
        case DemoFeature::Sanitize: return HitKind::SanitizeOption;
        case DemoFeature::Overview: return HitKind::None;
    }
    return HitKind::None;
}

std::vector<FocusTarget> featureTargets(const DemoState& state) {
    std::vector<FocusTarget> targets;
    const DemoFeature feature = selectedFeature(state);
    if (feature == DemoFeature::Overview || isProtected(selectedDrive(state))) {
        targets.push_back({HitKind::MainTab, state.main_tab});
        return targets;
    }

    const HitKind kind = optionHitKind(feature);
    const std::vector<DemoWorkflowDefinition> workflows =
        featureWorkflows(state, feature);
    for (int index = 0; index < static_cast<int>(workflows.size()); ++index) {
        if (workflows[static_cast<std::size_t>(index)].supported) {
            targets.push_back({kind, index});
        }
    }
    if (!workflows.empty()) {
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
    const DemoFeature feature = selectedFeature(state);
    const std::vector<std::string> labels = workflowLabels(
        featureWorkflows(state, feature));
    switch (target.kind) {
        case HitKind::SmartAction: return 3 + optionRow(labels, target.value, width);
        case HitKind::AtaAction: return 4 + optionRow(labels, target.value, width);
        case HitKind::BenchmarkProfile:
            return 2 + optionRow(labels, target.value, width);
        case HitKind::SanitizeOption:
            return 2 + optionRow(labels, target.value, width);
        case HitKind::OpenWorkflow:
            if (feature == DemoFeature::Smart) return buttonRows(labels, width) + 11;
            if (feature == DemoFeature::AtaHpa) return buttonRows(labels, width) + 11;
            if (feature == DemoFeature::Benchmark) return buttonRows(labels, width) + 11;
            if (feature == DemoFeature::Sanitize) return buttonRows(labels, width) + 10;
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
    switch (selectedFeature(state)) {
        case DemoFeature::Smart:
            preferred = {HitKind::SmartAction, state.smart_option};
            break;
        case DemoFeature::AtaHpa:
            preferred = {HitKind::AtaAction, state.ata_option};
            break;
        case DemoFeature::Benchmark:
            preferred = {HitKind::BenchmarkProfile, state.benchmark_profile};
            break;
        case DemoFeature::Sanitize:
            preferred = {HitKind::SanitizeOption, state.sanitize_option};
            break;
        case DemoFeature::Overview: break;
    }
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

void selectFeatureShortcut(DemoState& state, DemoFeature requested) {
    const std::vector<DemoFeature> features = availableFeatures(state);
    auto selected = std::find(features.begin(), features.end(), requested);
    if (selected == features.end()) return;
    int tab = static_cast<int>(std::distance(features.begin(), selected));
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
    if (key == 'o' || key == 'O') {
        selectFeatureShortcut(state, DemoFeature::Overview);
    } else if (key == 's' || key == 'S') {
        selectFeatureShortcut(state, DemoFeature::Smart);
    } else if (key == 'a' || key == 'A') {
        selectFeatureShortcut(state, DemoFeature::AtaHpa);
    } else if (key == 'b' || key == 'B') {
        selectFeatureShortcut(state, DemoFeature::Benchmark);
    } else if (key == 'n' || key == 'N') {
        selectFeatureShortcut(state, DemoFeature::Sanitize);
    }
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
    drivelab::NullLogger core_logger;
    drivelab::AppConfig core_config;
    core_config.mode = drivelab::ExecutionMode::Demo;
    auto application_result = drivelab::CoreApplication::createDemo(
        core_config, core_logger);
    if (!application_result) return 1;
    std::unique_ptr<drivelab::CoreApplication> application =
        std::move(application_result.value());
    auto read_model_result = drivelab::DemoReadModel::load(*application);
    if (!read_model_result) return 1;
    drivelab::DemoReadModel read_model = std::move(read_model_result.value());

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

    DemoState state = makeDemoState(*application, read_model);
    std::ofstream mouse_debug("/tmp/drivelab-demo-mouse.log",
                              std::ios::out | std::ios::trunc);
    bool running = true;
    bool dirty = true;
    Layout layout;
    std::vector<HitRegion> hits;
    bool button1_down = false;
    HitKind pressed_kind = HitKind::None;
    int pressed_value = 0;
    while (running) {
        dirty = tickCore(state) || dirty;
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
