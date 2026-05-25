#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// CronManager — FreeRTOS timer-based scheduler for the ESP32 AI agent.
//
// Like Hermes cron jobs but micro-scale:
//   - Schedules Telegram messages at fixed intervals
//   - Jobs stored in NVS (survive reboots)
//   - Max 8 concurrent jobs (FreeRTOS timer constraint)
//   - Intervals: minutes or hours (min 1 minute)
//   - Jobs can be one-shot or recurring
//
// ESP32 constraints:
//   - FreeRTOS software timers run in the timer daemon task (low priority)
//   - Timer callbacks must be ISR-safe (no blocking, no HTTP)
//   - So: callback sets a flag, loop() sends the actual message
//   - Max 8 jobs to stay within timer memory limits
// ---------------------------------------------------------------------------

#define CRON_MAX_JOBS       8
#define CRON_MAX_NAME       16
#define CRON_MAX_MESSAGE    128
#define CRON_MIN_INTERVAL   60000   // 1 minute minimum (ms)

class CronManager {
public:
    struct CronJob {
        char name[CRON_MAX_NAME];
        char chatId[32];      // Telegram chat ID
        char message[CRON_MAX_MESSAGE];
        unsigned long intervalMs;
        bool recurring;
        bool active;
        bool pending;          // set by timer callback, cleared by poll()
        TimerHandle_t timer;   // FreeRTOS software timer handle
    };

    CronManager();

    // Initialize the cron system. Call once in setup().
    bool begin();

    // Call in loop() — checks for pending messages and sends them.
    // Returns the chatId + message to send, or empty string if nothing pending.
    String poll();

    // Add a cron job. Returns human-readable status.
    // intervalMs: repeat interval in ms (min 60000 = 1 min)
    // chatId: Telegram chat to send to
    // message: text to send
    // recurring: true = repeat, false = one-shot
    String addJob(const String &name, unsigned long intervalMs,
                  const String &chatId, const String &message,
                  bool recurring = true);

    // Remove a job by name
    String removeJob(const String &name);

    // List all jobs
    String list() const;

    // Pause/resume a job
    String pauseJob(const String &name);
    String resumeJob(const String &name);

    // Get number of active jobs
    int count() const;

private:
 int _jobCount;
 bool _ready;

 CronJob _jobs[CRON_MAX_JOBS];

 // Find job index by name (-1 if not found)
 int _findJob(const char *name) const;

 // Persist jobs to NVS
    void _saveJobs();
    void _loadJobs();
};
