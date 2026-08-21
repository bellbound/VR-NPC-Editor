#include "overlay/SlaveTatsImport.h"

#include "Config.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>

namespace NPCEditor::Overlay::SlaveTats {
    namespace {
        // Both halves of the join between a pack id and the file that declares it: every
        // generated config carries this prefix, so the whole set can be rewritten without
        // ever touching a config another mod shipped.
        constexpr std::string_view kModIdPrefix = "slavetats-";
        constexpr std::string_view kGeneratedPrefix = "VRNPCEditor_slavetats_";

        // SlaveTats texture paths are relative to the folder its manifests live in.
        constexpr std::string_view kTextureRoot = "textures\\actors\\character\\slavetats\\";

        std::filesystem::path ConfigDir() {
            return std::filesystem::path(Config::GetSKSEPluginsPath()) / "ODF_mod_configs";
        }

        std::filesystem::path ManifestDir() {
            // GetSKSEPluginsPath is <game>\Data\SKSE\Plugins.
            return std::filesystem::path(Config::GetSKSEPluginsPath()).parent_path().parent_path() /
                   "textures" / "actors" / "character" / "slavetats";
        }

        // "FH - #1012 (prison)" -> "fh_1012_prison". Runs of punctuation collapse to a
        // single separator, and exact collisions are numbered rather than dropped.
        std::string Slug(std::string_view text) {
            std::string slug;
            slug.reserve(text.size());

            bool pendingSeparator = false;
            for (unsigned char c : text) {
                if (std::isalnum(c)) {
                    if (pendingSeparator && !slug.empty()) slug.push_back('_');
                    pendingSeparator = false;
                    slug.push_back(static_cast<char>(std::tolower(c)));
                } else {
                    pendingSeparator = true;
                }
            }
            return slug;
        }

        std::string GetString(const nlohmann::json& parent, const char* key) {
            if (!parent.contains(key) || !parent[key].is_string()) return {};
            return parent[key].get<std::string>();
        }

        // SlaveTats areas are the same four overlay locations ODF names, capitalised.
        // Anything else is an authoring mistake and the tattoo is dropped rather than
        // guessed at - one on the wrong body part is worse than a missing one.
        std::string SlotForArea(std::string_view area) {
            std::string lower;
            for (unsigned char c : area) lower.push_back(static_cast<char>(std::tolower(c)));

            if (lower == "body") return "body";
            if (lower == "face" || lower == "head") return "face";
            if (lower == "hands" || lower == "hand") return "hands";
            if (lower == "feet" || lower == "foot") return "feet";
            return {};
        }

        std::string TexturePath(std::string_view texture) {
            std::string path(texture);
            for (auto& c : path) {
                if (c == '/') c = '\\';
            }
            while (!path.empty() && path.front() == '\\') path.erase(path.begin());

            // Packs that spell out the whole Data-relative path are left alone; the
            // common form is relative to the slavetats folder and needs the root back.
            std::string lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.rfind("textures\\", 0) == 0 || lower.rfind("data\\", 0) == 0) return path;

            return std::string(kTextureRoot) + path;
        }
    }

    bool IsImportedPack(std::string_view modId) {
        return modId.rfind(kModIdPrefix, 0) == 0;
    }

    bool IsGeneratedConfig(std::string_view filename) {
        return filename.rfind(kGeneratedPrefix, 0) == 0;
    }

    std::vector<std::string> FindManifests() {
        std::vector<std::string> manifests;

        const auto dir = ManifestDir();
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            spdlog::info("SlaveTats: no packs installed ({} is not there)", dir.string());
            return manifests;
        }

        for (const auto& item : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!item.is_regular_file(ec)) continue;

            auto ext = item.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".json") manifests.push_back(item.path().string());
        }
        std::sort(manifests.begin(), manifests.end());

        spdlog::info("SlaveTats: found {} manifests in {}", manifests.size(), dir.string());
        return manifests;
    }

    std::vector<Pack> ParseManifest(const std::string& path) {
        std::vector<Pack> packs;
        const auto stem = std::filesystem::path(path).stem().string();

        std::string raw;
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                spdlog::warn("SlaveTats: cannot open {}", path);
                return packs;
            }
            raw.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        // These manifests are routinely saved with a BOM, which a strict JSON parser is
        // under no obligation to accept.
        if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
            static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
            raw.erase(0, 3);
        }

        nlohmann::json root;
        try {
            root = nlohmann::json::parse(raw);
        } catch (const std::exception& error) {
            spdlog::warn("SlaveTats: skipping {} - invalid JSON: {}", path, error.what());
            return packs;
        }

        if (!root.is_array()) {
            spdlog::warn("SlaveTats: skipping {} - manifest is not an array of tattoos", path);
            return packs;
        }

        // Keyed by the pack id rather than the section name, so two spellings of one
        // section merge instead of splitting - Alpia ships both "Alpia FULL" and "Alpia
        // Full". Order follows the manifest, so a pack's overlays appear in the order its
        // author wrote them rather than alphabetically.
        std::unordered_map<std::string, size_t> packIndex;
        std::unordered_map<std::string, std::unordered_set<std::string>> usedIds;
        size_t droppedBadArea = 0;

        for (const auto& item : root) {
            if (!item.is_object()) continue;

            const auto name = GetString(item, "name");
            const auto texture = GetString(item, "texture");
            if (name.empty() || texture.empty()) continue;

            auto slot = SlotForArea(GetString(item, "area"));
            if (slot.empty()) {
                spdlog::warn("SlaveTats: \"{}\" has unknown area \"{}\", skipping", name,
                             GetString(item, "area"));
                ++droppedBadArea;
                continue;
            }

            auto sectionName = GetString(item, "section");
            if (sectionName.empty()) sectionName = stem;

            auto modId = std::string(kModIdPrefix) + Slug(stem) + "-" + Slug(sectionName);
            auto found = packIndex.find(modId);
            if (found == packIndex.end()) {
                Pack pack;
                pack.modId = modId;
                pack.sectionName = sectionName;
                pack.manifestStem = stem;
                found = packIndex.emplace(std::move(modId), packs.size()).first;
                packs.push_back(std::move(pack));
            }
            auto& pack = packs[found->second];

            // This half of the qualified id ends up in the co-save, so it has to be
            // stable across runs and unique in its pack. Stable comes from deriving it
            // from the tattoo's own name.
            auto id = Slug(name);
            if (id.empty()) id = "tattoo";

            auto& taken = usedIds[pack.modId];
            if (taken.count(id) > 0) {
                std::string candidate;
                for (int suffix = 2;; ++suffix) {
                    candidate = id + "_" + std::to_string(suffix);
                    if (taken.count(candidate) == 0) break;
                }
                id = std::move(candidate);
            }
            taken.insert(id);

            pack.tattoos.push_back({std::move(id), name, std::move(slot), TexturePath(texture)});
        }

        if (droppedBadArea > 0) {
            spdlog::warn("SlaveTats: {} dropped {} tattoos for an unknown area", stem, droppedBadArea);
        }
        return packs;
    }

    size_t PurgeGeneratedConfigs() {
        const auto dir = ConfigDir();

        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return 0;

        size_t removed = 0;
        for (const auto& item : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!item.is_regular_file(ec)) continue;

            const auto name = item.path().filename().string();
            if (name.rfind(kGeneratedPrefix, 0) != 0) continue;
            if (std::filesystem::remove(item.path(), ec)) ++removed;
        }
        return removed;
    }
}
