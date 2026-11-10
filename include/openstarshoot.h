#ifndef OPENSTARSHOOT_H
#define OPENSTARSHOOT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Synchronous, per-camera API. Serialize calls from one owning thread.
 * No resets, driver detach, firmware or factory commands are implemented. */
typedef struct oss_camera oss_camera;
enum { OSS_OK=0, OSS_EINVAL=-100, OSS_EPROTO=-101, OSS_ESTATE=-102,
       OSS_EREJECTED=-103, OSS_ELIMIT=-104, OSS_EAMBIGUOUS=-105,
       OSS_ECANCELLED=-106, OSS_EIO=-107, OSS_EFRAME=-108 };
typedef struct { uint8_t bytes[8]; } oss_reply;
typedef struct { uint8_t state, auxiliary; } oss_status;
typedef enum { OSS_IDLE, OSS_EXPOSING, OSS_READY, OSS_READING, OSS_UNCERTAIN } oss_phase;
/* Transfer callback sees application USB buffers, not USB bus packets.
 * OUT data contains the attempted request; actual is what libusb completed.
 * IN data contains actual returned bytes, including partial timeout data. */
typedef void (*oss_trace_fn)(void *user, uint8_t endpoint, const uint8_t *data,
                            size_t requested, size_t actual, int result);
typedef int (*oss_frame_fn)(void *user, const uint8_t *data, size_t bytes);
typedef int (*oss_cancel_fn)(void *user);
typedef struct {
    uint8_t bus, address; /* both zero: select exactly one matching device */
    oss_trace_fn trace;
    void *trace_user;
} oss_open_options;
typedef struct {
    unsigned exposure_ms; /* 1..60000, provisional timing units */
    unsigned bin;         /* 1 or 2 (2x2) */
    unsigned readout_speed; /* original profile-5 setting 1..8; default 5 */
} oss_exposure;
typedef struct {
    size_t bytes;
    size_t expected_bytes, wire_bytes;
    size_t read_ahead_bytes;
    uint8_t read_ahead[1024]; /* final transfer remainder, not image pixels */
    unsigned transfers;
    int end_short_packet;
    int result; /* partial bytes remain valid when result != 0 */
} oss_capture_result;

int oss_open(oss_camera **out, const oss_open_options *options);
/* Close releases the interface. It never silently sends a camera command. */
void oss_close(oss_camera *camera);
const char *oss_error(int code);
oss_phase oss_get_phase(const oss_camera *camera);
int oss_identify(oss_camera *, uint8_t *profile, oss_reply *);
/* Normal connection initialization queries 01/07/02, requires profile 5 and
 * a non-exposing state, and preserves unreadable offset/cooler/fan settings. */
int oss_initialize(oss_camera *, uint8_t *capability, oss_status *status);
int oss_get_status(oss_camera *, oss_status *, oss_reply *);
int oss_get_capability(oss_camera *, uint8_t *, oss_reply *);
int oss_get_counter(oss_camera *, uint16_t *, oss_reply *);
/* ROI in zero-based unbinned active-sensor pixels. Bin 2 requires even
 * X, Y, width and height; output dimensions are width/2 and height/2. Horizontal selection is a
 * software crop; the device always transfers full-width rows. */
typedef struct { unsigned x, y, width, height; } oss_roi;
typedef struct {
    unsigned raw_width, raw_height, active_width, active_height;
    size_t raw_bytes;
} oss_roi_geometry;
int oss_get_roi_geometry(const oss_roi *, oss_roi_geometry *);
/* Decode host-endian active uint16 samples; input/output must not overlap. */
int oss_reconstruct_roi(const oss_roi *, const uint8_t *raw, size_t raw_bytes,
                        uint16_t *output, size_t output_samples);
/* Bin-aware variants retain the bin-1 functions as compatibility wrappers. */
int oss_get_roi_geometry_bin(unsigned bin, const oss_roi *, oss_roi_geometry *);
int oss_reconstruct_roi_bin(unsigned bin, const oss_roi *, const uint8_t *raw,
                            size_t raw_bytes, uint16_t *output, size_t output_samples);
int oss_start_exposure_roi(oss_camera *, const oss_exposure *, const oss_roi *, oss_reply *);
int oss_start_exposure(oss_camera *, const oss_exposure *, oss_reply *);
int oss_wait_ready(oss_camera *, unsigned deadline_ms, oss_cancel_fn, void *);
int oss_capture_raw(oss_camera *, oss_frame_fn, void *, unsigned deadline_ms,
                    oss_cancel_fn, void *, oss_capture_result *);
/* Command 05. No automatic protocol resynchronization or reset on failure. */
int oss_stop(oss_camera *, oss_reply *);
/* Explicit setters only. There is no setting readback or inferred restore.
 * Callers must explicitly choose both cooling settings and any restoration.
 * Offset checks require a known prior value for restoration. */
int oss_set_offset(oss_camera *, int offset, oss_reply *);
/* Command 0C always writes both controls. No readback or cached state.
 * An accepted reply confirms protocol framing, not physical operation. */
typedef enum { OSS_COOLER_OFF=0, OSS_COOLER_ON=1 } oss_cooler;
typedef enum { OSS_FAN_LOW=0, OSS_FAN_HIGH=1 } oss_fan;
int oss_set_cooling(oss_camera *, oss_cooler, oss_fan, oss_reply *);
/* Low-level compatibility API: bit 0 = cooler on, bit 1 = fan high. */
int oss_set_cooling_flags(oss_camera *, unsigned flags, oss_reply *);
/* Profile-5 bin-1 vertical window: active Y/height, raw height is height+12.
 * oss_start_exposure_roi configures this automatically; horizontal ROI is
 * applied only during reconstruction. Selected 512x512 windows are bench-tested; other windows remain experimental. */
int oss_set_vertical_window(oss_camera *, unsigned y, unsigned height, oss_reply *);

/* Pure protocol helpers for reproducible offline tests and integrations. */
/* Full-frame geometry. raw dimensions describe the unchanged USB samples,
 * not the compacted active image. Bin 2 framing is bench-tested; pixel
 * integrity and detector calibration are not qualified. */
typedef struct {
    unsigned raw_width, raw_height, active_width, active_height;
    size_t raw_bytes;
} oss_image_geometry;
int oss_get_image_geometry(unsigned bin, oss_image_geometry *);
/* Decode to host-endian uint16 active pixels without modifying raw input.
 * raw_bytes must be exact; output_samples is capacity in uint16 samples.
 * Input/output must not overlap. No calibration or orientation change.
 * Bin 2 selects alternate raw columns and clears bit 0 as required by
 * the supported reconstruction contract; it is not software 2x2 summing. */
int oss_reconstruct_active(unsigned bin, const uint8_t *raw, size_t raw_bytes,
                           uint16_t *output, size_t output_samples);
int oss_encode_exposure(const oss_exposure *, uint8_t request[6]);
int oss_decode_reply(uint8_t opcode, const uint8_t *, size_t, oss_reply *);
#ifdef __cplusplus
}
#endif
#endif
