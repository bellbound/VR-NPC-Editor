#include "menu/BodyMenuManager.h"

#include <algorithm>
#include <functional>
#include <vector>
#include <spdlog/spdlog.h>

#include "Config.h"
#include "FrameHook.h"
#include "health/HealthCheckManager.h"
#include "menu/EditSession.h"
#include "menu/MenuRouter.h"
#include "obody/ObodyBridge.h"
#include "tng/TngBridge.h"

namespace NPCEditor {
    namespace {
        constexpr const char* kModId = "VRNPCEditor";

        constexpr const char* kRootId       = "vrnpce_body_root";
        constexpr const char* kPresetRowId  = "vrnpce_body_presetrow";
        constexpr const char* kWeightRowId  = "vrnpce_body_weightrow";
        constexpr const char* kToolRowId    = "vrnpce_body_toolrow";
        constexpr const char* kPresetTextId = "vrnpce_body_presettext";
        constexpr const char* kWeightTextId = "vrnpce_body_weighttext";
        constexpr const char* kInfoId       = "vrnpce_body_info";

        constexpr const char* kPresetPrevId = "vrnpce_body_preset_prev";
        constexpr const char* kPresetIconId = "vrnpce_body_preset_icon";
        constexpr const char* kPresetNextId = "vrnpce_body_preset_next";

        constexpr const char* kWeightPrevId = "vrnpce_body_weight_prev";
        constexpr const char* kWeightIconId = "vrnpce_body_weight_icon";
        constexpr const char* kWeightNextId = "vrnpce_body_weight_next";

        constexpr const char* kAddonRowId   = "vrnpce_body_addonrow";
        constexpr const char* kAddonTextId  = "vrnpce_body_addontext";
        constexpr const char* kAddonPrevId  = "vrnpce_body_addon_prev";
        constexpr const char* kAddonIconId  = "vrnpce_body_addon_icon";
        constexpr const char* kAddonNextId  = "vrnpce_body_addon_next";

        constexpr const char* kSizeRowId    = "vrnpce_body_sizerow";
        constexpr const char* kSizeTextId   = "vrnpce_body_sizetext";
        constexpr const char* kSizePrevId   = "vrnpce_body_size_prev";
        constexpr const char* kSizeIconId   = "vrnpce_body_size_icon";
        constexpr const char* kSizeNextId   = "vrnpce_body_size_next";

        constexpr const char* kAnchorId   = "vrnpce_body_orb";
        constexpr const char* kUndoId     = "vrnpce_tool_undo";
        constexpr const char* kDoneId     = "vrnpce_tool_done";

        constexpr const char* kTexPreset   = "textures\\VRNPCEditor\\tpose.dds";
        constexpr const char* kTexPrev     = "textures\\VRNPCEditor\\chevron-left.dds";
        constexpr const char* kTexNext     = "textures\\VRNPCEditor\\chevron-right.dds";
        // Same vocabulary as the overlay menu, where cross discards and check commits.
        constexpr const char* kTexUndo     = "textures\\VRNPCEditor\\cross.dds";
        constexpr const char* kTexDone     = "textures\\VRNPCEditor\\check.dds";
        constexpr const char* kTexAddon    = "textures\\VRNPCEditor\\tng-addon.dds";

        // The five size categories fill a gauge the same way the five weight steps
        // do - see SizeTexture below.
        constexpr const char* kTexSize     = "textures\\VRNPCEditor\\tng-size.dds";
        constexpr const char* kAnchorModel = "meshes\\3DUI\\orb.nif";

        constexpr float kElementScale = 1.2f;

        // A row and the value written under it are one thing, so they sit closer together
        // than one group sits to the next. Only the first number tightened: shrinking
        // both would scale the whole menu down rather than group it more clearly.
        //
        // The group gap came down from 8.75 once the addon stepper made this a
        // four-row stack: the old spacing was set for three rows and left the taller
        // menu reading as loose bands rather than one panel. It stays one number for
        // every gap - the reason a row binds to its own label is that everything else
        // is further away, and tightening only the preset-to-weight gap would break
        // that by making one boundary look like an intra-group one.
        constexpr float kTextGap  = 4.2f;
        constexpr float kGroupGap = 6.5f;

        // The column layout, bottom to top: tools, the preset stepper and its name, the
        // weight stepper and its percentage, the TNG addon stepper and its name, and the
        // TNG size stepper on top. Each text sits kTextGap under the row it belongs to.
        constexpr float kToolRowZ    = 0.0f;
        constexpr float kInfoZ       = kToolRowZ - kGroupGap;
        constexpr float kPresetTextZ = kToolRowZ + kGroupGap;
        constexpr float kPresetRowZ  = kPresetTextZ + kTextGap;
        constexpr float kWeightTextZ = kPresetRowZ + kGroupGap;
        constexpr float kWeightRowZ  = kWeightTextZ + kTextGap;
        constexpr float kAddonTextZ  = kWeightRowZ + kGroupGap;
        constexpr float kAddonRowZ   = kAddonTextZ + kTextGap;
        constexpr float kSizeTextZ   = kAddonRowZ + kGroupGap;
        constexpr float kSizeRowZ    = kSizeTextZ + kTextGap;

        // The square layout: two steppers above the orb, two below, each pair a column
        // offset either side of it. The rows keep their own bottom-to-top shape - the
        // stepper above the line it is labelled by - so the bottom pair hangs below the
        // orb by one group gap and the top pair sits one group gap plus its own label
        // above it.
        constexpr float kGridTopRowZ     = kToolRowZ + kGroupGap + kTextGap;
        constexpr float kGridTopTextZ    = kToolRowZ + kGroupGap;
        constexpr float kGridBottomRowZ  = kToolRowZ - kGroupGap;
        constexpr float kGridBottomTextZ = kGridBottomRowZ - kTextGap;
        constexpr float kGridInfoZ       = kGridBottomTextZ - kGroupGap;

