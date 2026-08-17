#include "skee/SkeeBridge.h"

#include "external/skee/IPluginInterface.h"

#include <spdlog/spdlog.h>

namespace Skee {
    namespace {
        // Shader override keys, shared with nioverride.psc. Index 0 selects the
        // diffuse map for kTexture; the other keys take kIndexNone, which is what
        // Papyrus expresses as -1.
        constexpr skee_u16 kGlowColor     = 0;
        constexpr skee_u16 kGlowIntensity = 1;
        constexpr skee_u16 kTintColor     = 7;
        constexpr skee_u16 kAlpha         = 8;
        constexpr skee_u16 kTexture       = 9;
        constexpr skee_u8  kDiffuseIndex  = 0;
        constexpr skee_u8  kIndexNone     = 0xFF;

        IOverlayInterface*   g_overlay  = nullptr;
        IOverrideInterface*  g_override = nullptr;
        IActorUpdateManager* g_updates  = nullptr;

        // The SKEE header forward-declares game types in the global namespace, so
        // its pointers are opaque handles as far as CommonLibSSE is concerned.
        ::TESObjectREFR* AsRefr(RE::Actor* actor) {
            return reinterpret_cast<::TESObjectREFR*>(actor);
        }
        ::NiAVObject* AsNiObject(RE::NiAVObject* object) {
            return reinterpret_cast<::NiAVObject*>(object);
        }

        IOverlayInterface::OverlayLocation ToSkeeLocation(Location loc) {
            switch (loc) {
                case Location::Hand: return IOverlayInterface::OverlayLocation::Hand;
                case Location::Feet: return IOverlayInterface::OverlayLocation::Feet;
                case Location::Face: return IOverlayInterface::OverlayLocation::Face;
                case Location::Body:
                default:             return IOverlayInterface::OverlayLocation::Body;
            }
        }

        // RaceMenu's own node-name pattern, e.g. "Body [Ovl%d]".
        const char* GetOverlayFormatSafe(Location loc) {
            if (!g_overlay) return nullptr;
            return g_overlay->GetOverlayFormat(IOverlayInterface::OverlayType::Normal, ToSkeeLocation(loc));
        }

        class StringSetter : public IOverrideInterface::SetVariant {
        public:
            explicit StringSetter(const char* value) : m_value(value) {}
            Type GetType() override { return Type::String; }
            const char* String() override { return m_value; }
        private:
            const char* m_value;
        };

        class IntSetter : public IOverrideInterface::SetVariant {
        public:
            explicit IntSetter(skee_i32 value) : m_value(value) {}
            Type GetType() override { return Type::Int; }
            skee_i32 Int() override { return m_value; }
        private:
            skee_i32 m_value;
        };

        class FloatSetter : public IOverrideInterface::SetVariant {
        public:
            explicit FloatSetter(float value) : m_value(value) {}
            Type GetType() override { return Type::Float; }
            float Float() override { return m_value; }
        private:
            float m_value;
        };

        // Captures whichever variant SKEE happens to hand back for a property.
        class ValueGetter : public IOverrideInterface::GetVariant {
        public:
            void Int(const skee_i32 i) override { intValue = i; }
            void Float(const float f) override { floatValue = f; }
            void String(const char* str) override { if (str) stringValue = str; }
            void Bool(const bool b) override { intValue = b ? 1 : 0; }
            void TextureSet(const BGSTextureSet*) override {}

            std::optional<skee_i32> intValue;
            std::optional<float> floatValue;
            std::optional<std::string> stringValue;
        };

        // The actor's third-person 3D, which ApplyNodeOverrides needs. Null while the
        // actor is unloaded - every caller treats that as "skip, do not crash".
        RE::NiAVObject* GetActor3D(RE::Actor* actor) {
            return actor ? actor->Get3D(false) : nullptr;
        }

