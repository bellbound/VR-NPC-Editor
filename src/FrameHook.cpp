#include "FrameHook.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace NPCEditor {
    FrameHook* FrameHook::GetSingleton() {
        static FrameHook instance;
        return &instance;
    }

    bool FrameHook::IsInstalled() { return s_installed; }

    bool FrameHook::Install() {
        if (s_installed) return true;

        // The game's main update, called once a frame on the main thread. RelocationID
        // resolves the function across runtimes; the call site inside it sits at a
        // different offset on each, hence the second table. Taken from VR Editor, which
        // has been driving its frame listeners off this same call site.
        SKSE::AllocTrampoline(1 << 4);
        auto& trampoline = SKSE::GetTrampoline();

        REL::Relocation<std::uintptr_t> mainLoop{REL::RelocationID(35565, 36564)};
        const auto hookOffset = REL::Relocate(0x748, 0xc26, 0x7ee);

        s_originalFunc = trampoline.write_call<5>(mainLoop.address() + hookOffset,
                                                  &FrameHook::OnMainThreadUpdate);
        s_installed = true;

        spdlog::info("FrameHook: installed at {:x} + 0x{:x}", mainLoop.address(), hookOffset);
        return true;
    }

    void FrameHook::OnMainThreadUpdate() {
        s_originalFunc();
        GetSingleton()->Update();
    }

    void FrameHook::Update() {
        // Swapped out before running: a one-shot that asks for another frame must land on
        // the next one, not extend this one. That distinction is the whole point of the
        // hook, so losing it here would put the stall straight back.
        if (!m_pending.empty()) {
            std::vector<std::function<void()>> due;
            due.swap(m_pending);
            for (auto& work : due) work();
        }

        // Copied, because a listener may unregister itself - or the other one - from
        // inside its callback, and the still-registered check keeps a callback from
        // running after the same pass has removed it.
        const auto listeners = m_listeners;
        for (const auto& listener : listeners) {
            const bool live = std::any_of(m_listeners.begin(), m_listeners.end(),
                                          [&](const Listener& l) { return l.owner == listener.owner; });
            if (live) listener.work();
        }
    }

    void FrameHook::NextFrame(std::function<void()> work) {
        if (!work) return;

        if (!s_installed) {
            SKSE::GetTaskInterface()->AddTask(std::move(work));
            return;
        }
        m_pending.push_back(std::move(work));
    }

    void FrameHook::Register(const void* owner, std::function<void()> work) {
        if (!owner || !work) return;

        Unregister(owner);
        m_listeners.push_back({owner, std::move(work)});

        if (!s_installed) {
            spdlog::error("FrameHook: not installed, per-frame work for {} will never run", owner);
        }
    }

    void FrameHook::Unregister(const void* owner) {
        std::erase_if(m_listeners, [owner](const Listener& l) { return l.owner == owner; });
    }
}
