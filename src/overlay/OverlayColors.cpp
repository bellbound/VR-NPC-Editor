#include "overlay/OverlayColors.h"

namespace NPCEditor::Overlay {
    namespace {
        // Keep in step with PALETTE in assets/icons/make_swatches.py - `name` is the join
        // between this table and the generated textures.
        const std::vector<ColorSwatch> kPalette = {
            {"default",        L"Pack default",      0x000000, 1.0f, true},

            {"skin_pale",      L"Pale skin",         0xf7e0cb, 1.0f, false},
            {"skin_pink",      L"Pink skin",         0xedb8a6, 1.0f, false},
            {"skin_normal",    L"Normal skin",       0xc68642, 1.0f, false},
            {"skin_dark",      L"Dark skin",         0x6f4327, 1.0f, false},
            {"blush",          L"Blush",             0xf9cbdb, 1.0f, false},
            {"petal",          L"Petal",             0xf2a9c4, 1.0f, false},
            {"orchid",         L"Orchid",            0xe0a9d5, 1.0f, false},
            {"white",          L"White",             0xf2f2f2, 1.0f, false},
            {"ash",            L"Ash",               0x8c8c8c, 1.0f, false},
            {"black",          L"Black",             0x1a1a1a, 1.0f, false},
            {"crimson",        L"Crimson",           0xc0392b, 1.0f, false},
            {"amber",          L"Amber",             0xe67e22, 1.0f, false},
            {"gold",           L"Gold",              0xf1c40f, 1.0f, false},
            {"moss",           L"Moss",              0x4f8f3a, 1.0f, false},
            {"teal",           L"Teal",              0x16a085, 1.0f, false},
            {"azure",          L"Azure",             0x2980b9, 1.0f, false},
            {"indigo",         L"Indigo",            0x5b4b8a, 1.0f, false},
            {"violet",         L"Violet",            0x8e44ad, 1.0f, false},
            {"rose",           L"Rose",              0xd6336c, 1.0f, false},

            {"skin_pale_70",   L"Pale skin, 70%",    0xf7e0cb, 0.7f, false},
            {"skin_pink_70",   L"Pink skin, 70%",    0xedb8a6, 0.7f, false},
            {"skin_normal_70", L"Normal skin, 70%",  0xc68642, 0.7f, false},
            {"skin_dark_70",   L"Dark skin, 70%",    0x6f4327, 0.7f, false},
            {"blush_70",       L"Blush, 70%",        0xf9cbdb, 0.7f, false},
            {"petal_70",       L"Petal, 70%",        0xf2a9c4, 0.7f, false},
            {"orchid_70",      L"Orchid, 70%",       0xe0a9d5, 0.7f, false},
            {"white_70",       L"White, 70%",        0xf2f2f2, 0.7f, false},
            {"ash_70",         L"Ash, 70%",          0x8c8c8c, 0.7f, false},
            {"black_70",       L"Black, 70%",        0x1a1a1a, 0.7f, false},
            {"crimson_70",     L"Crimson, 70%",      0xc0392b, 0.7f, false},
            {"amber_70",       L"Amber, 70%",        0xe67e22, 0.7f, false},
            {"gold_70",        L"Gold, 70%",         0xf1c40f, 0.7f, false},
            {"moss_70",        L"Moss, 70%",         0x4f8f3a, 0.7f, false},
            {"teal_70",        L"Teal, 70%",         0x16a085, 0.7f, false},
            {"azure_70",       L"Azure, 70%",        0x2980b9, 0.7f, false},
            {"indigo_70",      L"Indigo, 70%",       0x5b4b8a, 0.7f, false},
            {"violet_70",      L"Violet, 70%",       0x8e44ad, 0.7f, false},
            {"rose_70",        L"Rose, 70%",         0xd6336c, 0.7f, false},
        };
    }

    const std::vector<ColorSwatch>& GetPalette() { return kPalette; }

    std::string SwatchTexture(const ColorSwatch& swatch) {
        return std::string("textures\\VRNPCEditor\\swatch_") + swatch.name + ".dds";
    }

    Skee::Appearance Tint(const Skee::Appearance& appearance, const ColorSwatch& swatch) {
        Skee::Appearance tinted = appearance;
        if (swatch.isDefault) return tinted;

        tinted.color = swatch.rgb;
        tinted.alpha = swatch.alpha;
        return tinted;
    }
}
