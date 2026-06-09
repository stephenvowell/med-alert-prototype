# MedAlert — mmWave vitals monitor (prototype)

Firmware for a **bedside-style vitals watcher** built from a **Seeed XIAO ESP32-C6**, **MR60BHA2** 60 GHz mmWave radar, and a **NeoPixel** ring. When heart rate and breathing rate stay below configurable limits for a debounced period **while the radar reports a person present**, the device raises a **local alarm** (flashing LEDs) and can send **staged SMS** via **Twilio** to a primary and a secondary contact.

**Purpose of this repository:** A **working prototype** to **demonstrate** the product vision—sensing, alarms, configuration, and remote notification—for **fundraising** and conversations toward **bringing a future product to market**. Pitches and decks should still distinguish **“investable prototype”** from **cleared / commercial medical hardware** (see v2 roadmap).

> **Not a medical device.** Not cleared for diagnosis or treatment. For **lab / demo / engineering evaluation** only, with appropriate clinical and legal oversight.

**Future direction (draft):** A v2 program outline—cellular backup, monitored escalation, regulatory path—is in [`docs/V2-ROADMAP.md`](docs/V2-ROADMAP.md). Edit that file with your counsel and partners.

**End-user instructions:** Step-by-step setup, form fields, dashboard, alarm timeline, and troubleshooting are in [`docs/USER-GUIDE.md`](docs/USER-GUIDE.md) (for caregivers, installers, and demos). **UI reference figures** (SVG wireframes of the setup portal and LAN dashboard) live in [`docs/images/`](docs/images/).

---

## What you are building (the device)

| Part | Role |
|------|------|
| **Seeed Studio XIAO ESP32-C6** | Main MCU: Wi‑Fi, USB serial/JTAG, web UI, NVS config, Twilio HTTPS client |
| **MR60BHA2** (60 GHz mmWave, Seeed SKU **114993387**) | Non-contact sensing: estimated **heart rate**, **breathing rate**, **distance**, human presence — UART to the XIAO @ **115200** (same wiring as Seeed’s MR60BHA2 + XIAO examples) |
| **Adafruit NeoPixel 16×5050 ring** (PID **1463**) | Local alarm: **red flash** during alert phases (pin **D10** by default) |

Together this forms a **standalone device**: it joins your Wi‑Fi (or starts a setup hotspot), serves a small **captive portal** for first-time configuration, then runs **continuously** — radar polling, alarm logic, optional SMS, and a **LAN dashboard** for status and cancel.

---

## Functionality (what it does)

1. **Sensing** — Reads HR / BR / distance from the MR60BHA2 stack (vendored Seeed mmWave library under `lib/`).
2. **Low-vitals detection** — If HR **and** BR are **both** below configured thresholds for a **debounce** window (default **15 s**), an alarm sequence can start **only while the radar reports a person present**; an optional **distance gate** (default **≤ 1.5 m**) reduces false triggers when nobody is near the bed.
3. **Local alarm** — NeoPixel ring **blinks red** while in alarm-related states (non-blocking UI loop).
4. **Staged notifications** — After alarm confirmation timing, sends **Twilio SMS** to a **primary** E.164, then (after another delay) to a **family** E.164. SMS failures use a **30 s** backoff between retries.
5. **Operator UI** — On the LAN: **live JSON status** (`/api/status`), **cancel** (`POST /api/alarm/cancel`), and a simple HTML dashboard. First boot (or forced setup) uses **SoftAP** `MedAlert-Setup` / password `medalert1` and captive portal at `http://192.168.4.1/`.
6. **Persistence** — Wi‑Fi, Twilio fields, thresholds, and options stored in **NVS** (`Preferences`).

### Alarm timeline (summary)

| Phase | Behavior |
|--------|-----------|
| Low vitals | HR and BR both under thresholds for debounce **while radar reports a person** (optional distance gate). |
| Alarm | Neo flashes red; dashboard shows state and countdowns; cancel via UI or API. Vitals recovering **or loss of person-detect** before SMS can cancel the pre-SMS alarm. |
| T + 45 s | First SMS → **primary** number. |
| T + 45 s + 60 s | Second SMS → **family** number. |
| Cooldown | **30 s** before a new debounce cycle may start. |

