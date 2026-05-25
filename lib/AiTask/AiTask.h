#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <ArduinoJson.h>

#include "AiProvider.h"
#include "TelegramBot.h"
#include "ToolDispatcher.h"

// ---------------------------------------------------------------------------
// AiTask — runs AI API calls on a dedicated FreeRTOS task (core 1)
//
// - Telegram callback enqueues messages via enqueue()
// - The task picks them up, calls ai.call(), and sends the reply via bot
// - Supports tool calling: if the AI responds with tool_calls, they are
//   executed by ToolDispatcher and results are sent back for a follow-up
// - Max tool call rounds: 3 (prevents infinite loops)
// - Telegram polling on core 0 never blocks
// ---------------------------------------------------------------------------

struct AiTaskMessage {
    String chatId;
    String text;
};

class AiTask {
public:
    AiTask(AiProvider &ai, TelegramBot &bot);
    ~AiTask();

    // Start the FreeRTOS task (call once in setup())
    void begin();

    // Set the tool dispatcher (must call before begin() or first message)
    void setToolDispatcher(ToolDispatcher &dispatcher);

    // Enqueue a message for AI processing (called from Telegram callback)
    // Returns false if the queue is full
    bool enqueue(const String &chatId, const String &text);

    // Returns the number of pending messages
    UBaseType_t pending();

private:
    AiProvider &_ai;
    TelegramBot &_bot;
    ToolDispatcher *_tools;
    TaskHandle_t _taskHandle;
    QueueHandle_t _queue;

    static const int QUEUE_SIZE = 3;
    static const int MAX_TOOL_ROUNDS = 3;

    // FreeRTOS task function (static trampoline)
    static void _taskFunc(void *arg);

    // Instance loop
    void _run();

    // Process a single user message, including tool call loops
    String _processWithTools(const String &userText);
};
