#include "overlay/OdfCleanup.h"

#include "Config.h"
#include "overlay/SlaveTatsImport.h"

#include <filesystem>
#include <spdlog/spdlog.h>

namespace NPCEditor::Overlay::OdfCleanup {
    void RemoveLegacyFiles() {
        std::filesystem::path rules(Config::GetSKSEPluginsPath());
        rules /= "ODF_distribution_rules";
        rules /= "VRNPCEditor_distribution.json";

        // remove() reports false with no error when the file was already gone, which is
        // the ordinary case on every boot after the first.
        std::error_code ec;
        if (std::filesystem::remove(rules, ec)) {
            spdlog::info("ODF: removed the rule file older builds wrote, {}", rules.string());
        } else if (ec) {
            spdlog::warn("ODF: cannot remove {}: {}", rules.string(), ec.message());
        }

        if (const auto removed = SlaveTats::PurgeGeneratedConfigs(); removed > 0) {
            spdlog::info("ODF: withdrew {} SlaveTats declarations", removed);
        }
    }
}
