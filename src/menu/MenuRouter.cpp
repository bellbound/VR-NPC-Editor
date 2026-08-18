#include "menu/MenuRouter.h"

#include <spdlog/spdlog.h>

#include "health/HealthCheckManager.h"
#include "menu/BodyMenuManager.h"
#include "menu/EditSession.h"
#include "menu/OverlayMenuManager.h"

namespace {
    // Every entry point here can be reached from inside a 3DUI event callback, which
    // runs while 3DUI is walking the very element hierarchy we are about to tear down
    // and rebuild. Doing that work on the next task instead costs one frame and takes
    // the reentrancy out of it entirely.
    void NextFrame(std::function<void()> work) {
        SKSE::GetTaskInterface()->AddTask(std::move(work));
    }
}

namespace NPCEditor {
    MenuRouter* MenuRouter::GetSingleton() {
        static MenuRouter instance;
        return &instance;
    }

    bool MenuRouter::IsAvailable(Editor editor, RE::Actor* actor) const {
        switch (editor) {
            case Editor::Overlay: return Health::CanEditOverlays(actor);
            case Editor::Body:    return Health::CanEditBody(actor);
            default:              return false;
        }
    }

    void MenuRouter::OpenForActor(RE::Actor* actor, bool isLeftHand, Editor editor) {
        if (!actor) return;

        const auto handle = actor->GetHandle();
        NextFrame([this, handle, isLeftHand, editor] {
            auto target = handle.get();
            if (!target) return;
            auto* actor = target.get();

            if (!IsAvailable(editor, actor)) {
                // The actor menu only offers a slot whose editor said it was available,
                // so the two disagreeing is worth a line rather than a silent no-op.
                spdlog::warn("Router: the {} editor is not available for {:08X} ({})",
                             editor == Editor::Overlay ? "overlay" : "body",
                             actor->GetFormID(), actor->GetName());
                return;
            }

            // Only one editor is ever up. Putting both away first also drops any hover
            // preview the overlay menu had left on the previous target.
            //
            // Whether the sitting ends with it depends on who this is: moving to the
            // other editor on the same actor is one continuous sitting, and ending the
            // session would both redress the actor mid-edit and shorten Undo's reach to
            // whichever half you happen to be looking at.
            auto* session = EditSession::GetSingleton();
            if (session->IsActive() && session->GetActor() == actor) {
                HideAll();
            } else {
                CloseAll();
            }

            // Keeps the snapshot a live session already has, so Undo still reaches the
            // start of the sitting rather than to this moment.
            session->Begin(actor);
            Open(editor, actor, isLeftHand);
        });
    }

    void MenuRouter::Open(Editor editor, RE::Actor* actor, bool isLeftHand) {
        if (editor == Editor::Overlay) {
            Overlay::MenuManager::GetSingleton()->OpenForActor(actor, isLeftHand);
        } else {
            BodyMenuManager::GetSingleton()->OpenForActor(actor, isLeftHand);
        }
        spdlog::debug("Router: opened the {} editor", editor == Editor::Overlay ? "overlay" : "body");
    }

    void MenuRouter::HideAll() {
        Overlay::MenuManager::GetSingleton()->Close();
        BodyMenuManager::GetSingleton()->Close();
    }

    void MenuRouter::CloseAll() {
        HideAll();
        EditSession::GetSingleton()->End();
    }

    bool MenuRouter::IsOpen() const {
        return Overlay::MenuManager::GetSingleton()->IsOpen() ||
               BodyMenuManager::GetSingleton()->IsOpen();
    }

    bool MenuRouter::IsEditorOpen(Editor editor) const {
        return editor == Editor::Overlay ? Overlay::MenuManager::GetSingleton()->IsOpen()
                                         : BodyMenuManager::GetSingleton()->IsOpen();
    }

    RE::Actor* MenuRouter::GetOpenTarget() const {
        auto* overlay = Overlay::MenuManager::GetSingleton();
        if (overlay->IsOpen()) return overlay->GetTargetActor();

        auto* body = BodyMenuManager::GetSingleton();
        if (body->IsOpen()) return body->GetTargetActor();

        return nullptr;
    }
}
