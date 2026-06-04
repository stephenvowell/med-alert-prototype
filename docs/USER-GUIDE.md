# MedAlert — detailed user instructions (prototype)

This guide is for **operators and caregivers** who set up and live with the **MedAlert prototype** (Seeed XIAO ESP32-C6 + MR60BHA2 radar + NeoPixel ring). It complements the developer-focused [`README.md`](../README.md).

| Document | Audience |
|----------|----------|
| **This file** | End users, family, installers — day-to-day setup and behavior |
| [`README.md`](../README.md) | Builders — compile, flash, wiring detail, repo layout |
| [`V2-ROADMAP.md`](V2-ROADMAP.md) | Product / regulatory direction for a future commercial program |

---

## Table of contents

1. [Important limitations](#1-important-limitations)
2. [What you need before starting](#2-what-you-need-before-starting)
3. [Hardware checklist](#3-hardware-checklist)
4. [Physical installation](#4-physical-installation)
5. [First-time Wi‑Fi setup (captive portal)](#5-first-time-wi-fi-setup-captive-portal)
6. [Setup form — field by field](#6-setup-form--field-by-field)
7. [After you tap Save](#7-after-you-tap-save)
8. [Using the home dashboard](#8-using-the-home-dashboard)
9. [What happens when an alarm runs](#9-what-happens-when-an-alarm-runs)
10. [Choosing phone numbers (primary vs family)](#10-choosing-phone-numbers-primary-vs-family)
11. [Twilio account (minimal checklist)](#11-twilio-account-minimal-checklist)
12. [Changing settings later](#12-changing-settings-later)
13. [Troubleshooting](#13-troubleshooting)
14. [Privacy and secrets](#14-privacy-and-secrets)

---

## 1. Important limitations

Read this section once; it sets expectations.

- **This firmware is a prototype.** It is **not** cleared as a medical device. It does **not** diagnose or treat any condition. Use only with **appropriate clinical and legal oversight** for your situation.
- **Radar vitals are estimates.** Heart rate and breathing rate from the mmWave module can **lag**, **drop out**, or **mis-read** because of movement, distance, bedding, room layout, or poor body lock. **Tune thresholds** on the bench before relying on it for anything important.
- **SMS is not the same as calling 911 from a phone.** Messages go through **Twilio** (internet → Twilio → carrier SMS). That path is **not** equivalent to **carrier Text-to-911** or **voice 911**. Do not assume public-safety routing.
- **Home Wi‑Fi must work** for remote SMS and the LAN dashboard in normal (station) mode. If the router or internet is down, **local NeoPixel alarming** can still run, but **SMS may not send**.

If you are using this to support **fundraising or demos**, keep investor language honest: **working demonstrator**, not a certified commercial medical product.

---

## 2. What you need before starting

- A **flashed** MedAlert board (someone with the repo runs `pio run -t upload` — see README).
- **Home Wi‑Fi** SSID and password (2.4 GHz band must be supported by the ESP32-C6; use a network the device is allowed to join).
- A **Twilio** account with a **phone number** you can send **From**, plus **Account SID** and **Auth Token** (see [section 11](#11-twilio-account-minimal-checklist)).
- **Two SMS destination numbers** in **E.164** format (international `+` and country code, e.g. `+15551234567`). For testing, Twilio provides **magic** test destinations (see Twilio docs); for real handsets, use numbers **you control**.
- A **phone or laptop** to join the setup hotspot and open the setup page.
- Optional: a **wire** or jumper to connect **D9 to GND** on the XIAO if you need to **re-open setup** later (see [section 12](#12-changing-settings-later)).

---

## 3. Hardware checklist

Your builder should confirm:

| Item | Purpose |
|------|--------|
| **Seeed XIAO ESP32-C6** | Main brain, Wi‑Fi, USB power/programming |
| **MR60BHA2** mmWave module | UART vitals to the XIAO @ **115200** baud (wiring per Seeed kit) |
| **NeoPixel ring** (e.g. 16 LEDs) | **Red flashing** during alarm-related states |
| **Level shifter** (e.g. 74AHCT125) | Often required: many rings expect **5 V** data; XIAO logic is **3.3 V** |
| **Stable power** | NeoPixels draw surges; a **strong 5 V** supply (README suggests **≥ ~1 A** headroom) is safer than marginal USB |
| **Common ground** | All modules share **GND** |

**Pins (defaults in firmware):**

| Function | XIAO pin | Notes |
|----------|-----------|--------|
| NeoPixel data | **D10** | Change in code only if rewired |
| Force setup at boot | **D9** → **GND** | Optional recovery: opens setup Wi‑Fi even if Wi‑Fi was already saved |

---

## 4. Physical installation

1. **Place the radar** so it can “see” the person’s torso area according to the MR60BHA2 datasheet and Seeed examples — typically **bedside**, aimed toward the bed, within the distance range you configure (default max distance gate **1.5 m** when enabled).
2. **Avoid** constant motion in the beam (fans, curtains, pets) if you get false triggers; adjust **distance gate** and **debounce** in setup.
3. **NeoPixel ring** should be **visible** to someone in the room when it flashes red.
4. **USB cable** can power the unit for bench use; for unattended use, prefer a **quality adapter** and wiring that matches your builder’s power plan.

---

## 5. First-time Wi‑Fi setup (captive portal)

The device enters **setup mode** when:

- There is **no saved home Wi‑Fi SSID** (factory or cleared memory), **or**
- **D9 is tied to GND** while the board **resets** or powers up (see [section 12](#12-changing-settings-later)).

### Step A — Start setup mode

Power the board. If it enters setup mode, the NeoPixel behavior follows the firmware (often idle/clear until alarm — your builder can confirm current build).

### Step B — Join the setup Wi‑Fi from your phone

On your phone or laptop:

1. Open **Wi‑Fi settings**.
2. Join network:

   | Field | Value |
   |-------|--------|
   | **Network name (SSID)** | `MedAlert-Setup` |
   | **Password** | `medalert1` |

3. Wait until connected.

### Step C — Open the setup page

- Many phones show a **“Sign in to network”** or captive portal banner — tap it.
- If nothing pops up, open a browser and go to: **`http://192.168.4.1/`**

You should see **“MedAlert setup”** with a form. If you see a blank page, disable VPN, try another browser, or try `http://192.168.4.1` without the trailing slash.

---

## 6. Setup form — field by field

Fill every required field carefully. **Typos in phone numbers** cause silent mis-delivery.

### Wi‑Fi (home network)

| Field | What to enter |
|-------|----------------|
| **WiFi SSID** | Your **home** network name **exactly** as broadcast (case-sensitive). |
| **WiFi password** | That network’s password. Leave blank only if the network is **open** (uncommon and not recommended). |

The device will try to join this network **after** you save and reboot.

### Twilio (SMS service)

| Field | What to enter |
|-------|----------------|
| **Twilio Account SID** | From Twilio Console (starts with `AC…`). |
| **Twilio Auth Token** | Secret token from Twilio Console — **anyone with this can send SMS on your bill**. Use **Show Wi-Fi password and Twilio Auth Token** to reveal typing (uncheck before handing the phone to someone else). |
| **Twilio From (E.164)** | Your Twilio-owned number, e.g. `+15551234567`. Must be SMS-capable on your account. |

### SMS recipients

| Field | What to enter |
|-------|----------------|
| **Primary SMS (E.164)** | **First** number to receive an SMS when the alarm reaches that stage (see [section 9](#9-what-happens-when-an-alarm-runs)). |
| **Family SMS (E.164)** | **Second** number. The label “Family” is only a name — it is simply the **second** slot in time order. |

**E.164** means: leading `+`, country code, full number. The device **normalizes on save**: spaces, dashes, and parentheses are ignored; if you omit `+`, **10-digit US numbers** become `+1` plus those digits; **11 digits starting with 1** become `+` plus all digits; other digit-only lengths (8–15) get a single leading `+`. You may still type full E.164 with `+` (recommended for non-US).

### Medical / location template

| Field | What to enter |
|-------|----------------|
| **Medical / location template** | Your **custom text** (location, access notes, context). The firmware **appends** vital signs and a short suffix to each SMS (see below). Keep the template concise — SMS has length limits. |

**How your text appears in SMS (current firmware):**

- **First SMS (primary slot):**  
  `[your template]` + ` | HR:` … ` BR:` … + ` | Automated prototype alert—verify before dispatch.`
- **Second SMS (family slot):**  
  `FAMILY NOTICE: ` + `[your template]` + ` | Primary SMS was sent. Last HR:` … ` BR:` …

Default example text in the portal may read like *“Medical alert prototype. Verify before dispatch.”* Replace with text appropriate **for your demo or trial**, not for unsupervised production emergency use without review.

### Thresholds and timing

| Field | Default (typical) | Meaning |
|-------|-------------------|--------|
| **Heart rate below (BPM)** | `40` | Alarm path considers “low” HR when **below** this value (together with breath rate — **both** must be low). |
| **Breath rate below (breaths/min)** | `6` | Same, for breathing rate. |
| **Debounce (ms)** | `15000` (15 s) | How long vitals must stay “bad” before the alarm sequence can start. **Longer** = fewer false alarms, **slower** to react. |
| **Max distance gate (m)** | `1.5` | If the distance gate is on, readings beyond this distance may **block** treating the scene as “at the bed.” |
| **Use distance gate** | Checked | Uncheck only if you understand you may get **more** false triggers. |
| **NeoPixel brightness (0–255)** | `24` | Ring intensity; higher is brighter but uses more current. |

> **Clinical tuning:** These numbers are **not** universal medical advice. Work with a clinician or your builder to pick safe test values for **your** room and **your** physiology.

### Save

Tap **Save & reboot**. The device stores settings in **NVS** (non-volatile memory), reboots, and attempts **station mode** on your home Wi‑Fi.

If Wi‑Fi credentials are wrong, the device may fail to connect; see [Troubleshooting](#13-troubleshooting).

---

## 7. After you tap Save

1. **Reconnect your phone** to your **normal home Wi‑Fi** (leave `MedAlert-Setup`).
2. Find the MedAlert **IP address on your LAN**:
   - Router admin page (“connected devices” / DHCP client list), or
   - USB serial monitor at **115200 baud** — the firmware may print Wi‑Fi status (ask your builder what the current build logs).
3. On any device **on the same home LAN**, open a browser: **`http://<that-ip>/`**

That page is the **dashboard** (status + cancel). There is no cloud login for the prototype; **LAN access only**.

---

## 8. Using the home dashboard

- **Status** — Shows vitals estimates, alarm state, and timers (exact layout depends on firmware version).
- **Cancel alert** — Sends **`POST /api/alarm/cancel`** to the device. Use this when someone has checked the situation and the alert should stop. Cancel behavior is tied to the alarm state machine (see README).
- **Last SMS error** — If Twilio fails, a short error string may appear so you know SMS did not go through (e.g. wrong credentials, quota, or network).

**API (for integrators):** `GET /api/status` returns JSON; `POST /api/alarm/cancel` clears the alarm path as implemented.

---

## 9. What happens when an alarm runs

**Detection (simplified):** While the device is running, it reads **heart rate** and **breathing rate** from the radar. If **both** are **below** your configured thresholds **continuously** for the **debounce** time (and the **distance gate** allows it, if enabled), the firmware can enter an **alarm** path.

**Local alarm:** The **NeoPixel ring flashes red** during alarm-related phases so someone **in the room** gets a strong visual cue.

**SMS staging (when Wi‑Fi and Twilio work):**

| Time (approx.) | Action |
|------------------|--------|
| **Alarm active (first phase)** | Neo flashes red; dashboard shows state; **Cancel** is available. While still in this **first** phase (**before** the primary SMS has been sent successfully), if vitals **return to normal**, the firmware **clears** the alarm and **does not** send SMS. |
| **45 seconds** after alarm entry | **First SMS** to the number saved in **Primary SMS** (`sms_pri`). |
| **60 seconds** after the **first SMS was successfully sent** | **Second SMS** to **Family SMS** (`sms_fam`). |

**Failures:** If a Twilio send fails, firmware uses about a **30 second** backoff before retrying that stage (see README).

**Cooldown:** After the sequence, a **cooldown** (about **30 s** in firmware constants) applies before a new debounce cycle can begin — reduces alarm chatter.

Exact state names and edge cases are in developer docs (`alarm_fsm.cpp`, README).

---

## 10. Choosing phone numbers (primary vs family)

The UI says “Primary” and “Family,” but the device only cares about **order**:

- **Primary field** = **first SMS** (after the first delay from alarm start).
- **Family field** = **second SMS** (after the second delay).

So you may put **who should be notified first** in the Primary box and **who second** in the Family box, regardless of whether you call them “primary” or “family.”

**Capacity:** This prototype supports **two** SMS destinations in this staged pattern. More numbers require **product changes** or an **external relay** (see [`V2-ROADMAP.md`](V2-ROADMAP.md)).

---

## 11. Twilio account (minimal checklist)

1. Create a **Twilio** account at [https://www.twilio.com](https://www.twilio.com).
2. **Buy or claim** an SMS-capable **phone number** (this becomes **Twilio From**).
3. Copy **Account SID** and **Auth Token** from the Console into the setup form.
4. For **early testing**, use Twilio’s documented **test numbers** and magic behaviors so you do not spam real emergency lines or strangers.
5. **Billing:** SMS costs money; set **usage limits** and alerts in Twilio.

**Security:** The **Auth Token** on the device is sensitive. Anyone with physical or network access to the device or your backups could extract NVS in a sophisticated attack — treat the prototype like **credentials in a consumer IoT device**.

---

## 12. Changing settings later

**Option A — Recovery pin (recommended for demos)**

1. Power off the XIAO.
2. **Connect D9 to GND** (jumper wire).
3. Power on while D9 stays grounded.
4. The device starts **`MedAlert-Setup`** again even if Wi‑Fi was previously saved.
5. Open **`http://192.168.4.1/`**, change fields, **Save & reboot**.
6. Remove the D9–GND jumper for normal operation.

**Option B — Clear configuration**

Erase NVS or do a **full chip erase** with the flashing tools (developer operation — see README). Next boot behaves like first-time setup.

---

## 13. Troubleshooting

| Symptom | Things to try |
|---------|----------------|
| Cannot see `MedAlert-Setup` Wi‑Fi | Power-cycle the board; confirm setup mode (no Wi‑Fi saved or D9 grounded at boot). |
| Captive page does not open | Go manually to `http://192.168.4.1/`; turn off VPN; try another device. |
| After Save, device never on home LAN | Wrong SSID/password; 5 GHz-only SSID (ESP32-C6 needs **2.4 GHz** Wi‑Fi); router MAC filtering; weak signal. Re-enter setup with D9. |
| Dashboard does not load | Phone must be on **same LAN** as device; use correct **IP**; try `http://` not `https://`. |
| No SMS | Wi‑Fi down; Twilio credentials wrong; Twilio trial restrictions; invalid E.164; Twilio account billing; check dashboard **last SMS error** if present. |
| Constant false alarms | Raise thresholds slightly; increase **debounce**; tighten **distance gate**; reposition radar; reduce motion in beam. |
| Never alarms when expected | Lower thresholds carefully **with clinical input**; check distance gate not blocking; verify sensor UART wiring and baud. |
| Red LED does not light | Wiring to D10; level shifter; brightness set to 0; power supply too weak. |

For **build and flash** issues, use [`README.md`](../README.md).

---

## 14. Privacy and secrets

- **Wi‑Fi password**, **Twilio token**, and **SMS content** are serious privacy topics. Do not photograph the setup screen with secrets visible for social media.
- **Medical template** text may contain **location** or **access** information — treat it like sensitive operational security.
- This prototype stores configuration in **NVS** on the chip — not encrypted by default on typical ESP32 Arduino setups. A **v2 product** should define a **threat model** and **data handling** policy (see [`V2-ROADMAP.md`](V2-ROADMAP.md)).

---

## Quick reference card

| Item | Value |
|------|--------|
| Setup Wi‑Fi SSID | `MedAlert-Setup` |
| Setup Wi‑Fi password | `medalert1` |
| Setup page URL | `http://192.168.4.1/` |
| Home dashboard | `http://<device-LAN-ip>/` |
| Force setup at boot | **D9** → **GND** |
| First SMS slot | “Primary SMS” field |
| Second SMS slot | “Family SMS” field |
| Cancel alert | Dashboard button → `POST /api/alarm/cancel` |

---

*This document describes the prototype as designed in the repository; firmware revisions may change labels or timings slightly — confirm constants in `alarm_fsm.cpp` and UI strings in `main.cpp` if behavior seems different.*
