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
    // A body only has so many overlay slots. Once every kind an installed pack could
    // fill is spent, the stepper and the pack filter are hidden: there is nothing left to
    // browse to, and a row of controls that cannot do anything is worse than no row. What
    // stays is the applied row, whose items then take an overlay off rather than select
    // it, because taking one off is the only move left that gets you anywhere.
    //
    // Browsing is non-destructive: stepping writes the overlay to the actor through
    // NiOverride's non-persistent path, so you see it on the body, and only the check
    // button commits it - or leaving does it for you. Walking away from an overlay you
    // stopped on - switching source, switching who you are editing, or closing the menu -
    // keeps it: the check button is the shortcut, not the toll. Only an overlay you
    // deliberately stepped or picked is a preview, so the pack the menu happened to open
    // on never commits itself.
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

        // ===== Undo and redo =====

        // A button with nothing behind it is drawn greyed out and its press does nothing,
        // the same bargain VR Dress Up's pair strikes.
        void UpdateHistoryButtons();
        void OnUndo();
        void OnRedo();

        // Rebuilds what a history restore changed. Deliberately not RefreshAll: that
        // reconciles the record against the actor's live slots, and a restore's writes
        // are still queued in the Papyrus VM when this runs.
        void AfterHistoryRestore();

        // ===== Slots =====

        // Whether an installed pack could still put anything on this actor. Reads the
        // actor's live 3D, so see PollSlotFullness for when it is allowed to be asked.
        bool SlotsFull() const;

        // Which kinds of slot the installed packs can fill for whoever is being edited.
        // A pack with nothing for this sex has no say in whether they have run out of
        // room, and neither does a kind of slot no pack targets.
        void RebuildSlotLocations();

        // Freeing a slot is a Papyrus write that lands a frame or more later, so the
        // answer cannot be recomputed at the press that caused it. Polled from Tick
        // instead, on a throttle - each check walks the actor's 3D for named nodes.
        void PollSlotFullness();

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

        // Commits the live preview, if there is one, on the way out of the state that
        // was showing it - a source switch, a target switch, or the menu closing. There
        // is no preview until the player steps or picks, which is what keeps a menu they
        // only opened and closed again from putting the default pack's first overlay on
        // the actor. Every caller runs it before the thing it is leaving has changed,
        // since it commits to whatever the pick list and the target say at the time.
        void KeepPreview();

        void PreviewCurrent();

        // Runs from Tick while a preview is waiting on RaceMenu to build the actor's
        // overlay geometry, and puts the preview on as soon as it has.
        void RetryPendingPreview();

        // EndPreview takes the preview off the actor and keeps the slot it was using;
        // DropPreviewSlot does that and gives the slot back too. See their definitions
        // for why holding on to a slot between steps matters.
        void EndPreview();
        void DropPreviewSlot();

        // Repaints the committed overlay in the currently chosen colour, in place.
        Skee::Appearance TintedAppearance(const Entry& entry) const;
        void RetintSelected();

        void OnAppliedActivated(size_t index);
        void RemoveApplied(size_t index);
        void OnPackActivated(size_t index);
        void OnColorActivated(size_t index);
        void OnToolActivated(const std::string& id);
        void ClearAllOverlays();

        // The status line, which sits directly under the pack row and so doubles as that
        // row's label. Resting is what the row is - "Available Overlays" - and is what
        // the line falls back to; ShowInfo is a result - "that slot is full", "loading" -
        // and stays until something replaces it.
        //
        // It used to echo whatever the hand was resting on as well, which was a tooltip
        // written in a second place: 3DUI already puts every element's own tooltip up
        // beside the hand, so the line was saying the same thing twice and wiping real
        // answers to do it.
        std::wstring RestingInfo() const;
        void ShowInfo(const std::wstring& text);
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

        // Kept for the same reason: their faces change as the history moves, and the row
        // must not be rebuilt under the hand that is pressing them.
        P3DUI::Element* m_undoButton = nullptr;
        P3DUI::Element* m_redoButton = nullptr;

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

        // The overlay currently on the actor as a non-persistent preview, the slot it was
        // written into, and what kind of slot that is. Cleared on commit and on menu
        // close. Stepping to another overlay of the same kind writes straight over the
        // slot rather than releasing it, so the location is kept to know when it can.
        //
        // An empty id is also how the menu tells "the player has chosen nothing yet"
        // from "the player is looking at their choice" - see KeepPreview.
        std::string m_previewId;
        std::string m_previewNode;
        Skee::Location m_previewLocation = Skee::Location::Body;

        // Whether the actor has room for another overlay, and when that was last
        // looked at. See PollSlotFullness.
        std::vector<Skee::Location> m_slotLocations;
        bool m_slotsFull = false;
        Clock::time_point m_slotCheckAt{};

        // A preview asked for before the actor had overlay geometry, and when it was
        // asked for. Tick retries it until the geometry lands or the wait runs out.
        bool m_awaitingNodes = false;
        Clock::time_point m_awaitingSince{};

        // The held chevron, if any, and when it was pressed. Repeat starts after the
        // configured delay and then fires on an interval, both driven from Tick.
        std::string m_repeatId;
        Clock::time_point m_repeatSince{};
        Clock::time_point m_repeatLast{};
        bool m_repeating = false;
    };
}
