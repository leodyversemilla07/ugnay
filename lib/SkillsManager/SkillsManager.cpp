#include "SkillsManager.h"

SkillsManager::SkillsManager() : _ready(false), _skillCount(0) {
    memset(_skills, 0, sizeof(_skills));
}

bool SkillsManager::begin() {
    if (!SPIFFS.begin(true)) { // true = format on fail
        Serial.println("[SkillsManager] SPIFFS mount failed!");
        return false;
    }

    _ready = true;

    // Create skills directory if it doesn't exist
    if (!SPIFFS.exists(SKILLS_DIR)) {
        SPIFFS.mkdir(SKILLS_DIR);
    }

    // Load all skills into RAM
    bool loaded = _loadAll();
    Serial.printf("[SkillsManager] Initialized: %d skill(s) loaded\n", _skillCount);
    return loaded;
}

bool SkillsManager::_loadAll() {
    _skillCount = 0;

    File dir = SPIFFS.open(SKILLS_DIR);
    if (!dir || !dir.isDirectory()) {
        return true; // Empty is fine
    }

    File file;
    while ((file = dir.openNextFile()) && _skillCount < SKILLS_MAX_COUNT) {
        if (file.isDirectory()) continue;

        // Read file content
        String content = file.readString();
        file.close();

        // Parse JSON
        DynamicJsonDocument doc(1024);
        if (deserializeJson(doc, content) != DeserializationError::Ok) {
            Serial.printf("[SkillsManager] Failed to parse: %s\n", file.name());
            continue;
        }

        SkillEntry &entry = _skills[_skillCount];
        strncpy(entry.name, doc["name"] | "", SKILLS_MAX_NAME - 1);
        strncpy(entry.triggers, doc["triggers"] | "", SKILLS_MAX_TRIGGERS - 1);
        strncpy(entry.instructions, doc["instructions"] | "", SKILLS_MAX_INSTR - 1);
        entry.enabled = doc["enabled"] | true;

        _skillCount++;
    }

    dir.close();
    return true;
}

bool SkillsManager::_saveSkill(const SkillEntry &skill) {
    String path = String(SKILLS_DIR) + "/" + _sanitizeName(skill.name) + ".json";

    DynamicJsonDocument doc(1024);
    doc["name"] = skill.name;
    doc["triggers"] = skill.triggers;
    doc["instructions"] = skill.instructions;
    doc["enabled"] = skill.enabled;

    File file = SPIFFS.open(path, "w");
    if (!file) {
        Serial.printf("[SkillsManager] Failed to write: %s\n", path.c_str());
        return false;
    }

    serializeJson(doc, file);
    file.close();
    return true;
}

bool SkillsManager::_deleteSkillFile(const char *name) {
    String path = String(SKILLS_DIR) + "/" + _sanitizeName(name) + ".json";
    return SPIFFS.remove(path);
}

