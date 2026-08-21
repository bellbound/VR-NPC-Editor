#include "menu/OverlayMenuManager.h"

#include "Config.h"
#include "dressup/UndressManager.h"
#include "FrameHook.h"
#include "health/HealthCheckManager.h"
#include "menu/EditSession.h"
#include "menu/MenuRouter.h"
#include "overlay/OdfWriter.h"
#include "overlay/OverlayColors.h"
#include "overlay/OverlayHistory.h"
#include "overlay/OverlayStateManager.h"

#include <algorithm>
#include <random>
#include <vector>
#include <spdlog/spdlog.h>

namespace NPCEditor::Overlay {
    namespace {
        constexpr const char* kModId = "VRNPCEditor";

        // How long a preview will wait for RaceMenu to build the actor's overlay
        // geometry before giving up on it. The build normally lands within a frame or
        // two; this is only here so a refusal reads as one.
        constexpr auto kNodeWait = std::chrono::seconds(3);

        constexpr const char* kRootId        = "vrnpce_root";
        constexpr const char* kAppliedRowId  = "vrnpce_appliedrow";
        constexpr const char* kAppliedTextId = "vrnpce_appliedtext";
        constexpr const char* kPickerRowId   = "vrnpce_pickerrow";
        constexpr const char* kPickTextId    = "vrnpce_picktext";
        constexpr const char* kPackRowId     = "vrnpce_packrow";
        constexpr const char* kColorRowId    = "vrnpce_colorrow";
        constexpr const char* kToolRowId     = "vrnpce_toolrow";
        constexpr const char* kInfoId        = "vrnpce_info";

        constexpr const char* kAnchorId   = "vrnpce_anchor";
        constexpr const char* kClothesId  = "vrnpce_tool_clothes";
        constexpr const char* kTargetId   = "vrnpce_tool_target";
        constexpr const char* kColorsId   = "vrnpce_tool_colors";
        constexpr const char* kClearId    = "vrnpce_tool_clear";
        constexpr const char* kUndoId     = "vrnpce_tool_undo";
        constexpr const char* kRedoId     = "vrnpce_tool_redo";

        constexpr const char* kRandomId   = "vrnpce_pick_random";
        constexpr const char* kPickPrevId = "vrnpce_pick_prev";
        constexpr const char* kPickIconId = "vrnpce_pick_icon";
        constexpr const char* kPickNextId = "vrnpce_pick_next";
        constexpr const char* kCommitId   = "vrnpce_pick_commit";

        constexpr const char* kAppliedPrefix = "vrnpce_applied_";
        constexpr const char* kPackPrefix    = "vrnpce_pack_";
        constexpr const char* kColorPrefix   = "vrnpce_color_";

        constexpr const char* kTexUndress     = "textures\\VRNPCEditor\\undress-full.dds";
        constexpr const char* kTexRedress     = "textures\\VRNPCEditor\\redress-full.dds";
        constexpr const char* kTexTargetNpc   = "textures\\VRNPCEditor\\npc.dds";
        constexpr const char* kTexTargetPlayer= "textures\\VRNPCEditor\\player.dds";
        constexpr const char* kTexColors      = "textures\\VRNPCEditor\\paint-palette.dds";
        constexpr const char* kTexColorsOn    = "textures\\VRNPCEditor\\paint-palette_highlight.dds";
        constexpr const char* kTexClear       = "textures\\VRNPCEditor\\clear-all.dds";
        constexpr const char* kTexPrev        = "textures\\VRNPCEditor\\chevron-left.dds";
        constexpr const char* kTexNext        = "textures\\VRNPCEditor\\chevron-right.dds";
        constexpr const char* kTexRandom      = "textures\\VRNPCEditor\\dice.dds";
        constexpr const char* kTexRemove      = "textures\\VRNPCEditor\\cross.dds";
        constexpr const char* kTexCommit      = "textures\\VRNPCEditor\\check.dds";
        constexpr const char* kTexUndo        = "textures\\VRNPCEditor\\undo.dds";
        constexpr const char* kTexUndoOff     = "textures\\VRNPCEditor\\undo_disabled.dds";
        constexpr const char* kTexRedo        = "textures\\VRNPCEditor\\redo.dds";
        constexpr const char* kTexRedoOff     = "textures\\VRNPCEditor\\redo_disabled.dds";
        constexpr const char* kAnchorModel    = "meshes\\3DUI\\orb.nif";

        // The gallery highlight from VR Dress Up: a second, non-interactive projectile
        // behind the element. The gradient sphere replaces the older cloud one because
        // its material is an effect shader, which is the only kind 3DUI can tint - see
        // ApplyBackdrop below.
        constexpr const char* kBackdropModel = "meshes\\3DUI\\gradient-background-sphere.nif";
        constexpr float kBackdropScale = 15.0f;

        constexpr float kToolScale = 1.2f;

        // One size for every pack cover. Marking the selection by scale made the covers
        // disagree about how big a pack icon is, and the difference read as the artwork
        // being wrong rather than as a selection; the backdrop below says it instead.
        constexpr float kPackScale = 1.02f;

        // A row and the line written under it are one thing, so they sit closer together
        // than one group sits to the next. Both gaps are 0.8 of what they were: with the
        // applied row, the stepper, the source row and the tools all stacked up, the menu
        // was taller than a glance covers, and the labels never needed that much air.
        constexpr float kTextGap  = 3.36f;
        constexpr float kGroupGap = 7.0f;

        // Bottom to top: the tool row, and above it three rows that each carry a line of
        // text kTextGap underneath - the pack filter (or the palette, same line) over the
        // status line, the stepper over the name of what it is showing, and what the
        // actor wears over "Applied Overlays". Every one of those texts sits the same
        // distance under its own row, which is why kTextGap is written once and reused.
        constexpr float kToolRowZ     = 0.0f;
        constexpr float kInfoZ        = kToolRowZ + kGroupGap;
        constexpr float kSourceRowZ   = kInfoZ + kTextGap;
        constexpr float kPickTextZ    = kSourceRowZ + kGroupGap;
        constexpr float kPickerRowZ   = kPickTextZ + kTextGap;
        constexpr float kAppliedTextZ = kPickerRowZ + kGroupGap;
        constexpr float kAppliedRowZ  = kAppliedTextZ + kTextGap;

        // 3DUI multiplies this by 1.25 internally, which is what VR Dress Up's info line
        // renders at.
        constexpr float kRowTextScale = 1.0f;

        // Hardcoded rather than measured off the tool row: the tool row's own width moves
        // as buttons come and go, and a scroll strip that changes width when you undress
        // an NPC is worse than one that is approximately right. Five stepper elements at
        // 8.0 spacing is the shape both rows are aiming at.
        constexpr float kStripWidth = 42.0f;

        // How hard the menu is bent round the player - see Root::SetCurvature, which
        // judges this against the menu's own half-width rather than its distance from the
        // head. The widest thing here is the pack strip at 45 units, so half-width is a
        // little over 22, and a radius of twice that carries the edges through half a
        // radian: the clear curve the interface describes, without wrapping a menu this
        // narrow round far enough to start reaching back at the player's shoulder.
        //
        // Horizontal only. Vertical curvature bends about the root's own origin, and this
        // root's origin is the tool row at the bottom of the stack rather than its middle
        // - the orb has to land on the hand - so a vertical bend would tip the whole menu
        // forward instead of bowing its top and bottom evenly.
        constexpr float kMenuCurveRadius = 45.0f;

        // How often the menu looks at whether the actor still has a free overlay slot.
        // Every check walks the actor's 3D for named nodes, and the answer only moves
        // when a write reaches the actor, which is frames away in any case.
        constexpr auto kSlotCheckInterval = std::chrono::milliseconds(250);

        // The undo and redo faces. A button with nothing behind it is drawn greyed out
        // and its press does nothing.
        const char* HistoryTexture(bool undo, bool enabled) {
            if (undo) return enabled ? kTexUndo : kTexUndoOff;
            return enabled ? kTexRedo : kTexRedoOff;
        }

