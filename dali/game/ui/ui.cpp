#include <dali/game/ui/ui.h>

#include <dali/core/api.h>
#include <dali/core/input.h>

namespace kdk {

UILayer UILayer::New(PlatformState* ps) {
    UILayer ui = {};
    ui.MousePos = ps->Input.MousePosition;
    ui.MouseDown = ps->Input.IsMouseDown(EMouseButton::Left);
    ui.MousePressed = ps->Input.IsMousePressed(EMouseButton::Left);
    ui.MouseCaptured = ps->Input.MouseOverride;
    return ui;
}

bool UIButton(DrawContext* dc, Vec2 pos, Vec2 size, const UIButtonStyle& style) {
    Vec2 max = pos + size;

    bool hovered = !dc->UI.MouseCaptured;
    hovered &= dc->UI.MousePos.x >= pos.x;
    hovered &= dc->UI.MousePos.x < max.x;
    hovered &= dc->UI.MousePos.y >= pos.y;
    hovered &= dc->UI.MousePos.y < max.y;
    if (hovered) {
        dc->UI.MouseCaptured = true;
    }

    bool held = hovered;
    held &= dc->UI.MouseDown;

    EUIButtonState state = EUIButtonState::Idle;
    if (hovered) {
        state = EUIButtonState::Hover;
    }
    if (held) {
        state = EUIButtonState::Pressed;
    }

    Color32 fill = style.Idle;
    if (state == EUIButtonState::Hover) {
        fill = style.Hover;
    }
    if (state == EUIButtonState::Pressed) {
        fill = style.Pressed;
    }

    ImVec2 rect_min(pos.x, pos.y);
    ImVec2 rect_max(max.x, max.y);
    dc->DrawList->AddRectFilled(rect_min, rect_max, fill.Bits, style.Rounding);
    if (style.BorderThickness > 0.0f) {
        dc->DrawList->AddRect(rect_min,
                              rect_max,
                              style.Border.Bits,
                              style.Rounding,
                              0,
                              style.BorderThickness);
    }

    bool clicked = hovered;
    clicked &= dc->UI.MousePressed;
    return clicked;
}

void UIText(DrawContext* dc, Vec2 pos, const char* text, Color32 color, float scale) {
    ImFont* font = ImGui::GetFont();
    float font_size = ImGui::GetFontSize() * scale;
    dc->DrawList->AddText(font, font_size, ImVec2(pos.x, pos.y), color.Bits, text);
}

Vec2 UITextSize(const char* text, float scale) {
    ImVec2 size = ImGui::CalcTextSize(text);
    return Vec2(size.x * scale, size.y * scale);
}

}  // namespace kdk