        // How far either column sits from the middle. A stepper is three elements at 8.0
        // spacing and so about 24 wide; at 20 out, the inner ends of the two columns are
        // a comfortable clear of the orb between them without the outer ends going where
        // an arm has to travel to reach them.
        constexpr float kGridColumnX = 20.0f;

        // How hard the menu is bent round the player - see Root::SetCurvature, which
        // judges a radius against the menu's own half-width and not its distance from the
        // head. Twice the half-width carries the edges through half a radian, which is
        // the clear curve the interface describes; the square layout is about 64 wide and
        // the column about 24, so each gets its own.
        //
        // Horizontal only, in both. Vertical curvature bends about the root's origin, and
        // this root's origin is the tool row rather than the middle of the stack - the orb
        // has to land on the hand - so a vertical bend would tip the whole menu forward
        // instead of bowing its top and bottom evenly.
        constexpr float kGridCurveRadius   = 64.0f;
        constexpr float kColumnCurveRadius = 24.0f;

        // 3DUI multiplies this by 1.25 internally, which is what VR Dress Up's info line
        // renders at. The old preset names rode along on the elements as label text,
        // which does not get that multiplier - that is why they read as too small.
        constexpr float kRowTextScale = 1.0f;

        constexpr int kWeightSteps = 5;   // 0, 25, 50, 75, 100

        const char* WeightTexture(int step) {
            switch (step) {
                case 0:  return "textures\\VRNPCEditor\\weight_0.dds";
                case 1:  return "textures\\VRNPCEditor\\weight_25.dds";
                case 2:  return "textures\\VRNPCEditor\\weight_50.dds";
                case 3:  return "textures\\VRNPCEditor\\weight_75.dds";
                default: return "textures\\VRNPCEditor\\weight_100.dds";
            }
        }

        // Same five-step gauge as the weight icon, and for the same reason: the number
        // that matters is which of five it is, and a bar filling up says that without
        // anyone having to read a word.
        const char* SizeTexture(int category) {
            switch (category) {
                case 0:  return "textures\\VRNPCEditor\\tng-size_0.dds";
                case 1:  return "textures\\VRNPCEditor\\tng-size_25.dds";
                case 2:  return "textures\\VRNPCEditor\\tng-size_50.dds";
                case 3:  return "textures\\VRNPCEditor\\tng-size_75.dds";
                case 4:  return "textures\\VRNPCEditor\\tng-size_100.dds";
                default: return kTexSize;
            }
        }

        int StepFromWeight(float weight) {
            const int step = static_cast<int>(std::lround(weight / 25.0f));
            return std::clamp(step, 0, kWeightSteps - 1);
        }

        std::wstring Widen(std::string_view text) {
            return std::wstring(text.begin(), text.end());
        }

        // Same reason as the overlay menu's: 3DUI spawns an element's projectile inside
        // CreateElement, before AddChild has laid it out, so anything created under a
        // visible row starts at the row's centre and slides outwards.
        void PopulateHidden(P3DUI::Positionable* container, const std::function<void()>& fill) {
            if (!container) return;
            container->SetVisible(false);
            fill();
            container->SetVisible(true);
        }
    }

    BodyMenuManager* BodyMenuManager::GetSingleton() {
        static BodyMenuManager instance;
        return &instance;
    }

    bool BodyMenuManager::Initialize() {
        if (IsInitialized()) return true;

        m_api = P3DUI::GetInterface001();
        if (!m_api) {
            spdlog::error("Body menu: 3DUI interface unavailable");
            return false;
        }

        if (!BuildMenu()) return false;

        // OBody goes unsafe across every save and load. Nothing here calls into it
        // without the bridge's readiness gate, so this is about not leaving a menu
        // floating over a world that is being torn down.
        Obody::SetUnreadyCallback([] {
            SKSE::GetTaskInterface()->AddTask([] {
                auto* menu = BodyMenuManager::GetSingleton();
                if (menu->IsOpen()) {
                    spdlog::info("Body menu: closing, OBody is no longer ready");
                    menu->Close();
                }
            });
        });

        return true;
    }

    bool BodyMenuManager::BuildMenu() {
        auto rootConfig = P3DUI::RootConfig::Default(kRootId, kModId);
        rootConfig.interactive = true;
        rootConfig.eventCallback = &BodyMenuManager::OnEvent;
        rootConfig.activationButtonMask = vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger);
        rootConfig.grabButtonMask = vr::ButtonMaskFromId(vr::k_EButton_Grip);

        m_root = m_api->GetOrCreateRoot(rootConfig);
        if (!m_root) {
            spdlog::error("Body menu: could not create the 3DUI root");
            return false;
        }

        // Every row is three elements wide and never scrolls. Where it goes is
        // LayoutRows' business: which rows there are decides that, and two of them are
        // not known about until the VM answers, frames from now.
        auto makeRow = [this](const char* id) -> P3DUI::ScrollableContainer* {
            auto config = P3DUI::ColumnGridConfig::Default(id);
            config.numRows = 1;
            config.columnSpacing = 8.0f;
            config.rowSpacing = 8.0f;
            config.visibleWidth = 1000.0f;

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

        // Built unconditionally, TNG installed or not: rows are created once here and
        // never destroyed, only their children refilled. Whether the TNG steppers are
        // offered is a question of visibility, decided per actor in their Populate*.
        m_sizeRow    = makeRow(kSizeRowId);
        m_sizeText   = makeText(kSizeTextId, kRowTextScale);
        m_addonRow   = makeRow(kAddonRowId);
        m_addonText  = makeText(kAddonTextId, kRowTextScale);
        m_weightRow  = makeRow(kWeightRowId);
        m_weightText = makeText(kWeightTextId, kRowTextScale);
        m_presetRow  = makeRow(kPresetRowId);
        m_presetText = makeText(kPresetTextId, kRowTextScale);
        m_toolRow    = makeRow(kToolRowId);
        m_infoText   = makeText(kInfoId, kRowTextScale);

        LayoutRows();
        m_root->SetVisible(false);
        spdlog::info("Body menu: built");
        return true;
    }

