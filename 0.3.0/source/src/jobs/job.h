#pragma once

#include "core/operation.h"
#include "core/result.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace drivelab {

using JobId = std::uint64_t;

enum class JobState {
    Planned,
    Queued,
    Starting,
    Running,
    Paused,
    Completed,
    Cancelled,
    Failed
};

struct JobRecord {
    JobId id = 0;
    OperationRequest operation;
    DriveId drive_id;
    std::string observed_path;
    JobState state = JobState::Queued;
    int progress_percent = 0;
    std::optional<std::chrono::seconds> estimated_remaining;
    std::chrono::system_clock::time_point queued_at = std::chrono::system_clock::now();
    std::optional<std::chrono::system_clock::time_point> started_at;
    std::optional<std::chrono::system_clock::time_point> finished_at;
    std::optional<Error> failure;
};

std::string jobStateName(JobState state);

}  // namespace drivelab
