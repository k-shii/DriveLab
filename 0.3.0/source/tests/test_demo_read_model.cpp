#include "test_support.h"

#include "app/core_application.h"
#include "ui/demo_read_model.h"

#include <array>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

using namespace drivelab;

namespace {

std::unique_ptr<CoreApplication> makeDemo(VectorLogger& logger) {
    AppConfig config;
    config.mode = ExecutionMode::Demo;
    Result<std::unique_ptr<CoreApplication>> application =
        CoreApplication::createDemo(config, logger);
    DL_CHECK(application);
    return std::move(application.value());
}

std::vector<DemoWorkflowDefinition> catalogFor(DemoFeature feature) {
    std::vector<DemoWorkflowDefinition> result;
    for (const DemoWorkflowDefinition& workflow : demoWorkflowCatalog()) {
        if (workflow.feature == feature) result.push_back(workflow);
    }
    return result;
}

void checkUnchanged(const DemoSnapshot& before, const DemoSnapshot& after) {
    DL_CHECK(after.drives.size() == before.drives.size());
    DL_CHECK(after.events.size() == before.events.size());
    DL_CHECK(after.jobs.size() == before.jobs.size());
    DL_CHECK(after.smart_progress.drive_id == before.smart_progress.drive_id);
    DL_CHECK(after.smart_progress.state == before.smart_progress.state);
    DL_CHECK(after.smart_progress.progress_percent ==
             before.smart_progress.progress_percent);
    DL_CHECK(after.benchmark_progress.drive_id ==
             before.benchmark_progress.drive_id);
    DL_CHECK(after.benchmark_progress.state == before.benchmark_progress.state);
    DL_CHECK(after.benchmark_progress.progress_percent ==
             before.benchmark_progress.progress_percent);

    for (std::size_t index = 0; index < before.events.size(); ++index) {
        DL_CHECK(after.events[index].message == before.events[index].message);
    }
    for (std::size_t index = 0; index < before.jobs.size(); ++index) {
        DL_CHECK(after.jobs[index].id == before.jobs[index].id);
        DL_CHECK(after.jobs[index].drive_id == before.jobs[index].drive_id);
        DL_CHECK(after.jobs[index].state == before.jobs[index].state);
        DL_CHECK(after.jobs[index].progress_percent ==
                 before.jobs[index].progress_percent);
    }
}

}  // namespace

