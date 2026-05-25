#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// SkillsManager — SPIFFS-backed skill definitions for the ESP32 AI agent.
//
// Skills are reusable instruction sets stored as JSON files in SPIFFS.
// Like Hermes skills but micro-scale: each skill is a JSON file with:
//   - name: skill identifier
//   - trigger: keywords that activate the skill
//   - instructions: what to inject into the system prompt
//   - enabled: whether the skill is active
//
// ESP32 constraints:
// - SPIFFS max ~1.5 MB on default partition
// - Each skill JSON: ~200-500 bytes
// - Max 16 skills (sufficient for embedded use)
// - Skills auto-inject into system prompt when trigger words match
// ---------------------------------------------------------------------------

#define SKILLS_MAX_NAME     32
#define SKILLS_MAX_TRIGGERS 64
#define SKILLS_MAX_INSTR    256
#define SKILLS_MAX_COUNT    16
#define SKILLS_DIR          "/skills"

class SkillsManager {
public:
    SkillsManager();

    // Initialize SPIFFS and load skill index. Call once in setup().
    bool begin();

    // Create a new skill from Telegram command params
    // Format: /skill create <name> <triggers> | <instructions>
    // Returns human-readable result message
    String create(const String &name, const String &triggers, const String &instructions);

    // Delete a skill by name
    String remove(const String &name);

    // List all skills (name + enabled status + triggers)
    String list() const;

    // Toggle a skill on/off
    String toggle(const String &name, bool enabled);

    // Get the combined instructions from all active skills whose
    // triggers match the given input text. Returns empty string if none match.
    String getMatchingInstructions(const String &inputText) const;

    // Get total number of skills
    int count() const;

    // Check if SPIFFS is initialized
    bool isReady() const { return _ready; }

private:
    bool _ready;

    // Skill entry (kept in RAM for fast matching — only 16 entries, ~500 bytes total)
    struct SkillEntry {
        char name[SKILLS_MAX_NAME];
        char triggers[SKILLS_MAX_TRIGGERS]; // comma-separated keywords
        char instructions[SKILLS_MAX_INSTR];
        bool enabled;
    };

    SkillEntry _skills[SKILLS_MAX_COUNT];
    int _skillCount;

    // Load all skills from SPIFFS into RAM
    bool _loadAll();

    // Save a single skill to SPIFFS
    bool _saveSkill(const SkillEntry &skill);

    // Delete a skill file from SPIFFS
    bool _deleteSkillFile(const char *name);

    // Find skill index by name (-1 if not found)
    int _findSkill(const char *name) const;

    // Check if any trigger word in the comma-separated list matches the input
    bool _triggersMatch(const char *triggers, const String &input) const;

    // Sanitize a name for use as a filename
    String _sanitizeName(const String &name) const;
};
