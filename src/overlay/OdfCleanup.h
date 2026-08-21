#pragma once

namespace NPCEditor::Overlay::OdfCleanup {
    // Deletes the distribution rule file and the SlaveTats mod configs earlier builds
    // wrote for it.
    //
    // RaceMenu persists an overlay itself, slot and tint included, so having ODF put the
    // same choice back at game start applied it twice - once where RaceMenu restored it
    // and once wherever ODF found a free slot, in ODF's colour rather than the chosen
    // one - and ran the actor out of overlay slots doing it. Those files go on being read
    // by ODF whether or not this mod still writes them, so they are cleared out rather
    // than merely abandoned.
    void RemoveLegacyFiles();
}
