#include <Arduino.h>
#include <ArduinoOTA.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

#include "config.h"
#include <WiFiManager.h>
#include <TelegramBot.h>
#include <AiProvider.h>
#include <AiTask.h>
#include <MemoryStore.h>
#include <ToolDispatcher.h>
#include <SkillsManager.h>
#include <CronManager.h>

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

WiFiManager wifi(WIFI_SSID, WIFI_PASSWORD);
TelegramBot bot(TELEGRAM_BOT_TOKEN, TELEGRAM_ALLOWED_CHAT_ID, LED_PIN);

AiProviderConfig aiConfig = { AI_BASE_URL, AI_API_KEY, AI_MODEL };
AiProvider ai(aiConfig);
MemoryStore memory;   // NVS-backed persistent memory
ToolDispatcher tools; // Hardware tool execution for LLM
SkillsManager skills; // SPIFFS-backed skill definitions
CronManager cron; // FreeRTOS timer-based scheduled messages
AiTask aiTask(ai, bot); // runs on core 1

#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW false
#endif

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

int ledActiveLevel() {
  return LED_ACTIVE_LOW ? LOW : HIGH;
}

int ledInactiveLevel() {
  return LED_ACTIVE_LOW ? HIGH : LOW;
}

void setLed(bool on) {
  digitalWrite(LED_PIN, on ? ledActiveLevel() : ledInactiveLevel());
}

bool isLedOn() {
  return digitalRead(LED_PIN) == ledActiveLevel();
}

