/**
 * Minimal Twilio relay for development.
 *
 * Environment:
 *   PORT                (default 8787)
 *   DEVICE_TOKEN        shared secret presented as Authorization: Bearer …
 *   TWILIO_ACCOUNT_SID
 *   TWILIO_AUTH_TOKEN
 *   TWILIO_FROM         E.164 sender owned by Twilio
 *
 * POST /sms  JSON { "to": "+1…", "body": "…" }
 * Header: Authorization: Bearer <DEVICE_TOKEN>
 */
const express = require("express");
const twilio = require("twilio");

const app = express();
app.use(express.json({ limit: "8kb" }));

const token = process.env.DEVICE_TOKEN || "";
const accountSid = process.env.TWILIO_ACCOUNT_SID || "";
const authToken = process.env.TWILIO_AUTH_TOKEN || "";
const from = process.env.TWILIO_FROM || "";

const client = twilio(accountSid, authToken);

app.post("/sms", async (req, res) => {
  const auth = req.headers.authorization || "";
  const bearer = auth.startsWith("Bearer ") ? auth.slice(7) : "";
  if (!token || bearer !== token) {
    return res.status(401).json({ ok: false, error: "unauthorized" });
  }
  const { to, body } = req.body || {};
  if (!to || !body) {
    return res.status(400).json({ ok: false, error: "to and body required" });
  }
  if (!accountSid || !authToken || !from) {
    return res.status(500).json({ ok: false, error: "Twilio env not configured" });
  }
  try {
    const msg = await client.messages.create({ to, from, body });
    return res.json({ ok: true, sid: msg.sid });
  } catch (e) {
    return res.status(502).json({ ok: false, error: String(e.message || e) });
  }
});

const port = Number(process.env.PORT || 8787);
app.listen(port, () => {
  // eslint-disable-next-line no-console
  console.log(`med-alert proxy listening on :${port}`);
});
