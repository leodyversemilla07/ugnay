#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ---------------------------------------------------------------------------
// AiProvider — OpenAI-compatible chat completion client for ESP32.
//
// Supports:
//   - Conversation history (rolling window, configurable turns)
//   - System prompt customization
//   - OpenAI, OpenRouter, NVIDIA NIM, and any compatible endpoint
//   - Tool calling: callRaw() returns raw JSON for tool_call detection,
//     callWithToolResults() sends tool results back for follow-up
// ---------------------------------------------------------------------------

struct AiProviderConfig {
    const char *baseUrl;
    const char *apiKey;
    const char *model;
};

// How many user/assistant turn pairs to keep in conversation history.
// Total: ~1.2 KB — safe on 320 KB RAM ESP32.
#define AI_MAX_HISTORY_TURNS 3

class AiProvider {
public:
    AiProvider(const AiProviderConfig &config);
    void begin();

    // Send a message and get a text reply. History is automatically tracked.
    // Returns the assistant's text content (no JSON wrapper).
    String call(const String &userText, unsigned long timeoutMs = 65000);

    // Send a message and get the RAW JSON response from the API.
    // Use this for tool calling — check for tool_calls in the response.
    // History is NOT updated automatically (call updateHistory() after).
    String callRaw(const String &userText, const String &toolsJson = "",
                   unsigned long timeoutMs = 65000);

    // Send tool results back to the LLM for a follow-up response.
    // toolResultsJson is a JSON array of tool result messages.
    // Returns the raw JSON response from the API.
    String callWithToolResults(const String &toolResultsJson,
                               unsigned long timeoutMs = 65000);

    // Manually update conversation history after a successful exchange
    void updateHistory(const String &userText, const String &assistantContent);

    // System prompt can be overridden at runtime
    void setSystemPrompt(const String &prompt);

    // Clear conversation history (e.g. on /start or error)
    void clearHistory();

    // Set max turns. Higher = more context but more memory + tokens.
    void setMaxHistoryTurns(int turns);

    // Get the current system prompt
    String getSystemPrompt() const { return _systemPrompt; }

private:
    AiProviderConfig _config;
    WiFiClientSecure _client;
    String _systemPrompt;

    // Rolling conversation history
    String _historyRoles[AI_MAX_HISTORY_TURNS * 2];
    String _historyContent[AI_MAX_HISTORY_TURNS * 2];
    int _historyCount;
    int _maxHistoryTurns;

    // Pending assistant message with tool_calls (stored between callRaw and callWithToolResults)
    String _pendingAssistantJson;

    String _jsonEscape(const String &s);
    String _extractContent(const String &jsonResponse, int httpCode, int responseBytes);
    void _trimHistory();

    // Build the messages array for a request
    String _buildMessagesJson(const String &userText);
};
