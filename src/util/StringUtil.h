#pragma once

#include <cctype>
#include <string_view>

namespace NPCEditor::Util {

    // Case-insensitive compare, ASCII only.
    //
    // Papyrus identifiers are case-insensitive, so the function-table walk in
    // PapyrusInterface cannot use == to match a name against what a script declares:
    // TNG writes `GetActorAddons`, another mod's script may well write `getactoraddons`,
    // and the VM considers those the same function.
    inline bool IEquals(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }

}  // namespace NPCEditor::Util
