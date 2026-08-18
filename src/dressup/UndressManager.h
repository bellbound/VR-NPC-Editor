#pragma once

#include <RE/Skyrim.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Lifted from skse/VR-Sex-Menu/src/undress/UndressManager.h, which is self-contained -
// unlike VR Dress Up's version, which pulls in the whole outfit-locking chain
// (OutfitLockManager -> OutfitFormBackend -> PapyrusBridge -> SeverActionsCompat).
//
// Two differences from the original:
//   - no UndressPartial. The body menu has a plain two-state clothes toggle, so a
//     semi-undressed state would be unreachable and would only confuse the icon.
//   - state is in-memory only and is dropped on load, because the toggle exists to
//     let you see the body you are editing, not to dress NPCs permanently. Anything
//     the player wants to keep is VR Dress Up's job.
namespace NPCEditor {
    enum class UndressState : uint8_t {
        Dressed = 0,
        FullyUndressed
    };

    class UndressManager {
    public:
        static UndressManager* GetSingleton() {
            static UndressManager instance;
            return &instance;
        }

        // Removes every equipped armour, remembering what came off.
        void UndressFull(RE::Actor* actor) {
            if (!actor) return;

            auto* equipManager = RE::ActorEquipManager::GetSingleton();
            if (!equipManager) {
                spdlog::error("Undress: no ActorEquipManager, cannot undress '{}'", actor->GetName());
                return;
            }

            SaveOriginalOutfit(actor);

            size_t removed = 0;
            for (auto* armor : GetInventoryArmor(actor)) {
                if (IsArmorEquipped(actor, armor)) {
                    equipManager->UnequipObject(actor, armor, nullptr, 1, nullptr, false, true);
                    ++removed;
                }
            }

            SetUndressState(actor, UndressState::FullyUndressed);
            spdlog::info("Undress: stripped {} items from '{}'", removed, actor->GetName());
        }

        // Puts back exactly what UndressFull took off, skipping anything since dropped.
        void Redress(RE::Actor* actor) {
            if (!actor) return;

            auto* equipManager = RE::ActorEquipManager::GetSingleton();
            if (!equipManager) {
                spdlog::error("Undress: no ActorEquipManager, cannot redress '{}'", actor->GetName());
                return;
            }

            const RE::FormID actorId = actor->GetFormID();

            std::unordered_set<RE::FormID> stored;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_storedOutfits.find(actorId);
                if (it == m_storedOutfits.end()) {
                    spdlog::warn("Undress: nothing stored for '{}', cannot redress", actor->GetName());
                } else {
                    stored = it->second;
                    m_storedOutfits.erase(it);
                }
            }

            // Clear whatever is on them now, so a preset change that re-equipped
            // something does not leave two items fighting over one slot.
            for (auto* armor : GetInventoryArmor(actor)) {
                if (IsArmorEquipped(actor, armor)) {
                    equipManager->UnequipObject(actor, armor, nullptr, 1, nullptr, false, true);
                }
            }

            size_t restored = 0;
            for (RE::FormID armorId : stored) {
                auto* form = RE::TESForm::LookupByID(armorId);
                auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
                if (armor && HasItemInInventory(actor, armor)) {
                    equipManager->EquipObject(actor, armor, nullptr, 1, nullptr, true, false, false);
                    ++restored;
                }
            }

            ClearUndressState(actor);
            spdlog::info("Undress: restored {} of {} items to '{}'", restored, stored.size(), actor->GetName());
        }

        void ClearUndressState(RE::Actor* actor) {
            if (!actor) return;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_undressStates.erase(actor->GetFormID());
            m_storedOutfits.erase(actor->GetFormID());
        }

        UndressState GetUndressState(RE::Actor* actor) const {
            if (!actor) return UndressState::Dressed;
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_undressStates.find(actor->GetFormID());
            return (it != m_undressStates.end()) ? it->second : UndressState::Dressed;
        }

        bool IsUndressed(RE::Actor* actor) const {
            return GetUndressState(actor) == UndressState::FullyUndressed;
        }

        // A loaded save has different actor instances behind the same FormIDs, and the
        // stored outfits describe a world that no longer exists.
        void Reset() {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_undressStates.empty()) {
                spdlog::info("Undress: dropping {} tracked actors", m_undressStates.size());
            }
            m_undressStates.clear();
            m_storedOutfits.clear();
        }

    private:
        UndressManager() = default;
        ~UndressManager() = default;
        UndressManager(const UndressManager&) = delete;
        UndressManager& operator=(const UndressManager&) = delete;

        // Only the first undress records the outfit: a second one must not overwrite
        // the real outfit with the already-stripped state.
        void SaveOriginalOutfit(RE::Actor* actor) {
            const RE::FormID actorId = actor->GetFormID();

            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_storedOutfits.contains(actorId)) return;

            std::unordered_set<RE::FormID> worn;
            for (auto* armor : GetInventoryArmor(actor)) {
                if (IsArmorEquipped(actor, armor)) worn.insert(armor->GetFormID());
            }

            spdlog::debug("Undress: remembered {} worn items for '{}'", worn.size(), actor->GetName());
            m_storedOutfits[actorId] = std::move(worn);
        }

        void SetUndressState(RE::Actor* actor, UndressState state) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_undressStates[actor->GetFormID()] = state;
        }

        static std::vector<RE::TESObjectARMO*> GetInventoryArmor(RE::Actor* actor) {
            std::vector<RE::TESObjectARMO*> result;
            if (!actor) return result;

            auto inventory = actor->GetInventory([](RE::TESBoundObject& obj) {
                return obj.Is(RE::FormType::Armor);
            });

            for (const auto& [item, data] : inventory) {
                if (data.first <= 0) continue;
                if (auto* armor = item->As<RE::TESObjectARMO>()) result.push_back(armor);
            }
            return result;
        }

        static bool IsArmorEquipped(RE::Actor* actor, RE::TESObjectARMO* armor) {
            if (!actor || !armor) return false;
            auto* worn = actor->GetWornArmor(armor->GetSlotMask());
            return worn && worn->GetFormID() == armor->GetFormID();
        }

        static bool HasItemInInventory(RE::Actor* actor, RE::TESBoundObject* item) {
            if (!actor || !item) return false;
            auto inventory = actor->GetInventory([item](RE::TESBoundObject& obj) {
                return obj.GetFormID() == item->GetFormID();
            });
            return !inventory.empty();
        }

        std::unordered_map<RE::FormID, UndressState> m_undressStates;
        std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> m_storedOutfits;
        mutable std::mutex m_mutex;
    };
}
