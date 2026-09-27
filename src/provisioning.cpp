#include "provisioning.h"
#include "config.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {
constexpr const char* NVS_NS = "hosted-ui";
char wifiSsid[64] = {};
char wifiPass[64] = {};
char adminKey[65] = {};
bool loaded = false;
bool restartRequested = false;
WebServer portal(80);

const char* PAGE = R"html(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>ESP32 setup</title>
<style>*{box-sizing:border-box}body{font-family:system-ui,sans-serif;background:#0d1b2a;color:#e9f5f6;min-height:100vh;display:grid;place-items:center;margin:0;padding:20px}.card{background:#183348;border:1px solid #315168;border-radius:16px;padding:28px;max-width:420px;width:100%}h1{font-size:24px;margin:0 0 8px}p{color:#a6c0ce;font-size:13px;line-height:1.5}label{display:block;font-size:13px;font-weight:700;margin:18px 0 7px}input{width:100%;padding:11px;border-radius:8px;border:1px solid #658295;background:#0d1b2a;color:white}button{margin-top:24px;padding:12px 16px;width:100%;background:#23aa91;color:white;border:0;border-radius:8px;font-weight:700}.error{color:#ffb8af}</style></head>
<body><div class="card"><h1>Connect your ESP32</h1><p>Enter your Wi-Fi and a unique device admin password. The ESP32 serves only a small API; the React page will live on the proxy.</p><p class="error">{ERROR}</p>
<form method="post" action="/"><label for="ssid">Wi-Fi SSID</label><input id="ssid" name="ssid" required maxlength="63" autocomplete="off"><label for="pass">Wi-Fi password</label><input id="pass" type="password" name="pass" maxlength="63"><label for="admin">Device admin password</label><input id="admin" type="password" name="admin" minlength="8" maxlength="64" required><button type="submit">Save and restart</button></form>
<p>The password protects status, settings, and firmware updates. It is separate from the clientproxy.io tunnel API key.</p></div></body></html>
)html";

String page(const String& error = "") {
  String html = PAGE;
  html.replace("{ERROR}", error);
  return html;
}

void clearParams() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.clear();
  prefs.end();
  loaded = false;
}
}  // namespace

const char* getWifiSsid() { return wifiSsid; }
const char* getWifiPass() { return wifiPass; }
const char* getAdminKey() { return adminKey; }

void loadParams() {
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  strlcpy(wifiSsid, prefs.getString("ssid", "").c_str(), sizeof(wifiSsid));
  strlcpy(wifiPass, prefs.getString("pass", "").c_str(), sizeof(wifiPass));
  strlcpy(adminKey, prefs.getString("admin", "").c_str(), sizeof(adminKey));
  prefs.end();
  loaded = wifiSsid[0] && strlen(adminKey) >= 8;
}

bool paramsLoaded() { return loaded; }

void checkProvisioningReset() {
  if (PROVISION_RESET_PIN < 0) return;
  pinMode(PROVISION_RESET_PIN, INPUT_PULLUP);
  if (digitalRead(PROVISION_RESET_PIN) != LOW) return;
  for (int i = 0; i < 30; ++i) {
    delay(100);
    if (digitalRead(PROVISION_RESET_PIN) != LOW) return;
  }
  clearParams();
  ESP.restart();
}

void runProvisioning() {
  char apSsid[32];
  snprintf(apSsid, sizeof(apSsid), "%s-%06lX", PROVISION_AP_PREFIX,
           static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFF));
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid);
  const IPAddress apIp = WiFi.softAPIP();
  Serial.printf("Setup AP: %s; open http://%s\n", apSsid, apIp.toString().c_str());

  portal.on("/", HTTP_GET, []() { portal.send(200, "text/html; charset=utf-8", page()); });
  portal.on("/", HTTP_POST, []() {
    String ssid = portal.arg("ssid");
    String pass = portal.arg("pass");
    String admin = portal.arg("admin");
    ssid.trim();
    if (ssid.isEmpty() || ssid.length() > 63 || pass.length() > 63 ||
        admin.length() < 8 || admin.length() > 64) {
      portal.send(400, "text/html; charset=utf-8", page("Check the field lengths and try again."));
      return;
    }
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.putString("admin", admin);
    prefs.end();
    portal.send(200, "text/html; charset=utf-8", "<h1>Saved. Restarting…</h1>");
    restartRequested = true;
  });
  portal.onNotFound([]() {
    portal.sendHeader("Location", "http://192.168.4.1/");
    portal.send(302, "text/plain", "");
  });
  DNSServer dns;
  dns.start(53, "*", apIp);
  portal.begin();
  while (!restartRequested) {
    dns.processNextRequest();
    portal.handleClient();
    delay(2);
  }
  delay(700);
  ESP.restart();
}
