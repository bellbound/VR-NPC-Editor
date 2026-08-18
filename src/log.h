#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>
#include "Config.h"

// Keeps the current log plus the three previous runs: .log, .log.1, .log.2, .log.3
inline constexpr int kLogHistoryCount = 3;

// spdlog's rotating sink rotates on size, not on startup, so the shift is done by
// hand before any sink opens the file. Every step is error_code-based: a log file
// held open by an editor must never throw out of SKSEPluginLoad.
//
// The outcome is returned rather than discarded, so it can be logged once the sink
// exists - a rotation that silently does nothing is indistinguishable from one that
// works until you go looking for the previous run and it is not there.
inline std::string RotateLogs(const std::filesystem::path& logFilePath) {
    std::error_code ec;

    const auto historyPath = [&logFilePath](int index) {
        auto p = logFilePath;
        p += std::format(".{}", index);
        return p;
    };

    // Drop the oldest, then walk backwards so nothing overwrites a file we still need.
    std::filesystem::remove(historyPath(kLogHistoryCount), ec);
    for (int i = kLogHistoryCount - 1; i >= 1; --i) {
        std::filesystem::rename(historyPath(i), historyPath(i + 1), ec);
    }

    if (!std::filesystem::exists(logFilePath, ec)) {
        return "no previous log to rotate";
    }

    std::filesystem::rename(logFilePath, historyPath(1), ec);
    if (ec) {
        // Fall back to a copy: on some layouts a rename across the virtualised file
        // system fails where a plain copy succeeds.
        std::error_code copyEc;
        std::filesystem::copy_file(logFilePath, historyPath(1),
                                   std::filesystem::copy_options::overwrite_existing, copyEc);
        if (copyEc) {
            return std::format("could not rotate previous log: rename failed ({}), copy failed ({})",
                               ec.message(), copyEc.message());
        }
        return std::format("previous log copied to .log.1 (rename failed: {})", ec.message());
    }
    return "previous run moved to .log.1";
}

inline void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);

    const auto rotationResult = RotateLogs(logFilePath);

    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");

    // Start at trace so that config loading itself is logged, then apply the configured level.
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);

    spdlog::info("=== {} starting ===", pluginName);
    spdlog::info("Log file: {} (keeping {} previous runs)", logFilePath.string(), kLogHistoryCount);
    spdlog::info("Log rotation: {}", rotationResult);

    Config::ReadConfigOptions();
    spdlog::set_level(Config::options.logLevel);
    spdlog::info("Log level set to: {}", spdlog::level::to_string_view(Config::options.logLevel));
}
