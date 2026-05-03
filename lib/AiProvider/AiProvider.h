#pragma once

#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// ---------------------------------------------------------------------------
// AI provider configuration
// ---------------------------------------------------------------------------
// All OpenAI-compatible chat completion providers share the same JSON schema:
//
//   POST {baseUrl}/v1/chat/completions
//   Authorization: Bearer {apiKey}
//   Body: { model, messages: [...], max_tokens, temperature }
//
// NVIDIA NIM (cloud): baseUrl="https://integrate.api.nvidia.com",
//   apiKey="nvapi-...", model="meta/llama-3.1-405b-instruct"
//
// OpenRouter: baseUrl="https://openrouter.ai/api",
//   apiKey="sk-or-v1-...", model="openai/gpt-4o-mini"
// ---------------------------------------------------------------------------

struct AiProviderConfig {
  const char *baseUrl;    // e.g. "https://integrate.api.nvidia.com"
  const char *apiKey;     // API key / bearer token
  const char *model;      // model identifier
};

// Maximum conversation turns to keep in memory (user+assistant = 1 turn).
// 3 turns = 6 messages stored. Each message is a ~200 byte String on average.
// Total: ~1.2 KB — safe on 320 KB RAM ESP32.
#define AI_MAX_HISTORY_TURNS 3

class AiProvider {
public:
  AiProvider(const AiProviderConfig &config);
  void begin();

  // Send a message and get a reply. History is automatically tracked.
  String call(const String &userText, unsigned long timeoutMs = 65000);

  // System prompt can be overridden at runtime
  void setSystemPrompt(const String &prompt);

  // Clear conversation history (e.g. on /start or error)
  void clearHistory();

  // Set max turns. Higher = more context but more memory + tokens.
  void setMaxHistoryTurns(int turns);

private:
  AiProviderConfig _config;
  WiFiClientSecure _client;
  String _systemPrompt;

  // Rolling conversation history: alternating "user" / "assistant" role strings and content Strings.
  // Stored as parallel arrays to avoid heap-fragmenting struct allocations.
  String _historyRoles[AI_MAX_HISTORY_TURNS * 2];
  String _historyContent[AI_MAX_HISTORY_TURNS * 2];
  int _historyCount;       // number of stored messages
  int _maxHistoryTurns;    // max user+assistant pairs

  String _jsonEscape(const String &s);
  String _extractContent(const String &jsonResponse, int httpCode, int responseBytes);
  void _trimHistory();
};