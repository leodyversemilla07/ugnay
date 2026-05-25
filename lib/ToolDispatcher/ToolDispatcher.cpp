#include "ToolDispatcher.h"
#include <MemoryStore.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
// Pin safety table for ESP32 DevKit
// ---------------------------------------------------------------------------

struct PinInfo {
    int pin;
    const char *note;
    bool inputSafe;
    bool outputSafe;
};

static const PinInfo pinTable[] = {
    {0,  "BOOT button (pull-up)",           true,  false},
    {1,  "TX0 (serial)",                    true,  false},
    {2,  "Onboard LED / boot strapping",    true,  true},
    {3,  "RX0 (serial)",                    false, false},
    {4,  "General purpose",                 true,  true},
    {5,  "Boot strapping (PWM)",            true,  true},
    {12, "Boot strapping (MISO)",           true,  true},
    {13, "General purpose (LED on some)",   true,  true},
    {14, "Boot strapping (CLK)",            true,  true},
    {15, "Boot strapping (CS0)",            true,  true},
    {16, "General purpose (PSRAM on WROVER)", true, true},
    {17, "General purpose (PSRAM on WROVER)", true, true},
    {18, "General purpose",                 true,  true},
    {19, "General purpose",                 true,  true},
    {21, "SDA (I2C default)",               true,  true},
    {22, "SCL (I2C default)",               true,  true},
    {23, "General purpose",                 true,  true},
    {25, "General purpose (DAC1)",          true,  true},
    {26, "General purpose (DAC2)",          true,  true},
    {27, "General purpose",                 true,  true},
    {32, "ADC1_CH4 / General purpose",      true,  true},
    {33, "ADC1_CH5 / General purpose",      true,  true},
    {34, "ADC1_CH6 (input only)",           true,  false},
    {35, "ADC1_CH7 (input only)",           true,  false},
    {36, "ADC1_CH0 (input only / VP)",      true,  false},
    {39, "ADC1_CH3 (input only / VN)",      true,  false},
};
#define PIN_TABLE_SIZE (sizeof(pinTable) / sizeof(pinTable[0]))

// ---------------------------------------------------------------------------
// ToolDispatcher implementation
// ---------------------------------------------------------------------------

ToolDispatcher::ToolDispatcher() : _memory(nullptr) {}

void ToolDispatcher::setMemoryStore(MemoryStore *mem) {
    _memory = mem;
}

bool ToolDispatcher::_isPinSafe(int pin, const char *action) const {
    for (size_t i = 0; i < PIN_TABLE_SIZE; i++) {
        if (pinTable[i].pin == pin) {
            if (strcmp(action, "output") == 0 || strcmp(action, "write") == 0) {
                if (!pinTable[i].outputSafe) {
                    Serial.printf("[ToolDispatcher] Pin %d unsafe for output: %s\n", pin, pinTable[i].note);
                    return false;
                }
            }
            return true;
        }
    }
    Serial.printf("[ToolDispatcher] Pin %d not in safety table - allowing with caution\n", pin);
    return true;
}

String ToolDispatcher::_gpioRead(JsonObject args) {
    int pin = args["pin"] | -1;
    if (pin < 0) return "{\"error\":\"Missing pin parameter\"}";

    if (!_isPinSafe(pin, "read")) {
        return "{\"error\":\"Pin " + String(pin) + " is not safe for reading\"}";
    }

    int value = digitalRead(pin);
    return "{\"pin\":" + String(pin) + ",\"value\":" + String(value) +
           ",\"state\":\"" + (value ? "HIGH" : "LOW") + "\"}";
}

String ToolDispatcher::_gpioWrite(JsonObject args) {
    int pin = args["pin"] | -1;
    int value = args["value"] | -1;
    if (pin < 0) return "{\"error\":\"Missing pin parameter\"}";
    if (value < 0 || value > 1) return "{\"error\":\"Value must be 0 or 1\"}";

    if (!_isPinSafe(pin, "write")) {
        return "{\"error\":\"Pin " + String(pin) + " is not safe for writing\"}";
    }

    pinMode(pin, OUTPUT);
    digitalWrite(pin, value);
    return "{\"pin\":" + String(pin) + ",\"value\":" + String(value) +
           ",\"state\":\"" + (value ? "HIGH" : "LOW") + "\",\"result\":\"ok\"}";
}

