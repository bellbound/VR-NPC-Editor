#include "Config.h"

#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace Config {
    Options options;

    static std::string g_configPath;
    static std::string g_pluginsPath;

    // ===== Default INI content =====
    static constexpr const char* DEFAULT_INI_CONTENT = R"(; VR NPC Editor Configuration
; Delete this file to regenerate with defaults

[General]
; Log level: trace, debug, info, warn, error, critical, off (default: info)
; trace = most verbose, every event the menu sees
; debug = every overlay apply/preview/remove, with node names and slot indices
; info  = startup, SKEE handshake, catalog summary
; warn  = broken overlay packs and skipped overlays
; error = errors only
logLevel=info

[Menu]
; Size multiplier for the overlay swatches - the strip of what the actor is wearing
; and the overlay shown in the middle of the stepper.
fElementScale=1.0

; Pack selected when the menu opens, by its ODF modId (e.g. titkit).
; Leave empty to select the first pack alphabetically.
sDefaultPack=

; Read SlaveTats texture packs as well as ODF ones (0=off, 1=on). SlaveTats tattoos
; are ordinary overlays, so any pack installed under
; Data\textures\actors\character\slavetats shows up in the menu, one pack icon
; per section. The SlaveTats mod itself is not needed and is never called.
;
; Only the tattoos actually applied to someone are ever declared to Overlay
; Distribution Framework, and only so it can put them back on the next game start.
; The rest stay between this menu and the pack, out of the pools other mods'
; distribution rules draw from.
bImportSlaveTats=1

; Build the overlay catalog shortly after a game loads, instead of waiting for the
; first time the menu is opened (0=off, 1=on). The build is a few milliseconds a frame
; either way and never hitches, but it does take a moment to get through every pack
; installed - doing it in the background means the menu has it ready when you open it
; rather than showing "Loading overlays..." while you wait.
;
; Turn off to keep the mod idle until it is used. Nothing is lost: the first open
; starts the same build, it just happens while you are looking at it.
bPreloadCatalog=1

[Body]
; Show the body menu at all (0=off, 1=on). Independent of whether OBody NG is
; detected - turning this off hides the menu even with OBody installed.
bEnableBodyMenu=1

; How long a chevron must be held before it starts stepping on its own, in
; milliseconds. Below this a press is one step. Both editors use this: the body
; menu's preset and weight steppers, and the overlay menu's overlay stepper.
iPresetRepeatDelayMs=200

; Gap between steps once a held chevron is repeating, in milliseconds. Every step
; applies the preset or writes the overlay to the actor, so very low values ask a
; lot of OBody and NiOverride.
iPresetRepeatIntervalMs=300

; Show the weight button (0=off, 1=on). It only ever appears on unique NPCs, because
; weight lives on the base record and generic actors share theirs.
bEnableWeightButton=1

; Minimum gap between full 3D resets when the weight button is tapped repeatedly,
; in milliseconds. A reset re-equips the actor's gear, so it is worth coalescing.
iWeightResetDebounceMs=400

; Show the TNG addon stepper (0=off, 1=on). It only appears when The New Gentleman
; is installed, the actor is one TNG will modify, and at least one addon fits them.
bEnableTngAddon=1

; Minimum gap between TNG addon writes when the stepper is held, in milliseconds.
; Every write swaps the actor's skin, so stepping through a long list without this
; would apply each one in turn.
iTngApplyDebounceMs=400

