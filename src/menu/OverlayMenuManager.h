#pragma once

#include <string>
#include <vector>

#include "api/ThreeDUIInterface001.h"
#include "overlay/OverlayCatalog.h"
#include "skee/SkeeBridge.h"

namespace Overlay {
    // The 3DUI menu itself: a wheel of overlay swatches, a pack filter row, a tool row
    // and a status line. Rows are rebuilt from scratch on every state change, which is
    // the idiom the other 3DUI mods in this workspace use.
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
        bool HandleEvent(const P3DUI::Event* event);
        bool BuildMenu();

        void RefreshAll();
        void RefreshWheel();
        void PopulatePackRow();
        void PopulateToolRow();
        void UpdatePackHighlight();

        void OnOverlayActivated(size_t index);
        void OnPackActivated(size_t index);
        void OnToolActivated(const std::string& id);

        void BeginPreview(size_t index);
        void EndPreview();

        void ShowInfo(const std::wstring& text);
        void ClearInfo();

        void SelectDefaultPack();
        void PersistToOdf();

        P3DUI::Interface001* m_api = nullptr;
        P3DUI::Root* m_root = nullptr;
        P3DUI::Container* m_wheel = nullptr;
        P3DUI::ScrollableContainer* m_packRow = nullptr;
        P3DUI::ScrollableContainer* m_toolRow = nullptr;
        P3DUI::Text* m_infoText = nullptr;

        bool m_open = false;
        RE::ActorHandle m_npcHandle;
        bool m_targetPlayer = false;

        size_t m_selectedPack = 0;

        // Parallel to the "vrsom_item_<n>" elements currently in the wheel.
        std::vector<const Entry*> m_wheelEntries;

        // Retained so the selected pack can be re-scaled without rebuilding the row.
        std::vector<P3DUI::Element*> m_packElements;

        // The live hover preview, if any.
        size_t m_previewIndex = static_cast<size_t>(-1);
        Skee::SlotSnapshot m_previewSnapshot;
    };
}
