#include "MemoryStore.h"

// ---------------------------------------------------------------------------
// NVS iterator callback context structs
// ---------------------------------------------------------------------------

struct ListCtx {
    String result;
    int count;
};

struct CountCtx {
    int count;
};

struct InjectionCtx {
    String result;
    int count;
};

// ---------------------------------------------------------------------------
// MemoryStore implementation
// ---------------------------------------------------------------------------

MemoryStore::MemoryStore(const char *nvsNamespace)
    : _nsName(nvsNamespace), _handle(0), _opened(false) {}

bool MemoryStore::begin() {
    // nvs_flash_init() returns ESP_OK on success (including when already initialized)
    // or an error code on failure
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        Serial.printf("[MemoryStore] NVS init failed: 0x%x\n", err);
        // Try erasing and re-initializing (corrupted NVS)
        Serial.println("[MemoryStore] Attempting NVS erase + reinit...");
        nvs_flash_erase();
        err = nvs_flash_init();
        if (err != ESP_OK) {
            Serial.printf("[MemoryStore] NVS reinit failed: 0x%x\n", err);
            return false;
        }
    }
    Serial.println("[MemoryStore] NVS initialized");
    return true;
}

String MemoryStore::_sanitizeKey(const String &key) const {
    String k = key;
    k.trim();
    // NVS keys: max 15 chars, alphanumeric + underscores/hyphens
    String sanitized;
    for (size_t i = 0; i < k.length() && sanitized.length() < MEMORY_MAX_KEY_LEN; i++) {
        char c = k[i];
        if (isalnum(c) || c == '_' || c == '-') {
            sanitized += c;
        }
    }
    return sanitized.length() > 0 ? sanitized : "mem";
}

bool MemoryStore::_open() {
    if (_opened) return true;
    esp_err_t err = nvs_open(_nsName, NVS_READWRITE, &_handle);
    if (err != ESP_OK) {
        Serial.printf("[MemoryStore] Failed to open NVS namespace '%s': 0x%x\n", _nsName, err);
        return false;
    }
    _opened = true;
    return true;
}

void MemoryStore::_close() {
    if (_opened) {
        nvs_commit(_handle);
        nvs_close(_handle);
        _opened = false;
    }
}

bool MemoryStore::remember(const String &key, const String &value) {
    String k = _sanitizeKey(key);
    if (k.length() == 0) return false;

    if (!_open()) return false;

    // Truncate value to NVS blob limit
    String v = value;
    if (v.length() > MEMORY_MAX_VALUE_LEN) {
        v = v.substring(0, MEMORY_MAX_VALUE_LEN);
    }

    esp_err_t err = nvs_set_str(_handle, k.c_str(), v.c_str());
    if (err != ESP_OK) {
        Serial.printf("[MemoryStore] Failed to set '%s': 0x%x\n", k.c_str(), err);
        _close();
        return false;
    }

    err = nvs_commit(_handle);
    _close();

    if (err == ESP_OK) {
        Serial.printf("[MemoryStore] Remembered: %s = %s\n", k.c_str(), v.substring(0, 60).c_str());
        return true;
    }
    return false;
}

bool MemoryStore::forget(const String &key) {
    String k = _sanitizeKey(key);
    if (k.length() == 0) return false;

    if (!_open()) return false;

    // Check if key exists first
    size_t requiredSize = 0;
    esp_err_t err = nvs_get_str(_handle, k.c_str(), nullptr, &requiredSize);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        _close();
        return false;
    }

    err = nvs_erase_key(_handle, k.c_str());
    if (err != ESP_OK) {
        Serial.printf("[MemoryStore] Failed to erase '%s': 0x%x\n", k.c_str(), err);
        _close();
        return false;
    }

    nvs_commit(_handle);
    _close();

    Serial.printf("[MemoryStore] Forgot: %s\n", k.c_str());
    return true;
}