String ToolDispatcher::_gpioMode(JsonObject args) {
    int pin = args["pin"] | -1;
    String mode = args["mode"] | "";

    if (pin < 0) return "{\"error\":\"Missing pin parameter\"}";
    if (mode.length() == 0) return "{\"error\":\"Missing mode parameter\"}";

    if (mode == "output") {
        if (!_isPinSafe(pin, "output")) {
            return "{\"error\":\"Pin " + String(pin) + " not safe as output\"}";
        }
        pinMode(pin, OUTPUT);
    } else if (mode == "input") {
        pinMode(pin, INPUT);
    } else if (mode == "input_pullup") {
        pinMode(pin, INPUT_PULLUP);
    } else if (mode == "input_pulldown") {
        pinMode(pin, INPUT_PULLDOWN);
    } else {
        return "{\"error\":\"Unknown mode: " + mode + "\"}";
    }

    return "{\"pin\":" + String(pin) + ",\"mode\":\"" + mode + "\",\"result\":\"ok\"}";
}

String ToolDispatcher::_pwmWrite(JsonObject args) {
    int pin = args["pin"] | -1;
    int channel = args["channel"] | 0;
    int duty = args["duty"] | 0;
    int freq = args["freq"] | 5000;
    int resolution = args["resolution"] | 8;

    if (pin < 0) return "{\"error\":\"Missing pin parameter\"}";
    if (!_isPinSafe(pin, "output")) {
        return "{\"error\":\"Pin " + String(pin) + " not safe for PWM\"}";
    }
    if (channel < 0 || channel > 15) return "{\"error\":\"Channel must be 0-15\"}";

    ledcSetup(channel, freq, resolution);
    ledcAttachPin(pin, channel);
    ledcWrite(channel, duty);

    char buf[200];
    snprintf(buf, sizeof(buf),
        "{\"pin\":%d,\"channel\":%d,\"duty\":%d,\"freq\":%d,\"resolution\":%d,\"result\":\"ok\"}",
        pin, channel, duty, freq, resolution);
    return String(buf);
}

String ToolDispatcher::_adcRead(JsonObject args) {
    int pin = args["pin"] | -1;
    if (pin < 0) return "{\"error\":\"Missing pin parameter\"}";

    if (pin < 32 || pin > 39) {
        return "{\"error\":\"ADC1 pins are 32-39 only\"}";
    }

    int raw = analogRead(pin);
    float voltage = (raw / 4095.0) * 3.3;

    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"pin\":%d,\"raw\":%d,\"voltage\":%.2f,\"resolution\":\"12-bit\"}",
        pin, raw, voltage);
    return String(buf);
}

String ToolDispatcher::_i2cScan(JsonObject args) {
    int sda = args["sda"] | 21;
    int scl = args["scl"] | 22;

    Wire.begin(sda, scl);

    String result = "{\"sda\":" + String(sda) + ",\"scl\":" + String(scl) + ",\"devices\":[";
    int found = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            if (found > 0) result += ",";
            char addrBuf[8];
            snprintf(addrBuf, sizeof(addrBuf), "\"0x%02X\"", addr);
            result += String(addrBuf);
            found++;
        }
    }

    result += "],\"count\":" + String(found) + "}";
    Wire.end();
    return result;
}

String ToolDispatcher::_systemInfo(JsonObject args) {
    (void)args;

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flashSize = 0;
    esp_flash_get_size(nullptr, &flashSize);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"chip\":\"ESP32\",\"cores\":%d,\"revision\":%d,"
        "\"flash_mb\":%u,\"free_heap\":%u,\"min_free_heap\":%u,"
        "\"psram\":%u,\"uptime_sec\":%lu}",
        chip.cores, chip.revision,
        flashSize / (1024 * 1024), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
        ESP.getPsramSize(), millis() / 1000);
    return String(buf);
}

String ToolDispatcher::_memoryStore(JsonObject args) {
    if (!_memory) return "{\"error\":\"MemoryStore not initialized\"}";

    String key = args["key"] | "";
    String value = args["value"] | "";
    if (key.length() == 0) return "{\"error\":\"Missing key parameter\"}";

    if (_memory->remember(key, value)) {
        return "{\"key\":\"" + key + "\",\"result\":\"stored\"}";
    }
    return "{\"error\":\"Failed to store memory\"}";
}

String ToolDispatcher::_memoryRead(JsonObject args) {
    if (!_memory) return "{\"error\":\"MemoryStore not initialized\"}";

    String key = args["key"] | "";
    if (key.length() == 0) return "{\"error\":\"Missing key parameter\"}";

    String value = _memory->recall(key);
    if (value.length() == 0) {
        return "{\"key\":\"" + key + "\",\"found\":false}";
    }
    return "{\"key\":\"" + key + "\",\"value\":\"" + value + "\",\"found\":true}";
}

// ---------------------------------------------------------------------------
// Tool execution router
// ---------------------------------------------------------------------------

