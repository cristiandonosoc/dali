#include <engine/core/color.h>

namespace kdk {

// clang-format off
const Color32 Color32::White             { .R = 255, .G = 255, .B = 255, .A = 255 };
const Color32 Color32::Red               { .R = 255, .G =   0, .B =   0, .A = 255 };
const Color32 Color32::Green             { .R =   0, .G = 255, .B =   0, .A = 255 };
const Color32 Color32::Blue              { .R =   0, .G =   0, .B = 255, .A = 255 };
const Color32 Color32::Magenta           { .R = 255, .G =   0, .B = 255, .A = 255 };
const Color32 Color32::Cyan              { .R =   0, .G = 255, .B = 255, .A = 255 };
const Color32 Color32::Yellow            { .R = 255, .G = 255, .B =   0, .A = 255 };
const Color32 Color32::Black             { .R =   0, .G =   0, .B =   0, .A = 255 };
const Color32 Color32::Aquamarine        { .R = 112, .G = 219, .B = 147, .A = 255 };
const Color32 Color32::BakerChocolate    { .R =  92, .G =  51, .B =  23, .A = 255 };
const Color32 Color32::BlueViolet        { .R = 159, .G =  95, .B = 159, .A = 255 };
const Color32 Color32::Brass             { .R = 181, .G = 166, .B =  66, .A = 255 };
const Color32 Color32::BrightGold        { .R = 217, .G = 217, .B =  25, .A = 255 };
const Color32 Color32::Brown             { .R = 166, .G =  42, .B =  42, .A = 255 };
const Color32 Color32::Bronze            { .R = 140, .G = 120, .B =  83, .A = 255 };
const Color32 Color32::BronzeII          { .R = 166, .G = 125, .B =  61, .A = 255 };
const Color32 Color32::CadetBlue         { .R =  95, .G = 159, .B = 159, .A = 255 };
const Color32 Color32::CoolCopper        { .R = 217, .G = 135, .B =  25, .A = 255 };
const Color32 Color32::Copper            { .R = 184, .G = 115, .B =  51, .A = 255 };
const Color32 Color32::Coral             { .R = 255, .G = 127, .B =   0, .A = 255 };
const Color32 Color32::CornFlowerBlue    { .R =  66, .G =  66, .B = 111, .A = 255 };
const Color32 Color32::DarkBrown         { .R =  92, .G =  64, .B =  51, .A = 255 };
const Color32 Color32::DarkGreen         { .R =  47, .G =  79, .B =  47, .A = 255 };
const Color32 Color32::DarkGreenCopper   { .R =  74, .G = 118, .B = 110, .A = 255 };
const Color32 Color32::DarkOliveGreen    { .R =  79, .G =  79, .B =  47, .A = 255 };
const Color32 Color32::DarkOrchid        { .R = 153, .G =  50, .B = 205, .A = 255 };
const Color32 Color32::DarkPurple        { .R = 135, .G =  31, .B = 120, .A = 255 };
const Color32 Color32::DarkSlateBlue     { .R = 107, .G =  35, .B = 142, .A = 255 };
const Color32 Color32::DarkSlateGrey     { .R =  47, .G =  79, .B =  79, .A = 255 };
const Color32 Color32::DarkTan           { .R = 151, .G = 105, .B =  79, .A = 255 };
const Color32 Color32::DarkTurquoise     { .R = 112, .G = 147, .B = 219, .A = 255 };
const Color32 Color32::DarkWood          { .R = 133, .G =  94, .B =  66, .A = 255 };
const Color32 Color32::DimGrey           { .R =  84, .G =  84, .B =  84, .A = 255 };
const Color32 Color32::DustyRose         { .R = 133, .G =  99, .B =  99, .A = 255 };
const Color32 Color32::Feldspar          { .R = 209, .G = 146, .B = 117, .A = 255 };
const Color32 Color32::Firebrick         { .R = 142, .G =  35, .B =  35, .A = 255 };
const Color32 Color32::ForestGreen       { .R =  35, .G = 142, .B =  35, .A = 255 };
const Color32 Color32::Gold              { .R = 205, .G = 127, .B =  50, .A = 255 };
const Color32 Color32::Goldenrod         { .R = 219, .G = 219, .B = 112, .A = 255 };
const Color32 Color32::Grey              { .R = 192, .G = 192, .B = 192, .A = 255 };
const Color32 Color32::GreenCopper       { .R =  82, .G = 127, .B = 118, .A = 255 };
const Color32 Color32::GreenYellow       { .R = 147, .G = 219, .B = 112, .A = 255 };
const Color32 Color32::HunterGreen       { .R =  33, .G =  94, .B =  33, .A = 255 };
const Color32 Color32::IndianRed         { .R =  78, .G =  47, .B =  47, .A = 255 };
const Color32 Color32::Khaki             { .R = 159, .G = 159, .B =  95, .A = 255 };
const Color32 Color32::LightBlue         { .R = 192, .G = 217, .B = 217, .A = 255 };
const Color32 Color32::LightGrey         { .R = 168, .G = 168, .B = 168, .A = 255 };
const Color32 Color32::LightSteelBlue    { .R = 143, .G = 143, .B = 189, .A = 255 };
const Color32 Color32::LightWood         { .R = 233, .G = 194, .B = 166, .A = 255 };
const Color32 Color32::LimeGreen         { .R =  50, .G = 205, .B =  50, .A = 255 };
const Color32 Color32::MandarianOrange   { .R = 228, .G = 120, .B =  51, .A = 255 };
const Color32 Color32::Maroon            { .R = 142, .G =  35, .B = 107, .A = 255 };
const Color32 Color32::MediumAquamarine  { .R =  50, .G = 205, .B = 153, .A = 255 };
const Color32 Color32::MediumBlue        { .R =  50, .G =  50, .B = 205, .A = 255 };
const Color32 Color32::MediumForestGreen { .R = 107, .G = 142, .B =  35, .A = 255 };
const Color32 Color32::MediumGoldenrod   { .R = 234, .G = 234, .B = 174, .A = 255 };
const Color32 Color32::MediumOrchid      { .R = 147, .G = 112, .B = 219, .A = 255 };
const Color32 Color32::MediumSeaGreen    { .R =  66, .G = 111, .B =  66, .A = 255 };
const Color32 Color32::MediumSlateBlue   { .R = 127, .G =   0, .B = 255, .A = 255 };
const Color32 Color32::MediumSpringGreen { .R = 127, .G = 255, .B =   0, .A = 255 };
const Color32 Color32::MediumTurquoise   { .R = 112, .G = 219, .B = 219, .A = 255 };
const Color32 Color32::MediumVioletRed   { .R = 219, .G = 112, .B = 147, .A = 255 };
const Color32 Color32::MediumWood        { .R = 166, .G = 128, .B = 100, .A = 255 };
const Color32 Color32::MidnightBlue      { .R =  47, .G =  47, .B =  79, .A = 255 };
const Color32 Color32::NavyBlue          { .R =  35, .G =  35, .B = 142, .A = 255 };
const Color32 Color32::NeonBlue          { .R =  77, .G =  77, .B = 255, .A = 255 };
const Color32 Color32::NeonPink          { .R = 255, .G = 110, .B = 199, .A = 255 };
const Color32 Color32::NewMidnightBlue   { .R =   0, .G =   0, .B = 156, .A = 255 };
const Color32 Color32::NewTan            { .R = 235, .G = 199, .B = 158, .A = 255 };
const Color32 Color32::OldGold           { .R = 207, .G = 181, .B =  59, .A = 255 };
const Color32 Color32::Orange            { .R = 255, .G = 127, .B =   0, .A = 255 };
const Color32 Color32::OrangeRed         { .R = 255, .G =  36, .B =   0, .A = 255 };
const Color32 Color32::Orchid            { .R = 219, .G = 112, .B = 219, .A = 255 };
const Color32 Color32::PaleGreen         { .R = 143, .G = 188, .B = 143, .A = 255 };
const Color32 Color32::Pink              { .R = 188, .G = 143, .B = 143, .A = 255 };
const Color32 Color32::Plum              { .R = 234, .G = 173, .B = 234, .A = 255 };
const Color32 Color32::Quartz            { .R = 217, .G = 217, .B = 243, .A = 255 };
const Color32 Color32::RichBlue          { .R =  89, .G =  89, .B = 171, .A = 255 };
const Color32 Color32::Salmon            { .R = 111, .G =  66, .B =  66, .A = 255 };
const Color32 Color32::Scarlet           { .R = 140, .G =  23, .B =  23, .A = 255 };
const Color32 Color32::SeaGreen          { .R =  35, .G = 142, .B = 104, .A = 255 };
const Color32 Color32::SemiSweetChocolate{ .R = 107, .G =  66, .B =  38, .A = 255 };
const Color32 Color32::Sienna            { .R = 142, .G = 107, .B =  35, .A = 255 };
const Color32 Color32::Silver            { .R = 230, .G = 232, .B = 250, .A = 255 };
const Color32 Color32::SkyBlue           { .R =  50, .G = 153, .B = 204, .A = 255 };
const Color32 Color32::SlateBlue         { .R =   0, .G = 127, .B = 255, .A = 255 };
const Color32 Color32::SpicyPink         { .R = 255, .G =  28, .B = 174, .A = 255 };
const Color32 Color32::SpringGreen       { .R =   0, .G = 255, .B = 127, .A = 255 };
const Color32 Color32::SteelBlue         { .R =  35, .G = 107, .B = 142, .A = 255 };
const Color32 Color32::SummerSky         { .R =  56, .G = 176, .B = 222, .A = 255 };
const Color32 Color32::Tan               { .R = 219, .G = 147, .B = 112, .A = 255 };
const Color32 Color32::Thistle           { .R = 216, .G = 191, .B = 216, .A = 255 };
const Color32 Color32::Turquoise         { .R = 173, .G = 234, .B = 234, .A = 255 };
const Color32 Color32::VeryDarkBrown     { .R =  92, .G =  64, .B =  51, .A = 255 };
const Color32 Color32::VeryLightGrey     { .R = 205, .G = 205, .B = 205, .A = 255 };
const Color32 Color32::Violet            { .R =  79, .G =  47, .B =  79, .A = 255 };
const Color32 Color32::VioletRed         { .R = 204, .G =  50, .B = 153, .A = 255 };
const Color32 Color32::Wheat             { .R = 216, .G = 216, .B = 191, .A = 255 };
const Color32 Color32::YellowGreen       { .R = 153, .G = 204, .B =  50, .A = 255 };
// clang-format on

Vec3 ToVec3(const Color32& color) {
    return Vec3((float)color.R / 255.0f, (float)color.G / 255.0f, (float)color.B / 255.0f);
}

Vec4 ToVec4(const Color32& color) {
    return Vec4((float)color.R / 255.0f,
                (float)color.G / 255.0f,
                (float)color.B / 255.0f,
                (float)color.A / 255.0f);
}

}  // namespace kdk
