#pragma once

#include <WiFi.h>

class WiFiManager {
public:
  WiFiManager(const char *ssid, const char *password);
  void connect(unsigned long timeoutMs = 30000);
  bool isConnected();
  void reconnect();
  String localIP();
  String macAddress();
  int rssi();

private:
  const char *_ssid;
  const char *_password;
};