int main() {
    return test::run([] {
        VectorLogger logger;
        std::unique_ptr<CoreApplication> application = makeDemo(logger);
        DemoSnapshot before = application->demoSnapshot().value();

        Result<DemoReadModel> loaded = DemoReadModel::load(*application);
        DL_CHECK(loaded);
        DemoReadModel model = std::move(loaded.value());
        DL_CHECK(model.snapshot().jobs.empty());
        DL_CHECK(model.snapshot().events.size() == 1);

        const auto& drives = model.drives();
        DL_CHECK(drives.size() == 7);
        const std::array<DriveStatus, 7> expected_statuses = {
            DriveStatus::Ready,
            DriveStatus::Ready,
            DriveStatus::Protected,
            DriveStatus::Protected,
            DriveStatus::Protected,
            DriveStatus::Failing,
            DriveStatus::Degraded
        };
        const std::array<const char*, 7> expected_status_names = {
            "READY", "READY", "PROTECTED", "PROTECTED", "PROTECTED",
            "FAILING", "DEGRADED"
        };
        for (std::size_t index = 0; index < drives.size(); ++index) {
            DL_CHECK(drives[index].drive.status == expected_statuses[index]);
            DL_CHECK(driveStatusName(drives[index].drive.status) ==
                     expected_status_names[index]);
            Result<DriveId> projected_id = model.driveId(index);
            Result<DriveId> source_id = before.drives[index].drive.identity.driveId();
            DL_CHECK(projected_id && source_id);
            DL_CHECK(projected_id.value() == source_id.value());
            DL_CHECK(model.driveId(index).value() == projected_id.value());
            DL_CHECK(drives[index].drive.current_path ==
                     before.drives[index].drive.current_path);
            DL_CHECK(drives[index].drive.identity.model ==
                     before.drives[index].drive.identity.model);
            DL_CHECK(drives[index].drive.identity.serial ==
                     before.drives[index].drive.identity.serial);
            DL_CHECK(drives[index].display_capacity ==
                     before.drives[index].display_capacity);
            DL_CHECK(drives[index].transport == before.drives[index].transport);
            DL_CHECK(drives[index].smart.health_summary ==
                     before.drives[index].smart.health_summary);
            DL_CHECK(drives[index].smart.temperature_celsius ==
                     before.drives[index].smart.temperature_celsius);
            DL_CHECK(drives[index].ata.ata_security_state ==
                     before.drives[index].ata.ata_security_state);
            DL_CHECK(drives[index].ata.hpa_state == before.drives[index].ata.hpa_state);
            DL_CHECK(drives[index].benchmark_results.size() ==
                     before.drives[index].benchmark_results.size());
        }

        DL_CHECK(drives[0].drive.current_path == "/dev/sdd");
        DL_CHECK(drives[0].display_capacity == "596.2 GiB");
        DL_CHECK(drives[0].drive.identity.model == "WDC WD6400AAKS-75A7B0");
        DL_CHECK(drives[0].drive.identity.serial == "WD-WMASY1520445");
        DL_CHECK(drives[0].drive.media == MediaKind::Hdd);
        DL_CHECK(drives[0].smart.health_summary == "PASSED");
        DL_CHECK(drives[0].ata.hpa_current_capacity_bytes ==
                 drives[0].drive.identity.capacity_bytes);
        DL_CHECK(drives[5].smart.health_summary == "FAILED");
        DL_CHECK(driveStatusName(drives[5].drive.status) == "FAILING");

        const std::array<std::size_t, 3> protected_indices = {2, 3, 4};
        for (std::size_t index : protected_indices) {
            Result<std::vector<DemoFeature>> features = model.features(index);
            DL_CHECK(features && features.value().size() == 2);
            DL_CHECK(features.value()[0] == DemoFeature::Overview);
            DL_CHECK(features.value()[1] == DemoFeature::Smart);
            DL_CHECK(model.workflows(index, DemoFeature::Smart).value().empty());
            DL_CHECK(model.workflows(index, DemoFeature::Sanitize).value().empty());
        }

        const std::array<DemoFeature, 5> expected_features = {
            DemoFeature::Overview,
            DemoFeature::Smart,
            DemoFeature::AtaHpa,
            DemoFeature::Benchmark,
            DemoFeature::Sanitize
        };
        Result<std::vector<DemoFeature>> ready_features = model.features(0);
        DL_CHECK(ready_features);
        DL_CHECK(ready_features.value() ==
                 std::vector<DemoFeature>(expected_features.begin(), expected_features.end()));

        for (DemoFeature feature : expected_features) {
            Result<std::vector<DemoWorkflowDefinition>> projected =
                model.workflows(0, feature);
            DL_CHECK(projected);
            std::vector<DemoWorkflowDefinition> expected = catalogFor(feature);
            DL_CHECK(projected.value().size() == expected.size());
            for (std::size_t index = 0; index < expected.size(); ++index) {
                DL_CHECK(projected.value()[index].id == expected[index].id);
                DL_CHECK(projected.value()[index].label == expected[index].label);
                DL_CHECK(projected.value()[index].supported == expected[index].supported);
                DL_CHECK(projected.value()[index].help.what == expected[index].help.what);
                DL_CHECK(projected.value()[index].help.future_provider ==
                         expected[index].help.future_provider);
                DL_CHECK(projected.value()[index].help.typical_use ==
                         expected[index].help.typical_use);
                DL_CHECK(projected.value()[index].help.risk_or_limit ==
                         expected[index].help.risk_or_limit);
            }
        }

        Result<std::vector<DemoWorkflowDefinition>> nvme_smart =
            model.workflows(5, DemoFeature::Smart);
        DL_CHECK(nvme_smart && nvme_smart.value().size() == 4);
        DL_CHECK(!nvme_smart.value()[2].supported);
        DL_CHECK(!findDemoWorkflow(DemoWorkflowId::SmartShortTest)
                      ->estimated_duration);
        DL_CHECK(findDemoWorkflow(DemoWorkflowId::AtaSecureErase)
                     ->estimated_duration == std::chrono::minutes{102});

        DL_CHECK(model.drive(7) == nullptr);
        Result<DriveId> missing_id = model.driveId(7);
        DL_CHECK(!missing_id && missing_id.error().code == ErrorCode::NotFound);
        DL_CHECK(!model.features(7));
        DL_CHECK(!model.workflows(7, DemoFeature::Smart));

        DemoSnapshot after = application->demoSnapshot().value();
        checkUnchanged(before, after);

        Result<DriveId> workflow_drive = model.driveId(0);
        DL_CHECK(workflow_drive);
        Result<DemoWorkflowResult> submitted = application->runDemoWorkflow({
            workflow_drive.value(), DemoWorkflowId::BenchmarkQuickSequential
        });
        DL_CHECK(submitted && submitted.value().job_id == JobId{1});
        DL_CHECK(model.snapshot().jobs.empty());
        DL_CHECK(model.refresh());
        DL_CHECK(model.snapshot().jobs.size() == 1);
        DL_CHECK(model.snapshot().jobs[0].id == JobId{1});
        DL_CHECK(model.snapshot().jobs[0].state == JobState::Running);
        DL_CHECK(model.snapshot().events.size() == before.events.size() + 1);

        DL_CHECK(application->advanceDemo(std::chrono::milliseconds{500}));
        DL_CHECK(model.snapshot().jobs[0].progress_percent == 0);
        DL_CHECK(model.refresh());
        DL_CHECK(model.snapshot().jobs[0].progress_percent == 2);
        DL_CHECK(model.snapshot().benchmark_progress.job_id == JobId{1});
        DL_CHECK(model.snapshot().benchmark_progress.progress_percent == 2);
        DL_CHECK(application->processRunner().planned().empty());
    });
}
