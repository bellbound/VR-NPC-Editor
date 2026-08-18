#pragma once

#include <string>
#include <vector>

// One place that knows what is installed and what that permits.
//
// Every conditional button and every eligibility test asks this rather than
// null-checking an interface pointer of its own, so "why is that button missing?"
// has exactly one answer to read, and it is in the log.
//
// Silent to the player by design: a missing optional dependency hides a feature, it
// does not pop a notification.
namespace NPCEditor::Health {
    struct Dependency {
        std::string name;
        bool present = false;
        std::string detail;   // version, or why it was rejected
        bool required = false;
    };

    enum class Feature {
        Overlays,       // the ODF skin-overlay wheel
        Body,           // the OBody preset grid
        Weight,         // the weight cycle button
        ClothesToggle   // undress/redress
    };

    // Probes what can be probed this early: 3DUI, HIGGS, RaceMenu, OBody.
    void RunEarlyChecks();

    // Re-probes anything that may have registered late, then writes the summary block.
    void RunDataLoadedChecks();

    bool IsFeatureAvailable(Feature feature);

    // Whether the actor-menu entry should offer anything at all for this actor.
    // Overlays additionally need a persistable editorID; the body menu does not,
    // because OBody stores its assignment per actor instance rather than per base.
    bool AnyEditorAvailable(RE::Actor* actor);

    // Overlays can be committed for this specific actor (unique, with an editorID).
    bool CanEditOverlays(RE::Actor* actor);
    bool CanEditBody(RE::Actor* actor);

    // Weight lives on the base record, which non-unique actors share.
    bool CanEditWeight(RE::Actor* actor);

    const std::vector<Dependency>& GetDependencies();
}