    bool BodyMenuManager::UseGridLayout() const {
        return !m_presets.empty() && Health::CanEditWeight(GetTargetActor()) && HasAddonRow() &&
               HasSizeRow();
    }

    void BodyMenuManager::LayoutRows() {
        const bool grid = UseGridLayout();

        // Only a change of layout actually moves anything, and only then is the dance
        // below worth its flicker.
        const bool moving = grid != m_gridLayout;

        auto place = [moving](P3DUI::Positionable* thing, float x, float z) {
            if (!thing) return;

            // A filled container that moves drags its contents after it: every element
            // under it is a projectile lerping toward wherever its parent has gone, so
            // the whole row visibly flies across the menu. Hidden, they are released and
            // rebound at the target when the row comes back - the same trick
            // PopulateHidden uses when a row is refilled.
            const bool wasVisible = moving && thing->IsVisible();
            if (wasVisible) thing->SetVisible(false);

            thing->SetLocalPosition(x, 0.0f, z);

            if (wasVisible) thing->SetVisible(true);
        };

        if (grid) {
            // Weight over preset on the left, addon over size on the right, the orb in
            // the middle of the four.
            place(m_weightRow,  -kGridColumnX, kGridTopRowZ);
            place(m_weightText, -kGridColumnX, kGridTopTextZ);
            place(m_addonRow,    kGridColumnX, kGridTopRowZ);
            place(m_addonText,   kGridColumnX, kGridTopTextZ);
            place(m_presetRow,  -kGridColumnX, kGridBottomRowZ);
            place(m_presetText, -kGridColumnX, kGridBottomTextZ);
            place(m_sizeRow,     kGridColumnX, kGridBottomRowZ);
            place(m_sizeText,    kGridColumnX, kGridBottomTextZ);
            place(m_toolRow,     0.0f,         kToolRowZ);
            place(m_infoText,    0.0f,         kGridInfoZ);
        } else {
            place(m_sizeRow,    0.0f, kSizeRowZ);
            place(m_sizeText,   0.0f, kSizeTextZ);
            place(m_addonRow,   0.0f, kAddonRowZ);
            place(m_addonText,  0.0f, kAddonTextZ);
            place(m_weightRow,  0.0f, kWeightRowZ);
            place(m_weightText, 0.0f, kWeightTextZ);
            place(m_presetRow,  0.0f, kPresetRowZ);
            place(m_presetText, 0.0f, kPresetTextZ);
            place(m_toolRow,    0.0f, kToolRowZ);
            place(m_infoText,   0.0f, kInfoZ);
        }

        // Bend the rows round the player like a curved monitor, the same treatment VR
        // Dress Up gives its wheel. Free as far as the layouts are concerned: 3DUI
        // applies it after they have laid out, so every spacing above is still the flat
        // number it was tuned as.
        m_root->SetCurvature(grid ? kGridCurveRadius : kColumnCurveRadius,
                             /*horizontal*/ true, /*vertical*/ false, /*tiltElements*/ true);

        if (moving) {
            spdlog::info("Body menu: laid out as {}", grid ? "a square about the orb"
                                                           : "a single column");
        }
        m_gridLayout = grid;
    }

    RE::Actor* BodyMenuManager::GetTargetActor() const {
        auto actor = m_npcHandle.get();
        return actor ? actor.get() : nullptr;
    }

    void BodyMenuManager::OpenForActor(RE::Actor* actor, bool isLeftHand) {
        if (!IsInitialized() || !actor) return;

        m_npcHandle = actor->GetHandle();
        m_open = true;

        spdlog::info("Body menu: opening for {:08X} ({})", actor->GetFormID(), actor->GetName());

        // A never-seen NPC has no assignment yet, so the stepper would not know where to
        // start.
        Obody::EnsureProcessed(actor);

        SyncWeightStep();

        // Asked for now so the answers have the whole open animation to arrive in. The
        // row is not built here - it appears from Tick once the VM has replied.
        PrimeAddon();

        // A switch leaves the previous menu's position behind as a local offset. An
        // open from the actor menu is meant to land on the hand, so it starts clean.
        m_root->SetLocalPosition(0.0f, 0.0f, 0.0f);
        m_root->SetVRAnchor(P3DUI::VRAnchorType::HMD);
        m_root->ShowAtHand(isLeftHand);

        RefreshAll();

        FrameHook::GetSingleton()->Register(this, [] { GetSingleton()->Tick(); });
    }

    void BodyMenuManager::Close() {
        if (!m_open) return;

        m_open = false;
        EndRepeat();

        FrameHook::GetSingleton()->Unregister(this);

        // A weight change that was still waiting out its debounce would otherwise be
        // written to the base record and never applied to the model.
        if (m_weightPending) CommitWeight();

        // Same for the addon and the size: the label showed the new one, so closing
        // without writing it would silently discard a change the player watched
        // themselves make.
        if (m_tngPending) CommitAddon();
        if (m_tngSizePending) CommitSize();

        if (m_root) {
            m_root->EndPositioning();
            m_root->SetVisible(false);
        }
        spdlog::debug("Body menu: closed");
    }

