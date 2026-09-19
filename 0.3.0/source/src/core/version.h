#pragma once

#include <string_view>

#ifndef DRIVELAB_VERSION
#define DRIVELAB_VERSION "0.3.0"
#endif

namespace drivelab {

inline constexpr std::string_view kVersion = DRIVELAB_VERSION;

}  // namespace drivelab