---

## Safety and legal limitations

1. **SMS is not 911** — Twilio (or any OTT SMS API) is **not** the same as carrier **SMS-to-911** or a **voice E911** call. Do not assume PSAP routing. For development use **Twilio test numbers** (e.g. `+15005550006` magic routes) or your own handsets until counsel and partners approve production routing.
2. **Radar vitals are estimates** — HR/BR can drop, lag, or mis-read with motion, multipath, distance, or poor lock. Tune on the bench.
3. **TLS prototype** — Twilio uses `WiFiClientSecure::setInsecure()` for speed of bring-up. For real deployments, **pin Twilio’s CA** and enable proper cert validation.

---

## Wiring (quick reference)

| Signal | XIAO ESP32-C6 | Notes |
|--------|----------------|--------|
| NeoPixel **DIN** | **D10** (`PIN_NEOPIXEL` in `main.cpp`) | Many rings are **5 V** data; XIAO is **3.3 V** — use a **74AHCT125** (or similar) level shifter + common **GND**. |
| NeoPixel **5V** / **V+** | **5 V** (≥ **1 A** peak recommended) | Prefer a dedicated supply, not marginal USB back-power. |
| NeoPixel **GND** | **GND** | |
| Force setup portal | **D9** → **GND** at boot | On **XIAO ESP32-C6**, silkscreen **D9** is **GPIO20** (Arduino `D9`); firmware uses that pin. |

MR60BHA2 ↔ XIAO UART follows Seeed’s kit (this project uses **`HardwareSerial(0)` @ 115200**).

---

## Build and flash

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI).
2. **ESP32-C6 + Arduino** needs the **pioarduino** platform fork (see `platformio.ini`); stock registry `espressif32` still marks this board as ESP-IDF-only for Arduino.
3. From the repo root:

```bash
pio run -t upload
pio device monitor
```

Windows: if `pio` is missing from PATH, use e.g.  
`%USERPROFILE%\.platformio\penv\Scripts\pio.exe run -t upload`.

**Arduino IDE Serial Monitor:** use the **same COM port** that upload uses, baud **115200**, and open the monitor **before** reset (or press reset after opening). This firmware uses **native USB CDC** with **`ARDUINO_USB_CDC_ON_BOOT` + `ARDUINO_USB_MODE`** in `platformio.ini` (both are required for `Serial` to reach the USB port on many ESP32‑C6 Arduino builds). The chip waits briefly at boot for the host to attach so the first log lines are not lost. After flashing with a new USB build flag, do a full **upload** again.

**If the monitor opens but you see no text:** run **`pio device list`** (or Windows Device Manager) and confirm you are on the COM that **disappears when you unplug the XIAO**; try **reset** while the monitor is open; use a **USB data** cable; close other apps holding the port. In **SoftAP portal** mode the firmware used to print only at boot—this project also emits a **`[hb]`** line about every **20 s** so an open monitor is not silent.

### Serial monitor (Cursor / no bottom “plug”)

**PlatformIO IDE for Cursor** often does **not** show the classic **status-bar plug** icon. Use any of these instead:

| How | What to do |
|-----|------------|
| **Keyboard** | **`Ctrl+Alt+S`** — runs **PlatformIO: Serial Monitor** (binding from the extension). |
| **Editor title bar** | Open a project file (e.g. `src/main.cpp` or `platformio.ini`). With **Show buttons in the editor title bar** on (workspace default), look at the **top-right of the editor** for the **plug** and other PIO actions — not only the bottom bar. |
| **PlatformIO sidebar** | Click the **PlatformIO (alien)** icon → **Project Tasks** → **seeed_xiao_esp32c6** → **Monitor** → **Monitor**. |
| **Command Palette** | **Ctrl+Shift+P** → **`PlatformIO: Serial Monitor`** or **`PlatformIO: Upload and Monitor`**. |
| **Run Task** | **Terminal → Run Task…** → **`PIO: Serial Monitor`** (uses `pio.exe` directly; no extension UI needed). |
| **Windows batch** | Double-click **`scripts/monitor.bat`** (opens a console on the repo; uses `platformio.ini` port/baud). |