        void ApplyAppearance(::TESObjectREFR* refr, bool isFemale, const char* node, const Appearance& look,
                             bool persistent) {
            const auto set = [&](skee_u16 key, skee_u8 index, IOverrideInterface::SetVariant& value) {
                if (persistent) {
                    g_override->AddNodeOverride(refr, isFemale, node, key, index, value);
                } else {
                    g_override->SetNodeProperty(refr, false, node, key, index, value, true);
                }
            };

            StringSetter texture(look.texture.c_str());
            set(kTexture, kDiffuseIndex, texture);

            if (look.color) {
                IntSetter tint(static_cast<skee_i32>(*look.color));
                set(kTintColor, kIndexNone, tint);
            }
            if (look.alpha) {
                FloatSetter alpha(*look.alpha);
                set(kAlpha, kIndexNone, alpha);
            }
            if (look.glowColor) {
                IntSetter glow(static_cast<skee_i32>(*look.glowColor));
                set(kGlowColor, kIndexNone, glow);
            }
            if (look.glowIntensity) {
                FloatSetter intensity(*look.glowIntensity);
                set(kGlowIntensity, kIndexNone, intensity);
            }
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

    bool IsAvailable() {
        return g_overlay && g_override && g_updates;
    }

    bool Initialize() {
        if (IsAvailable()) return true;

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            spdlog::error("SKEE: no SKSE messaging interface, cannot exchange interfaces");
            return false;
        }

        InterfaceExchangeMessage message;
        messaging->Dispatch(InterfaceExchangeMessage::kMessage_ExchangeInterface, &message, sizeof(message), "skee");

        if (!message.interfaceMap) {
            spdlog::warn("SKEE: interface exchange returned no map - RaceMenu (skeevr.dll) not loaded yet?");
            return false;
        }

        g_overlay  = static_cast<IOverlayInterface*>(message.interfaceMap->QueryInterface("Overlay"));
        g_override = static_cast<IOverrideInterface*>(message.interfaceMap->QueryInterface("Override"));
        g_updates  = static_cast<IActorUpdateManager*>(message.interfaceMap->QueryInterface("ActorUpdateManager"));

        spdlog::info("SKEE: Overlay={} Override={} ActorUpdateManager={}",
                     g_overlay ? "ok" : "MISSING",
                     g_override ? "ok" : "MISSING",
                     g_updates ? "ok" : "MISSING");

        if (!IsAvailable()) {
            spdlog::error("SKEE: required interfaces missing - overlay features disabled");
            g_overlay = nullptr;
            g_override = nullptr;
            g_updates = nullptr;
            return false;
        }

        spdlog::info("SKEE: interface versions Overlay={} Override={}", g_overlay->GetVersion(), g_override->GetVersion());
        for (auto loc : {Location::Body, Location::Hand, Location::Feet, Location::Face}) {
            const char* format = GetOverlayFormatSafe(loc);
            spdlog::info("SKEE: {} slots={} format=\"{}\"", LocationName(loc), GetSlotCount(loc), format ? format : "(null)");
        }
        return true;
    }

    uint32_t GetSlotCount(Location loc) {
        if (!g_overlay) return 0;
        return g_overlay->GetOverlayCount(IOverlayInterface::OverlayType::Normal, ToSkeeLocation(loc));
    }

    std::string GetNodeName(Location loc, uint32_t index) {
        const char* format = GetOverlayFormatSafe(loc);
        if (!format) return {};

        // The format string is RaceMenu's own, e.g. "Body [Ovl%d]".
        char buffer[128];
        const int written = std::snprintf(buffer, sizeof(buffer), format, static_cast<int>(index));
        if (written <= 0 || written >= static_cast<int>(sizeof(buffer))) {
            spdlog::warn("SKEE: could not format node name from \"{}\" index {}", format, index);
            return {};
        }
        return buffer;
    }

