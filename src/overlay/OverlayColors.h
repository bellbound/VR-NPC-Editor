#pragma once

#include <string>
#include <vector>

#include "skee/SkeeBridge.h"

namespace NPCEditor::Overlay {
    // The tint the wheel applies to an overlay, as one row of pickable swatches.
    //
    // Curated rather than harvested: the installed ODF packs between them declare only
    // "random-saturated" and 0x000000 across 2678 overlays, so there is no palette in the
    // data to read. Four skin tones lead, then three pale pinks, then neutrals and twelve
    // hues, all chosen to stay distinguishable from each other at arm's length in a headset.
    //
    // Every colour appears twice, at full and reduced strength. Reduced is real RaceMenu
    // alpha rather than a lighter tint, so what the swatch looks like is what the skin
    // gets. The row runs all the full-strength swatches first and the reduced ones after,
    // so reaching a solid colour never means scrolling past a faded one.
    struct ColorSwatch {
        const char* name;        // matches the swatch texture stem, see make_swatches.py
        const wchar_t* label;
        uint32_t rgb;            // 0xRRGGBB
        float alpha;
        bool isDefault;          // leaves the pack's own colour and alpha untouched
    };

    // Index 0 is always the "pack default" entry.
    const std::vector<ColorSwatch>& GetPalette();

    // Data-relative path of a swatch's texture.
    std::string SwatchTexture(const ColorSwatch& swatch);

    // The pack's appearance with this swatch applied. The default swatch returns it
    // unchanged, so a pack that ships a deliberate colour keeps it unless overridden.
    Skee::Appearance Tint(const Skee::Appearance& appearance, const ColorSwatch& swatch);
}