        const wchar_t* HistoryTooltip(bool undo, bool enabled) {
            if (undo) return enabled ? L"Undo" : L"Nothing to undo";
            return enabled ? L"Redo" : L"Nothing to redo";
        }

        // The backdrop mesh is emissive: rgb is the hue and glow scales how hard it
        // burns. The mesh itself authors (0.24, 0.78, 1.0) at 2.4, so these are read
        // against that rather than against a flat sRGB swatch.
        struct Backdrop {
            float r, g, b, glow;
        };

        // What the menu's degrees of "this one matters" look like.
        //
        // Selected is the saturated blue: the overlay the stepper is sitting on, and the
        // one a colour swatch repaints. Idle is the resting state of a pack cover and of
        // an uncommitted overlay in the stepper - near grey, barely glowing, there to
        // give artwork an edge to sit against. Muted is dimmer still, and it is what the
        // applied row wears: that row is a statement of fact rather than a thing the next
        // click acts on, so it should not compete with the stepper below it.
        constexpr Backdrop kSelectedBackdrop = {0.15f, 0.42f, 1.00f, 2.6f};
        constexpr Backdrop kIdleBackdrop     = {0.58f, 0.63f, 0.70f, 0.8f};
        constexpr Backdrop kMutedBackdrop    = {0.62f, 0.65f, 0.70f, 0.35f};

        // The one place that puts the selection backdrop on an element, so every row
        // agrees about which mesh and what size the highlight is.
        void ApplyBackdrop(P3DUI::Element* element) {
            if (!element) return;
            element->SetBackgroundModel(kBackdropModel);
            element->SetBackgroundScale(kBackdropScale);
        }

        void ApplyBackdrop(P3DUI::Element* element, const Backdrop& backdrop) {
            if (!element) return;
            ApplyBackdrop(element);
            element->SetBackgroundColor(backdrop.r, backdrop.g, backdrop.b, 1.0f, backdrop.glow);
        }

        // Same, tinted with a swatch's own colour so the highlight says which colour is
        // selected as well as which swatch. The hue is lifted to full brightness first:
        // the dark swatches (black, dark skin) would otherwise produce a backdrop too
        // dim to read as a highlight at all. The pack-default swatch has no colour of
        // its own, so it keeps the mesh's authored blue.
        void ApplyBackdropTinted(P3DUI::Element* element, const ColorSwatch& swatch) {
            ApplyBackdrop(element);
            if (!element || swatch.isDefault) return;

            float r = ((swatch.rgb >> 16) & 0xFF) / 255.0f;
            float g = ((swatch.rgb >> 8) & 0xFF) / 255.0f;
            float b = (swatch.rgb & 0xFF) / 255.0f;
            const float peak = std::max({r, g, b});
            if (peak < 0.01f) return;  // pure black, nothing to tint with

            const float lift = 1.0f / peak;
            element->SetBackgroundColor(r * lift, g * lift, b * lift, 1.0f, 0.0f);
        }

        // Parses "vrnpce_pack_12" -> 12. Returns npos when the id has a different shape.
        size_t ParseIndex(const std::string& id, const char* prefix) {
            const size_t prefixLength = std::strlen(prefix);
            if (id.rfind(prefix, 0) != 0) return static_cast<size_t>(-1);
            try {
                return static_cast<size_t>(std::stoul(id.substr(prefixLength)));
            } catch (...) {
                return static_cast<size_t>(-1);
            }
        }

        bool IsFemale(RE::Actor* actor) {
            auto* base = actor ? actor->GetActorBase() : nullptr;
            return base && base->GetSex() == RE::SEX::kFemale;
        }

        // Every element 3DUI creates spawns its projectile at whatever the parent's
        // smoother currently holds - the container's centre - because CreateElement
        // initialises it before AddChild has laid it out. Under a visible container that
        // reads as the contents flying apart from the middle for a few frames. Under a
        // hidden one the spawn is skipped, and showing the container afterwards binds
        // each projectile with current already at target, so nothing travels.
        void PopulateHidden(P3DUI::Positionable* container, const std::function<void()>& fill) {
            if (!container) return;
            container->SetVisible(false);
            fill();
            container->SetVisible(true);
        }
    }

    MenuManager* MenuManager::GetSingleton() {
        static MenuManager instance;
        return &instance;
    }

    // An SKSE task is enough for the frame's wait: these are queued from a 3DUI event
    // callback rather than from inside a task, so the drain that runs them is genuinely
    // a later one.
    void MenuManager::NextFrameIfOpen(std::function<void(MenuManager&)> work) {
        SKSE::GetTaskInterface()->AddTask([work = std::move(work)] {
            auto* menu = GetSingleton();
            if (menu->m_open) work(*menu);
        });
    }

    bool MenuManager::Initialize() {
        if (IsInitialized()) return true;

        m_api = P3DUI::GetInterface001();
        if (!m_api) {
            spdlog::error("Menu: 3DUI interface unavailable - is 3DUI.dll installed?");
            return false;
        }
        const uint32_t version = m_api->GetInterfaceVersion();
        spdlog::info("Menu: 3DUI interface version {}", version);
        if (version < P3DUI::P3DUI_INTERFACE_VERSION) {
            // Nothing here breaks against an older 3DUI - the calls it does not have land
            // in reserved vtable slots and do nothing - but the swatch highlights come out
            // untinted, and that is worth saying rather than leaving to look like a bug.
            spdlog::warn("Menu: 3DUI is older than {} - selection backdrops will not be tinted",
                P3DUI::P3DUI_INTERFACE_VERSION);
        }
        return BuildMenu();
    }

    bool MenuManager::BuildMenu() {
        auto rootConfig = P3DUI::RootConfig::Default(kRootId, kModId);
        rootConfig.interactive = true;
        rootConfig.eventCallback = &MenuManager::OnEvent;

        m_root = m_api->GetOrCreateRoot(rootConfig);
        if (!m_root) {
            spdlog::error("Menu: could not create the 3DUI root");
            return false;
        }

        // Bend the rows round the player like a curved monitor, the same treatment VR
        // Dress Up gives its wheel. It costs the layouts nothing - 3DUI applies it after
        // they have laid out, so spacings and scroll extents are still the flat numbers
        // they were tuned as. See kMenuCurveRadius for the radius and for why the
        // vertical axis is left alone.
        m_root->SetCurvature(kMenuCurveRadius, /*horizontal*/ true, /*vertical*/ false,
                             /*tiltElements*/ true);

        auto makeRow = [this](const char* id, float spacing, float width) -> P3DUI::ScrollableContainer* {
            auto config = P3DUI::ColumnGridConfig::Default(id);
            config.columnSpacing = spacing;
            config.rowSpacing = spacing;
            config.numRows = 1;
            config.visibleWidth = width;

            auto* row = m_api->CreateColumnGrid(config);
            if (!row) return nullptr;

            m_root->AddChild(row);
            row->SetFillDirection(P3DUI::VerticalFill::TopToBottom, P3DUI::HorizontalFill::LeftToRight);
            row->SetOrigin(P3DUI::VerticalOrigin::Center, P3DUI::HorizontalOrigin::Center);
            return row;
        };

        auto makeText = [this](const char* id, float scale) -> P3DUI::Text* {
            auto config = P3DUI::TextConfig::Default(id);
            config.facingMode = P3DUI::FacingMode::YawOnly;
            config.scale = scale;

            auto* text = m_api->CreateText(config);
            if (!text) return nullptr;

            m_root->AddChild(text);
            text->SetVisible(false);
            return text;
        };

        // What the actor already wears. Narrow on purpose so it scrolls rather than
        // growing sideways past the rest of the menu once an actor has a few on.
        m_appliedRow  = makeRow(kAppliedRowId, 8.0f, kStripWidth);
        m_appliedText = makeText(kAppliedTextId, kRowTextScale);

        // Five elements, never scrolls.
        m_pickerRow = makeRow(kPickerRowId, 8.0f, 1000.0f);
        m_pickText  = makeText(kPickTextId, kRowTextScale);

        // Wide visible width so the handful of tool buttons never scroll. Same spacing
        // as the body menu's rows - the two menus share the orb and the tool buttons,
        // so a switch should not shuffle them.
        m_toolRow = makeRow(kToolRowId, 8.0f, 1000.0f);

        // The pack row and the palette share a line and are never both up. Both are
        // tight: a cover and a swatch are single flat discs with nothing to read, so
        // they sit close enough to scan as one strip rather than a row of separate
        // things.
        m_packRow  = makeRow(kPackRowId, 7.8f, 45.0f);
        m_colorRow = makeRow(kColorRowId, 7.8f, 45.0f);
        m_colorRow->SetVisible(false);

        m_infoText = makeText(kInfoId, kRowTextScale);

        LayoutRows();
        m_root->SetVisible(false);
        spdlog::info("Menu: built");
        return true;
    }

