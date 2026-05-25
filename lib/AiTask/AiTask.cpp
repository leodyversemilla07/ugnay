#include "AiTask.h"

AiTask::AiTask(AiProvider &ai, TelegramBot &bot)
    : _ai(ai), _bot(bot), _tools(nullptr), _taskHandle(nullptr) {
    _queue = xQueueCreate(QUEUE_SIZE, sizeof(AiTaskMessage *));
}

AiTask::~AiTask() {
    if (_taskHandle) {
        vTaskDelete(_taskHandle);
    }
    if (_queue) {
        vQueueDelete(_queue);
    }
}

void AiTask::begin() {
    xTaskCreatePinnedToCore(
        _taskFunc,     // task function
        "AiTask",      // name
        12288,         // stack size (bytes) — increased for JSON + tool processing
        this,          // parameter
        1,             // priority
        &_taskHandle,  // task handle
        1              // core 1 (core 0 = WiFi/Telegram)
    );
}

void AiTask::setToolDispatcher(ToolDispatcher &dispatcher) {
    _tools = &dispatcher;
}

bool AiTask::enqueue(const String &chatId, const String &text) {
    if (!_queue) return false;

    AiTaskMessage *msg = new AiTaskMessage();
    msg->chatId = chatId;
    msg->text = text;

    if (xQueueSend(_queue, &msg, 0) != pdTRUE) {
        delete msg;
        return false;
    }
    return true;
}

UBaseType_t AiTask::pending() {
    return _queue ? uxQueueMessagesWaiting(_queue) : 0;
}

void AiTask::_taskFunc(void *arg) {
    AiTask *self = static_cast<AiTask *>(arg);
    self->_run();
}

String AiTask::_processWithTools(const String &userText) {
    // Get the tools definition if we have a dispatcher
    String toolsJson = "";
    if (_tools) {
        toolsJson = _tools->getToolsDefinition();
    }

    // First call to the AI (raw, so we can detect tool_calls)
    String rawResponse = _ai.callRaw(userText, toolsJson);

    // If no tool dispatcher, extract content from raw response
    if (!_tools) {
        // Fall back to extracting content
        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, rawResponse) == DeserializationError::Ok &&
            doc.containsKey("choices")) {
            return doc["choices"][0]["message"]["content"] | rawResponse;
        }
        return rawResponse;
    }

    // Tool call loop: up to MAX_TOOL_ROUNDS
    for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
        DynamicJsonDocument doc(16384);
        DeserializationError err = deserializeJson(doc, rawResponse);

        if (err != DeserializationError::Ok) {
            // Not valid JSON — return as-is
            return rawResponse;
        }

        // Check if the response contains tool_calls
        if (err == DeserializationError::Ok &&
            doc.containsKey("choices") &&
            doc["choices"][0].containsKey("message") &&
            doc["choices"][0]["message"].containsKey("tool_calls") &&
            doc["choices"][0]["message"]["tool_calls"].size() > 0) {

            JsonArray toolCalls = doc["choices"][0]["message"]["tool_calls"].as<JsonArray>();
            Serial.printf("[AiTask] Tool round %d: %d tool(s)\n", round + 1, toolCalls.size());

            // Execute each tool and collect results as JSON array
            String toolResultsJson = "[";
            bool first = true;

            for (JsonObject toolCall : toolCalls) {
                DynamicJsonDocument result = _tools->executeTool(toolCall);

                if (!first) toolResultsJson += ",";
                first = false;

                String resultStr;
                serializeJson(result, resultStr);
                toolResultsJson += resultStr;
            }
            toolResultsJson += "]";

            Serial.printf("[AiTask] Tool results: %s\n", toolResultsJson.substring(0, 120).c_str());

            // Send tool results back for follow-up
            rawResponse = _ai.callWithToolResults(toolResultsJson);

            // Next iteration will check if the follow-up also has tool_calls
            continue;
        }

        // No tool calls — extract the text content and return
        if (doc.containsKey("choices") && doc["choices"][0].containsKey("message")) {
            String content = doc["choices"][0]["message"]["content"] | "";
            if (content.length() > 0) {
                // Update conversation history
                _ai.updateHistory(userText, content);
                return content;
            }
        }

        // Fallback
        return rawResponse;
    }

    // After max tool rounds, extract final content
    DynamicJsonDocument finalDoc(16384);
    if (deserializeJson(finalDoc, rawResponse) == DeserializationError::Ok &&
        finalDoc.containsKey("choices")) {
        String content = finalDoc["choices"][0]["message"]["content"] | rawResponse;
        _ai.updateHistory(userText, content);
        return content;
    }

    return rawResponse;
}

void AiTask::_run() {
    AiTaskMessage *msg = nullptr;

    while (true) {
        if (xQueueReceive(_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (!msg) continue;

            _bot.sendThinking(msg->chatId);

            Serial.printf("[AiTask] Processing: %s\n", msg->text.c_str());

            String response = _processWithTools(msg->text);

            Serial.printf("[AiTask] Reply (%d bytes): %s\n",
                response.length(),
                response.substring(0, 80).c_str());

            _bot.sendMessage(msg->chatId, response);

            delete msg;
            msg = nullptr;
        }
    }
}
