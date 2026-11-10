#ifndef OSS_INTERNAL_H
#define OSS_INTERNAL_H
#include "openstarshoot.h"
#include <libusb.h>
struct oss_camera {
    libusb_context *context;
    libusb_device_handle *device;
    oss_trace_fn trace;
    void *trace_user;
    uint8_t profile;
    int initialized, synchronized, claimed;
    oss_phase phase;
    size_t expected_bytes;
#ifdef OSS_TESTING
    int (*test_transfer)(void *, uint8_t, uint8_t *, int, int *, unsigned);
    void *test_user;
#endif
};
#endif
