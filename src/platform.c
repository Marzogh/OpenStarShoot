#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>

uint64_t oss_monotonic_ms(void) { return GetTickCount64(); }
double oss_monotonic_seconds(void) {
    LARGE_INTEGER ticks, frequency;
    QueryPerformanceCounter(&ticks);
    QueryPerformanceFrequency(&frequency);
    return (double)ticks.QuadPart / (double)frequency.QuadPart;
}
double oss_realtime_seconds(void) {
    FILETIME time;
    ULARGE_INTEGER ticks;
    GetSystemTimeAsFileTime(&time);
    ticks.LowPart=time.dwLowDateTime;
    ticks.HighPart=time.dwHighDateTime;
    return (double)(ticks.QuadPart-116444736000000000ULL)/10000000.0;
}
void oss_sleep_ms(unsigned milliseconds) { Sleep(milliseconds); }
int oss_make_directory(const char *path) { return _mkdir(path); }
FILE *oss_create_file(const char *path) {
    int fd=_open(path,_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
    FILE *file;
    if(fd<0)return NULL;
    file=_fdopen(fd,"wb");
    if(!file)_close(fd);
    return file;
}
#else
#include <time.h>
#include <unistd.h>

uint64_t oss_monotonic_ms(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC,&time);
    return (uint64_t)time.tv_sec*1000u+(uint64_t)time.tv_nsec/1000000u;
}
double oss_monotonic_seconds(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC,&time);
    return (double)time.tv_sec+(double)time.tv_nsec/1e9;
}
double oss_realtime_seconds(void) {
    struct timespec time;
    clock_gettime(CLOCK_REALTIME,&time);
    return (double)time.tv_sec+(double)time.tv_nsec/1e9;
}
void oss_sleep_ms(unsigned milliseconds) {
    struct timespec nap={(time_t)(milliseconds/1000),(long)(milliseconds%1000)*1000000L};
    nanosleep(&nap,NULL);
}
int oss_make_directory(const char *path) { return mkdir(path,0755); }
FILE *oss_create_file(const char *path) {
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0644);
    FILE *file;
    if(fd<0)return NULL;
    file=fdopen(fd,"wb");
    if(!file)close(fd);
    return file;
}
#endif
