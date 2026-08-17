#pragma once

#include "api/ThreeDUIActorMenu.h"
#include "higgsinterface001.h"
#include "log.h"
#include "menu/OverlayMenuManager.h"
#include "NpcUtils.h"
#include "skee/SkeeBridge.h"

// Registers this mod with the 3DUI ActorMenu, which owns the shared
// "grab an NPC, press trigger" gesture and disambiguates between mods that want it.
class InputDispatcher {
public:
    static InputDispatcher* GetSingleton() {
        static InputDispatcher instance;
        return &instance;
    }

    void Initialize() {
        if (m_initialized) return;

        auto* actorMenu = P3DUI::GetActorMenuInterface();
        if (!actorMenu) {
            spdlog::error("InputDispatcher: ActorMenu interface unavailable, the menu cannot be reached");
            return;
        }

        auto config = P3DUI::ActorMenuElementConfig::Default("VRSkinOverlayMenu", "skinoverlays");
        config.texturePath = "textures\\VRSkinOverlays\\paint-palette.dds";
        config.tooltip = L"Skin Overlays";
        config.scale = 1.4f;

        if (!actorMenu->RegisterElement(config, &InputDispatcher::IsEligible, &InputDispatcher::OnActivate, this)) {
            spdlog::error("InputDispatcher: failed to register with ActorMenu");
            return;
        }

        m_initialized = true;
        spdlog::info("InputDispatcher: registered with ActorMenu");
    }

private:
    InputDispatcher() = default;
    InputDispatcher(const InputDispatcher&) = delete;
    InputDispatcher& operator=(const InputDispatcher&) = delete;

    // Runs on every actor-menu open, so it stays cheap and rejects early.
    static bool IsEligible(RE::Actor* actor, void*) {
        if (!actor || actor->IsDead() || actor->IsChild()) return false;

        // Without RaceMenu's engine extender there is nothing this menu could do.
        if (!Skee::IsAvailable()) return false;

        auto* race = actor->GetRace();
        if (race && !race->GetPlayable() && !race->AllowsPickpocket()) return false;

        // Overlays chosen here are persisted through an ODF rule keyed on editorID, and
        // a generic base record's editorID is shared by every actor spawned from it.
        // Rather than silently spreading one choice across a whole template, the entry
        // simply does not appear for those actors.
        if (NpcUtils::GetPersistableEditorID(actor).empty()) {
            spdlog::trace("InputDispatcher: {:08X} is not a unique NPC, not offering the menu", actor->GetFormID());
            return false;
        }

        auto* menu = Overlay::MenuManager::GetSingleton();
        if (!menu->IsInitialized()) return false;
        if (menu->IsOpen() && menu->GetTargetActor() == actor) return false;

        return true;
    }

    static void OnActivate(RE::Actor* actor, const char*, const char*, void*) {
        if (!actor) return;

        // The NPC is held in one hand and the trigger was pulled with the other, so the
        // menu belongs at the trigger hand.
        bool npcInLeftHand = false;
        if (g_higgsInterface) {
            auto* leftObject = g_higgsInterface->GetGrabbedObject(true);
            if (leftObject && leftObject->As<RE::Actor>() == actor) npcInLeftHand = true;
        }

        Overlay::MenuManager::GetSingleton()->OpenForActor(actor, !npcInLeftHand);
    }

    bool m_initialized = false;
};