void blinkLed(int times = 3, int delayMs = 250) {
  for (int i = 0; i < times; i++) {
    setLed(true);
    delay(delayMs);
    setLed(false);
    delay(delayMs);
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
  s += "Memories: " + String(memory.count()) + "\n";
  s += "Skills: " + String(skills.count()) + "\n";
  s += "Cron jobs: " + String(cron.count()) + "\n";
  s += "LED pin: GPIO" + String(LED_PIN) + "\n";
  s += "LED active low: " + String(LED_ACTIVE_LOW ? "yes" : "no") + "\n";
  s += "LED: " + String(isLedOn() ? "ON" : "OFF");
  return s;
}

String lowerTrimmed(String s) {
  s.trim();
  s.toLowerCase();
  return s;
}

// ---------------------------------------------------------------------------
// System prompt builder — injects persistent memories into AI context
// ---------------------------------------------------------------------------

void _updateSystemPrompt() {
  String prompt =
    "You are a concise AI agent connected to an ESP32 DevKit. "
    "Reply with the final answer only. Do not show hidden reasoning. "
    "Keep replies under 60 words unless the user asks for detail. "
    "The ESP32 can respond on Telegram and has commands: /status, /led on, /led off, /led toggle, /remember, /forget, /memory, /skill, /cron. "
    "If the user wants hardware action, tell them the exact command to send.";

  // Inject custom personality if stored
  String personality = memory.recall("personality");
  if (personality.length() > 0) {
    prompt += "\n\nPersonality: " + personality;
  }

  // Inject custom context if stored
  String context = memory.recall("context");
  if (context.length() > 0) {
    prompt += "\n\nContext: " + context;
  }

  String memInjection = memory.buildSystemInjection();
  if (memInjection.length() > 0) {
    prompt += "\n\n" + memInjection;
  }

  ai.setSystemPrompt(prompt);
}

// Per-message system prompt augmentation with matching skills
String _buildPromptWithSkills(const String &userText) {
  String base = ai.getSystemPrompt();
  String skillInstr = skills.getMatchingInstructions(userText);
  if (skillInstr.length() > 0) {
    return base + "\n\n" + skillInstr;
  }
  return base;
}

bool hasAny(const String &s, const char *const phrases[], int count) {
  for (int i = 0; i < count; i++) {
    if (s.indexOf(phrases[i]) >= 0) return true;
  }
  return false;
}

int detectLedAction(const String &cmd) {
  // Allow short follow-up prompts after LED use, e.g. "turn it off".
  // This firmware only controls one local actuator, the LED, so these are safe.
  const char *const genericTogglePhrases[] = {
    "toggle it", "switch it", "toggle that", "switch that"
  };
  if (cmd == "toggle" || hasAny(cmd, genericTogglePhrases, sizeof(genericTogglePhrases) / sizeof(genericTogglePhrases[0]))) return 2;

  const char *const genericOffPhrases[] = {
    "turn it off", "turn that off", "switch it off", "switch that off",
    "shut it off", "shut that off", "power it off", "power that off"
  };
  if (cmd == "off" || cmd == "turn off" || hasAny(cmd, genericOffPhrases, sizeof(genericOffPhrases) / sizeof(genericOffPhrases[0]))) return 0;

  const char *const genericOnPhrases[] = {
    "turn it on", "turn that on", "switch it on", "switch that on",
    "power it on", "power that on"
  };
  if (cmd == "on" || cmd == "turn on" || hasAny(cmd, genericOnPhrases, sizeof(genericOnPhrases) / sizeof(genericOnPhrases[0]))) return 1;

  bool mentionsLed = cmd.indexOf("led") >= 0 || cmd.indexOf("light") >= 0;
  if (!mentionsLed) return -1;

  const char *const togglePhrases[] = {
    "/led toggle", "led toggle", "toggle led", "toggle the led",
    "toggle light", "toggle the light", "switch led", "switch the led"
  };
  if (hasAny(cmd, togglePhrases, sizeof(togglePhrases) / sizeof(togglePhrases[0]))) return 2;

  const char *const offPhrases[] = {
    "/led off", "led off", "light off", "turn off led", "turn the led off",
    "turn off the led", "turn off light", "turn the light off", "turn off the light",
    "switch off led", "switch the led off", "switch off the led",
    "disable led", "disable the led", "shut off led", "shut off the led"
  };
  if (hasAny(cmd, offPhrases, sizeof(offPhrases) / sizeof(offPhrases[0]))) return 0;

  const char *const onPhrases[] = {
    "/led on", "led on", "light on", "turn on led", "turn the led on",
    "turn on the led", "turn on light", "turn the light on", "turn on the light",
    "switch on led", "switch the led on", "switch on the led",
    "enable led", "enable the led", "power on led", "power on the led"
  };
  if (hasAny(cmd, onPhrases, sizeof(onPhrases) / sizeof(onPhrases[0]))) return 1;

  return -1;
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
    unsigned int percent = total == 0 ? 0 : (progress * 100) / total;
    Serial.printf("OTA progress: %u%%\r", percent);
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
      "/led toggle - toggle LED\n"
      "/led test - blink LED for testing\n"
      "/remember key=value - store a memory\n"
      "/forget key - delete a memory\n"
      "/memory - list all memories\n"
      "/memory clear - erase all memories\n"
      "/skill create name triggers | instructions - create skill\n"
      "/skill list - show all skills\n"
      "/skill delete name - remove a skill\n"
      "/skill on/off name - toggle skill\n"
      "/cron add name interval message - schedule msg (5m, 1h)\n"
      "/cron list - show scheduled jobs\n"
      "/cron delete name - remove a job\n"
      "/cron pause/resume name - toggle job\n"
      "/personality text - set AI personality\n"
      "/context text - set additional AI context\n\n"
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

  if (cmd == "/led test" || cmd == "led test" || cmd == "test led" || cmd == "blink led" || cmd == "blink the led") {
    bot.sendMessage(chatId, "Blinking LED test on GPIO" + String(LED_PIN) + "...");
    blinkLed();
    bot.sendMessage(chatId, "LED test finished. If nothing blinked, your board likely has no LED on GPIO" + String(LED_PIN) + " or uses another LED pin.");
    return;
  }

  int ledAction = detectLedAction(cmd);
  if (ledAction == 1) {
    setLed(true);
    bot.sendMessage(chatId, "LED is ON");
    return;
  }

  if (ledAction == 0) {
    setLed(false);
    bot.sendMessage(chatId, "LED is OFF");
    return;
  }

  if (ledAction == 2) {
    bool state = !isLedOn();
    setLed(state);
    bot.sendMessage(chatId, String("LED is ") + (state ? "ON" : "OFF"));
    return;
  }

  // /remember key=value — store persistent memory
  if (cmd.startsWith("/remember ") || cmd.startsWith("remember ")) {
    String args = text.substring(text.indexOf(' ') + 1);
    args.trim();
    int eqPos = args.indexOf('=');
    if (eqPos <= 0) {
      bot.sendMessage(chatId, "Usage: /remember key=value\nExample: /remember user_name=Leodyver");
      return;
    }
    String key = args.substring(0, eqPos);
    String value = args.substring(eqPos + 1);
    key.trim();
    value.trim();
    if (memory.remember(key, value)) {
      // Update system prompt with new memory context
      _updateSystemPrompt();
      bot.sendMessage(chatId, "Remembered: " + key + " = " + value);
    } else {
      bot.sendMessage(chatId, "Failed to store memory. Key too long or NVS error.");
    }
    return;
  }

  // /forget key — delete a memory
  if (cmd.startsWith("/forget ") || cmd.startsWith("forget ")) {
    String key = text.substring(text.indexOf(' ') + 1);
    key.trim();
    if (memory.forget(key)) {
      _updateSystemPrompt();
      bot.sendMessage(chatId, "Forgot: " + key);
    } else {
      bot.sendMessage(chatId, "No memory found for: " + key);
    }
    return;
  }

  // /memory — list all memories
  if (cmd == "/memory" || cmd == "memory") {
    String list = memory.list();
    if (list.length() == 0) {
      bot.sendMessage(chatId, "No memories stored. Use /remember key=value");
    } else {
      bot.sendMessage(chatId, "Memories (" + String(memory.count()) + "):\n" + list);
    }
    return;
  }

  // /memory clear — erase all memories
  if (cmd == "/memory clear" || cmd == "memory clear") {
    int cleared = memory.clearAll();
    _updateSystemPrompt();
    bot.sendMessage(chatId, "Cleared " + String(cleared) + " memories.");
    return;
  }

  // /personality <text> — set custom AI personality
  if (cmd.startsWith("/personality ") || cmd.startsWith("personality ")) {
    String p = text.substring(text.indexOf(' ') + 1);
    p.trim();
    if (p == "clear" || p == "reset") {
      memory.forget("personality");
      _updateSystemPrompt();
      bot.sendMessage(chatId, "Personality reset to default.");
    } else {
      memory.remember("personality", p);
      _updateSystemPrompt();
      bot.sendMessage(chatId, "Personality set: " + p);
    }
    return;
  }

  // /personality (no args) — show current personality
  if (cmd == "/personality" || cmd == "personality") {
    String p = memory.recall("personality");
    if (p.length() > 0) {
      bot.sendMessage(chatId, "Current personality: " + p);
    } else {
      bot.sendMessage(chatId, "No custom personality set. Use /personality <text>");
    }
    return;
  }

  // /context <text> — set additional AI context
  if (cmd.startsWith("/context ") || cmd.startsWith("context ")) {
    String c = text.substring(text.indexOf(' ') + 1);
    c.trim();
    if (c == "clear" || c == "reset") {
      memory.forget("context");
      _updateSystemPrompt();
      bot.sendMessage(chatId, "Context cleared.");
    } else {
      memory.remember("context", c);
      _updateSystemPrompt();
      bot.sendMessage(chatId, "Context set: " + c);
    }
    return;
  }

  // /context (no args) — show current context
  if (cmd == "/context" || cmd == "context") {
    String c = memory.recall("context");
    if (c.length() > 0) {
      bot.sendMessage(chatId, "Current context: " + c);
    } else {
      bot.sendMessage(chatId, "No custom context set. Use /context <text>");
    }
    return;
  }

  // /skill create <name> <triggers> | <instructions>
  if (cmd.startsWith("/skill create ") || cmd.startsWith("skill create ")) {
    String args = text.substring(text.indexOf("create") + 6);
    args.trim();
    int pipePos = args.indexOf('|');
    if (pipePos <= 0) {
      bot.sendMessage(chatId,
        "Usage: /skill create <name> <triggers> | <instructions>\n"
        "Example: /skill create led-control led,light,gpio | When asked about LEDs, mention pin 2 and use gpio_write tool");
      return;
    }
    String nameAndTriggers = args.substring(0, pipePos);
    String instructions = args.substring(pipePos + 1);
    instructions.trim();
    int spacePos = nameAndTriggers.indexOf(' ');
    if (spacePos <= 0) {
      bot.sendMessage(chatId, "Need both name and triggers. Example: /skill create led-control led,light | ...");
      return;
    }
    String skillName = nameAndTriggers.substring(0, spacePos);
    String triggers = nameAndTriggers.substring(spacePos + 1);
    skillName.trim();
    triggers.trim();
    bot.sendMessage(chatId, skills.create(skillName, triggers, instructions));
    return;
  }

  // /skill list
  if (cmd == "/skill list" || cmd == "/skill" || cmd == "skill") {
    bot.sendMessage(chatId, skills.list());
    return;
  }

  // /skill delete <name>
  if (cmd.startsWith("/skill delete ") || cmd.startsWith("skill delete ")) {
    String name = text.substring(text.indexOf("delete") + 6);
    name.trim();
    bot.sendMessage(chatId, skills.remove(name));
    return;
  }

  // /skill on <name>
  if (cmd.startsWith("/skill on ") || cmd.startsWith("skill on ")) {
    String name = text.substring(text.indexOf("on ") + 3);
    name.trim();
    bot.sendMessage(chatId, skills.toggle(name, true));
    return;
  }

  // /skill off <name>
  if (cmd.startsWith("/skill off ") || cmd.startsWith("skill off ")) {
    String name = text.substring(text.indexOf("off ") + 4);
    name.trim();
    bot.sendMessage(chatId, skills.toggle(name, false));
    return;
  }

  // /cron add <name> <interval> <message>
  // interval format: Nm or Nh (e.g. 5m, 1h)
  if (cmd.startsWith("/cron add ") || cmd.startsWith("cron add ")) {
    String args = text.substring(text.indexOf("add") + 3);
    args.trim();
    // Parse: name interval message
    // Find first space → name, find second space → interval, rest → message
    int sp1 = args.indexOf(' ');
    if (sp1 <= 0) {
      bot.sendMessage(chatId,
        "Usage: /cron add <name> <interval> <message>\n"
        "Interval: Nm (minutes) or Nh (hours)\n"
        "Example: /cron add status-check 5m System OK");
      return;
    }
    String cronName = args.substring(0, sp1);
    String rest = args.substring(sp1 + 1);
    rest.trim();
    int sp2 = rest.indexOf(' ');
    if (sp2 <= 0) {
      bot.sendMessage(chatId,
        "Need interval and message.\n"
        "Example: /cron add status-check 5m System OK");
      return;
    }
    String intervalStr = rest.substring(0, sp2);
    String cronMsg = rest.substring(sp2 + 1);
    cronMsg.trim();

    // Parse interval
    unsigned long intervalMs = 0;
    if (intervalStr.endsWith("h")) {
      int hours = intervalStr.substring(0, intervalStr.length() - 1).toInt();
      intervalMs = hours * 3600000UL;
    } else if (intervalStr.endsWith("m")) {
      int mins = intervalStr.substring(0, intervalStr.length() - 1).toInt();
      intervalMs = mins * 60000UL;
    } else {
      bot.sendMessage(chatId, "Interval must end with 'm' or 'h' (e.g. 5m, 1h)");
      return;
    }

    bot.sendMessage(chatId, cron.addJob(cronName, intervalMs, chatId, cronMsg));
    return;
  }

  // /cron list
  if (cmd == "/cron list" || cmd == "/cron" || cmd == "cron") {
    bot.sendMessage(chatId, cron.list());
    return;
  }

  // /cron delete <name>
  if (cmd.startsWith("/cron delete ") || cmd.startsWith("cron delete ")) {
    String name = text.substring(text.indexOf("delete") + 6);
    name.trim();
    bot.sendMessage(chatId, cron.removeJob(name));
    return;
  }

  // /cron pause <name>
  if (cmd.startsWith("/cron pause ") || cmd.startsWith("cron pause ")) {
    String name = text.substring(text.indexOf("pause") + 5);
    name.trim();
    bot.sendMessage(chatId, cron.pauseJob(name));
    return;
  }

  // /cron resume <name>
  if (cmd.startsWith("/cron resume ") || cmd.startsWith("cron resume ")) {
    String name = text.substring(text.indexOf("resume") + 6);
    name.trim();
    bot.sendMessage(chatId, cron.resumeJob(name));
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
  memory.begin(); // Initialize NVS for persistent memory
  tools.setMemoryStore(&memory); // Let AI tools read/write persistent memory
  skills.begin(); // Mount SPIFFS, load skill index
  cron.begin(); // Restore scheduled jobs from NVS

  // LED off initially
  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  Serial.println("\nESP32 AI Agent booting...");
  Serial.println("AI provider: " + String(AI_BASE_URL));
  Serial.println("AI model: " + String(AI_MODEL));

  // Initialize system prompt with any stored memories
  _updateSystemPrompt();

  Serial.println(buildStatusText());
  Serial.printf("MemoryStore: %d memories loaded\n", memory.count());

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
  aiTask.setToolDispatcher(tools);
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

  // Dispatch any pending cron messages
  String pending = cron.poll();
  if (pending.length() > 0) {
    int pipePos = pending.indexOf('|');
    if (pipePos > 0) {
      String cronChatId = pending.substring(0, pipePos);
      String cronMsg = pending.substring(pipePos + 1);
      bot.sendMessage(cronChatId, "[cron] " + cronMsg);
    }
  }
}