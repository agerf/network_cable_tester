# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Two-node RJ45 cable tester with ESP-IDF 6.1.x (C++):

- **Node A / Master (ESP32-S3)**: WLAN access point + embedded web UI + test
  orchestration. Runs in APSTA mode: the AP serves the UI, the STA interface
  carries ESP-NOW.
- **Node B / Slave (ESP32-S3)**: ESP-NOW responder on the fixed channel; only
  samples the eight RJ45 lines (never drives them). Both nodes currently use
  ESP32-S3 boards; the classic-ESP32 pin map remains as a fallback.
- Role is chosen at compile time via `CONFIG_CABLE_TESTER_ROLE_MASTER` /
  `CONFIG_CABLE_TESTER_ROLE_SLAVE` (see `main/Kconfig.projbuild`); the target is
  set per role in `sdkconfig.master.defaults` (`esp32s3`) and
  `sdkconfig.slave.defaults` (`esp32s3`). The two boards get different firmware
  images; after protocol changes, rebuild and flash both.
- Inter-node transport is ESP-NOW (WiFi radio, no IP/router) on channel 1. The
  master's AP is also channel 1. The slave broadcasts a HELLO every 3 s for
  automatic re-pairing.
- Measurement: the master drives one line HIGH at a time; both sides read all
  eight lines; the slave returns its bitmask; `cable_test::analyze` derives the
  mapping, shorts, opens, and cable kind. Length is an optional experimental
  capacitance measurement.

## Commands

ESP-IDF is installed on this machine at `~/.espressif/v6.1.1/esp-idf`:

```sh
source /home/agerf/.espressif/tools/activate_idf_v6.1.1.sh
```

Build / flash / monitor a role (separate build dirs and sdkconfigs):

```sh
scripts/idf_role.sh master build
scripts/idf_role.sh slave build
scripts/idf_role.sh master flash -p /dev/ttyACM0
scripts/idf_role.sh slave  flash -p /dev/ttyACM1
scripts/idf_role.sh master monitor -p /dev/ttyACM0
```

`scripts/idf_role.sh <role> fullclean` works as well. If an existing build
directory has the wrong chip target, run
`scripts/idf_role.sh <role> set-target <esp32s3|esp32>` once, then rebuild.
Generated files (`build-master/`, `build-slave/`, `sdkconfig.master`,
`sdkconfig.slave`) are git-ignored; the `*.defaults` files are tracked.

Host tests for the pure analysis core (no hardware/IDF involved):

```sh
cmake -S host_tests -B host_tests/build
cmake --build host_tests/build
ctest --test-dir host_tests/build --output-on-failure
```

All cases run in `host_tests/build/cable_test_tests`; there is no per-case
filter. There is no linter configured; both IDF builds and the host build must be
warning-free.

### VS Code

- VS Code 1.147.0 is installed at `~/develop/vscode/VSCode-linux-x64/bin/code`
  (not on `PATH`). The `ms-vscode.cpptools` and `espressif.esp-idf-extension`
  extensions are installed.
- `.vscode/settings.json` selects the master `compile_commands.json`, configures
  the Espressif extension for the master build (`idf.buildPath=build-master`,
  `idf.sdkconfigFilePath=<repo>/sdkconfig.master`), and provides an `ESP-IDF`
  terminal profile that sources the activation script.
- `.vscode/c_cpp_properties.json` has IntelliSense configurations for
  `ESP32 Master`, `ESP32 Slave`, and `Host Tests`; each points at the
  corresponding `compile_commands.json`. `host_tests/CMakeLists.txt` exports
  compile commands.
- `.vscode/tasks.json` is authoritative for per-role build/flash/monitor; the
  default build task builds both roles. Flash tasks prompt for the serial port.
  The Espressif extension itself is preset to the master build only.
- JTAG debugging is not configured; add a launch configuration with the
  extension if a probe is available.

## Architecture

- `components/cable_test/` — pure C++17-compatible core, no ESP-IDF dependency:
  `model.hpp` (`TestData`, `Analysis`), `analyzer.cpp` (classification), and
  `version.hpp`. Keep it host-testable; add analyzer tests for any logic change.
- `components/rj45_port/` — GPIO hardware layer (`Rj45Port`: drive/sample/
  length) and `pin_map.hpp` with default pin arrays for Node A and B. On
  ESP32-S3 both nodes use the same RJ45 wiring, GPIO 4–11 (`kNodeBPins` aliases
  `kNodeAPins`), so identical boards/harnesses can be used; the role is
  firmware-only. Conservative fallback for classic ESP32: 16,17,18,19,21,22,23,
  25. The master additionally reserves GPIO 12–18 and 21/47 for an SPI ILI9488
  display (`namespace display`: SCLK 12, MOSI 13, MISO 14, CS 15, DC 16, RST 17,
  backlight 18, optional touch CS 21/IRQ 47); a `static_assert` rejects overlap
  with `kNodeAPins`. `LengthConfig` holds the calibration constants for the
  experimental length measurement.
