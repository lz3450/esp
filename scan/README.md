# scan

Bus scanner for the **M5Stack Fire v2.6** (`ESP32-D0WDQ6-V3`, 16 MB flash,
8 MB PSRAM). Build it, flash it, and read the serial console to see which
devices are attached to the board's **I2C**, **SPI** and **UART** buses.

The scan runs once at boot (about 17-20 s, dominated by UART listening); reset the
board to scan again.

## What is scanned

| Bus | Default pins | How devices are detected |
| --- | --- | --- |
| I2C | SDA `21`, SCL `22` | Probes all 7-bit addresses `0x08..0x77`; reads WHO_AM_I / chip-ID registers for known parts |
| SPI | SCLK `18`, MOSI `23`, MISO `19`; CS candidates `4, 13, 26, 5, 2` | SD card `CMD0` response, SPI-flash `JEDEC ID` (`0x9F`), and a generic `0x00`/`0xFF` response probe |
| UART | RX candidates `13, 26, 36, 18, 19, 23` at `4800, 9600, 19200, 38400, 115200` baud | Listens on each RX pin and classifies captured data as GPS/NMEA (with checksum validation), printable text, or unidentified binary |

`scan` also reports the expected onboard devices so the output is easy to read:

- LCD: ILI9342C 320x240 on SPI2 (not a scannable "device" — same bus as TF card)
- IMU: MPU6886 at I2C `0x68`
- Magnetometer: BMM150 at I2C `0x10` — older FIRE revisions only; the connected v2.6 unit has none
- PMIC: IP5306 at I2C `0x75` (M5Stack power-management IC)
- TF card: CS `4` on the LCD/TF SPI bus (detected only if a card is inserted)
- LED bar GPIO15, speaker GPIO25, microphone GPIO34, buttons GPIO37/38/39

## Deliberately not scanned

- **UART0 (GPIO1/3)**: it is the console/log port.
- **PORT C (GPIO16/17)**: physically wired to the Fire's PSRAM (GPIO16 = PSRAM
  CS, GPIO17 = PSRAM clock). PSRAM is disabled in this project, but the pins
  are still left alone so the PSRAM chip can never be disturbed. Do not use
  PORT C unless you know the board variant does not populate PSRAM.
- **LCD pins** are only used as the SPI bus, with the LCD chip-select held high.

## Build, flash and monitor

```bash
source /home/kzl/Projects/esp-idf/export.sh   # ESP-IDF v5.5.6 checkout

cd scan
idf.py set-target esp32        # first time only
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Press `Ctrl+]` to leave the monitor. A plain serial terminal at 115200 baud
works as well (GPIO1/3, through the USB bridge).

## Configuration

`idf.py menuconfig` → **Device Scan Configuration**:

- Enable/disable the I2C, SPI and UART scans independently.
- Change bus pins (SDA/SCL, SCLK/MOSI/MISO, LCD CS hold, dummy UART TX).
- Probe one extra CS pin or one extra UART RX pin for custom wiring.
- Set the UART listen time per pin/baud (default 600 ms; lower it for a faster,
  less sensitive scan).

Defaults match the M5Stack Fire v2.6 pin map. If you wire a sensor to a
non-standard pin, set it in menuconfig — e.g. an I2C temperature sensor on a
custom bus, or a GPS module whose TX goes to a different M-Bus pin.

## Example output (verified on the connected board, 2026-11-27)

Verified on a Fire v2.6 with a GPS module feeding NMEA into GPIO13 and a TF card
inserted in the slot:

```
I (346) scan.i2c: --- I2C scan: SDA=21 SCL=22 @ 100000 Hz ---
I (366) scan.i2c:   found 0x68 : MPU6886 / MPU6050 / MPU9250 inertial measurement unit
I (366) scan.i2c:              WHO_AM_I 0x19 (MPU6886 confirmed)
I (376) scan.i2c:   found 0x75 : IP5306 power management IC (onboard)
I (376) scan.i2c: --- I2C scan done: 2 device(s) ---
I (376) scan.spi: --- SPI scan: SCLK=18 MOSI=23 MISO=19 @ 400000 Hz ---
I (386) scan.spi:   CS=4  : TF/SD card responded (R1=0x01, idle state)
I (396) scan.spi: --- SPI scan done: 1 device(s) ---
I (396) scan.uart: --- UART scan: listening on candidate RX pins at 4800..115200 baud ---
I (1966) scan.uart:   RX=13 : GPS/NMEA serial device @ 9600 baud (valid checksum)
I (1966) scan.uart:            $GNRMC,,V,,,,,,,,,,N*4D
...
I (16966) scan.uart: --- UART scan done ---
I (16966) scan:  scan complete in 17164 ms
```

The GPS was indoors without a fix (`RMC` status `V`), but the device and its
baud rate were identified. A scan with no UART device present takes about 20 s
(the full listen time); a detected NMEA device shortens it because the scanner
stops probing other baud rates once the stream is recognised.

## Notes

- `scan` is a diagnostic project: it only reads buses and never writes to
  devices, except the harmless SD `CMD0` / flash `0x9F` probes.
- PSRAM is intentionally left disabled in `sdkconfig.defaults`; enable it only
  if you also remove GPIO16/17 from the UART scan (it is not a scan pin anyway).
- The board is left idle after the scan. Flash another project (for example
  `blink`) to give it a different job.
