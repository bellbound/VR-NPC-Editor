#include "overlay/OverlayHistory.h"

#include <spdlog/spdlog.h>

#include "overlay/OverlayCatalog.h"

namespace NPCEditor::Overlay {
    namespace {
        // Deep enough to cover a sitting's worth of pressing without letting a held
        // chevron's worth of commits grow without bound.
        constexpr size_t kMaxDepth = 32;
    }

    History* History::GetSingleton() {
        static History instance;
        return &instance;
    }

    Snapshot History::Capture(RE::Actor* actor) {
        const auto* state = actor ? StateManager::GetSingleton()->Find(actor) : nullptr;
        return state ? state->applied : Snapshot{};
    }

    void History::Record(RE::Actor* actor, Snapshot before) {
        if (!actor) return;

        // The press may have changed nothing this history can see - a commit of an
        // overlay already on, a retint to the colour it already wore. A step back that
        // lands where you already are reads as a dead button.
        if (before == Capture(actor)) return;

        m_undo.push_back(std::move(before));
        if (m_undo.size() > kMaxDepth) m_undo.erase(m_undo.begin());

        // The redo stack described a future that this press has just replaced.
        m_redo.clear();
    }

    bool History::Undo(RE::Actor* actor) {
        if (!actor || m_undo.empty()) return false;

        // Captured now rather than when the press was recorded: equips and overlay
        // writes land a frame or more after the press, and whatever has drifted since
        // is what a redo should honestly put back.
        m_redo.push_back(Capture(actor));

        const auto snapshot = std::move(m_undo.back());
        m_undo.pop_back();

        spdlog::info("History: undo -> {} overlay(s) on {:08X}", snapshot.size(), actor->GetFormID());
        Restore(actor, snapshot);
        return true;
    }

    bool History::Redo(RE::Actor* actor) {
        if (!actor || m_redo.empty()) return false;

        m_undo.push_back(Capture(actor));

        const auto snapshot = std::move(m_redo.back());
        m_redo.pop_back();

        spdlog::info("History: redo -> {} overlay(s) on {:08X}", snapshot.size(), actor->GetFormID());
        Restore(actor, snapshot);
        return true;
    }

    void History::Restore(RE::Actor* actor, const Snapshot& snapshot) {
        auto* state = StateManager::GetSingleton();
        state->ClearAll(actor);

        const auto* catalog = Catalog::GetSingleton();
        for (const auto& applied : snapshot) {
            const auto* entry = catalog->FindEntry(applied.qualifiedId);
            if (!entry) {
                spdlog::warn("History: '{}' is no longer in the catalog, cannot restore it",
                             applied.qualifiedId);
                continue;
            }

            // Back into the slot it came out of, with the tint it had. The slot matters
            // as much as the tint: the clear above is a Papyrus call that has not run
            // yet, so a fresh search would still read that slot as occupied and spend a
            // second one on the same overlay. The VM runs the two in the order they are
            // queued, so the clear always lands first.
            const auto look = LookFor(*entry, applied.appearance);
            state->Apply(actor, *entry, &look, applied.node);
        }
    }

    void History::Clear() {
        m_undo.clear();
        m_redo.clear();
    }
}
