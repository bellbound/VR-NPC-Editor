#include "obody/ObodyBridge.h"

#include <atomic>
#include <mutex>
#include <spdlog/spdlog.h>

// API.h names `Actor` and `TESForm` without a namespace and leaves them undefined on
// purpose, so that consumers can point them at whichever Skyrim bindings they use.
// These two aliases are the whole reason the header is confined to this file.
using Actor = RE::Actor;
using TESForm = RE::TESForm;
#include "external/obody/API.h"

namespace NPCEditor::Obody {
    namespace {
        // OBody writes through this pointer during the handshake; it may do so before
        // Dispatch returns, and it reads it again from its own threads.
        OBody::API::IPluginInterface* g_api = nullptr;

        std::atomic_bool g_ready{false};
        std::string g_status = "not checked";

        std::mutex g_callbackMutex;
        std::function<void()> g_onUnready;

        // OBody keeps a pointer to this for the life of the process, so it is a
        // function-local static rather than anything with a shorter life.
        class ReadinessListener final : public OBody::API::IOBodyReadinessEventListener {
        public:
            void OBodyIsReady() override {
                g_ready.store(true);
                spdlog::debug("OBody: ready");
            }

            void OBodyIsNoLongerReady() override {
                g_ready.store(false);
                spdlog::debug("OBody: no longer ready");

                // Copy under the lock, then call outside it: the callback closes the
                // body menu, which must not be holding this lock if it ever calls back.
                std::function<void()> callback;
                {
                    std::lock_guard<std::mutex> lock(g_callbackMutex);
                    callback = g_onUnready;
                }
                if (callback) callback();
            }
        };

        ReadinessListener& GetListener() {
            static ReadinessListener listener;
            return listener;
        }

        // Every entry point funnels through here. Returning null is the normal path
        // when OBody is absent or mid-teardown, and callers treat it as "do nothing".
        OBody::API::IPluginInterface* Usable(RE::Actor* actor) {
            if (!actor) return nullptr;
            if (!g_api) return nullptr;
            if (!g_ready.load()) {
                spdlog::debug("OBody: call skipped, OBody is not ready");
                return nullptr;
            }
            return g_api;
        }

        OBody::API::PresetCategory CategoryFor(RE::Actor* actor, bool blacklisted) {
            auto* base = actor->GetActorBase();
            const bool female = base && base->GetSex() == RE::SEX::kFemale;
            if (female) {
                return blacklisted ? OBody::API::PresetCategoryFemaleBlacklisted
                                   : OBody::API::PresetCategoryFemale;
            }
            return blacklisted ? OBody::API::PresetCategoryMaleBlacklisted
                               : OBody::API::PresetCategoryMale;
        }

        // Pulls one category into `out`. OBody hands back string_views into its own
        // storage, which is invalidated on the next unready event, so they are copied
        // on the spot rather than held.
        void AppendCategory(OBody::API::IPluginInterface* api, OBody::API::PresetCategory category,
                            uint32_t count, std::vector<std::string>& out) {
            if (count == 0) return;

            std::vector<std::string_view> buffer(count);
            const size_t written = api->GetPresetNames(category, buffer.data(), buffer.size());

            for (size_t i = 0; i < written; ++i) {
                if (!buffer[i].empty()) out.emplace_back(buffer[i]);
            }
        }
    }

    void Initialize() {
        if (g_api) return;

        OBody::API::SKSEMessages::RequestPluginInterface request{};
        request.version = OBody::API::PluginAPIVersion::Latest;
        request.pluginInterface = &g_api;
        request.readinessEventListener = &GetListener();

        SKSE::GetMessagingInterface()->Dispatch(
            decltype(request)::type, &request, sizeof(request), "OBody");

        if (!g_api) {
            g_status = "not installed (no response to RequestPluginInterface)";
            spdlog::info("OBody: {}", g_status);
            return;
        }

        g_api->SetOwner("VRNPCEditor");
        g_status = std::format("plugin API v{}", static_cast<uint32_t>(g_api->PluginAPIVersion()));
        spdlog::info("OBody: acquired interface, {}", g_status);
    }

    bool IsAvailable() { return g_api != nullptr; }
    bool IsReady() { return g_api != nullptr && g_ready.load(); }
    const std::string& GetStatus() { return g_status; }

    void SetUnreadyCallback(std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        g_onUnready = std::move(callback);
    }

    std::vector<std::string> GetPresetNames(RE::Actor* actor) {
        std::vector<std::string> names;

        auto* api = Usable(actor);
        if (!api) return names;

        OBody::API::PresetCounts counts{};
        api->GetPresetCounts(counts);

        auto* base = actor->GetActorBase();
        const bool female = base && base->GetSex() == RE::SEX::kFemale;
        const uint32_t plain = female ? counts.female : counts.male;
        const uint32_t blacklisted = female ? counts.femaleBlacklisted : counts.maleBlacklisted;

        names.reserve(static_cast<size_t>(plain) + blacklisted);

        // Blacklisted presets are ones OBody will not hand out on its own. Picking one
        // by hand is exactly the case they exist for, so they are listed too.
        AppendCategory(api, CategoryFor(actor, false), plain, names);
        AppendCategory(api, CategoryFor(actor, true), blacklisted, names);

        spdlog::debug("OBody: {} presets for '{}' ({}, {} plain + {} blacklisted)",
                      names.size(), actor->GetName(), female ? "female" : "male", plain, blacklisted);
        return names;
    }

    std::string GetAssignedPreset(RE::Actor* actor) {
        auto* api = Usable(actor);
        if (!api) return {};

        // The header is explicit that flags must be initialised by the caller.
        OBody::API::PresetAssignmentInformation info{};
        info.flags = OBody::API::PresetAssignmentInformation::Flags::None;
        api->GetPresetAssignedToActor(actor, info);

        return std::string(info.presetName);
    }

    bool ApplyPreset(RE::Actor* actor, const std::string& presetName) {
        auto* api = Usable(actor);
        if (!api) return false;

        OBody::API::AssignPresetPayload payload{};
        payload.flags = OBody::API::AssignPresetPayload::Flags::ForceImmediateApplicationOfMorphs;
        payload.presetName = presetName;

        const bool found = api->AssignPresetToActor(actor, payload);
        if (!found) {
            spdlog::warn("OBody: preset '{}' not recognised for '{}'", presetName, actor->GetName());
        } else {
            spdlog::debug("OBody: applied '{}' to '{}'", presetName, actor->GetName());
        }
        return found;
    }

    void ReapplyMorphs(RE::Actor* actor) {
        if (auto* api = Usable(actor)) api->ApplyOBodyMorphsToActor(actor);
    }

    void EnsureProcessed(RE::Actor* actor) {
        if (auto* api = Usable(actor)) api->EnsureActorIsProcessed(actor);
    }
}
