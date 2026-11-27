# AGENTS.md — ESP32 workspace quick context

**Scope:** everything under `/home/kzl/Projects/esp`.
**Purpose:** give future agents fast, accurate context before touching any project.
**Last verified:** 2026-11-27 (toolchain + repo state). Update this file when the facts below change.

## 1. TL;DR

- Collection of **independent ESP-IDF projects** for one board: **M5Stack Fire v2.6 (K007-V26)**.
  No top-level build and no shared component tree — each subdirectory is built/flashed on its own.
- Current toolchain: **ESP-IDF v5.5.6** at `/home/kzl/Projects/esp-idf` (sibling of this workspace).
  Fresh shells have no `idf.py` / `IDF_PATH`; always run
  `source /home/kzl/Projects/esp-idf/export.sh` first.
- Every project targets `esp32`; build with `idf.py build`, flash with
  `idf.py -p /dev/ttyACM0 flash monitor` (verify the port first).
- Five projects: `blink/`, `gps/`, `hello_world/`, `hello_world_vs/`, `lcd/` — see §5.
- Git is intentionally split: root repo tracks only helper files + `blink/`; `gps/` and `lcd/` are
  **nested independent repos**; `hello_world*/` are untracked copies. See §6.

## 2. Hardware: M5Stack Fire v2.6

- MCU: ESP32-D0WDQ6-V3, dual-core Xtensa LX6 @ 240 MHz, 520 KB SRAM, Wi-Fi + dual-mode BT.
- Flash: **16 MB**; PSRAM: **8 MB quad** (`CONFIG_SPIRAM` on in `gps/` and `lcd/`, off in the rest).
- Display: 2.0" **320x240** IPS, **ILI9342C** controller, SPI.
- LEDs: **10x SK6812** RGB LED bar on **GPIO15** (RMT, GRB order).
- IMU: MPU6886 6-axis (I2C).
- USB-serial bridge varies by board revision; on this host it enumerates as `/dev/ttyACM0`
  (user `kzl` is in `dialout`). Verify with `ls /dev/ttyACM* /dev/ttyUSB*` before flashing.
- Board docs: https://docs.m5stack.com/en/core/Fire_v2.6 — further datasheet links are in the root
  `README.md`.

Pins already claimed by the projects (check before reusing):

| Function                          | Pin              |
| --------------------------------- | ---------------- |
| LCD SCLK / MOSI / MISO            | 18 / 23 / 19     |
| LCD CS / DC / RST / backlight     | 14 / 27 / 33 / 32 |
| LED bar data                      | 15               |
| GPS UART2 RX (external module)    | 13 (TX left at driver default) |

## 3. Toolchain & environment

- ESP-IDF **v5.5.6** checkout: `/home/kzl/Projects/esp-idf` (external sibling; not part of this workspace).
- Python env: `~/.espressif/python_env/idf5.5_py3.12_env` (Python 3.12.3); tools under `~/.espressif`.
- Compiler: `xtensa-esp-elf-gcc 14.2.0`; CMake 3.28.3 and Ninja 1.11.1 on `PATH`.
- Build system: `idf.py` (CMake + Ninja) with ESP-IDF Component Manager for registry dependencies.
- `~/.espressif/idf-env.json` registers the 5.5 install (target `esp32`).
- No test/CI wiring: the only test files are the stock `hello_world*/pytest_hello_world.py`
  (pytest-embedded, never configured here).

## 4. Common commands

```bash
source /home/kzl/Projects/esp-idf/export.sh   # required in every new shell

cd blink/                                     # or gps/, hello_world/, hello_world_vs/, lcd/
idf.py build                                  # configure + build
idf.py -p /dev/ttyACM0 flash monitor          # flash and open serial monitor (Ctrl+] exits)
idf.py -p /dev/ttyACM0 erase-flash            # full chip erase (destructive to device)
idf.py menuconfig                             # Kconfig UI (board, flash, PSRAM, project options)
idf.py fullclean                              # wipe build/ when CMake/IDF state is confused
idf.py add-dependency "espressif/led_strip^3.0.3"   # add an IDF Component Manager dep
```

