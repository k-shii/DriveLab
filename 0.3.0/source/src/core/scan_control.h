#pragma once

#include <exception>
#include <stop_token>

namespace drivelab {
// Cancellation aborts a generation; it never stands in for negative evidence.
struct ScanCancelled final : std::exception {
    const char* what() const noexcept override { return "Production scan cancelled"; }
};
inline thread_local std::stop_token scan_stop_token;
class ScanCancellationScope {
public:
    explicit ScanCancellationScope(std::stop_token token) : previous_(scan_stop_token) { scan_stop_token = token; }
    ~ScanCancellationScope() { scan_stop_token = previous_; }
private:
    std::stop_token previous_;
};
inline void checkScanCancelled() {
    if (scan_stop_token.stop_requested()) throw ScanCancelled{};
}
} // namespace drivelab
