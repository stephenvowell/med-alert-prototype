# MedAlert — Alexa / Matter feature set (draft)

This document defines a **candidate Alexa integration feature set** for a future MedAlert product generation. It is **product and engineering planning notes only** — not a commitment of features, not legal advice, and (like [`V2-ROADMAP.md`](V2-ROADMAP.md)) subject to review with **regulatory counsel** before any marketing claim is made.

**Positioning context:** MedAlert's core promise is **peace of mind** — quiet reassurance that someone you care about is okay, and a strong multi-channel alert when they may not be. Two personas share the same radar capabilities:

| Persona | Watching over | Peace-of-mind moment |
|---------|---------------|----------------------|
| **Caregiver** (adult child, spouse, home aide) | Elder or patient in bed | "Mom is in bed and breathing normally — I can sleep." |
| **Parent** | Infant in crib | "Baby is settled, breathing 24/min — without me opening the nursery door." |

**Privacy as the wedge:** Competing baby and elder monitors are cameras and microphones. MedAlert senses **presence and breathing by radar only — no camera, no microphone**. That is a lead differentiator for bedrooms and nurseries and should anchor the Alexa-era marketing voice as well.

---

## 1. Why Alexa (and why Matter)

- An LED ring alerts **the room**; every Echo in the house alerts **the household**. Announcements, lights, and phone notifications via Alexa Routines turn a bedside alarm into a **whole-home escalation channel** — complementing (not replacing) the Twilio SMS path.
- The ESP32-C6 already in the design natively supports **Matter over Wi-Fi**, and Amazon has qualified the C6 for its **Alexa Connect Kit (ACK) SDK for Matter**. Matter also brings Google Home and Apple Home support **for free** — one integration, three ecosystems.
- Tiers 1–2 below require **no Amazon partnership**: standard Matter device types plus user-configured Alexa Routines. Tier 3 is where ACK and/or a custom Alexa Skill (and recurring revenue) enter.

---

## 2. Tier 1 — Safety alerts (MVP)

Achievable with standard **Matter device types + Alexa Routines**. No custom skill, no cloud.

| Feature | Experience | Mechanism |
|---------|------------|-----------|
| **Whole-home alarm announcement** | Every Echo announces "MedAlert: alert in the nursery"; Alexa-connected lights can flash | Matter **contact/alarm sensor** state change → Alexa Routine |
| **Occupancy alerts** | "The crib has been empty for 10 minutes" / elder out of bed at night | Matter **occupancy sensor** (radar person-present) + Routine timer condition |
| **Voice cancel** | "Alexa, turn off the MedAlert alarm" | Matter **on/off (switch/mode)** cluster mapped to alarm cancel (same semantics as `POST /api/alarm/cancel`) |
| **Caregiver phone notification** | Alexa app push notification alongside the announcement | Built into Routines — a free second channel beside Twilio SMS |

**Exposed Matter device types (working set):**

| Matter device type | Backing signal (v1 firmware equivalent) |
|--------------------|------------------------------------------|
| Occupancy sensor | `human` (MR60BHA2 person-present) |
| Contact / alarm sensor | Alarm FSM state ≥ `alarm_pending` |
| On/off (cancel control) | `alarmCancel()` path |

## 3. Tier 2 — Reassurance (peace-of-mind differentiators)

Still Routine-driven or simple device behavior; this is where the product feels different from an alarm box.

- **"Alexa, check on the baby" / "check on Mom"** — returns presence + breathing: *"Someone is in the crib, breathing 24 times per minute."* The demo centerpiece.
- **Settled / quiet-hours confirmation** — proactive evening announcement: "MedAlert: baby settled, vitals normal." Reassurance **without being asked**.
- **Morning summary** — "All quiet overnight, no alerts." The absence of bad news, delivered.
- **Sensor health watchdog** — device offline or radar lock lost → Alexa announces it. A silently dead monitor is the nightmare scenario for both personas; **detecting that is itself the product**.

> **Note (engineering):** Vitals values (breathing rate) are not cleanly expressible in today's standard Matter clusters; Tier 2 voice **queries** of numeric vitals may require a custom skill or vendor cluster. Presence/settled/offline states map to standard clusters. Validate during the Matter spike (§5).

## 4. Tier 3 — Premium / subscription potential

Requires a **custom Alexa Skill** and operator cloud (aligns with the v2 public-API direction, V2-ROADMAP §4.1). Candidate recurring-revenue surface:

- **Natural-language history** — "How did Mom sleep this week?"
- **Trends and gentle anomaly flags** — restlessness, breathing-rate drift (wellness language only — see §6)
- **Multi-home caregiving** — adult child's Echo announces alerts from a parent's house across town
- **Care-circle escalation** — Alexa announcement → app notification → Twilio SMS → voice call, as one configured policy (v2 policy engine)

---

## 5. Engineering path (suggested order)

1. **Bench spike (weekend-scale):** C6 dev board running the esp-matter (or arduino-esp32 3.x Matter) **occupancy-sensor example**, paired to an Echo, Routine announcing on state change. Proves the entire Tier 1 story end-to-end before any architecture commitment. Demos extremely well.
2. **Flash budget check:** v1 firmware is at ~92% of a 4 MB app partition **before** the Matter stack. Expect a partition-table change at minimum; plan for an **8 MB / 16 MB flash module** in v2 hardware selection.
3. **ACK conversation (parallel):** email `ack-apply@amazon.com` — (a) does a health-adjacent alert device fit supported ACK device categories? (b) per-device economics. Outcome decides ACK-managed infrastructure vs. plain Matter + own/RainMaker backend.
4. **Certification (productization):** Matter certification via **CSA** (logo + reliable pairing); **Works with Alexa** badging via Amazon if desired.

---

## 6. Claims discipline (counsel before copy)

- **Baby monitoring is an FDA/FTC minefield** (see Owlet warning-letter history). "Peace of mind," "wellness," "see that baby is settled" — acceptable direction. **"Prevents SIDS," "detects medical conditions," "alerts you to a health emergency" — not** without clearance and clinical evidence.
- Same discipline the README already applies to the elder-care side: alerts support **human judgment**; the product does not diagnose, treat, or dispatch emergency services.
- Alexa announcements are **not** an emergency-services channel and copy must never imply 911/PSAP routing (consistent with V2-ROADMAP §2–§4).

---

## 7. Document control

| Field | Value |
|-------|--------|
| Status | Draft |
| Owner | Project maintainer |
| Reviewers | Regulatory counsel (TBD), marketing (TBD) |

*Last updated: 2026-06-10 — initial feature set: safety / reassurance / premium tiers, Matter device-type mapping, baby-monitor persona, claims discipline.*