    // Fixed: the pack row and the palette occupy the same line rather than pushing each
    // other around, so opening the colours never moves the rest of the menu.
    void MenuManager::LayoutRows() {
        if (m_appliedRow)  m_appliedRow->SetLocalPosition(0.0f, 0.0f, kAppliedRowZ);
        if (m_appliedText) m_appliedText->SetLocalPosition(0.0f, 0.0f, kAppliedTextZ);
        if (m_pickerRow)   m_pickerRow->SetLocalPosition(0.0f, 0.0f, kPickerRowZ);
        if (m_pickText)    m_pickText->SetLocalPosition(0.0f, 0.0f, kPickTextZ);
        if (m_packRow)     m_packRow->SetLocalPosition(0.0f, 0.0f, kSourceRowZ);
        if (m_colorRow)    m_colorRow->SetLocalPosition(0.0f, 0.0f, kSourceRowZ);
        if (m_infoText)    m_infoText->SetLocalPosition(0.0f, 0.0f, kInfoZ);
        if (m_toolRow)     m_toolRow->SetLocalPosition(0.0f, 0.0f, kToolRowZ);
    }

    RE::Actor* MenuManager::GetTargetActor() const {
        if (m_targetPlayer) return RE::PlayerCharacter::GetSingleton();

        auto actor = m_npcHandle.get();
        return actor ? actor.get() : nullptr;
    }

    void MenuManager::OpenForActor(RE::Actor* actor, bool isLeftHand) {
        if (!IsInitialized() || !actor) return;

        // Reopening on a second NPC must not strand the preview on the first one:
        // the release still resolves the old target, so it has to run before the swap.
        DropPreviewSlot();

        m_npcHandle = actor->GetHandle();
        m_targetPlayer = false;
        m_selectedOverlay.clear();
        m_pickIndex = static_cast<size_t>(-1);
        m_awaitingNodes = false;
        m_open = true;

        // Per sitting, the same lifetime the body menu's session snapshot has.
        History::GetSingleton()->Clear();
        m_slotsFull = false;
        m_slotCheckAt = Clock::now();

        // Asked for here rather than at the first preview. An NPC has no overlay geometry
        // until RaceMenu builds it, the build goes on the task queue, and anything written
        // before it lands is discarded - so the request goes in while the player is still
        // reading the menu, and the geometry is usually there by the first chevron.
        Skee::EnsureOverlays(actor);

        spdlog::info("Menu: opening for {:08X} ({})", actor->GetFormID(), actor->GetName());

        // A previous sitting leaves where the player dragged the menu behind as a local
        // offset. An open from the actor menu is meant to land on the hand, so it starts
        // clean.
        m_root->SetLocalPosition(0.0f, 0.0f, 0.0f);
        m_root->SetVRAnchor(P3DUI::VRAnchorType::HMD);
        m_root->ShowAtHand(isLeftHand);

        FrameHook::GetSingleton()->Register(this, [] { GetSingleton()->Tick(); });

        auto* catalog = Catalog::GetSingleton();
        if (catalog->IsReady()) {
            RefreshAll();
            return;
        }

        // First open of the session: nothing has been parsed yet. Show the menu straight
        // away with a status line and fill it in when the build finishes.
        PopulateToolRow();
        ShowInfo(L"Loading overlays...");
        catalog->StartBuildAsync([this](bool success) {
            // The build spans frames; the menu may have been closed or retargeted since.
            if (!m_open) {
                spdlog::debug("Menu: catalog finished after the menu closed, discarding");
                return;
            }
            if (!success) {
                ShowInfo(L"No overlay packs found");
                return;
            }
            SelectDefaultPack();

            // Takes the status line off "Loading overlays..." on its way past.
            RefreshAll();
        });
    }

    void MenuManager::Close() {
        if (!m_open) return;

        // Closing on an overlay keeps it. It runs before the teardown below so the rows
        // it repaints are still standing, and the DropPreviewSlot after it is the
        // backstop for the cases KeepPreview leaves a preview in place - it has already
        // given the slot up on every path that committed.
        KeepPreview();
        DropPreviewSlot();
        EndRepeat();
        m_awaitingNodes = false;
        m_open = false;

        FrameHook::GetSingleton()->Unregister(this);

        m_pickEntries.clear();
        m_appliedEntries.clear();
        m_appliedElements.clear();
        m_packElements.clear();
        m_colorElements.clear();
        m_slotLocations.clear();
        m_pickIcon = nullptr;
        m_randomButton = nullptr;
        m_undoButton = nullptr;
        m_redoButton = nullptr;

        History::GetSingleton()->Clear();

        if (m_root) {
            m_root->EndPositioning();
            m_root->SetVisible(false);
        }
        spdlog::debug("Menu: closed");
    }

    void MenuManager::SelectDefaultPack() {
        const auto& packs = Catalog::GetSingleton()->GetPacks();
        m_selectedPack = 0;

        if (Config::options.defaultPack.empty()) return;
        for (size_t i = 0; i < packs.size(); ++i) {
            if (packs[i].modId == Config::options.defaultPack) {
                m_selectedPack = i;
                return;
            }
        }
        spdlog::warn("Menu: sDefaultPack \"{}\" is not an installed pack, using \"{}\"",
                     Config::options.defaultPack, packs.empty() ? "(none)" : packs[0].modId);
    }

    void MenuManager::RefreshAll() {
        auto* actor = GetTargetActor();
        if (actor) StateManager::GetSingleton()->SyncFromActor(actor);

        RebuildPickList();
        RebuildSlotLocations();

        // Read once here rather than left to the poll, so a menu opened on an actor with
        // no room left comes up already showing that rather than showing a stepper for a
        // quarter of a second first.
        m_slotsFull = SlotsFull();

        PopulateAppliedRow();
        PopulatePickerRow();
        PopulatePackRow();
        PopulateToolRow();
        UpdatePickText();

        // Nothing has anything to say yet, so the status line goes back to naming the
        // row above it rather than sitting blank.
        ClearInfo();
    }

    // ===== What the actor is wearing =====

    void MenuManager::PopulateAppliedRow() {
        if (!m_appliedRow) return;

        m_appliedRow->Clear();
        m_appliedElements.clear();
        m_appliedEntries.clear();

        auto* actor = GetTargetActor();
        const auto* state = actor ? StateManager::GetSingleton()->Find(actor) : nullptr;
        if (state) {
            const auto* catalog = Catalog::GetSingleton();
            for (const auto& applied : state->applied) {
                if (const auto* entry = catalog->FindEntry(applied.qualifiedId)) {
                    m_appliedEntries.push_back(entry);
                }
            }
        }

        UpdateAppliedText();

        // An empty strip is a line of nothing where the player expects content, and it
        // still costs the row's own layout pass.
        if (m_appliedEntries.empty()) {
            m_appliedRow->SetVisible(false);
            return;
        }

        PopulateHidden(m_appliedRow, [this] { FillAppliedRow(); });
        spdlog::debug("Menu: applied row rebuilt with {} overlays", m_appliedEntries.size());
    }

