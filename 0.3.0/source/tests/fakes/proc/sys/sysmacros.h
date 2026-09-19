#pragma once
#include <cstdint>
// Test-only encoding; no claim to reproduce Linux dev_t's binary encoding.
inline std::uint32_t major(std::uint64_t value) { return static_cast<std::uint32_t>(value >> 32); }
inline std::uint32_t minor(std::uint64_t value) { return static_cast<std::uint32_t>(value); }