DynamicJsonDocument ToolDispatcher::executeTool(JsonObject toolCall) {
    String funcName = toolCall["function"]["name"] | "";
    String callId = toolCall["id"] | "";

    DynamicJsonDocument resultDoc(1024);

    String argsStr = toolCall["function"]["arguments"] | "{}";
    DynamicJsonDocument argsDoc(512);
    deserializeJson(argsDoc, argsStr);
    JsonObject args = argsDoc.as<JsonObject>();

    String content;

    if (funcName == "gpio_read") {
        content = _gpioRead(args);
    } else if (funcName == "gpio_write") {
        content = _gpioWrite(args);
    } else if (funcName == "gpio_mode") {
        content = _gpioMode(args);
    } else if (funcName == "pwm_write") {
        content = _pwmWrite(args);
    } else if (funcName == "adc_read") {
        content = _adcRead(args);
    } else if (funcName == "i2c_scan") {
        content = _i2cScan(args);
    } else if (funcName == "system_info") {
        content = _systemInfo(args);
    } else if (funcName == "memory_store") {
        content = _memoryStore(args);
    } else if (funcName == "memory_read") {
        content = _memoryRead(args);
    } else {
        content = "{\"error\":\"Unknown tool: " + funcName + "\"}";
    }

    Serial.printf("[ToolDispatcher] %s -> %s\n", funcName.c_str(), content.substring(0, 80).c_str());

    resultDoc["tool_call_id"] = callId;
    resultDoc["role"] = "tool";
    resultDoc["content"] = content;

    return resultDoc;
}

// ---------------------------------------------------------------------------
// Tools definition (for the chat completion request)
// ---------------------------------------------------------------------------

