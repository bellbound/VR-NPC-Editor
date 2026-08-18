#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "api/ThreeDUIInterface001.h"

namespace NPCEditor {
    // The body editor: three short rows, bottom to top - tools, a BodySlide preset
    // stepper, and a weight stepper - each stepper with its current value written out
    // underneath it.
    //
    // It used to be a scrolling grid of every preset the actor had, two elements a row.
    // At 165 presets that is 330 projectiles, and 3DUI charges for every live one on
    // every frame, so even one page of it stuttered. A stepper shows one preset at a
    // time and costs nine elements for the whole menu, which is a cost that does not
    // grow with the number of presets installed.
    class BodyMenuManager {
    public:
        static BodyMenuManager* GetSingleton();

        bool Initialize();
        bool IsInitialized() const { return m_api != nullptr && m_root != nullptr; }

        void OpenForActor(RE::Actor* actor, bool isLeftHand);
        void Close();
        bool IsOpen() const { return m_open; }

        RE::Actor* GetTargetActor() const;

        static bool OnEvent(const P3DUI::Event* event);

    private:
        BodyMenuManager() = default;
        BodyMenuManager(const BodyMenuManager&) = delete;
        BodyMenuManager& operator=(const BodyMenuManager&) = delete;

        using Clock = std::chrono::steady_clock;

        bool BuildMenu();
        bool HandleEvent(const P3DUI::Event* event);

        void RefreshAll();

        // Each row is cleared and refilled in one go. They are three elements each, so
        // there is nothing here worth chunking across frames.
        void PopulatePresetRow();
        void PopulateWeightRow();
        void PopulateToolRow();

        // Registered with FrameHook while the menu is open; drives the auto-repeat and
        // the weight debounce.
        void Tick();

        // Presets
        void StepPreset(int delta);
        void ApplyCurrentPreset(const char* reason);
        void UpdatePresetText();
        std::wstring PresetTooltip() const;

        // Weight
        void StepWeight(int delta);
        void CommitWeight();
        void UpdateWeightText();
        void SyncWeightStep();

        void OnToolActivated(const std::string& id);
        void UndoChanges();

        void ShowInfo(const std::wstring& text);
        void ClearInfo();

        // Auto-repeat: the trigger held on a chevron steps once, then keeps stepping.
        void BeginRepeat(const std::string& id);
        void EndRepeat();

        P3DUI::Interface001* m_api = nullptr;
        P3DUI::Root* m_root = nullptr;
        P3DUI::ScrollableContainer* m_presetRow = nullptr;
        P3DUI::ScrollableContainer* m_weightRow = nullptr;
        P3DUI::ScrollableContainer* m_toolRow = nullptr;
        P3DUI::Text* m_presetText = nullptr;
        P3DUI::Text* m_weightText = nullptr;
        P3DUI::Text* m_infoText = nullptr;

        // The centre of each stepper, kept so its tooltip and its gauge texture can be
        // updated in place. 3DUI has no lookup-by-id, and rebuilding the row on every
        // step would destroy the element the hand is resting on.
        P3DUI::Element* m_presetIcon = nullptr;
        P3DUI::Element* m_weightIcon = nullptr;

        bool m_open = false;
        RE::ActorHandle m_npcHandle;

        // The undo button appears the moment this sitting has a change to undo. The
        // rebuild that adds it waits for Tick rather than happening inside the 3DUI
        // event that caused it, which is still walking the elements Clear() destroys.
        bool m_toolRowDirty = false;

        std::vector<std::string> m_presets;

        // Which preset the stepper is sitting on. Seeded from OBody's assignment so the
        // menu opens showing the body the actor already has, and npos when the actor has
        // no presets at all.
        size_t m_presetIndex = static_cast<size_t>(-1);

        // The held chevron, if any, and when it was pressed. Repeat starts after the
        // configured delay and then fires on an interval, both driven from Tick.
        std::string m_repeatId;
        Clock::time_point m_repeatSince{};
        Clock::time_point m_repeatLast{};
        bool m_repeating = false;

        // Weight in 25% steps, 0-4. The commit is deferred so a burst of steps costs
        // one 3D reset rather than one per step.
        int m_weightStep = 0;
        bool m_weightPending = false;
        Clock::time_point m_weightRequestedAt{};
    };
}
