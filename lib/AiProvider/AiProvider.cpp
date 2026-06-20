#include "AiProvider.h"
#include <Arduino.h>
#include <HTTPClient.h>

AiProvider::AiProvider(const AiProviderConfig &config)
    : _config(config), _historyCount(0), _maxHistoryTurns(AI_MAX_HISTORY_TURNS) {
    _systemPrompt =
        "You are a concise AI agent connected to an ESP32 DevKit. "
        "Reply with the final answer only. Do not show hidden reasoning. "
        "Keep replies under 60 words unless the user asks for detail. "
        "For greetings like hello or hi, greet the user naturally and say you are online. "
        "The ESP32 handles LED control directly when the user asks to turn the LED/light on, off, or toggle it. "
        "Available local commands: /status, /led on|off|toggle|test, /remember, /forget, /memory, /skill, /cron, /personality, /context.";
}

void AiProvider::begin() {
    _client.setInsecure();
}

void AiProvider::setSystemPrompt(const String &prompt) {
    _systemPrompt = prompt;
}

void AiProvider::clearHistory() {
    for (int i = 0; i < AI_MAX_HISTORY_TURNS * 2; i++) {
        _historyRoles[i] = "";
        _historyContent[i] = "";
    }
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
    if (maxMessages <= 0) {
        clearHistory();
        return;
    }

    if (_historyCount > maxMessages) {
        int remove = _historyCount - maxMessages;
        for (int i = 0; i < maxMessages; i++) {
            _historyRoles[i] = _historyRoles[i + remove];
            _historyContent[i] = _historyContent[i + remove];
        }
        for (int i = maxMessages; i < AI_MAX_HISTORY_TURNS * 2; i++) {
            _historyRoles[i] = "";
            _historyContent[i] = "";
        }
        _historyCount = maxMessages;
    }
}

void AiProvider::updateHistory(const String &userText, const String &assistantContent) {
    int maxMessages = _maxHistoryTurns * 2;
    if (maxMessages <= 0) return;

    // Make room for the new user+assistant exchange before appending.
    while (_historyCount + 2 > maxMessages) {
        for (int i = 0; i < _historyCount - 1; i++) {
            _historyRoles[i] = _historyRoles[i + 1];
            _historyContent[i] = _historyContent[i + 1];
        }
        _historyCount--;
        _historyRoles[_historyCount] = "";
        _historyContent[_historyCount] = "";
    }

    if (_historyCount < maxMessages) {
        _historyRoles[_historyCount] = "user";
        _historyContent[_historyCount] = userText;
        _historyCount++;

        _historyRoles[_historyCount] = "assistant";
        _historyContent[_historyCount] = assistantContent;
        _historyCount++;
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
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

String AiProvider::_buildMessagesJson(const String &userText) {
    String messages = "[";

    // 1. System prompt
    messages += "{\"role\":\"system\",\"content\":\"" + _jsonEscape(_systemPrompt) + "\"}";

    // 2. Conversation history (oldest first)
    for (int i = 0; i < _historyCount; i++) {
        messages += ",{\"role\":\"" + _historyRoles[i] + "\",\"content\":\"" + _jsonEscape(_historyContent[i]) + "\"}";
    }

    // 3. Current user message
    messages += ",{\"role\":\"user\",\"content\":\"" + _jsonEscape(userText) + "\"}";

    messages += "]";
    return messages;
}

String AiProvider::_extractContent(const String &trimmed, int httpCode, int responseBytes) {
    if (trimmed.length() == 0) {
        return "AI provider returned only whitespace/incomplete data. Try again with a short prompt.";
    }

    if (!trimmed.startsWith("{") && !trimmed.startsWith("[")) {
        String preview = trimmed.substring(0, min(120, (int)trimmed.length()));
        return "AI provider returned non-JSON data (HTTP " + String(httpCode) + ", bytes " + String(responseBytes) + "): " + preview;
    }

    size_t parseCapacity = max((size_t)4096, (size_t)trimmed.length() * 2 + 1024);
    if (parseCapacity > 32768) parseCapacity = 32768;

    DynamicJsonDocument doc(parseCapacity);
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
    String endpoint = String(_config.baseUrl) + "/v1/chat/completions";

    HTTPClient http;
    if (!http.begin(_client, endpoint)) {
        return "AI provider error: HTTP begin failed.";
    }
    http.setTimeout(timeoutMs);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + _config.apiKey);

    if (String(_config.baseUrl).indexOf("openrouter.ai") >= 0) {
        http.addHeader("HTTP-Referer", "https://local-esp32-agent");
        http.addHeader("X-Title", "ESP32 AI Agent");
    }

    // Build request body
    String messagesJson = _buildMessagesJson(userText);
    String body = "{\"model\":\"" + String(_config.model) + "\","
                  "\"max_tokens\":500,"
                  "\"temperature\":0.3,"
                  "\"messages\":" + messagesJson + "}";

    int code = http.POST(body);
    String response = http.getString();
    http.end();

    int responseBytes = response.length();
    Serial.printf("AI [%s] HTTP %d, response bytes=%d, history=%d messages\n",
        _config.baseUrl, code, responseBytes, _historyCount);

    if (code < 200 || code >= 300) {
        return "AI provider error HTTP " + String(code) + ": " + response;
    }

    String trimmed = response;
    trimmed.trim();

    String result = _extractContent(trimmed, code, responseBytes);

    // Store this exchange in history
    if (result.length() > 0 &&
        !result.startsWith("AI provider") &&
        !result.startsWith("Failed to parse") &&
        !result.startsWith("Model returned reasoning only")) {
        updateHistory(userText, result);
    }

    return result;
}

String AiProvider::callRaw(const String &userText, const String &toolsJson,
                           unsigned long timeoutMs) {
    String endpoint = String(_config.baseUrl) + "/v1/chat/completions";

    HTTPClient http;
    if (!http.begin(_client, endpoint)) {
        return "{\"error\": \"HTTP begin failed\"}";
    }
    http.setTimeout(timeoutMs);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + _config.apiKey);

    if (String(_config.baseUrl).indexOf("openrouter.ai") >= 0) {
        http.addHeader("HTTP-Referer", "https://local-esp32-agent");
        http.addHeader("X-Title", "ESP32 AI Agent");
    }

    // Build request body with optional tools
    String messagesJson = _buildMessagesJson(userText);
    String body = "{\"model\":\"" + String(_config.model) + "\","
                  "\"max_tokens\":500,"
                  "\"temperature\":0.3,"
                  "\"messages\":" + messagesJson;

    if (toolsJson.length() > 0) {
        body += ",\"tools\":" + toolsJson;
    }

    body += "}";

    int code = http.POST(body);
    String response = http.getString();
    http.end();

    Serial.printf("AI [raw] [%s] HTTP %d, bytes=%d\n",
        _config.baseUrl, code, response.length());

    if (code < 200 || code >= 300) {
        return "{\"error\": \"HTTP " + String(code) + "\", \"detail\": \"" + _jsonEscape(response.substring(0, 200)) + "\"}";
    }

    // Store a sanitized assistant message (may include tool_calls) for follow-up.
    // Some providers reject extra nullable fields from the raw response message.
    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, response);
    if (err == DeserializationError::Ok &&
        doc.containsKey("choices") &&
        doc["choices"][0].containsKey("message")) {
        JsonObject msg = doc["choices"][0]["message"];

        DynamicJsonDocument cleanDoc(12288);
        JsonObject clean = cleanDoc.to<JsonObject>();
        clean["role"] = msg["role"] | "assistant";

        if (msg.containsKey("content") && !msg["content"].isNull()) {
            clean["content"] = msg["content"].as<String>();
        } else {
            // Tool-call assistant messages may have null content. Keep it explicit.
            clean["content"] = "";
        }

        if (msg.containsKey("tool_calls") && !msg["tool_calls"].isNull()) {
            clean["tool_calls"] = msg["tool_calls"];
        }

        if (msg.containsKey("name") && !msg["name"].isNull()) {
            clean["name"] = msg["name"].as<String>();
        }

        String assistantJson;
        serializeJson(clean, assistantJson);
        _pendingAssistantJson = assistantJson;
    } else {
        _pendingAssistantJson = "";
    }

    return response;
}

