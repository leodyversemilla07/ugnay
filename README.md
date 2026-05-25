# Ugnay ESP32 AI Agent

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-blue)](https://platformio.org/)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)

> **Ugnay** (Tagalog: *to connect, link, bind*) — a Hermes-inspired AI agent for the ESP32 microcontroller.

Telegram-controlled AI agent firmware for an ESP32 DevKit. The board connects to WiFi, polls a Telegram bot for messages, handles local hardware commands, and forwards normal messages to any OpenAI-compatible AI provider. The AI can **read sensors, control GPIO, store memories, learn skills, and run scheduled tasks** — a closed learning loop on a $5 microcontroller.

## Features

| Feature | Description | Hermes Equivalent |
|---|---|---|
| **MemoryStore** | NVS-backed key-value memory, survives reboots | `memory` tool |
| **ToolDispatcher** | 9 hardware tools the AI calls directly (GPIO, PWM, ADC, I2C, system info, memory) | `tools` system |
| **SkillsManager** | Trigger-matched skill injection from SPIFFS | `skills` system |
| **CronManager** | FreeRTOS timer-based scheduler with NVS persistence | `cronjob` tool |
| **Personality/Context** | Custom AI behavior via NVS-stored system prompt injection | System prompt customization |
| **AI Chat** | Provider-agnostic with rolling conversation history | Core LLM interaction |
| **OTA Updates** | Over-the-air firmware updates via WiFi | — |
| **Dual-core** | Instant local commands even during AI processing | — |

### The Closed Learning Loop (Micro-Scale)

Just like Hermes, Ugnay follows **observe → remember → skill-create → improve**:

```
  User sends message
        │
        ▼
  ┌─────────────┐     ┌──────────────┐     ┌────────────────┐
  │  AI reasons  │────▶│  Calls tool  │────▶│  GPIO / I2C /  │
  │  & responds  │     │  (function   │     │  ADC / memory  │
  └──────┬───────┘     │   calling)   │     └───────┬────────┘
         │             └──────────────┘             │
         │                                          │
         │          ┌────────────────┐              │
         └─────────▶│  Stores facts  │◀─────────────┘
                    │  in MemoryStore│     (tool returns
                    └───────┬────────┘      sensor data)
                            │
                            ▼
                    ┌────────────────┐
                    │  User creates  │
                    │  skill from    │
                    │  the pattern   │
                    └───────┬────────┘
                            │
                            ▼
                    ┌────────────────┐
                    │  Skill auto-   │
                    │  injects into  │
                    │  system prompt │
                    └────────────────┘
```

## Hardware target

| Spec | Value |
|---|---|
| Board | ESP32 Dev Module / `esp32dev` |
| Framework | Arduino |
| CPU | ESP32 up to 240 MHz (dual-core) |
| Flash | 4 MB |
| RAM | 320 KB |
| Serial speed | 115200 baud |
| Default LED pin | GPIO 2 |

### Build stats (v2.0.0)

| Resource | Usage | Percentage |
|---|---|---|
| RAM | 59,536 / 327,680 bytes | 18.2% |
| Flash | 1,098,353 / 1,310,720 bytes | 83.8% |

## Architecture

The firmware runs on both ESP32 cores:

| Core | Role | Responsibilities |
|---|---|---|
| **Core 0** | Main loop | WiFi management, Telegram polling, local command handling, cron dispatch |
| **Core 1** | AI task | Queued AI API calls, tool execution, response sending via Telegram |

```
                    ┌──────────────────────────────────────┐
                    │           ESP32 DevKit               │
                    │                                      │
  Telegram ◄────────┤  Core 0              Core 1         │
     API            │  ┌──────────┐      ┌────────────┐  │
     │              │  │ WiFiMgr  │      │  AiTask    │  │
     │    getUpdate │  │ Telegram │──queue──▶ AiProvider│  │
     │    sendMessage│  │ CmdHandler│      │ ToolDisp   │  │
     │              │  │ CronPoll │      │ SkillsMatch│  │
     │              │  └──────────┘      └────────────┘  │
     │              │                                      │
     │              │  ┌─────────────────────────────┐    │
     │              │  │     Shared State (RAM)       │    │
     │              │  │  MemoryStore (NVS)           │    │
     │              │  │  SkillsManager (SPIFFS)      │    │
     │              │  │  CronManager (NVS + timers)  │    │
     │              │  │  ToolDispatcher (GPIO/PWM)   │    │
     │              │  └─────────────────────────────┘    │
                    └──────────────────┬───────────────────┘
                                       │
                              ┌────────┴────────┐
                              │   Physical I/O  │
                              │  GPIO  PWM  ADC │
                              │  I2C   LED  SPI │
                              └─────────────────┘
```

### Data flow

```
Telegram message
       │
       ▼
Callback (core 0)
       │
       ├── Local command? (/status, /led, /remember, /skill, /cron...)
       │   └── Execute immediately, reply to Telegram
       │
       └── Normal message? ──▶ FreeRTOS queue
                                    │
                                    ▼
                             AiTask (core 1)
                                    │
                                    ├── 1. Build system prompt
                                    │      (base + personality + context +
                                    │       memories + matching skills)
                                    │
                                    ├── 2. Call AI provider (HTTP)
                                    │
                                    ├── 3. Tool calls in response?
                                    │      └── Execute via ToolDispatcher
                                    │         (up to 3 rounds)
                                    │
                                    └── 4. Send final reply via Telegram
```

## Project structure

```text
ugnay
├── AGENTS.md                  # AI coding agent guidance
├── CHANGELOG.md               # Version history
├── LICENSE                    # MIT
├── README.md                  # This file
├── include/
│   ├── config.example.h       # Template configuration (safe to commit)
│   └── config.h               # Local secrets/config (gitignored)
├── lib/
│   ├── AiProvider/            # Generic OpenAI-compatible AI client
│   │   ├── AiProvider.h
│   │   └── AiProvider.cpp
│   ├── AiTask/                # FreeRTOS task on core 1
│   │   ├── AiTask.h
│   │   └── AiTask.cpp
│   ├── CronManager/           # FreeRTOS timer-based scheduler
│   │   ├── CronManager.h
│   │   └── CronManager.cpp
│   ├── MemoryStore/           # NVS-backed persistent key-value store
│   │   ├── MemoryStore.h
│   │   └── MemoryStore.cpp
│   ├── SkillsManager/         # SPIFFS-backed skill definitions
│   │   ├── SkillsManager.h
│   │   └── SkillsManager.cpp
│   ├── TelegramBot/           # Telegram polling, messaging, auth
│   │   ├── TelegramBot.h
│   │   └── TelegramBot.cpp
│   ├── ToolDispatcher/        # LLM → hardware tool execution
│   │   ├── ToolDispatcher.h
│   │   └── ToolDispatcher.cpp
│   └── WiFiManager/           # WiFi connection management
│       ├── WiFiManager.h
│       └── WiFiManager.cpp
├── src/
│   └── main.cpp               # Firmware entry point (orchestration)
└── platformio.ini             # PlatformIO environment config
```

### Library reference

| Library | Storage | Description |
|---|---|---|
| **MemoryStore** | NVS | Key-value store persisted across reboots. Auto-injects memories into AI system prompt. Max 50 entries. |
| **ToolDispatcher** | RAM | 9 hardware tools exposed as OpenAI function-calling tools. Pin safety table prevents writing to boot/serial pins. Up to 3 tool-call rounds per AI request. |
| **SkillsManager** | SPIFFS | JSON skill files with trigger keywords. Matching skills auto-inject instructions into system prompt. Max 16 skills. |
| **CronManager** | NVS + FreeRTOS timers | Scheduled Telegram messages at minute/hour intervals. Jobs survive reboots. Max 8 concurrent jobs. Min interval: 1 minute. |
| **AiProvider** | RAM | Generic OpenAI-compatible chat completions client. Rolling conversation history (default 3 turns). Supports tool-calling protocol. |
| **AiTask** | FreeRTOS queue | Pinned to core 1. Receives messages via queue, processes through AiProvider + ToolDispatcher, sends replies via TelegramBot. |
| **TelegramBot** | RAM | Polls `getUpdates`, dispatches via callback, handles chat authorization, sends chunked messages. |
| **WiFiManager** | RAM | WiFi connect, reconnect monitoring, and status calls. |

## AI tools (function calling)

The AI can call these hardware tools directly via OpenAI function-calling protocol:

| Tool | Description | Key Parameters |
|---|---|---|
| `gpio_read` | Read digital pin state | `pin` (int) |
| `gpio_write` | Write digital pin HIGH/LOW | `pin`, `value` (0/1) |
| `gpio_mode` | Set pin INPUT/OUTPUT | `pin`, `mode` ("input"/"output") |
| `pwm_write` | Write PWM signal | `pin`, `duty` (0-255) |
| `adc_read` | Read analog pin value | `pin` (ADC channel) |
| `i2c_scan` | Scan I2C bus for devices | `sda`, `scl` (optional pins) |
| `system_info` | Get chip info, memory, uptime | — |
| `memory_store` | Store a key-value pair | `key`, `value` |
| `memory_read` | Read a stored value | `key` |

**Pin safety**: GPIO 0, 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 14, 15 are protected — `gpio_write` and `gpio_mode` refuse to modify these (boot/flash/serial pins).

## Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- ESP32 DevKit (any variant with 4 MB flash)
- USB cable for initial flash
- WiFi network
- Telegram bot token (from [@BotFather](https://t.me/BotFather))
- AI provider API key (OpenRouter, NVIDIA NIM, OpenAI, etc.)

### Installation

1. **Clone the repository:**

   ```bash
   git clone https://github.com/leodyversemilla07/ugnay.git
   cd ugnay
   ```

2. **Copy the example configuration:**

   ```bash
   cp include/config.example.h include/config.h
   ```

3. **Edit `include/config.h`** with your values (see [Configuration](#configuration) below).

4. **Build the firmware:**

   ```bash
   platformio run
   ```

5. **Upload to the ESP32** (USB, first time only):

   ```bash
   platformio run --target upload
   ```

6. **Open the serial monitor** to see the boot log and get the ESP's IP:

   ```bash
   platformio device monitor
   ```

7. **Send `/start` to your Telegram bot** — the ESP32 will respond with the help message.

### OTA updates (after initial USB flash)

```bash
# Find the ESP's IP from serial monitor output, then:
pio run -e ota --target upload --upload-port 192.168.1.100
```

The OTA hostname is `ugnay-esp32`.

## Configuration

Edit `include/config.h` (copy from `include/config.example.h` first):

```cpp
// ---- WiFi ----
#define WIFI_SSID "your_network_name"
#define WIFI_PASSWORD "your_network_password"

// ---- Telegram ----
#define TELEGRAM_BOT_TOKEN "123456789:ABCdefGHIjklMNOpqrsTUVwxyz"
#define TELEGRAM_ALLOWED_CHAT_ID "" // set to restrict to one chat

// ---- AI Provider (OpenAI-compatible) ----
#define AI_BASE_URL "https://openrouter.ai/api" // provider endpoint
#define AI_API_KEY "sk-or-v1-..."               // API key
#define AI_MODEL "openai/gpt-4o-mini"           // model name

// ---- Hardware ----
#define LED_PIN 2
```

### Supported AI providers

| Provider | `AI_BASE_URL` | Key prefix | Example model |
|---|---|---|---|
| OpenRouter | `https://openrouter.ai/api` | `sk-or-v1-...` | `openai/gpt-4o-mini` |
| NVIDIA NIM | `https://integrate.api.nvidia.com` | `nvapi-...` | `meta/llama-3.1-405b-instruct` |
| OpenAI | `https://api.openai.com` | `sk-...` | `gpt-4o-mini` |
| Any OpenAI-compatible | your endpoint | as required | as required |

> **Tip**: Use a model that supports function/tool calling for the ToolDispatcher to work. `gpt-4o-mini` and most OpenRouter models support this.

## Telegram commands

### General

| Command | Description |
|---|---|
| `/start` | Show help message |
| `/help` | Show help message |
| `/status` | Show ESP32 status (chip, memory, WiFi, AI, skills, cron jobs) |
| `/restart` | Reboot the ESP32 |

### LED control

| Command | Description |
|---|---|
| `/led on` | Turn onboard LED on |
| `/led off` | Turn onboard LED off |
| `/led toggle` | Toggle onboard LED |

### Memory (NVS-backed, survives reboot)

| Command | Description |
|---|---|
| `/remember key=value` | Store a key-value pair |
| `/forget key` | Delete a stored memory |
| `/memory` | List all stored memories |
| `/memory clear` | Erase all memories |

**Examples:**
```
/remember room=living
/remember temp_threshold=28
/forget room
/memory
```

### Skills (SPIFFS-backed, trigger-matched)

| Command | Description |
|---|---|
| `/skill create name triggers \| instructions` | Create a skill |
| `/skill list` | Show all skills |
| `/skill delete name` | Remove a skill |
| `/skill on name` | Enable a skill |
| `/skill off name` | Disable a skill |

**Examples:**
```
/skill create temp-monitor temperature,temp | Read ADC on pin 34, convert to Celsius, report if above threshold
/skill create led-helper light,led,illumination | Use gpio_read on pin 2 to check LED, then gpio_write to toggle it
/skill list
/skill off temp-monitor
```

When a user message contains any of the skill's trigger keywords, the skill's instructions are automatically injected into the AI's system prompt — no manual activation needed (unless the skill is turned off).

### Cron (FreeRTOS timer-based, survives reboot)

| Command | Description |
|---|---|
| `/cron add name interval message` | Schedule a recurring message |
| `/cron list` | Show all scheduled jobs |
| `/cron delete name` | Remove a job |
| `/cron pause name` | Pause a job |
| `/cron resume name` | Resume a paused job |

**Interval format:** `Nm` (minutes) or `Nh` (hours). Min: 1m. Max: 8 concurrent jobs.

**Examples:**
```
/cron add heartbeat 5m System alive — uptime and free heap
/cron add morning 24h Good morning! Check /status for system health
/cron list
/cron pause heartbeat
/cron resume heartbeat
/cron delete morning
```

### Personality & Context (NVS-backed, survives reboot)

| Command | Description |
|---|---|
| `/personality text` | Set custom AI personality |
| `/personality` | Show current personality |
| `/personality clear` | Reset to default personality |
| `/context text` | Set additional AI context |
| `/context` | Show current context |
| `/context clear` | Clear custom context |

**Examples:**
```
/personality You are a friendly home automation assistant named Ugnay. Be brief and helpful.
/context Living room has a DHT11 on pin 4. Kitchen light is on pin 26.
/personality
/context clear
```

Personality and context are injected into the system prompt every time the AI is called, so the AI always "remembers" its role and environment.

### AI chat

Any message that isn't a recognized command is forwarded to the AI provider. The AI:

1. Receives a system prompt (base + personality + context + memories + matching skills)
2. Processes the message with conversation history (last 3 exchanges)
3. May call hardware tools (function calling) — up to 3 rounds
4. Sends the final text reply via Telegram

## Security

- `include/config.h` contains secrets and is **gitignored**.
- Do not commit WiFi passwords, Telegram bot tokens, or API keys.
- Libraries use `setInsecure()` for simple TLS. For production, configure certificate validation.
- Set `TELEGRAM_ALLOWED_CHAT_ID` to restrict bot access to a single Telegram chat ID.
- ToolDispatcher has a pin safety table to prevent writing to boot/flash/serial pins.
- CronManager minimum interval is 1 minute to prevent spam.

## PlatformIO commands

```bash
platformio run                                        # Build (USB env)
platformio run --target upload                        # Upload over USB
platformio device monitor                             # Serial monitor
platformio run --target clean                         # Clean build files
pio run -e ota --target upload --upload-port <IP>     # OTA upload
```

## Roadmap

- [ ] Flash & test on real ESP32 hardware
- [ ] DHT11/22 temperature & humidity sensor tool
- [ ] Servo motor control tool
- [ ] OLED SSD1306 display tool
- [ ] TLS certificate pinning
- [ ] Command rate limiting
- [ ] WiFi reconnect with exponential backoff
- [ ] Web dashboard for monitoring

## License

[MIT](LICENSE)
