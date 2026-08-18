#include "Config.h"
#include "dressup/UndressManager.h"
#include "FrameHook.h"
#include "health/HealthCheckManager.h"
#include "higgsinterface001.h"
#include "InputDispatcher.h"
#include "InputManager.h"
#include "log.h"
#include "MenuChecker.h"
#include "menu/BodyMenuManager.h"
#include "menu/MenuRouter.h"
#include "menu/OverlayMenuManager.h"
#include "obody/ObodyBridge.h"
#include "overlay/OdfWriter.h"
#include "overlay/OverlayCatalog.h"
#include "overlay/OverlayStateManager.h"
#include "skee/SkeeBridge.h"

namespace {
    constexpr uint32_t kSerializationId = 'VNPE';
    constexpr uint32_t kActorRecord = 'AOVL';

    // Reported once, on the first load screen, so the player learns why nothing happens.
    bool g_warnedMissing3DUI = false;
    bool g_missing3DUI = false;

    void ReportMissingDependencies() {
        // Only 3DUI is worth interrupting the player for: without it there is no menu
        // at all. A missing RaceMenu or OBody merely hides one editor, which the
        // dependency block in the log already explains.
        if (g_missing3DUI && !g_warnedMissing3DUI) {
            RE::DebugNotification("VR NPC Editor: 3DUI is missing, the menu is disabled");
            g_warnedMissing3DUI = true;
        }
    }

    // Reading every installed pack takes a moment - not a hitch, the build only ever
    // uses a couple of milliseconds of any frame, but long enough that a first open used
    // to sit on "Loading overlays..." while it finished. Started here instead, it is done
    // long before anyone grabs an NPC, and the menu opens with its packs already in hand.
    //
    // Once per session is enough: the catalog describes what is installed, which does not
    // change between saves, so the second load screen finds it built and returns at once.
    void PreloadCatalog() {
        if (!Config::options.preloadCatalog) return;
        if (!NPCEditor::Health::IsFeatureAvailable(NPCEditor::Health::Feature::Overlays)) return;

        NPCEditor::Overlay::Catalog::GetSingleton()->StartBuildAsync();
    }

    void OnGameSave(SKSE::SerializationInterface* serialization) {
        NPCEditor::Overlay::StateManager::GetSingleton()->Save(serialization);
    }

    void OnGameLoad(SKSE::SerializationInterface* serialization) {
        NPCEditor::Overlay::StateManager::GetSingleton()->Revert();

        std::uint32_t type = 0, version = 0, length = 0;
        while (serialization->GetNextRecordInfo(type, version, length)) {
            switch (type) {
                case kActorRecord:
                    NPCEditor::Overlay::StateManager::GetSingleton()->Load(serialization);
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
        NPCEditor::MenuRouter::GetSingleton()->CloseAll();
        NPCEditor::Overlay::StateManager::GetSingleton()->Revert();
        // Stored outfits describe actor instances that no longer exist after a load.
        NPCEditor::UndressManager::GetSingleton()->Reset();
    }
}

void MessageHandler(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kPostPostLoad: {
            // By now every SKSE plugin DLL is loaded, so module checks resolve and both
            // interface handshakes have someone left to answer them.
            Skee::Initialize();
            NPCEditor::Obody::Initialize();

            auto* messaging = SKSE::GetMessagingInterface();
            HiggsPluginAPI::GetHiggsInterface001(messaging);
            if (g_higgsInterface) {
                spdlog::info("HIGGS interface acquired, build {}", g_higgsInterface->GetBuildNumber());
            } else {
                spdlog::error("HIGGS interface unavailable - the grab gesture will not work");
            }

            NPCEditor::Health::RunEarlyChecks();
            break;
        }

        case SKSE::MessagingInterface::kDataLoaded: {
            // A second attempt, in case RaceMenu registered after our PostPostLoad.
            if (!Skee::IsAvailable()) Skee::Initialize();

            MenuChecker::RegisterEventSink();

            if (!NPCEditor::Overlay::MenuManager::GetSingleton()->Initialize()) {
                spdlog::error("Overlay menu could not be initialised - 3DUI.dll may not be installed");
                g_missing3DUI = true;
            }
            if (!NPCEditor::BodyMenuManager::GetSingleton()->Initialize()) {
                spdlog::error("Body menu could not be initialised - 3DUI.dll may not be installed");
                g_missing3DUI = true;
            }

            InputManager::GetSingleton()->Initialize();

            NPCEditor::Health::RunDataLoadedChecks();

            // An entry that always refuses is just a dead icon in the actor menu, so it
            // is only registered when at least one editor could ever be available.
            const bool anyEditor =
                NPCEditor::Health::IsFeatureAvailable(NPCEditor::Health::Feature::Overlays) ||
                NPCEditor::Health::IsFeatureAvailable(NPCEditor::Health::Feature::Body);

            if (anyEditor && !g_missing3DUI) {
                InputDispatcher::GetSingleton()->Initialize();
            } else {
                spdlog::error("No editor is available - not registering the actor menu entry");
            }

            spdlog::info("ODF rules will be written to {}", NPCEditor::Overlay::OdfWriter::GetOutputPath());
            break;
        }

        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            NPCEditor::MenuRouter::GetSingleton()->CloseAll();
            ReportMissingDependencies();
            PreloadCatalog();
            break;

        default:
            break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    // false = do not install CommonLib's own logger. It opens <plugin>.log immediately,
    // which held the file open across SetupLog's rotation: the rename failed, the copy
    // fallback duplicated CommonLib's one-line stub, and the previous run's log was then
    // truncated away. Every crash report we went looking for had already been destroyed.
    SKSE::Init(skse, false);
    SetupLog();

    // Before anything schedules work: the chunked menu builds and the body menu's tick
    // both hang off this, and without it they fall back to running inside one frame.
    NPCEditor::FrameHook::Install();

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

    spdlog::info("VR NPC Editor loaded");
    return true;
}
