#include "overlay/OverlayCatalog.h"

#include "Config.h"
#include "FrameHook.h"
#include "overlay/SlaveTatsImport.h"
#include "util/TextureUtil.h"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace NPCEditor::Overlay {
    namespace {
        // How much of a frame the build may take. Every overlay costs an archive lookup
        // to verify its texture, and the installed packs between them declare a few
        // thousand; the work is real but it only happens once, so it is paid for in
        // slices small enough to disappear into a frame.
        //
        // Measured rather than counted. A fixed number of overlays per frame is really a
        // guess about how long a lookup takes, which depends on whether the pack is loose
        // or in a BSA and on how many archives are installed; the count that suits one
        // setup builds three times too slowly on another. Twenty-four per frame was that
        // guess, and against 3556 declared overlays it meant a hundred and fifty frames -
        // most of a second and a half of "Loading overlays..." at the first open, on work
        // that fits into a fraction of it.
        constexpr auto kFrameBudget = std::chrono::milliseconds(2);

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

        // Several shipped packs are missing a comma between two members - skin-feature-
        // overlays, nordic-warmaiden-bodyhair and wolfpaint all have it after
        // "modVersion". ODF's parser tolerates this and loads them, so dropping them
        // would cost real content (skin-feature-overlays alone has 159 usable overlays).
        // This inserts the separator where a value clearly ends and the next member
        // begins, and is only ever tried after a strict parse has already failed.
        std::string RepairMissingCommas(const std::string& text) {
            std::string repaired;
            repaired.reserve(text.size() + 16);

            bool inString = false;
            bool escaped = false;
            size_t lastValueEnd = std::string::npos;  // index in `repaired`

            for (char c : text) {
                if (inString) {
                    repaired.push_back(c);
                    if (escaped) {
                        escaped = false;
                    } else if (c == '\\') {
                        escaped = true;
                    } else if (c == '"') {
                        inString = false;
                        lastValueEnd = repaired.size();
                    }
                    continue;
                }

                if (c == '"') {
                    // A new string starting straight after a finished value, with only
                    // whitespace between them, means the comma was left out.
                    if (lastValueEnd != std::string::npos &&
                        repaired.find_first_not_of(" \t\r\n", lastValueEnd) == std::string::npos) {
                        repaired.insert(lastValueEnd, ",");
                    }
                    inString = true;
                    repaired.push_back(c);
                    continue;
                }

                if (!std::isspace(static_cast<unsigned char>(c))) {
                    lastValueEnd = std::string::npos;
                }
                repaired.push_back(c);
            }
            return repaired;
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
            default: {
                const auto total = m_configFiles.size() + m_slaveTatsFiles.size();
                if (total == 0) return 0.0f;
                return static_cast<float>(m_fileCursor + m_slaveTatsCursor) / static_cast<float>(total);
            }
        }
    }

    void Catalog::StartBuildAsync(CompleteCallback callback) {
        const auto state = m_state.load();
        if (state == LoadState::Ready) {
            if (callback) callback(true);
            return;
        }
        if (state == LoadState::Loading) {
            spdlog::debug("Catalog: build already in progress, waiting on it");
            if (callback) m_callbacks.push_back(std::move(callback));
            return;
        }

        m_callbacks.clear();
        if (callback) m_callbacks.push_back(std::move(callback));
        m_packs.clear();
        m_entries.clear();
        m_configFiles.clear();
        m_fileCursor = 0;
        m_slaveTatsFiles.clear();
        m_slaveTatsCursor = 0;
        m_pendingOdfEntries.clear();
        m_pendingOdfIndex = 0;
        m_pendingOdfPack = {};
        m_pendingOdfActive = false;
        m_pendingSlaveTats.clear();
        m_pendingSlaveTatsPack = 0;
        m_pendingSlaveTatsTattoo = 0;
        m_pendingStPack = {};
        m_pendingStActive = false;
        m_slaveTatsPacks = 0;
        m_packsSeen = m_packsBroken = m_overlaysSeen = 0;
        m_droppedMissingTexture = m_droppedBadDimensions = m_droppedBadSlot = 0;
        m_state = LoadState::Loading;

        EnumerateConfigs();
        if (m_configFiles.empty() && m_slaveTatsFiles.empty()) {
            Finalize();
            return;
        }
        QueueNextBatch();
    }

    void Catalog::EnumerateConfigs() {
        // SlaveTats packs are overlays like any other, just described in their own
        // manifest format, so they are read straight into entries alongside the ODF
        // ones. Nothing about them is written anywhere until one is applied.
        if (Config::options.importSlaveTats) {
            m_slaveTatsFiles = SlaveTats::FindManifests();
        } else if (const auto removed = SlaveTats::PurgeGeneratedConfigs(); removed > 0) {
            spdlog::info("Catalog: SlaveTats import is off, withdrew {} declarations", removed);
        }

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
            if (ext != ".json") continue;

            // Our own declarations of what is applied. The manifests they came from are
            // the authority, and reading both would list every applied tattoo twice.
            if (SlaveTats::IsGeneratedConfig(item.path().filename().string())) continue;

            m_configFiles.push_back(item.path().string());
        }
        std::sort(m_configFiles.begin(), m_configFiles.end());
        spdlog::info("Catalog: found {} pack manifests", m_configFiles.size());
    }

    void Catalog::QueueNextBatch() {
        // A real frame apart. As an SKSE task this was not a batched build at all: tasks
        // queued from inside a task are drained by the same pass, so all 31 manifests and
        // every texture existence check for 2678 overlays landed in the frame the menu
        // opened on - which is what made the first open stall.
        FrameHook::GetSingleton()->NextFrame([this]() { ProcessBatch(); });
    }

    // Both sources share one frame's slice. SlaveTats manifests go last so a pack that
    // was already browsable keeps the pack order it had before one was installed.
    void Catalog::ProcessBatch() {
        m_deadline = Clock::now() + kFrameBudget;

        auto odfLeft = [this] { return m_pendingOdfActive || m_fileCursor < m_configFiles.size(); };
        auto slaveTatsLeft = [this] {
            return m_pendingStActive || !m_pendingSlaveTats.empty() ||
                   m_slaveTatsCursor < m_slaveTatsFiles.size();
        };

        while (!OutOfTime() && odfLeft()) ProcessOdfConfig();
        while (!OutOfTime() && slaveTatsLeft()) ProcessSlaveTatsPack();

        if (odfLeft() || slaveTatsLeft()) {
            QueueNextBatch();
            return;
        }
        Finalize();
    }

    // Verified here rather than at declaration time because the check is an archive
    // lookup, and doing several thousand of them is the whole reason the build is sliced
    // across frames.
    void Catalog::AddEntry(Pack& pack, Entry entry) {
        // A path that does not resolve is the single most common ODF authoring mistake,
        // and in game it shows up as a solid-colour body rather than an error. Dropping
        // the entry is the only way to keep that off the actor.
        uint32_t width = 0, height = 0;
        switch (TextureUtil::Probe(entry.texture, width, height)) {
            case TextureUtil::Verdict::Ok:
                break;

            case TextureUtil::Verdict::Missing:
                spdlog::warn("Catalog: {} texture not found: {}", entry.qualifiedId, entry.texture);
                ++m_droppedMissingTexture;
                return;

            default:
                spdlog::warn("Catalog: {} texture {} is {}x{} - not a power-of-two DDS, skipping to avoid a crash",
                             entry.qualifiedId, entry.texture, width, height);
                ++m_droppedBadDimensions;
                return;
        }

        entry.appearance.texture = entry.texture;
        if (pack.coverTexture.empty()) pack.coverTexture = entry.texture;
        pack.entryIndices.push_back(m_entries.size());
        m_entries.push_back(std::move(entry));
    }

    void Catalog::KeepPack(Pack&& pack, bool fromSlaveTats) {
        if (pack.entryIndices.empty()) {
            // Packs whose textures are not installed (Community Overlays 1 and 2 in
            // this setup) would otherwise show up as empty filters.
            spdlog::info("Catalog: pack \"{}\" contributed no usable overlays, dropping", pack.modId);
            return;
        }

        spdlog::debug("Catalog: {}pack \"{}\" -> {} overlays", fromSlaveTats ? "SlaveTats " : "",
                      pack.modId, pack.entryIndices.size());
        m_packs.push_back(std::move(pack));
        if (fromSlaveTats) ++m_slaveTatsPacks;
    }

    // One call either opens the next manifest or drains overlays out of the one already
    // open. Opening is a file read and a JSON parse and is taken as this call's whole
    // share of the frame; the draining is what the deadline is really policing.
    void Catalog::ProcessOdfConfig() {
        if (m_pendingOdfActive) {
            while (!OutOfTime() && m_pendingOdfIndex < m_pendingOdfEntries.size()) {
                AddEntry(m_pendingOdfPack, std::move(m_pendingOdfEntries[m_pendingOdfIndex]));
                ++m_pendingOdfIndex;
            }

            if (m_pendingOdfIndex >= m_pendingOdfEntries.size()) {
                KeepPack(std::move(m_pendingOdfPack), false);
                m_pendingOdfPack = {};
                m_pendingOdfEntries.clear();
                m_pendingOdfIndex = 0;
                m_pendingOdfActive = false;
            }
            return;
        }

        const auto& path = m_configFiles[m_fileCursor];
        ++m_fileCursor;
        ++m_packsSeen;

        std::string raw;
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                spdlog::warn("Catalog: cannot open {}", path);
                ++m_packsBroken;
                return;
            }
            raw.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        nlohmann::json root;
        try {
            root = nlohmann::json::parse(raw);
        } catch (const std::exception& strictError) {
            try {
                root = nlohmann::json::parse(RepairMissingCommas(raw));
                spdlog::warn("Catalog: {} has malformed JSON ({}) - recovered by inserting the missing separator",
                             path, strictError.what());
            } catch (const std::exception& repairError) {
                // One bad pack must never take out the whole catalog.
                spdlog::warn("Catalog: skipping {} - invalid JSON: {}", path, repairError.what());
                ++m_packsBroken;
                return;
            }
        }

        const auto modId = GetString(root, "modId");
        if (modId.empty() || !root.contains("overlays") || !root["overlays"].is_array()) {
            spdlog::warn("Catalog: skipping {} - no modId or overlays array", path);
            ++m_packsBroken;
            return;
        }

        m_pendingOdfPack = {};
        m_pendingOdfPack.modId = modId;
        m_pendingOdfPack.esp = GetString(root, "esp");
        // Generated SlaveTats configs carry the author's own section name, which
        // reads better than any prettifying of a slug can.
        const auto declaredName = GetString(root, "displayName");
        m_pendingOdfPack.displayName = declaredName.empty() ? PrettifyId(modId) : Widen(declaredName);

        m_pendingOdfEntries.clear();
        m_pendingOdfEntries.reserve(root["overlays"].size());
        m_pendingOdfIndex = 0;

        for (const auto& item : root["overlays"]) {
            if (!item.is_object()) continue;
            ++m_overlaysSeen;

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

            entry.gender = ParseGender(GetString(item, "gender"));
            entry.type = GetString(item, "type");
            entry.theme = GetString(item, "theme");
            entry.set = GetString(item, "set");
            entry.displayName = PrettifyId(entry.overlayId);

            if (item.contains("color")) entry.appearance.color = ParseColor(item["color"]);
            if (item.contains("glowColor")) entry.appearance.glowColor = ParseColor(item["glowColor"]);
            entry.appearance.alpha = ParseNumber<float>(item, "alpha");
            entry.appearance.glowIntensity = ParseNumber<float>(item, "glowIntensity");

            m_pendingOdfEntries.push_back(std::move(entry));
        }

        m_pendingOdfActive = true;
    }

    // The same shape as the ODF path: a call either takes the next manifest or the next
    // section, or drains tattoos out of the section already open. Alpia's largest section
    // is 174 tattoos and its manifest is 861 of them, so neither a manifest nor a section
    // is small enough to do in one go.
    void Catalog::ProcessSlaveTatsPack() {
        if (m_pendingStActive) {
            const auto& source = m_pendingSlaveTats[m_pendingSlaveTatsPack];

            while (!OutOfTime() && m_pendingSlaveTatsTattoo < source.tattoos.size()) {
                const auto& tattoo = source.tattoos[m_pendingSlaveTatsTattoo];
                ++m_pendingSlaveTatsTattoo;
                ++m_overlaysSeen;

                Entry entry;
                entry.modId = source.modId;
                entry.overlayId = tattoo.id;
                entry.qualifiedId = source.modId + ":" + tattoo.id;

                auto location = Skee::ParseLocation(tattoo.slot);
                if (!location) {
                    ++m_droppedBadSlot;
                    continue;
                }
                entry.location = *location;

                entry.texture = TextureUtil::Normalize(tattoo.texture);
                if (entry.texture.empty()) continue;

                // SlaveTats declares no gender, colour or alpha per tattoo: its packs are
                // unisex and untinted. Untinted is the useful half - these are greyscale
                // designs, so the colour palette drives them properly.
                entry.gender = Gender::Any;
                entry.type = std::string(SlaveTats::kOverlayType);
                entry.theme = source.sectionName;
                entry.set = source.manifestStem;
                entry.displayName = Widen(tattoo.displayName);

                AddEntry(m_pendingStPack, std::move(entry));
            }

            if (m_pendingSlaveTatsTattoo < source.tattoos.size()) return;

            KeepPack(std::move(m_pendingStPack), true);
            m_pendingStPack = {};
            m_pendingStActive = false;
            ++m_pendingSlaveTatsPack;

            // Manifest drained; the next call takes the next one.
            if (m_pendingSlaveTatsPack >= m_pendingSlaveTats.size()) {
                m_pendingSlaveTats.clear();
                m_pendingSlaveTatsPack = 0;
            }
            return;
        }

        if (m_pendingSlaveTats.empty()) {
            const auto& path = m_slaveTatsFiles[m_slaveTatsCursor];
            ++m_slaveTatsCursor;

            // Parsing is the whole call's work: Alpia's manifest is 861 tattoos of JSON,
            // which costs more than a frame's worth of texture checks would.
            m_pendingSlaveTats = SlaveTats::ParseManifest(path);
            m_pendingSlaveTatsPack = 0;
            return;
        }

        ++m_packsSeen;
        m_pendingStPack = {};
        m_pendingStPack.modId = m_pendingSlaveTats[m_pendingSlaveTatsPack].modId;
        // Resolved once every pack is known, because whether a section name needs its
        // manifest in front of it depends on what else is installed.
        m_pendingStPack.displayName = Widen(m_pendingSlaveTats[m_pendingSlaveTatsPack].sectionName);
        m_pendingSlaveTatsTattoo = 0;
        m_pendingStActive = true;
    }

    // A section keeps its own name when it is unique across everything installed, which
    // covers the descriptive ones ("Alpia Back", "Slave Marks"). The generic ones -
    // "General" turns up in more than one pack - get their manifest in front, because two
    // identically named pack icons are unpickable.
    void Catalog::NameSlaveTatsPacks() {
        std::unordered_map<std::string, int> sectionCounts;
        for (const auto& pack : m_packs) {
            if (!SlaveTats::IsImportedPack(pack.modId) || pack.entryIndices.empty()) continue;
            ++sectionCounts[m_entries[pack.entryIndices.front()].theme];
        }

        for (auto& pack : m_packs) {
            if (!SlaveTats::IsImportedPack(pack.modId) || pack.entryIndices.empty()) continue;

            const auto& entry = m_entries[pack.entryIndices.front()];
            if (sectionCounts[entry.theme] > 1) {
                pack.displayName = Widen(entry.set + ": " + entry.theme);
            }
        }
    }

    void Catalog::Finalize() {
        NameSlaveTatsPacks();

        std::sort(m_packs.begin(), m_packs.end(),
                  [](const Pack& a, const Pack& b) { return a.modId < b.modId; });

        const bool success = !m_packs.empty();
        m_state = success ? LoadState::Ready : LoadState::Failed;

        spdlog::info("Catalog: {} manifests read, {} broken, {} packs kept ({} from SlaveTats), {} of {} overlays usable",
                     m_packsSeen, m_packsBroken, m_packs.size(), m_slaveTatsPacks, m_entries.size(),
                     m_overlaysSeen);
        spdlog::info("Catalog: dropped {} for a missing texture, {} for bad dimensions, {} for an unknown slot",
                     m_droppedMissingTexture, m_droppedBadDimensions, m_droppedBadSlot);

        if (!success) {
            spdlog::error("Catalog: no usable overlay packs found - the menu will have nothing to show");
        }

        // Moved out first: a callback may open the menu, which asks the catalog for
        // things, and one of them re-entering StartBuildAsync must not find a list that
        // is still being walked.
        auto waiting = std::move(m_callbacks);
        m_callbacks.clear();
        for (const auto& callback : waiting) callback(success);
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
