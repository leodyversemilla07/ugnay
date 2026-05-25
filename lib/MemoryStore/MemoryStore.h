#pragma once

#include <Arduino.h>
#include <nvs.h>
#include <nvs_flash.h>

// ---------------------------------------------------------------------------
// MemoryStore — NVS-backed persistent key-value memory for the ESP32 AI agent.
//
// Hermes-like memory system adapted for embedded constraints:
//   - Key-value pairs stored in NVS (flash-backed, survives reboot)
//   - /remember key=value  → store a memory
//   - /forget key          → delete a memory
//   - /memory              → list all memories
//   - Auto-inject into system prompt so the AI "remembers" context
//
// NVS constraints:
//   - Keys: max 15 chars
//   - Values: max 400 bytes (NVS page limit for string blobs)
//   - Total namespace: ~32 KB per namespace (plenty for agent memory)
//   - Zero RAM cost at rest — data lives in flash, loaded on demand
// ---------------------------------------------------------------------------

// Maximum single value length (NVS blob limit)
#define MEMORY_MAX_VALUE_LEN 400

// Maximum key length (NVS limit including null terminator)
#define MEMORY_MAX_KEY_LEN 15

// Maximum memories to inject into system prompt (to keep prompt size small)
#define MEMORY_MAX_PROMPT_INJECT 20

// Maximum length of the generated memory injection text
#define MEMORY_INJECTION_MAX_LEN 1024

class MemoryStore {
public:
    MemoryStore(const char *nvsNamespace = "agent_mem");

    // Call once in setup() to initialize NVS
    bool begin();

    // Store a memory. Key is truncated to 15 chars, value to 400 bytes.
    // Returns true on success.
    bool remember(const String &key, const String &value);

    // Delete a memory. Returns true if key existed and was deleted.
    bool forget(const String &key);

    // Get a memory value. Returns empty string if not found.
    String recall(const String &key) const;

    // Get all stored keys (for /memory listing)
    // Returns newline-separated "key = value" pairs
    String list() const;

    // Count of stored memories
    int count() const;

    // Build a text block to inject into the AI system prompt.
    // Format: "Your persistent memories:\n- key1: value1\n- key2: value2\n"
    // Returns empty string if no memories stored.
    String buildSystemInjection() const;

    // Clear all memories. Returns number of entries cleared.
    int clearAll();

private:
    const char *_nsName;
    nvs_handle_t _handle;
    bool _opened;

    // Iterate all keys in the namespace. Calls callback for each key.
    // Returns number of keys iterated.
    int _iterateKeys(void (*callback)(const char *key, void *ctx), void *ctx) const;

    // Open the NVS handle in read-write mode
    bool _open();
    void _close();

    // Truncate a key to NVS limit (15 chars + null)
    String _sanitizeKey(const String &key) const;
};
