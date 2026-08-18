#include "menu/EditSession.h"

#include <spdlog/spdlog.h>

#include "dressup/UndressManager.h"
#include "obody/ObodyBridge.h"
#include "overlay/OverlayCatalog.h"
#include "overlay/OverlayStateManager.h"

namespace NPCEditor {
    EditSession* EditSession::GetSingleton() {
        static EditSession instance;
        return &instance;
    }

    RE::Actor* EditSession::GetActor() const {
        auto actor = m_handle.get();
        return actor ? actor.get() : nullptr;
    }

    void EditSession::Begin(RE::Actor* actor) {
        if (!actor) return;

        // Switching editors re-enters here. Re-snapshotting would quietly make Undo mean
        // "since the switch", which is not what anyone pressing it expects.
        if (m_active && GetActor() == actor) return;

        if (m_active) End();

        m_handle = actor->GetHandle();
        m_active = true;
        m_changed = false;

        auto* base = actor->GetActorBase();
        m_weight = base ? base->weight : 0.0f;
        m_preset = Obody::GetAssignedPreset(actor);

        m_overlays.clear();
        auto* state = Overlay::StateManager::GetSingleton();
        state->SyncFromActor(actor);
        if (const auto* actorState = state->Find(actor)) {
            for (const auto& applied : actorState->applied) m_overlays.push_back(applied.qualifiedId);
        }

        spdlog::info("Session: opened on {:08X} ({}) - preset='{}' weight={:.0f} overlays={}",
                     actor->GetFormID(), actor->GetName(), m_preset, m_weight, m_overlays.size());
    }

    void EditSession::End() {
        if (!m_active) return;

        // Always, regardless of Undo: undressing is how you see the body while editing,
        // never something to leave an NPC standing in.
        if (auto* actor = GetActor()) {
            auto* undress = UndressManager::GetSingleton();
            if (undress->IsUndressed(actor)) {
                spdlog::info("Session: redressing {:08X} on close", actor->GetFormID());
                undress->Redress(actor);
            }
        }

        spdlog::debug("Session: closed (changed={})", m_changed);

        m_active = false;
        m_changed = false;
        m_preset.clear();
        m_overlays.clear();
        m_handle = {};
    }

    void EditSession::NoteChange(const char* what) {
        if (!m_active) return;
        if (!m_changed) spdlog::debug("Session: first change ({}), Undo is now offered", what);
        m_changed = true;
    }

    void EditSession::Undo() {
        auto* actor = GetActor();
        if (!m_active || !actor) return;

        spdlog::info("Session: undoing everything since open on {:08X}", actor->GetFormID());

        // Overlays: clear what is on the actor now, then put the snapshot back. Going
        // through the catalog rather than the raw slots keeps the state manager's record
        // and the actor's slots in step.
        auto* state = Overlay::StateManager::GetSingleton();
        const size_t cleared = state->ClearAll(actor);

        size_t restored = 0;
        const auto* catalog = Overlay::Catalog::GetSingleton();
        for (const auto& qualifiedId : m_overlays) {
            const auto* entry = catalog->FindEntry(qualifiedId);
            if (!entry) {
                spdlog::warn("Session: '{}' is no longer in the catalog, cannot restore it", qualifiedId);
                continue;
            }
            if (state->Apply(actor, *entry)) ++restored;
        }
        spdlog::info("Session: overlays - cleared {}, restored {}", cleared, restored);

        // Weight before the preset, so the single 3D reset below covers both.
        auto* base = actor->GetActorBase();
        const bool weightChanged = base && base->weight != m_weight;
        if (weightChanged) {
            spdlog::info("Session: weight {:.0f} -> {:.0f}", base->weight, m_weight);
            base->weight = m_weight;
            actor->DoReset3D(true);
        }

        if (!m_preset.empty()) {
            spdlog::info("Session: restoring preset '{}'", m_preset);
            Obody::ApplyPreset(actor, m_preset);
        } else if (weightChanged) {
            // No preset to reapply, but the reset above dropped the morphs.
            Obody::ReapplyMorphs(actor);
        }

        m_changed = false;
    }
}
