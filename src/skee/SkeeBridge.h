#pragma once

#include <optional>
#include <string>
#include <vector>

// Talks to RaceMenu's engine extender (skeevr.dll in VR, skee64.dll on SE) through
// the public IPluginInterface handshake. Everything here degrades to a no-op and a
// log line when RaceMenu is absent - nothing in this file may throw or crash.
namespace Skee {
    // Mirrors IOverlayInterface::OverlayLocation. Kept separate so the rest of the
    // mod does not have to include the SKEE header (it forward-declares game types
    // in the global namespace and clashes with RE::).
    enum class Location { Body, Hand, Feet, Face };

    const char* LocationName(Location loc);

    // Maps an ODF pack's "slot" string ("body", "hands", "feet", "face").
    std::optional<Location> ParseLocation(std::string_view slot);

    // How an overlay should look once applied. Only `texture` is required; the rest
    // come from the ODF pack's optional appearance defaults.
    struct Appearance {
        std::string texture;
        std::optional<uint32_t> color;           // 0xRRGGBB tint
        std::optional<float> alpha;              // 0.0 - 1.0
        std::optional<uint32_t> glowColor;       // 0xRRGGBB
        std::optional<float> glowIntensity;      // RaceMenu slider / 10
    };

    // A snapshot of one overlay node's live shader properties, used to undo a preview.
    struct SlotSnapshot {
        std::string node;
        std::optional<std::string> texture;
        std::optional<int32_t> color;
        std::optional<float> alpha;
        std::optional<int32_t> glowColor;
        std::optional<float> glowIntensity;
    };

    // Runs the interface exchange. Safe to call more than once; returns true once all
    // required interfaces have been acquired.
    bool Initialize();
    bool IsAvailable();

    // Slot geometry, always asked of SKEE rather than hardcoded - VR ships 6 body
    // slots where SE ships 8, and users edit these counts.
    uint32_t GetSlotCount(Location loc);
    std::string GetNodeName(Location loc, uint32_t index);

    // NPCs have no overlay nodes until this runs (skeevr.ini bPlayerOnly=1).
    bool EnsureOverlays(RE::Actor* actor);

    bool IsSlotOccupied(RE::Actor* actor, bool isFemale, const std::string& node);
    std::optional<std::string> GetSlotTexture(RE::Actor* actor, bool isFemale, const std::string& node);

    // First slot of `loc` with no texture override, or nullopt when all are in use.
    std::optional<std::string> FindFreeSlot(RE::Actor* actor, bool isFemale, Location loc);

    // Every node of `loc` that currently carries a texture override.
    std::vector<std::string> GetOccupiedSlots(RE::Actor* actor, bool isFemale, Location loc);

    // Persistent: recorded in SKEE's database and written to the co-save.
    bool ApplyToSlot(RE::Actor* actor, bool isFemale, const std::string& node, const Appearance& look);
    bool ClearSlot(RE::Actor* actor, bool isFemale, const std::string& node);

    // Non-persistent: stamps the shader directly, leaving the database untouched.
    SlotSnapshot SnapshotSlot(RE::Actor* actor, const std::string& node);
    bool PreviewOnSlot(RE::Actor* actor, const std::string& node, const Appearance& look);
    bool RestoreSlot(RE::Actor* actor, const SlotSnapshot& snapshot);

    // Pushes pending database changes onto the actor's 3D.
    void Flush(RE::Actor* actor);
}
