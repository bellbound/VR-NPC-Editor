#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Thin wrapper over The New Gentleman's Papyrus surface.
//
// Unlike OBody, TNG ships no C++ interface at all: its plugin registers an SKSE
// message listener and exports only GetPluginVersion, so everything has to go
// through the 24 global natives on the hidden script TNG_PapyrusUtil.
//
// That makes this bridge shaped differently from ObodyBridge. A Papyrus call is
// dispatched now and answered a frame or more later, on the VM thread, so a read
// cannot be a function that returns a value - it is PrimeActor() to ask and
// Collect() to poll. Nothing here ever blocks: waiting on the game thread for a VM
// answer deadlocks the VM, because the game thread is what pumps it.
//
// This is only affordable because the body menu does not pause the game. It is a
// 3DUI overlay opened by a HIGGS grab, so the VM keeps running while it is open and
// answers land within a frame or two.
namespace NPCEditor::Tng {

    // Probes for TheNewGentleman.dll and the TNG_PapyrusUtil script type.
    // Must run at kDataLoaded: resolving the script type loads its .pex, and before
    // kDataLoaded there is nothing to load it from.
    void Initialize();

    bool IsAvailable();

    // Human-readable version of what Initialize() found, for the health-check log.
    const std::string& GetStatus();

    // Everything one actor's spinner needs, once the VM has answered for all of it.
    struct AddonState {
        // CanModifyActor came back > 0. Note *greater than*, not >= 0: TNG returns
        // resOkFixed (0) for a race it can size but whose addon is fixed, and every
        // set on such an actor is rejected. TNG's own MCM gates its addon list the
        // same way.
        bool modifiable = false;

        // What CanModifyActor actually said, kept for the log: "not modifiable" covers
        // a fixed race (0), an unsupported one (-1), a missing base (-2) and a VM that
        // answered with something that was not an Int (nullopt), and those want telling
        // apart when a row does not appear.
        std::optional<int32_t> rawCanModify;

        // TNG's list verbatim, including the two leading pseudo-entries:
        //   [0] "$TNG_TRS" -> reset to default   [1] "$TNG_TNT" -> no genital
        // so a real addon starts at index 2 and `choice = index - 2`.
        std::vector<std::string> entries;

        // Where in `entries` the actor currently sits.
        int index = 0;

        // The actor's size category, TNG's own 0-4 for X-Small to X-Large. Empty when
        // TNG could not answer - it returns -1 for that - which is what hides the size
        // stepper rather than opening it on a made-up value.
        std::optional<int> size;
    };

    // The five size categories, in TNG's order.
    constexpr int kSizeCategories = 5;
    const wchar_t* SizeLabel(int category);

    // Dispatches the three reads for `actor`, discarding anything still in flight
    // from a previous call. `generation` is echoed back through every callback so a
    // late answer for a previous NPC cannot be mistaken for this one's.
    void PrimeActor(RE::Actor* actor, std::uint64_t generation);

    // Non-blocking poll. Returns a value only once every answer for `generation`
    // has landed (or been established as never coming).
    std::optional<AddonState> Collect(std::uint64_t generation);

    // Fire-and-forget, `category` in 0-4. TNG also takes -2 for "back to the default
    // for this race" and -1 for "leave it alone"; neither is offered here, because a
    // stepper that walks five sizes and two words is a stepper you have to read.
    void SetSize(RE::Actor* actor, int category);

    // Fire-and-forget. `choice` is TNG's own convention:
    //   -3 player default, -2 default, -1 no genital, 0..n the addon at entries[n+2]
    // Also queues the NiNode update TNG's own MCM performs after a set.
    void SetAddon(RE::Actor* actor, int choice);

    // The label to show for an entry, with TNG's untranslatable MCM keys replaced.
    std::wstring EntryLabel(const std::vector<std::string>& entries, int index);

}  // namespace NPCEditor::Tng
