#include <Arduino.h>
#include <WiFi.h>
#include "device_api.h"
#include "provisioning.h"

namespace {
bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(getWifiSsid(), getWifiPass());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("ESP32 API: http://%s:80\n", WiFi.localIP().toString().c_str());
  return true;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  loadParams();
  checkProvisioningReset();
  if (!paramsLoaded() || !connectWifi()) runProvisioning();
  startDeviceApi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi lost; reconnecting");
    WiFi.reconnect();
    delay(5000);
  } else {
    delay(1000);
  }
}
