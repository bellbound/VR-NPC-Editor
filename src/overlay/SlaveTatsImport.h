#pragma once

#include <string>
#include <string_view>
#include <vector>

// SlaveTats packs are overlays too - the same NiOverride textures ODF distributes, just
// described in a different manifest. This reads them so the menu can browse them, and
// nothing else: they are applied through RaceMenu like any other overlay and nothing is
// ever declared to ODF.
//
// Earlier builds did declare the applied ones, so an ODF rule could name them after a
// restart. A mod config is a global statement to ODF that an overlay exists, and rules
// can select by type and theme rather than by id, so those declarations put tattoos into
// pools other mods' rules drew from. PurgeGeneratedConfigs takes them back.
//
// The SlaveTats mod itself is not required and is never called; only its texture packs
// are read.
namespace NPCEditor::Overlay {
    namespace SlaveTats {
        // The catalog's type for these entries. Its own rather than "tattoo", which is
        // also the honest label for a ZaZ pack whose sections are dirt and tears.
        inline constexpr std::string_view kOverlayType = "slavetats";

        struct Tattoo {
            std::string id;           // slug of the name, unique within its pack
            std::string displayName;  // the author's own name, e.g. "Forehead - Slave"
            std::string slot;         // ODF slot name: body, face, hands or feet
            std::string texture;      // Data-relative, backslashes
        };

        // One SlaveTats section. Alpia alone declares 861 tattoos across eleven of them,
        // and the menu browses a pack an overlay at a time - as a single pack that is a
        // stepper nobody reaches the end of. Sections are also how SlaveTats' own UI
        // splits a pack, so this matches what the author meant their grouping to do.
        struct Pack {
            std::string modId;         // "slavetats-alpia_v2-alpia_back"
            std::string sectionName;   // "Alpia Back"
            std::string manifestStem;  // "Alpia V2"
            std::vector<Tattoo> tattoos;
        };

        // Every manifest in Data/textures/actors/character/slavetats. A directory listing
        // and nothing more, so the catalog can spread the parsing over frames.
        std::vector<std::string> FindManifests();

        // One manifest's sections. Reads and parses the file; no texture checks, no
        // writes - the catalog does its own vetting on the way to building entries.
        std::vector<Pack> ParseManifest(const std::string& path);

        // Whether a pack id came from here rather than from an installed ODF config.
        bool IsImportedPack(std::string_view modId);

        // Whether an ODF_mod_configs filename is one an earlier build generated. The
        // catalog skips those when it scans: they only restate overlays the manifests
        // already describe, and reading both would list every applied tattoo twice.
        bool IsGeneratedConfig(std::string_view filename);

        // Deletes every config those builds generated. Run once at load.
        size_t PurgeGeneratedConfigs();
    }
}
