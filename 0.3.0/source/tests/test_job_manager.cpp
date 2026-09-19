#include "test_support.h"

#include "jobs/job_manager.h"
#include "providers/mock_providers.h"

using namespace drivelab;

int main() {
    return test::run([] {
        MockInventoryProvider inventory;
        MockBenchmarkProvider benchmark;
        MockSanitizeProvider sanitize;
        SafetyPolicy safety;
        VectorLogger logger;
        JobManager jobs(ExecutionMode::Demo, 2, inventory, safety, logger);

        const Drive first = inventory.drives()[0];
        const Drive second = inventory.drives()[1];
        OperationRequest first_a = benchmark.plan(first, BenchmarkProfile::QuickRead).value();
        OperationRequest first_b = benchmark.plan(first, BenchmarkProfile::HddCharacterization).value();
        first_b.target.current_path = "/dev/sdz";
        first_b.target.identity.topology = "pci-0000:09:00.0-ata-9";
        OperationRequest second_a = benchmark.plan(second, BenchmarkProfile::QuickRead).value();

        JobId first_id = jobs.submit(first_a).value();
        JobId queued_id = jobs.submit(first_b).value();
        JobId second_id = jobs.submit(second_a).value();
        DL_CHECK(jobs.find(first_id)->state == JobState::Running);
        DL_CHECK(jobs.find(queued_id)->state == JobState::Queued);
        DL_CHECK(jobs.find(first_id)->drive_id == jobs.find(queued_id)->drive_id);
        DL_CHECK(jobs.find(queued_id)->observed_path == "/dev/sdz");
        DL_CHECK(jobs.find(second_id)->state == JobState::Running);
        DL_CHECK(jobs.activeDriveCount() == 2);

        DL_CHECK(jobs.pause(first_id));
        DL_CHECK(jobs.find(first_id)->state == JobState::Paused);
        DL_CHECK(jobs.find(queued_id)->state == JobState::Queued);
        DL_CHECK(jobs.activeDriveCount() == 2);
        DL_CHECK(jobs.cancel(queued_id));
        DL_CHECK(jobs.find(queued_id)->state == JobState::Cancelled);

        JobId replacement_id = jobs.submit(first_b).value();
        DL_CHECK(jobs.find(replacement_id)->state == JobState::Queued);
        DL_CHECK(jobs.complete(first_id));
        DL_CHECK(jobs.find(replacement_id)->state == JobState::Running);
        DL_CHECK(jobs.find(replacement_id)->observed_path == first.current_path);

        const Drive protected_drive = inventory.drives()[2];
        Result<JobId> protected_job = jobs.submit(
            benchmark.plan(protected_drive, BenchmarkProfile::QuickRead).value());
        DL_CHECK(!protected_job);
        DL_CHECK(protected_job.error().code == ErrorCode::SafetyBlocked);

        MockInventoryProvider changing_inventory;
        VectorLogger changing_logger;
        JobManager changing_jobs(
            ExecutionMode::Demo, 1, changing_inventory, safety, changing_logger);
        const Drive changing_drive = changing_inventory.drives()[0];
        OperationRequest active_operation = benchmark.plan(
            changing_drive, BenchmarkProfile::QuickRead).value();
        OperationRequest waiting_operation = benchmark.plan(
            changing_drive, BenchmarkProfile::HddCharacterization).value();
        JobId active_id = changing_jobs.submit(active_operation).value();
        JobId waiting_id = changing_jobs.submit(waiting_operation).value();
        Drive replacement = changing_drive;
        replacement.identity.model = "Unexpected replacement model";
        changing_inventory.replace(0, replacement);
        DL_CHECK(changing_jobs.complete(active_id));
        DL_CHECK(changing_jobs.find(waiting_id)->state == JobState::Failed);
        DL_CHECK(changing_jobs.find(waiting_id)->failure->code == ErrorCode::IdentityMismatch);

        MockInventoryProvider dry_inventory;
        VectorLogger dry_logger;
        JobManager dry_jobs(ExecutionMode::DryRun, 2, dry_inventory, safety, dry_logger);
        JobId planned_id = dry_jobs.submit(
            benchmark.plan(dry_inventory.drives()[0], BenchmarkProfile::QuickRead).value()).value();
        DL_CHECK(dry_jobs.find(planned_id)->state == JobState::Planned);
        DL_CHECK(dry_jobs.activeDriveCount() == 0);

        MockInventoryProvider locked_inventory;
        VectorLogger locked_logger;
        JobManager locked_jobs(ExecutionMode::Demo, 2, locked_inventory, safety, locked_logger);
        JobId locked_id = locked_jobs.submit(
            sanitize.plan(locked_inventory.drives()[0], SanitizeMethod::SecureErase).value()).value();
        DL_CHECK(locked_jobs.find(locked_id)->state == JobState::Running);
        DL_CHECK(!locked_jobs.canTerminate());
        DL_CHECK(!locked_jobs.cancel(locked_id));
    });
}
