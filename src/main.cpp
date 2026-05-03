#include <Arduino.h>
#include <ArduinoOTA.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

#include "config.h"
#include <WiFiManager.h>
#include <TelegramBot.h>
#include <AiProvider.h>
#include <AiTask.h>

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

WiFiManager wifi(WIFI_SSID, WIFI_PASSWORD);
TelegramBot bot(TELEGRAM_BOT_TOKEN, TELEGRAM_ALLOWED_CHAT_ID, LED_PIN);

AiProviderConfig aiConfig = { AI_BASE_URL, AI_API_KEY, AI_MODEL };
AiProvider ai(aiConfig);
AiTask aiTask(ai, bot);  // runs on core 1

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

String chipModelName(esp_chip_model_t model) {
  switch (model) {
    case CHIP_ESP32:   return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32H2: return "ESP32-H2";
    default:           return "Unknown";
  }
}

String buildStatusText() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);

  uint32_t flashSize = 0;
  esp_flash_get_size(nullptr, &flashSize);

  String s;
  s += "ESP32 AI Agent status\n";
  s += "Chip: " + chipModelName(chip.model) + " rev " + String(chip.revision) + "\n";
  s += "Cores: " + String(chip.cores) + "\n";
  s += "Flash: " + String(flashSize / (1024 * 1024)) + " MB\n";
  s += "Free heap: " + String(ESP.getFreeHeap()) + " bytes\n";
  s += "Min free heap: " + String(ESP.getMinFreeHeap()) + " bytes\n";
  s += "PSRAM: " + String(ESP.getPsramSize()) + " bytes\n";
  s += "Uptime: " + String(millis() / 1000) + " sec\n";
  s += "WiFi RSSI: " + String(wifi.rssi()) + " dBm\n";
  s += "IP: " + wifi.localIP() + "\n";
  s += "MAC: " + wifi.macAddress() + "\n";
  s += "AI: " + String(AI_BASE_URL) + " (" + String(AI_MODEL) + ")\n";
  s += "AI pending: " + String(aiTask.pending()) + "\n";
  s += "LED: " + String(digitalRead(LED_PIN) == HIGH ? "ON" : "OFF");
  return s;
}

String lowerTrimmed(String s) {
  s.trim();
  s.toLowerCase();
  return s;
}

// ---------------------------------------------------------------------------
// OTA event handlers
// ---------------------------------------------------------------------------

void setupOTA() {
  ArduinoOTA.setHostname("ugnay-esp32");

  ArduinoOTA.onStart([]() {
    String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
    Serial.println("\nOTA update started: " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA update finished. Rebooting...");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", progress / (total / 100));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error [%u]: ", error);
    if (error == OTA_AUTH_ERROR)       Serial.println("Auth failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive failed");
    else if (error == OTA_END_ERROR)   Serial.println("End failed");
  });

  ArduinoOTA.begin();
  Serial.print("OTA hostname: ");
  Serial.println(ArduinoOTA.getHostname());
  Serial.print("OTA upload via: pio run --target upload --upload-port ");
  Serial.println(WiFi.localIP());
}

// ---------------------------------------------------------------------------
// Command handler (called by TelegramBot callback, runs on core 0)
// ---------------------------------------------------------------------------

void onTelegramMessage(const String &chatId, const String &text, const String &from) {
  String cmd = lowerTrimmed(text);

  if (cmd == "/start" || cmd == "/help") {
    bot.sendMessage(chatId,
      "ESP32 AI Agent online.\n\n"
      "Commands:\n"
      "/status - board details\n"
      "/restart - reboot the ESP32\n"
      "/led on - turn LED on\n"
      "/led off - turn LED off\n"
      "/led toggle - toggle LED\n\n"
      "Or send any normal message and I will ask the AI (with conversation history).");
    return;
  }

  if (cmd == "/status" || cmd == "status") {
    bot.sendMessage(chatId, buildStatusText());
    return;
  }

  if (cmd == "/restart" || cmd == "restart") {
    bot.sendMessage(chatId, "Restarting...");
    delay(500);
    ESP.restart();
    return;
  }

  if (cmd == "/led on" || cmd == "led on" || cmd == "turn on led" || cmd == "turn the led on") {
    digitalWrite(LED_PIN, HIGH);
    bot.sendMessage(chatId, "LED is ON");
    return;
  }

  if (cmd == "/led off" || cmd == "led off" || cmd == "turn off led" || cmd == "turn the led off") {
    digitalWrite(LED_PIN, LOW);
    bot.sendMessage(chatId, "LED is OFF");
    return;
  }

  if (cmd == "/led toggle" || cmd == "led toggle" || cmd == "toggle led") {
    bool state = digitalRead(LED_PIN) == HIGH ? false : true;
    digitalWrite(LED_PIN, state ? HIGH : LOW);
    bot.sendMessage(chatId, String("LED is ") + (state ? "ON" : "OFF"));
    return;
  }

  // Not a local command → enqueue for AI task (non-blocking)
  if (!aiTask.enqueue(chatId, text)) {
    bot.sendMessage(chatId, "Queue full. Wait for the current request to finish.");
  }
}

// ---------------------------------------------------------------------------
// Setup & Loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  bot.begin();
  ai.begin();

  // LED off initially
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\nESP32 AI Agent booting...");
  Serial.println("AI provider: " + String(AI_BASE_URL));
  Serial.println("AI model:    " + String(AI_MODEL));
  Serial.println(buildStatusText());

  // Check for placeholder credentials
  if (String(WIFI_SSID) == "YOUR_WIFI_SSID" ||
      String(TELEGRAM_BOT_TOKEN).indexOf("YOUR_TELEGRAM_BOT_TOKEN") >= 0 ||
      String(AI_API_KEY).indexOf("YOUR_AI_API_KEY") >= 0) {
    Serial.println("\nWARNING: include/config.h still has placeholder credentials.");
    Serial.println("Edit include/config.h before uploading for real use.");
  }

  wifi.connect();

  // Set up OTA after WiFi is connected
  setupOTA();

  bot.onMessage(onTelegramMessage);

  // Start the AI task on core 1
  aiTask.begin();

  Serial.println("Telegram polling started (core 0). AI task on core 1.");
  if (String(TELEGRAM_ALLOWED_CHAT_ID).length() == 0) {
    Serial.println("TELEGRAM_ALLOWED_CHAT_ID is empty. First message will print chat_id in this monitor.");
  }
}

void loop() {
  if (!wifi.isConnected()) {
    wifi.reconnect();
  }

  ArduinoOTA.handle();
  bot.poll();
}