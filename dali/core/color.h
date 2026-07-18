#pragma once

#include <dali/core/defines.h>
#include <dali/core/math.h>

namespace kdk {

#pragma warning(push)
// Disable anon-struct warning.
#pragma warning(disable : 4201)
struct Color32 {
    union {
        struct {
            u8 R;
            u8 G;
            u8 B;
            u8 A;
        };
        u32 Bits;
    };

    static constexpr Color32 FromRGBA(u8 r, u8 g, u8 b, u8 a) {
        return Color32{.R = r, .G = g, .B = b, .A = a};
    }

    static const Color32 White;
    static const Color32 Red;
    static const Color32 Green;
    static const Color32 Blue;
    static const Color32 Magenta;
    static const Color32 Cyan;
    static const Color32 Yellow;
    static const Color32 Black;
    static const Color32 Aquamarine;
    static const Color32 BakerChocolate;
    static const Color32 BlueViolet;
    static const Color32 Brass;
    static const Color32 BrightGold;
    static const Color32 Brown;
    static const Color32 Bronze;
    static const Color32 BronzeII;
    static const Color32 CadetBlue;
    static const Color32 CoolCopper;
    static const Color32 Copper;
    static const Color32 Coral;
    static const Color32 CornFlowerBlue;
    static const Color32 DarkBrown;
    static const Color32 DarkGreen;
    static const Color32 DarkGreenCopper;
    static const Color32 DarkOliveGreen;
    static const Color32 DarkOrchid;
    static const Color32 DarkPurple;
    static const Color32 DarkSlateBlue;
    static const Color32 DarkSlateGrey;
    static const Color32 DarkTan;
    static const Color32 DarkTurquoise;
    static const Color32 DarkWood;
    static const Color32 DimGrey;
    static const Color32 DustyRose;
    static const Color32 Feldspar;
    static const Color32 Firebrick;
    static const Color32 ForestGreen;
    static const Color32 Gold;
    static const Color32 Goldenrod;
    static const Color32 Grey;
    static const Color32 GreenCopper;
    static const Color32 GreenYellow;
    static const Color32 HunterGreen;
    static const Color32 IndianRed;
    static const Color32 Khaki;
    static const Color32 LightBlue;
    static const Color32 LightGrey;
    static const Color32 LightSteelBlue;
    static const Color32 LightWood;
    static const Color32 LimeGreen;
    static const Color32 MandarianOrange;
    static const Color32 Maroon;
    static const Color32 MediumAquamarine;
    static const Color32 MediumBlue;
    static const Color32 MediumForestGreen;
    static const Color32 MediumGoldenrod;
    static const Color32 MediumOrchid;
    static const Color32 MediumSeaGreen;
    static const Color32 MediumSlateBlue;
    static const Color32 MediumSpringGreen;
    static const Color32 MediumTurquoise;
    static const Color32 MediumVioletRed;
    static const Color32 MediumWood;
    static const Color32 MidnightBlue;
    static const Color32 NavyBlue;
    static const Color32 NeonBlue;
    static const Color32 NeonPink;
    static const Color32 NewMidnightBlue;
    static const Color32 NewTan;
    static const Color32 OldGold;
    static const Color32 Orange;
    static const Color32 OrangeRed;
    static const Color32 Orchid;
    static const Color32 PaleGreen;
    static const Color32 Pink;
    static const Color32 Plum;
    static const Color32 Quartz;
    static const Color32 RichBlue;
    static const Color32 Salmon;
    static const Color32 Scarlet;
    static const Color32 SeaGreen;
    static const Color32 SemiSweetChocolate;
    static const Color32 Sienna;
    static const Color32 Silver;
    static const Color32 SkyBlue;
    static const Color32 SlateBlue;
    static const Color32 SpicyPink;
    static const Color32 SpringGreen;
    static const Color32 SteelBlue;
    static const Color32 SummerSky;
    static const Color32 Tan;
    static const Color32 Thistle;
    static const Color32 Turquoise;
    static const Color32 VeryDarkBrown;
    static const Color32 VeryLightGrey;
    static const Color32 Violet;
    static const Color32 VioletRed;
    static const Color32 Wheat;
    static const Color32 YellowGreen;
};
#pragma warning(pop)

// Drops the A component.
Vec3 ToVec3(const Color32& color);
Vec4 ToVec4(const Color32& color);

}  // namespace kdk