    void MenuManager::FillAppliedRow() {
        const auto* current = CurrentPick();

        for (size_t i = 0; i < m_appliedEntries.size(); ++i) {
            const auto* entry = m_appliedEntries[i];

            auto config = P3DUI::ElementConfig::Default("");
            const auto id = std::string(kAppliedPrefix) + std::to_string(i);
            config.id = id.c_str();
            config.texturePath = entry->texture.c_str();

            // With every slot spent, pressing one of these takes it off - see
            // OnAppliedActivated - so the tooltip has to say the thing the press does.
            const auto tooltip = m_slotsFull ? L"Take off " + entry->displayName
                                             : entry->displayName;
            config.tooltip = tooltip.c_str();
            config.scale = Config::options.elementScale;
            config.facingMode = P3DUI::FacingMode::None;

            auto* element = m_api->CreateElement(config);
            if (!element) continue;

            ApplyBackdrop(element, entry == current ? kSelectedBackdrop : kMutedBackdrop);

            m_appliedRow->AddChild(element);
            m_appliedElements.push_back(element);
        }

        m_appliedRow->ResetScroll();
    }

    void MenuManager::UpdateAppliedHighlight() {
        const auto* current = CurrentPick();
        for (size_t i = 0; i < m_appliedElements.size() && i < m_appliedEntries.size(); ++i) {
            ApplyBackdrop(m_appliedElements[i],
                          m_appliedEntries[i] == current ? kSelectedBackdrop : kMutedBackdrop);
        }
    }

    void MenuManager::OnAppliedActivated(size_t index) {
        if (index >= m_appliedEntries.size()) return;

        // With the stepper hidden there is nothing to select these into, and taking one
        // off is the only move that gets the player anywhere - so that is what the row
        // does while it is the only row left.
        if (m_slotsFull) {
            RemoveApplied(index);
            return;
        }
        SelectEntry(m_appliedEntries[index]);
    }

    void MenuManager::RemoveApplied(size_t index) {
        auto* actor = GetTargetActor();
        if (!actor || index >= m_appliedEntries.size()) return;

        const auto* entry = m_appliedEntries[index];
        auto before = History::Capture(actor);

        if (!StateManager::GetSingleton()->Remove(actor, *entry)) return;

        if (m_selectedOverlay == entry->qualifiedId) m_selectedOverlay.clear();
        History::GetSingleton()->Record(actor, std::move(before));
        EditSession::GetSingleton()->NoteChange("overlay");

        PersistToOdf();

        PopulateAppliedRow();
        UpdateHistoryButtons();
        UpdatePickIcon();
    }

    // ===== The stepper =====

    void MenuManager::RebuildPickList() {
        m_pickEntries.clear();

        auto* actor = GetTargetActor();
        const auto& packs = Catalog::GetSingleton()->GetPacks();
        if (!actor || m_selectedPack >= packs.size()) {
            m_pickIndex = static_cast<size_t>(-1);
            return;
        }

        m_pickEntries = Catalog::GetSingleton()->GetEntriesForPack(packs[m_selectedPack], IsFemale(actor));

        // A pack switch or a first open starts at the top; an index that already points
        // inside the new list is kept, which is what SelectEntry relies on.
        if (m_pickEntries.empty() || m_pickIndex >= m_pickEntries.size()) {
            m_pickIndex = m_pickEntries.empty() ? static_cast<size_t>(-1) : 0;
        }
    }

    const Entry* MenuManager::CurrentPick() const {
        return m_pickIndex < m_pickEntries.size() ? m_pickEntries[m_pickIndex] : nullptr;
    }

    bool MenuManager::PickIsApplied() const {
        const auto* entry = CurrentPick();
        auto* actor = GetTargetActor();
        return entry && actor && StateManager::GetSingleton()->IsApplied(actor, *entry);
    }

    std::wstring MenuManager::PickTooltip() const {
        const auto* entry = CurrentPick();
        if (!entry) return L"Nothing in this pack fits";

        return entry->displayName + L" (" + std::to_wstring(m_pickIndex + 1) +
               L"/" + std::to_wstring(m_pickEntries.size()) + L")";
    }

    void MenuManager::PopulatePickerRow() {
        if (!m_pickerRow) return;

        // Cleared before the pointers are dropped, not after: Clear() destroys the
        // elements they point at, and the refill below is not guaranteed to run.
        m_pickIcon = nullptr;
        m_randomButton = nullptr;
        m_pickerRow->Clear();

        // No room for another overlay means nothing here can do anything: the chevrons
        // would step through a list nothing can be taken from, and the check button would
        // refuse every press. Cleared rather than merely hidden - 3DUI charges for every
        // live element on every frame, on screen or not.
        if (m_pickEntries.empty() || m_slotsFull) {
            m_pickerRow->SetVisible(false);
            if (m_pickText) m_pickText->SetVisible(false);
            return;
        }

        PopulateHidden(m_pickerRow, [this] {
            auto add = [this](const char* id, const char* texture, const std::wstring& tooltip,
                              float scale) -> P3DUI::Element* {
                auto config = P3DUI::ElementConfig::Default(id);
                config.texturePath = texture;
                config.tooltip = tooltip.c_str();
                config.scale = scale;
                config.facingMode = P3DUI::FacingMode::None;

                auto* element = m_api->CreateElement(config);
                if (element) m_pickerRow->AddChild(element);
                return element;
            };

            const bool applied = PickIsApplied();

            m_randomButton = add(kRandomId, applied ? kTexRemove : kTexRandom,
                                 applied ? L"Take this one off" : L"Pick one at random", kToolScale);
            add(kPickPrevId, kTexPrev, L"Previous overlay - hold to run through them", kToolScale);

            const auto* entry = CurrentPick();
            m_pickIcon = add(kPickIconId, entry ? entry->texture.c_str() : nullptr,
                             PickTooltip(), Config::options.elementScale);
            ApplyBackdrop(m_pickIcon, applied ? kSelectedBackdrop : kIdleBackdrop);

            add(kPickNextId, kTexNext, L"Next overlay - hold to run through them", kToolScale);
            add(kCommitId, kTexCommit, L"Keep this one", kToolScale);
        });
    }

    void MenuManager::StepPick(int delta) {
        if (m_pickEntries.empty()) return;

        const size_t count = m_pickEntries.size();
        const size_t current = m_pickIndex < count ? m_pickIndex : 0;

        // Wraps, so a held chevron walks the whole pack without dead-ending.
        m_pickIndex = (current + static_cast<size_t>(delta) + count) % count;
        ShowPick(true);
    }

    void MenuManager::RandomPick() {
        if (m_pickEntries.size() < 2) return;

        // Never lands on the one already showing: a dice that does nothing reads as a
        // dead button rather than as bad luck.
        static std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<size_t> pick(1, m_pickEntries.size() - 1);

        const size_t current = m_pickIndex < m_pickEntries.size() ? m_pickIndex : 0;
        m_pickIndex = (current + pick(rng)) % m_pickEntries.size();
        ShowPick(true);
    }

    void MenuManager::SelectEntry(const Entry* entry) {
        if (!entry) return;

        const auto& packs = Catalog::GetSingleton()->GetPacks();
        for (size_t i = 0; i < packs.size(); ++i) {
            if (packs[i].modId == entry->modId) {
                m_selectedPack = i;
                break;
            }
        }

        RebuildPickList();
        for (size_t i = 0; i < m_pickEntries.size(); ++i) {
            if (m_pickEntries[i] == entry) {
                m_pickIndex = i;
                break;
            }
        }

        // The pack under the stepper moved, so the filter row has to agree with it.
        PopulatePackRow();
        PopulatePickerRow();
        ShowPick(true);
    }

