#include "device_api.h"
#include "provisioning.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {
WebServer server(80);
String deviceName = "My ESP32";
uint32_t sampleIntervalSeconds = 30;
uint32_t sampleCount = 0;
uint32_t lastSampleMs = 0;
bool updateActive = false;
bool chunkFailed = false;
uint32_t expectedBytes = 0;
uint32_t receivedBytes = 0;
uint32_t lastUpdateMs = 0;

bool authorized() {
  const String supplied = server.header("X-Device-Key");
  const char* expected = getAdminKey();
  const size_t expectedLen = strlen(expected);
  if (supplied.length() != expectedLen || expectedLen < 8) return false;
  uint8_t difference = 0;
  for (size_t i = 0; i < expectedLen; ++i) {
    difference |= static_cast<uint8_t>(supplied[i] ^ expected[i]);
  }
  return difference == 0;
}

void sendJson(int code, const JsonDocument& doc) {
  String output;
  serializeJson(doc, output);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", output);
}

void sendError(int code, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  sendJson(code, doc);
}

bool requireAuth() {
  if (authorized()) return true;
  sendError(401, "Incorrect device admin password");
  return false;
}

void loadSettings() {
  Preferences prefs;
  prefs.begin("demo-ui", true);
  deviceName = prefs.getString("name", "My ESP32");
  sampleIntervalSeconds = prefs.getUInt("interval", 30);
  prefs.end();
  if (sampleIntervalSeconds < 5 || sampleIntervalSeconds > 3600) sampleIntervalSeconds = 30;
}

void settingsJson(JsonDocument& doc) {
  doc["deviceName"] = deviceName;
  doc["sampleIntervalSeconds"] = sampleIntervalSeconds;
}

void handleStatus() {
  if (!requireAuth()) return;
  JsonDocument doc;
  doc["deviceName"] = deviceName;
  doc["firmwareVersion"] = "demo-1.0.0";
  doc["chip"] = ESP.getChipModel();
  doc["uptimeSeconds"] = millis() / 1000;
  doc["freeHeapBytes"] = ESP.getFreeHeap();
  doc["sampleCount"] = sampleCount;
  doc["localIp"] = WiFi.localIP().toString();
  doc["rssiDbm"] = WiFi.RSSI();
  sendJson(200, doc);
}

void handleGetSettings() {
  if (!requireAuth()) return;
  JsonDocument doc;
  settingsJson(doc);
  sendJson(200, doc);
}

void handleSaveSettings() {
  if (!requireAuth()) return;
  JsonDocument request;
  if (deserializeJson(request, server.arg("plain"))) {
    sendError(400, "Invalid JSON");
    return;
  }
  String name = request["deviceName"] | "";
  name.trim();
  const uint32_t interval = request["sampleIntervalSeconds"] | 0;
  if (name.isEmpty() || name.length() > 32 || interval < 5 || interval > 3600) {
    sendError(400, "Name must be 1–32 characters and interval 5–3600 seconds");
    return;
  }
  Preferences prefs;
  prefs.begin("demo-ui", false);
  prefs.putString("name", name);
  prefs.putUInt("interval", interval);
  prefs.end();
  deviceName = name;
  sampleIntervalSeconds = interval;
  JsonDocument response;
  settingsJson(response);
  sendJson(200, response);
}

void handleUpdateStart() {
  if (!requireAuth()) return;
  if (updateActive) {
    sendError(409, "An update is already in progress");
    return;
  }
  const String body = server.arg("plain");
  const uint32_t size = static_cast<uint32_t>(body.toInt());
  if (size == 0 || size > ESP.getFreeSketchSpace()) {
    sendError(400, "Firmware size is invalid or exceeds the free update partition");
    return;
  }
  if (!Update.begin(size, U_FLASH)) {
    sendError(500, "Could not begin firmware update");
    return;
  }
  expectedBytes = size;
  receivedBytes = 0;
  updateActive = true;
  lastUpdateMs = millis();
  JsonDocument response;
  response["ok"] = true;
  response["maxChunkBytes"] = 12288;
  sendJson(200, response);
}

void handleUpdateChunkUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    chunkFailed = !authorized() || !updateActive;
  } else if (upload.status == UPLOAD_FILE_WRITE && !chunkFailed) {
    if (receivedBytes + upload.currentSize > expectedBytes ||
        Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      chunkFailed = true;
      Update.abort();
      updateActive = false;
    } else {
      receivedBytes += upload.currentSize;
      lastUpdateMs = millis();
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    chunkFailed = true;
    Update.abort();
    updateActive = false;
  }
}

void handleUpdateChunkDone() {
  if (!requireAuth()) return;
  if (chunkFailed || !updateActive) {
    sendError(400, "Firmware chunk failed");
    return;
  }
  JsonDocument response;
  response["receivedBytes"] = receivedBytes;
  sendJson(200, response);
}

void handleUpdateFinish() {
  if (!requireAuth()) return;
  if (!updateActive || receivedBytes != expectedBytes) {
    sendError(400, "Firmware upload is incomplete");
    return;
  }
  if (!Update.end(true)) {
    updateActive = false;
    sendError(500, "Firmware validation failed");
    return;
  }
  updateActive = false;
  JsonDocument response;
  response["ok"] = true;
  response["restarting"] = true;
  sendJson(200, response);
  delay(300);
  ESP.restart();
}

void apiTask(void*) {
  loadSettings();
  const char* headers[] = {"X-Device-Key"};
  server.collectHeaders(headers, 1);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handleSaveSettings);
  server.on("/api/update/start", HTTP_POST, handleUpdateStart);
  server.on("/api/update/chunk", HTTP_POST, handleUpdateChunkDone, handleUpdateChunkUpload);
  server.on("/api/update/finish", HTTP_POST, handleUpdateFinish);
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8",
      "<h1>ESP32 hosted UI demo</h1><p>The React UI is served by your clientproxy.io domain cache. "
      "Open that domain to use it.</p>");
  });
  server.onNotFound([]() { sendError(404, "Not found"); });
  server.begin();
  log_i("Device API listening on port 80");
  for (;;) {
    server.handleClient();
    const uint32_t now = millis();
    if (updateActive && now - lastUpdateMs > 60000UL) {
      Update.abort();
      updateActive = false;
    }
    if (now - lastSampleMs >= sampleIntervalSeconds * 1000UL) {
      ++sampleCount;
      lastSampleMs = now;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
}  // namespace

void startDeviceApi() {
  xTaskCreatePinnedToCore(apiTask, "deviceApi", 8192, nullptr, 1, nullptr, 0);
}
