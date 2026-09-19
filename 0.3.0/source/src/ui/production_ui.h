#pragma once

namespace drivelab {
class CoreApplication;
namespace tui {
// ncurses is initialized by the executable bootstrap.
int runProductionUi(CoreApplication& application);
}
}
