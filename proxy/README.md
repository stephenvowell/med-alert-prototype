# Optional Twilio proxy

The firmware can call Twilio **directly** (credentials stored in NVS — convenient for demos, risky for production).

This folder contains a tiny **Express + Twilio** relay so the microcontroller only needs:

- Your HTTPS endpoint
- A long random **device token** (HTTP `Authorization: Bearer …`)

## Run locally

```bash
cd proxy
npm install
set DEVICE_TOKEN=change-me
set TWILIO_ACCOUNT_SID=ACxxxxxxxx
set TWILIO_AUTH_TOKEN=xxxxxxxx
set TWILIO_FROM=+15551234567
npm start
```

Then point a **future** firmware build at `https://your-host/sms` instead of embedding Twilio secrets. The current `main.cpp` uses direct Twilio; to switch, add a small `WiFiClientSecure` POST to this relay mirroring the JSON schema in `server.js`.

## Security notes

- Run behind TLS (Caddy, nginx, or a host like Fly.io / Railway).
- Rotate `DEVICE_TOKEN` if leaked.
- Rate-limit `/sms` at the edge.
