#pragma once

#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <freertos/semphr.h>
#include <functional>

class TelegramBot {
public:
  TelegramBot(const char *token, const char *allowedChatId, int ledPin);
  void begin();
  void poll();
  void sendMessage(const String &chatId, const String &text);
  void sendThinking(const String &chatId);
  long getOffset();

  // Called when a text message is received
  using MessageHandler = std::function<void(const String &chatId, const String &text, const String &from)>;
  void onMessage(MessageHandler handler);

private:
  const char *_token;
  const char *_allowedChatId;
  int _ledPin;
  long _offset;
  unsigned long _lastPollMs;
  unsigned long _pollIntervalMs;
  SemaphoreHandle_t _networkMutex;
  MessageHandler _handler;

  String _baseUrl();
  bool _lockNetwork(unsigned long timeoutMs = 10000);
  void _unlockNetwork();
  void _handleUpdate(JsonObject update);
  bool _isAuthorized(const String &chatId);
};