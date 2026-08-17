#include "Config.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

namespace Config {
    Options options;

    static std::string g_configPath;
    static std::string g_pluginsPath;

    // ===== Default INI content =====
    static constexpr const char* DEFAULT_INI_CONTENT = R"(; VR Skin Overlay Menu Configuration
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
; Preview an overlay on the actor while the hand hovers it (0=off, 1=on)
; The preview is not persisted - only clicking an overlay commits it.
bEnableHoverPreview=1

; Size multiplier for the overlay swatches in the wheel
fElementScale=1.0

; Pack selected when the menu opens, by its ODF modId (e.g. titkit).
; Leave empty to select the first pack alphabetically.
sDefaultPack=

[Persistence]
; Write applied overlays into SKSE/Plugins/ODF_distribution_rules so that
; Overlay Distribution Framework reapplies them on the next game start.
; Turn off to keep every change session-only. (0=off, 1=on)
bWriteODFRules=1
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

        if (GetConfigOptionBool("Menu", "bEnableHoverPreview", &options.enableHoverPreview)) {
            spdlog::info("Config: [Menu] bEnableHoverPreview = {}", options.enableHoverPreview);
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

        if (GetConfigOptionBool("Persistence", "bWriteODFRules", &options.writeOdfRules)) {
            spdlog::info("Config: [Persistence] bWriteODFRules = {}", options.writeOdfRules);
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
            p /= "VRSkinOverlayMenu.ini";
            g_configPath = p.string();
        }
        return g_configPath;
    }
}
