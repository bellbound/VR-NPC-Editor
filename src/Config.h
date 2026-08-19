#pragma once

#include <string>
#include <spdlog/spdlog.h>

namespace Config {
    struct Options {
        // ===== Logging =====
        spdlog::level::level_enum logLevel = spdlog::level::info;  // Log verbosity (trace, debug, info, warn, error)

        // ===== Menu =====
        float elementScale       = 1.0f;   // Size multiplier for the overlay swatches
        std::string defaultPack;           // Pack selected when the menu opens; empty = first alphabetically
        bool importSlaveTats     = true;   // Convert installed SlaveTats packs into ODF packs
        bool preloadCatalog      = true;   // Build the overlay catalog after a load rather than on first open

        // ===== Body =====
        bool enableBodyMenu      = true;   // Master switch, independent of whether OBody is detected
        int  presetRepeatDelayMs = 200;    // Hold a stepper this long before it starts repeating
        int  presetRepeatIntervalMs = 300; // Gap between steps once it is repeating
        bool enableWeightButton  = true;   // Show the weight cycle button on unique NPCs
        int  weightResetDebounceMs = 400;  // Minimum gap between DoReset3D calls
        bool enableTngAddon      = true;   // Show the TNG addon stepper, if TNG is installed
        int  tngApplyDebounceMs  = 400;    // Minimum gap between TNG addon writes
        int  tngPrimeTimeoutMs   = 2000;   // Give up waiting for TNG's Papyrus answers after this

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