This workspace sets **`platformio-ide.showEditorTitleShortcuts": true`** so title-bar shortcuts stay on.

**If Ctrl+Shift+P shows no “PlatformIO: …” commands:** those commands only register after the extension sees a **folder workspace** whose **root contains `platformio.ini`**. Do not open only `src/` or a single file — open **`med-alert-prototype`** as the folder. Wait a few seconds for PlatformIO to start; check **View → Output** and choose **PlatformIO** in the dropdown for errors. You can still open the monitor without the palette: **Ctrl+Shift+P** → **`Tasks: Run Task`** → type **`serial`** → run **`Serial monitor — USB (no PlatformIO command needed)`** or **`PIO: Serial Monitor`**.

### Firmware version

Each **`pio run`** runs **`scripts/gen_build_version.py`**, which generates **`include/build_version.h`** (listed in `.gitignore`, not committed). The binary embeds:

- **`fw_ver` / `MEDALERT_FW_VERSION`** — `git describe --tags --always --dirty` (use **`git tag v0.1.0`** etc. for human-readable release names).
- **`fw_git`** — short SHA.
- **`fw_build`** — Unix timestamp at **compile** time (so every rebuild gets a new build id even on the same commit).

These appear on **Serial** at boot, on the **setup** and **LAN dashboard** pages, and in **`GET /api/status`** (`fw_ver`, `fw_git`, `fw_build`). Until the first successful **`pio run`**, `include/build_version.h` may not exist locally; `main.cpp` uses `__has_include` and falls back to a **`dev`** label so the project still parses in the IDE.

**IDE / red squiggles (many “errors”):** With **ESP32‑C6 + RISC‑V GCC**, **clangd** often cannot fully model the cross‑compiler’s libstdc++ paths even with `compile_commands.json`, so the Problems panel may show noise (for example `<algorithm>` not found) while **`pio run` is clean**.

This workspace **disables clangd** and uses **Microsoft C/C++ IntelliSense** with `compile_commands.json` (see `.vscode/settings.json`). Install the **C/C++** extension (`ms-vscode.cpptools`) if prompted.

If you prefer **clangd** instead: set `clangd.enable` to `true` in `.vscode/settings.json`, add a permissive `--query-driver=.+` entry under `clangd.arguments`, run a full **`pio run`** (not only `compiledb`) so `scripts/patch_compile_commands.py` can rewrite the compiler to an **absolute** `riscv32-esp-elf-g++.exe` path, then **Reload Window**.

1. Generate / refresh the DB:  
   `"%USERPROFILE%\.platformio\penv\Scripts\pio.exe" run -t compiledb`  
   A subsequent **`pio run`** (normal build) patches `compile_commands.json` for clangd (see `scripts/patch_compile_commands.py`).

2. **`.clangd`** still strips GCC-only flags Clang rejects: `-fstrict-volatile-bitfields`, `-fno-tree-switch-conversion`.

3. After config changes: **Developer: Reload Window**.

---

## Optional embedded credentials (`include/credentials.h`)

For **development** or a **headless first boot**, you can supply Wi‑Fi and Twilio strings at **compile time** instead of typing them only in the captive portal:

1. Copy [`include/credentials.example.h`](include/credentials.example.h) to **`include/credentials.h`** (same directory).
2. Uncomment and set the `MEDALERT_*` `#define` lines you need.
3. Build and flash. **`include/credentials.h` is gitignored** — never commit real secrets.

After `configLoad`, the firmware fills **only NVS fields that are still empty**. Once you **Save** in the portal, those values live in NVS and override the embedded defaults on later boots.

**Disable without deleting the file:** in `platformio.ini`, keep **`-DMEDALERT_SKIP_CREDENTIALS_H=1`** under `build_flags` (default in this repo) so `credentials.h` is ignored and empty NVS still opens **SoftAP**. Remove that flag when you want compile-time defaults again.

---

## First-time configuration

