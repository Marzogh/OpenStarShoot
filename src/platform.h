#ifndef OSS_PLATFORM_H
#define OSS_PLATFORM_H
#include <stdint.h>
#include <stdio.h>

/* Private operating-system helpers; camera protocol stays in openstarshoot.c. */
uint64_t oss_monotonic_ms(void);
double oss_monotonic_seconds(void);
double oss_realtime_seconds(void);
void oss_sleep_ms(unsigned milliseconds);
int oss_make_directory(const char *path);
FILE *oss_create_file(const char *path);
#endif
