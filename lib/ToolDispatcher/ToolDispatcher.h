#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// ToolDispatcher — enables the AI to execute hardware actions via tool_calls.
//
// When the LLM response contains tool_calls, AiTask passes them here.
// The dispatcher executes each tool and returns JSON results that get sent
// back to the LLM for a follow-up response.
//
// Built-in tools:
// gpio_read — read a GPIO pin: {"pin": 2}
// gpio_write — write a GPIO pin: {"pin": 2, "value": 1}
// gpio_mode — set pin mode: {"pin": 2, "mode": "output"}
// pwm_write — write PWM: {"pin": 2, "channel": 0, "duty": 128, "freq": 5000}
// adc_read — read analog: {"pin": 34}
// i2c_scan — scan I2C bus: {"sda": 21, "scl": 22}
// system_info — get ESP32 system info: {}
// memory_store — store to NVS: {"key": "...", "value": "..."}
// memory_read — read from NVS: {"key": "..."}
//
// ESP32 constraints:
// - ADC: only pins 32-39 (ADC1) usable without WiFi conflicts
// - GPIO: pins 0, 2 (boot), 5, 12, 14, 15 (strapping) need care
// - PWM: 16 LEDC channels (0-15), 2 high-speed timers
// ---------------------------------------------------------------------------

class MemoryStore; // forward declaration

class ToolDispatcher {
public:
    ToolDispatcher();

    // Set the memory store reference (for memory_store/memory_read tools)
    void setMemoryStore(MemoryStore *mem);

    // Process a single tool_call from the LLM.
    // toolCall is a JsonObject with "id", "function.name", "function.arguments".
    // Returns a JsonDocument with the tool result:
    // {"tool_call_id": "...", "role": "tool", "content": "..."}
    DynamicJsonDocument executeTool(JsonObject toolCall);

    // Get the tools JSON array to include in the chat completion request.
    // This tells the LLM what tools are available.
    String getToolsDefinition() const;

    // Check if a response contains tool_calls (pass the root doc)
    static bool hasToolCalls(DynamicJsonDocument &response);

    // Extract tool_calls array from a response
    static JsonArray getToolCalls(DynamicJsonDocument &response);

private:
    MemoryStore *_memory;

    // Individual tool implementations
    String _gpioRead(JsonObject args);
    String _gpioWrite(JsonObject args);
    String _gpioMode(JsonObject args);
    String _pwmWrite(JsonObject args);
    String _adcRead(JsonObject args);
    String _i2cScan(JsonObject args);
    String _systemInfo(JsonObject args);
    String _memoryStore(JsonObject args);
    String _memoryRead(JsonObject args);

    // Safety check — returns true if pin is safe to use
    bool _isPinSafe(int pin, const char *action) const;
};
