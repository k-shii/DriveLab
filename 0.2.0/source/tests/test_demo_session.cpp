#include "test_support.h"

#include "app/core_application.h"

#include <algorithm>
#include <chrono>
#include <string>

using namespace drivelab;

namespace {

std::unique_ptr<CoreApplication> makeDemo(VectorLogger& logger) {
    AppConfig config;
    config.mode = ExecutionMode::Demo;
    config.max_parallel_drives = 2;
    Result<std::unique_ptr<CoreApplication>> application =
        CoreApplication::createDemo(config, logger);
    DL_CHECK(application);
    return std::move(application.value());
}

DriveId idOf(const DemoDriveDetails& details) {
    Result<DriveId> id = details.drive.identity.driveId();
    DL_CHECK(id);
    return id.value();
}

const JobRecord* findJob(const DemoSnapshot& snapshot, JobId id) {
    auto found = std::find_if(snapshot.jobs.begin(), snapshot.jobs.end(),
                              [id](const JobRecord& job) { return job.id == id; });
    return found == snapshot.jobs.end() ? nullptr : &*found;
}

bool hasEvent(const DemoSnapshot& snapshot, const std::string& text) {
    return std::any_of(snapshot.events.begin(), snapshot.events.end(),
                       [&](const DemoEvent& event) {
                           return event.message.find(text) != std::string::npos;
                       });
}

bool isActiveOrQueued(JobState state) {
    return state == JobState::Queued || state == JobState::Starting ||
           state == JobState::Running || state == JobState::Paused;
}

}  // namespace

