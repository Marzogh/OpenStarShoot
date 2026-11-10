# Command-line capture and FITS export

The native diagnostic CLI is useful when you want to operate the Mono III without KStars or INDI, inspect a capture, or write software that uses the camera library directly.

Only one process may control the camera at a time. Disconnect it in Ekos before using `starshoot-diag`.

## Identification and acquisition

Every CLI command that writes results takes a **new output directory**. Existing output is not overwritten.

```sh
ROOT="${XDG_STATE_HOME:-$HOME/.local/state}/openstarshoot-cli"
mkdir -p "$ROOT"

starshoot-diag info "$ROOT/info-$(date -u +%Y%m%dT%H%M%SZ)-$$"

starshoot-diag capture "$ROOT/full-$(date -u +%Y%m%dT%H%M%SZ)-$$" \
  --illuminated --exposure-ms 100

starshoot-diag capture "$ROOT/bin2-$(date -u +%Y%m%dT%H%M%SZ)-$$" \
  --illuminated --exposure-ms 100 --bin 2

starshoot-diag capture "$ROOT/roi-$(date -u +%Y%m%dT%H%M%SZ)-$$" \
  --illuminated --exposure-ms 100 --roi 440 264 512 512

starshoot-diag capture "$ROOT/bin2-roi-$(date -u +%Y%m%dT%H%M%SZ)-$$" \
  --illuminated --exposure-ms 100 --bin 2 --roi 440 264 512 512
```

These are separate examples. Save the directory name for any capture you want to export later. For a true dark exposure, cover the camera and use `--covered --dark-ms 100` instead of `--illuminated --exposure-ms 100`. The resulting raw file is named `dark.raw` instead of `light.raw`.

ROI coordinates are measured in **unbinned active-sensor pixels**, with origin `(0,0)` and bounds **1392 x 1040**. In bin 2, the origin and dimensions must be even. For example, an ROI of `(440,264,512,512)` produces a 256 x 256 output image in bin 2.

## Exporting a capture

To convert a saved light exposure, pass the raw file and the acquisition directory containing its geometry and command record:

```sh
starshoot-fits-export "$OUT/light.raw" "$OUT/active.fits" \
  --exposure-ms 100 --image-type LIGHT --trace-dir "$OUT"
```

Here, `OUT` is the actual output directory from the preceding capture. For a dark image, use `dark.raw` and `--image-type DARK`. The exporter will not overwrite an existing FITS file.

The exporter verifies recorded geometry, commands and expected raw-byte length, and records a SHA-256 of the unchanged raw file. Output uses `BITPIX=16`, `BSCALE=1` and `BZERO=32768` so the FITS file represents unsigned 16-bit pixel counts. Binning and ROI sensor origins are recorded in the headers. `EXPTIME` is the encoded nominal exposure, and `DATE-OBS` is a host command timestamp, not a measured shutter time. No image orientation correction, WCS, dark subtraction, flat-field correction or interpolation is applied.

If there is no capture trace, specify the binning and ROI explicitly with the exporter's `--bin` and `--roi` arguments. Without those, the legacy no-trace path assumes a full-frame 1x1 image.

When working directly from the source tree, the exporter can be invoked with `python3 tools/fits_export.py` and requires only Python's standard library. The installed command is `starshoot-fits-export`.

## Saved files and recovery behaviour

An acquisition retains `frame.raw` or `light.raw` / `dark.raw` as applicable, application-level USB IN/OUT buffers, transfer indices, status and geometry data. These logs record the buffers passed to and returned from libusb, **not** an independent USB bus trace. Their exact filenames and content depend on the capture interface.

On ordinary completion, the camera is read for the expected byte count, receives a stop command and is checked for idle status. A failed read leaves available partial evidence; the implementation does not invent replacement pixels, automatically retry an exposure or reset the device.

Ekos/INDI saves its raw capture and diagnostic files under `${XDG_STATE_HOME:-$HOME/.local/state}/openstarshoot` by default. To use another directory, set `OPENSTARSHOOT_RAW_DIRECTORY` before starting the INDI server. Files are kept until you remove them yourself.
