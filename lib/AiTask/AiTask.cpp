#include "AiTask.h"

AiTask::AiTask(AiProvider &ai, TelegramBot &bot)
  : _ai(ai), _bot(bot), _taskHandle(nullptr) {
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
    _taskFunc,           // task function
    "AiTask",            // name
    8192,                // stack size (bytes)
    this,                // parameter
    1,                   // priority
    &_taskHandle,        // task handle
    1                    // core 1 (core 0 = WiFi/Telegram)
  );
}

bool AiTask::enqueue(const String &chatId, const String &text) {
  if (!_queue) return false;

  // Allocate on heap; the task will free it
  AiTaskMessage *msg = new AiTaskMessage();
  msg->chatId = chatId;
  msg->text = text;

  if (xQueueSend(_queue, &msg, 0) != pdTRUE) {
    delete msg;
    return false; // queue full
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

void AiTask::_run() {
  AiTaskMessage *msg = nullptr;

  while (true) {
    // Wait indefinitely for a message
    if (xQueueReceive(_queue, &msg, portMAX_DELAY) == pdTRUE) {
      if (!msg) continue;

      // Send "Thinking..." immediately while AI processes
      _bot.sendThinking(msg->chatId);

      Serial.printf("[AiTask] Processing: %s\n", msg->text.c_str());

      // This blocks the AI task, but Telegram polling continues on core 0
      String response = _ai.call(msg->text);

      Serial.printf("[AiTask] Reply (%d bytes): %s\n",
                    response.length(),
                    response.substring(0, 80).c_str());

      _bot.sendMessage(msg->chatId, response);

      delete msg;
      msg = nullptr;
    }
  }
}