    void BodyMenuManager::SyncWeightStep() {
        auto* actor = GetTargetActor();
        auto* base = actor ? actor->GetActorBase() : nullptr;

        m_weightStep = StepFromWeight(base ? base->weight : 0.0f);
        m_weightPending = false;
    }

    void BodyMenuManager::RefreshAll() {
        auto* actor = GetTargetActor();
        if (!actor) return;

        m_presets = Obody::GetPresetNames(actor);

        // Open on the body the actor already has, so the first press steps away from
        // what is on screen rather than jumping somewhere unrelated.
        const auto assigned = Obody::GetAssignedPreset(actor);
        m_presetIndex = m_presets.empty() ? static_cast<size_t>(-1) : 0;
        for (size_t i = 0; i < m_presets.size(); ++i) {
            if (m_presets[i] == assigned) {
                m_presetIndex = i;
                break;
            }
        }

        // Before the rows are filled, so a row that has to move does it while empty.
        LayoutRows();

        PopulateSizeRow();
        PopulateAddonRow();
        PopulateWeightRow();
        PopulatePresetRow();
        PopulateToolRow();

        UpdatePresetText();
        UpdateWeightText();
        UpdateAddonText();
        UpdateSizeText();

        if (m_presets.empty()) {
            ShowInfo(Obody::IsReady() ? L"No BodySlide presets for this actor"
                                      : L"OBody is busy, try again in a moment");
        } else {
            ClearInfo();
        }
    }

    // ===== Rows =====

    void BodyMenuManager::PopulatePresetRow() {
        if (!m_presetRow) return;

        // Cleared before the pointer is dropped, not after: Clear() destroys the element
        // it points at, and the refill below is not guaranteed to run.
        m_presetIcon = nullptr;
        m_presetRow->Clear();

        // Nothing to step through, so the row and its name line would only be three dead
        // icons and an empty string.
        if (m_presets.empty()) {
            m_presetRow->SetVisible(false);
            if (m_presetText) m_presetText->SetVisible(false);
            return;
        }

        PopulateHidden(m_presetRow, [this] {
            const auto tooltip = PresetTooltip();

            auto add = [this](const char* id, const char* texture, const wchar_t* tooltip) -> P3DUI::Element* {
                auto config = P3DUI::ElementConfig::Default(id);
                config.texturePath = texture;
                config.tooltip = tooltip;
                config.scale = kElementScale;
                config.facingMode = P3DUI::FacingMode::None;

                auto* element = m_api->CreateElement(config);
                if (element) m_presetRow->AddChild(element);
                return element;
            };

            add(kPresetPrevId, kTexPrev, L"Previous preset - hold to run through them");
            m_presetIcon = add(kPresetIconId, kTexPreset, tooltip.c_str());
            add(kPresetNextId, kTexNext, L"Next preset - hold to run through them");
        });
    }

    void BodyMenuManager::PopulateWeightRow() {
        if (!m_weightRow) return;

        m_weightIcon = nullptr;
        m_weightRow->Clear();

        // Weight lives on the base record, so generic actors would share any change.
        if (!Health::CanEditWeight(GetTargetActor())) {
            m_weightRow->SetVisible(false);
            if (m_weightText) m_weightText->SetVisible(false);
            return;
        }

        PopulateHidden(m_weightRow, [this] {
            auto add = [this](const char* id, const char* texture, const wchar_t* tooltip) -> P3DUI::Element* {
                auto config = P3DUI::ElementConfig::Default(id);
                config.texturePath = texture;
                config.tooltip = tooltip;
                config.scale = kElementScale;
                config.facingMode = P3DUI::FacingMode::None;

                auto* element = m_api->CreateElement(config);
                if (element) m_weightRow->AddChild(element);
                return element;
            };

            add(kWeightPrevId, kTexPrev, L"Lighter");
            m_weightIcon = add(kWeightIconId, WeightTexture(m_weightStep), L"Weight - tap to cycle");
            add(kWeightNextId, kTexNext, L"Heavier");
        });
    }

    // Whether this actor has an addon row at all.
    //
    // Two entries means TNG offered nothing but its own "default" and "no genital"
    // pseudo-options, which is TNG's way of saying no addon fits this actor. A row
    // built from that would be three icons that only ever toggle between nothing and
    // nothing.
    bool BodyMenuManager::HasAddonRow() const {
        return m_tngEntries.size() > 2;
    }

    void BodyMenuManager::PopulateAddonRow() {
        if (!m_addonRow) return;

        // Same ordering as the other rows: Clear() destroys the element this points at.
        m_addonIcon = nullptr;
        m_addonRow->Clear();

        if (!HasAddonRow()) {
            m_addonRow->SetVisible(false);
            if (m_addonText) m_addonText->SetVisible(false);
            return;
        }

        PopulateHidden(m_addonRow, [this] {
            auto add = [this](const char* id, const char* texture, const wchar_t* tooltip) -> P3DUI::Element* {
                auto config = P3DUI::ElementConfig::Default(id);
                config.texturePath = texture;
                config.tooltip = tooltip;
                config.scale = kElementScale;
                config.facingMode = P3DUI::FacingMode::None;

                auto* element = m_api->CreateElement(config);
                if (element) m_addonRow->AddChild(element);
                return element;
            };

            add(kAddonPrevId, kTexPrev, L"Previous addon - hold to run through them");
            m_addonIcon = add(kAddonIconId, kTexAddon, L"Addon - tap to reset to default");
            add(kAddonNextId, kTexNext, L"Next addon - hold to run through them");
        });
    }

    // Offered exactly when the addon stepper is, and only once TNG has said what size
    // the actor is: a stepper that opens on a made-up value writes that value to the
    // actor the moment it is touched.
    bool BodyMenuManager::HasSizeRow() const {
        return HasAddonRow() && m_tngSize >= 0;
    }

