#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "api/ThreeDUIInterface001.h"
#include "overlay/OverlayCatalog.h"
#include "skee/SkeeBridge.h"

namespace NPCEditor::Overlay {
    // The overlay editor, four short rows deep. Top to bottom: what the actor is already
    // wearing, a stepper that browses one overlay at a time, the pack filter (or the
    // colour swatches, which take the same line), and the tool row with its orb. Each of
    // the first three carries a line of text directly underneath it saying what it is
    // showing; the tool row sits at the bottom with nothing under it.
    //
    // It used to be a scroll wheel holding every overlay in the selected pack. A big pack
    // is well over a hundred, 3DUI charges for every live element on every frame, and
    // building them cost enough per frame that the fill stuttered however finely it was
    // chunked. The stepper shows one at a time and costs five elements no matter how
    // large the pack is, the same trade the body menu made for its presets.
    //
    // Browsing is non-destructive: stepping writes the overlay to the actor through
    // NiOverride's non-persistent path, so you see it on the body, and only the check
    // button commits it. Anything left uncommitted is taken back off when the menu
    // closes.
    class MenuManager {
    public:
        static MenuManager* GetSingleton();

        bool Initialize();
        bool IsInitialized() const { return m_api != nullptr && m_root != nullptr; }

        void OpenForActor(RE::Actor* actor, bool isLeftHand);
        void Close();
        bool IsOpen() const { return m_open; }

        // The actor the menu currently acts on - the grabbed NPC, or the player when
        // the target toggle is flipped.
        RE::Actor* GetTargetActor() const;

        static bool OnEvent(const P3DUI::Event* event);

    private:
        using Clock = std::chrono::steady_clock;

        bool HandleEvent(const P3DUI::Event* event);
        bool BuildMenu();

        // Runs `work` on the next frame, and only if the menu is still open. Everything
        // that rebuilds a row has to wait a frame - 3DUI dispatches events while walking
        // the element hierarchy, and Clear() destroys the elements it is walking - and by
        // then the same press may have closed the menu, leaving a handler about to act on
        // an actor the player has walked away from.
        static void NextFrameIfOpen(std::function<void(MenuManager&)> work);

        void RefreshAll();
        void LayoutRows();

        // Each Populate* clears its row and hands the refill to PopulateHidden, which
        // keeps the row hidden while the elements go in - 3DUI spawns an element before
        // its parent has laid it out, so anything created under a visible row starts at
        // the row's centre and slides into place.
        void PopulateAppliedRow();
        void FillAppliedRow();
        void UpdateAppliedHighlight();

        void PopulatePickerRow();

        void PopulatePackRow();
        void FillPackRow(const std::vector<Pack>& packs);
        void UpdatePackHighlight();

        // The tint row lives on the pack row's line and is built only while it is open,
        // so a palette nobody opened costs 3DUI nothing.
        void PopulateColorRow();
        void FillColorRow();
        void ToggleColorRow();
        void UpdateColorHighlight();

        void PopulateToolRow();
        void FillToolRow();

        // Registered with FrameHook while the menu is open; drives the chevron auto-repeat.
        void Tick();
        void BeginRepeat(const std::string& id);
        void EndRepeat();

        // ===== The stepper =====

        // The entries of the selected pack that suit the target, in the order the
        // chevrons walk them.
        void RebuildPickList();
        const Entry* CurrentPick() const;
        bool PickIsApplied() const;
        std::wstring PickTooltip() const;

        void StepPick(int delta);
        void RandomPick();

        // Points the stepper at a specific overlay, switching pack if it belongs to
        // another one. This is what the applied row's items do when clicked.
        void SelectEntry(const Entry* entry);

        // Repaints the stepper, its name line and the applied row's highlight for
        // whatever m_pickIndex now points at. With `preview` it also puts the overlay on
        // the actor, which is what every deliberate selection does.
        void ShowPick(bool preview);
        void UpdatePickIcon();
        void UpdatePickText();

        void CommitPick();
        void RemovePick();

        void PreviewCurrent();

        // EndPreview takes the preview off the actor and keeps the slot it was using;
        // DropPreviewSlot does that and gives the slot back too. See their definitions
        // for why holding on to a slot between steps matters.
        void EndPreview();
        void DropPreviewSlot();

