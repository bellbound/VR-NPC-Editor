#pragma once

#include <optional>
#include <string>
#include <vector>

// Talks to RaceMenu's engine extender.
//
// On Skyrim VR the extender is skeevr.dll 3.4.5, an old-style SKSE64 plugin that does
// NOT publish the modern IInterfaceMap - the native interface exchange added for AE
// simply is not there, and dispatching to "skee" fails. Overlay Distribution Framework
// hits the same wall and logs "Runtime below 1.6.1130 detected - using Papyrus SKEE
// interface", so this bridge takes the same route:
//
//   writes  -> the NiOverride Papyrus globals, dispatched through the VM
//   reads   -> the actor's own 3D, since the overlay nodes carry the applied texture
//
// Reading the 3D rather than asking NiOverride keeps every query synchronous (a Papyrus
// call only returns through an async callback) and has the bonus that overlays ODF
// distributed at spawn are visible to us too.
namespace Skee {
    enum class Location { Body, Hand, Feet, Face };

    const char* LocationName(Location loc);
    std::optional<Location> ParseLocation(std::string_view slot);

    struct Appearance {
        std::string texture;
        std::optional<uint32_t> color;
        std::optional<float> alpha;
        std::optional<uint32_t> glowColor;
        std::optional<float> glowIntensity;
    };

    // Loads the overlay slot counts from skeevr.ini / skee64.ini. Safe to call twice.
    bool Initialize();
    bool IsAvailable();

    uint32_t GetSlotCount(Location loc);
    std::string GetNodeName(Location loc, uint32_t index);

    // NPCs have no overlay nodes until this runs (bPlayerOnly=1). Idempotent.
    bool EnsureOverlays(RE::Actor* actor);

    // Reads the diffuse texture the overlay node currently carries. Empty when the slot
    // is unused - RaceMenu leaves those on its transparent default texture.
    std::optional<std::string> GetSlotTexture(RE::Actor* actor, const std::string& node);
    bool IsSlotOccupied(RE::Actor* actor, const std::string& node);

    std::optional<std::string> FindFreeSlot(RE::Actor* actor, Location loc);
    std::vector<std::string> GetOccupiedSlots(RE::Actor* actor, Location loc);

    // Persistent: recorded by RaceMenu and written to its co-save.
    bool ApplyToSlot(RE::Actor* actor, bool isFemale, const std::string& node, const Appearance& look);
    bool ClearSlot(RE::Actor* actor, bool isFemale, const std::string& node);

    // Non-persistent (NiOverride's own persist=false), so a preview leaves no trace.
    bool PreviewOnSlot(RE::Actor* actor, bool isFemale, const std::string& node, const Appearance& look);

    void Flush(RE::Actor* actor);
}
