#include "higgsinterface001.h"
#include "InputDispatcher.h"
#include "InputManager.h"
#include "log.h"
#include "MenuChecker.h"
#include "menu/OverlayMenuManager.h"
#include "overlay/OdfWriter.h"
#include "overlay/OverlayStateManager.h"
#include "skee/SkeeBridge.h"

namespace {
    constexpr uint32_t kSerializationId = 'VSOM';
    constexpr uint32_t kActorRecord = 'AOVL';

    // Reported once, on the first load screen, so the player learns why nothing happens.
    bool g_warnedMissing3DUI = false;
    bool g_warnedMissingSkee = false;
    bool g_missing3DUI = false;

    void ReportMissingDependencies() {
        if (g_missing3DUI && !g_warnedMissing3DUI) {
            RE::DebugNotification("VR Skin Overlay Menu: 3DUI is missing, the menu is disabled");
            g_warnedMissing3DUI = true;
        }
        if (!Skee::IsAvailable() && !g_warnedMissingSkee) {
            RE::DebugNotification("VR Skin Overlay Menu: RaceMenu not found, overlays are disabled");
            g_warnedMissingSkee = true;
        }
    }

    void OnGameSave(SKSE::SerializationInterface* serialization) {
        Overlay::StateManager::GetSingleton()->Save(serialization);
    }

    void OnGameLoad(SKSE::SerializationInterface* serialization) {
        Overlay::StateManager::GetSingleton()->Revert();

        std::uint32_t type = 0, version = 0, length = 0;
        while (serialization->GetNextRecordInfo(type, version, length)) {
            switch (type) {
                case kActorRecord:
                    Overlay::StateManager::GetSingleton()->Load(serialization);
                    break;
                default:
                    // Skipping rather than aborting keeps an older or newer co-save from
                    // desynchronising the read cursor.
                    if (length > 0) {
                        std::vector<char> skip(length);
                        serialization->ReadRecordData(skip.data(), length);
                    }
                    spdlog::warn("Load: unknown record {:08X}, skipped {} bytes", type, length);
                    break;
            }
        }
        spdlog::info("Load: finished reading co-save records");
    }

    void OnRevert(SKSE::SerializationInterface*) {
        Overlay::MenuManager::GetSingleton()->Close();
        Overlay::StateManager::GetSingleton()->Revert();
    }
}

void MessageHandler(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kPostPostLoad: {
            // By now every SKSE plugin DLL is loaded, so the RaceMenu module check resolves.
            Skee::Initialize();

            auto* messaging = SKSE::GetMessagingInterface();
            HiggsPluginAPI::GetHiggsInterface001(messaging);
            if (g_higgsInterface) {
                spdlog::info("HIGGS interface acquired, build {}", g_higgsInterface->GetBuildNumber());
            } else {
                spdlog::error("HIGGS interface unavailable - the grab gesture will not work");
            }
            break;
        }

        case SKSE::MessagingInterface::kDataLoaded: {
            // A second attempt, in case RaceMenu registered after our PostPostLoad.
            if (!Skee::IsAvailable()) Skee::Initialize();

            MenuChecker::RegisterEventSink();

            if (!Overlay::MenuManager::GetSingleton()->Initialize()) {
                spdlog::error("Menu could not be initialised - 3DUI.dll may not be installed");
                g_missing3DUI = true;
            }

            InputManager::GetSingleton()->Initialize();

            // Registering the actor-menu entry is pointless without RaceMenu, and an
            // entry that always refuses would just be a dead icon in the tween menu.
            if (Skee::IsAvailable()) {
                InputDispatcher::GetSingleton()->Initialize();
            } else {
                spdlog::error("RaceMenu (skeevr.dll) not found - not registering the actor menu entry");
            }

            spdlog::info("ODF rules will be written to {}", Overlay::OdfWriter::GetOutputPath());
            break;
        }

        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            Overlay::MenuManager::GetSingleton()->Close();
            ReportMissingDependencies();
            break;

        default:
            break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLog();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener("SKSE", MessageHandler)) {
        spdlog::error("Failed to register the SKSE message listener");
        return false;
    }

    if (auto* serialization = SKSE::GetSerializationInterface()) {
        serialization->SetUniqueID(kSerializationId);
        serialization->SetSaveCallback(OnGameSave);
        serialization->SetLoadCallback(OnGameLoad);
        serialization->SetRevertCallback(OnRevert);
    } else {
        spdlog::error("No SKSE serialization interface - overlays will not survive a save");
    }

    spdlog::info("VR Skin Overlay Menu loaded");
    return true;
}
