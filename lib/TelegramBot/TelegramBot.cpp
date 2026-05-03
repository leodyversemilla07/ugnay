#include "TelegramBot.h"
#include <Arduino.h>
#include <HTTPClient.h>

TelegramBot::TelegramBot(const char *token, const char *allowedChatId, int ledPin)
  : _token(token), _allowedChatId(allowedChatId), _ledPin(ledPin),
    _offset(0), _lastPollMs(0), _pollIntervalMs(2500), _handler(nullptr) {
  pinMode(_ledPin, OUTPUT);
  digitalWrite(_ledPin, LOW);
}

void TelegramBot::begin() {
  _client.setInsecure();
}

String TelegramBot::_baseUrl() {
  return String("https://api.telegram.org/bot") + _token;
}

bool TelegramBot::_isAuthorized(const String &chatId) {
  return String(_allowedChatId).length() == 0 || chatId == String(_allowedChatId);
}

void TelegramBot::onMessage(MessageHandler handler) {
  _handler = handler;
}

long TelegramBot::getOffset() {
  return _offset;
}

void TelegramBot::sendMessage(const String &chatId, const String &text) {
  if (!_client.connected() && !_client.connect("api.telegram.org", 443)) return;

  for (int start = 0; start < text.length(); start += 3500) {
    String chunk = text.substring(start, min(start + 3500, (int)text.length()));
    HTTPClient http;
    String url = _baseUrl() + "/sendMessage";
    http.begin(_client, url);
    http.addHeader("Content-Type", "application/json");

    // Escape special characters for JSON
    String escaped;
    escaped.reserve(chunk.length() + 16);
    for (size_t i = 0; i < chunk.length(); i++) {
      char c = chunk[i];
      switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
      }
    }

    String body = "{\"chat_id\":\"" + chatId + "\",\"text\":\"" + escaped + "\"}";
    int code = http.POST(body);
    if (code < 200 || code >= 300) {
      Serial.printf("Telegram send failed: HTTP %d\n", code);
      Serial.println(http.getString());
    }
    http.end();
    delay(200);
  }
}

void TelegramBot::sendThinking(const String &chatId) {
  sendMessage(chatId, "Thinking...");
}

void TelegramBot::poll() {
  unsigned long now = millis();
  if (now - _lastPollMs < _pollIntervalMs) return;
  _lastPollMs = now;

  HTTPClient http;
  String url = _baseUrl() + "/getUpdates?offset=" + String(_offset) + "&timeout=0";
  http.begin(_client, url);
  int code = http.GET();
  String response = http.getString();
  http.end();

  if (code != 200) {
    Serial.printf("Telegram poll failed: HTTP %d\n", code);
    Serial.println(response);
    return;
  }

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.printf("Telegram JSON parse failed: %s\n", err.c_str());
    return;
  }

  JsonArray results = doc["result"].as<JsonArray>();
  for (JsonObject update : results) {
    _handleUpdate(update);
  }
}

void TelegramBot::_handleUpdate(JsonObject update) {
  long updateId = update["update_id"] | 0;
  if (updateId >= _offset) _offset = updateId + 1;

  JsonObject msg = update["message"];
  if (msg.isNull()) return;

  String chatId;
  serializeJson(msg["chat"]["id"], chatId);
  String text = msg["text"] | "";
  String from = msg["from"]["username"] | "unknown";

  if (text.length() == 0) return;

  Serial.printf("Telegram from @%s chat_id=%s: %s\n", from.c_str(), chatId.c_str(), text.c_str());

  if (!_isAuthorized(chatId)) {
    sendMessage(chatId, "Unauthorized chat. Your chat_id is: " + chatId);
    return;
  }

  if (_handler) {
    _handler(chatId, text, from);
  }
}