- `components/cable_link/` — `protocol.hpp` (8-byte packed `Frame`, `MsgType`,
  channel, settle and timeout constants), `master_link` (pairing, request/reply
  with seq matching, 3 retries × 300 ms), `slave_link` (command worker, periodic
  HELLO). ESP-NOW RX callbacks only enqueue; every send runs in a worker task.
- `components/test_runner/` — orchestrates idle sampling, the eight driven
  rounds, optional length measurement, and cleanup. A `try_lock` mutex reports
  `ESP_ERR_INVALID_STATE` when busy; unpaired link is also `ESP_ERR_INVALID_STATE`.
- `components/report/` — hand-built JSON and German console summary. No cJSON
  (not bundled in IDF 6).
- `components/web_ui/` — `esp_http_server`, serves `www/index.html` (embedded via
  `EMBED_TXTFILES`) plus `/api/status` and `/api/test`. UI edits require a
  rebuild (the file is compiled into the firmware).
- `components/display_ui/` — master-only ILI9488 SPI driver (SPI2, 20 MHz,
  18-bit RGB666, portrait 320×480) and a graphical LVGL 9.x UI styled after
  the web UI. LVGL and the XPT2046 share SPI2; touch coordinates come from
  XPT2046 X/Y readings (2 MHz) with `T_IRQ` as a fast hint. First boot runs a
  four-point calibration stored in NVS namespace `touch_cal` (`touchcal`
  console command resets it). The display task polls
  `TestRunner::last_result()`, shows header/hero/result cards (verdict,
  wiring, matrix, diagnosis, lengths, raw), and runs a test synchronously from
  the "Kabel testen" button. LVGL is fetched via the component manager
  (`components/display_ui/idf_component.yml`); `managed_components/` is
  git-ignored, `dependencies.lock` is tracked. Sources are guarded by
  `CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3`; the
  component is registered unconditionally because component CMake runs before
  sdkconfig is loaded. LVGL uses CLIB malloc, RGB565 and Montserrat 14/20/28;
  UI strings are ASCII because the built-in fonts lack German umlauts.
- `main/` — role-specific app entry (only `app_master.cpp` or `app_slave.cpp` is
  selected in `main/CMakeLists.txt`), WiFi APSTA/STA setup and runtime AP
  reconfiguration (`wifi.cpp`), UART REPL (`console_ui.cpp`), AP credential
  defaults (`app_config.hpp`) and NVS-backed credential storage
  (`wifi_config.cpp`).

## Key files to edit

- Pin mapping: `components/rj45_port/include/rj45_port/pin_map.hpp`
  (`kNodeAPins` / `kNodeBPins`, target-conditional). Both S3 nodes use the same
  S3-safe RJ45 pins, GPIO 4–11, avoiding 0/3/45/46 (strapping), 19/20 (USB),
  26–37 (flash/PSRAM), 39–42 (JTAG), 43/44 (UART0). The classic-ESP32 fallback
  avoids 0/2/5/12/15, 6–11, 34–39, 1/3. Both boards can be wired identically;
  only the firmware role differs.
- Display reservation (master only): an SPI ILI9488 may be wired to
  `board::display::kSpiSclk/Mosi/Miso`, `kCs`, `kDc`, `kRst`, `kBacklight`
  (GPIO 12–18), optional XPT2046 touch on `kTouchCs`/`kTouchIrq` (21/47).
  SPI runs through the GPIO matrix, so no RJ45 pins need to move. Keep these
  constants out of `kNodeAPins`; the header enforces it at compile time.
  `docs/schematic-variante-b.svg` documents the complete wiring: shared SPI2
  SCK/MOSI/MISO, separate display/touch CS, 3.3 V supply with decoupling, and
  a high-side P-MOSFET (AO3401) backlight switch driven by GPIO 18
  (LOW = on, inverted PWM duty). An optional series R_DIM (1–4.7 Ω, 0.5 W)
  provides fixed dimming; omit Q1/R_BL for that variant, leaving GPIO 18
  free. The ESP32-S3 is not 5 V tolerant: 5 V VCC on the display is only
  safe with onboard regulation/level shifting and 3.3 V logic; never expose
  SDO/T_DO/T_IRQ to 5 V. Firmware support is implemented in
  `components/display_ui/`; `TestRunner::last_result()` caches the last
  successful result so the display also shows tests started by Web-UI or
  console.
- AP SSID/password defaults: `main/app_config.hpp`. The active credentials are
  persisted in NVS (`main/wifi_config.cpp`, namespace `wifi_cfg`) and can be
  changed at runtime via the serial `wifi` command (`wifi ssid …`,
  `wifi pass …`, `wifi reset`). `wifi` / `wifi status` also prints the AP IPv4
  address, netmask/gateway, channel, AP/STA MACs, and connected clients with
  their DHCP/ARP-assigned IPv4 addresses. `init_wifi_config()` runs before
  `start_wifi_master()`; changes apply immediately through
  `apply_ap_credentials()` (the AP restarts, web clients must reconnect).
  The REPL also registers `reboot` for both roles (`esp_restart()`).
