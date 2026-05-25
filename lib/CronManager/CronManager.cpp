#include "CronManager.h"
#include <nvs.h>
#include <nvs_flash.h>

#define CRON_NVS_NAMESPACE "cron_cfg"

// ---------------------------------------------------------------------------
// Global job pointer for timer callback access
// (FreeRTOS software timer callbacks can't access member data directly,
//  so we use a module-level pointer set during begin())
// ---------------------------------------------------------------------------

static CronManager::CronJob *g_jobs = nullptr;

static void cronTimerCallback(TimerHandle_t timer) {
    int idx = (int)(uintptr_t)pvTimerGetTimerID(timer);
    if (g_jobs && idx >= 0 && idx < CRON_MAX_JOBS) {
        g_jobs[idx].pending = true;
    }
}

// ---------------------------------------------------------------------------
// Construction & init
// ---------------------------------------------------------------------------

CronManager::CronManager() : _jobCount(0), _ready(false) {
    memset(_jobs, 0, sizeof(_jobs));
}

bool CronManager::begin() {
    _ready = true;
    g_jobs = _jobs; // Set global pointer for timer callbacks

    _loadJobs();

    // Recreate FreeRTOS timers for persisted jobs
    for (int i = 0; i < _jobCount; i++) {
        _jobs[i].pending = false;
        _jobs[i].timer = nullptr;

        if (_jobs[i].active && _jobs[i].intervalMs >= CRON_MIN_INTERVAL) {
            _jobs[i].timer = xTimerCreate(
                _jobs[i].name,
                pdMS_TO_TICKS(_jobs[i].intervalMs),
                _jobs[i].recurring ? pdTRUE : pdFALSE,
                (void *)(uintptr_t)i,
                cronTimerCallback
            );
            if (_jobs[i].timer) {
                xTimerStart(_jobs[i].timer, 0);
                Serial.printf("[Cron] Restored '%s' every %lu ms\n",
                    _jobs[i].name, _jobs[i].intervalMs);
            }
        }
    }

    Serial.printf("[Cron] Initialized: %d job(s)\n", _jobCount);
    return true;
}

// ---------------------------------------------------------------------------
// Poll — call from loop() to dispatch pending messages
// ---------------------------------------------------------------------------

