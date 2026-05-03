#include "AiProvider.h"
#include <Arduino.h>
#include <HTTPClient.h>

AiProvider::AiProvider(const AiProviderConfig &config)
  : _config(config), _historyCount(0), _maxHistoryTurns(AI_MAX_HISTORY_TURNS) {
  _systemPrompt =
    "You are a concise AI agent connected to an ESP32 DevKit. "
    "Reply with the final answer only. Do not show hidden reasoning. "
    "Keep replies under 60 words unless the user asks for detail. "
    "The ESP32 can respond on Telegram and has commands: /status, /led on, /led off, /led toggle. "
    "If the user wants hardware action, tell them the exact command to send.";
}

void AiProvider::begin() {
  _client.setInsecure();
}

void AiProvider::setSystemPrompt(const String &prompt) {
  _systemPrompt = prompt;
}

void AiProvider::clearHistory() {
  _historyCount = 0;
}

void AiProvider::setMaxHistoryTurns(int turns) {
  if (turns < 0) turns = 0;
  if (turns > AI_MAX_HISTORY_TURNS) turns = AI_MAX_HISTORY_TURNS;
  _maxHistoryTurns = turns;
  _trimHistory();
}

void AiProvider::_trimHistory() {
  int maxMessages = _maxHistoryTurns * 2; // each turn = user + assistant
  if (_historyCount > maxMessages) {
    int remove = _historyCount - maxMessages;
    // Shift remaining messages to the front
    for (int i = 0; i < maxMessages; i++) {
      _historyRoles[i] = _historyRoles[i + remove];
      _historyContent[i] = _historyContent[i + remove];
    }
    _historyCount = maxMessages;
  }
}

String AiProvider::_jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:   out += c; break;
    }
  }
  return out;
}

String AiProvider::_extractContent(const String &trimmed, int httpCode, int responseBytes) {
  if (trimmed.length() == 0) {
    return "AI provider returned only whitespace/incomplete data. Try again with a short prompt.";
  }

  if (!trimmed.startsWith("{") && !trimmed.startsWith("[")) {
    String preview = trimmed.substring(0, min(120, (int)trimmed.length()));
    return "AI provider returned non-JSON data (HTTP " + String(httpCode) + ", bytes " + String(responseBytes) + "): " + preview;
  }

  DynamicJsonDocument doc(32768);
  DeserializationError err = deserializeJson(doc, trimmed);
  if (err) {
    String preview = trimmed.substring(0, min(120, (int)trimmed.length()));
    return "Failed to parse AI response: " + String(err.c_str()) + " (HTTP " + String(httpCode) + ", bytes " + String(responseBytes) + "). Preview: " + preview;
  }

  // Standard OpenAI-compatible: choices[0].message.content
  const char *content = doc["choices"][0]["message"]["content"];
  if (content && String(content).length() > 0) return String(content);

  // Some providers return "reasoning" in a separate field (e.g. DeepSeek R1)
  const char *reasoning = doc["choices"][0]["message"]["reasoning"];
  if (reasoning && String(reasoning).length() > 0) {
    return String("Model returned reasoning only, no final answer yet:\n") + reasoning;
  }

  return "AI provider returned no message content.";
}

String AiProvider::call(const String &userText, unsigned long timeoutMs) {
  // Build endpoint URL: {baseUrl}/v1/chat/completions
  String endpoint = String(_config.baseUrl) + "/v1/chat/completions";

  HTTPClient http;
  http.begin(_client, endpoint);
  http.setTimeout(timeoutMs);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + _config.apiKey);

  // Optional headers for OpenRouter
  if (String(_config.baseUrl).indexOf("openrouter.ai") >= 0) {
    http.addHeader("HTTP-Referer", "https://local-esp32-agent");
    http.addHeader("X-Title", "ESP32 AI Agent");
  }

  // Build request body (OpenAI-compatible) with conversation history
  // Estimate needed size: system prompt + history + new user message + overhead
  int estimatedSize = 2048 + (_historyCount * 256);
  DynamicJsonDocument req(estimatedSize);

  req["model"] = _config.model;
  req["max_tokens"] = 900;
  req["temperature"] = 0.3;

  JsonArray messages = req.createNestedArray("messages");

  // 1. System prompt
  JsonObject sys = messages.createNestedObject();
  sys["role"] = "system";
  sys["content"] = _systemPrompt;

  // 2. Conversation history (oldest first)
  for (int i = 0; i < _historyCount; i++) {
    JsonObject hist = messages.createNestedObject();
    hist["role"] = _historyRoles[i];
    hist["content"] = _historyContent[i];
  }

  // 3. Current user message
  JsonObject user = messages.createNestedObject();
  user["role"] = "user";
  user["content"] = userText;

  String body;
  serializeJson(req, body);

  int code = http.POST(body);
  String response = http.getString();
  http.end();

  int responseBytes = response.length();
  Serial.printf("AI [%s] HTTP %d, response bytes=%d, history=%d messages\n",
                _config.baseUrl, code, responseBytes, _historyCount);

  if (code < 200 || code >= 300) {
    // Don't store failed calls in history
    return "AI provider error HTTP " + String(code) + ": " + response;
  }

  String trimmed = response;
  trimmed.trim();

  String result = _extractContent(trimmed, code, responseBytes);

  // Store this exchange in history (if the call succeeded and returned content)
  if (result.length() > 0 &&
      !result.startsWith("AI provider") &&
      !result.startsWith("Failed to parse") &&
      !result.startsWith("Model returned reasoning only")) {

    // Trim old history before adding new entries
    _trimHistory();

    int maxMessages = _maxHistoryTurns * 2;
    if (_historyCount < maxMessages) {
      _historyRoles[_historyCount] = "user";
      _historyContent[_historyCount] = userText;
      _historyCount++;

      _historyRoles[_historyCount] = "assistant";
      _historyContent[_historyCount] = result;
      _historyCount++;
    }
  }

  return result;
}