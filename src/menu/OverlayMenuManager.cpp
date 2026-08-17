#include "menu/OverlayMenuManager.h"

#include "Config.h"
#include "NpcUtils.h"
#include "overlay/OdfWriter.h"
#include "overlay/OverlayStateManager.h"

#include <spdlog/spdlog.h>

namespace Overlay {
    namespace {
        constexpr const char* kModId = "VRSkinOverlayMenu";

        constexpr const char* kRootId     = "vrsom_root";
        constexpr const char* kWheelId    = "vrsom_wheel";
        constexpr const char* kPackRowId  = "vrsom_packrow";
        constexpr const char* kToolRowId  = "vrsom_toolrow";
        constexpr const char* kInfoId     = "vrsom_info";
        constexpr const char* kAnchorId   = "vrsom_anchor";
        constexpr const char* kClearId    = "vrsom_tool_clear";
        constexpr const char* kTargetId   = "vrsom_tool_target";

        constexpr const char* kItemPrefix = "vrsom_item_";
        constexpr const char* kPackPrefix = "vrsom_pack_";

        constexpr const char* kTexAnchor      = "textures\\VRSkinOverlays\\move.dds";
        constexpr const char* kTexClearAll    = "textures\\VRSkinOverlays\\undress-full.dds";
        constexpr const char* kTexRestoreAll  = "textures\\VRSkinOverlays\\redress-full.dds";
        constexpr const char* kTexTargetNpc   = "textures\\VRSkinOverlays\\npc.dds";
        constexpr const char* kTexTargetPlayer= "textures\\VRSkinOverlays\\player.dds";

        // The gallery highlight from VR Dress Up: a second, non-interactive projectile
        // behind the element.
        constexpr const char* kBackdropModel = "meshes\\3DUI\\cloud-background-sphere.nif";
        constexpr float kBackdropScale = 20.0f;

        // The pack row has no per-icon highlight variants, so the selected pack is shown
        // by scale, as VR Sex Menu's filter row does.
        constexpr float kPackScaleActive   = 1.45f;
        constexpr float kPackScaleInactive = 1.02f;

        // Parses "vrsom_item_12" -> 12. Returns npos when the id has a different shape.
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
    }

    MenuManager* MenuManager::GetSingleton() {
        static MenuManager instance;
        return &instance;
    }

    bool MenuManager::Initialize() {
        if (IsInitialized()) return true;

        m_api = P3DUI::GetInterface001();
        if (!m_api) {
            spdlog::error("Menu: 3DUI interface unavailable - is 3DUI.dll installed?");
            return false;
        }
        spdlog::info("Menu: 3DUI interface version {}", m_api->GetInterfaceVersion());
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

        auto wheelConfig = P3DUI::ScrollWheelConfig::Default(kWheelId);
        wheelConfig.itemSpacing = 8.0f;
        wheelConfig.ringSpacing = 10.0f;
        wheelConfig.firstRingSpacing = 15.0f;
        m_wheel = m_api->CreateScrollWheel(wheelConfig);
        m_root->AddChild(m_wheel);

        // Narrow visible width so the pack row scrolls; there are ~25 packs.
        auto packConfig = P3DUI::ColumnGridConfig::Default(kPackRowId);
        packConfig.columnSpacing = 15.0f;
        packConfig.numRows = 1;
        packConfig.visibleWidth = 40.0f;
        m_packRow = m_api->CreateColumnGrid(packConfig);
        m_packRow->SetOrigin(P3DUI::VerticalOrigin::Center, P3DUI::HorizontalOrigin::Center);
        m_root->AddChild(m_packRow);
        m_packRow->SetLocalPosition(0.0f, 0.0f, -20.0f);

        // Wide visible width so the handful of tool buttons never scroll.
        auto toolConfig = P3DUI::ColumnGridConfig::Default(kToolRowId);
        toolConfig.columnSpacing = 10.0f;
        toolConfig.numRows = 1;
        toolConfig.visibleWidth = 1000.0f;
        m_toolRow = m_api->CreateColumnGrid(toolConfig);
        m_toolRow->SetOrigin(P3DUI::VerticalOrigin::Center, P3DUI::HorizontalOrigin::Center);
        m_root->AddChild(m_toolRow);
        m_toolRow->SetLocalPosition(0.0f, 0.0f, -10.5f);

        auto textConfig = P3DUI::TextConfig::Default(kInfoId);
        textConfig.facingMode = P3DUI::FacingMode::YawOnly;
        m_infoText = m_api->CreateText(textConfig);
        m_root->AddChild(m_infoText);
        m_infoText->SetLocalPosition(0.0f, 0.0f, -28.0f);
        m_infoText->SetVisible(false);

        m_root->SetVisible(false);
        spdlog::info("Menu: built");
        return true;
    }

