# Release notes

## Initial release candidate

- Native Linux libusb library and diagnostic CLI for the Orion StarShoot Deep
  Space Monochrome Imager III (#52076).
- Full-frame and region-of-interest acquisition at 1×1 and 2×2 binning, with
  preserved raw data and unsigned 16-bit FITS export.
- Optional INDI CCD adapter with standard exposure, frame and binning controls.
- Native paired cooler on/off and fan high/low controls, tested offline.
- Source installation targets for the library, header, command-line tools and
  optional INDI adapter.
- MIT-licensed independent source with synthetic offline regression tests.

Known limitations include intermittent nine-pixel zero runs, unverified recovery
after interrupted or failed readout, and uncalibrated optical orientation and
precision timing. Native cooling command exchange has not been exercised on the
camera; temperature telemetry/regulation and INDI cooling controls are absent.
Combined bin2 ROI passed native physical captures and INDI mock tests, without
a separate physical Ekos validation. Other camera models are unsupported.