    void MenuManager::ShowPick(bool preview) {
        if (preview) {
            auto* actor = GetTargetActor();
            const auto* entry = CurrentPick();

            if (entry && actor) {
                // Already committed: it is on the actor for real, and previewing it would
                // either fail for want of a slot or stamp a second copy on top.
                if (StateManager::GetSingleton()->IsApplied(actor, *entry)) {
                    EndPreview();
                } else {
                    PreviewCurrent();
                }
            }
        }

        UpdatePickIcon();
        UpdatePickText();
        UpdateAppliedHighlight();
    }

    void MenuManager::UpdatePickIcon() {
        const auto* entry = CurrentPick();
        const bool applied = PickIsApplied();

        if (m_pickIcon && entry) {
            m_pickIcon->SetTexture(entry->texture.c_str());
            m_pickIcon->SetTooltip(PickTooltip().c_str());
            // Committed shouts, uncommitted stays at the row's resting tone - the
            // backdrop is how you tell "this is on them" from "this is only a look".
            ApplyBackdrop(m_pickIcon, applied ? kSelectedBackdrop : kIdleBackdrop);
        }

        // One button, two jobs: roll the dice while there is nothing to undo, take the
        // overlay off once it is committed.
        if (m_randomButton) {
            m_randomButton->SetTexture(applied ? kTexRemove : kTexRandom);
            m_randomButton->SetTooltip(applied ? L"Take this one off" : L"Pick one at random");
        }
    }

    void MenuManager::UpdatePickText() {
        if (!m_pickText) return;

        const auto* entry = CurrentPick();
        if (!entry || m_slotsFull) {
            m_pickText->SetVisible(false);
            return;
        }

        m_pickText->SetText(entry->displayName.c_str());
        m_pickText->SetVisible(true);
    }

    void MenuManager::PreviewCurrent() {
        const auto* entry = CurrentPick();
        auto* actor = GetTargetActor();
        if (!entry || !actor) return;

        Skee::EnsureOverlays(actor);

        // RaceMenu schedules the overlay build; it is not done when AddOverlays returns.
        // A preview written before the geometry exists is dropped without a word, and
        // that is exactly what "the chevrons only change the text" was: the write went
        // nowhere. Park the request and let Tick take it up once the nodes arrive.
        if (!Skee::HasOverlayNodes(actor, entry->location)) {
            if (!m_awaitingNodes) {
                m_awaitingNodes = true;
                m_awaitingSince = Clock::now();
                ShowInfo(L"Preparing overlays...");
            }
            return;
        }
        m_awaitingNodes = false;

        // Tinted with the selected swatch, so what you are looking at is what the check
        // button will commit.
        const auto appearance = TintedAppearance(*entry);

        // Stepping to another overlay of the same kind stays in the slot the last one
        // was using: it is emptied and refilled in place rather than released and looked
        // for again. Looking again is what the old code did, and it does not work - the
        // release is a Papyrus call that has not run yet, so the search finds that slot
        // still occupied and moves on to the next one. A chevron held down did that once
        // per step and had walked the actor out of slots within a second, after which
        // nothing appeared on them at all.
        //
        // Emptying first rather than writing straight over matters: an appearance only
        // carries the keys its pack declared, so an overlay with no alpha of its own
        // would otherwise inherit the last one's. The VM runs these in the order they
        // are queued, so the empty always lands before the refill.
        const bool reuse = !m_previewNode.empty() && m_previewLocation == entry->location;
        EndPreview();

        std::string node;
        if (reuse) {
            node = m_previewNode;
        } else {
            // A different kind of slot: hand the old one back, it is no use for this
            // overlay, and take one of the right kind.
            DropPreviewSlot();

            auto found = Skee::FindFreeSlot(actor, entry->location);
            if (!found) {
                ShowInfo(L"Every overlay slot is full - take one off first");
                return;
            }
            node = std::move(*found);
        }

        if (Skee::PreviewOnSlot(actor, IsFemale(actor), node, appearance)) {
            m_previewId = entry->qualifiedId;
            m_previewNode = std::move(node);
            m_previewLocation = entry->location;
            ClearInfo();
        }
    }

    // RaceMenu never says when an overlay build has finished, so the only way to know is
    // to look. One named lookup a frame, and only while a preview is waiting on it.
    void MenuManager::RetryPendingPreview() {
        const auto* entry = CurrentPick();
        auto* actor = GetTargetActor();
        if (!entry || !actor) {
            m_awaitingNodes = false;
            return;
        }

        if (Skee::HasOverlayNodes(actor, entry->location)) {
            m_awaitingNodes = false;
            PreviewCurrent();
            return;
        }

        // A build this late is not coming - bPlayerOnly, an actor with no body addon, a
        // RaceMenu that refused. Better to say so than to sit on "Preparing overlays..."
        // for the rest of the sitting.
        if (Clock::now() - m_awaitingSince >= kNodeWait) {
            m_awaitingNodes = false;
            spdlog::warn("Menu: RaceMenu has not built the {} overlay nodes on {:08X}",
                         Skee::LocationName(entry->location), actor->GetFormID());
            ShowInfo(L"RaceMenu could not add overlays to this NPC");
        }
    }

    // Takes the preview back off the actor but keeps the slot: it was free when we took
    // it and nothing else in this menu will claim it, so holding on to it is what lets
    // the next step write straight over it instead of hunting for a slot the game has
    // not caught up with yet.
    void MenuManager::EndPreview() {
        if (m_previewId.empty()) return;

        // The preview always went into a slot that was free, so clearing it restores the
        // actor exactly - there is nothing underneath to put back.
        if (auto* actor = GetTargetActor()) {
            Skee::ClearSlot(actor, IsFemale(actor), m_previewNode);
        }

        m_previewId.clear();
    }

    // The same, and gives the slot up as well. For when the slot is about to belong to
    // something else - a commit writing into it - or to nobody, because the menu is
    // moving to another actor or closing.
    void MenuManager::DropPreviewSlot() {
        EndPreview();
        m_previewNode.clear();
    }

    void MenuManager::CommitPick() {
        const auto* entry = CurrentPick();
        auto* actor = GetTargetActor();
        if (!entry || !actor) return;

        auto* state = StateManager::GetSingleton();
        if (state->IsApplied(actor, *entry)) return;

        // Taken before the write, handed to the history after it: the history compares
        // the two and drops a press that moved nothing.
        auto before = History::Capture(actor);

        // The preview of this very overlay is already in a slot, so the commit goes into
        // that same slot rather than releasing it and searching: the release would not
        // have reached the actor by the time the search reads it, and the overlay would
        // land in a second slot with the first still looking taken.
        const auto previewed = (m_previewId == entry->qualifiedId) ? m_previewNode : std::string{};
        DropPreviewSlot();

        const auto appearance = TintedAppearance(*entry);
        if (!state->Apply(actor, *entry, &appearance, previewed)) {
            ShowInfo(L"Every overlay slot is full - take one off first");
            return;
        }

        // The one you just put on becomes the one a colour swatch repaints.
        m_selectedOverlay = entry->qualifiedId;
        History::GetSingleton()->Record(actor, std::move(before));
        EditSession::GetSingleton()->NoteChange("overlay");

        ClearInfo();
        PersistToOdf();

        PopulateAppliedRow();
        UpdateHistoryButtons();
        UpdatePickIcon();
    }

    // What leaving does with the overlay you are looking at. Taking it back off was the
    // old bargain, and in practice it read as the menu undoing the player's work: you
    // step onto one you like, reach for another pack to compare, and it comes off. The
    // check button is still there for committing without moving, but walking away is now
    // a way of saying yes rather than no.
    //
    // Nothing is previewed until the player steps, rolls or picks off the applied row,
    // so an empty id means they have chosen nothing and there is nothing to keep - which
    // is what stops a menu that was only opened and closed again from stamping the
    // default pack's first overlay onto the actor.
    void MenuManager::KeepPreview() {
        if (m_previewId.empty()) return;

        // CommitPick writes whatever the stepper is sitting on. In every path that
        // reaches here that is the previewed overlay, but the two disagreeing would mean
        // committing something the player never saw, so that one goes back off instead.
        const auto* entry = CurrentPick();
        if (!entry || entry->qualifiedId != m_previewId) {
            spdlog::warn("Menu: preview \"{}\" is not what the stepper is on, dropping it", m_previewId);
            DropPreviewSlot();
            return;
        }

        spdlog::debug("Menu: keeping previewed overlay \"{}\"", m_previewId);
        CommitPick();
    }

