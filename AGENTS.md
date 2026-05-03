# AGENTS.md

Guidance for AI coding agents working on this repository.

## Project overview

This is a PlatformIO ESP32 Arduino firmware project. The firmware connects an ESP32 DevKit to WiFi, polls the Telegram Bot API, supports local LED/status/restart commands, runs AI API calls on a dedicated FreeRTOS task (core 1), and maintains rolling conversation history. It supports any OpenAI-compatible AI provider (OpenRouter, NVIDIA NIM, OpenAI, etc.) and can receive firmware updates over-the-air (OTA).

## Important files

- `src/main.cpp` — firmware entry point (orchestration layer)
- `lib/WiFiManager/` — WiFi connection and status
- `lib/TelegramBot/` — Telegram polling, messaging, authorization
- `lib/AiProvider/` — Generic OpenAI-compatible AI provider with conversation history
- `lib/AiTask/` — FreeRTOS task on core 1 for non-blocking AI calls
- `platformio.ini` — board, framework, serial, OTA, and dependency configuration
- `include/config.example.h` — safe template for local configuration
- `include/config.h` — local secrets/config; do not commit or print unnecessarily

## AI provider config

The firmware uses a generic OpenAI-compatible AI provider. Set these in `config.h`:

```cpp
#define AI_BASE_URL "https://openrouter.ai/api"   // provider base URL
#define AI_API_KEY "sk-or-v1-..."                  // API key / bearer token
#define AI_MODEL "openai/gpt-4o-mini"               // model identifier
```

Supported: OpenRouter, NVIDIA NIM, OpenAI, or any OpenAI-compatible endpoint.

## Build and validation

Use PlatformIO commands from the repository root:

```bash
platformio run                   # Build (USB env)
platformio run --target upload   # USB upload

# OTA upload (after initial USB upload):
pio run -e ota --target upload --upload-port <ESP_IP>

platformio device monitor        # Serial monitor (USB)
```

Before considering firmware changes complete, run both envs:

```bash
platformio run
platformio run -e ota
```

## Coding conventions

- Keep firmware compatible with Arduino on ESP32.
- Prefer small, simple functions over large monolithic logic.
- Avoid unnecessary heap allocations in long-running code.
- Be careful with `String` usage on ESP32; avoid excessive temporary strings in loops.
- Keep Telegram messages concise because they are sent over HTTP and may be chunked.
- Preserve existing local commands unless explicitly asked to change them.

## Secrets and configuration

- Never commit real credentials.
- `include/config.h` is intentionally ignored by git.
- Add new configurable values to both:
  - `include/config.example.h`
  - `include/config.h` only when working locally and necessary
- Do not include real WiFi passwords, Telegram tokens, chat IDs, or API keys in documentation.

## Security considerations

The firmware currently uses `setInsecure()` across multiple WiFiClientSecure instances. This disables TLS certificate validation. Acceptable for local experiments, but production changes should replace it with proper certificate validation or a trusted CA bundle.

## Runtime constraints

Target board is `esp32dev` with 4 MB flash and 320 KB RAM. Keep memory usage conservative:

- `AiProvider` stores conversation history in parallel String arrays (default 3 turns = 6 messages, ~1.2 KB). The `AI_MAX_HISTORY_TURNS` constant caps this at compile time.
- `DynamicJsonDocument(32768)` is the largest single allocation (AI response parsing).
- `AiTask` has an 8 KB FreeRTOS stack.
- OTA adds ~3.5 KB to flash over the base build.
- Avoid very large `DynamicJsonDocument` allocations beyond the above.
- Consider heap fragmentation when adding long-lived features.

## OTA notes

- Hostname: `ugnay-esp32`
- The `[env:ota]` section in `platformio.ini` uses `upload_protocol = espota`
- After the initial USB upload, run: `pio run -e ota --target upload --upload-port <IP>`
- The ESP prints `OTA upload via: pio run --target upload --upload-port <IP>` to serial on boot.

## Conversation history

- `AiProvider` tracks the last `AI_MAX_HISTORY_TURNS` (3) user+assistant exchanges.
- History is stored as parallel `String` arrays in the class (not heap-allocated structs).
- Failed API calls, errors, and reasoning-only responses are NOT stored in history.
- Call `ai.clearHistory()` to reset. Call `ai.setMaxHistoryTurns(N)` to adjust at runtime (max `AI_MAX_HISTORY_TURNS`).

## Do not modify unless requested

- Upload/monitor COM port settings in `platformio.ini`
- Real local values inside `include/config.h`
- `.gitignore` entry for `include/config.h`

## Expected agent workflow

1. Inspect relevant files before editing.
2. Make focused changes.
3. Build with `platformio run` and `platformio run -e ota`.
4. Report changed files and build result.