- First build from clean/fullclean downloads components (`managed_components/`) from
  components.espressif.com — needs network.
- Format with the project's own config, e.g. `clang-format -i main/*.c` (see §7).

## 5. Projects

### `blink/`
- Entry `main/blink.c`: `app_main` configures the RMT LED strip and loops red → green → blue sweeps
  over all 10 LEDs.
- Config in `main/Kconfig.projbuild`: `CONFIG_BLINK_GPIO=15`, `CONFIG_BLINK_PERIOD=1000`, RMT mode;
  16 MB flash.
- Dependency: `espressif/led_strip ^3.0.3` (`main/idf_component.yml`; already vendored in
  `managed_components/espressif__led_strip`).
- The only project tracked in the root git repo.

### `gps/`
- Entry `main/main.c`: NMEA parser on `UART_NUM_2`, 9600 8N1, RX `GPIO13`; the event handler logs
  date/time, latitude, longitude, altitude, speed. `TIME_ZONE` is hardcoded to `-5`.
- `main/nmea_parser.{c,h}` (~785 + 191 lines) copied from the ESP-IDF NMEA example;
  `main/Kconfig.projbuild` exposes RX pin, ring-buffer/task settings, and GGA/GSA/GSV/RMC/GLL/VTG
  parse toggles. No third-party registry dependencies.
- Own git repo (2 commits, **no remote**). Uncommitted local changes: `.gitignore`, removal of
  commented-out UART demo code from the end of `main.c`, and a regenerated `sdkconfig`.
- `sdkconfig` records IDF **5.2.0**; the committed version is older still — expect a one-time
  sdkconfig migration/diff when built with IDF 5.5.6.

### `hello_world/`
- Stock ESP-IDF hello_world: prints chip info, flash size, heap, counts down 10 s, restarts.
- `CMakeLists.txt` sets `CMAKE_EXPORT_COMPILE_COMMANDS ON`; includes the stock pytest/README files.
- Flash size left at the IDF default 2 MB (this board has 16 MB) — fine for the tiny app, but change
  it if a larger partition table is ever needed.

### `hello_world_vs/`
- Near-identical copy of `hello_world/` for VS Code/clangd-oriented editing. Only differences:
  no `CMAKE_EXPORT_COMPILE_COMMANDS`, `.clangd` strips `-f*`/`-m*` flags, and a broader `.gitignore`
  (also ignores `sdkconfig`, `.espressif/`, etc.).
- Untracked in any repo; treat as a scratch/experimental copy.

### `lcd/`
- Entry `main/main.c` (226 lines): SPI2 at 20 MHz drives the ILI9342C via `esp_lcd`
  (16 bpp, BGR, inverted color); LVGL 8.3.7 display driver with two 320x20 draw buffers and a
  2 ms `esp_timer` tick. `main/lvgl_demo_ui.c` adds a meter widget + rotate button; the main loop
  also fills color bands as a self-test.
- Local component `components/lcd_ili9342c/` provides the ILI9342C init sequence and
  `esp_lcd_new_panel_ili9342c()`.
- `main/idf_component.yml` requires `lvgl/lvgl ^8.3.7`; `managed_components/` is currently absent,
  so the first build must download it. `sdkconfig.defaults` sets `LV_USE_USER_DATA=y`,
  `LV_COLOR_16_SWAP=y`, `LV_COLOR_DEPTH_16=y`; `CONFIG_SPIRAM=y`, 16 MB flash.
- Own git repo (1 commit, **no remote**); uncommitted change: `.gitignore` (adds
  `compile_commands.json`). An old `dependencies.lock` pins lvgl 8.3.7.

## 6. Git state, layout & gotchas

**Root repo** `github.com/lz3450/esp`, branch `main`, author `KZL <lz3450@outlook.com>`, 2 commits
("helper files", "blink", both 2026-03-12). Tracks exactly 10 files: `.clang-format`, `.clangd`,
`.cspell.json`, `.gitignore` + `blink/` (CMake, Kconfig, `blink.c`, `idf_component.yml`,
`sdkconfig`).

- Uncommitted at root: `README.md` (untracked), `gps/`, `hello_world/`, `hello_world_vs/`, `lcd/`
  (untracked directories), and a modified `.cspell.json`.