String ToolDispatcher::getToolsDefinition() const {
    DynamicJsonDocument doc(4096);
    JsonArray tools = doc.to<JsonArray>();

    // gpio_read
    {
        JsonObject t = tools.createNestedObject();
        t["type"] = "function";
        JsonObject func = t["function"].to<JsonObject>();
        func["name"] = "gpio_read";
        func["description"] = "Read the digital state of a GPIO pin (HIGH=1 or LOW=0)";
        JsonObject params = func["parameters"].to<JsonObject>();
        params["type"] = "object";
        JsonObject props = params["properties"].to<JsonObject>();
        JsonObject pPin = props["pin"].to<JsonObject>();
        pPin["type"] = "integer";
        pPin["description"] = "GPIO pin number (2, 4, 12-19, 21-23, 25-27, 32-39)";
        JsonArray req = params.createNestedArray("required");
        req.add("pin");
    }

    // gpio_write
    {
        JsonObject t = tools.createNestedObject();
        t["type"] = "function";
        JsonObject func = t["function"].to<JsonObject>();
        func["name"] = "gpio_write";
        func["description"] = "Write a digital value (0=LOW, 1=HIGH) to a GPIO pin";
        JsonObject params = func["parameters"].to<JsonObject>();
        params["type"] = "object";
        JsonObject props = params["properties"].to<JsonObject>();
        JsonObject pPin = props["pin"].to<JsonObject>();
        pPin["type"] = "integer";
        pPin["description"] = "GPIO pin number";
        JsonObject pVal = props["value"].to<JsonObject>();
        pVal["type"] = "integer";
        pVal["description"] = "0 for LOW, 1 for HIGH";
        JsonArray req = params.createNestedArray("required");
        req.add("pin");
        req.add("value");
    }

    // gpio_mode
    {
        JsonObject t = tools.createNestedObject();
        t["type"] = "function";
        JsonObject func = t["function"].to<JsonObject>();
        func["name"] = "gpio_mode";
        func["description"] = "Set GPIO pin mode: output, input, input_pullup, input_pulldown";
        JsonObject params = func["parameters"].to<JsonObject>();
        params["type"] = "object";
        JsonObject props = params["properties"].to<JsonObject>();
        JsonObject pPin = props["pin"].to<JsonObject>();
        pPin["type"] = "integer";
        pPin["description"] = "GPIO pin number";
        JsonObject pMode = props["mode"].to<JsonObject>();
        pMode["type"] = "string";
        pMode["description"] = "Pin mode: output, input, input_pullup, input_pulldown";
        JsonArray req = params.createNestedArray("required");
        req.add("pin");
        req.add("mode");
    }

    // pwm_write
    {
        JsonObject t = tools.createNestedObject();
        t["type"] = "function";
        JsonObject func = t["function"].to<JsonObject>();
        func["name"] = "pwm_write";
        func["description"] = "Write a PWM signal to a GPIO pin using ESP32 LEDC. Good for LED dimming or servo control.";
        JsonObject params = func["parameters"].to<JsonObject>();
        params["type"] = "object";
        JsonObject props = params["properties"].to<JsonObject>();
        JsonObject pPin = props["pin"].to<JsonObject>();
        pPin["type"] = "integer";
        pPin["description"] = "GPIO pin number";
        JsonObject pCh = props["channel"].to<JsonObject>();
        pCh["type"] = "integer";
        pCh["description"] = "LEDC channel (0-15). Default: 0";
        JsonObject pDuty = props["duty"].to<JsonObject>();
        pDuty["type"] = "integer";
        pDuty["description"] = "Duty cycle. 8-bit: 0-255. Default: 128";
        JsonObject pFreq = props["freq"].to<JsonObject>();
        pFreq["type"] = "integer";
        pFreq["description"] = "Frequency in Hz. Default: 5000";
        JsonArray req = params.createNestedArray("required");
        req.add("pin");
        req.add("duty");
    }

    // adc_read
    {
        JsonObject t = tools.createNestedObject();
        t["type"] = "function";
        JsonObject func = t["function"].to<JsonObject>();
        func["name"] = "adc_read";
        func["description"] = "Read analog value from ADC1 pin (32-39). Returns raw 12-bit value (0-4095) and voltage.";
        JsonObject params = func["parameters"].to<JsonObject>();
        params["type"] = "object";
        JsonObject props = params["properties"].to<JsonObject>();
        JsonObject pPin = props["pin"].to<JsonObject>();
        pPin["type"] = "integer";
        pPin["description"] = "ADC1 pin (32-39). 34-39 are input-only.";
        JsonArray req = params.createNestedArray("required");
        req.add("pin");
    }

    // i2c_scan
    {
        JsonObject t = tools.createNestedObject();
        t["type"] = "function";
        JsonObject func = t["function"].to<JsonObject>();
        func["name"] = "i2c_scan";
        func["description"] = "Scan the I2C bus for connected devices. Returns list of addresses found.";
        JsonObject params = func["parameters"].to<JsonObject>();
        params["type"] = "object";
        JsonObject props = params["properties"].to<JsonObject>();
        JsonObject pSda = props["sda"].to<JsonObject>();
        pSda["type"] = "integer";
        pSda["description"] = "I2C SDA pin. Default: 21";
        JsonObject pScl = props["scl"].to<JsonObject>();
        pScl["type"] = "integer";
        pScl["description"] = "I2C SCL pin. Default: 22";
    }

    // system_info
    {
        JsonObject t = tools.createNestedObject();
        t["type"] = "function";
        JsonObject func = t["function"].to<JsonObject>();
        func["name"] = "system_info";
        func["description"] = "Get ESP32 system info: chip, cores, heap, flash, uptime";
        JsonObject params = func["parameters"].to<JsonObject>();
        params["type"] = "object";
    }

    // memory_store
    {
        JsonObject t = tools.createNestedObject();
        t["type"] = "function";
        JsonObject func = t["function"].to<JsonObject>();
        func["name"] = "memory_store";
        func["description"] = "Store a key-value pair in persistent NVS memory. Survives reboots. Max key: 15 chars, max value: 400 bytes.";
        JsonObject params = func["parameters"].to<JsonObject>();
        params["type"] = "object";
        JsonObject props = params["properties"].to<JsonObject>();
        JsonObject pKey = props["key"].to<JsonObject>();
        pKey["type"] = "string";
        pKey["description"] = "Memory key (max 15 chars)";
        JsonObject pVal = props["value"].to<JsonObject>();
        pVal["type"] = "string";
        pVal["description"] = "Memory value (max 400 chars)";
        JsonArray req = params.createNestedArray("required");
        req.add("key");
        req.add("value");
    }

    // memory_read
    {
        JsonObject t = tools.createNestedObject();
        t["type"] = "function";
        JsonObject func = t["function"].to<JsonObject>();
        func["name"] = "memory_read";
        func["description"] = "Read a value from persistent NVS memory by key.";
        JsonObject params = func["parameters"].to<JsonObject>();
        params["type"] = "object";
        JsonObject props = params["properties"].to<JsonObject>();
        JsonObject pKey = props["key"].to<JsonObject>();
        pKey["type"] = "string";
        pKey["description"] = "Memory key to look up";
        JsonArray req = params.createNestedArray("required");
        req.add("key");
    }

    String output;
    serializeJson(doc, output);
    return output;
}

bool ToolDispatcher::hasToolCalls(DynamicJsonDocument &response) {
    return response["choices"][0]["message"].containsKey("tool_calls") &&
           response["choices"][0]["message"]["tool_calls"].size() > 0;
}

JsonArray ToolDispatcher::getToolCalls(DynamicJsonDocument &response) {
    return response["choices"][0]["message"]["tool_calls"].as<JsonArray>();
}
