# Changelog

All notable changes to this project will be documented in this file.

## [2.0.0] — 2025-05-26

### Added

- **MemoryStore**: NVS-backed persistent key-value store (max 50 entries).
  - `/remember key=value`, `/forget key`, `/memory`, `/memory clear` commands.
  - Auto-injects stored memories into AI system prompt.
- **ToolDispatcher**: 9 hardware tools exposed via OpenAI function-calling protocol.
  - `gpio_read`, `gpio_write`, `gpio_mode`, `pwm_write`, `adc_read`, `i2c_scan`, `system_info`, `memory_store`, `memory_read`.
  - Pin safety table prevents writing to boot/flash/serial pins.
  - Multi-round tool execution (up to 3 rounds per AI request).
- **SkillsManager**: SPIFFS-backed skill definitions with trigger-keyword matching.
  - `/skill create/list/delete/on/off` commands.
  - Auto-injects matching skill instructions into AI system prompt.
  - Max 16 skills, persisted across reboots.
- **CronManager**: FreeRTOS software timer-based scheduler.
  - `/cron add/list/delete/pause/resume` commands.
  - Jobs persisted in NVS (survive reboots).
  - Max 8 concurrent jobs, min interval 1 minute.
  - Pending messages dispatched in main loop (ISR-safe).
- **Personality/Context**: NVS-backed system prompt customization.
  - `/personality`, `/context` commands (set, show, clear).
  - Custom personality and context injected into every AI system prompt.
- **AiProvider**: Added `callRaw()` for tool-calling and `callWithToolResults()` for multi-round tool execution.
  - Tool definitions registered at runtime via `addTool()`.
- **AiTask**: Integrated ToolDispatcher for automatic tool_call detection and execution.
  - Up to 3 tool-call rounds per AI request.

### Changed

- System prompt builder now injects personality, context, memories, and matching skills dynamically.
- `/status` now reports memory count, skill count, and cron job count.
- `/start` and `/help` now list all available commands including skills, cron, personality, and context.

### Build

- RAM: 18.2% (59,536 / 327,680 bytes)
- Flash: 83.8% (1,098,353 / 1,310,720 bytes)

## [1.0.0] — 2025-05-24

### Added

- Initial release: ESP32 Telegram bot with AI chat (OpenAI-compatible providers).
- WiFiManager: WiFi connect, reconnect monitoring, status reporting.
- TelegramBot: Polling, callback dispatch, chat authorization, chunked messaging.
- AiProvider: Generic OpenAI-compatible chat completions client with conversation history.
- AiTask: FreeRTOS task on core 1 for non-blocking AI processing.
- Local commands: `/start`, `/help`, `/status`, `/restart`, `/led on/off/toggle`.
- OTA firmware updates via ArduinoOTA.
- PlatformIO build system with USB and OTA environments.