    void MenuManager::RemovePick() {
        const auto* entry = CurrentPick();
        auto* actor = GetTargetActor();
        if (!entry || !actor) return;

        auto before = History::Capture(actor);
        if (!StateManager::GetSingleton()->Remove(actor, *entry)) return;

        if (m_selectedOverlay == entry->qualifiedId) m_selectedOverlay.clear();
        History::GetSingleton()->Record(actor, std::move(before));
        EditSession::GetSingleton()->NoteChange("overlay");

        PersistToOdf();

        PopulateAppliedRow();
        UpdateHistoryButtons();
        UpdatePickIcon();
    }

    // ===== Pack filter =====

    void MenuManager::PopulatePackRow() {
        if (!m_packRow) return;

        m_packRow->Clear();
        m_packElements.clear();

        // The palette takes this same line. Cleared rather than merely hidden: 3DUI
        // charges per live element every frame, whether or not it is on screen.
        //
        // The full-slots case goes the same way: a pack filter over a stepper that is not
        // there is a control whose every press changes nothing visible. The palette is
        // deliberately still offered while full, because repainting what the actor
        // already wears is a thing that still works.
        if (m_colorRowOpen || m_slotsFull) {
            m_packRow->SetVisible(false);
            return;
        }

        const auto& packs = Catalog::GetSingleton()->GetPacks();
        if (m_selectedPack >= packs.size()) m_selectedPack = 0;

        PopulateHidden(m_packRow, [&] { FillPackRow(packs); });

        spdlog::debug("Menu: pack row rebuilt with {} packs, selected {}", m_packElements.size(), m_selectedPack);
    }

    void MenuManager::FillPackRow(const std::vector<Pack>& packs) {
        for (size_t i = 0; i < packs.size(); ++i) {
            const auto& pack = packs[i];

            auto config = P3DUI::ElementConfig::Default("");
            const auto id = std::string(kPackPrefix) + std::to_string(i);
            config.id = id.c_str();
            config.texturePath = pack.coverTexture.c_str();
            config.tooltip = pack.displayName.c_str();
            config.scale = kPackScale;
            config.facingMode = P3DUI::FacingMode::None;

            auto* element = m_api->CreateElement(config);
            if (!element) continue;

            // Every cover carries one: pack artwork is busy and dark and was getting
            // lost against the world, and a row of discs reads as a row of buttons.
            // The selected one takes the same blue the stepper's overlay wears.
            ApplyBackdrop(element, i == m_selectedPack ? kSelectedBackdrop : kIdleBackdrop);

            m_packRow->AddChild(element);
            m_packElements.push_back(element);
        }

        m_packRow->ResetScroll();
    }

    void MenuManager::UpdatePackHighlight() {
        for (size_t i = 0; i < m_packElements.size(); ++i) {
            ApplyBackdrop(m_packElements[i], i == m_selectedPack ? kSelectedBackdrop : kIdleBackdrop);
        }
    }

    void MenuManager::OnPackActivated(size_t index) {
        const auto& packs = Catalog::GetSingleton()->GetPacks();
        if (index >= packs.size()) return;

        // Clicking the selected pack again returns to the default rather than emptying
        // the stepper - an empty stepper reads as a broken menu. Decided before anything
        // else so a press that moves nothing stays a no-op rather than committing.
        if (index == m_selectedPack) {
            const auto previous = m_selectedPack;
            SelectDefaultPack();
            if (m_selectedPack == previous) return;
        } else {
            m_selectedPack = index;
        }

        // The stepper is about to walk away from whatever is on show, so it is kept:
        // reaching for another source to see what else there is should not undo the one
        // you already stopped on. Reads the pick list of the pack we are leaving, which
        // RebuildPickList below has not replaced yet.
        KeepPreview();

        spdlog::debug("Menu: pack filter -> \"{}\"", packs[m_selectedPack].modId);

        // Starts at the top of the new pack, and shows it without stamping it on the
        // actor: browsing packs should not leave a trail of overlays behind it.
        m_pickIndex = 0;
        RebuildPickList();
        UpdatePackHighlight();
        PopulatePickerRow();
        ShowPick(false);
    }

    // ===== Colours =====

    void MenuManager::ToggleColorRow() {
        m_colorRowOpen = !m_colorRowOpen;

        if (m_colorRowOpen) {
            PopulateColorRow();
        } else if (m_colorRow) {
            m_colorRow->Clear();
            m_colorElements.clear();
            m_colorRow->SetVisible(false);
        }

        // The pack row shares the line, so it comes and goes with the palette - and so
        // does what the line underneath is a label for.
        PopulatePackRow();
        PopulateToolRow();
        ClearInfo();
        spdlog::debug("Menu: colour row {}", m_colorRowOpen ? "opened" : "closed");
    }

    void MenuManager::PopulateColorRow() {
        if (!m_colorRow) return;

        m_colorRow->Clear();
        m_colorElements.clear();

        PopulateHidden(m_colorRow, [this] { FillColorRow(); });
    }

    void MenuManager::FillColorRow() {
        const auto& palette = GetPalette();
        for (size_t i = 0; i < palette.size(); ++i) {
            auto config = P3DUI::ElementConfig::Default("");
            const auto id = std::string(kColorPrefix) + std::to_string(i);
            const auto texture = SwatchTexture(palette[i]);
            config.id = id.c_str();
            config.texturePath = texture.c_str();
            config.tooltip = palette[i].label;
            config.scale = kPackScale;
            config.facingMode = P3DUI::FacingMode::None;

            auto* element = m_api->CreateElement(config);
            if (!element) continue;

            if (i == m_selectedColor) {
                ApplyBackdropTinted(element, palette[i]);
            }

            m_colorRow->AddChild(element);
            m_colorElements.push_back(element);
        }

        m_colorRow->ResetScroll();
    }

    void MenuManager::UpdateColorHighlight() {
        const auto& palette = GetPalette();
        for (size_t i = 0; i < m_colorElements.size() && i < palette.size(); ++i) {
            if (i == m_selectedColor) {
                ApplyBackdropTinted(m_colorElements[i], palette[i]);
            } else {
                m_colorElements[i]->ClearBackground();
            }
        }
    }

    Skee::Appearance MenuManager::TintedAppearance(const Entry& entry) const {
        const auto& palette = GetPalette();
        const size_t index = m_selectedColor < palette.size() ? m_selectedColor : 0;
        return Tint(entry.appearance, palette[index]);
    }

    void MenuManager::OnColorActivated(size_t index) {
        if (index >= GetPalette().size() || index == m_selectedColor) return;

        m_selectedColor = index;
        spdlog::info("Menu: colour -> {}", GetPalette()[index].name);
        UpdateColorHighlight();
        RetintSelected();

        // The live preview was drawn in the old colour, so redraw it in the new one.
        if (!m_previewId.empty()) ShowPick(true);
    }

    void MenuManager::RetintSelected() {
        if (m_selectedOverlay.empty()) return;

        auto* actor = GetTargetActor();
        if (!actor) return;

        const auto* entry = Catalog::GetSingleton()->FindEntry(m_selectedOverlay);
        if (!entry) return;

        auto before = History::Capture(actor);
        if (!StateManager::GetSingleton()->Retint(actor, *entry, TintedAppearance(*entry))) {
            // Gone from under us - taken off through some other route, or the actor was
            // reloaded. Drop the selection rather than leaving a highlight pointing at
            // something no colour will ever reach.
            m_selectedOverlay.clear();
            PopulateAppliedRow();
            UpdatePickIcon();
            return;
        }

        History::GetSingleton()->Record(actor, std::move(before));
        EditSession::GetSingleton()->NoteChange("overlay");
        PersistToOdf();
        UpdateHistoryButtons();
    }

