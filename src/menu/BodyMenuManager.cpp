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

        constexpr const char* kAnchorId   = "vrnpce_body_orb";
        constexpr const char* kUndoId     = "vrnpce_tool_undo";

        constexpr const char* kTexPreset   = "textures\\VRNPCEditor\\tpose.dds";
        constexpr const char* kTexPrev     = "textures\\VRNPCEditor\\chevron-left.dds";
        constexpr const char* kTexNext     = "textures\\VRNPCEditor\\chevron-right.dds";
        constexpr const char* kTexUndo     = "textures\\VRNPCEditor\\rewind.dds";
        constexpr const char* kAnchorModel = "meshes\\3DUI\\orb.nif";

        constexpr float kElementScale = 1.2f;

        // A row and the value written under it are one thing, so they sit closer together
        // than one group sits to the next. Only the first number tightened: shrinking
        // both would scale the whole menu down rather than group it more clearly.
        constexpr float kTextGap  = 4.2f;
        constexpr float kGroupGap = 8.75f;

        // Bottom to top: tools, the preset stepper and its name, the weight stepper and
        // its percentage. Each text sits kTextGap under the row it belongs to.
        constexpr float kToolRowZ    = 0.0f;
        constexpr float kInfoZ       = kToolRowZ - kGroupGap;
        constexpr float kPresetTextZ = kToolRowZ + kGroupGap;
        constexpr float kPresetRowZ  = kPresetTextZ + kTextGap;
        constexpr float kWeightTextZ = kPresetRowZ + kGroupGap;
        constexpr float kWeightRowZ  = kWeightTextZ + kTextGap;

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

        // Every row is three elements wide and never scrolls.
        auto makeRow = [this](const char* id, float z) -> P3DUI::ScrollableContainer* {
            auto config = P3DUI::ColumnGridConfig::Default(id);
            config.numRows = 1;
            config.columnSpacing = 8.0f;
            config.rowSpacing = 8.0f;
            config.visibleWidth = 1000.0f;

            auto* row = m_api->CreateColumnGrid(config);
            if (!row) return nullptr;

            m_root->AddChild(row);
            row->SetLocalPosition(0.0f, 0.0f, z);
            row->SetFillDirection(P3DUI::VerticalFill::TopToBottom, P3DUI::HorizontalFill::LeftToRight);
            row->SetOrigin(P3DUI::VerticalOrigin::Center, P3DUI::HorizontalOrigin::Center);
            return row;
        };

        auto makeText = [this](const char* id, float z, float scale) -> P3DUI::Text* {
            auto config = P3DUI::TextConfig::Default(id);
            config.facingMode = P3DUI::FacingMode::YawOnly;
            config.scale = scale;

            auto* text = m_api->CreateText(config);
            if (!text) return nullptr;

            m_root->AddChild(text);
            text->SetLocalPosition(0.0f, 0.0f, z);
            text->SetVisible(false);
            return text;
        };

        m_weightRow  = makeRow(kWeightRowId, kWeightRowZ);
        m_weightText = makeText(kWeightTextId, kWeightTextZ, kRowTextScale);
        m_presetRow  = makeRow(kPresetRowId, kPresetRowZ);
        m_presetText = makeText(kPresetTextId, kPresetTextZ, kRowTextScale);
        m_toolRow    = makeRow(kToolRowId, kToolRowZ);
        m_infoText   = makeText(kInfoId, kInfoZ, kRowTextScale);

        m_root->SetVisible(false);
        spdlog::info("Body menu: built");
        return true;
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

        PopulateWeightRow();
        PopulatePresetRow();
        PopulateToolRow();

        UpdatePresetText();
        UpdateWeightText();

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

            // Centre handle: grip drags the menu, trigger closes it.
            auto orbConfig = P3DUI::ElementConfig::Default(kAnchorId);
            orbConfig.modelPath = kAnchorModel;
            orbConfig.tooltip = L"Move or close";
            orbConfig.scale = kElementScale;
            orbConfig.isAnchorHandle = true;
            orbConfig.facingMode = P3DUI::FacingMode::None;
            if (auto* orb = m_api->CreateElement(orbConfig)) m_toolRow->AddChild(orb);

            if (EditSession::GetSingleton()->HasChanges()) {
                add(kUndoId, kTexUndo, L"Undo everything since you opened this NPC");
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
            }
        }

        if (m_weightPending) {
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_weightRequestedAt);
            if (waited.count() >= Config::options.weightResetDebounceMs) {
                CommitWeight();
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

    // ===== Tools =====

    void BodyMenuManager::UndoChanges() {
        // The session owns the snapshot, so this reverts the overlay side too - the two
        // editors are one sitting as far as the player is concerned.
        EditSession::GetSingleton()->Undo();

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
                               id == kWeightPrevId || id == kWeightNextId;

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
                    return true;
                }
                if (id == kWeightIconId) {
                    StepWeight(1);
                    return true;
                }
                return false;
            }

            case P3DUI::EventType::ActivateUp: {
                if (isStepper) {
                    EndRepeat();
                    return true;
                }
                if (id == kWeightIconId || id == kPresetIconId) return true;

                if (id == kAnchorId) {
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
                       id == kAnchorId || id == kUndoId;

            default:
                return false;
        }
    }
}
