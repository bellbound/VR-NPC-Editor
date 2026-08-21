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

        bool operator==(const Appearance&) const = default;
    };

    // Loads the overlay slot counts from skeevr.ini / skee64.ini. Safe to call twice.
    bool Initialize();
    bool IsAvailable();

    uint32_t GetSlotCount(Location loc);
    std::string GetNodeName(Location loc, uint32_t index);

    // NPCs have no overlay nodes until this runs (bPlayerOnly=1). Idempotent.
    bool EnsureOverlays(RE::Actor* actor);

    // Whether the overlay geometry for this kind of slot is actually on the actor yet.
    //
    // EnsureOverlays only queues the build - RaceMenu puts it on the SKSE task queue and
    // the geometry appears a frame or more later. Every write below is a no-op against a
    // node that is not there, silently, so anything that means to be seen has to wait for
    // this to come back true first.
    bool HasOverlayNodes(RE::Actor* actor, Location loc);

    // Reads the diffuse texture the overlay node currently carries. Empty when the slot
    // is unused - RaceMenu leaves those on its transparent default texture.
    std::optional<std::string> GetSlotTexture(RE::Actor* actor, const std::string& node);
    bool IsSlotOccupied(RE::Actor* actor, const std::string& node);

    // Note that a slot released moments ago still reads as occupied: every write goes
    // through the Papyrus VM and lands a frame or more later, and this reads the actor's
    // live 3D. A caller that means to reuse a slot it just gave up has to remember it
    // rather than expect to find it here.
    std::optional<std::string> FindFreeSlot(RE::Actor* actor, Location loc);

    // The same question without the answer or the warning FindFreeSlot logs when it
    // comes back empty. Asked repeatedly - the overlay menu polls it to know when there
    // is no room left for another overlay - so a miss must not be an event.
    bool HasFreeSlot(RE::Actor* actor, Location loc);

    std::vector<std::string> GetOccupiedSlots(RE::Actor* actor, Location loc);

    // Persistent: recorded by RaceMenu and written to its co-save.
    bool ApplyToSlot(RE::Actor* actor, bool isFemale, const std::string& node, const Appearance& look);

    // Empties a slot: drops the overrides and repaints the node with RaceMenu's own
    // empty-slot texture. Both halves are needed - see the note on the definition.
    bool ClearSlot(RE::Actor* actor, bool isFemale, const std::string& node);

    // Non-persistent (NiOverride's own persist=false), so a preview leaves no trace.
    bool PreviewOnSlot(RE::Actor* actor, bool isFemale, const std::string& node, const Appearance& look);

    void Flush(RE::Actor* actor);
}
