#pragma once

#include <functional>
#include <string>
#include <vector>

// Thin wrapper over OBody NG's C++ plugin API.
//
// OBody's own types are deliberately kept out of this header: API.h refers to bare
// `Actor` and `TESForm` and expects the consumer to define them, so it is included in
// exactly one translation unit (ObodyBridge.cpp) rather than leaking those aliases
// into every file that wants to ask whether OBody is around.
//
// Two gates guard every call:
//   IsAvailable() - OBody answered the interface handshake at all.
//   IsReady()     - OBody is currently in a state where the interface may be touched.
//                   It goes false across every save and load; using the interface then
//                   is documented as memory corruption, not merely a failed call.
namespace NPCEditor::Obody {
    // Dispatches RequestPluginInterface. Call once, at kPostPostLoad.
    void Initialize();

    bool IsAvailable();
    bool IsReady();

    // Human-readable version of what Initialize() found, for the health-check log.
    const std::string& GetStatus();

    // Invoked (on OBody's thread) the moment the interface stops being safe, so an open
    // menu can take itself down before it calls into a mid-teardown OBody.
    void SetUnreadyCallback(std::function<void()> callback);

    // BodySlide presets applicable to this actor's sex, blacklisted ones included -
    // a blacklisted preset is one OBody will not auto-assign, not one it refuses.
    // Returned by value: OBody's own string_views die at the next unready event.
    std::vector<std::string> GetPresetNames(RE::Actor* actor);

    // Empty when no preset is assigned, or when OBody is unavailable or unready.
    std::string GetAssignedPreset(RE::Actor* actor);

    // Assigns and applies immediately, so the change is visible while hovering.
    // An empty name unassigns. Returns false if the preset was not recognised.
    bool ApplyPreset(RE::Actor* actor, const std::string& presetName);

    // Re-applies the assigned preset's morphs. Needed after DoReset3D, which drops them.
    void ReapplyMorphs(RE::Actor* actor);

    // Makes sure OBody has processed the actor for the current distribution key, so
    // GetAssignedPreset returns something meaningful on a never-seen NPC.
    void EnsureProcessed(RE::Actor* actor);
}
