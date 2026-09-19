#pragma once
#include <cstdint>
struct stat {
    unsigned st_mode = 0;
    std::uint64_t st_rdev = 0;
};
constexpr unsigned fake_block_mode = 0060000;
#define S_ISBLK(mode) (((mode) & 0170000) == fake_block_mode)
extern "C" int stat(const char*, struct stat*);