String MemoryStore::recall(const String &key) const {
    String k = _sanitizeKey(key);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(_nsName, NVS_READONLY, &handle);
    if (err != ESP_OK) return "";

    size_t requiredSize = 0;
    err = nvs_get_str(handle, k.c_str(), nullptr, &requiredSize);
    if (err != ESP_OK) {
        nvs_close(handle);
        return "";
    }

    char *buf = new char[requiredSize];
    err = nvs_get_str(handle, k.c_str(), buf, &requiredSize);
    nvs_close(handle);

    if (err != ESP_OK) {
        delete[] buf;
        return "";
    }

    String result(buf);
    delete[] buf;
    return result;
}

int MemoryStore::_iterateKeys(void (*callback)(const char *key, void *ctx), void *ctx) const {
    nvs_iterator_t it = nvs_entry_find("nvs", _nsName, NVS_TYPE_STR);
    int count = 0;

    while (it != nullptr) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        callback(info.key, ctx);
        count++;
        it = nvs_entry_next(it);
    }
    // NULL iterator is auto-released per IDF docs
    return count;
}

String MemoryStore::list() const {
    ListCtx ctx;
    ctx.count = 0;

    _iterateKeys([](const char *key, void *userData) {
        ListCtx *c = static_cast<ListCtx *>(userData);
        if (c->count > 0) c->result += "\n";

        nvs_handle_t h;
        if (nvs_open("agent_mem", NVS_READONLY, &h) == ESP_OK) {
            size_t sz = 0;
            if (nvs_get_str(h, key, nullptr, &sz) == ESP_OK && sz > 0) {
                char *buf = new char[sz];
                if (nvs_get_str(h, key, buf, &sz) == ESP_OK) {
                    c->result += String(key) + " = " + String(buf);
                }
                delete[] buf;
            }
            nvs_close(h);
        }
        c->count++;
    }, &ctx);

    return ctx.result;
}

int MemoryStore::count() const {
    CountCtx ctx;
    ctx.count = 0;
    _iterateKeys([](const char *key, void *userData) {
        (void)key;
        static_cast<CountCtx *>(userData)->count++;
    }, &ctx);
    return ctx.count;
}

String MemoryStore::buildSystemInjection() const {
    InjectionCtx ctx;
    ctx.count = 0;

    _iterateKeys([](const char *key, void *userData) {
        InjectionCtx *c = static_cast<InjectionCtx *>(userData);
        if (c->count >= MEMORY_MAX_PROMPT_INJECT) return;

        nvs_handle_t h;
        if (nvs_open("agent_mem", NVS_READONLY, &h) == ESP_OK) {
            size_t sz = 0;
            if (nvs_get_str(h, key, nullptr, &sz) == ESP_OK && sz > 0) {
                char *buf = new char[sz];
                if (nvs_get_str(h, key, buf, &sz) == ESP_OK) {
                    String entry = "- " + String(key) + ": " + String(buf) + "\n";
                    if (c->result.length() + entry.length() < MEMORY_INJECTION_MAX_LEN) {
                        c->result += entry;
                    }
                }
                delete[] buf;
            }
            nvs_close(h);
        }
        c->count++;
    }, &ctx);

    if (ctx.result.length() == 0) return "";

    return "Your persistent memories:\n" + ctx.result;
}

int MemoryStore::clearAll() {
    int cleared = 0;

    nvs_iterator_t it = nvs_entry_find("nvs", _nsName, NVS_TYPE_STR);

    // Collect keys into a small buffer
    String keys[50];
    int keyCount = 0;

    while (it != nullptr && keyCount < 50) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        keys[keyCount++] = String(info.key);
        it = nvs_entry_next(it);
    }

    if (keyCount > 0) {
        if (!_open()) return 0;
        for (int i = 0; i < keyCount; i++) {
            if (nvs_erase_key(_handle, keys[i].c_str()) == ESP_OK) {
                cleared++;
            }
        }
        nvs_commit(_handle);
        _close();
    }

    Serial.printf("[MemoryStore] Cleared %d memories\n", cleared);
    return cleared;
}
