# Ugnay ESP32 AI Agent

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-blue)](https://platformio.org/)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)

Telegram-controlled AI agent firmware for an ESP32 DevKit. The board connects to WiFi, polls a Telegram bot for messages, handles local hardware commands, and forwards normal messages to any OpenAI-compatible AI provider (OpenRouter, NVIDIA NIM, OpenAI, self-hosted, etc.).

## Features

- Telegram bot command interface (non-blocking — local commands respond instantly even during AI calls)
- Provider-agnostic AI chat (OpenRouter, NVIDIA NIM, OpenAI, or any OpenAI-compatible endpoint)
- Conversation history (remembers last 3 exchanges for natural chat)
- Over-the-air (OTA) firmware updates
- ESP32 status reporting
- Onboard LED control
- Remote restart command
- Optional Telegram chat allowlist
- PlatformIO-based Arduino firmware

## Hardware target

- Board: ESP32 Dev Module / `esp32dev`
- Framework: Arduino
- CPU: ESP32 up to 240 MHz (dual-core)
- Flash: 4 MB configured
- Serial monitor speed: 115200 baud
- Default LED pin: GPIO 2

## Project structure

```text
ugnay
├── CHANGELOG.md
├── include/
│   ├── config.example.h      # Template configuration (safe to commit)
│   └── config.h              # Local secrets/config, gitignored
├── lib/
│   ├── AiProvider/           # Generic OpenAI-compatible AI client
│   │   ├── AiProvider.h
│   │   └── AiProvider.cpp
│   ├── AiTask/               # FreeRTOS task on core 1 for non-blocking AI calls
│   │   ├── AiTask.h
│   │   └── AiTask.cpp
│   ├── TelegramBot/          # Telegram polling, messaging, auth
│   │   ├── TelegramBot.h
│   │   └── TelegramBot.cpp
│   └── WiFiManager/          # WiFi connection management
│       ├── WiFiManager.h
│       └── WiFiManager.cpp
├── src/
│   └── main.cpp              # Firmware entry point (orchestration)
├── platformio.ini            # PlatformIO environment (USB + OTA)
└── README.md
```

## Architecture

The firmware runs on both ESP32 cores:

| Core       | Role      | Responsibilities                                          |
| ---------- | --------- | --------------------------------------------------------- |
| **Core 0** | Main loop | WiFi management, Telegram polling, local command handling |
| **Core 1** | AI task   | Queued AI API calls, response sending via Telegram        |

Messages flow: `Telegram → callback (core 0) → FreeRTOS queue → AiTask (core 1) → HTTP call → reply`

This means `/status` and `/led` commands respond instantly even while the AI is processing a request.

### Libraries

- **WiFiManager** — WiFi connect, reconnect, and status calls.
- **TelegramBot** — Polls `getUpdates`, dispatches via callback, handles authorization, sends chunked messages.
- **AiProvider** — Generic OpenAI-compatible chat completions client. Supports OpenRouter, NVIDIA NIM, OpenAI, or any compatible endpoint. Includes rolling conversation history (configurable, default 3 turns).
- **AiTask** — FreeRTOS task pinned to core 1. Receives messages via queue, processes them through `AiProvider`, sends replies back through `TelegramBot`.

### OTA updates

Built-in over-the-air firmware updates via `ArduinoOTA`. After the initial USB upload, subsequent updates can be done over WiFi:

```bash
# See the ESP's IP in serial output, then:
pio run -e ota --target upload --upload-port 192.168.1.100
```

The OTA hostname is `ugnay-esp32`.

## Setup

1. Install [PlatformIO](https://platformio.org/).
2. Copy the example configuration:

   ```bash
   cp include/config.example.h include/config.h
   ```

3. Edit `include/config.h` with your own values (see [Configuration](#configuration) below).
4. Build the firmware:

   ```bash
   platformio run
   ```

5. Upload to the ESP32 (USB, first time only):

   ```bash
   platformio run --target upload
   ```

6. Open the serial monitor:

   ```bash
   platformio device monitor
   ```

## Configuration

Edit `include/config.h` (copy from `include/config.example.h` first):

```cpp
// ---- WiFi ----
#define WIFI_SSID "your_network_name"
#define WIFI_PASSWORD "your_network_password"

// ---- Telegram ----
#define TELEGRAM_BOT_TOKEN "123456789:ABCdefGHIjklMNOpqrsTUVwxyz"
#define TELEGRAM_ALLOWED_CHAT_ID ""   // set to restrict to one chat

// ---- AI Provider (OpenAI-compatible) ----
#define AI_BASE_URL "https://openrouter.ai/api"       // provider endpoint
#define AI_API_KEY "sk-or-v1-..."                     // API key
#define AI_MODEL "openai/gpt-4o-mini"                  // model name

// ---- Hardware ----
#define LED_PIN 2
```

### Supported AI providers

| Provider              | `AI_BASE_URL`                      | Key prefix     | Example model                  |
| --------------------- | ---------------------------------- | -------------- | ------------------------------ |
| OpenRouter            | `https://openrouter.ai/api`        | `sk-or-v1-...` | `openai/gpt-4o-mini`           |
| NVIDIA NIM            | `https://integrate.api.nvidia.com` | `nvapi-...`    | `meta/llama-3.1-405b-instruct` |
| OpenAI                | `https://api.openai.com`           | `sk-...`       | `gpt-4o-mini`                  |
| Any OpenAI-compatible | your endpoint                      | as required    | as required                    |

## Telegram commands

```text
/start       Show help
/help        Show help
/status      Show ESP32 status and AI provider info
/restart     Reboot the ESP32 remotely
/led on      Turn LED on
/led off     Turn LED off
/led toggle  Toggle LED
```

Any other text message is forwarded to the configured AI provider. The AI remembers the last 3 exchanges for conversational continuity.

## Security

- `include/config.h` contains secrets and is gitignored.
- Do not commit WiFi passwords, Telegram bot tokens, or API keys.
- Libraries use `setInsecure()` for simple TLS. For production, configure certificate validation.
- Set `TELEGRAM_ALLOWED_CHAT_ID` to restrict bot access to a single Telegram chat ID.

## PlatformIO commands

```bash
platformio run                          # Build (USB env)
platformio run --target upload          # Upload over USB
platformio device monitor               # Serial monitor
platformio run --target clean           # Clean build files
pio run -e ota --target upload --upload-port <IP>  # OTA upload
pio run -e ota --target upload          # OTA upload (uses IP from platformio.ini)
```
