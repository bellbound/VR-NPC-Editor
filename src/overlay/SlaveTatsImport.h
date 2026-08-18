#pragma once

#include <string>
#include <string_view>
#include <vector>

// SlaveTats packs are overlays too - the same NiOverride textures ODF distributes, just
// described in a different manifest. This reads them so the menu can browse them, and
// declares to ODF only the ones that actually end up on an actor.
//
// That split is deliberate. A mod config is a global statement to ODF that an overlay
// exists, and ODF distribution rules can select overlays by type and theme rather than
// by id - so declaring a whole installed library would quietly drop hundreds of tattoos
// into pools that other mods' rules draw from, and NPCs across the game would start
// wearing overlays nobody picked. Nothing is declared until it is chosen.
//
// The SlaveTats mod itself is not required and is never called; only its texture packs
// are read.
namespace NPCEditor::Overlay {
    struct Entry;

    namespace SlaveTats {
        // Not "tattoo": ODF rules can ask for every overlay of a type, and a type another
        // pack also uses would put these in a pool they were never meant to join. Their
        // own type keeps a declared overlay reachable by id - which is all our rules use -
        // and unreachable by accident. It is also the honest label for a ZaZ pack whose
        // sections are dirt and tears rather than tattoos.
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

        // Whether an ODF_mod_configs filename is one this generated. The catalog skips
        // those when it scans: they only ever restate overlays the manifests already
        // describe, and reading both would list every applied tattoo twice.
        bool IsGeneratedConfig(std::string_view filename);

        // Declares exactly these overlays to ODF, as one generated mod config per pack,
        // so the distribution rules that survive a restart can resolve their ids. Every
        // other installed tattoo stays unknown to ODF. Entries from real ODF packs are
        // ignored - they are already declared by the pack that shipped them.
        //
        // Rewrites the generated set whole, so taking a tattoo back off an actor
        // withdraws its declaration too.
        size_t WriteAppliedConfigs(const std::vector<const Entry*>& applied);

        // Deletes every config this ever generated. Used when the import is switched off.
        size_t PurgeGeneratedConfigs();
    }
}
