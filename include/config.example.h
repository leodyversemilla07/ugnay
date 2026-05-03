#pragma once

// Copy this file to include/config.h and fill in your real values.

// ---- WiFi ----
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ---- Telegram ----
#define TELEGRAM_BOT_TOKEN "123456789:YOUR_TELEGRAM_BOT_TOKEN"

// Optional but recommended. If set to "", any Telegram user can talk to the ESP32.
// Get it by messaging your bot, then watching Serial Monitor output.
#define TELEGRAM_ALLOWED_CHAT_ID ""

// ---- AI Provider (OpenAI-compatible) ----
//
// Supported providers (set AI_BASE_URL and AI_API_KEY accordingly):
//
//   OpenRouter:
//     AI_BASE_URL  "https://openrouter.ai/api"
//     AI_API_KEY   "sk-or-v1-..."
//     AI_MODEL     "openai/gpt-4o-mini"
//
//   NVIDIA NIM (cloud API catalog):
//     AI_BASE_URL  "https://integrate.api.nvidia.com"
//     AI_API_KEY   "nvapi-..."
//     AI_MODEL     "meta/llama-3.1-405b-instruct"
//
//   OpenAI:
//     AI_BASE_URL  "https://api.openai.com"
//     AI_API_KEY   "sk-..."
//     AI_MODEL     "gpt-4o-mini"
//
// Any OpenAI-compatible endpoint will work.

#define AI_BASE_URL "https://openrouter.ai/api"
#define AI_API_KEY "sk-or-v1-YOUR_AI_API_KEY"
#define AI_MODEL "openai/gpt-4o-mini"

// ---- Hardware ----
// Common ESP32 DevKit onboard LED pin. Change if your board uses another pin.
#define LED_PIN 2