- Channel, settle time, RPC timeouts, frame types:
  `components/cable_link/include/cable_link/protocol.hpp`.
- Length calibration: `LengthConfig` in
  `components/rj45_port/include/rj45_port/rj45_port.hpp`.
- Web UI: `components/web_ui/www/index.html` (embedded; rebuild after edits).
- Display UI: `components/display_ui/src/display_ui.cpp` (LVGL layout,
  touch calibration, NVS `touch_cal`) and `src/ili9488.cpp` (portrait
  `kMadctl`, SPI clock, init sequence, XPT2046 reads, backlight). LVGL
  settings: `sdkconfig.master.defaults`; dependency:
  `components/display_ui/idf_component.yml`.
- Role defaults: `sdkconfig.master.defaults`, `sdkconfig.slave.defaults`.

## Constraints and gotchas

- **LVGL component manager**: the first master build fetches LVGL 9.x from the
  ESP-IDF Component Registry (network required). `managed_components/` is
  git-ignored; `dependencies.lock` is tracked. New LVGL Kconfig defaults do
  not back-propagate into an existing `sdkconfig.master`; delete it or use
  `menuconfig`, then rebuild.
- **Display UI strings** are ASCII because the built-in Montserrat fonts lack
  German umlauts. Use e.g. "Laengen"/"Kurzschluesse" or add a Latin-1 font.
- **Touch calibration** is stored in NVS namespace `touch_cal`; the console
  command `touchcal` plus `reboot` reruns the four-point calibration screen.
  `T_IRQ` is only a fast hint; X/Y are read from the XPT2046 over SPI2.
- **Common ground**: both boards must share a ground (e.g., both USB cables on
  the same host/hub). Without it, the slave reads all lines low and the report
  shows all-open. This cannot be fixed in firmware; UI and README contain hints.
- **T568A/B cannot be distinguished electrically** (identical pair-to-pin
  assignment), and a simple continuity tester cannot detect split pairs. The
  classification is straight / crossover (Gigabit or 10-100) / custom / faulty.
  Do not promise A/B or pair-grouping detection.
- A pure conductor swap is a *custom* mapping, not a short. The analyzer logic
  and `host_tests/test_main.cpp` guard this distinction.
- Length measurement is capacitance-based, only runs on electrically clean
  wiring (no shorts/opens/idle activity), and is deliberately labeled
  experimental (±20-30 %).
- ESP-IDF 6.1 specifics: the old `driver` component is split — use
  `esp_driver_gpio`; cJSON is no longer bundled; build with IDF 6.1.x.
- Target/role pairing: **both roles are built for `esp32s3`** and use the same
  RJ45 pins (GPIO 4–11). A build compiled for the wrong target silently selects
  the classic pin-map branch, so verify `CONFIG_IDF_TARGET` in the generated
  sdkconfig when changing hardware. `build-slave/` was previously configured
  for `esp32`; after this change run
  `scripts/idf_role.sh slave set-target esp32s3` once.
- The S3 console defaults to UART0; the REPL also supports USB Serial/JTAG when
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`. On this setup both boards expose their
  UART bridge as CH343 CDC-ACM (`1a86:55d3`), typically `/dev/ttyACM*`; the
  native USB-Serial/JTAG would be Espressif `303a:1001`. Use `by-path` for a
  stable name.
- C++ exceptions/RTTI are disabled. Use `std::optional`/error codes, don't throw.
- Master AP and ESP-NOW must use the same `protocol::kChannel` (1). Changing it
  requires rebuilding/flashing both roles.
- The web UI is only protected by WPA2 (default `cabletest123`); there is no
  separate authentication. SSID/password can be changed over the serial console
  and are persisted in NVS; `wifi reset` restores the defaults from
  `app_config.hpp`.
- Hardware-in-the-loop is manual: there is no automated hardware test. Verify
  with a known-good cable after flashing both roles.
- If the serial monitor reports `-- WARNING: Writing to serial is timing out`
  (host-side 300 ms write timeout), or "device reports readiness to read but
  returned no data", check the USB bridge first: the tty can be wedged even
  though the ESP32 keeps running (web UI/ESP-NOW unaffected). Reproduce with
  `lsusb -v -d 1a86:55d3` (hangs) or a failed `TIOCMSET`; fix by replugging the
  USB cable, using another port/cable, or a powered hub. The kernel log
  (`journalctl -k -f`) shows `error -71` / `USB disconnect` on unstable links.
  This is a USB cable/port/power issue, not firmware. The `phy_init: failed to
  load RF calibration data` warning is normal on the first boot. Port names can
  change on re-enumeration (`ttyACM0` ↔ `ttyACM1`); use
  `/dev/serial/by-path/...` for a stable path and stop any stale monitor before
  reflashing.
