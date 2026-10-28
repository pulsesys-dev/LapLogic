[← LapLogic](../README.md)

# Building LapLogic yourself

## What's in the repository

| Folder / file | What it's for |
|---|---|
| `LapLogic-Flasher.html` | **Start here.** Installs LapLogic and sets up Wi-Fi from Chrome or Edge |
| `firmware/LapLogic-*.bin` | Ready-to-flash firmware, one per board, for other flashing tools |
| `firmware/<board>/` | The same firmware as separate parts, for advanced tools (see `OFFSETS.txt`) |
| `LapLogic/` | Source code. Open `LapLogic/LapLogic.ino` in the Arduino IDE |
| `platformio.ini` | Build file for PlatformIO (uses the same `LapLogic/` source) |
| `docs/` | Screenshots of every page on both boards |
| `tools/` | Builds `LapLogic-Flasher.html` from the firmware (see [Customising](#customising)) |


## With the Arduino IDE

The same sketch builds for both boards; it detects which one from the board you select in the
Tools menu. Use these **exact versions**: newer versions of these libraries changed their APIs
and won't compile.

### One-time setup

1. Install the [Arduino IDE 2](https://www.arduino.cc/en/software).
2. **Board support:** *Arduino IDE → Settings* (Mac) or *File → Preferences* (Windows). In
   *Additional boards manager URLs* add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   Then open *Boards Manager*, search **esp32** (by Espressif Systems) and install version **2.0.17**.
3. **Libraries:** open *Library Manager* and install these versions (use the version dropdown):

   | Library | Author | Version |
   |---|---|---|
   | GFX Library for Arduino | Moon On Our Nation | **1.4.9** |
   | lvgl | kisvegabor | **8.3.11** |
   | ArduinoJson | Benoit Blanchon | **7.2.1** |
   | WiFiManager | tzapu | **2.0.17** |
   | PubSubClient | Nick O'Leary | **2.8** |

   If the IDE offers to install extra dependencies, click *Install all*.
4. **Copy `LapLogic/lv_conf.h` into your Arduino libraries folder**, next to the `lvgl` folder
   (not inside it):
   - Mac: `~/Documents/Arduino/libraries/lv_conf.h`
   - Windows: `Documents\Arduino\libraries\lv_conf.h`

   If you skip this you'll get an error saying `lv_conf.h` can't be found. **Copy it again
   whenever you update LapLogic.** If the copy is out of date, LapLogic prints a warning in the
   serial log at start-up, and the CYD will run short of memory.

### Build and upload

Open `LapLogic/LapLogic.ino`, then set the **Tools** menu for your board:

| Setting | Guition JC4827W543C | CYD ESP32-2432S028R |
|---|---|---|
| Board | **ESP32S3 Dev Module** | **ESP32 Dev Module** |
| Flash Size | **4MB (32Mb)** | **4MB (32Mb)** |
| Partition Scheme | **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** | **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** |
| PSRAM | **OPI PSRAM** | **Disabled** |
| Flash Mode | **QIO 80MHz** | (leave default) |
| USB CDC On Boot | **Enabled** | (not shown) |
| USB Mode | **Hardware CDC and JTAG** | (not shown) |
| Port | the board's USB port | the board's USB port |

Click **Upload**. The first build takes a few minutes because LVGL is large.


## With PlatformIO

Open this folder (the one containing `platformio.ini`) in VS Code with the PlatformIO extension,
or from a terminal:

```
pio run -e jc4827w543 -t upload     # Guition 4.3"
pio run -e cyd -t upload            # Cheap Yellow Display 2.8"
pio device monitor
```

Libraries and the ESP32 platform are downloaded automatically at the pinned versions.


## Customising

- `LapLogic/config.h`: pins for each board, night-dimming hours, brightness, refresh intervals, hostname, hotspot name
- `LapLogic/ui.cpp`: the dark and light colour schemes (`DARK`, `LIGHT`), layout, and teams
  (`TEAM_COLOURS`: colour, badge code and name, keyed by the API's team id).
  Layout values are written as `PICK(4.3" value, 2.8" value)`; portrait layouts branch on `PORT`
- `LapLogic/settings.cpp`: the time zone list, and every setting by name (shared by the screen,
  the web page and USB setup)
- `tools/web/`: the settings page and form. The same form is used on the board's page and in the
  flasher. After editing, run `python3 tools/build_webpage.py` (writes `LapLogic/webpage.h`), then
  rebuild the firmware
- **After changing the firmware**, rebuild it for both boards, copy the merged `.bin` files into
  `firmware/` (same names), then run `python3 tools/build_flasher.py` to regenerate
  `LapLogic-Flasher.html`. It needs only Python 3, no internet.

## How it works

| File | Job |
|---|---|
| `LapLogic.ino` | Start-up, main loop, background data fetching on the second CPU core |
| `config.h` | Picks the board from the chip being compiled for, and holds its pins |
| `board_jc4827.cpp`, `gt911.*` | Guition: display (NV3041A over QSPI), capacitive touch, backlight |
| `board_cyd.cpp` | CYD: display (ILI9341 or ST7789, auto-detected), resistive touch and calibration, backlight |
| `net.*` | Wi-Fi setup hotspot, network time, `laplogic.local`, time zone detection |
| `web.*`, `webpage.h` | The settings page at `laplogic.local` and its JSON API (`/api/settings`, `/api/action`) |
| `settings.*` | Every setting, stored in flash, read and changed by name |
| `improv.*` | [Improv Wi-Fi](https://www.improv-wifi.com/serial/) over USB, so a web flasher can send Wi-Fi details, plus LapLogic's own commands for reading and changing settings |
| `f1data.*` | Fetches and parses the calendar, standings and results (Jolpica) |
| `logos.*`, `pngsmall.*` | Team logos: reads PNGs from the CYD's SD card at start-up, shrinks them to badge size and keeps them in flash |
| `ota.*` | Updates over Wi-Fi from GitHub releases: the daily check, the prompt, the download into the spare firmware slot |
| `led.*` | CYD: the rear race light, driven by the session times |
| `weather.cpp` | Weather at the next circuit (Open-Meteo, over plain HTTP so it needs very little memory) |
| `trackdata.*` | Track map: circuit outlines and final-lap replays from OpenF1 (CSV, streamed), cached in flash; live positions over MQTT on the 4.3" board |
| `ui.*` | All the screens (LVGL 8.3), for both screen sizes in landscape and portrait |
