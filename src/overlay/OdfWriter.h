#pragma once

#include <string>

namespace Overlay {
    class StateManager;

    // Mirrors the live overlay assignments into a single Overlay Distribution Framework
    // rule file, so choices survive a restart. ODF reads these at game start only.
    //
    // The file is owned exclusively by this mod and rewritten whole; no other mod's rule
    // file is ever read or touched.
    namespace OdfWriter {
        const std::string& GetOutputPath();

        // Rewrites the rule file from the current state. Returns false and logs on any
        // failure - persistence is best-effort and never blocks an in-game change.
        bool WriteAll(const StateManager& state);
    }
}
