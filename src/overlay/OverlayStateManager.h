#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "overlay/OverlayCatalog.h"

namespace Overlay {
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

        bool Apply(RE::Actor* actor, const Entry& entry);
        bool Remove(RE::Actor* actor, const Entry& entry);
        bool IsApplied(RE::Actor* actor, const Entry& entry) const;

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
