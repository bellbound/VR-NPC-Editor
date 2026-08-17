#include "skee/SkeeBridge.h"

#include "Config.h"

#include <Windows.h>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace Skee {
    namespace {
        // Shader override keys, as documented at the top of nioverride.psc. Index 0
        // selects the diffuse map for the texture key; the rest take -1, "not relevant".
        constexpr int32_t kGlowColor     = 0;
        constexpr int32_t kGlowIntensity = 1;
        constexpr int32_t kTintColor     = 7;
        constexpr int32_t kAlpha         = 8;
        constexpr int32_t kTexture       = 9;
        constexpr int32_t kDiffuseIndex  = 0;
        constexpr int32_t kIndexNone     = -1;

        constexpr const char* kNiOverride = "NiOverride";

        bool g_available = false;

        struct SlotCounts {
            uint32_t body = 6;   // skeevr.ini defaults; VR ships 6 where SE ships 8
            uint32_t hand = 3;
            uint32_t feet = 3;
            uint32_t face = 3;
        } g_slots;

        uint32_t& CountFor(Location loc) {
            switch (loc) {
                case Location::Hand: return g_slots.hand;
                case Location::Feet: return g_slots.feet;
                case Location::Face: return g_slots.face;
                case Location::Body:
                default:             return g_slots.body;
            }
        }

        const char* NodePrefix(Location loc) {
            switch (loc) {
                case Location::Hand: return "Hands";
                case Location::Feet: return "Feet";
                case Location::Face: return "Face";
                case Location::Body:
                default:             return "Body";
            }
        }

        RE::BSScript::IVirtualMachine* GetVM() {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) spdlog::error("NiOverride: no Papyrus virtual machine");
            return vm;
        }

        // Every NiOverride write is fire-and-forget: the VM only returns a value through
        // an async callback, and none of these need one.
        bool CallGlobal(const char* fnName, RE::BSScript::IFunctionArguments* args) {
            auto* vm = GetVM();
            if (!vm) return false;

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{};
            const bool ok = vm->DispatchStaticCall(kNiOverride, fnName, args, callback);
            if (!ok) spdlog::warn("NiOverride: dispatch of {} failed", fnName);
            return ok;
        }

        // RaceMenu parks unused overlay slots on this texture, so a node wearing it is free.
        bool IsDefaultOverlayTexture(std::string_view path) {
            if (path.empty()) return true;
            return path.find("overlays\\default") != std::string_view::npos ||
                   path.find("overlays/default") != std::string_view::npos;
        }

        std::string ToLower(std::string text) {
            for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        RE::BSGeometry* FindOverlayGeometry(RE::Actor* actor, const std::string& node) {
            if (!actor || node.empty()) return nullptr;

            auto* root = actor->Get3D(false);
            if (!root) return nullptr;

            // ODF writes these node names lowercase and it works, so the game's lookup is
            // not case-sensitive - but try the canonical spelling first regardless.
            auto* object = root->GetObjectByName(RE::BSFixedString(node.c_str()));
            if (!object) object = root->GetObjectByName(RE::BSFixedString(ToLower(node).c_str()));
            return object ? object->AsGeometry() : nullptr;
        }

        uint32_t ReadIniCount(const std::string& iniPath, const char* section, uint32_t fallback) {
            const auto value = GetPrivateProfileIntA(section, "iNumOverlays", -1, iniPath.c_str());
            if (value < 0) return fallback;
            return static_cast<uint32_t>(value);
        }
    }

    const char* LocationName(Location loc) {
        switch (loc) {
            case Location::Hand: return "hands";
            case Location::Feet: return "feet";
            case Location::Face: return "face";
            case Location::Body:
            default:             return "body";
        }
    }

    std::optional<Location> ParseLocation(std::string_view slot) {
        if (slot == "body") return Location::Body;
        if (slot == "hands" || slot == "hand") return Location::Hand;
        if (slot == "feet" || slot == "foot") return Location::Feet;
        if (slot == "face" || slot == "head") return Location::Face;
        return std::nullopt;
    }

    bool IsAvailable() { return g_available; }

    bool Initialize() {
        if (g_available) return true;

        // The VR extender is skeevr.dll; skee64.dll is the SE/AE name. Checking the
        // loaded module avoids guessing at an SKSE plugin name that VR does not publish.
        const bool vr = GetModuleHandleA("skeevr.dll") != nullptr;
        const bool flat = GetModuleHandleA("skee64.dll") != nullptr;
        if (!vr && !flat) {
            spdlog::error("NiOverride: neither skeevr.dll nor skee64.dll is loaded - is RaceMenu installed?");
            return false;
        }
        spdlog::info("NiOverride: found {}", vr ? "skeevr.dll (VR)" : "skee64.dll");

        // Slot counts are user-editable, so they are read rather than assumed.
        std::filesystem::path iniPath(Config::GetSKSEPluginsPath());
        iniPath /= vr ? "skeevr.ini" : "skee64.ini";

        std::error_code ec;
        if (std::filesystem::exists(iniPath, ec)) {
            const auto path = iniPath.string();
            g_slots.body = ReadIniCount(path, "Overlays/Body", g_slots.body);
            g_slots.hand = ReadIniCount(path, "Overlays/Hands", g_slots.hand);
            g_slots.feet = ReadIniCount(path, "Overlays/Feet", g_slots.feet);
            g_slots.face = ReadIniCount(path, "Overlays/Face", g_slots.face);
            spdlog::info("NiOverride: slot counts from {}", path);
        } else {
            spdlog::warn("NiOverride: {} not found, using defaults", iniPath.string());
        }
        spdlog::info("NiOverride: slots body={} hands={} feet={} face={}",
                     g_slots.body, g_slots.hand, g_slots.feet, g_slots.face);

        g_available = true;
        return true;
    }

    uint32_t GetSlotCount(Location loc) { return g_available ? CountFor(loc) : 0; }

    std::string GetNodeName(Location loc, uint32_t index) {
        return std::format("{} [Ovl{}]", NodePrefix(loc), index);
    }

    bool EnsureOverlays(RE::Actor* actor) {
        if (!g_available || !actor) return false;

        // AddOverlays is idempotent, and asking HasOverlays would cost an async round
        // trip for information we would only use to skip an idempotent call.
        return CallGlobal("AddOverlays", RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(actor)));
    }

    std::optional<std::string> GetSlotTexture(RE::Actor* actor, const std::string& node) {
        auto* geometry = FindOverlayGeometry(actor, node);
        if (!geometry) return std::nullopt;

        auto& runtime = geometry->GetGeometryRuntimeData();
        auto* effect = runtime.properties[RE::BSGeometry::States::kEffect].get();
        auto* shader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect);
        if (!shader || !shader->material) return std::nullopt;

        auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(shader->material);
        auto* textures = material->textureSet.get();
        if (!textures) return std::nullopt;

        const char* diffuse = textures->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
        if (!diffuse || !*diffuse) return std::nullopt;
        return std::string(diffuse);
    }

    bool IsSlotOccupied(RE::Actor* actor, const std::string& node) {
        auto texture = GetSlotTexture(actor, node);
        return texture && !IsDefaultOverlayTexture(*texture);
    }

    std::optional<std::string> FindFreeSlot(RE::Actor* actor, Location loc) {
        const uint32_t count = GetSlotCount(loc);
        for (uint32_t i = 0; i < count; ++i) {
            auto node = GetNodeName(loc, i);
            if (!IsSlotOccupied(actor, node)) return node;
        }
        spdlog::warn("NiOverride: all {} {} slots on {:08X} are in use",
                     count, LocationName(loc), actor ? actor->GetFormID() : 0);
        return std::nullopt;
    }

    std::vector<std::string> GetOccupiedSlots(RE::Actor* actor, Location loc) {
        std::vector<std::string> occupied;
        const uint32_t count = GetSlotCount(loc);
        for (uint32_t i = 0; i < count; ++i) {
            auto node = GetNodeName(loc, i);
            if (IsSlotOccupied(actor, node)) occupied.push_back(std::move(node));
        }
        return occupied;
    }

    namespace {
        void WriteAppearance(RE::Actor* actor, bool isFemale, const std::string& node,
                             const Appearance& look, bool persist) {
            auto* refr = static_cast<RE::TESObjectREFR*>(actor);

            CallGlobal("AddNodeOverrideString",
                       RE::MakeFunctionArguments(std::move(refr), bool(isFemale), RE::BSFixedString(node.c_str()),
                                                 int32_t(kTexture), int32_t(kDiffuseIndex),
                                                 RE::BSFixedString(look.texture.c_str()), bool(persist)));

            if (look.color) {
                CallGlobal("AddNodeOverrideInt",
                           RE::MakeFunctionArguments(std::move(refr), bool(isFemale), RE::BSFixedString(node.c_str()),
                                                     int32_t(kTintColor), int32_t(kIndexNone),
                                                     int32_t(*look.color), bool(persist)));
            }
            if (look.alpha) {
                CallGlobal("AddNodeOverrideFloat",
                           RE::MakeFunctionArguments(std::move(refr), bool(isFemale), RE::BSFixedString(node.c_str()),
                                                     int32_t(kAlpha), int32_t(kIndexNone),
                                                     float(*look.alpha), bool(persist)));
            }
            if (look.glowColor) {
                CallGlobal("AddNodeOverrideInt",
                           RE::MakeFunctionArguments(std::move(refr), bool(isFemale), RE::BSFixedString(node.c_str()),
                                                     int32_t(kGlowColor), int32_t(kIndexNone),
                                                     int32_t(*look.glowColor), bool(persist)));
            }
            if (look.glowIntensity) {
                CallGlobal("AddNodeOverrideFloat",
                           RE::MakeFunctionArguments(std::move(refr), bool(isFemale), RE::BSFixedString(node.c_str()),
                                                     int32_t(kGlowIntensity), int32_t(kIndexNone),
                                                     float(*look.glowIntensity), bool(persist)));
            }
        }
    }

    bool ApplyToSlot(RE::Actor* actor, bool isFemale, const std::string& node, const Appearance& look) {
        if (!g_available || !actor || node.empty() || look.texture.empty()) return false;

        spdlog::debug("NiOverride: apply {:08X} female={} node=\"{}\" texture=\"{}\"",
                      actor->GetFormID(), isFemale, node, look.texture);

        WriteAppearance(actor, isFemale, node, look, true);
        Flush(actor);
        return true;
    }

    bool PreviewOnSlot(RE::Actor* actor, bool isFemale, const std::string& node, const Appearance& look) {
        if (!g_available || !actor || node.empty() || look.texture.empty()) return false;

        spdlog::debug("NiOverride: preview {:08X} node=\"{}\" texture=\"{}\"",
                      actor->GetFormID(), node, look.texture);

        // persist=false keeps this out of RaceMenu's co-save and off any re-equip.
        WriteAppearance(actor, isFemale, node, look, false);
        Flush(actor);
        return true;
    }

    bool ClearSlot(RE::Actor* actor, bool isFemale, const std::string& node) {
        if (!g_available || !actor || node.empty()) return false;

        spdlog::debug("NiOverride: clear {:08X} female={} node=\"{}\"", actor->GetFormID(), isFemale, node);

        auto* refr = static_cast<RE::TESObjectREFR*>(actor);
        // The same key set ODFHelperScript.psc clears, so a slot we release is
        // indistinguishable from one ODF released.
        const std::pair<int32_t, int32_t> keys[] = {
            {kTexture, kDiffuseIndex}, {kTintColor, kIndexNone}, {kAlpha, kIndexNone},
            {kGlowColor, kIndexNone}, {kGlowIntensity, kIndexNone},
        };
        for (const auto& [key, index] : keys) {
            CallGlobal("RemoveNodeOverride",
                       RE::MakeFunctionArguments(std::move(refr), bool(isFemale), RE::BSFixedString(node.c_str()),
                                                 int32_t(key), int32_t(index)));
        }

        Flush(actor);
        return true;
    }

    void Flush(RE::Actor* actor) {
        if (!g_available || !actor) return;
        CallGlobal("ApplyNodeOverrides", RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(actor)));
    }
}
