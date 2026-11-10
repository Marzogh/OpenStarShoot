#ifndef OSS_TEST_PATHS_H
#define OSS_TEST_PATHS_H
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define unlink _unlink
#define rmdir _rmdir
#else
#include <unistd.h>
#endif
#endif