int SkillsManager::_findSkill(const char *name) const {
    for (int i = 0; i < _skillCount; i++) {
        if (strcasecmp(_skills[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

String SkillsManager::_sanitizeName(const String &name) const {
    String sanitized;
    sanitized.reserve(name.length());
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        if (isalnum(c) || c == '-' || c == '_') {
            sanitized += tolower(c);
        } else if (c == ' ') {
            sanitized += '-';
        }
        // Skip other characters
    }
    return sanitized;
}

bool SkillsManager::_triggersMatch(const char *triggers, const String &input) const {
    // Parse comma-separated trigger words and check if any appear in the input
    String triggersStr = String(triggers);
    triggersStr.toLowerCase();
    String inputLower = input;
    inputLower.toLowerCase();

    int start = 0;
    while (start < (int)triggersStr.length()) {
        int comma = triggersStr.indexOf(',', start);
        String word;
        if (comma < 0) {
            word = triggersStr.substring(start);
            start = triggersStr.length();
        } else {
            word = triggersStr.substring(start, comma);
            start = comma + 1;
        }
        word.trim();
        if (word.length() > 0 && inputLower.indexOf(word) >= 0) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Public API — called from Telegram command handler
// ---------------------------------------------------------------------------

String SkillsManager::create(const String &name, const String &triggers, const String &instructions) {
    if (!_ready) return "Skills system not initialized.";

    if (name.length() == 0 || name.length() >= SKILLS_MAX_NAME) {
        return "Skill name must be 1-" + String(SKILLS_MAX_NAME - 1) + " chars.";
    }
    if (triggers.length() >= SKILLS_MAX_TRIGGERS) {
        return "Triggers too long (max " + String(SKILLS_MAX_TRIGGERS - 1) + " chars).";
    }
    if (instructions.length() >= SKILLS_MAX_INSTR) {
        return "Instructions too long (max " + String(SKILLS_MAX_INSTR - 1) + " chars).";
    }

    // Check if skill already exists — update it
    int idx = _findSkill(name.c_str());
    if (idx >= 0) {
        // Update existing
        strncpy(_skills[idx].triggers, triggers.c_str(), SKILLS_MAX_TRIGGERS - 1);
        strncpy(_skills[idx].instructions, instructions.c_str(), SKILLS_MAX_INSTR - 1);
        _skills[idx].enabled = true;
        if (_saveSkill(_skills[idx])) {
            return "Skill '" + name + "' updated.";
        }
        return "Failed to save skill '" + name + "'.";
    }

    // Check capacity
    if (_skillCount >= SKILLS_MAX_COUNT) {
        return "Max " + String(SKILLS_MAX_COUNT) + " skills. Delete one first.";
    }

    // Create new
    SkillEntry &entry = _skills[_skillCount];
    memset(&entry, 0, sizeof(SkillEntry));
    strncpy(entry.name, name.c_str(), SKILLS_MAX_NAME - 1);
    strncpy(entry.triggers, triggers.c_str(), SKILLS_MAX_TRIGGERS - 1);
    strncpy(entry.instructions, instructions.c_str(), SKILLS_MAX_INSTR - 1);
    entry.enabled = true;

    if (_saveSkill(entry)) {
        _skillCount++;
        return "Skill '" + name + "' created with triggers: " + triggers;
    }
    return "Failed to save skill '" + name + "'.";
}

String SkillsManager::remove(const String &name) {
    if (!_ready) return "Skills system not initialized.";

    int idx = _findSkill(name.c_str());
    if (idx < 0) {
        return "Skill '" + name + "' not found.";
    }

    // Delete file
    _deleteSkillFile(_skills[idx].name);

    // Remove from RAM array by shifting
    for (int i = idx; i < _skillCount - 1; i++) {
        _skills[i] = _skills[i + 1];
    }
    memset(&_skills[_skillCount - 1], 0, sizeof(SkillEntry));
    _skillCount--;

    return "Skill '" + name + "' deleted.";
}

String SkillsManager::list() const {
    if (!_ready) return "Skills system not initialized.";
    if (_skillCount == 0) return "No skills. Create one with /skill create";

    String result = "Skills (" + String(_skillCount) + "):\n";
    for (int i = 0; i < _skillCount; i++) {
        result += "  " + String(_skills[i].name);
        result += _skills[i].enabled ? " [ON] " : " [OFF] ";
        result += "triggers: " + String(_skills[i].triggers) + "\n";
    }
    return result;
}

String SkillsManager::toggle(const String &name, bool enabled) {
    if (!_ready) return "Skills system not initialized.";

    int idx = _findSkill(name.c_str());
    if (idx < 0) {
        return "Skill '" + name + "' not found.";
    }

    _skills[idx].enabled = enabled;
    if (_saveSkill(_skills[idx])) {
        return "Skill '" + name + "' " + (enabled ? "enabled" : "disabled") + ".";
    }
    return "Failed to update skill '" + name + "'.";
}

String SkillsManager::getMatchingInstructions(const String &inputText) const {
    if (!_ready || _skillCount == 0) return "";

    String combined;
    for (int i = 0; i < _skillCount; i++) {
        if (!_skills[i].enabled) continue;
        if (_triggersMatch(_skills[i].triggers, inputText)) {
            if (combined.length() > 0) combined += "\n";
            combined += "[Skill: " + String(_skills[i].name) + "] ";
            combined += String(_skills[i].instructions);
        }
    }
    return combined;
}

int SkillsManager::count() const {
    return _skillCount;
}
