#include "app/native_application.h"
#include "core/scan_profile.h"
#include <iomanip>
#include <iostream>
#include <set>
#include <tuple>

int main() {
    using namespace drivelab;
    NullLogger logger;
    auto app = createNativeApplication(logger);
    if (!app) { std::cerr << app.error().message << '\n'; return 1; }
    ScanProfile profile;
    std::cout << "Production acquisition profile (read-only, no safety override)\n" << std::flush;
    Result<void> result = Result<void>::success();
    {
        ScanProfileSession session(&profile);
        result = app.value()->rescanProduction();
    }
    if (!result) { std::cerr << result.error().message << '\n'; return 1; }
    auto snapshot = [&] {
        ScanProfileSession session(&profile);
        ScanStage stage("snapshot.copy");
        return app.value()->productionSnapshot();
    }();
    if (!snapshot) return 1;
    const auto& s = snapshot.value();
    std::cout << std::fixed << std::setprecision(3);
    for (const auto& [name, stage] : profile.stages)
        std::cout << "stage=" << name << " calls=" << stage.calls << " ms=" << stage.milliseconds << '\n';
    for (const auto& [name, count] : profile.counts)
        std::cout << "count=" << name << " value=" << count << '\n';
    std::set<std::tuple<OwnershipReasonCode,std::string,std::string,std::string>> gaps;
    std::size_t gap_count = 0;
    for (const auto& [source, values] : s.ownership.coverage_gaps) {
        (void)source; gap_count += values.size();
        for (const auto& r : values) gaps.emplace(r.code,r.node,r.source,r.detail);
    }
    std::cout << "nodes=" << s.inventory.nodes.size() << " physical=" << s.drives.size()
              << " claims=" << s.ownership.claims.size() << " gaps=" << gap_count
              << " unique_gaps=" << gaps.size() << " mounts=" << s.ownership.namespace_mounts.size() << '\n';
    for (const auto& d : s.drives)
        std::cout << "physical=" << d.observation.kernel_name << " status=" << driveStatusName(d.status)
                  << " kind=" << physicalDeviceKindName(d.observation.physical_kind)
                  << " reasons=" << d.reasons.size() << '\n';
    std::cout << "Nested stage times overlap; do not sum inclusive stages.\n";
}
