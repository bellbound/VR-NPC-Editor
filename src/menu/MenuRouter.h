#pragma once

#include <RE/Skyrim.h>

// Opens and closes the two editors on behalf of the actor menu.
//
// Each editor owns its own 3DUI root and only one is ever visible. The router is the
// only thing that opens or closes them, so "which menu is up" has a single owner.
//
// Which editor to open is not a decision made here: the actor menu offers one slot per
// editor and the player picks, so the choice arrives with the request.
namespace NPCEditor {
    enum class Editor { Overlay, Body };

    class MenuRouter {
    public:
        static MenuRouter* GetSingleton();

        // Puts away whatever is open and opens the named editor on this actor at the
        // given hand. A no-op if that editor is not available for the actor, which the
        // actor menu's eligibility test has already ruled out.
        //
        // Asking for the other editor on the actor already being edited is how you move
        // between them, and it stays inside the same edit session - that session is what
        // Undo reaches back through, and it is meant to span both editors.
        void OpenForActor(RE::Actor* actor, bool isLeftHand, Editor editor);

        // Closes whichever editor is open and ends the edit session. Safe to call when
        // none is open.
        void CloseAll();

        bool IsOpen() const;
        bool IsEditorOpen(Editor editor) const;

        // The actor the open editor is acting on, or null when nothing is open.
        RE::Actor* GetOpenTarget() const;

        bool IsAvailable(Editor editor, RE::Actor* actor) const;

    private:
        MenuRouter() = default;
        MenuRouter(const MenuRouter&) = delete;
        MenuRouter& operator=(const MenuRouter&) = delete;

        void Open(Editor editor, RE::Actor* actor, bool isLeftHand);

        // Puts both editors away without ending the session, for moving between them.
        void HideAll();
    };
}