    bool EnsureOverlays(RE::Actor* actor) {
        if (!IsAvailable() || !actor) return false;

        auto* refr = AsRefr(actor);
        if (g_overlay->HasOverlays(refr)) return true;

        // bPlayerOnly=1 in skeevr.ini means NPCs get no overlay nodes until asked.
        spdlog::debug("SKEE: installing overlay nodes on {:08X}", actor->GetFormID());
        g_overlay->AddOverlays(refr, false);
        return g_overlay->HasOverlays(refr);
    }

    bool IsSlotOccupied(RE::Actor* actor, bool isFemale, const std::string& node) {
        if (!IsAvailable() || !actor || node.empty()) return false;
        return g_override->HasNodeOverride(AsRefr(actor), isFemale, node.c_str(), kTexture, kDiffuseIndex);
    }

    std::optional<std::string> GetSlotTexture(RE::Actor* actor, bool isFemale, const std::string& node) {
        if (!IsAvailable() || !actor || node.empty()) return std::nullopt;

        ValueGetter getter;
        if (!g_override->GetNodeOverride(AsRefr(actor), isFemale, node.c_str(), kTexture, kDiffuseIndex, getter)) {
            return std::nullopt;
        }
        return getter.stringValue;
    }

    std::optional<std::string> FindFreeSlot(RE::Actor* actor, bool isFemale, Location loc) {
        const uint32_t count = GetSlotCount(loc);
        for (uint32_t i = 0; i < count; ++i) {
            auto node = GetNodeName(loc, i);
            if (node.empty()) continue;
            if (!IsSlotOccupied(actor, isFemale, node)) return node;
        }
        spdlog::warn("SKEE: no free {} slot on {:08X} ({} in use)", LocationName(loc), actor ? actor->GetFormID() : 0, count);
        return std::nullopt;
    }

    std::vector<std::string> GetOccupiedSlots(RE::Actor* actor, bool isFemale, Location loc) {
        std::vector<std::string> occupied;
        const uint32_t count = GetSlotCount(loc);
        for (uint32_t i = 0; i < count; ++i) {
            auto node = GetNodeName(loc, i);
            if (!node.empty() && IsSlotOccupied(actor, isFemale, node)) occupied.push_back(std::move(node));
        }
        return occupied;
    }

    bool ApplyToSlot(RE::Actor* actor, bool isFemale, const std::string& node, const Appearance& look) {
        if (!IsAvailable() || !actor || node.empty() || look.texture.empty()) return false;

        spdlog::debug("SKEE: apply {:08X} female={} node=\"{}\" texture=\"{}\"",
                      actor->GetFormID(), isFemale, node, look.texture);

        ApplyAppearance(AsRefr(actor), isFemale, node.c_str(), look, true);
        Flush(actor);
        return true;
    }

    bool ClearSlot(RE::Actor* actor, bool isFemale, const std::string& node) {
        if (!IsAvailable() || !actor || node.empty()) return false;

        spdlog::debug("SKEE: clear {:08X} female={} node=\"{}\"", actor->GetFormID(), isFemale, node);

        auto* refr = AsRefr(actor);
        // The same key set ODFHelperScript.psc clears, so a slot we release looks
        // identical to one ODF released.
        g_override->RemoveNodeOverride(refr, isFemale, node.c_str(), kTexture, kDiffuseIndex);
        g_override->RemoveNodeOverride(refr, isFemale, node.c_str(), kTintColor, kIndexNone);
        g_override->RemoveNodeOverride(refr, isFemale, node.c_str(), kAlpha, kIndexNone);
        g_override->RemoveNodeOverride(refr, isFemale, node.c_str(), kGlowColor, kIndexNone);
        g_override->RemoveNodeOverride(refr, isFemale, node.c_str(), kGlowIntensity, kIndexNone);

        // Removing the override leaves the shader as it was, so reset the node and
        // restamp whatever overrides remain.
        g_overlay->RevertOverlay(refr, node.c_str(), 0, 0, true, false);
        Flush(actor);
        return true;
    }

