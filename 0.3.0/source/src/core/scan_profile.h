#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace drivelab {

// Opt-in development measurement only. No timing influences acquisition or safety.
struct ScanProfile {
    struct Stage { std::uint64_t calls = 0; double milliseconds = 0; };
    std::map<std::string, Stage, std::less<>> stages;
    std::map<std::string, std::uint64_t, std::less<>> counts;
};
inline thread_local ScanProfile* active_scan_profile = nullptr;

class ScanProfileSession {
public:
    explicit ScanProfileSession(ScanProfile* profile)
        : previous_(active_scan_profile) { active_scan_profile = profile; }
    ~ScanProfileSession() { active_scan_profile = previous_; }
    ScanProfileSession(const ScanProfileSession&) = delete;
    ScanProfileSession& operator=(const ScanProfileSession&) = delete;
private:
    ScanProfile* previous_;
};

class ScanStage {
public:
    explicit ScanStage(std::string_view name)
        : profile_(active_scan_profile), name_(name),
          start_(profile_ ? Clock::now() : Clock::time_point{}) {}
    ~ScanStage() { finish(); }
    void finish() {
        if (!profile_) return;
        auto& stage = profile_->stages[std::string(name_)];
        ++stage.calls;
        stage.milliseconds += std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
        profile_ = nullptr;
    }
    ScanStage(const ScanStage&) = delete;
    ScanStage& operator=(const ScanStage&) = delete;
private:
    using Clock = std::chrono::steady_clock;
    ScanProfile* profile_;
    std::string_view name_;
    Clock::time_point start_;
};

inline void scanCount(std::string_view name, std::uint64_t count = 1) {
    if (active_scan_profile) active_scan_profile->counts[std::string(name)] += count;
}
} // namespace drivelab
