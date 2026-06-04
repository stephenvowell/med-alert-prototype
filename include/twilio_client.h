#pragma once

#include <Arduino.h>

/** Account SID + auth token + E.164 sender owned by your Twilio project. */
struct TwilioCredentials {
  String account_sid;
  String auth_token;
  String from_e164;
};

/// Sends SMS via Twilio REST. Returns true on HTTP 2xx.
bool twilioSendSms(const TwilioCredentials& cred, const String& to_e164, const String& body,
                   String& err_out);