    SlotSnapshot SnapshotSlot(RE::Actor* actor, const std::string& node) {
        SlotSnapshot snapshot;
        snapshot.node = node;
        if (!IsAvailable() || !actor || node.empty()) return snapshot;

        auto* refr = AsRefr(actor);
        const auto read = [&](skee_u16 key, skee_u8 index, ValueGetter& getter) {
            return g_override->GetNodeProperty(refr, false, node.c_str(), key, index, getter);
        };

        ValueGetter texture, color, alpha, glowColor, glowIntensity;
        if (read(kTexture, kDiffuseIndex, texture)) snapshot.texture = texture.stringValue;
        if (read(kTintColor, kIndexNone, color)) snapshot.color = color.intValue;
        if (read(kAlpha, kIndexNone, alpha)) snapshot.alpha = alpha.floatValue;
        if (read(kGlowColor, kIndexNone, glowColor)) snapshot.glowColor = glowColor.intValue;
        if (read(kGlowIntensity, kIndexNone, glowIntensity)) snapshot.glowIntensity = glowIntensity.floatValue;

        return snapshot;
    }

    bool PreviewOnSlot(RE::Actor* actor, const std::string& node, const Appearance& look) {
        if (!IsAvailable() || !actor || node.empty() || look.texture.empty()) return false;

        spdlog::debug("SKEE: preview {:08X} node=\"{}\" texture=\"{}\"", actor->GetFormID(), node, look.texture);

        // isFemale is irrelevant here: SetNodeProperty writes the live shader, not the
        // gendered override database.
        ApplyAppearance(AsRefr(actor), false, node.c_str(), look, false);
        return true;
    }

    bool RestoreSlot(RE::Actor* actor, const SlotSnapshot& snapshot) {
        if (!IsAvailable() || !actor || snapshot.node.empty()) return false;

        spdlog::debug("SKEE: restore {:08X} node=\"{}\"", actor->GetFormID(), snapshot.node);

        auto* refr = AsRefr(actor);
        const char* node = snapshot.node.c_str();

        // A slot with no recorded texture was empty before the preview; reverting the
        // node puts back RaceMenu's transparent default rather than leaving the preview on.
        if (!snapshot.texture || snapshot.texture->empty()) {
            g_overlay->RevertOverlay(refr, node, 0, 0, true, false);
            Flush(actor);
            return true;
        }

        StringSetter texture(snapshot.texture->c_str());
        g_override->SetNodeProperty(refr, false, node, kTexture, kDiffuseIndex, texture, true);

        if (snapshot.color) {
            IntSetter value(*snapshot.color);
            g_override->SetNodeProperty(refr, false, node, kTintColor, kIndexNone, value, true);
        }
        if (snapshot.alpha) {
            FloatSetter value(*snapshot.alpha);
            g_override->SetNodeProperty(refr, false, node, kAlpha, kIndexNone, value, true);
        }
        if (snapshot.glowColor) {
            IntSetter value(*snapshot.glowColor);
            g_override->SetNodeProperty(refr, false, node, kGlowColor, kIndexNone, value, true);
        }
        if (snapshot.glowIntensity) {
            FloatSetter value(*snapshot.glowIntensity);
            g_override->SetNodeProperty(refr, false, node, kGlowIntensity, kIndexNone, value, true);
        }
        return true;
    }

    void Flush(RE::Actor* actor) {
        if (!IsAvailable() || !actor) return;

        auto* object = GetActor3D(actor);
        if (!object) {
            // Unloaded actor: the database change stands, it just cannot be stamped yet.
            spdlog::debug("SKEE: {:08X} has no 3D, deferring apply to the update manager", actor->GetFormID());
            g_updates->AddNodeOverrideUpdate(actor->GetFormID());
            g_updates->Flush();
            return;
        }

        g_override->ApplyNodeOverrides(AsRefr(actor), AsNiObject(object), true);
        g_updates->AddNodeOverrideUpdate(actor->GetFormID());
        g_updates->Flush();
    }
}
