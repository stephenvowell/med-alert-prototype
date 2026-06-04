# MedAlert — mmWave vitals monitor (prototype)

Firmware for a **bedside-style vitals watcher** built from a **Seeed XIAO ESP32-C6**, **MR60BHA2** 60 GHz mmWave radar, and a **NeoPixel** ring. When heart rate and breathing rate stay below configurable limits for a debounced period, the device raises a **local alarm** (flashing LEDs) and can send **staged SMS** via **Twilio** to a primary and a secondary contact.

> **Not a medical device.** Not cleared for diagnosis or treatment. For **lab / demo / engineering evaluation** only, with appropriate clinical and legal oversight.

---

## What you are building (the device)

| Part | Role |
|------|------|
| **Seeed Studio XIAO ESP32-C6** | Main MCU: Wi‑Fi, USB serial/JTAG, web UI, NVS config, Twilio HTTPS client |
| **MR60BHA2** (60 GHz mmWave, Seeed SKU **114993387**) | Non-contact sensing: estimated **heart rate**, **breathing rate**, **distance**, human presence — UART to the XIAO @ **115200** (same wiring as Seeed’s MR60BHA2 + XIAO examples) |
| **Adafruit NeoPixel 16×5050 ring** (PID **1463**) | Local alarm: **red flash** during alert phases (pin **D10** by default) |

Together this forms a **standalone gadget**: it joins your Wi‑Fi (or starts a setup hotspot), serves a small **captive portal** for first-time configuration, then runs **continuously** — radar polling, alarm logic, optional SMS, and a **LAN dashboard** for status and cancel.

---

## Functionality (what it does)

1. **Sensing** — Reads HR / BR / distance from the MR60BHA2 stack (vendored Seeed mmWave library under `lib/`).
2. **Low-vitals detection** — If HR **and** BR are **both** below configured thresholds for a **debounce** window (default **15 s**), an alarm sequence can start. An optional **distance gate** (default **≤ 1.5 m**) reduces false triggers when nobody is near the bed.
3. **Local alarm** — NeoPixel ring **blinks red** while in alarm-related states (non-blocking UI loop).
4. **Staged notifications** — After alarm confirmation timing, sends **Twilio SMS** to a **primary** E.164, then (after another delay) to a **family** E.164. SMS failures use a **30 s** backoff between retries.
5. **Operator UI** — On the LAN: **live JSON status** (`/api/status`), **cancel** (`POST /api/alarm/cancel`), and a simple HTML dashboard. First boot (or forced setup) uses **SoftAP** `MedAlert-Setup` / password `medalert1` and captive portal at `http://192.168.4.1/`.
6. **Persistence** — Wi‑Fi, Twilio fields, thresholds, and options stored in **NVS** (`Preferences`).

### Alarm timeline (summary)

| Phase | Behavior |
|--------|-----------|
| Low vitals | HR and BR both under thresholds for debounce (optional distance gate). |
| Alarm | Neo flashes red; dashboard shows state and countdowns; cancel via UI or API. Vitals recovering before SMS can cancel the pre-SMS alarm. |
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
| Force setup portal | **D9** → **GND** at boot | Optional; portal also runs if **no WiFi SSID** in NVS. |

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

**IDE / red squiggles (many “errors”):** With **ESP32‑C6 + RISC‑V GCC**, **clangd** often cannot fully model the cross‑compiler’s libstdc++ paths even with `compile_commands.json`, so the Problems panel may show noise (for example `<algorithm>` not found) while **`pio run` is clean**.

This workspace **disables clangd** and uses **Microsoft C/C++ IntelliSense** with `compile_commands.json` (see `.vscode/settings.json`). Install the **C/C++** extension (`ms-vscode.cpptools`) if prompted.

If you prefer **clangd** instead: set `clangd.enable` to `true` in `.vscode/settings.json`, add a permissive `--query-driver=.+` entry under `clangd.arguments`, run a full **`pio run`** (not only `compiledb`) so `scripts/patch_compile_commands.py` can rewrite the compiler to an **absolute** `riscv32-esp-elf-g++.exe` path, then **Reload Window**.

1. Generate / refresh the DB:  
   `"%USERPROFILE%\.platformio\penv\Scripts\pio.exe" run -t compiledb`  
   A subsequent **`pio run`** (normal build) patches `compile_commands.json` for clangd (see `scripts/patch_compile_commands.py`).

2. **`.clangd`** still strips GCC-only flags Clang rejects: `-fstrict-volatile-bitfields`, `-fno-tree-switch-conversion`.

3. After config changes: **Developer: Reload Window**.

---

## First-time configuration

1. Flash firmware. With **no WiFi SSID** in NVS, join **`MedAlert-Setup`** / `medalert1`.
2. Captive portal: **`http://192.168.4.1/`** — enter Wi‑Fi, Twilio **Account SID** / **Auth Token**, **From** (E.164), **primary** and **family** SMS targets, medical text template, thresholds, debounce, distance gate, Neo brightness. **Save** reboots to STA mode.
3. On your LAN: **`http://<device-ip>/`** — dashboard + **Cancel alert**.

Re-open the portal later: **ground D9** at reset, or erase NVS / full chip erase.

---

## Twilio

Create a Twilio account, buy a number, then configure:

- **Account SID** + **Auth Token** (HTTP Basic from firmware via `HTTPClient`).
- **From** = your Twilio number (E.164).
- **Primary / family** = destinations you control for testing.

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
| `pio_no_progress.py` | PlatformIO post-hook: inserts esptool `--no-progress` after `write-flash` (Windows-friendly) |
| `proxy/` | Optional Twilio relay |

---

## License

Prototype — use and modify **at your own risk**. **No warranty.** Third-party materials under `lib/` remain subject to their upstream licenses (see `lib/Seeed Arduino mmWave/LICENSE`).
