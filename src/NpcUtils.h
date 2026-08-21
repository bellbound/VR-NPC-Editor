#pragma once

#include <RE/Skyrim.h>
#include "log.h"

namespace NpcUtils
{
    // Check if the grabbed object is an NPC (Actor)
    inline bool IsGrabbingNpc(RE::TESObjectREFR* grabbedObj)
    {
        if (!grabbedObj) {
            return false;
        }

        auto* actor = grabbedObj->As<RE::Actor>();
        if (!actor) {
            return false;
        }

        spdlog::debug("NpcUtils: Grabbed object {:08X} IS an Actor (NPC: {})",
            grabbedObj->GetFormID(),
            actor->GetName());
        return true;
    }

    // Only unique, named NPCs are offered the overlay editor. The reason was ODF, whose
    // rules could target an NPC only by editorID - one a whole bandit template shares.
    // RaceMenu persists per actor instead, so this is now a choice rather than a limit.
    inline std::string GetPersistableEditorID(RE::Actor* actor)
    {
        auto* base = actor ? actor->GetActorBase() : nullptr;
        if (!base || !base->IsUnique()) {
            return {};
        }

        const char* editorId = base->GetFormEditorID();
        return (editorId && *editorId) ? editorId : std::string{};
    }

    // Get the Actor from a reference, or nullptr if not an actor
    inline RE::Actor* GetActor(RE::TESObjectREFR* ref)
    {
        return ref ? ref->As<RE::Actor>() : nullptr;
    }

    // Get the actor's name safely
    inline const char* GetActorName(RE::TESObjectREFR* ref)
    {
        auto* actor = GetActor(ref);
        return actor ? actor->GetName() : "unknown";
    }
}
