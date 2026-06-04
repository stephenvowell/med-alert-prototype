# SMS opt-in and audit trail (prototype)

This is **not legal advice**. Twilio, carriers, and A2P / 10DLC rules expect you to **obtain and retain proof of consent** before sending application-to-person SMS. How you do that in production is a **program and counsel** decision.

## What this firmware records

When an operator saves the captive portal form **with the SMS opt-in checkbox checked**, the device stores in **NVS** (same `Preferences` namespace as Wi‑Fi / Twilio):

| NVS key        | Meaning |
|----------------|---------|
| `sms_opt_in`   | `true` — operator confirmed consent on save. |
| `sms_opt_in_ts`| Up to 40 characters: sanitized **ISO 8601 string** from the **browser’s clock** at submit (via hidden field), when JavaScript runs. If JS is off or the field is empty, this may be blank; the boolean still records that the box was checked at save. |

**`GET /api/status`** exposes `sms_opt_in` and `sms_opt_in_recorded_at` so you can screenshot the home dashboard or call the API after setup.

**Serial** logs a line at successful save, for example:

```text
[cfg] SMS opt-in saved (operator ack); client_ts=2026-05-31T12:34:56.789Z
```

## Behavior

- **Save** is rejected with HTTP 400 if the opt-in checkbox is not checked.
- **Outbound SMS** is **blocked** until `sms_opt_in` is true (e.g. after a full portal save on this firmware). If you upgrade from an older build that never saved opt-in, **open setup once**, confirm the box, and **Save** again.

## Suggested records beyond the device

For Twilio or internal compliance you will typically also keep:

- **Who** each destination number belongs to and **how** they agreed (paper, app flow, recorded call policy, etc.).
- **Message type** (e.g. automated medical alert) and **how to opt out** (even if this prototype does not implement STOP handling).
- Screenshots or exports of the dashboard / API showing `sms_opt_in` and `sms_opt_in_recorded_at` **after** setup, if useful for your audit binder.

See also [`USER-GUIDE.md`](USER-GUIDE.md) (setup form) and [`README.md`](../README.md).
