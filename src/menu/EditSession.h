#pragma once

#include <string>
#include <vector>

namespace NPCEditor {
    // One snapshot per NPC you sit down to edit, shared by both editors.
    //
    // A session begins when the actor menu opens an editor on an actor and ends when the
    // menu is closed - switching between the overlay and body editors stays inside the
    // same session. That is the whole point: Undo in either menu reverts everything done
    // since you opened the NPC, not just the half that menu is responsible for.
    //
    // Undressing is deliberately not part of the snapshot. It is a way to see the body
    // while editing rather than a change to the NPC, so it is always reversed when the
    // session ends, whether or not you press Undo.
    class EditSession {
    public:
        static EditSession* GetSingleton();

        // Snapshots the actor's preset, weight and overlays. Re-opening the same actor
        // while a session is live keeps the original snapshot, so Undo still reaches all
        // the way back.
        void Begin(RE::Actor* actor);

        // Redresses the actor and drops the snapshot.
        void End();

        bool IsActive() const { return m_active; }
        RE::Actor* GetActor() const;

        // Whether Undo has anything to revert - the tool rows show the button only then.
        bool HasChanges() const { return m_active && m_changed; }

        // Called by whichever editor just changed something.
        void NoteChange(const char* what);

        // Restores preset, weight and overlays as they were when the session began.
        void Undo();

    private:
        EditSession() = default;
        EditSession(const EditSession&) = delete;
        EditSession& operator=(const EditSession&) = delete;

        RE::ActorHandle m_handle;
        bool m_active = false;
        bool m_changed = false;

        std::string m_preset;                  // OBody preset assigned at open, may be empty
        float m_weight = 0.0f;                 // TESNPC::weight at open
        std::vector<std::string> m_overlays;   // qualifiedIds applied at open
    };
}
