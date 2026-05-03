#include "WiFiManager.h"
#include <Arduino.h>

WiFiManager::WiFiManager(const char *ssid, const char *password)
  : _ssid(ssid), _password(password) {}

void WiFiManager::connect(unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(_ssid, _password);
  Serial.printf("Connecting to WiFi SSID '%s'", _ssid);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start >= timeoutMs) {
      Serial.println("\nWiFi connect timeout. Restarting...");
      ESP.restart();
    }
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP: " + localIP());
  Serial.println("MAC: " + macAddress());
}

bool WiFiManager::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void WiFiManager::reconnect() {
  Serial.println("WiFi disconnected. Reconnecting...");
  connect();
}

String WiFiManager::localIP() {
  return WiFi.localIP().toString();
}

String WiFiManager::macAddress() {
  return WiFi.macAddress();
}

int WiFiManager::rssi() {
  return WiFi.RSSI();
}