1. Flash firmware. With **no WiFi SSID** in NVS, join **`MedAlert-Setup`** / `medalert1`.
2. Captive portal: **`http://192.168.4.1/`** — enter Wi‑Fi, Twilio **Account SID** / **Auth Token**, **From** (E.164), **primary** and **family** SMS targets, medical text template, thresholds, debounce, distance gate, Neo brightness. **Save** reboots to STA mode.
3. On your LAN: **`http://<device-ip>/`** — dashboard + **Cancel alert**. The page footer shows **firmware version / git / build** (same data as `GET /api/status`).

Re-open the portal later: **ground D9** at reset, or erase NVS / full chip erase.

**If you never see `MedAlert-Setup` or `http://192.168.4.1/`:** the board may be in **STA mode** (Wi‑Fi already saved in NVS). Open **USB serial at 115200**: it prints whether SoftAP started, whether **D9 (GPIO20 on XIAO ESP32-C6)** was LOW at boot to force the portal, and (in STA) **`[wifi] connected IP=…`** when join succeeds. **Wrong saved Wi‑Fi:** after **60 s** without a connection the firmware **opens the setup SoftAP automatically**. Use **D9 → GND while resetting** to force the portal immediately. If you used `include/credentials.h`, remove **`MEDALERT_SKIP_CREDENTIALS_H`** only when you want compile-time Wi‑Fi again. Use a **powered USB** port or hub if the AP is flaky.

---

## Twilio

Create a Twilio account, buy a number, then configure:

- **Account SID** + **Auth Token** (HTTP Basic from firmware via `HTTPClient`).
- **From** = your Twilio number (E.164).
- **Primary / family** = destinations you control for testing.

**SMS consent:** The captive portal requires an **opt-in acknowledgment** before save; the device stores that flag plus an optional **browser timestamp** in NVS and exposes them on **`GET /api/status`**. See [`docs/SMS-OPT-IN.md`](docs/SMS-OPT-IN.md) for what is recorded and how it relates to carrier/Twilio expectations (not legal advice).

---

## Optional HTTPS proxy (production-style secrets)

To avoid storing Twilio secrets on-device, use the small Node relay in [`proxy/README.md`](proxy/README.md) (`proxy/server.js`).

---

## Repository layout

| Path | Purpose |
|------|---------|
| `src/main.cpp` | Wi‑Fi AP/STA, WebServer, captive portal + dashboard, mmWave poll, NeoPixel, SMS |
| `src/alarm_fsm.cpp` | Debounce, staged alarm, cooldown |
| `src/config_store.cpp` | NVS (`Preferences`) config |
| `src/twilio_client.cpp` | Twilio REST over TLS |
| `lib/Seeed Arduino mmWave/` | Vendored mmWave driver (header fix documented in [`lib/README.md`](lib/README.md)) |
| `scripts/patch_compile_commands.py` | Post-link: rewrite `compile_commands.json` driver to absolute `riscv32-esp-elf-g++.exe` (helps clangd if you re-enable it) |
| `scripts/gen_build_version.py` | Pre-build: write `include/build_version.h` (`git describe` + build time) for `fw_ver` / Serial / UI |
| `scripts/monitor.bat` | Windows: double-click to run `pio device monitor` from repo root (COM/baud from `platformio.ini`) |
| `include/credentials.example.h` | Template for optional `include/credentials.h` (Wi‑Fi / Twilio compile defaults; real file is gitignored) |
| `pio_no_progress.py` | PlatformIO post-hook: inserts esptool `--no-progress` after `write-flash` (Windows-friendly) |
| `proxy/` | Optional Twilio relay |
| `docs/V2-ROADMAP.md` | Draft v2 direction (cellular, monitoring, regulatory) — edit with counsel / partners |
| `docs/USER-GUIDE.md` | Detailed operator / caregiver instructions (setup portal, dashboard, SMS behavior) |
| `docs/SMS-OPT-IN.md` | What the firmware stores for SMS consent / audit (not legal advice) |

---

## License

Prototype — use and modify **at your own risk**. **No warranty.** Third-party materials under `lib/` remain subject to their upstream licenses (see `lib/Seeed Arduino mmWave/LICENSE`).