    void BodyMenuManager::PopulateSizeRow() {
        if (!m_sizeRow) return;

        m_sizeIcon = nullptr;
        m_sizeRow->Clear();

        if (!HasSizeRow()) {
            m_sizeRow->SetVisible(false);
            if (m_sizeText) m_sizeText->SetVisible(false);
            return;
        }

        PopulateHidden(m_sizeRow, [this] {
            auto add = [this](const char* id, const char* texture, const wchar_t* tooltip) -> P3DUI::Element* {
                auto config = P3DUI::ElementConfig::Default(id);
                config.texturePath = texture;
                config.tooltip = tooltip;
                config.scale = kElementScale;
                config.facingMode = P3DUI::FacingMode::None;

                auto* element = m_api->CreateElement(config);
                if (element) m_sizeRow->AddChild(element);
                return element;
            };

            add(kSizePrevId, kTexPrev, L"Smaller");
            m_sizeIcon = add(kSizeIconId, SizeTexture(m_tngSize), L"Size - tap to cycle");
            add(kSizeNextId, kTexNext, L"Bigger");
        });
    }

    void BodyMenuManager::PopulateToolRow() {
        if (!m_toolRow) return;

        m_toolRow->Clear();

        PopulateHidden(m_toolRow, [this] {
            auto add = [this](const char* id, const char* texture, const wchar_t* tooltip) {
                auto config = P3DUI::ElementConfig::Default(id);
                config.texturePath = texture;
                config.tooltip = tooltip;
                config.scale = kElementScale;
                config.facingMode = P3DUI::FacingMode::None;
                if (auto* element = m_api->CreateElement(config)) m_toolRow->AddChild(element);
            };

            // Discard and keep bracket the orb, one either side, so the row stays
            // symmetric about its handle rather than growing off one end. Both appear
            // and disappear together - a keep button next to nothing to discard would
            // only duplicate what the orb already does.
            const bool hasChanges = EditSession::GetSingleton()->HasChanges();

            if (hasChanges) {
                add(kUndoId, kTexUndo, L"Discard everything since you opened this NPC");
            }

            // Centre handle: grip drags the menu, trigger closes it.
            auto orbConfig = P3DUI::ElementConfig::Default(kAnchorId);
            orbConfig.modelPath = kAnchorModel;
            orbConfig.tooltip = L"Move or close";
            orbConfig.scale = kElementScale;
            orbConfig.isAnchorHandle = true;
            orbConfig.facingMode = P3DUI::FacingMode::None;
            if (auto* orb = m_api->CreateElement(orbConfig)) m_toolRow->AddChild(orb);

            if (hasChanges) {
                // Closes, nothing more: every change is already applied to the actor by
                // the time it can be undone, so there is nothing left here to commit.
                add(kDoneId, kTexDone, L"Keep these changes and close");
            }
        });
    }

    // ===== Frame tick =====

    void BodyMenuManager::Tick() {
        if (!m_open) return;

        // Out here rather than in the event that set the flag: a rebuild inside 3DUI's
        // dispatch destroys the elements it is still walking.
        if (m_toolRowDirty) {
            m_toolRowDirty = false;
            PopulateToolRow();
        }

        // The one piece of this menu that cannot answer synchronously. Polled here
        // rather than waited on anywhere: blocking the game thread for a VM answer
        // deadlocks the VM, because the game thread is what pumps it.
        PollAddon();

        const auto now = Clock::now();

        if (!m_repeatId.empty()) {
            const auto sincePress = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_repeatSince);
            const auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_repeatLast);

            // The first step already happened on press. Repeat only once the hold has
            // outlasted the delay, so a tap steps exactly one preset.
            const bool due = m_repeating
                ? sinceLast.count() >= Config::options.presetRepeatIntervalMs
                : sincePress.count() >= Config::options.presetRepeatDelayMs;