String AiProvider::callWithToolResults(const String &toolResultsJson,
                                       unsigned long timeoutMs) {
    String endpoint = String(_config.baseUrl) + "/v1/chat/completions";

    HTTPClient http;
    if (!http.begin(_client, endpoint)) {
        return "AI provider error on tool follow-up: HTTP begin failed.";
    }
    http.setTimeout(timeoutMs);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + _config.apiKey);

    if (String(_config.baseUrl).indexOf("openrouter.ai") >= 0) {
        http.addHeader("HTTP-Referer", "https://local-esp32-agent");
        http.addHeader("X-Title", "ESP32 AI Agent");
    }

    // Build messages: system + history + pending assistant (with tool_calls) + tool results
    String messages = "[";

    // 1. System prompt
    messages += "{\"role\":\"system\",\"content\":\"" + _jsonEscape(_systemPrompt) + "\"}";

    // 2. Conversation history
    for (int i = 0; i < _historyCount; i++) {
        messages += ",{\"role\":\"" + _historyRoles[i] + "\",\"content\":\"" + _jsonEscape(_historyContent[i]) + "\"}";
    }

    // 3. Pending assistant message (the one that requested tool calls)
    if (_pendingAssistantJson.length() > 0) {
        messages += "," + _pendingAssistantJson;
    }

    // 4. Tool result messages.
    // AiTask passes an array string like: [{...},{...}].
    // messages must be a flat array of message objects, not a nested array.
    String toolMsgs = toolResultsJson;
    toolMsgs.trim();
    if (toolMsgs.startsWith("[") && toolMsgs.endsWith("]") && toolMsgs.length() >= 2) {
        toolMsgs = toolMsgs.substring(1, toolMsgs.length() - 1);
        toolMsgs.trim();
    }
    if (toolMsgs.length() > 0) {
        messages += "," + toolMsgs;
    }

    messages += "]";

    String body = "{\"model\":\"" + String(_config.model) + "\","
                  "\"max_tokens\":900,"
                  "\"temperature\":0.3,"
                  "\"messages\":" + messages + "}";

    int code = http.POST(body);
    String response = http.getString();
    http.end();

    Serial.printf("AI [tool-results] [%s] HTTP %d, bytes=%d\n",
        _config.baseUrl, code, response.length());

    if (code < 200 || code >= 300) {
        _pendingAssistantJson = "";
        String detail = response.substring(0, 180);
        detail.replace("\n", " ");
        return "AI provider error on tool follow-up HTTP " + String(code) + ": " + detail;
    }

    String trimmed = response;
    trimmed.trim();

    // Extract the content from the follow-up response
    String result = _extractContent(trimmed, code, response.length());

    // Clear the pending assistant message
    _pendingAssistantJson = "";

    return result;
}