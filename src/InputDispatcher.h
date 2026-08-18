#pragma once

#include "api/ThreeDUIActorMenu.h"
#include "higgsinterface001.h"
#include "log.h"
#include "menu/MenuRouter.h"

// Registers this mod with the 3DUI ActorMenu, which owns the shared
// "grab an NPC, press trigger" gesture and disambiguates between mods that want it.
//
// One slot per editor rather than one for the mod. The two editors used to be reached
// through a button on each other's tool row, which meant opening the wrong one first
// and paying a rebuild to correct it; the actor menu already exists to ask "which of
// these do you want", so the question is asked there. An actor only one editor applies
// to never sees a wheel for it: the ActorMenu opens a lone eligible element straight
// away.
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

        // The palette and the T-pose figure: the same two pictures the tool rows used
        // for the buttons this replaces, so the icons still say the same things.
        const bool overlays = Register(actorMenu, "overlays", "textures\\VRNPCEditor\\paint-palette.dds",
                                       L"Skin overlays", &InputDispatcher::IsOverlaysEligible,
                                       &InputDispatcher::OnOverlaysActivated);
        const bool body = Register(actorMenu, "body", "textures\\VRNPCEditor\\tpose.dds",
                                   L"Body", &InputDispatcher::IsBodyEligible,
                                   &InputDispatcher::OnBodyActivated);

        if (!overlays && !body) {
            spdlog::error("InputDispatcher: failed to register with ActorMenu");
            return;
        }

        m_initialized = true;
        spdlog::info("InputDispatcher: registered with ActorMenu (overlays={}, body={})", overlays, body);
    }

private:
    InputDispatcher() = default;
    InputDispatcher(const InputDispatcher&) = delete;
    InputDispatcher& operator=(const InputDispatcher&) = delete;

    bool Register(P3DUI::ActorMenuInterface* actorMenu, const char* elementId, const char* texture,
                  const wchar_t* tooltip, P3DUI::ActorMenuEligibilityCallback isEligible,
                  P3DUI::ActorMenuActivationCallback onActivate) {
        auto config = P3DUI::ActorMenuElementConfig::Default("VRNPCEditor", elementId);
        config.texturePath = texture;
        config.tooltip = tooltip;
        config.scale = 1.4f;

        if (actorMenu->RegisterElement(config, isEligible, onActivate, this)) return true;

        spdlog::error("InputDispatcher: could not register the \"{}\" slot", elementId);
        return false;
    }

    // Runs on every actor-menu open, once per slot, so it stays cheap and rejects early.
    // Eligibility is not told which slot it is answering for, so there is one callback
    // per slot and they share this.
    static bool IsSlotAvailable(RE::Actor* actor, NPCEditor::Editor editor) {
        if (!actor || actor->IsDead() || actor->IsChild()) return false;

        auto* race = actor->GetRace();
        if (race && !race->GetPlayable() && !race->AllowsPickpocket()) return false;

        // The editor already up on this actor has nothing to offer. The other one does,
        // and offering it is how you move between the two now that neither carries a
        // button for the other - the router keeps the sitting open across the change.
        auto* router = NPCEditor::MenuRouter::GetSingleton();
        if (router->GetOpenTarget() == actor && router->IsEditorOpen(editor)) return false;

        // An overlay on a shared base record cannot be persisted without spreading to
        // every actor off that base, which is why a generic NPC gets the body slot and
        // not the overlay one.
        return router->IsAvailable(editor, actor);
    }

    static bool IsOverlaysEligible(RE::Actor* actor, void*) {
        return IsSlotAvailable(actor, NPCEditor::Editor::Overlay);
    }

    static bool IsBodyEligible(RE::Actor* actor, void*) {
        return IsSlotAvailable(actor, NPCEditor::Editor::Body);
    }

    static void Open(RE::Actor* actor, NPCEditor::Editor editor) {
        if (!actor) return;

        // The NPC is held in one hand and the trigger was pulled with the other, so the
        // menu belongs at the trigger hand.
        bool npcInLeftHand = false;
        if (g_higgsInterface) {
            auto* leftObject = g_higgsInterface->GetGrabbedObject(true);
            if (leftObject && leftObject->As<RE::Actor>() == actor) npcInLeftHand = true;
        }

        NPCEditor::MenuRouter::GetSingleton()->OpenForActor(actor, !npcInLeftHand, editor);
    }

    static void OnOverlaysActivated(RE::Actor* actor, const char*, const char*, void*) {
        Open(actor, NPCEditor::Editor::Overlay);
    }

    static void OnBodyActivated(RE::Actor* actor, const char*, const char*, void*) {
        Open(actor, NPCEditor::Editor::Body);
    }

    bool m_initialized = false;
};