    RE::Actor* MenuManager::GetTargetActor() const {
        if (m_targetPlayer) return RE::PlayerCharacter::GetSingleton();

        auto actor = m_npcHandle.get();
        return actor ? actor.get() : nullptr;
    }

    void MenuManager::OpenForActor(RE::Actor* actor, bool isLeftHand) {
        if (!IsInitialized() || !actor) return;

        m_npcHandle = actor->GetHandle();
        m_targetPlayer = false;
        m_previewIndex = static_cast<size_t>(-1);
        m_open = true;

        spdlog::info("Menu: opening for {:08X} ({})", actor->GetFormID(), actor->GetName());

        m_root->SetVRAnchor(P3DUI::VRAnchorType::HMD);
        m_root->ShowAtHand(isLeftHand);

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
            RefreshAll();
            ClearInfo();
        });
    }

    void MenuManager::Close() {
        if (!m_open) return;

        EndPreview();
        m_open = false;
        m_wheelEntries.clear();
        m_packElements.clear();

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

        PopulatePackRow();
        RefreshWheel();
        PopulateToolRow();
    }

    void MenuManager::PopulatePackRow() {
        if (!m_packRow) return;

        m_packRow->Clear();
        m_packElements.clear();

        const auto& packs = Catalog::GetSingleton()->GetPacks();
        if (m_selectedPack >= packs.size()) m_selectedPack = 0;

        for (size_t i = 0; i < packs.size(); ++i) {
            const auto& pack = packs[i];

            auto config = P3DUI::ElementConfig::Default("");
            const auto id = std::string(kPackPrefix) + std::to_string(i);
            config.id = id.c_str();
            config.texturePath = pack.coverTexture.c_str();
            config.tooltip = pack.displayName.c_str();
            config.scale = (i == m_selectedPack) ? kPackScaleActive : kPackScaleInactive;
            config.facingMode = P3DUI::FacingMode::None;

            auto* element = m_api->CreateElement(config);
            if (!element) continue;

            m_packRow->AddChild(element);
            m_packElements.push_back(element);
        }

        m_packRow->ResetScroll();
        m_packRow->SetVisible(true);
        spdlog::debug("Menu: pack row rebuilt with {} packs, selected {}", m_packElements.size(), m_selectedPack);
    }

    void MenuManager::UpdatePackHighlight() {
        for (size_t i = 0; i < m_packElements.size(); ++i) {
            m_packElements[i]->SetScale(i == m_selectedPack ? kPackScaleActive : kPackScaleInactive);
        }
    }

    void MenuManager::RefreshWheel() {
        if (!m_wheel) return;

        m_wheel->Clear();
        m_wheelEntries.clear();

        auto* actor = GetTargetActor();
        const auto& packs = Catalog::GetSingleton()->GetPacks();
        if (!actor || m_selectedPack >= packs.size()) return;

        auto* state = StateManager::GetSingleton();
        const bool female = IsFemale(actor);
        const auto entries = Catalog::GetSingleton()->GetEntriesForPack(packs[m_selectedPack], female);

        for (const auto* entry : entries) {
            auto config = P3DUI::ElementConfig::Default("");
            const auto id = std::string(kItemPrefix) + std::to_string(m_wheelEntries.size());
            config.id = id.c_str();
            // Texture mode: the swatch is the overlay's own artwork.
            config.texturePath = entry->texture.c_str();
            config.tooltip = entry->displayName.c_str();
            config.scale = Config::options.elementScale;
            config.facingMode = P3DUI::FacingMode::Full;

            auto* element = m_api->CreateElement(config);
            if (!element) continue;

            if (state->IsApplied(actor, *entry)) {
                element->SetBackgroundModel(kBackdropModel);
                element->SetBackgroundScale(kBackdropScale);
            }

            m_wheel->AddChild(element);
            m_wheelEntries.push_back(entry);
        }

        m_wheel->SetVisible(true);
        spdlog::debug("Menu: wheel rebuilt with {} of {} overlays from \"{}\" (female={})",
                      m_wheelEntries.size(), packs[m_selectedPack].entryIndices.size(),
                      packs[m_selectedPack].modId, female);
    }

    void MenuManager::PopulateToolRow() {
        if (!m_toolRow) return;

        m_toolRow->Clear();

        auto addButton = [this](const char* id, const char* texture, const wchar_t* tooltip, bool anchor) {
            auto config = P3DUI::ElementConfig::Default(id);
            config.texturePath = texture;
            config.tooltip = tooltip;
            config.scale = 1.2f;
            config.facingMode = P3DUI::FacingMode::None;
            config.isAnchorHandle = anchor;

            if (auto* element = m_api->CreateElement(config)) m_toolRow->AddChild(element);
        };

        addButton(kAnchorId, kTexAnchor, L"Move menu", true);

        addButton(kTargetId,
                  m_targetPlayer ? kTexTargetPlayer : kTexTargetNpc,
                  m_targetPlayer ? L"Editing yourself - switch to the NPC" : L"Editing the NPC - switch to yourself",
                  false);

        // Same two-state cycle as VR Dress Up's undress button: the icon always says
        // what pressing it will do next.
        auto* actor = GetTargetActor();
        auto* state = StateManager::GetSingleton();
        if (actor && state->HasApplied(actor)) {
            addButton(kClearId, kTexClearAll, L"Clear all overlays", false);
        } else if (actor && state->HasCleared(actor)) {
            addButton(kClearId, kTexRestoreAll, L"Restore cleared overlays", false);
        }

        m_toolRow->SetVisible(true);
    }

    void MenuManager::ShowInfo(const std::wstring& text) {
        if (!m_infoText) return;
        m_infoText->SetText(text.c_str());
        m_infoText->SetVisible(true);
    }

    void MenuManager::ClearInfo() {
        if (m_infoText) m_infoText->SetVisible(false);
    }

    void MenuManager::BeginPreview(size_t index) {
        if (!Config::options.enableHoverPreview) return;
        if (index >= m_wheelEntries.size()) return;

        auto* actor = GetTargetActor();
        if (!actor) return;

        const auto* entry = m_wheelEntries[index];
        auto* state = StateManager::GetSingleton();

        // Already on the actor - there is nothing to show, and stamping it again would
        // risk clobbering the committed slot.
        if (state->IsApplied(actor, *entry)) return;

        EndPreview();

        if (!Skee::EnsureOverlays(actor)) return;

        auto node = Skee::FindFreeSlot(actor, IsFemale(actor), entry->location);
        if (!node) {
            ShowInfo(L"No free overlay slot");
            return;
        }

        m_previewSnapshot = Skee::SnapshotSlot(actor, *node);
        if (Skee::PreviewOnSlot(actor, *node, entry->appearance)) {
            m_previewIndex = index;
        }
    }

    void MenuManager::EndPreview() {
        if (m_previewIndex == static_cast<size_t>(-1)) return;

        if (auto* actor = GetTargetActor()) Skee::RestoreSlot(actor, m_previewSnapshot);

        m_previewIndex = static_cast<size_t>(-1);
        m_previewSnapshot = {};
    }

    void MenuManager::OnOverlayActivated(size_t index) {
        if (index >= m_wheelEntries.size()) return;

        auto* actor = GetTargetActor();
        if (!actor) return;

        // Drop the preview first so the committed apply picks its own slot cleanly.
        EndPreview();

        const auto* entry = m_wheelEntries[index];
        auto* state = StateManager::GetSingleton();

        if (state->IsApplied(actor, *entry)) {
            state->Remove(actor, *entry);
        } else if (!state->Apply(actor, *entry)) {
            ShowInfo(L"No free overlay slot");
            return;
        }

        ClearInfo();
        RefreshWheel();
        PopulateToolRow();
        PersistToOdf();
    }

    void MenuManager::OnPackActivated(size_t index) {
        const auto& packs = Catalog::GetSingleton()->GetPacks();
        if (index >= packs.size()) return;

        EndPreview();

        // Clicking the selected pack again returns to the default rather than emptying
        // the wheel - an empty wheel reads as a broken menu.
        if (index == m_selectedPack) {
            const auto previous = m_selectedPack;
            SelectDefaultPack();
            if (m_selectedPack == previous) return;
        } else {
            m_selectedPack = index;
        }

        spdlog::debug("Menu: pack filter -> \"{}\"", packs[m_selectedPack].modId);
        UpdatePackHighlight();
        RefreshWheel();
    }

    void MenuManager::OnToolActivated(const std::string& id) {
        auto* actor = GetTargetActor();
        auto* state = StateManager::GetSingleton();

        if (id == kTargetId) {
            EndPreview();
            m_targetPlayer = !m_targetPlayer;
            spdlog::info("Menu: target switched to {}", m_targetPlayer ? "the player" : "the NPC");
            RefreshAll();
            return;
        }

        if (id == kClearId) {
            if (!actor) return;
            EndPreview();

            if (state->HasApplied(actor)) {
                state->ClearAll(actor);
            } else {
                state->RestoreAll(actor);
            }
            RefreshWheel();
            PopulateToolRow();
            PersistToOdf();
        }
    }

    void MenuManager::PersistToOdf() {
        if (!Config::options.writeOdfRules) return;
        OdfWriter::WriteAll(*StateManager::GetSingleton());
    }

    bool MenuManager::OnEvent(const P3DUI::Event* event) {
        return GetSingleton()->HandleEvent(event);
    }

    bool MenuManager::HandleEvent(const P3DUI::Event* event) {
        if (!event || !event->sourceID || !m_open) return false;

        const std::string id(event->sourceID);

        switch (event->type) {
            case P3DUI::EventType::HoverEnter: {
                const auto itemIndex = ParseIndex(id, kItemPrefix);
                if (itemIndex != static_cast<size_t>(-1)) {
                    BeginPreview(itemIndex);
                    return true;
                }
                if (id == kClearId) {
                    auto* actor = GetTargetActor();
                    const bool hasApplied = actor && StateManager::GetSingleton()->HasApplied(actor);
                    ShowInfo(hasApplied ? L"Remove every overlay from this actor"
                                        : L"Put the cleared overlays back");
                    return true;
                }
                if (id == kTargetId) {
                    ShowInfo(L"Switch between the NPC and yourself");
                    return true;
                }
                return false;
            }

            case P3DUI::EventType::HoverExit: {
                const auto itemIndex = ParseIndex(id, kItemPrefix);
                if (itemIndex != static_cast<size_t>(-1)) {
                    if (itemIndex == m_previewIndex) EndPreview();
                    return true;
                }
                if (id == kClearId || id == kTargetId) {
                    ClearInfo();
                    return true;
                }
                return false;
            }

            case P3DUI::EventType::ActivateUp: {
                const auto itemIndex = ParseIndex(id, kItemPrefix);
                if (itemIndex != static_cast<size_t>(-1)) {
                    OnOverlayActivated(itemIndex);
                    return true;
                }
                const auto packIndex = ParseIndex(id, kPackPrefix);
                if (packIndex != static_cast<size_t>(-1)) {
                    OnPackActivated(packIndex);
                    return true;
                }
                if (id == kClearId || id == kTargetId) {
                    OnToolActivated(id);
                    return true;
                }
                return false;
            }

            default:
                return false;
        }
    }
}
