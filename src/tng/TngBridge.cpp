#include "tng/TngBridge.h"

#include <mutex>
#include <string_view>

#include <spdlog/spdlog.h>

#include "papyrus/PapyrusInterface.h"

namespace NPCEditor::Tng {
    namespace {
        // TNG's Papyrus surface. Every native this bridge calls is a global on it.
        constexpr const char* kScript = "TNG_PapyrusUtil";
        constexpr const char* kDll = "TheNewGentleman.dll";

        // The two entries TNG puts in front of the real addons, and what they mean to
        // SetActorAddon. Their strings are MCM translation keys rather than names.
        constexpr int kEntryDefault = 0;
        constexpr int kEntryNone = 1;
        constexpr int kPseudoEntries = 2;

        bool g_available = false;
        std::string g_status = "not checked";

        // What PrimeActor asked for and what has come back.
        //
        // File-static and mutex-guarded because the answers arrive on the VM thread
        // while the menu polls on the game thread. Each `...Done` flag means "this
        // question will not be answered again", which is not the same as "it was
        // answered" - a dispatch the VM refused sets it too, because its callback is
        // then guaranteed never to fire and waiting for it would hang the row forever.
        struct Capture {
            std::mutex mutex;
            std::uint64_t generation = 0;

            bool modifiableDone = false;
            bool entriesDone = false;
            bool currentDone = false;
            bool sizeDone = false;

            std::optional<int32_t> canModify;
            std::vector<std::string> entries;
            std::string currentName;
            std::optional<int32_t> size;

            // Field by field rather than `*this = {}`: the mutex above makes the
            // struct neither copyable nor movable, and it is held while this runs.
            void Reset(std::uint64_t newGeneration) {
                generation = newGeneration;
                modifiableDone = false;
                entriesDone = false;
                currentDone = false;
                sizeDone = false;
                canModify.reset();
                entries.clear();
                currentName.clear();
                size.reset();
            }
        };

        Capture g_capture;

        // TNG suffixes an addon's name with " (d)" or " (s)" on a race group that is
        // not the main one. The suffix belongs in the label but not in a comparison
        // against the armour's own name, which never carries it.
        std::string_view StripGroupSuffix(std::string_view name) {
            for (const auto* suffix : {" (d)", " (s)"}) {
                if (name.ends_with(suffix)) {
                    return name.substr(0, name.size() - 4);
                }
            }
            return name;
        }

        // Where the actor currently sits in `entries`.
        //
        // TNG exposes no per-actor index getter - GetActorAddon hands back the Armor
        // form - so the only route from one to the other is the name. Duplicate
        // display names would land on the first match; TNG's own MCM list has exactly
        // the same ambiguity.
        int ResolveIndex(const std::vector<std::string>& entries, const std::string& currentName) {
            if (currentName.empty()) {
                // No addon on the actor. That is a real state, not a failure, and it
                // is what "No genital" means - so the spinner opens on what is
                // actually there rather than on Default.
                return kEntryNone;
            }
            for (size_t i = kPseudoEntries; i < entries.size(); ++i) {
                if (StripGroupSuffix(entries[i]) == currentName) {
                    return static_cast<int>(i);
                }
            }
            return kEntryDefault;
        }

        std::wstring Widen(std::string_view text) { return std::wstring(text.begin(), text.end()); }
    }  // namespace

    const wchar_t* SizeLabel(int category) {
        // TNG's own MCM names, which are translation keys there ($TNG_SXS and friends).
        // 3DUI renders a $ string verbatim, so they are spelled out here instead.
        switch (category) {
            case 0:  return L"X-Small";
            case 1:  return L"Small";
            case 2:  return L"Medium";
            case 3:  return L"Large";
            case 4:  return L"X-Large";
            default: return L"";
        }
    }

    void Initialize() {
        // Two independent signals, because a partial install fails one and not the
        // other: the DLL can be present with the scripts left uninstalled, and the
        // scripts can be present with the DLL blocked from loading.
        const bool dll = GetModuleHandleA(kDll) != nullptr;

        bool script = false;
        if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
            RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;
            // Resolving the type loads the .pex if nothing has touched it yet, which
            // is exactly the question being asked - hence kDataLoaded and not earlier.
            script = vm->GetScriptObjectType(RE::BSFixedString(kScript), typeInfo) &&
                     static_cast<bool>(typeInfo);
        }

