#include <dali/game/ui/ui.h>

#include <dali/core/api.h>
#include <dali/core/input.h>

namespace kdk {

UILayer UILayer::New(PlatformState* ps, ImDrawList* draw_list) {
    UILayer ui = {};
    ui.MousePos = ps->Input.MousePosition;
    ui.MouseDown = ps->Input.IsMouseDown(EMouseButton::Left);
    ui.MousePressed = ps->Input.IsMousePressed(EMouseButton::Left);
    ui.MouseCaptured = ps->Input.MouseOverride;
    ui._DrawList = draw_list;
    return ui;
}

bool UIButton(UILayer* ui, Vec2 pos, Vec2 size, const UIButtonStyle& style) {
    Vec2 max = pos + size;

    bool hovered = !ui->MouseCaptured;
    hovered &= ui->MousePos.x >= pos.x;
    hovered &= ui->MousePos.x < max.x;
    hovered &= ui->MousePos.y >= pos.y;
    hovered &= ui->MousePos.y < max.y;
    if (hovered) {
        ui->MouseCaptured = true;
    }

    bool held = hovered;
    held &= ui->MouseDown;

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
    ui->_DrawList->AddRectFilled(rect_min, rect_max, fill.Bits, style.Rounding);
    if (style.BorderThickness > 0.0f) {
        ui->_DrawList
            ->AddRect(rect_min, rect_max, style.Border.Bits, style.Rounding, 0, style.BorderThickness);
    }

    bool clicked = hovered;
    clicked &= ui->MousePressed;
    return clicked;
}

}  // namespace kdk