    // ===== Tools =====

    void MenuManager::PopulateToolRow() {
        if (!m_toolRow) return;

        m_toolRow->Clear();
        PopulateHidden(m_toolRow, [this] { FillToolRow(); });
    }

    // Left to right: undo, redo, the palette, the orb, then the bin, the target toggle
    // and the clothes toggle. Three buttons either side of the handle, so the orb stays
    // in the middle of the row whatever state the buttons are in.
    void MenuManager::FillToolRow() {
        auto addButton = [this](const char* id, const char* texture,
                                const wchar_t* tooltip) -> P3DUI::Element* {
            auto config = P3DUI::ElementConfig::Default(id);
            config.texturePath = texture;
            config.tooltip = tooltip;
            config.scale = kToolScale;
            config.facingMode = P3DUI::FacingMode::None;

            auto* element = m_api->CreateElement(config);
            if (element) m_toolRow->AddChild(element);
            return element;
        };

        // The pair leads the row, the same place VR Dress Up puts them, so the hand goes
        // to the same end of either menu to take a press back.
        const bool canUndo = History::GetSingleton()->CanUndo();
        const bool canRedo = History::GetSingleton()->CanRedo();
        m_undoButton = addButton(kUndoId, HistoryTexture(true, canUndo),
                                 HistoryTooltip(true, canUndo));
        m_redoButton = addButton(kRedoId, HistoryTexture(false, canRedo),
                                 HistoryTooltip(false, canRedo));

        // The library's highlight palette is how every other icon says "this is on", and
        // the swatch row it opens is far enough down the menu to be easy to miss.
        addButton(kColorsId, m_colorRowOpen ? kTexColorsOn : kTexColors,
                  m_colorRowOpen ? L"Hide the colours" : L"Pick a colour");

        // The same orb the body menu uses: grip drags the menu, trigger closes it.
        auto orbConfig = P3DUI::ElementConfig::Default(kAnchorId);
        orbConfig.modelPath = kAnchorModel;
        orbConfig.tooltip = L"Move or close";
        orbConfig.scale = kToolScale;
        orbConfig.isAnchorHandle = true;
        orbConfig.facingMode = P3DUI::FacingMode::None;
        if (auto* orb = m_api->CreateElement(orbConfig)) m_toolRow->AddChild(orb);

        // Immediately right of the orb, and unconditional in both target modes rather
        // than gated on us having something on: the applied row is only ever as complete
        // as the last sync, so "nothing to take off" is something the button says rather
        // than a reason for it to go missing.
        //
        // It takes off what this menu recognises and nothing else. The player in
        // particular arrives from chargen - or from another overlay mod - with hand and
        // face slots already full of textures no installed pack declares, and those are
        // not ours to wipe: emptying someone else's tattoos to free a slot is not a
        // trade this button gets to make on the player's behalf.
        addButton(kClearId, kTexClear, L"Take our overlays off");

        addButton(kTargetId, m_targetPlayer ? kTexTargetPlayer : kTexTargetNpc,
                  m_targetPlayer ? L"Editing yourself - switch to the NPC"
                                 : L"Editing the NPC - switch to yourself");

        // Undressing is how you see an overlay on the body it was painted for. Same
        // two-state cycle as VR Dress Up's button: the icon says what pressing it does
        // next, and the session redresses on close either way.
        auto* actor = GetTargetActor();
        const bool undressed = actor && UndressManager::GetSingleton()->IsUndressed(actor);
        addButton(kClothesId, undressed ? kTexRedress : kTexUndress,
                  undressed ? L"Put their clothes back" : L"Take their clothes off");
    }

    void MenuManager::ClearAllOverlays() {
        auto* actor = GetTargetActor();
        if (!actor) return;

        DropPreviewSlot();

        auto before = History::Capture(actor);
        const size_t cleared = StateManager::GetSingleton()->ClearAll(actor);
        if (cleared == 0) {
            ShowInfo(L"There is nothing of ours to take off");
            return;
        }

        m_selectedOverlay.clear();
        History::GetSingleton()->Record(actor, std::move(before));
        EditSession::GetSingleton()->NoteChange("overlay");

        ClearInfo();
        PersistToOdf();

        PopulateAppliedRow();
        UpdateHistoryButtons();
        UpdatePickIcon();
    }

    void MenuManager::OnToolActivated(const std::string& id) {
        auto* actor = GetTargetActor();

        if (id == kColorsId) {
            ToggleColorRow();
            return;
        }

        if (id == kClearId) {
            ClearAllOverlays();
            return;
        }

        if (id == kTargetId) {
            // Before the flip: the commit resolves the target itself, and after it that
            // would be the wrong body. Same bargain as a source switch - the overlay you
            // stopped on stays on whoever you were looking at while you go and look at
            // the other one.
            KeepPreview();
            DropPreviewSlot();
            m_selectedOverlay.clear();
            m_pickIndex = static_cast<size_t>(-1);
            m_targetPlayer = !m_targetPlayer;

            // The stack describes the body we are leaving, and putting one of its
            // snapshots on the other one is a press nobody made.
            History::GetSingleton()->Clear();
            spdlog::info("Menu: target switched to {}", m_targetPlayer ? "the player" : "the NPC");
            RefreshAll();
            return;
        }

        if (id == kClothesId) {
            if (!actor) return;

            auto* undress = UndressManager::GetSingleton();
            if (undress->IsUndressed(actor)) {
                undress->Redress(actor);
            } else {
                undress->UndressFull(actor);
            }
            PopulateToolRow();
            return;
        }

    }

    // ===== Undo and redo =====

    // The history moved without the row being rebuilt: swap the faces in place, so a
    // press does not destroy the element the hand is still resting on.
    void MenuManager::UpdateHistoryButtons() {
        auto* history = History::GetSingleton();

        if (m_undoButton) {
            const bool can = history->CanUndo();
            m_undoButton->SetTexture(HistoryTexture(true, can));
            m_undoButton->SetTooltip(HistoryTooltip(true, can));
        }
        if (m_redoButton) {
            const bool can = history->CanRedo();
            m_redoButton->SetTexture(HistoryTexture(false, can));
            m_redoButton->SetTooltip(HistoryTooltip(false, can));
        }
    }

    void MenuManager::OnUndo() {
        auto* actor = GetTargetActor();
        if (!actor || !History::GetSingleton()->CanUndo()) return;  // greyed out

        // The preview is not in the history - it is not on the actor for real - and
        // leaving it would put it back on top of whatever the restore writes.
        DropPreviewSlot();

        History::GetSingleton()->Undo(actor);
        AfterHistoryRestore();
    }

    void MenuManager::OnRedo() {
        auto* actor = GetTargetActor();
        if (!actor || !History::GetSingleton()->CanRedo()) return;

        DropPreviewSlot();

        History::GetSingleton()->Redo(actor);
        AfterHistoryRestore();
    }

    void MenuManager::AfterHistoryRestore() {
        // Whatever a swatch was repainting may not be on the actor any more.
        m_selectedOverlay.clear();

        EditSession::GetSingleton()->NoteChange("overlay");
        PersistToOdf();

        // Rows only, no SyncFromActor: the restore's writes are still queued in the
        // Papyrus VM, so reconciling the record against the actor's live slots now would
        // read the state we have just left and write it back over the one we asked for.
        // The slot poll picks the rest up once the writes have landed.
        PopulateAppliedRow();
        UpdateHistoryButtons();
        UpdatePickIcon();
        ClearInfo();
    }

    // ===== Slots =====

    void MenuManager::RebuildSlotLocations() {
        m_slotLocations.clear();

        auto* actor = GetTargetActor();
        if (!actor) return;

        const auto wanted = IsFemale(actor) ? Gender::Female : Gender::Male;
        for (const auto& entry : Catalog::GetSingleton()->GetEntries()) {
            if (entry.gender != Gender::Any && entry.gender != wanted) continue;
            if (std::find(m_slotLocations.begin(), m_slotLocations.end(), entry.location) ==
                m_slotLocations.end()) {
                m_slotLocations.push_back(entry.location);
            }
        }
    }