            if (due) {
                m_repeating = true;
                m_repeatLast = now;

                if (m_repeatId == kPresetPrevId)      StepPreset(-1);
                else if (m_repeatId == kPresetNextId) StepPreset(1);
                else if (m_repeatId == kWeightPrevId) StepWeight(-1);
                else if (m_repeatId == kWeightNextId) StepWeight(1);
                else if (m_repeatId == kAddonPrevId)  StepAddon(-1);
                else if (m_repeatId == kAddonNextId)  StepAddon(1);
                else if (m_repeatId == kSizePrevId)   StepSize(-1);
                else if (m_repeatId == kSizeNextId)   StepSize(1);
            }
        }

        if (m_weightPending) {
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_weightRequestedAt);
            if (waited.count() >= Config::options.weightResetDebounceMs) {
                CommitWeight();
            }
        }

        if (m_tngPending) {
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_tngRequestedAt);
            if (waited.count() >= Config::options.tngApplyDebounceMs) {
                CommitAddon();
            }
        }

        if (m_tngSizePending) {
            const auto waited =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_tngSizeRequestedAt);
            if (waited.count() >= Config::options.tngApplyDebounceMs) {
                CommitSize();
            }
        }
    }

    void BodyMenuManager::BeginRepeat(const std::string& id) {
        m_repeatId = id;
        m_repeatSince = Clock::now();
        m_repeatLast = m_repeatSince;
        m_repeating = false;
    }

    void BodyMenuManager::EndRepeat() {
        m_repeatId.clear();
        m_repeating = false;
    }

    // ===== Presets =====

    std::wstring BodyMenuManager::PresetTooltip() const {
        if (m_presets.empty() || m_presetIndex >= m_presets.size()) return L"No presets";

        return Widen(m_presets[m_presetIndex]) + L" (" + std::to_wstring(m_presetIndex + 1) +
               L"/" + std::to_wstring(m_presets.size()) + L")";
    }

    void BodyMenuManager::UpdatePresetText() {
        if (!m_presetText) return;

        if (m_presets.empty() || m_presetIndex >= m_presets.size()) {
            m_presetText->SetVisible(false);
            return;
        }

        m_presetText->SetText(Widen(m_presets[m_presetIndex]).c_str());
        m_presetText->SetVisible(true);

        // The count belongs on the icon, where the hand already is.
        if (m_presetIcon) m_presetIcon->SetTooltip(PresetTooltip().c_str());
    }

    void BodyMenuManager::StepPreset(int delta) {
        if (m_presets.empty()) return;

        const size_t count = m_presets.size();
        const size_t current = m_presetIndex < count ? m_presetIndex : 0;

        // Wraps, so a held chevron walks the whole list without dead-ending.
        m_presetIndex = (current + static_cast<size_t>(delta) + count) % count;

        ApplyCurrentPreset(m_repeating ? "hold" : "step");
        UpdatePresetText();
    }

    void BodyMenuManager::ApplyCurrentPreset(const char* reason) {
        if (m_presetIndex >= m_presets.size()) return;

        auto* actor = GetTargetActor();
        if (!actor) return;

        if (!Obody::ApplyPreset(actor, m_presets[m_presetIndex])) {
            ShowInfo(L"OBody would not apply that preset");
            return;
        }

        spdlog::debug("Body menu: preset '{}' applied to '{}' ({})",
                      m_presets[m_presetIndex], actor->GetName(), reason);

        ClearInfo();

        // Only when the undo button has to appear - rebuilding the tool row on every step
        // would churn elements for nothing.
        const bool hadChanges = EditSession::GetSingleton()->HasChanges();
        EditSession::GetSingleton()->NoteChange("preset");
        if (!hadChanges) m_toolRowDirty = true;
    }

    // ===== Weight =====

    void BodyMenuManager::UpdateWeightText() {
        if (!m_weightText) return;

        if (!Health::CanEditWeight(GetTargetActor())) {
            m_weightText->SetVisible(false);
            return;
        }

        m_weightText->SetText((std::to_wstring(m_weightStep * 25) + L"% weight").c_str());
        m_weightText->SetVisible(true);

        if (m_weightIcon) m_weightIcon->SetTexture(WeightTexture(m_weightStep));
    }

    void BodyMenuManager::StepWeight(int delta) {
        auto* actor = GetTargetActor();
        if (!Health::CanEditWeight(actor)) return;

        auto* base = actor->GetActorBase();
        if (!base) return;

        m_weightStep = (m_weightStep + delta + kWeightSteps) % kWeightSteps;
        const float newWeight = static_cast<float>(m_weightStep) * 25.0f;

        spdlog::debug("Body menu: weight {:.0f} -> {:.0f} on '{}'", base->weight, newWeight, actor->GetName());
        base->weight = newWeight;

        // The visible refresh is deferred: DoReset3D re-equips everything the actor
        // wears, so a burst of steps must cost one reset, not one per step.
        m_weightPending = true;
        m_weightRequestedAt = Clock::now();

        const bool hadChanges = EditSession::GetSingleton()->HasChanges();
        EditSession::GetSingleton()->NoteChange("weight");
        if (!hadChanges) m_toolRowDirty = true;

        UpdateWeightText();
    }

    void BodyMenuManager::CommitWeight() {
        m_weightPending = false;

        auto* actor = GetTargetActor();
        if (!actor) return;

        auto* base = actor->GetActorBase();
        spdlog::info("Body menu: applying weight {:.0f} to '{}'", base ? base->weight : -1.0f, actor->GetName());

        actor->DoReset3D(true);
        // The reset rebuilds the model from the base record, dropping OBody's morphs.
        Obody::ReapplyMorphs(actor);
    }

    // ===== TNG addon =====
    //
    // The only asynchronous corner of this menu. TNG ships no C++ interface, so its
    // addon list has to be asked for through the Papyrus VM and collected later.

    void BodyMenuManager::PrimeAddon() {
        m_tngEntries.clear();
        m_tngIndex = 0;
        m_tngInitialIndex = 0;
        m_tngPending = false;
        m_tngSize = -1;
        m_tngInitialSize = -1;
        m_tngSizePending = false;

        if (!Health::IsFeatureAvailable(Health::Feature::Tng)) {
            // Nothing will ever arrive, so the poll must not sit waiting for it.
            m_tngResolved = true;
            return;
        }

        // Bumped on every open, so an answer that was in flight for the previous NPC
        // is recognisable as stale when it lands.
        ++m_tngGeneration;
        m_tngResolved = false;
        m_tngPrimedAt = Clock::now();

        Tng::PrimeActor(GetTargetActor(), m_tngGeneration);
    }

    void BodyMenuManager::PollAddon() {
        if (m_tngResolved) return;

        if (auto state = Tng::Collect(m_tngGeneration)) {
            m_tngResolved = true;

            // A race TNG will not modify gets no row, whatever it listed.
            m_tngEntries = state->modifiable ? std::move(state->entries)
                                             : std::vector<std::string>{};
            m_tngIndex = state->index;
            m_tngInitialIndex = m_tngIndex;

            // -1 stands for "TNG would not say", which is what leaves the size row
            // hidden while the addon row is up.
            m_tngSize = state->size.value_or(-1);
            m_tngInitialSize = m_tngSize;

            // Info, not debug, and it names both gates: a hidden row is otherwise
            // indistinguishable from a broken one, and the default log level is info -
            // so a debug line here is a diagnostic nobody testing this can actually
            // read. Once per menu open, so it is not chatter.
            const std::string canModify =
                state->rawCanModify ? std::to_string(*state->rawCanModify)
                                    : std::string("not an Int");

            if (HasAddonRow()) {
                spdlog::info("Body menu: TNG offers {} addon(s) (CanModifyActor={}), starting on "
                             "entry {} of {}",
                             m_tngEntries.size() - 2, canModify, m_tngIndex + 1,
                             m_tngEntries.size());
            } else if (!state->modifiable) {
                spdlog::info("Body menu: no TNG row - CanModifyActor={}, so TNG will not change "
                             "this actor's addon", canModify);
            } else {
                // Two entries means TNG returned its pseudo-options and nothing else.
                // Worth distinguishing from an empty array, which would instead mean
                // the string-array read came back with nothing at all.
                spdlog::info("Body menu: no TNG row - CanModifyActor={} but TNG listed {} "
                             "entries, so no addon fits this actor's race group and sex",
                             canModify, m_tngEntries.size());
            }

            // Out here rather than in the VM callback that produced it: building a row
            // is a 3DUI call, and 3DUI is main-thread only. The layout goes first:
            // these two rows arriving is what decides between the square and the column.
            LayoutRows();
            PopulateAddonRow();
            PopulateSizeRow();
            UpdateAddonText();
            UpdateSizeText();
            return;
        }

        const auto waited =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_tngPrimedAt);
        if (waited.count() >= Config::options.tngPrimeTimeoutMs) {
            // There is no timeout on a Papyrus call itself - a VM that never answers
            // simply never answers - so the deadline has to live here. Giving up leaves
            // the row hidden, which is the same as TNG having nothing to offer.
            m_tngResolved = true;
            spdlog::warn("Body menu: TNG did not answer within {}ms, addon row stays hidden",
                         Config::options.tngPrimeTimeoutMs);
        }
    }

    std::wstring BodyMenuManager::AddonTooltip() const {
        if (!HasAddonRow()) return L"No addons";

        return Tng::EntryLabel(m_tngEntries, m_tngIndex) + L" (" +
               std::to_wstring(m_tngIndex + 1) + L"/" + std::to_wstring(m_tngEntries.size()) +
               L") - tap to reset";
    }

    void BodyMenuManager::UpdateAddonText() {
        if (!m_addonText) return;

        if (!HasAddonRow()) {
            m_addonText->SetVisible(false);
            return;
        }

        m_addonText->SetText(Tng::EntryLabel(m_tngEntries, m_tngIndex).c_str());
        m_addonText->SetVisible(true);

        // The position in the list belongs on the icon, where the hand already is.
        if (m_addonIcon) m_addonIcon->SetTooltip(AddonTooltip().c_str());
    }

    void BodyMenuManager::StepAddon(int delta) {
        if (!HasAddonRow()) return;

        const int count = static_cast<int>(m_tngEntries.size());
        // Wraps, so a held chevron walks the whole list without dead-ending.
        m_tngIndex = (m_tngIndex + delta + count) % count;

        // Deferred like the weight commit: every set swaps the actor's skin, so
        // stepping through a long list must cost one swap rather than one per step.
        m_tngPending = true;
        m_tngRequestedAt = Clock::now();

        const bool hadChanges = EditSession::GetSingleton()->HasChanges();
        EditSession::GetSingleton()->NoteChange("tng");
        if (!hadChanges) m_toolRowDirty = true;

        UpdateAddonText();
    }

    void BodyMenuManager::ResetAddon() {
        if (!HasAddonRow()) return;
        if (m_tngIndex == 0) return;  // already on Default

        m_tngIndex = 0;
        m_tngPending = true;
        m_tngRequestedAt = Clock::now();

        const bool hadChanges = EditSession::GetSingleton()->HasChanges();
        EditSession::GetSingleton()->NoteChange("tng");
        if (!hadChanges) m_toolRowDirty = true;

        UpdateAddonText();
    }

    void BodyMenuManager::CommitAddon() {
        m_tngPending = false;

        auto* actor = GetTargetActor();
        if (!actor || !HasAddonRow()) return;

        // TNG's list leads with two pseudo-entries, so index 2 is its addon 0.
        int choice = m_tngIndex - 2;

        // TNG keeps the player's default apart from everyone else's and has its own
        // sentinel for it. The body menu only ever opens on a grabbed NPC today, so
        // this is defensive rather than reachable - but the two are not interchangeable
        // and picking the wrong one here would be silent.
        if (choice == -2 && actor->IsPlayerRef()) choice = -3;

        spdlog::info("Body menu: applying TNG addon '{}' (choice {}) to '{}'",
                     m_tngEntries[static_cast<size_t>(m_tngIndex)], choice, actor->GetName());

        Tng::SetAddon(actor, choice);
    }

    // ===== TNG size =====

    std::wstring BodyMenuManager::SizeTooltip() const {
        if (!HasSizeRow()) return L"No size";

        return std::wstring(Tng::SizeLabel(m_tngSize)) + L" (" + std::to_wstring(m_tngSize + 1) +
               L"/" + std::to_wstring(Tng::kSizeCategories) + L")";
    }

    void BodyMenuManager::UpdateSizeText() {
        if (!m_sizeText) return;

        if (!HasSizeRow()) {
            m_sizeText->SetVisible(false);
            return;
        }

        m_sizeText->SetText(Tng::SizeLabel(m_tngSize));
        m_sizeText->SetVisible(true);

        if (m_sizeIcon) {
            m_sizeIcon->SetTexture(SizeTexture(m_tngSize));
            m_sizeIcon->SetTooltip(SizeTooltip().c_str());
        }
    }

    void BodyMenuManager::StepSize(int delta) {
        if (!HasSizeRow()) return;

        // Wraps, so a held chevron walks the five without dead-ending.
        m_tngSize = (m_tngSize + delta + Tng::kSizeCategories) % Tng::kSizeCategories;

        // Deferred like the addon and the weight: a size change rescales the actor, and
        // a held chevron must cost one rescale rather than one per step.
        m_tngSizePending = true;
        m_tngSizeRequestedAt = Clock::now();

        const bool hadChanges = EditSession::GetSingleton()->HasChanges();
        EditSession::GetSingleton()->NoteChange("tng size");
        if (!hadChanges) m_toolRowDirty = true;

        UpdateSizeText();
    }

    void BodyMenuManager::CommitSize() {
        m_tngSizePending = false;

        auto* actor = GetTargetActor();
        if (!actor || !HasSizeRow()) return;

        spdlog::info("Body menu: applying TNG size category {} of {} to '{}'", m_tngSize + 1,
                     Tng::kSizeCategories, actor->GetName());
        Tng::SetSize(actor, m_tngSize);
    }

    // ===== Tools =====

    void BodyMenuManager::UndoChanges() {
        // The session owns the snapshot, so this reverts the overlay side too - the two
        // editors are one sitting as far as the player is concerned.
        EditSession::GetSingleton()->Undo();

        // The addon is not in the session snapshot: EditSession::Begin is synchronous
        // and TNG's state is not, so making it wait for a VM answer would hold up every
        // menu open for a mod most actors do not even use. Reverting it here keeps the
        // button honest about covering everything since the NPC was opened.
        if (HasAddonRow() && m_tngIndex != m_tngInitialIndex) {
            m_tngIndex = m_tngInitialIndex;
            CommitAddon();
        }
        if (HasSizeRow() && m_tngSize != m_tngInitialSize) {
            m_tngSize = m_tngInitialSize;
            CommitSize();
        }

        SyncWeightStep();
        RefreshAll();
    }

    void BodyMenuManager::OnToolActivated(const std::string& id) {
        if (id == kUndoId) UndoChanges();
    }

    // ===== Text =====

    void BodyMenuManager::ShowInfo(const std::wstring& text) {
        if (!m_infoText) return;
        m_infoText->SetText(text.c_str());
        m_infoText->SetVisible(true);
    }

    void BodyMenuManager::ClearInfo() {
        if (m_infoText) m_infoText->SetVisible(false);
    }

    // ===== Events =====

    bool BodyMenuManager::OnEvent(const P3DUI::Event* event) {
        return GetSingleton()->HandleEvent(event);
    }

    bool BodyMenuManager::HandleEvent(const P3DUI::Event* event) {
        if (!event || !event->sourceID || !m_open) return false;

        const std::string id(event->sourceID);

        const bool isStepper = id == kPresetPrevId || id == kPresetNextId ||
                               id == kWeightPrevId || id == kWeightNextId ||
                               id == kAddonPrevId  || id == kAddonNextId  ||
                               id == kSizePrevId   || id == kSizeNextId;

        switch (event->type) {
            case P3DUI::EventType::ActivateDown: {
                // The step happens on press, not release, so a hold is one continuous
                // run rather than a run plus one more when the finger comes off.
                if (isStepper) {
                    BeginRepeat(id);

                    if (id == kPresetPrevId)      StepPreset(-1);
                    else if (id == kPresetNextId) StepPreset(1);
                    else if (id == kWeightPrevId) StepWeight(-1);
                    else if (id == kWeightNextId) StepWeight(1);
                    else if (id == kAddonPrevId)  StepAddon(-1);
                    else if (id == kAddonNextId)  StepAddon(1);
                    else if (id == kSizePrevId)   StepSize(-1);
                    else if (id == kSizeNextId)   StepSize(1);
                    return true;
                }
                if (id == kWeightIconId) {
                    StepWeight(1);
                    return true;
                }
                // Safe inline for the same reason as the weight icon: it moves an index
                // and rewrites a label, and destroys nothing 3DUI is walking.
                if (id == kSizeIconId) {
                    StepSize(1);
                    return true;
                }
                // Safe to run inline, unlike the tool buttons: it moves an index and
                // rewrites a label and a tooltip, and destroys nothing 3DUI is walking.
                if (id == kAddonIconId) {
                    ResetAddon();
                    return true;
                }
                return false;
            }

            case P3DUI::EventType::ActivateUp: {
                if (isStepper) {
                    EndRepeat();
                    return true;
                }
                if (id == kWeightIconId || id == kPresetIconId || id == kAddonIconId ||
                    id == kSizeIconId) {
                    return true;
                }

                // Same as the orb, and safe to do inline for the same reason: closing
                // tears the menu down wholesale rather than rebuilding a row underneath
                // the walk that is still in progress.
                if (id == kAnchorId || id == kDoneId) {
                    MenuRouter::GetSingleton()->CloseAll();
                    return true;
                }

                // Deferred a frame: this rebuilds the tool row, and 3DUI is still
                // walking the elements the rebuild destroys.
                if (id == kUndoId) {
                    SKSE::GetTaskInterface()->AddTask([id] {
                        BodyMenuManager::GetSingleton()->OnToolActivated(id);
                    });
                    return true;
                }
                return false;
            }

            case P3DUI::EventType::HoverExit: {
                // The hand slid off the chevron while still holding the trigger; carrying
                // on stepping something the player is no longer pointing at is worse than
                // stopping.
                if (isStepper && id == m_repeatId) EndRepeat();
                return isStepper;
            }

            case P3DUI::EventType::HoverEnter:
                return isStepper || id == kPresetIconId || id == kWeightIconId ||
                       id == kAddonIconId || id == kSizeIconId || id == kAnchorId ||
                       id == kUndoId || id == kDoneId;

            default:
                return false;
        }
    }
}
