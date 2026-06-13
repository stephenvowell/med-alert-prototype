# Draft — ACK program application email

**To:** ack-apply@amazon.com
**Status:** Draft — fill in signature block before sending.
**Context:** See [`ALEXA-FEATURES.md`](ALEXA-FEATURES.md) §5 (engineering path, step 3).

---

**Subject:** ACK SDK for Matter — application / device category inquiry (radar-based home alert device, ESP32-C6)

Dear Alexa Connect Kit team,

I am an independent product developer preparing to commercialize **MedAlert**, a bedside presence-and-vitals monitor currently at the working-prototype stage, and I would like to apply for access to the **ACK SDK for Matter** program.

**About the product:**

- A standalone bedside device that uses **60 GHz mmWave radar** to sense human presence and estimate breathing/heart rate — **no camera, no microphone**, which is central to our privacy positioning for bedrooms and nurseries.
- When configurable alert conditions are met, the device raises a local visual alarm and notifies designated family members or caregivers (currently via SMS).
- Two target uses: **caregiver reassurance** (elder or patient in bed) and **infant monitoring** (presence and settled-state awareness for parents).
- Built on the **ESP32-C6** — I was pleased to see its recent qualification for ACK SDK for Matter, which prompted this inquiry.

**Planned Alexa integration:** exposing the device through standard Matter device types (occupancy sensor, alarm/contact sensor, cancel control) so households can use Echo announcements, Routines, and the Alexa app as a whole-home notification and reassurance layer.

**Two questions before we architect around ACK:**

1. **Device category fit** — MedAlert is a wellness/awareness product, not a regulated medical device, and we are careful to avoid medical claims. Does a health-adjacent home alert device of this kind fit within ACK's supported device categories and Works with Alexa certification scope?
2. **Program economics** — could you share the current per-device fee structure and any minimum-volume expectations, so I can evaluate ACK against a self-managed Matter implementation for our first production run?

I'd welcome the opportunity to discuss the product in more detail or provide additional documentation. Thank you for your time.

Best regards,
[Your name]
[Company / project name]
[Phone · email · website]

---

## Sending checklist

- [ ] Fill in the signature block — use the LLC / company entity if one exists (Amazon partner programs generally expect a business identity).
- [ ] Keep the "wellness/awareness product, not a regulated medical device" framing consistent with counsel-approved language.
- [ ] Log the response: answers to the two questions feed the ACK-vs-plain-Matter decision in `ALEXA-FEATURES.md` §5.
