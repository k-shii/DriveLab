#pragma once

#include "app/core_application.h"

namespace drivelab {

// Native composition belongs to the application bootstrap, never the TUI.
Result<std::unique_ptr<CoreApplication>> createNativeApplication(ILogger& logger);

}  // namespace drivelab
