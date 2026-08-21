#include "health/HealthCheckManager.h"

#include <Windows.h>
#include <filesystem>
#include <spdlog/spdlog.h>

#include "api/ThreeDUIInterface001.h"
#include "Config.h"
#include "higgsinterface001.h"
#include "NpcUtils.h"
#include "obody/ObodyBridge.h"
#include "skee/SkeeBridge.h"
#include "tng/TngBridge.h"

namespace NPCEditor::Health {
    namespace {
        constexpr const char* kThreeDUI = "3DUI";
        constexpr const char* kHiggs = "HIGGS";
        constexpr const char* kRaceMenu = "RaceMenu VR (SKEE)";
        constexpr const char* kOdf = "Overlay Distribution Framework";
        constexpr const char* kObody = "OBody NG";
        constexpr const char* kTng = "The New Gentleman";
        constexpr const char* kIcons = "VR NPC Editor icons";

        // Every texture the two menus can hand to 3DUI. A missing one is invisible in
        // every log but this: BSShaderManager returns a live NiTexture for a path that
        // does not resolve, so 3DUI reports a successful load and the icon then renders
        // as nothing at all. Enumerating them is the only way that failure gets said out
        // loud.
        constexpr const char* kIconFiles[] = {
            "tpose.dds", "tpose_highlight.dds", "transparent.dds",
            "paint-palette.dds", "move.dds", "npc.dds", "player.dds",
            "undress-full.dds", "redress-full.dds", "dice.dds",
            // Discard and keep, in both menus.
            "cross.dds", "check.dds",
            "weight_0.dds", "weight_25.dds", "weight_50.dds", "weight_75.dds", "weight_100.dds",
            "tng-addon.dds", "tng-addon_highlight.dds",
            "tng-size_0.dds", "tng-size_25.dds", "tng-size_50.dds", "tng-size_75.dds",
            "tng-size_100.dds",
            // Undo and redo, both faces each.
            "undo.dds", "undo_disabled.dds", "redo.dds", "redo_disabled.dds",
        };

        std::vector<Dependency> g_dependencies;

        Dependency& Slot(const char* name, bool required) {
            for (auto& dep : g_dependencies) {
                if (dep.name == name) return dep;
            }
            g_dependencies.push_back(Dependency{name, false, "not checked", required});
            return g_dependencies.back();
        }

        bool Present(const char* name) {
            for (const auto& dep : g_dependencies) {
                if (dep.name == name) return dep.present;
            }
            return false;
        }

        // The catalog is built lazily on first menu open - deliberately, so the mod
        // costs nothing until used - so ODF cannot be measured by pack count here.
        // Counting manifest files is the cheap stand-in: a directory listing, no parse.
        void ProbeOdf() {
            auto& dep = Slot(kOdf, false);

            std::filesystem::path configs(Config::GetSKSEPluginsPath());
            configs /= "ODF_mod_configs";

            std::error_code ec;
            if (!std::filesystem::is_directory(configs, ec)) {
                dep.present = false;
                dep.detail = std::format("no {} directory", configs.string());
                return;
            }

            size_t manifests = 0;
            for (const auto& file : std::filesystem::directory_iterator(configs, ec)) {
                if (file.is_regular_file(ec) && file.path().extension() == ".json") ++manifests;
            }

            dep.present = manifests > 0;
            dep.detail = std::format("{} pack manifests found (contents validated on first open)", manifests);
        }

        void ProbeThreeDUI() {
            auto& dep = Slot(kThreeDUI, true);
            auto* api = P3DUI::GetInterface001();
            dep.present = api != nullptr;
            dep.detail = api ? std::format("interface version {}", api->GetInterfaceVersion())
                             : "3DUI.dll not found - nothing in this mod can be reached";
        }

        void ProbeHiggs() {
            auto& dep = Slot(kHiggs, false);
            dep.present = g_higgsInterface != nullptr;
            dep.detail = g_higgsInterface
                ? std::format("build {}", g_higgsInterface->GetBuildNumber())
                : "not found - the menu cannot tell which hand holds the NPC";
        }

        void ProbeRaceMenu() {
            auto& dep = Slot(kRaceMenu, false);
            dep.present = Skee::IsAvailable();
            if (dep.present) {
                dep.detail = std::format("body={} hands={} feet={} face={} overlay slots",
                                         Skee::GetSlotCount(Skee::Location::Body),
                                         Skee::GetSlotCount(Skee::Location::Hand),
                                         Skee::GetSlotCount(Skee::Location::Feet),
                                         Skee::GetSlotCount(Skee::Location::Face));
            } else {
                dep.detail = "neither skeevr.dll nor skee64.dll is loaded";
            }
        }