; How long to wait for TNG to answer, in milliseconds, before giving up and leaving
; the addon row hidden. TNG has no C++ interface, so the addon list has to come back
; through the Papyrus VM, which answers a frame or more after being asked.
iTngPrimeTimeoutMs=2000
)";

    // ===== Low-level INI readers using Windows API =====
    static std::string GetConfigOption(const char* section, const char* key) {
        const std::string& configPath = GetConfigPath();
        if (configPath.empty()) return "";

        char buffer[256];
        GetPrivateProfileStringA(section, key, "", buffer, sizeof(buffer), configPath.c_str());
        return buffer;
    }

    static bool GetConfigOptionBool(const char* section, const char* key, bool* out) {
        std::string data = GetConfigOption(section, key);
        if (data.empty()) return false;
        try {
            int val = std::stoi(data);
            *out = (val != 0);
            return true;
        } catch (...) {
            spdlog::warn("Config: Failed to parse bool for {}/{}", section, key);
            return false;
        }
    }

    // Clamped rather than rejected: an out-of-range timing value is a typo, and the
    // nearest sane value is far more useful than silently keeping the default.
    static bool GetConfigOptionInt(const char* section, const char* key, int* out, int min, int max) {
        std::string data = GetConfigOption(section, key);
        if (data.empty()) return false;
        try {
            int val = std::stoi(data);
            if (val < min || val > max) {
                spdlog::warn("Config: [{}] {} = {} out of range {}-{}, clamping", section, key, val, min, max);
                val = std::clamp(val, min, max);
            }
            *out = val;
            return true;
        } catch (...) {
            spdlog::warn("Config: Failed to parse int for {}/{}", section, key);
            return false;
        }
    }

    static bool GetConfigOptionFloat(const char* section, const char* key, float* out) {
        std::string data = GetConfigOption(section, key);
        if (data.empty()) return false;
        try {
            *out = std::stof(data);
            return true;
        } catch (...) {
            spdlog::warn("Config: Failed to parse float for {}/{}", section, key);
            return false;
        }
    }

    static spdlog::level::level_enum ParseLogLevel(const std::string& levelStr) {
        if (levelStr == "trace") return spdlog::level::trace;
        if (levelStr == "debug") return spdlog::level::debug;
        if (levelStr == "info") return spdlog::level::info;
        if (levelStr == "warn" || levelStr == "warning") return spdlog::level::warn;
        if (levelStr == "error" || levelStr == "err") return spdlog::level::err;
        if (levelStr == "critical") return spdlog::level::critical;
        if (levelStr == "off") return spdlog::level::off;
        return spdlog::level::info;  // default
    }

    static const char* LogLevelToString(spdlog::level::level_enum level) {
        switch (level) {
            case spdlog::level::trace: return "trace";
            case spdlog::level::debug: return "debug";
            case spdlog::level::info: return "info";
            case spdlog::level::warn: return "warn";
            case spdlog::level::err: return "error";
            case spdlog::level::critical: return "critical";
            case spdlog::level::off: return "off";
            default: return "info";
        }
    }

    // ===== Create default INI file =====
    static bool CreateDefaultConfigFile(const std::string& path) {
        spdlog::info("Config: Creating default config file at {}", path);

        std::filesystem::path filePath(path);
        std::filesystem::path dirPath = filePath.parent_path();

        std::error_code ec;
        if (!std::filesystem::exists(dirPath, ec)) {
            std::filesystem::create_directories(dirPath, ec);
            if (ec) {
                spdlog::error("Config: Failed to create directory {}: {}", dirPath.string(), ec.message());
                return false;
            }
            spdlog::info("Config: Created directory {}", dirPath.string());
        }

        std::ofstream file(path);
        if (!file.is_open()) {
            spdlog::error("Config: Failed to create config file at {}", path);
            return false;
        }

        file << DEFAULT_INI_CONTENT;
        file.close();

        spdlog::info("Config: Default config file created successfully");
        return true;
    }

    bool ReadConfigOptions() {
        const std::string& path = GetConfigPath();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            spdlog::info("Config: Config file not found, creating default");
            if (!CreateDefaultConfigFile(path)) {
                spdlog::error("Config: Failed to create default config, using built-in defaults");
            }
        }

        spdlog::info("Config: Reading config from {}", path);

        // Reset options to defaults before reading
        options = Options{};

        std::string logLevelStr = GetConfigOption("General", "logLevel");
        if (!logLevelStr.empty()) {
            options.logLevel = ParseLogLevel(logLevelStr);
            spdlog::info("Config: [General] logLevel = {}", LogLevelToString(options.logLevel));
        } else {
            spdlog::info("Config: logLevel not found, using default {}", LogLevelToString(options.logLevel));
        }

        if (GetConfigOptionFloat("Menu", "fElementScale", &options.elementScale)) {
            // A zero or negative scale makes the swatches invisible and looks like a broken mod.
            if (options.elementScale < 0.1f || options.elementScale > 10.0f) {
                spdlog::warn("Config: [Menu] fElementScale {} out of range 0.1-10.0, using 1.0", options.elementScale);
                options.elementScale = 1.0f;
            } else {
                spdlog::info("Config: [Menu] fElementScale = {}", options.elementScale);
            }
        }

        options.defaultPack = GetConfigOption("Menu", "sDefaultPack");
        if (!options.defaultPack.empty()) {
            spdlog::info("Config: [Menu] sDefaultPack = {}", options.defaultPack);
        }

        if (GetConfigOptionBool("Body", "bEnableBodyMenu", &options.enableBodyMenu)) {
            spdlog::info("Config: [Body] bEnableBodyMenu = {}", options.enableBodyMenu);
        }

        if (GetConfigOptionInt("Body", "iPresetRepeatDelayMs", &options.presetRepeatDelayMs, 0, 5000)) {
            spdlog::info("Config: [Body] iPresetRepeatDelayMs = {}", options.presetRepeatDelayMs);
        }

        if (GetConfigOptionInt("Body", "iPresetRepeatIntervalMs", &options.presetRepeatIntervalMs, 30, 5000)) {
            spdlog::info("Config: [Body] iPresetRepeatIntervalMs = {}", options.presetRepeatIntervalMs);
        }

        if (GetConfigOptionBool("Body", "bEnableWeightButton", &options.enableWeightButton)) {
            spdlog::info("Config: [Body] bEnableWeightButton = {}", options.enableWeightButton);
        }

        if (GetConfigOptionInt("Body", "iWeightResetDebounceMs", &options.weightResetDebounceMs, 0, 5000)) {
            spdlog::info("Config: [Body] iWeightResetDebounceMs = {}", options.weightResetDebounceMs);
        }

        if (GetConfigOptionBool("Body", "bEnableTngAddon", &options.enableTngAddon)) {
            spdlog::info("Config: [Body] bEnableTngAddon = {}", options.enableTngAddon);
        }

        if (GetConfigOptionInt("Body", "iTngApplyDebounceMs", &options.tngApplyDebounceMs, 0, 5000)) {
            spdlog::info("Config: [Body] iTngApplyDebounceMs = {}", options.tngApplyDebounceMs);
        }

        if (GetConfigOptionInt("Body", "iTngPrimeTimeoutMs", &options.tngPrimeTimeoutMs, 100, 30000)) {
            spdlog::info("Config: [Body] iTngPrimeTimeoutMs = {}", options.tngPrimeTimeoutMs);
        }

        if (GetConfigOptionBool("Menu", "bImportSlaveTats", &options.importSlaveTats)) {
            spdlog::info("Config: [Menu] bImportSlaveTats = {}", options.importSlaveTats);
        }

        if (GetConfigOptionBool("Menu", "bPreloadCatalog", &options.preloadCatalog)) {
            spdlog::info("Config: [Menu] bPreloadCatalog = {}", options.preloadCatalog);
        }

        spdlog::info("Config: Loaded successfully");
        return true;
    }

    const std::string& GetSKSEPluginsPath() {
        if (g_pluginsPath.empty()) {
            wchar_t pathBuf[MAX_PATH];
            GetModuleFileNameW(nullptr, pathBuf, MAX_PATH);

            std::filesystem::path gamePath(pathBuf);
            gamePath = gamePath.parent_path();  // Remove exe name
            gamePath /= "Data";
            gamePath /= "SKSE";
            gamePath /= "Plugins";

            g_pluginsPath = gamePath.string();
        }
        return g_pluginsPath;
    }

    const std::string& GetConfigPath() {
        if (g_configPath.empty()) {
            std::filesystem::path p(GetSKSEPluginsPath());
            p /= "VRNPCEditor.ini";
            g_configPath = p.string();
        }
        return g_configPath;
    }
}
