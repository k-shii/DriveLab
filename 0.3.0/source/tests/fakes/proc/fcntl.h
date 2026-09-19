#pragma once
// Private test ABI; production always uses Linux system headers.
constexpr int O_RDONLY = 0;
constexpr int O_CLOEXEC = 02000000;
extern "C" int open(const char*, int, ...);
