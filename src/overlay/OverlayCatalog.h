#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "overlay/SlaveTatsImport.h"
#include "skee/SkeeBridge.h"

// Everything the installed overlay packs declare, filtered down to overlays this game
// can actually show. Built on first menu open, never at game start, so the mod costs
// nothing until it is used.
//
// Two sources feed it: the ODF mod configs, and any SlaveTats pack installed alongside
// them. Both end up as the same entries - a SlaveTats tattoo is an ordinary overlay
// with its own manifest format - and nothing downstream of here can tell them apart.
namespace NPCEditor::Overlay {
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

        // Returns immediately. Work is spread over frames on the main thread, a couple of
        // milliseconds per frame, so a build costs no visible hitch however many packs
        // are installed. The callback runs on the main thread when the build finishes,
        // or straight away if the catalog is already built.
        //
        // Calling this while a build is already running joins that build: the callback
        // is added to the ones it will run when it finishes. That is what lets the menu
        // open in the middle of the preload started at load time and still be told when
        // the packs are there.
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
        using Clock = std::chrono::steady_clock;

        void QueueNextBatch();
        void ProcessBatch();
        bool OutOfTime() const { return Clock::now() >= m_deadline; }

        // Both take one step: opening the next manifest, or draining overlays out of the
        // one already open until this frame's slice is spent. Neither ever runs a whole
        // manifest in one go - the largest packs declare hundreds of overlays and every
        // one of them is an archive lookup, so a manifest at a time was a manifest-sized
        // hitch on whichever frame that manifest came up.
        void ProcessOdfConfig();
        void ProcessSlaveTatsPack();

        void EnumerateConfigs();
        void NameSlaveTatsPacks();
        void Finalize();

        // Verifies a declared overlay's texture and either files it under `pack` or
        // counts why it could not be used. Shared by both sources, which differ only in
        // where the fields come from and not in what makes an overlay usable.
        void AddEntry(Pack& pack, Entry entry);

        // Files the finished pack, or drops it when nothing in it turned out usable.
        void KeepPack(Pack&& pack, bool fromSlaveTats);

        std::atomic<LoadState> m_state{LoadState::NotStarted};

        // Everyone waiting on the build in flight, in the order they asked.
        std::vector<CompleteCallback> m_callbacks;

        // When this frame's slice of the build runs out.
        Clock::time_point m_deadline{};

        std::vector<std::string> m_configFiles;
        size_t m_fileCursor = 0;

        // The ODF manifest being drained. Reading and parsing the file is one frame's
        // work and produces these, every field filled in but the texture unverified;
        // the verification is an archive lookup each and is what the frame slices are
        // really for, so it happens here, a few at a time.
        std::vector<Entry> m_pendingOdfEntries;
        size_t m_pendingOdfIndex = 0;
        Pack m_pendingOdfPack;
        bool m_pendingOdfActive = false;

        std::vector<std::string> m_slaveTatsFiles;
        size_t m_slaveTatsCursor = 0;

        // The manifest being drained, the section within it, and how far into that
        // section's tattoos we are.
        std::vector<SlaveTats::Pack> m_pendingSlaveTats;
        size_t m_pendingSlaveTatsPack = 0;
        size_t m_pendingSlaveTatsTattoo = 0;
        Pack m_pendingStPack;
        bool m_pendingStActive = false;

        std::vector<Pack> m_packs;
        std::vector<Entry> m_entries;

        // Build statistics, reported in one summary line when the build completes.
        size_t m_packsSeen = 0;
        size_t m_slaveTatsPacks = 0;
        size_t m_packsBroken = 0;
        size_t m_overlaysSeen = 0;
        size_t m_droppedMissingTexture = 0;
        size_t m_droppedBadDimensions = 0;
        size_t m_droppedBadSlot = 0;
    };
}