int main() {
    return test::run([] {
        VectorLogger fixture_logger;
        std::unique_ptr<CoreApplication> fixture_app = makeDemo(fixture_logger);
        DemoSnapshot fixture = fixture_app->demoSnapshot().value();

        DL_CHECK(fixture.drives.size() == 7);
        DL_CHECK(fixture.events.size() == 1);
        DL_CHECK(hasEvent(fixture, "Demo session started"));
        DL_CHECK(fixture.jobs.empty());
        DL_CHECK(std::none_of(fixture.jobs.begin(), fixture.jobs.end(),
                              [](const JobRecord& job) {
                                  return isActiveOrQueued(job.state);
                              }));
        DL_CHECK(fixture_app->jobs().activeDriveCount() == 0);
        DL_CHECK(!fixture.smart_progress.job_id);
        DL_CHECK(fixture.smart_progress.state == JobState::Planned);
        DL_CHECK(fixture.smart_progress.progress_percent == 0);
        DL_CHECK(!fixture.benchmark_progress.job_id);
        DL_CHECK(fixture.benchmark_progress.state == JobState::Planned);
        DL_CHECK(fixture.benchmark_progress.progress_percent == 0);

        DL_CHECK(demoWorkflowCatalog().size() == 19);
        DL_CHECK(findDemoWorkflow(DemoWorkflowId::SmartShortTest)->safety_class ==
                 SafetyClass::FirmwareSelfTest);
        DL_CHECK(demoWorkflowSafetyLabel(
                     *findDemoWorkflow(DemoWorkflowId::SmartShortTest)) == "READ-ONLY");
        DL_CHECK(demoWorkflowSafetyLabel(
                     *findDemoWorkflow(DemoWorkflowId::AtaSecureErase)) == "DESTRUCTIVE");
        DL_CHECK(fixture.drives[0].drive.current_path == "/dev/sdd");
        DL_CHECK(fixture.drives[0].display_capacity == "596.2 GiB");
        DL_CHECK(fixture.drives[0].transport == "SATA 3 Gb/s");
        DL_CHECK(fixture.drives[0].smart.health_summary == "PASSED");
        DL_CHECK(fixture.drives[0].smart.temperature_celsius == 35);
        DL_CHECK(fixture.drives[0].smart.power_on_hours == 37256);
        DL_CHECK(fixture.drives[5].drive.status == DriveStatus::Failing);
        DL_CHECK(fixture.drives[5].smart.reallocated == 18);
        DL_CHECK(fixture.drives[5].smart.pending == 7);
        DL_CHECK(fixture.drives[5].smart.offline_uncorrectable == 3);
        DL_CHECK(fixture.drives[0].benchmark_results.size() == 5);
        DL_CHECK(fixture.drives[0].benchmark_results[0].result ==
                 "184.6 MiB/s avg . 192.3 MiB/s peak");

        const DriveId ready_id = idOf(fixture.drives[0]);
        const DriveId protected_id = idOf(fixture.drives[2]);
        const DriveId failing_id = idOf(fixture.drives[5]);
        Result<std::vector<DemoFeature>> ready_features =
            fixture_app->demoFeatures(ready_id);
        Result<std::vector<DemoFeature>> protected_features =
            fixture_app->demoFeatures(protected_id);
        DL_CHECK(ready_features && ready_features.value().size() == 5);
        DL_CHECK(protected_features && protected_features.value().size() == 2);
        DL_CHECK(protected_features.value()[1] == DemoFeature::Smart);
        DL_CHECK(fixture_app->demoWorkflows(protected_id, DemoFeature::Smart)
                     .value().empty());
        Result<std::vector<DemoWorkflowDefinition>> failing_smart =
            fixture_app->demoWorkflows(failing_id, DemoFeature::Smart);
        DL_CHECK(failing_smart && failing_smart.value().size() == 4);
        DL_CHECK(!failing_smart.value()[2].supported);
        DL_CHECK(findDemoWorkflow(DemoWorkflowId::SanitizeAtaEnhancedSecureErase)
                     ->recommended);
        DL_CHECK(findDemoWorkflow(DemoWorkflowId::AtaSecureErase)
                     ->estimated_duration == std::chrono::minutes{102});
        DL_CHECK(findDemoWorkflow(DemoWorkflowId::BenchmarkLatency)
                     ->help.future_provider == "ioping latency adapter");

        const std::size_t blocked_events = fixture.events.size();
        Result<DemoWorkflowResult> blocked = fixture_app->runDemoWorkflow({
            protected_id, DemoWorkflowId::BenchmarkQuickSequential
        });
        DL_CHECK(!blocked && blocked.error().code == ErrorCode::SafetyBlocked);
        DL_CHECK(fixture_app->demoSnapshot().value().jobs.empty());
        DL_CHECK(fixture_app->demoSnapshot().value().events.size() == blocked_events);

        Result<DemoWorkflowResult> inspection = fixture_app->runDemoWorkflow({
            ready_id, DemoWorkflowId::SmartRawData
        });
        DL_CHECK(inspection);
        DL_CHECK(inspection.value().outcome == DemoWorkflowOutcome::InspectionCompleted);
        DL_CHECK(!inspection.value().job_id);
        DemoSnapshot after_inspection = fixture_app->demoSnapshot().value();
        DL_CHECK(after_inspection.jobs.empty());
        DL_CHECK(hasEvent(after_inspection, "Raw S.M.A.R.T demo opened"));
        DL_CHECK(fixture_app->scanDemoInventory());
        DL_CHECK(hasEvent(fixture_app->demoSnapshot().value(), "Demo scan complete"));
        DL_CHECK(fixture_app->processRunner().planned().empty());

        VectorLogger scheduler_logger;
        std::unique_ptr<CoreApplication> scheduler_app = makeDemo(scheduler_logger);
        DemoSnapshot scheduler_fixture = scheduler_app->demoSnapshot().value();
        const DriveId scheduler_ready = idOf(scheduler_fixture.drives[0]);
        const DriveId scheduler_other = idOf(scheduler_fixture.drives[6]);

        Result<DemoWorkflowResult> first = scheduler_app->runDemoWorkflow({
            scheduler_ready, DemoWorkflowId::BenchmarkQuickSequential
        });
        Result<DemoWorkflowResult> same_drive = scheduler_app->runDemoWorkflow({
            scheduler_ready, DemoWorkflowId::SmartShortTest
        });
        Result<DemoWorkflowResult> other_drive = scheduler_app->runDemoWorkflow({
            scheduler_other, DemoWorkflowId::BenchmarkRandom4kQd1
        });
        DL_CHECK(first && first.value().job_id == JobId{1});
        DL_CHECK(same_drive && same_drive.value().job_id == JobId{2});
        DL_CHECK(other_drive && other_drive.value().job_id == JobId{3});

        DemoSnapshot submitted = scheduler_app->demoSnapshot().value();
        DL_CHECK(submitted.jobs.size() == 3);
        DL_CHECK(findJob(submitted, 1)->state == JobState::Running);
        DL_CHECK(findJob(submitted, 2)->state == JobState::Queued);
        DL_CHECK(findJob(submitted, 3)->state == JobState::Running);
        DL_CHECK(findJob(submitted, 1)->drive_id == scheduler_ready);
        DL_CHECK(findJob(submitted, 2)->drive_id == scheduler_ready);
        DL_CHECK(findJob(submitted, 3)->drive_id == scheduler_other);
        DL_CHECK(findJob(submitted, 1)->drive_id != findJob(submitted, 3)->drive_id);
        DL_CHECK(hasEvent(submitted, "created for /dev/sdd"));
        DL_CHECK(scheduler_app->jobs().activeDriveCount() == 2);

        DL_CHECK(scheduler_app->advanceDemo(std::chrono::milliseconds{499}));
        DL_CHECK(findJob(scheduler_app->demoSnapshot().value(), 1)->progress_percent == 0);
        DL_CHECK(scheduler_app->advanceDemo(std::chrono::milliseconds{1}));
        DemoSnapshot advanced = scheduler_app->demoSnapshot().value();
        DL_CHECK(findJob(advanced, 1)->progress_percent == 2);
        DL_CHECK(findJob(advanced, 2)->progress_percent == 0);
        DL_CHECK(findJob(advanced, 3)->progress_percent == 2);

        DL_CHECK(scheduler_app->pauseDemoJob(1));
        DL_CHECK(findJob(scheduler_app->demoSnapshot().value(), 1)->state ==
                 JobState::Paused);
        DL_CHECK(hasEvent(scheduler_app->demoSnapshot().value(), "Job #1 paused"));
        DL_CHECK(scheduler_app->advanceDemo(std::chrono::milliseconds{500}));
        advanced = scheduler_app->demoSnapshot().value();
        DL_CHECK(findJob(advanced, 1)->progress_percent == 2);
        DL_CHECK(findJob(advanced, 3)->progress_percent == 4);
        DL_CHECK(scheduler_app->resumeDemoJob(1));
        DL_CHECK(findJob(scheduler_app->demoSnapshot().value(), 1)->state ==
                 JobState::Running);
        DL_CHECK(hasEvent(scheduler_app->demoSnapshot().value(), "Job #1 resumed"));
        DL_CHECK(scheduler_app->cancelDemoJob(1));
        advanced = scheduler_app->demoSnapshot().value();
        DL_CHECK(findJob(advanced, 1)->state == JobState::Cancelled);
        DL_CHECK(findJob(advanced, 2)->state == JobState::Running);
        DL_CHECK(advanced.smart_progress.job_id == JobId{2});
        DL_CHECK(advanced.smart_progress.state == JobState::Running);
        DL_CHECK(hasEvent(advanced, "Queued job #2 started"));
        DL_CHECK(scheduler_app->cancelDemoJob(2));
        DL_CHECK(findJob(scheduler_app->demoSnapshot().value(), 2)->state ==
                 JobState::Cancelled);
        DL_CHECK(scheduler_app->processRunner().planned().empty());

        VectorLogger completion_logger;
        std::unique_ptr<CoreApplication> completion_app = makeDemo(completion_logger);
        DemoSnapshot completion_fixture = completion_app->demoSnapshot().value();
        const DriveId completion_target = idOf(completion_fixture.drives[0]);
        DL_CHECK(completion_app->runDemoWorkflow({
            completion_target, DemoWorkflowId::BenchmarkQuickSequential
        }));
        DL_CHECK(completion_app->runDemoWorkflow({
            completion_target, DemoWorkflowId::SmartExtendedTest
        }));
        DL_CHECK(completion_app->advanceDemo(std::chrono::seconds{25}));
        DemoSnapshot completed = completion_app->demoSnapshot().value();
        DL_CHECK(findJob(completed, 1)->state == JobState::Completed);
        DL_CHECK(findJob(completed, 1)->progress_percent == 100);
        DL_CHECK(findJob(completed, 2)->state == JobState::Running);
        DL_CHECK(findJob(completed, 2)->progress_percent == 0);
        DL_CHECK(findJob(completed, 2)->estimated_remaining ==
                 std::chrono::seconds{25});
        DL_CHECK(hasEvent(completed, "Job #1 completed"));
        DL_CHECK(hasEvent(completed, "Queued job #2 started"));

        VectorLogger failure_logger;
        std::unique_ptr<CoreApplication> failure_app = makeDemo(failure_logger);
        DemoSnapshot failure_fixture = failure_app->demoSnapshot().value();
        const DriveId failure_target = idOf(failure_fixture.drives[5]);
        DL_CHECK(failure_app->runDemoWorkflow({
            failure_target, DemoWorkflowId::SmartShortTest
        }));
        DL_CHECK(failure_app->runDemoWorkflow({
            failure_target, DemoWorkflowId::BenchmarkQuickSequential
        }));
        DL_CHECK(failure_app->advanceDemo(std::chrono::seconds{5}));
        DemoSnapshot failed = failure_app->demoSnapshot().value();
        DL_CHECK(findJob(failed, 1)->state == JobState::Failed);
        DL_CHECK(findJob(failed, 1)->progress_percent == 20);
        DL_CHECK(findJob(failed, 1)->failure);
        DL_CHECK(findJob(failed, 1)->failure->code == ErrorCode::ProcessFailure);
        DL_CHECK(findJob(failed, 2)->state == JobState::Running);
        DL_CHECK(hasEvent(failed, "failed (simulated S.M.A.R.T failure)"));
        DL_CHECK(hasEvent(failed, "Queued job #2 started"));

        VectorLogger catalog_logger;
        std::unique_ptr<CoreApplication> catalog_app = makeDemo(catalog_logger);
        const DriveId catalog_target = idOf(catalog_app->demoSnapshot().value().drives[0]);
        int inspections = 0;
        int submitted_jobs = 0;
        for (const DemoWorkflowDefinition& workflow : demoWorkflowCatalog()) {
            Result<DemoWorkflowResult> result = catalog_app->runDemoWorkflow({
                catalog_target, workflow.id
            });
            DL_CHECK(result);
            if (result.value().outcome == DemoWorkflowOutcome::InspectionCompleted) {
                ++inspections;
            } else {
                ++submitted_jobs;
                DL_CHECK(result.value().job_id);
            }
        }
        DL_CHECK(inspections == 4);
        DL_CHECK(submitted_jobs == 15);
        DL_CHECK(catalog_app->demoSnapshot().value().jobs.size() == 15);
        DL_CHECK(catalog_app->processRunner().planned().empty());

        VectorLogger irreversible_logger;
        std::unique_ptr<CoreApplication> irreversible_app = makeDemo(irreversible_logger);
        const DriveId irreversible_target =
            idOf(irreversible_app->demoSnapshot().value().drives[0]);
        Result<DemoWorkflowResult> irreversible = irreversible_app->runDemoWorkflow({
            irreversible_target, DemoWorkflowId::SanitizeAtaEnhancedSecureErase
        });
        DL_CHECK(irreversible && irreversible.value().job_id == JobId{1});
        DL_CHECK(!irreversible_app->canTerminateDemo().value());
        DL_CHECK(!irreversible_app->requestDemoTermination().value());
        DL_CHECK(hasEvent(irreversible_app->demoSnapshot().value(),
                          "termination blocked"));
        DL_CHECK(!irreversible_app->cancelDemoJob(1));
        DL_CHECK(irreversible_app->processRunner().planned().empty());

        AppConfig invalid_demo_config;
        invalid_demo_config.mode = ExecutionMode::Demo;
        invalid_demo_config.max_parallel_drives = 0;
        DL_CHECK(!CoreApplication::createDemo(invalid_demo_config,
                                               irreversible_logger));
    });
}
