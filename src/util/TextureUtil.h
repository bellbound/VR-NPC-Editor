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

    // Why a texture cannot be used as a swatch, or Ok. 3DUI crashes the game when it
    // loads a texture whose dimensions are not powers of two, so every swatch is checked
    // before an element is built for it.
    enum class Verdict { Ok, Missing, NotDds, BadDimensions };

    // One archive lookup for the whole question. The catalog asks it of every overlay
    // every pack declares - several thousand of them - and asking "is it there?" and
    // "is it usable?" separately opened the same file through the archive system twice.
    Verdict Probe(const std::string& dataRelativePath, uint32_t& width, uint32_t& height);

    // Returns false when the file is missing, is not a DDS, or has a non-power-of-two
    // dimension. Probe says which of those it was.
    bool IsUsableSwatch(const std::string& dataRelativePath, uint32_t& width, uint32_t& height);
}