String CronManager::poll() {
    if (!_ready) return "";

    for (int i = 0; i < _jobCount; i++) {
        if (_jobs[i].pending) {
            _jobs[i].pending = false;

            // Return format: "CHAT_ID|MESSAGE"
            String result = String(_jobs[i].chatId) + "|" + String(_jobs[i].message);

            // One-shot cleanup
            if (!_jobs[i].recurring) {
                if (_jobs[i].timer) {
                    xTimerDelete(_jobs[i].timer, 0);
                    _jobs[i].timer = nullptr;
                }
                _jobs[i].active = false;
                _saveJobs();
            }

            return result;
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// Job management
// ---------------------------------------------------------------------------

int CronManager::_findJob(const char *name) const {
    for (int i = 0; i < _jobCount; i++) {
        if (strcasecmp(_jobs[i].name, name) == 0) return i;
    }
    return -1;
}

String CronManager::addJob(const String &name, unsigned long intervalMs,
                           const String &chatId, const String &message,
                           bool recurring) {
    if (!_ready) return "Cron not initialized.";
    if (name.length() == 0 || name.length() >= CRON_MAX_NAME)
        return "Name must be 1-" + String(CRON_MAX_NAME - 1) + " chars.";
    if (intervalMs < CRON_MIN_INTERVAL)
        return "Min interval: 1 minute.";
    if (message.length() >= CRON_MAX_MESSAGE)
        return "Message too long (max " + String(CRON_MAX_MESSAGE - 1) + ").";

    // Update existing or create new
    int idx = _findJob(name.c_str());

    if (idx >= 0) {
        // Delete old timer
        if (_jobs[idx].timer) {
            xTimerDelete(_jobs[idx].timer, 0);
            _jobs[idx].timer = nullptr;
        }
        strncpy(_jobs[idx].chatId, chatId.c_str(), 31);
        _jobs[idx].chatId[31] = '\0';
        strncpy(_jobs[idx].message, message.c_str(), CRON_MAX_MESSAGE - 1);
        _jobs[idx].message[CRON_MAX_MESSAGE - 1] = '\0';
        _jobs[idx].intervalMs = intervalMs;
        _jobs[idx].recurring = recurring;
        _jobs[idx].active = true;
        _jobs[idx].pending = false;
    } else {
        if (_jobCount >= CRON_MAX_JOBS)
            return "Max " + String(CRON_MAX_JOBS) + " jobs. Remove one first.";

        idx = _jobCount;
        memset(&_jobs[idx], 0, sizeof(CronJob));
        strncpy(_jobs[idx].name, name.c_str(), CRON_MAX_NAME - 1);
        strncpy(_jobs[idx].chatId, chatId.c_str(), 31);
        strncpy(_jobs[idx].message, message.c_str(), CRON_MAX_MESSAGE - 1);
        _jobs[idx].intervalMs = intervalMs;
        _jobs[idx].recurring = recurring;
        _jobs[idx].active = true;
        _jobs[idx].pending = false;
        _jobCount++;
    }

    // Create FreeRTOS timer
    _jobs[idx].timer = xTimerCreate(
        _jobs[idx].name,
        pdMS_TO_TICKS(intervalMs),
        recurring ? pdTRUE : pdFALSE,
        (void *)(uintptr_t)idx,
        cronTimerCallback
    );
    if (_jobs[idx].timer) xTimerStart(_jobs[idx].timer, 0);

    _saveJobs();

    String iStr = (intervalMs >= 3600000)
        ? String(intervalMs / 3600000) + "h"
        : String(intervalMs / 60000) + "m";

    return "Job '" + name + "' every " + iStr +
           (recurring ? " (recurring)" : " (one-shot)");
}

String CronManager::removeJob(const String &name) {
    if (!_ready) return "Cron not initialized.";

    int idx = _findJob(name.c_str());
    if (idx < 0) return "Job '" + name + "' not found.";

    if (_jobs[idx].timer) xTimerDelete(_jobs[idx].timer, 0);

    // Shift remaining jobs down
    for (int i = idx; i < _jobCount - 1; i++) {
        _jobs[i] = _jobs[i + 1];
        // Fix timer ID for shifted job
        if (_jobs[i].timer) {
            vTimerSetTimerID(_jobs[i].timer, (void *)(uintptr_t)i);
        }
    }
    memset(&_jobs[_jobCount - 1], 0, sizeof(CronJob));
    _jobCount--;

    _saveJobs();
    return "Job '" + name + "' removed.";
}

String CronManager::list() const {
    if (!_ready) return "Cron not initialized.";
    if (_jobCount == 0) return "No cron jobs. Add with /cron add";

    String result = "Cron (" + String(_jobCount) + "):\n";
    for (int i = 0; i < _jobCount; i++) {
        String iStr = (_jobs[i].intervalMs >= 3600000)
            ? String(_jobs[i].intervalMs / 3600000) + "h"
            : String(_jobs[i].intervalMs / 60000) + "m";
        result += "  " + String(_jobs[i].name);
        result += _jobs[i].active ? " [ON] " : " [OFF] ";
        result += "every " + iStr;
        result += _jobs[i].recurring ? " recur" : " once";
        result += ": " + String(_jobs[i].message) + "\n";
    }
    return result;
}

String CronManager::pauseJob(const String &name) {
    if (!_ready) return "Cron not initialized.";
    int idx = _findJob(name.c_str());
    if (idx < 0) return "Job '" + name + "' not found.";
    if (_jobs[idx].timer) xTimerStop(_jobs[idx].timer, 0);
    _jobs[idx].active = false;
    _saveJobs();
    return "Job '" + name + "' paused.";
}

String CronManager::resumeJob(const String &name) {
    if (!_ready) return "Cron not initialized.";
    int idx = _findJob(name.c_str());
    if (idx < 0) return "Job '" + name + "' not found.";
    _jobs[idx].active = true;
    if (_jobs[idx].timer) xTimerStart(_jobs[idx].timer, 0);
    _saveJobs();
    return "Job '" + name + "' resumed.";
}

int CronManager::count() const {
    return _jobCount;
}

// ---------------------------------------------------------------------------
// NVS persistence — pack/unpack each job as a blob
// ---------------------------------------------------------------------------

void CronManager::_saveJobs() {
    nvs_handle_t h;
    if (nvs_open(CRON_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_i32(h, "count", _jobCount);

    for (int i = 0; i < _jobCount; i++) {
        // Blob layout: name(16) + chatId(32) + message(128) + interval(4) + flags(2) = 182
        uint8_t blob[182];
        memset(blob, 0, sizeof(blob));
        memcpy(blob, _jobs[i].name, CRON_MAX_NAME);
        memcpy(blob + CRON_MAX_NAME, _jobs[i].chatId, 32);
        memcpy(blob + CRON_MAX_NAME + 32, _jobs[i].message, CRON_MAX_MESSAGE);
        uint32_t iv = (uint32_t)_jobs[i].intervalMs;
        memcpy(blob + CRON_MAX_NAME + 32 + CRON_MAX_MESSAGE, &iv, 4);
        blob[CRON_MAX_NAME + 32 + CRON_MAX_MESSAGE + 4] = _jobs[i].recurring ? 1 : 0;
        blob[CRON_MAX_NAME + 32 + CRON_MAX_MESSAGE + 5] = _jobs[i].active ? 1 : 0;

        String key = "j" + String(i);
        nvs_set_blob(h, key.c_str(), blob, sizeof(blob));
    }

    nvs_commit(h);
    nvs_close(h);
}

void CronManager::_loadJobs() {
    nvs_handle_t h;
    if (nvs_open(CRON_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        _jobCount = 0;
        return;
    }

    int32_t count = 0;
    if (nvs_get_i32(h, "count", &count) != ESP_OK) {
        nvs_close(h);
        _jobCount = 0;
        return;
    }

    _jobCount = (count > CRON_MAX_JOBS) ? CRON_MAX_JOBS : (count < 0 ? 0 : count);

    for (int i = 0; i < _jobCount; i++) {
        String key = "j" + String(i);
        uint8_t blob[182];
        size_t len = sizeof(blob);

        if (nvs_get_blob(h, key.c_str(), blob, &len) != ESP_OK || len != sizeof(blob)) {
            memset(&_jobs[i], 0, sizeof(CronJob));
            continue;
        }

        memcpy(_jobs[i].name, blob, CRON_MAX_NAME);
        _jobs[i].name[CRON_MAX_NAME - 1] = '\0';
        memcpy(_jobs[i].chatId, blob + CRON_MAX_NAME, 32);
        _jobs[i].chatId[31] = '\0';
        memcpy(_jobs[i].message, blob + CRON_MAX_NAME + 32, CRON_MAX_MESSAGE);
        _jobs[i].message[CRON_MAX_MESSAGE - 1] = '\0';
        uint32_t iv;
        memcpy(&iv, blob + CRON_MAX_NAME + 32 + CRON_MAX_MESSAGE, 4);
        _jobs[i].intervalMs = iv;
        _jobs[i].recurring = (blob[CRON_MAX_NAME + 32 + CRON_MAX_MESSAGE + 4] == 1);
        _jobs[i].active = (blob[CRON_MAX_NAME + 32 + CRON_MAX_MESSAGE + 5] == 1);
        _jobs[i].pending = false;
        _jobs[i].timer = nullptr; // Timers recreated in begin()
    }

    nvs_close(h);
}
