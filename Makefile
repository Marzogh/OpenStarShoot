CC ?= cc
AR ?= ar
INSTALL ?= install
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
DATADIR ?= $(PREFIX)/share
INDI_DATA_DIR ?= $(DATADIR)/indi
LICENSEDIR ?= $(DATADIR)/licenses/openstarshoot
LIBUSB_CFLAGS ?= -I/usr/include/libusb-1.0
LIBUSB_LIBS ?= -lusb-1.0
CPPFLAGS += -Iinclude -Isrc $(LIBUSB_CFLAGS)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -fPIC
BUILD := build
.PHONY: all check check-fits check-roi install install-license clean
all: $(BUILD)/libopenstarshoot.a $(BUILD)/libopenstarshoot.so $(BUILD)/starshoot-diag
$(BUILD):
	@printf 'Creating build directory\n'
	mkdir -p $@
$(BUILD)/openstarshoot.o: src/openstarshoot.c src/internal.h include/openstarshoot.h | $(BUILD)
	@printf 'Compiling camera library\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/libopenstarshoot.a: $(BUILD)/openstarshoot.o
	@printf 'Building static library\n'
	$(AR) rcs $@ $<
$(BUILD)/libopenstarshoot.so: $(BUILD)/openstarshoot.o
	@printf 'Building shared library\n'
	$(CC) -shared -Wl,-soname,libopenstarshoot.so -o $@ $< $(LIBUSB_LIBS)
$(BUILD)/starshoot-diag: src/cli.c $(BUILD)/libopenstarshoot.a include/openstarshoot.h
	@printf 'Building diagnostic CLI\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libopenstarshoot.a $(LIBUSB_LIBS) -o $@
$(BUILD)/test-protocol: tests/test_protocol.c src/openstarshoot.c src/internal.h include/openstarshoot.h | $(BUILD)
	@printf 'Building offline protocol tests\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) -DOSS_TESTING tests/test_protocol.c src/openstarshoot.c $(LIBUSB_LIBS) -o $@
$(BUILD)/test-cli-errors: tests/test_cli_errors.c src/cli.c $(BUILD)/libopenstarshoot.a | $(BUILD)
	@printf 'Building CLI error-classification tests\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libopenstarshoot.a $(LIBUSB_LIBS) -o $@
$(BUILD)/test-binning: tests/test_binning.c src/openstarshoot.c src/internal.h include/openstarshoot.h | $(BUILD)
	@printf 'Building offline binning geometry/acquisition checks\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) -DOSS_TESTING tests/test_binning.c src/openstarshoot.c $(LIBUSB_LIBS) -o $@
$(BUILD)/test-capture-cli: tests/test_capture_cli.c src/cli.c src/openstarshoot.c src/internal.h include/openstarshoot.h | $(BUILD)
	@printf 'Building synthetic CLI capture workflow checks\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) -DOSS_TESTING tests/test_capture_cli.c src/openstarshoot.c $(LIBUSB_LIBS) -Wl,--wrap=oss_open -Wl,--wrap=oss_close -o $@
$(BUILD)/test-roi: tests/test_roi.c src/openstarshoot.c src/internal.h include/openstarshoot.h | $(BUILD)
	@printf 'Building offline ROI geometry/acquisition checks\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) -DOSS_TESTING tests/test_roi.c src/openstarshoot.c $(LIBUSB_LIBS) -o $@
$(BUILD)/test-cli-roi: tests/test_cli_roi.c src/cli.c src/openstarshoot.c src/internal.h include/openstarshoot.h | $(BUILD)
	@printf 'Building synthetic CLI ROI transport\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) -DOSS_TESTING tests/test_cli_roi.c src/openstarshoot.c $(LIBUSB_LIBS) -o $@
$(BUILD)/test-cooling: tests/test_cooling.c src/cli.c src/openstarshoot.c src/internal.h include/openstarshoot.h | $(BUILD)
	@printf 'Building offline cooler/fan API and CLI checks\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) -DOSS_TESTING tests/test_cooling.c src/openstarshoot.c $(LIBUSB_LIBS) -Wl,--wrap=oss_open -Wl,--wrap=oss_close -o $@
check: $(BUILD)/test-cooling $(BUILD)/test-protocol $(BUILD)/test-cli-errors $(BUILD)/test-binning $(BUILD)/test-capture-cli $(BUILD)/test-roi
	@printf 'Running offline checks; no hardware access\n'
	$(BUILD)/test-cooling
	$(BUILD)/test-protocol
	$(BUILD)/test-cli-errors
	$(BUILD)/test-binning
	$(BUILD)/test-capture-cli
	$(BUILD)/test-roi
