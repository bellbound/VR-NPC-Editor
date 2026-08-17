#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "skee/SkeeBridge.h"

// Everything the installed ODF add-on packs declare, filtered down to overlays this
// game can actually show. Built on first menu open, never at game start, so the mod
// costs nothing until it is used.
namespace Overlay {
    enum class Gender : uint8_t { Any, Male, Female };

    struct Entry {
        std::string qualifiedId;   // "titkit:light_1_big" - the id ODF distribution rules use
        std::string overlayId;     // "light_1_big"
        std::string modId;         // "titkit"
        std::string texture;       // Data-relative, backslashes, verified to exist
        Skee::Location location = Skee::Location::Body;
        Gender gender = Gender::Any;
        std::string type;          // "tattoo", "areola", "pubichair", ...
        std::string theme;
        std::string set;
        Skee::Appearance appearance;  // the pack's own colour/alpha/glow defaults
        std::wstring displayName;
    };

    struct Pack {
        std::string modId;
        std::string esp;
        std::wstring displayName;
        std::vector<size_t> entryIndices;  // into the flat entry list
        std::string coverTexture;          // first usable swatch, used as the filter icon
    };

    enum class LoadState : uint8_t { NotStarted, Loading, Ready, Failed };

    class Catalog {
    public:
        static Catalog* GetSingleton();

        using CompleteCallback = std::function<void(bool success)>;

        // Returns immediately. Work is spread over frames on the main thread via the
        // SKSE task interface, the same way VR Dress Up builds its armour gallery.
        // The callback runs on the main thread when the build finishes.
        void StartBuildAsync(CompleteCallback callback = nullptr);

        LoadState GetLoadState() const { return m_state.load(); }
        bool IsReady() const { return m_state.load() == LoadState::Ready; }
        float GetProgress() const;

        const std::vector<Pack>& GetPacks() const { return m_packs; }
        const std::vector<Entry>& GetEntries() const { return m_entries; }

        const Pack* FindPack(std::string_view modId) const;
        const Entry* FindEntry(std::string_view qualifiedId) const;

        // Entries of a pack that suit the given actor, i.e. matching gender or unisex.
        std::vector<const Entry*> GetEntriesForPack(const Pack& pack, bool isFemale) const;

        // Which catalog entry a slot's texture came from, so applied overlays can be
        // highlighted and named. Null when the texture came from somewhere else.
        const Entry* FindEntryByTexture(std::string_view texture) const;

    private:
        struct PendingPack;

        void QueueNextBatch();
        void ProcessBatch();
        void EnumerateConfigs();
        void Finalize();

        std::atomic<LoadState> m_state{LoadState::NotStarted};
        CompleteCallback m_callback;

        std::vector<std::string> m_configFiles;
        size_t m_fileCursor = 0;

        std::vector<Pack> m_packs;
        std::vector<Entry> m_entries;

        // Build statistics, reported in one summary line when the build completes.
        size_t m_packsSeen = 0;
        size_t m_packsBroken = 0;
        size_t m_overlaysSeen = 0;
        size_t m_droppedMissingTexture = 0;
        size_t m_droppedBadDimensions = 0;
        size_t m_droppedBadSlot = 0;
    };
}