        // Repaints the committed overlay in the currently chosen colour, in place.
        Skee::Appearance TintedAppearance(const Entry& entry) const;
        void RetintSelected();

        void OnAppliedActivated(size_t index);
        void OnPackActivated(size_t index);
        void OnColorActivated(size_t index);
        void OnToolActivated(const std::string& id);
        void ClearAllOverlays();

        // The status line, which sits directly under the pack row and so doubles as that
        // row's label. It has three kinds of tenant. Resting is what the row is -
        // "Available Overlays" - and is what the line falls back to. ShowInfo is a result -
        // "that slot is full", "loading" - and stays until something replaces it.
        // ShowHint is what the hand is resting on, and only a hint is taken down when the
        // hand moves away, so drifting off an unrelated button no longer wipes the answer
        // you just asked for.
        std::wstring RestingInfo() const;
        void ShowInfo(const std::wstring& text);
        void ShowHint(const std::wstring& text);
        void ClearHint();
        void ClearInfo();

        // "Applied Overlays", under the top row, and hidden while that row is empty.
        void UpdateAppliedText();

        void SelectDefaultPack();
        void PersistToOdf();

        P3DUI::Interface001* m_api = nullptr;
        P3DUI::Root* m_root = nullptr;
        P3DUI::ScrollableContainer* m_appliedRow = nullptr;
        P3DUI::ScrollableContainer* m_pickerRow = nullptr;
        P3DUI::ScrollableContainer* m_packRow = nullptr;
        P3DUI::ScrollableContainer* m_colorRow = nullptr;
        P3DUI::ScrollableContainer* m_toolRow = nullptr;
        P3DUI::Text* m_appliedText = nullptr;
        P3DUI::Text* m_pickText = nullptr;
        P3DUI::Text* m_infoText = nullptr;

        // The two elements of the stepper row that change without the row being rebuilt.
        // 3DUI has no lookup-by-id, and a rebuild would destroy the element the hand is
        // resting on mid-hold.
        P3DUI::Element* m_pickIcon = nullptr;
        P3DUI::Element* m_randomButton = nullptr;

        bool m_open = false;
        RE::ActorHandle m_npcHandle;
        bool m_targetPlayer = false;

        size_t m_selectedPack = 0;

        // What the chevrons walk, and where in it they are sitting. npos when the pack
        // has nothing this actor can wear.
        std::vector<const Entry*> m_pickEntries;
        size_t m_pickIndex = static_cast<size_t>(-1);

        // What the actor is wearing, and the elements showing it. Parallel to the
        // "vrnpce_applied_<n>" ids.
        std::vector<const Entry*> m_appliedEntries;
        std::vector<P3DUI::Element*> m_appliedElements;

        // Retained so the selection backdrop can move without rebuilding the row.
        std::vector<P3DUI::Element*> m_packElements;
        std::vector<P3DUI::Element*> m_colorElements;

        // The overlay a colour swatch acts on: whatever was committed last, by qualified
        // id so it survives pack switches. Empty when nothing on the actor came from this
        // menu since it opened, in which case a swatch only decides what the next commit
        // will look like.
        std::string m_selectedOverlay;

        // Index into GetPalette(). 0 is "pack default", and survives closing the row so
        // a chosen tint keeps applying while it is hidden.
        size_t m_selectedColor = 0;
        bool m_colorRowOpen = false;

        // Whether the status line is currently showing a hover hint rather than a result.
        bool m_infoIsHint = false;

        // The overlay currently on the actor as a non-persistent preview, the slot it was
        // written into, and what kind of slot that is. Cleared on commit and on menu
        // close. Stepping to another overlay of the same kind writes straight over the
        // slot rather than releasing it, so the location is kept to know when it can.
        std::string m_previewId;
        std::string m_previewNode;
        Skee::Location m_previewLocation = Skee::Location::Body;

        // The held chevron, if any, and when it was pressed. Repeat starts after the
        // configured delay and then fires on an interval, both driven from Tick.
        std::string m_repeatId;
        Clock::time_point m_repeatSince{};
        Clock::time_point m_repeatLast{};
        bool m_repeating = false;
    };
}
