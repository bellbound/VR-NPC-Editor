#pragma once

#include <string>

namespace TextureUtil {
    // ODF packs write filepaths in two conventions: "textures\\actors\\..." for loose
    // files and "data/textures/actors/..." for BSA-packed ones. Both mean the same
    // Data-relative texture; this collapses them to the backslash form the game and
    // 3DUI expect.
    std::string Normalize(std::string path);

    // Resolves through the archive system, so it finds loose files and BSA contents alike.
    bool Exists(const std::string& dataRelativePath);

    // 3DUI crashes the game when it loads a texture whose dimensions are not powers of
    // two, so every swatch is checked before an element is built for it. Returns false
    // when the file is missing, is not a DDS, or has a non-power-of-two dimension.
    bool IsUsableSwatch(const std::string& dataRelativePath, uint32_t& width, uint32_t& height);
}
