#pragma once

#include <vector>

#include "overlay/OverlayStateManager.h"

namespace NPCEditor::Overlay {
    // Undo and redo for the overlay editor, built the way VR Dress Up's is: every press
    // that changes what the actor is wearing records a snapshot of them taken just before
    // it, undo puts that snapshot back, and redo puts back the snapshot taken at the
    // moment of the undo. Nothing here knows how to invert an individual press, which is
    // the point - a restore stays right even after something this history never saw has
    // moved the actor in between.
    //
    // A snapshot is what the state manager has the actor wearing, slot and tint included,
    // so a retint is as undoable as an apply. It is deliberately not the raw slots: those
    // also hold overlays no installed pack declares - chargen tattoos, another mod's work
    // - and those are not ours to put back or take away.
    //
    // The history lives for one sitting and is cleared when the menu opens and when it
    // closes, the same lifetime as the session snapshot the body menu's Undo works from.
    using Snapshot = std::vector<StateManager::Applied>;

    class History {
    public:
        static History* GetSingleton();

        // What the actor is wearing, as this mod's record has it.
        static Snapshot Capture(RE::Actor* actor);

        // Called after a press has changed `actor`, with the snapshot taken before it.
        // Drops the redo stack. A press that left the actor exactly as it found them is
        // discarded rather than recorded, so a step back always lands somewhere else.
        void Record(RE::Actor* actor, Snapshot before);

        bool CanUndo() const { return !m_undo.empty(); }
        bool CanRedo() const { return !m_redo.empty(); }

        // Both put a snapshot on the actor and return true when they did. Main thread
        // only: the restore writes through the Papyrus VM.
        bool Undo(RE::Actor* actor);
        bool Redo(RE::Actor* actor);

        void Clear();

    private:
        History() = default;
        History(const History&) = delete;
        History& operator=(const History&) = delete;

        // Takes every overlay of ours off `actor` and puts `snapshot` on instead.
        static void Restore(RE::Actor* actor, const Snapshot& snapshot);

        std::vector<Snapshot> m_undo;
        std::vector<Snapshot> m_redo;
    };
}
