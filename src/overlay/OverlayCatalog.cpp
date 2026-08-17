#include "overlay/OverlayCatalog.h"

#include "Config.h"
#include "util/TextureUtil.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Overlay {
    namespace {
        // One pack per frame would hitch on the larger packs (lewd-marks declares 192
        // overlays), so work is capped by overlay count instead.
        constexpr size_t kOverlaysPerBatch = 64;

        std::wstring Widen(std::string_view text) {
            return std::wstring(text.begin(), text.end());
        }

        // "light_1_big" -> "Light 1 Big"
        std::wstring PrettifyId(std::string_view id) {
            std::string pretty(id);
            for (auto& c : pretty) {
                if (c == '_' || c == '-') c = ' ';
            }
            bool startOfWord = true;
            for (auto& c : pretty) {
                if (startOfWord) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                startOfWord = (c == ' ');
            }
            return Widen(pretty);
        }

        Gender ParseGender(std::string_view text) {
            if (text == "male") return Gender::Male;
            if (text == "female") return Gender::Female;
            return Gender::Any;
        }

        // ODF writes colours as "0xRRGGBB" strings.
        std::optional<uint32_t> ParseColor(const nlohmann::json& value) {
            if (value.is_number_unsigned()) return value.get<uint32_t>();
            if (!value.is_string()) return std::nullopt;

            const auto text = value.get<std::string>();
            if (text.empty()) return std::nullopt;
            try {
                return static_cast<uint32_t>(std::stoul(text, nullptr, 16));
            } catch (...) {
                return std::nullopt;
            }
        }

        template <class T>
        std::optional<T> ParseNumber(const nlohmann::json& parent, const char* key) {
            if (!parent.contains(key) || !parent[key].is_number()) return std::nullopt;
            return parent[key].get<T>();
        }

        std::string GetString(const nlohmann::json& parent, const char* key) {
            if (!parent.contains(key) || !parent[key].is_string()) return {};
            return parent[key].get<std::string>();
        }
    }

    Catalog* Catalog::GetSingleton() {
        static Catalog instance;
        return &instance;
    }

    float Catalog::GetProgress() const {
        switch (m_state.load()) {
            case LoadState::NotStarted: return 0.0f;
            case LoadState::Ready:      return 1.0f;
            case LoadState::Failed:     return 0.0f;
            case LoadState::Loading:
            default:
                if (m_configFiles.empty()) return 0.0f;
                return static_cast<float>(m_fileCursor) / static_cast<float>(m_configFiles.size());
        }
    }

    void Catalog::StartBuildAsync(CompleteCallback callback) {
        const auto state = m_state.load();
        if (state == LoadState::Ready) {
            if (callback) callback(true);
            return;
        }
        if (state == LoadState::Loading) {
            spdlog::debug("Catalog: build already in progress");
            return;
        }

        m_callback = std::move(callback);
        m_packs.clear();
        m_entries.clear();
        m_configFiles.clear();
        m_fileCursor = 0;
        m_packsSeen = m_packsBroken = m_overlaysSeen = 0;
        m_droppedMissingTexture = m_droppedBadDimensions = m_droppedBadSlot = 0;
        m_state = LoadState::Loading;

        EnumerateConfigs();
        if (m_configFiles.empty()) {
            Finalize();
            return;
        }
        QueueNextBatch();
    }

    void Catalog::EnumerateConfigs() {
        std::filesystem::path dir(Config::GetSKSEPluginsPath());
        dir /= "ODF_mod_configs";

        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            spdlog::error("Catalog: {} not found - is Overlay Distribution Framework installed?", dir.string());
            return;
        }

        spdlog::info("Catalog: scanning {}", dir.string());
        for (const auto& item : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!item.is_regular_file(ec)) continue;

            auto ext = item.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".json") m_configFiles.push_back(item.path().string());
        }
        std::sort(m_configFiles.begin(), m_configFiles.end());
        spdlog::info("Catalog: found {} pack manifests", m_configFiles.size());
    }

    void Catalog::QueueNextBatch() {
        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            spdlog::error("Catalog: no SKSE task interface, cannot build");
            m_state = LoadState::Failed;
            if (m_callback) m_callback(false);
            return;
        }
        taskInterface->AddTask([this]() { ProcessBatch(); });
    }

    void Catalog::ProcessBatch() {
        size_t processedThisBatch = 0;

        while (m_fileCursor < m_configFiles.size() && processedThisBatch < kOverlaysPerBatch) {
            const auto& path = m_configFiles[m_fileCursor];
            ++m_fileCursor;
            ++m_packsSeen;

            nlohmann::json root;
            try {
                std::ifstream file(path);
                if (!file.is_open()) {
                    spdlog::warn("Catalog: cannot open {}", path);
                    ++m_packsBroken;
                    continue;
                }
                root = nlohmann::json::parse(file);
            } catch (const std::exception& e) {
                // Some shipped packs have malformed JSON; one bad pack must not take
                // out the catalog, so it is named and skipped exactly like ODF does.
                spdlog::warn("Catalog: skipping {} - invalid JSON: {}", path, e.what());
                ++m_packsBroken;
                continue;
            }

            const auto modId = GetString(root, "modId");
            if (modId.empty() || !root.contains("overlays") || !root["overlays"].is_array()) {
                spdlog::warn("Catalog: skipping {} - no modId or overlays array", path);
                ++m_packsBroken;
                continue;
            }

            Pack pack;
            pack.modId = modId;
            pack.esp = GetString(root, "esp");
            pack.displayName = PrettifyId(modId);

            for (const auto& item : root["overlays"]) {
                if (!item.is_object()) continue;
                ++m_overlaysSeen;
                ++processedThisBatch;

                Entry entry;
                entry.modId = modId;
                entry.overlayId = GetString(item, "id");
                if (entry.overlayId.empty()) continue;
                entry.qualifiedId = modId + ":" + entry.overlayId;

                const auto slot = GetString(item, "slot");
                auto location = Skee::ParseLocation(slot);
                if (!location) {
                    spdlog::warn("Catalog: {} has unknown slot \"{}\", skipping", entry.qualifiedId, slot);
                    ++m_droppedBadSlot;
                    continue;
                }
                entry.location = *location;

                entry.texture = TextureUtil::Normalize(GetString(item, "filepath"));
                if (entry.texture.empty()) continue;

                // A path that does not resolve is the single most common ODF authoring
                // mistake, and in game it shows up as a solid-colour body rather than an
                // error. Dropping the entry is the only way to keep that off the actor.
                uint32_t width = 0, height = 0;
                if (!TextureUtil::Exists(entry.texture)) {
                    spdlog::warn("Catalog: {} texture not found: {}", entry.qualifiedId, entry.texture);
                    ++m_droppedMissingTexture;
                    continue;
                }
                if (!TextureUtil::IsUsableSwatch(entry.texture, width, height)) {
                    spdlog::warn("Catalog: {} texture {} is {}x{} - not a power-of-two DDS, skipping to avoid a crash",
                                 entry.qualifiedId, entry.texture, width, height);
                    ++m_droppedBadDimensions;
                    continue;
                }

                entry.gender = ParseGender(GetString(item, "gender"));
                entry.type = GetString(item, "type");
                entry.theme = GetString(item, "theme");
                entry.set = GetString(item, "set");
                entry.displayName = PrettifyId(entry.overlayId);

                entry.appearance.texture = entry.texture;
                if (item.contains("color")) entry.appearance.color = ParseColor(item["color"]);
                if (item.contains("glowColor")) entry.appearance.glowColor = ParseColor(item["glowColor"]);
                entry.appearance.alpha = ParseNumber<float>(item, "alpha");
                entry.appearance.glowIntensity = ParseNumber<float>(item, "glowIntensity");

                if (pack.coverTexture.empty()) pack.coverTexture = entry.texture;
                pack.entryIndices.push_back(m_entries.size());
                m_entries.push_back(std::move(entry));
            }

            if (pack.entryIndices.empty()) {
                // Packs whose textures are not installed (Community Overlays 1 and 2 in
                // this setup) would otherwise show up as empty filters.
                spdlog::info("Catalog: pack \"{}\" contributed no usable overlays, dropping", pack.modId);
                continue;
            }

            spdlog::debug("Catalog: pack \"{}\" -> {} overlays", pack.modId, pack.entryIndices.size());
            m_packs.push_back(std::move(pack));
        }

        if (m_fileCursor < m_configFiles.size()) {
            QueueNextBatch();
            return;
        }
        Finalize();
    }

    void Catalog::Finalize() {
        std::sort(m_packs.begin(), m_packs.end(),
                  [](const Pack& a, const Pack& b) { return a.modId < b.modId; });

        const bool success = !m_packs.empty();
        m_state = success ? LoadState::Ready : LoadState::Failed;

        spdlog::info("Catalog: {} manifests read, {} broken, {} packs kept, {} of {} overlays usable",
                     m_packsSeen, m_packsBroken, m_packs.size(), m_entries.size(), m_overlaysSeen);
        spdlog::info("Catalog: dropped {} for a missing texture, {} for bad dimensions, {} for an unknown slot",
                     m_droppedMissingTexture, m_droppedBadDimensions, m_droppedBadSlot);

        if (!success) {
            spdlog::error("Catalog: no usable overlay packs found - the menu will have nothing to show");
        }

        if (m_callback) {
            auto callback = std::move(m_callback);
            m_callback = nullptr;
            callback(success);
        }
    }

    const Pack* Catalog::FindPack(std::string_view modId) const {
        for (const auto& pack : m_packs) {
            if (pack.modId == modId) return &pack;
        }
        return nullptr;
    }

    const Entry* Catalog::FindEntry(std::string_view qualifiedId) const {
        for (const auto& entry : m_entries) {
            if (entry.qualifiedId == qualifiedId) return &entry;
        }
        return nullptr;
    }

    std::vector<const Entry*> Catalog::GetEntriesForPack(const Pack& pack, bool isFemale) const {
        std::vector<const Entry*> result;
        result.reserve(pack.entryIndices.size());

        const auto wanted = isFemale ? Gender::Female : Gender::Male;
        for (size_t index : pack.entryIndices) {
            if (index >= m_entries.size()) continue;
            const auto& entry = m_entries[index];
            if (entry.gender == Gender::Any || entry.gender == wanted) result.push_back(&entry);
        }
        return result;
    }

    const Entry* Catalog::FindEntryByTexture(std::string_view texture) const {
        if (texture.empty()) return nullptr;

        const auto normalized = TextureUtil::Normalize(std::string(texture));
        for (const auto& entry : m_entries) {
            if (_stricmp(entry.texture.c_str(), normalized.c_str()) == 0) return &entry;
        }
        return nullptr;
    }
}
