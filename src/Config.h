#pragma once

#include <string>
#include <spdlog/spdlog.h>

namespace Config {
    struct Options {
        // ===== Logging =====
        spdlog::level::level_enum logLevel = spdlog::level::info;  // Log verbosity (trace, debug, info, warn, error)

        // ===== Menu =====
        bool  enableHoverPreview = true;   // Hovering an unapplied overlay shows it on the actor
        float elementScale       = 1.0f;   // Size multiplier for the overlay swatches in the wheel
        std::string defaultPack;           // Pack selected when the menu opens; empty = first alphabetically

        // ===== Persistence =====
        bool writeOdfRules = true;         // Write choices to an ODF distribution rule file for next boot
    };

    extern Options options;

    // Read all config from INI file (creates default if not found)
    bool ReadConfigOptions();

    // Get path to INI file
    const std::string& GetConfigPath();

    // <game>/Data/SKSE/Plugins - the anchor both the INI and the ODF rule file hang off
    const std::string& GetSKSEPluginsPath();
}
