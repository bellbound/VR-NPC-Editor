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

    // ODF distribution rules can only target an NPC by editorID, and a generic base
    // record's editorID is shared by every actor spawned from it. Anything but a
    // unique, named NPC would spread one tattoo across a whole bandit template, so
    // those actors are excluded from the menu entirely.
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