        void ProbeIcons() {
            auto& dep = Slot(kIcons, true);

            // <game>\Data\SKSE\Plugins -> <game>\Data\textures\VRNPCEditor
            std::filesystem::path textures =
                std::filesystem::path(Config::GetSKSEPluginsPath()).parent_path().parent_path();
            textures /= "textures";
            textures /= "VRNPCEditor";

            std::error_code ec;
            if (!std::filesystem::is_directory(textures, ec)) {
                dep.present = false;
                dep.detail = std::format("{} does not exist - every icon renders blank",
                                         textures.string());
                return;
            }

            std::string missing;
            size_t missingCount = 0;
            for (const char* icon : kIconFiles) {
                if (std::filesystem::exists(textures / icon, ec)) continue;
                if (!missing.empty()) missing += ", ";
                missing += icon;
                ++missingCount;
            }

            dep.present = missingCount == 0;
            dep.detail = dep.present
                ? std::format("all {} icons present in {}", std::size(kIconFiles), textures.string())
                : std::format("{} of {} icons missing from {}: {}",
                              missingCount, std::size(kIconFiles), textures.string(), missing);
        }

        void ProbeObody() {
            auto& dep = Slot(kObody, false);
            dep.present = Obody::IsAvailable();
            dep.detail = Obody::GetStatus();
        }

        // Runs at kDataLoaded only. Unlike the others this probe resolves a Papyrus
        // script type, which loads the .pex - there is nothing to load it from during
        // the early pass, so asking then would record a false absence.
        void ProbeTng() {
            auto& dep = Slot(kTng, false);
            dep.present = Tng::IsAvailable();
            dep.detail = Tng::GetStatus();
        }
    }

    void RunEarlyChecks() {
        ProbeThreeDUI();
        ProbeHiggs();
        ProbeRaceMenu();
        ProbeObody();
        ProbeOdf();
        ProbeIcons();
    }

    void RunDataLoadedChecks() {
        // Anything that registered after our PostPostLoad gets a second look.
        ProbeThreeDUI();
        ProbeHiggs();
        ProbeRaceMenu();
        ProbeObody();
        ProbeTng();
        ProbeOdf();
        ProbeIcons();

        spdlog::info("--- dependency check ---");
        for (const auto& dep : g_dependencies) {
            const char* mark = dep.present ? "OK  " : (dep.required ? "FAIL" : "----");
            spdlog::info("  [{}] {}: {}", mark, dep.name, dep.detail);
        }
        spdlog::info("  features: overlays={} body={} weight={} clothes={} tng={}",
                     IsFeatureAvailable(Feature::Overlays),
                     IsFeatureAvailable(Feature::Body),
                     IsFeatureAvailable(Feature::Weight),
                     IsFeatureAvailable(Feature::ClothesToggle),
                     IsFeatureAvailable(Feature::Tng));
        spdlog::info("------------------------");

        // Repeated above error level, because a blank menu looks like a broken mod and
        // the cause is always the same one thing: the textures are not where we look.
        if (!Present(kIcons)) {
            spdlog::error("Icons: {} - the menus will draw, but every button will be invisible",
                          Slot(kIcons, true).detail);
        }
    }

    bool IsFeatureAvailable(Feature feature) {
        switch (feature) {
            case Feature::Overlays:
                return Present(kRaceMenu) && Present(kOdf);
            case Feature::Body:
                return Config::options.enableBodyMenu && Present(kObody);
            case Feature::Weight:
                return Config::options.enableWeightButton && Present(kObody);
            case Feature::ClothesToggle:
                return true;
            case Feature::Tng:
                return Config::options.enableTngAddon && Present(kTng);
            default:
                return false;
        }
    }

    bool CanEditOverlays(RE::Actor* actor) {
        if (!actor || !IsFeatureAvailable(Feature::Overlays)) return false;
        return !NpcUtils::GetPersistableEditorID(actor).empty();
    }

    bool CanEditBody(RE::Actor* actor) {
        return actor && IsFeatureAvailable(Feature::Body);
    }

    bool CanEditWeight(RE::Actor* actor) {
        if (!actor || !IsFeatureAvailable(Feature::Weight)) return false;
        // TESNPC::weight is a base-record field. On a generic actor it is shared with
        // every other actor off that base, so one nudge reshapes the whole template.
        auto* base = actor->GetActorBase();
        return base && base->IsUnique();
    }

    bool AnyEditorAvailable(RE::Actor* actor) {
        return CanEditOverlays(actor) || CanEditBody(actor);
    }

    const std::vector<Dependency>& GetDependencies() { return g_dependencies; }
}
