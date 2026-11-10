# Native C library

`libopenstarshoot` is a synchronous C11 camera-control library built on libusb. It works without INDI. The public header is `include/openstarshoot.h` in the source tree and is installed as `openstarshoot.h` under the chosen prefix's `include/` directory.

One thread should own a camera handle. The API supports identification, initialization, status and counter queries, exposure, readout, and normal stop. Initialization does not silently reset offset or cooling settings.

## Imaging

Set the requested binning through `oss_exposure.bin`, using **1** or **2**. Full-frame acquisition uses `oss_get_image_geometry` and `oss_reconstruct_active`. Both work with the raw data produced by the acquisition.

For subframes, use an `oss_roi` with `oss_start_exposure_roi`, obtain the expected layout with `oss_get_roi_geometry_bin`, and reconstruct with `oss_reconstruct_roi_bin`. The output buffer contains host-endian `uint16_t` pixels. The existing `oss_get_roi_geometry` and `oss_reconstruct_roi` functions remain bin-1 compatibility wrappers.

ROI coordinates describe the unbinned active-sensor area, with bounds 1392 x 1040. In bin 2, `x`, `y`, `width` and `height` must be even, and output dimensions are half the requested dimensions. Invalid regions are rejected before acquisition commands are sent. Raw and reconstructed buffers must not overlap.

These functions retain the recorded raw byte stream; image reconstruction does not overwrite it. The exact raw layouts and the two binning modes are described in [Technical notes](technical-notes.md).

## Cooling and fan command

The native API exposes paired cooler/fan settings, subject to the camera being initialized and idle:

```c
oss_set_cooling(camera, OSS_COOLER_ON, OSS_FAN_HIGH, reply);
```

The older `oss_set_cooling_flags` interface uses masks `0` (off/low), `1` (on/low), `2` (off/high) and `3` (on/high). The CLI command is:

```sh
starshoot-diag cooling-set NEW_OUTPUT_DIRECTORY on|off high|low
```

The implementation validates command encoding, framed replies and failure handling in offline tests. The physical camera command has not yet been exercised, so these calls should not be interpreted as confirmation of fan speed or cooling performance. No temperature measurement, cooling setpoint, regulation or separate fan-off mode is implemented. Changes are not automatically restored; request both desired values explicitly.

## Building and linking

A normal native build generates `build/libopenstarshoot.a`, `build/libopenstarshoot.so` and `build/starshoot-diag`:

```sh
make
```

`BUILD=PATH` places generated files in another directory. `PREFIX`, `DESTDIR` and directory overrides support packaging or staged installations. For nonstandard libusb installations, use `LIBUSB_CFLAGS` and `LIBUSB_LIBS`. The library's public API is in the header and should be consulted for the complete signatures, types and return codes.

Installed CLI and INDI executables link the native library statically and use the system libusb runtime. A program linking the shared library needs the library directory to be discoverable by the dynamic loader; `ldconfig` handles normal system installation paths.

For a staged installation without writing to system directories, run `make DESTDIR="$PWD/stage" PREFIX=/usr install install-indi` after building the optional adapter. The Makefile also supports `INDI_CFLAGS`, `INDI_LIBS` and `INDI_CXXFLAGS` overrides for nonstandard INDI installations.