    bool MenuManager::SlotsFull() const {
        auto* actor = GetTargetActor();
        if (!actor || m_slotLocations.empty()) return false;

        // The slot a live preview sits in is one the stepper writes straight over, so it
        // is not spent until the preview is committed - and hiding the stepper out from
        // under the overlay the player is looking at would be the worst possible moment.
        if (!m_previewNode.empty()) return false;

        for (const auto location : m_slotLocations) {
            // Geometry RaceMenu has not built yet reads as having no free slots at all.
            // Waiting for it is PreviewCurrent's job; treating it as full here would hide
            // the menu on every open.
            if (!Skee::HasOverlayNodes(actor, location)) return false;
            if (Skee::HasFreeSlot(actor, location)) return false;
        }
        return true;
    }

    void MenuManager::PollSlotFullness() {
        const auto now = Clock::now();
        if (now - m_slotCheckAt < kSlotCheckInterval) return;
        m_slotCheckAt = now;

        const bool full = SlotsFull();
        if (full == m_slotsFull) return;

        m_slotsFull = full;
        spdlog::info("Menu: overlay slots {} on {:08X}",
                     full ? "are full - hiding the stepper and the pack filter"
                          : "have room again",
                     GetTargetActor() ? GetTargetActor()->GetFormID() : 0);

        // The applied row goes too: what its items do, and so what they say they do,
        // depends on this.
        PopulateAppliedRow();
        PopulatePickerRow();
        PopulatePackRow();
        UpdatePickText();
        ClearInfo();
    }

    // ===== Text =====

    void MenuManager::UpdateAppliedText() {
        if (!m_appliedText) return;

        // Nothing on them, nothing to head - and the row above is hidden too, so a label
        // there would be captioning empty space.
        if (m_appliedEntries.empty()) {
            m_appliedText->SetVisible(false);
            return;
        }
        m_appliedText->SetText(L"Applied Overlays");
        m_appliedText->SetVisible(true);
    }

    // What the status line says when nothing has anything to tell it. It sits under the
    // row that shares its line, so it names that row: the packs you can browse, or the
    // colours, whichever is up.
    std::wstring MenuManager::RestingInfo() const {
        // It names whatever is sharing its line, and the palette is the one thing that
        // still opens with the slots full.
        if (m_colorRowOpen) return L"Colours";

        // Full slots take the stepper and the pack filter away with them, so the line
        // under where they were is the only place left to say why.
        if (m_slotsFull) return L"No overlay slots left - take one off to add another";
        return L"Available Overlays";
    }

    void MenuManager::ShowInfo(const std::wstring& text) {
        if (!m_infoText) return;
        m_infoText->SetText(text.c_str());
        m_infoText->SetVisible(true);
    }

    void MenuManager::ClearInfo() {
        ShowInfo(RestingInfo());
    }

    void MenuManager::PersistToOdf() {
        if (!Config::options.writeOdfRules) return;
        OdfWriter::WriteAll(*StateManager::GetSingleton());
    }

    // ===== Frame tick =====

    void MenuManager::Tick() {
        if (!m_open) return;

        if (m_awaitingNodes) RetryPendingPreview();

        PollSlotFullness();

        if (m_repeatId.empty()) return;

        const auto now = Clock::now();
        const auto sincePress = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_repeatSince);
        const auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_repeatLast);

        // The first step already happened on press. Repeat only once the hold has
        // outlasted the delay, so a tap steps exactly one overlay.
        const bool due = m_repeating
            ? sinceLast.count() >= Config::options.presetRepeatIntervalMs
            : sincePress.count() >= Config::options.presetRepeatDelayMs;
        if (!due) return;

        m_repeating = true;
        m_repeatLast = now;
        StepPick(m_repeatId == kPickPrevId ? -1 : 1);
    }

    void MenuManager::BeginRepeat(const std::string& id) {
        m_repeatId = id;
        m_repeatSince = Clock::now();
        m_repeatLast = m_repeatSince;
        m_repeating = false;
    }

    void MenuManager::EndRepeat() {
        m_repeatId.clear();
        m_repeating = false;
    }

    // ===== Events =====

    bool MenuManager::OnEvent(const P3DUI::Event* event) {
        return GetSingleton()->HandleEvent(event);
    }

    bool MenuManager::HandleEvent(const P3DUI::Event* event) {
        if (!event || !event->sourceID || !m_open) return false;

        const std::string id(event->sourceID);
        const bool isStepper = id == kPickPrevId || id == kPickNextId;

        switch (event->type) {
            case P3DUI::EventType::ActivateDown: {
                // The step happens on press, not release, so a hold is one continuous
                // run rather than a run plus one more when the finger comes off.
                if (isStepper) {
                    BeginRepeat(id);
                    StepPick(id == kPickPrevId ? -1 : 1);
                    return true;
                }
                return false;
            }

            case P3DUI::EventType::ActivateUp: {
                if (isStepper) {
                    EndRepeat();
                    return true;
                }

                // Grip drags the handle to reposition the menu; trigger on it closes,
                // the same gesture VR Dress Up uses.
                if (id == kAnchorId) {
                    MenuRouter::GetSingleton()->CloseAll();
                    return true;
                }

                // The centre is how you put the overlay on show without stepping off it,
                // which is what a pack switch leaves you wanting.
                if (id == kPickIconId) {
                    ShowPick(true);
                    return true;
                }

                // Everything below rebuilds a row, and 3DUI is still walking the elements
                // Clear() destroys, so all of it waits a frame.
                if (id == kRandomId) {
                    NextFrameIfOpen([](MenuManager& menu) {
                        if (menu.PickIsApplied()) menu.RemovePick();
                        else menu.RandomPick();
                    });
                    return true;
                }
                if (id == kCommitId) {
                    NextFrameIfOpen([](MenuManager& menu) { menu.CommitPick(); });
                    return true;
                }
                if (const auto appliedIndex = ParseIndex(id, kAppliedPrefix);
                    appliedIndex != static_cast<size_t>(-1)) {
                    NextFrameIfOpen([appliedIndex](MenuManager& menu) { menu.OnAppliedActivated(appliedIndex); });
                    return true;
                }
                if (const auto packIndex = ParseIndex(id, kPackPrefix);
                    packIndex != static_cast<size_t>(-1)) {
                    NextFrameIfOpen([packIndex](MenuManager& menu) { menu.OnPackActivated(packIndex); });
                    return true;
                }
                if (const auto colorIndex = ParseIndex(id, kColorPrefix);
                    colorIndex != static_cast<size_t>(-1)) {
                    NextFrameIfOpen([colorIndex](MenuManager& menu) { menu.OnColorActivated(colorIndex); });
                    return true;
                }
                if (id == kUndoId) {
                    NextFrameIfOpen([](MenuManager& menu) { menu.OnUndo(); });
                    return true;
                }
                if (id == kRedoId) {
                    NextFrameIfOpen([](MenuManager& menu) { menu.OnRedo(); });
                    return true;
                }
                if (id == kClothesId || id == kTargetId ||
                    id == kColorsId || id == kClearId) {
                    NextFrameIfOpen([id](MenuManager& menu) { menu.OnToolActivated(id); });
                    return true;
                }
                return false;
            }

            // Nothing to do but claim the event: every element here carries its own
            // tooltip and 3DUI puts that up beside the hand by itself. The status line
            // used to echo it, which meant the same sentence in two places and a real
            // answer - "that slot is full" - wiped by drifting past an unrelated button.
            case P3DUI::EventType::HoverEnter:
                return true;

            case P3DUI::EventType::HoverExit: {
                // The hand slid off the chevron while still holding the trigger; carrying
                // on stepping something the player is no longer pointing at is worse than
                // stopping.
                if (isStepper && id == m_repeatId) EndRepeat();
                return true;
            }

            default:
                return false;
        }
    }
}