check-fits:
	@printf 'Validating FITS crop and unsigned storage with Astropy; no hardware access\n'
	python3 tests/test_fits.py
	python3 tests/test_fits_roi.py
check-roi: $(BUILD)/test-cli-roi
	@printf 'Checking CLI ROI parsing and FITS with synthetic USB; no camera access\n'
	python3 tests/test_fits_roi.py --cli $(BUILD)/test-cli-roi
install: all install-license
	@printf 'Installing native library, header, CLI and FITS exporter\n'
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(LIBDIR)" "$(DESTDIR)$(INCLUDEDIR)"
	$(INSTALL) -m 755 $(BUILD)/starshoot-diag "$(DESTDIR)$(BINDIR)/starshoot-diag"
	$(INSTALL) -m 755 tools/fits_export.py "$(DESTDIR)$(BINDIR)/starshoot-fits-export"
	$(INSTALL) -m 644 $(BUILD)/libopenstarshoot.a "$(DESTDIR)$(LIBDIR)/libopenstarshoot.a"
	$(INSTALL) -m 755 $(BUILD)/libopenstarshoot.so "$(DESTDIR)$(LIBDIR)/libopenstarshoot.so"
	$(INSTALL) -m 644 include/openstarshoot.h "$(DESTDIR)$(INCLUDEDIR)/openstarshoot.h"
install-license: LICENSE
	@printf 'Installing license and attribution notice\n'
	$(INSTALL) -d "$(DESTDIR)$(LICENSEDIR)"
	$(INSTALL) -m 644 LICENSE "$(DESTDIR)$(LICENSEDIR)/LICENSE"
clean:
	@printf 'Removing generated build files\n'
	rm -rf $(BUILD)

# Optional INDI adapter. Native library/CLI builds do not require INDI.
CXX ?= c++
INDI_CFLAGS ?= -I/usr/include/libindi
INDI_LIBS ?= -lindidriver -lcfitsio
INDI_CXXFLAGS ?= -O2 -g -std=c++17 -Wall -Wextra -Werror
.PHONY: indi check-indi install-indi
indi: $(BUILD)/indi_openstarshoot
$(BUILD)/indi_openstarshoot: indi/openstarshoot_ccd.cpp $(BUILD)/libopenstarshoot.a include/openstarshoot.h
	@printf 'Building optional INDI CCD adapter\n'
	$(CXX) $(CPPFLAGS) $(INDI_CFLAGS) $(INDI_CXXFLAGS) $< $(BUILD)/libopenstarshoot.a $(INDI_LIBS) $(LIBUSB_LIBS) -o $@
INDI_MOCK_FUNCTIONS := oss_open oss_close oss_error oss_get_phase oss_initialize oss_start_exposure oss_start_exposure_roi oss_get_status oss_stop oss_capture_raw
INDI_MOCK_RENAMES := $(foreach fn,$(INDI_MOCK_FUNCTIONS),-D$(fn)=__wrap_$(fn))
INDI_MOCK_WRAPS := $(foreach fn,$(INDI_MOCK_FUNCTIONS),-Wl,--wrap=$(fn))
$(BUILD)/mock-indi-camera.o: tests/mock_indi_camera.c include/openstarshoot.h | $(BUILD)
	@printf 'Building synthetic INDI-test camera backend; no USB implementation\n'
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INDI_MOCK_RENAMES) -c $< -o $@
$(BUILD)/indi_openstarshoot_mock: indi/openstarshoot_ccd.cpp $(BUILD)/mock-indi-camera.o $(BUILD)/libopenstarshoot.a include/openstarshoot.h
	@printf 'Building the same INDI adapter with the test-only camera backend\n'
	$(CXX) $(CPPFLAGS) $(INDI_CFLAGS) $(INDI_CXXFLAGS) $< $(BUILD)/mock-indi-camera.o $(BUILD)/libopenstarshoot.a $(INDI_MOCK_WRAPS) $(INDI_LIBS) $(LIBUSB_LIBS) -o $@
check-indi: $(BUILD)/indi_openstarshoot_mock
	@printf 'Running offline INDI XML/FITS integration; no camera access\n'
	python3 tests/test_indi.py $(BUILD)/indi_openstarshoot_mock
install-indi: $(BUILD)/indi_openstarshoot install-license
	@printf 'Installing optional INDI driver and discovery XML\n'
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(INDI_DATA_DIR)"
	$(INSTALL) -m 755 $(BUILD)/indi_openstarshoot "$(DESTDIR)$(BINDIR)/indi_openstarshoot"
	$(INSTALL) -m 644 indi/indi_openstarshoot.xml "$(DESTDIR)$(INDI_DATA_DIR)/indi_openstarshoot.xml"