- `gps/` and `lcd/` are **nested independent git repos** (their own `.git`, no remotes). A
  root-level `git add -A` will not capture their contents — commit inside each repo separately.
- `hello_world/`, `hello_world_vs/`, and root `README.md` have no git history at all.
- Ignored per project: `build/`, `managed_components/`, `dependencies.lock`, `sdkconfig.old`,
  `compile_commands.json`, `.cache/`, `.vscode/`. `sdkconfig` **is** committed in `blink/`, `gps/`,
  `lcd/` — that is intentional (encodes the working board config); preserve it.

Known stale/quirky artifacts:

- `gps/compile_commands.json` points at a **nonexistent** `/home/kzl/Projects/esp/esp-idf` path and
  an old toolchain (`esp-12.2.0`, IDF 5.2 environment). Ignore it or regenerate via a build.
- `.vscode/` in `gps/` and `lcd/` is from the old layout (`idf.espIdfPath =
  /home/kzl/Projects/esp/esp-idf`, `idf5.2_py3.11_env`); fix paths before trusting VS Code tasks.
- `build/` directories exist only for `blink/` (126 MB), `hello_world/` (62 MB),
  `hello_world_vs/` (62 MB); all are dated 2026-03-12 and were generated with IDF 5.5.3, not the
  current 5.5.6 checkout. If CMake complains about IDF path/version, run `idf.py fullclean`.
- Root `.cspell.json` was edited to register a custom dictionary `.esp.txt`, **which does not
  exist**. Spelling checks will error until that file is added or the edit reverted.
- Root `.clangd` sets `Compiler: gcc` (not the Xtensa toolchain); workable but the explicit
  `hello_world_vs/.clangd` is the better clangd example.
- sdkconfig generations differ: `blink/`, `hello_world/`, `hello_world_vs/` say 5.5.3,
  `gps/` says 5.2.0, `lcd/` has no version header (older). Building everything with the current
  5.5.6 will migrate them and produce sdkconfig diffs — expected, not a bug.

## 7. Conventions

- C only (no C++ here), ESP-IDF style: `app_main`, `static const char *TAG` + `ESP_LOGI/W/E`,
  `ESP_ERROR_CHECK()` on init calls, static file-local helpers.
- Formatting is per project — **always use the local `.clang-format`**:
  - root + `lcd/`: Allman braces, 4 spaces, column limit 0, `SortIncludes: Never`, aligned
    declarations/assignments/macros.
  - `gps/`: LLVM-based, Linux brace placement, 4 spaces, column limit 0.
- `.cspell.json` holds project-specific words for the spell checker; `.clangd` files are editor config.
- Comments and docs are in English; infer/keep the existing style when editing.
- Keep projects self-contained: no shared root components, no cross-project includes. New project
  pattern: copy the `hello_world` scaffold, `idf.py set-target esp32`, then enable 16 MB flash and
  PSRAM in `menuconfig` to match the board.
- `sdkconfig` changes should go through `idf.py menuconfig` and be committed with the change.

## 8. Ground rules for agents

- Source `export.sh` in every non-interactive shell before invoking `idf.py`.
- Build the project you touched (`idf.py build`) before claiming the change works; hardware behavior
  can only be confirmed on the physical board.
- Flashing/erasing is a side effect on a real device — do it only when the task calls for it.
- Commit in the correct repo (root vs `gps/` vs `lcd/`); never commit `build/`,
  `managed_components/`, `dependencies.lock`, or other ignored artifacts.
- Don't "fix" intentionally committed `sdkconfig` values (16 MB flash, 240 MHz, PSRAM) without a
  reason tied to the board.
- Format edited C files with the project's `.clang-format` before finishing.

## 9. Keeping this file current

- Update **Last verified** and the affected section when you: add/remove a project, change pins,
  dependencies, target or build state; upgrade ESP-IDF; commit previously uncommitted changes.
- Keep it factual and terse (1–3 lines per point), prefer tables/command blocks, and point at files
  instead of pasting sdkconfig contents.
- Add new gotchas under §6 as they are discovered; remove ones that are fixed.
