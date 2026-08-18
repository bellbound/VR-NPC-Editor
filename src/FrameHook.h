#pragma once

#include <functional>
#include <vector>

namespace NPCEditor {
    // A genuine once-per-frame callback on the main thread.
    //
    // SKSE's task queue is not one-task-per-frame. The drain loop keeps popping until the
    // queue is empty, so a task added from inside a task is picked up by the same drain,
    // in the same frame. Two things follow, and this mod hit both:
    //
    //   - Work that re-queues itself unconditionally never gives the frame back. That is
    //     what froze the game when the body menu opened: its tick queued the next tick,
    //     which queued the next tick, on the main thread, forever.
    //   - Work "chunked" across tasks does every chunk in one frame anyway, so the chunk
    //     size buys nothing. The 73-element overlay wheel was built inside two
    //     milliseconds of a single frame, which is the stall on first open.
    //
    // Both want a signal that only arrives once the game has actually run a frame, which
    // is the hook below - the same main update VR Editor hooks for its frame listeners.
    class FrameHook {
    public:
        static FrameHook* GetSingleton();

        // Call once from SKSEPluginLoad. Returns false if the hook could not be written,
        // in which case everything still runs, just without the per-frame spacing.
        static bool Install();
        static bool IsInstalled();

        // Runs `work` once, on the next frame. Without the hook this degrades to an SKSE
        // task: the work still happens, it just is not spread across frames.
        void NextFrame(std::function<void()> work);

        // Runs `work` every frame until Unregister(owner). One callback per owner;
        // registering the same owner twice replaces the first.
        void Register(const void* owner, std::function<void()> work);
        void Unregister(const void* owner);

    private:
        FrameHook() = default;
        FrameHook(const FrameHook&) = delete;
        FrameHook& operator=(const FrameHook&) = delete;

        static void OnMainThreadUpdate();
        void Update();

        static inline REL::Relocation<decltype(OnMainThreadUpdate)> s_originalFunc;
        static inline bool s_installed = false;

        struct Listener {
            const void* owner;
            std::function<void()> work;
        };

        std::vector<Listener> m_listeners;
        std::vector<std::function<void()>> m_pending;
    };
}