        g_available = dll && script;
        if (g_available) {
            g_status = "TNG_PapyrusUtil and TheNewGentleman.dll both present";
        } else if (dll) {
            g_status = "TheNewGentleman.dll is loaded but TNG_PapyrusUtil did not resolve";
        } else if (script) {
            g_status = "TNG_PapyrusUtil resolved but TheNewGentleman.dll is not loaded";
        } else {
            g_status = "not installed";
        }
        spdlog::info("TNG: {}", g_status);
    }

    bool IsAvailable() { return g_available; }

    const std::string& GetStatus() { return g_status; }

    void PrimeActor(RE::Actor* actor, std::uint64_t generation) {
        if (!g_available || !actor) {
            return;
        }

        {
            std::lock_guard lock(g_capture.mutex);
            g_capture.Reset(generation);
        }

        auto* papyrus = Papyrus::PapyrusInterface::GetSingleton();

        // The cast is load-bearing: it picks the RE::Actor* alternative of the
        // argument variant, which packs the form as the Papyrus class "Actor". A bare
        // TESForm* takes a different path and TNG would receive something it cannot
        // bind.
        const std::vector<Papyrus::PapyrusValue> args{static_cast<RE::Actor*>(actor)};

        // All four at once - they are independent, and serialising them would cost four
        // round-trips of latency for nothing.
        const bool sentModifiable = papyrus->CallGlobalFunctionOptInt(
            kScript, "CanModifyActor", args, [generation](std::optional<int32_t> result) {
                std::lock_guard lock(g_capture.mutex);
                if (g_capture.generation != generation) {
                    return;  // an answer for an NPC the menu has already left
                }
                g_capture.canModify = result;
                g_capture.modifiableDone = true;
            });

        const bool sentEntries = papyrus->CallGlobalFunctionStringArray(
            kScript, "GetActorAddons", args,
            [generation](const std::vector<std::string>& entries) {
                std::lock_guard lock(g_capture.mutex);
                if (g_capture.generation != generation) {
                    return;
                }
                g_capture.entries = entries;
                g_capture.entriesDone = true;
            });

        const bool sentCurrent = papyrus->CallGlobalFunctionForm(
            kScript, "GetActorAddon", args, [generation](RE::TESForm* form) {
                // The name is copied out here rather than the pointer being kept: this
                // runs on the VM thread, and the menu reads it frames later.
                std::string name;
                if (form && form->GetName()) {
                    name = form->GetName();
                }
                std::lock_guard lock(g_capture.mutex);
                if (g_capture.generation != generation) {
                    return;
                }
                g_capture.currentName = std::move(name);
                g_capture.currentDone = true;
            });

        const bool sentSize = papyrus->CallGlobalFunctionOptInt(
            kScript, "GetActorSize", args, [generation](std::optional<int32_t> result) {
                std::lock_guard lock(g_capture.mutex);
                if (g_capture.generation != generation) {
                    return;
                }
                g_capture.size = result;
                g_capture.sizeDone = true;
            });

        // A refused dispatch never reaches the VM, so its callback is guaranteed not
        // to fire. Marking it done now is the difference between the row staying
        // hidden and the poll spinning until its timeout.
        if (!sentModifiable || !sentEntries || !sentCurrent || !sentSize) {
            spdlog::warn("TNG: dispatch refused (modifiable={} entries={} current={} size={})",
                         sentModifiable, sentEntries, sentCurrent, sentSize);
            std::lock_guard lock(g_capture.mutex);
            if (g_capture.generation == generation) {
                g_capture.modifiableDone = g_capture.modifiableDone || !sentModifiable;
                g_capture.entriesDone = g_capture.entriesDone || !sentEntries;
                g_capture.currentDone = g_capture.currentDone || !sentCurrent;
                g_capture.sizeDone = g_capture.sizeDone || !sentSize;
            }
        }
    }

    std::optional<AddonState> Collect(std::uint64_t generation) {
        std::lock_guard lock(g_capture.mutex);

        if (g_capture.generation != generation) {
            return std::nullopt;
        }
        if (!g_capture.modifiableDone || !g_capture.entriesDone || !g_capture.currentDone ||
            !g_capture.sizeDone) {
            return std::nullopt;
        }

        AddonState state;
        // nullopt means the VM handed back something that was not an Int, which is not
        // the same as TNG saying no - but the honest response to both is to offer
        // nothing.
        state.rawCanModify = g_capture.canModify;
        state.modifiable = g_capture.canModify.has_value() && *g_capture.canModify > 0;
        state.entries = g_capture.entries;
        state.index = state.modifiable ? ResolveIndex(state.entries, g_capture.currentName) : 0;

        // TNG answers -1 when it cannot size this actor, and nullopt when the VM handed
        // back something that was not an Int. Both mean there is no size to show.
        if (state.modifiable && g_capture.size && *g_capture.size >= 0 &&
            *g_capture.size < kSizeCategories) {
            state.size = *g_capture.size;
        }
        return state;
    }

    void SetSize(RE::Actor* actor, int category) {
        if (!g_available || !actor) {
            return;
        }

        spdlog::debug("TNG: SetActorSize({}) on '{}'", category, actor->GetName());
        Papyrus::PapyrusInterface::GetSingleton()->CallGlobalFunction(
            kScript, "SetActorSize", {static_cast<RE::Actor*>(actor), category});

        // No NiNode update here, unlike SetAddon: TNG rescales the node it already has
        // rather than swapping the actor's skin, and its own MCM does not queue one
        // after a size change either.
    }

    void SetAddon(RE::Actor* actor, int choice) {
        if (!g_available || !actor) {
            return;
        }

        auto* papyrus = Papyrus::PapyrusInterface::GetSingleton();

        spdlog::debug("TNG: SetActorAddon({}) on '{}'", choice, actor->GetName());
        papyrus->CallGlobalFunction(kScript, "SetActorAddon",
                                    {static_cast<RE::Actor*>(actor), choice});

        // TNG does not refresh the model on this path - its only internal
        // QueueNiNodeUpdate is in UpdateActor, which SetActorAddon does not reach - so
        // its own MCM calls this afterwards and so must we. Skipped on a mount, where
        // the update is what makes the actor pop; TNG skips it there for the same
        // reason.
        if (!actor->IsOnMount()) {
            papyrus->CallMethod(actor, "Actor", "QueueNiNodeUpdate", {});
        }
    }

    std::wstring EntryLabel(const std::vector<std::string>& entries, int index) {
        if (index < 0 || static_cast<size_t>(index) >= entries.size()) {
            return L"";
        }
        // TNG's first two entries are MCM translation keys ("$TNG_TRS", "$TNG_TNT").
        // 3DUI does not resolve $ strings - it would render the key verbatim - so they
        // are named here instead.
        if (index == kEntryDefault) {
            return L"Default";
        }
        if (index == kEntryNone) {
            return L"No genital";
        }
        return Widen(entries[static_cast<size_t>(index)]);
    }

}  // namespace NPCEditor::Tng
