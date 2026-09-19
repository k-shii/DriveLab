#pragma once
// Private syscall test double; native builds use the Linux system declaration.
struct stat { unsigned st_mode = 0; };
#define S_ISCHR(mode) (((mode) & 0170000) == 0020000)
extern "C" int fstat(int, struct stat*);
