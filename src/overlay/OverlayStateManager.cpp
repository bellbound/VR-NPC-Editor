#include "overlay/OverlayStateManager.h"

#include "util/FormKeyUtil.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace NPCEditor::Overlay {
    namespace {
        constexpr uint32_t kActorRecord = 'AOVL';

        // 1: qualifiedId and node per overlay, ids only for the cleared list.
        // 2: both carry the appearance that was written, so the chosen tint survives a
        //    save. A v1 record still loads - it just has no tint on record, and the
        //    pack's own declaration stands in, which is exactly what it meant before.
        constexpr uint32_t kRecordVersion = 2;

        constexpr Skee::Location kAllLocations[] = {
            Skee::Location::Body, Skee::Location::Hand, Skee::Location::Feet, Skee::Location::Face
        };

        bool IsFemale(RE::Actor* actor) {
            auto* base = actor ? actor->GetActorBase() : nullptr;
            return base && base->GetSex() == RE::SEX::kFemale;
        }

        std::string KeyFor(RE::Actor* actor) {
            if (!actor) return {};
            return Persistence::FormKeyUtil::BuildFormKey(actor);
        }

        bool WriteString(SKSE::SerializationInterface* intfc, const std::string& text) {
            const auto length = static_cast<uint32_t>(text.size());
            if (!intfc->WriteRecordData(length)) return false;
            return length == 0 || intfc->WriteRecordData(text.data(), length);
        }

        bool ReadString(SKSE::SerializationInterface* intfc, std::string& out) {
            uint32_t length = 0;
            if (!intfc->ReadRecordData(length)) return false;
            // A corrupt or mismatched co-save must not make us allocate wildly.
            if (length > 0x10000) return false;
            out.assign(length, '\0');
            return length == 0 || intfc->ReadRecordData(out.data(), length);
        }

        // Each optional is a present flag followed by the value only when it is set, so
        // "the pack declared no alpha" stays distinct from "alpha 0", which is invisible.
        template <class T>
        bool WriteOptional(SKSE::SerializationInterface* intfc, const std::optional<T>& value) {
            if (!intfc->WriteRecordData(static_cast<uint8_t>(value.has_value() ? 1 : 0))) return false;
            return !value || intfc->WriteRecordData(*value);
        }

        template <class T>
        bool ReadOptional(SKSE::SerializationInterface* intfc, std::optional<T>& out) {
            uint8_t present = 0;
            if (!intfc->ReadRecordData(present)) return false;
            if (!present) {
                out.reset();
                return true;
            }
            T value{};
            if (!intfc->ReadRecordData(value)) return false;
            out = value;
            return true;
        }

        bool WriteAppearance(SKSE::SerializationInterface* intfc, const std::optional<Skee::Appearance>& look) {
            if (!intfc->WriteRecordData(static_cast<uint8_t>(look.has_value() ? 1 : 0))) return false;
            if (!look) return true;

            return WriteString(intfc, look->texture) && WriteOptional(intfc, look->color) &&
                   WriteOptional(intfc, look->alpha) && WriteOptional(intfc, look->glowColor) &&
                   WriteOptional(intfc, look->glowIntensity);
        }

        bool ReadAppearance(SKSE::SerializationInterface* intfc, std::optional<Skee::Appearance>& out) {
            uint8_t present = 0;
            if (!intfc->ReadRecordData(present)) return false;
            if (!present) {
                out.reset();
                return true;
            }

            Skee::Appearance look;
            if (!ReadString(intfc, look.texture) || !ReadOptional(intfc, look.color) ||
                !ReadOptional(intfc, look.alpha) || !ReadOptional(intfc, look.glowColor) ||
                !ReadOptional(intfc, look.glowIntensity)) {
                return false;
            }
            out = std::move(look);
            return true;
        }
    }

    StateManager* StateManager::GetSingleton() {
        static StateManager instance;
        return &instance;
    }

    StateManager::ActorState* StateManager::GetOrCreate(RE::Actor* actor) {
        const auto key = KeyFor(actor);
        if (key.empty()) return nullptr;

        auto& state = m_actors[key];
        state.isFemale = IsFemale(actor);
        if (state.editorId.empty()) {
            if (auto* base = actor->GetActorBase()) state.editorId = base->GetFormEditorID();
        }
        return &state;
    }

    const StateManager::ActorState* StateManager::Find(RE::Actor* actor) const {
        const auto key = KeyFor(actor);
        if (key.empty()) return nullptr;

        auto it = m_actors.find(key);
        return it == m_actors.end() ? nullptr : &it->second;
    }

    void StateManager::SyncFromActor(RE::Actor* actor) {
        if (!actor || !Skee::IsAvailable()) return;

        auto* state = GetOrCreate(actor);
        if (!state) return;

        const bool female = state->isFemale;
        const auto* catalog = Catalog::GetSingleton();

        std::vector<Applied> live;
        for (auto location : kAllLocations) {
            for (const auto& node : Skee::GetOccupiedSlots(actor, location)) {
                auto texture = Skee::GetSlotTexture(actor, node);
                if (!texture || texture->empty()) continue;

                const auto* entry = catalog->FindEntryByTexture(*texture);
                if (!entry) {
                    // Something outside the installed packs put this here - leave it be,
                    // but do not claim it as ours or offer to restore it.
                    spdlog::debug("State: {} node \"{}\" holds unrecognised texture \"{}\"",
                                  actor->GetFormID(), node, *texture);
                    continue;
                }
                // The tint we already have on record for this overlay survives the
                // reconcile. Reading the slot tells us the texture and nothing else, so
                // rebuilding the row from the actor alone would forget the colour of
                // every overlay we put on - and the next write to the ODF rule file
                // would put the pack's own black back.
                auto known = std::find_if(state->applied.begin(), state->applied.end(),
                                          [&](const Applied& a) { return a.qualifiedId == entry->qualifiedId; });

                std::optional<Skee::Appearance> recorded;
                if (known != state->applied.end()) recorded = known->appearance;
                live.push_back({entry->qualifiedId, node, std::move(recorded)});
            }
        }

        spdlog::debug("State: synced {:08X} - {} overlays live (was tracking {})",
                      actor->GetFormID(), live.size(), state->applied.size());
        state->applied = std::move(live);
    }

    bool StateManager::IsApplied(RE::Actor* actor, const Entry& entry) const {
        const auto* state = Find(actor);
        if (!state) return false;

        return std::any_of(state->applied.begin(), state->applied.end(),
                           [&](const Applied& a) { return a.qualifiedId == entry.qualifiedId; });
    }

    bool StateManager::Apply(RE::Actor* actor, const Entry& entry,
                             const Skee::Appearance* appearanceOverride,
                             const std::string& preferredNode) {
        if (!actor || !Skee::IsAvailable()) return false;
        if (IsApplied(actor, entry)) return true;

        auto* state = GetOrCreate(actor);
        if (!state) return false;

        if (!Skee::EnsureOverlays(actor)) {
            spdlog::warn("State: could not install overlay nodes on {:08X}", actor->GetFormID());
            return false;
        }

        auto node = preferredNode.empty()
            ? Skee::FindFreeSlot(actor, entry.location)
            : std::optional<std::string>(preferredNode);
        if (!node) {
            spdlog::warn("State: no free {} slot on {:08X} for {}",
                         Skee::LocationName(entry.location), actor->GetFormID(), entry.qualifiedId);
            return false;
        }

        const auto& appearance = appearanceOverride ? *appearanceOverride : entry.appearance;
        if (!Skee::ApplyToSlot(actor, state->isFemale, *node, appearance)) return false;

        // Recorded, not recomputed later: this is the only moment the chosen tint is
        // known, and everything that writes the overlay again - the ODF rule file, a
        // restore - has to write the same thing or the colour changes under the player.
        state->applied.push_back({entry.qualifiedId, *node, appearance});
        spdlog::info("State: applied {} to {:08X} ({}) in \"{}\"",
                     entry.qualifiedId, actor->GetFormID(), state->editorId, *node);
        return true;
    }

    bool StateManager::Remove(RE::Actor* actor, const Entry& entry) {
        if (!actor || !Skee::IsAvailable()) return false;

        auto* state = GetOrCreate(actor);
        if (!state) return false;

        auto it = std::find_if(state->applied.begin(), state->applied.end(),
                               [&](const Applied& a) { return a.qualifiedId == entry.qualifiedId; });
        if (it == state->applied.end()) return false;

        Skee::ClearSlot(actor, state->isFemale, it->node);
        spdlog::info("State: removed {} from {:08X} (\"{}\")", entry.qualifiedId, actor->GetFormID(), it->node);
        state->applied.erase(it);
        return true;
    }

    bool StateManager::Retint(RE::Actor* actor, const Entry& entry,
                              const Skee::Appearance& appearance) {
        if (!actor || !Skee::IsAvailable()) return false;

        auto* state = GetOrCreate(actor);
        if (!state) return false;

        auto it = std::find_if(state->applied.begin(), state->applied.end(),
                               [&](const Applied& a) { return a.qualifiedId == entry.qualifiedId; });
        if (it == state->applied.end()) return false;

        if (!Skee::ApplyToSlot(actor, state->isFemale, it->node, appearance)) return false;

        it->appearance = appearance;
        spdlog::info("State: retinted {} on {:08X} (\"{}\")",
                     entry.qualifiedId, actor->GetFormID(), it->node);
        return true;
    }

    size_t StateManager::ClearAll(RE::Actor* actor) {
        if (!actor || !Skee::IsAvailable()) return 0;

        auto* state = GetOrCreate(actor);
        if (!state || state->applied.empty()) return 0;

        state->lastCleared.clear();
        state->lastCleared.reserve(state->applied.size());
        for (const auto& applied : state->applied) {
            state->lastCleared.push_back(applied);
            Skee::ClearSlot(actor, state->isFemale, applied.node);
        }

        const auto count = state->applied.size();
        state->applied.clear();
        spdlog::info("State: cleared {} overlays from {:08X} ({})", count, actor->GetFormID(), state->editorId);
        return count;
    }

    size_t StateManager::RestoreAll(RE::Actor* actor) {
        if (!actor || !Skee::IsAvailable()) return 0;

        auto* state = GetOrCreate(actor);
        if (!state || state->lastCleared.empty()) return 0;

        // Copied out first: Apply() mutates state->applied, and a failed entry must not
        // silently disappear from the restore list mid-iteration.
        const auto toRestore = state->lastCleared;
        const auto* catalog = Catalog::GetSingleton();

        size_t restored = 0;
        for (const auto& cleared : toRestore) {
            const auto* entry = catalog->FindEntry(cleared.qualifiedId);
            if (!entry) {
                spdlog::warn("State: cannot restore {} - no longer in the catalog", cleared.qualifiedId);
                continue;
            }

            // Back into the slot it came out of, with the tint it had. The slot matters
            // as much as the tint here: ClearAll's release is a Papyrus call that has
            // not run yet, so FindFreeSlot would still read that slot as occupied and
            // spend a second one on the same overlay. The VM runs the two in the order
            // they are queued, so the clear always lands before this refill.
            const auto look = LookFor(*entry, cleared.appearance);
            if (Apply(actor, *entry, &look, cleared.node)) ++restored;
        }

        state->lastCleared.clear();
        spdlog::info("State: restored {} of {} overlays to {:08X}", restored, toRestore.size(), actor->GetFormID());
        return restored;
    }

    bool StateManager::HasApplied(RE::Actor* actor) const {
        const auto* state = Find(actor);
        return state && !state->applied.empty();
    }

    bool StateManager::HasCleared(RE::Actor* actor) const {
        const auto* state = Find(actor);
        return state && !state->lastCleared.empty();
    }

    void StateManager::Save(SKSE::SerializationInterface* serialization) {
        if (!serialization->OpenRecord(kActorRecord, kRecordVersion)) {
            spdlog::error("State: failed to open co-save record");
            return;
        }

        const auto actorCount = static_cast<uint32_t>(m_actors.size());
        serialization->WriteRecordData(actorCount);

        for (const auto& [key, state] : m_actors) {
            WriteString(serialization, key);
            WriteString(serialization, state.editorId);
            serialization->WriteRecordData(state.isFemale);

            serialization->WriteRecordData(static_cast<uint32_t>(state.applied.size()));
            for (const auto& applied : state.applied) {
                WriteString(serialization, applied.qualifiedId);
                WriteString(serialization, applied.node);
                WriteAppearance(serialization, applied.appearance);
            }

            serialization->WriteRecordData(static_cast<uint32_t>(state.lastCleared.size()));
            for (const auto& cleared : state.lastCleared) {
                WriteString(serialization, cleared.qualifiedId);
                WriteString(serialization, cleared.node);
                WriteAppearance(serialization, cleared.appearance);
            }
        }
        spdlog::info("State: saved {} actors to the co-save", actorCount);
    }

    void StateManager::Load(SKSE::SerializationInterface* serialization, uint32_t version) {
        if (version > kRecordVersion) {
            spdlog::warn("State: co-save record is version {}, newer than the {} this build writes - skipping it",
                         version, kRecordVersion);
            return;
        }

        uint32_t actorCount = 0;
        if (!serialization->ReadRecordData(actorCount)) {
            spdlog::error("State: co-save record truncated at the actor count");
            return;
        }

        m_actors.clear();
        for (uint32_t i = 0; i < actorCount; ++i) {
            std::string key;
            ActorState state;
            if (!ReadString(serialization, key) || !ReadString(serialization, state.editorId) ||
                !serialization->ReadRecordData(state.isFemale)) {
                spdlog::error("State: co-save record truncated in actor {} of {}", i, actorCount);
                return;
            }

            uint32_t appliedCount = 0;
            if (!serialization->ReadRecordData(appliedCount)) return;
            for (uint32_t j = 0; j < appliedCount; ++j) {
                Applied applied;
                if (!ReadString(serialization, applied.qualifiedId) || !ReadString(serialization, applied.node)) return;
                if (version >= 2 && !ReadAppearance(serialization, applied.appearance)) return;
                state.applied.push_back(std::move(applied));
            }

            uint32_t clearedCount = 0;
            if (!serialization->ReadRecordData(clearedCount)) return;
            for (uint32_t j = 0; j < clearedCount; ++j) {
                Applied cleared;
                if (!ReadString(serialization, cleared.qualifiedId)) return;
                // v1 kept ids alone here, so there is no slot to go back to and no tint
                // to go back with - RestoreAll finds a free slot and the pack's colour.
                if (version >= 2 &&
                    (!ReadString(serialization, cleared.node) ||
                     !ReadAppearance(serialization, cleared.appearance))) {
                    return;
                }
                state.lastCleared.push_back(std::move(cleared));
            }

            m_actors.emplace(std::move(key), std::move(state));
        }
        spdlog::info("State: loaded {} actors from the co-save", m_actors.size());
    }

    void StateManager::Revert() {
        spdlog::info("State: reverting, dropping {} tracked actors", m_actors.size());
        m_actors.clear();
    }
}
