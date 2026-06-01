#include "twilio_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static String urlEncodeFormValue(const String& s) {
  String out;
  out.reserve(s.length() * 3);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); ++i) {
    const unsigned char c = (unsigned char)s[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else if (c == ' ') {
      out += '+';
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

bool twilioSendSms(const TwilioCredentials& cred, const String& to_e164, const String& body,
                   String& err_out) {
  err_out = "";
  if (cred.account_sid.length() == 0 || cred.auth_token.length() == 0 ||
      cred.from_e164.length() == 0) {
    err_out = "Twilio credentials incomplete";
    return false;
  }

  String url = "https://api.twilio.com/2010-04-01/Accounts/";
  url += cred.account_sid;
  url += "/Messages.json";

  String post = "To=";
  post += urlEncodeFormValue(to_e164);
  post += "&From=";
  post += urlEncodeFormValue(cred.from_e164);
  post += "&Body=";
  post += urlEncodeFormValue(body);

  WiFiClientSecure client;
  client.setInsecure();  // Prototype: pin Twilio root CA for production

  HTTPClient https;
  https.setTimeout(20000);
  if (!https.begin(client, url)) {
    err_out = "https.begin failed";
    return false;
  }

  https.setAuthorization(cred.account_sid.c_str(), cred.auth_token.c_str());
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");

  const int code = https.POST(post);
  const String resp = https.getString();
  https.end();

  if (code < 200 || code >= 300) {
    err_out = "HTTP ";
    err_out += String(code);
    err_out += " ";
    err_out += resp;
    return false;
  }

  (void)resp;
  return true;
}
