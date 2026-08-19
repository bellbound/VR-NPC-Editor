#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "overlay/OverlayCatalog.h"

namespace NPCEditor::Overlay {
    // Tracks which overlays this mod put on which actor, so the wheel can highlight
    // them, the tool row can clear and restore them, and a mid-session save keeps them.
    class StateManager {
    public:
        static StateManager* GetSingleton();

        struct Applied {
            std::string qualifiedId;
            std::string node;      // the SKEE overlay node it occupies, e.g. "Body [Ovl2]"
        };

        struct ActorState {
            std::string editorId;
            bool isFemale = false;
            std::vector<Applied> applied;
            std::vector<std::string> lastCleared;  // what "restore all" puts back
        };

        // Reads the actor's live overlay slots and reconciles them with our record, so
        // overlays ODF distributed at spawn are recognised as applied too.
        void SyncFromActor(RE::Actor* actor);

        // `appearanceOverride` replaces the pack's own colour/alpha for this apply. Null
        // keeps whatever the pack declared, which is what Undo and ODF restores want.
        //
        // `preferredNode` is the slot to write into, skipping the search. The overlay
        // menu passes the slot its live preview is already sitting in: the preview's
        // release only reaches the actor once the Papyrus VM runs it, so a fresh search
        // would find that slot still occupied and burn a second one on the same overlay.
        bool Apply(RE::Actor* actor, const Entry& entry,
                   const Skee::Appearance* appearanceOverride = nullptr,
                   const std::string& preferredNode = {});
        bool Remove(RE::Actor* actor, const Entry& entry);

        // Repaints an overlay that is already on, in the slot it already occupies.
        // Apply cannot do this - it returns early for anything already applied - and
        // remove-then-apply would hand it back a different slot, changing which overlay
        // draws on top of which.
        bool Retint(RE::Actor* actor, const Entry& entry, const Skee::Appearance& appearance);
        bool IsApplied(RE::Actor* actor, const Entry& entry) const;

        // Takes off what we are tracking and only that - overlays chargen or another
        // mod put on the actor stay where they are. SyncFromActor is what decides who
        // counts as ours: anything whose texture an installed pack declares, however it
        // got there. Foreign textures are left holding their slots, which can leave the
        // actor with none free; that is the honest outcome, since a slot we cannot name
        // is not one we are entitled to empty.
        size_t ClearAll(RE::Actor* actor);
        size_t RestoreAll(RE::Actor* actor);

        bool HasApplied(RE::Actor* actor) const;
        bool HasCleared(RE::Actor* actor) const;
        const ActorState* Find(RE::Actor* actor) const;

        const std::unordered_map<std::string, ActorState>& GetAll() const { return m_actors; }

        void Save(SKSE::SerializationInterface* serialization);
        void Load(SKSE::SerializationInterface* serialization);
        void Revert();

    private:
        ActorState* GetOrCreate(RE::Actor* actor);

        std::unordered_map<std::string, ActorState> m_actors;  // keyed by FormKeyUtil key
    };
}
