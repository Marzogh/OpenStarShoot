# Native builds

The C library and diagnostic CLI use C11 and libusb 1.0. CMake 3.20 or newer builds static and shared libraries on Linux, Apple Silicon macOS and Windows x64. INDI remains an optional Linux Makefile target.

Linux has physical camera validation. On Apple Silicon macOS, native USB connection, one full-frame unbinned acquisition and FITS export have been verified. Binned and ROI acquisitions have not been hardware-tested on macOS. Windows has build and offline-test coverage; physical camera operation remains unverified. The existing camera limitations still apply.

## Linux

The existing build and installation commands remain supported:

```sh
sudo apt install build-essential libusb-1.0-0-dev python3
make
sudo make install
```

For CMake, install `cmake` and the same compiler/libusb dependencies. From the source directory:

```sh
cmake -S . -B ../openstarshoot-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build ../openstarshoot-build --parallel
cmake --install ../openstarshoot-build --prefix "$HOME/.local"
```

See the [README](../README.md) for Linux USB permissions and INDI installation.

## Apple Silicon macOS

Use an arm64 terminal with Xcode Command Line Tools and Homebrew:

```sh
brew install cmake libusb
cmake -S . -B ../openstarshoot-build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_TESTING=OFF
cmake --build ../openstarshoot-build --parallel
cmake --install ../openstarshoot-build --prefix "$HOME/.local"
```

The shared library is `libopenstarshoot.dylib`.

## Windows x64 with MSVC

Install Visual Studio 2022 with the C++ desktop workload, CMake 3.20 or newer, Python 3, and [vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started). In PowerShell, set `VCPKG_ROOT` to your vcpkg checkout:

```powershell
& "$env:VCPKG_ROOT/vcpkg.exe" install libusb:x64-windows
cmake -S . -B ../openstarshoot-build -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DBUILD_TESTING=OFF
cmake --build ../openstarshoot-build --config Release --parallel
cmake --install ../openstarshoot-build --config Release --prefix ../openstarshoot-install
```

The build produces `openstarshoot.dll`, its `openstarshoot.lib` import library, `openstarshoot_static.lib`, and `starshoot-diag.exe`. Keep the libusb runtime DLL on `PATH` or next to the executable. For an installed build, also put the installation's `bin` directory on `PATH`.

Define `OSS_SHARED` when compiling an application against the OpenStarShoot DLL. Do not define it for the static library. The public function signatures and structures are the same on all platforms. Static-library applications also link libusb.

Physical USB access requires a libusb-compatible Windows driver, such as WinUSB. The original vendor driver has not been demonstrated to work with this library. Driver installation or replacement is a separate operator action; the library does not install drivers, detach them, change the USB configuration or reset the camera. See [libusb's Windows documentation](https://github.com/libusb/libusb/wiki/Windows).

Run the FITS exporter with `python tools/fits_export.py` on Windows. The installed `starshoot-fits-export` is a Python script, not a Windows executable.

## Offline tests

No camera is needed. Use a Python environment with NumPy and Astropy for independent FITS verification. The exporter itself uses only the Python standard library.

```sh
python -m pip install numpy astropy
cmake -S . -B ../openstarshoot-tests -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build ../openstarshoot-tests --config Release --parallel
ctest --test-dir ../openstarshoot-tests -C Release --output-on-failure
```

On Windows, use the same toolchain and `-A x64` options as above, and pass your Python executable as `-DPython3_EXECUTABLE=C:/path/to/python.exe`. For C tests without FITS dependencies, add `-DOSS_TEST_FITS=OFF`. For a library/CLI build without tests or a Python dependency, add `-DBUILD_TESTING=OFF`.

The suite covers protocol bytes, acquisition state, full-frame binning, ROI reconstruction, synthetic CLI capture/cooling, USB-open guards, timing, exclusive binary output, static/shared linking and FITS output. Test assertions remain enabled in Release builds. CI runs these tests on Linux x64, macOS arm64 and Windows x64/MSVC, and compiles/runs consumers of the installed static and shared libraries. Linux additionally runs the existing Makefile and mock-INDI regression gate. Hardware acquisition is never part of CI.

The Linux regression command is:

```sh
make BUILD=../openstarshoot-make-tests all check check-fits check-roi indi check-indi
```

It needs the Linux INDI/CFITSIO development packages and NumPy/Astropy. Set `PYTHON=/path/to/python` when those packages are in a separate Python environment.
