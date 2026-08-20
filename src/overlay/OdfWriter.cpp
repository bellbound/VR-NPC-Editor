#include "overlay/OdfWriter.h"

#include "Config.h"
#include "overlay/OverlayCatalog.h"
#include "overlay/OverlayStateManager.h"
#include "overlay/SlaveTatsImport.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace NPCEditor::Overlay::OdfWriter {
    namespace {
        std::string g_outputPath;

        std::string FormatColor(uint32_t color) {
            return std::format("0x{:06X}", color & 0xFFFFFF);
        }
    }

    const std::string& GetOutputPath() {
        if (g_outputPath.empty()) {
            std::filesystem::path path(Config::GetSKSEPluginsPath());
            path /= "ODF_distribution_rules";
            path /= "VRNPCEditor_distribution.json";
            g_outputPath = path.string();
        }
        return g_outputPath;
    }

    bool WriteAll(const StateManager& state) {
        const auto& outputPath = GetOutputPath();
        const auto* catalog = Catalog::GetSingleton();

        nlohmann::json rules = nlohmann::json::array();

        // ODF can only resolve a rule against an overlay some mod config declares. The
        // ODF packs declare their own; SlaveTats packs have no config at all until we
        // write one, and we write one only for what is actually worn - see SlaveTats
        // for why declaring a whole installed library is not harmless.
        std::vector<const Entry*> appliedEntries;

        for (const auto& [key, actorState] : state.GetAll()) {
            if (actorState.editorId.empty() || actorState.applied.empty()) continue;

            nlohmann::json overlays = nlohmann::json::array();
            for (const auto& applied : actorState.applied) {
                nlohmann::json overlay;
                overlay["id"] = applied.qualifiedId;
                // distributionMode "all" plus chance 1.0 makes this deterministic rather
                // than a weighted roll - the actor gets exactly what was chosen.
                overlay["chance"] = 1.0;

                if (const auto* entry = catalog->FindEntry(applied.qualifiedId)) {
                    appliedEntries.push_back(entry);

                    // The look that was actually written, not the pack's declaration.
                    // ODF re-applies this at the next game start and it is the only
                    // record of the choice that outlives the session, so a rule built
                    // from the entry alone hands the player back the pack's own colour -
                    // which across the installed packs is almost always black.
                    const auto look = LookFor(*entry, applied.appearance);
                    if (look.color) overlay["color"] = FormatColor(*look.color);
                    if (look.alpha) overlay["alpha"] = *look.alpha;
                    if (look.glowColor) {
                        overlay["glow"] = true;
                        overlay["glowColor"] = FormatColor(*look.glowColor);
                    }
                    if (look.glowIntensity) overlay["glowIntensity"] = *look.glowIntensity;
                }
                overlays.push_back(std::move(overlay));
            }

            nlohmann::json condition;
            condition["editorId"] = nlohmann::json::array({actorState.editorId});

            nlohmann::json rule;
            rule["conditions"] = nlohmann::json::array({std::move(condition)});
            rule["baseChance"] = 1.0;
            rule["distributionMode"] = "all";
            rule["overlays"] = std::move(overlays);
            rules.push_back(std::move(rule));
        }

        nlohmann::json document;
        document["distributionRules"] = std::move(rules);

        std::error_code ec;
        const std::filesystem::path finalPath(outputPath);
        std::filesystem::create_directories(finalPath.parent_path(), ec);
        if (ec) {
            spdlog::error("ODF: cannot create {}: {}", finalPath.parent_path().string(), ec.message());
            return false;
        }

        // Written to a sibling temp file first: a crash part-way through must not leave
        // ODF a truncated JSON to choke on at the next game start.
        auto tempPath = finalPath;
        tempPath += ".tmp";
        {
            std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                spdlog::error("ODF: cannot open {} for writing", tempPath.string());
                return false;
            }
            file << document.dump(4);
            if (!file.good()) {
                spdlog::error("ODF: write to {} failed", tempPath.string());
                return false;
            }
        }

        std::filesystem::remove(finalPath, ec);
        std::filesystem::rename(tempPath, finalPath, ec);
        if (ec) {
            spdlog::error("ODF: cannot move {} into place: {}", tempPath.string(), ec.message());
            std::filesystem::remove(tempPath, ec);
            return false;
        }

        // After the rules are safely in place: a declaration for a rule that failed to
        // write is a pack the menu never asked ODF for.
        SlaveTats::WriteAppliedConfigs(appliedEntries);

        spdlog::info("ODF: wrote {} rules to {}", document["distributionRules"].size(), outputPath);
        return true;
    }
}
