#pragma once

#include <dali/core/color.h>
#include <dali/core/defines.h>
#include <dali/core/math.h>
#include <dali/game/hex.h>

#include <imgui.h>

namespace kdk {

struct PlatformState;
struct DrawContext;
struct AssetRegistry;

// The game's UI layer: a tier that traps the mouse the same way ImGui does, sitting between it and
// the game. Priority is ImGui -> UI -> game, arbitrated by execution order rather than by ids or
// retained state. An element that takes the mouse sets MouseCaptured; the game is the lowest tier,
// so it runs last and acts only when nothing above it claimed the frame's input. Nothing here
// persists across frames.
//
// This is NOT built on ImGui's widget system — no Begin/End, no ids, no internal state. ImDrawList
// is used purely as a triangle sink, the same temporary draw path the world already renders through
// (see DrawHexGrid), so a real render layer swaps both at once. Input comes from Dali's own
// InputState, never from ImGui. That is the line between this and dali/game/imgui_widgets.h, which
// IS built on ImGui's widget system and is dev tooling rather than game UI.
//
// Emission order is UI-then-world (input priority), which is the opposite of the draw order we
// want, so the caller paints the two into separate channels of one draw list and merges. See
// GameRender.
//
// Coordinates are window-space. A world-space overload (a button that tracks a tower rather than a
// screen rect) needs the world->screen transform, which DrawContext already carries and GameRender
// already builds — but DrawContext is private to game.cpp, so it has to move to a header before
// UILayer can hold one in place of its own _DrawList.

enum class EUIButtonState : u8 {
    Idle = 0,
    Hover,
    Pressed,
};

// One color per EUIButtonState. Defaults are a neutral dark slate that reads over the world.
struct UIButtonStyle {
    Color32 Idle = Color32::FromRGBA(40, 44, 52, 220);
    Color32 Hover = Color32::FromRGBA(70, 78, 92, 240);
    Color32 Pressed = Color32::FromRGBA(100, 130, 180, 255);
    Color32 Border = Color32::FromRGBA(200, 205, 215, 255);
    float BorderThickness = 2.0f;
    float Rounding = 4.0f;
};

struct UILayer {
    Vec2 MousePos = {};
    bool MouseDown = false;
    bool MousePressed = false;
    // True once ImGui or a UI element owns the mouse this frame. Seeded from InputState's
    // MouseOverride (ImGui's claim, computed from its previous-frame window rects), then set by any
    // hovered element. One flag spans all three tiers because the game reads the accumulated answer
    // last.
    bool MouseCaptured = false;

    static UILayer New(PlatformState* ps);
};

// A square button, painted per EUIButtonState. Returns true on the frame a press lands inside it.
//
// Hover captures the mouse rather than the click: with the pointer over a button, a click belongs
// to the UI whether or not this button wanted it, and later elements plus the game then fall out
// for free. The consequence is that the FIRST button submitted wins an overlap while the LAST one
// submitted paints on top — irrelevant for a non-overlapping row, wrong as soon as popups or
// tooltips exist, which is when this needs a retained rect list walked back-to-front.
//
// Clicks fire on press, not on release-over-the-widget, so there is no drag-off-to-cancel. Fine for
// a build palette; revisit before anything destructive hangs off one.
bool UIButton(DrawContext* dc, Vec2 pos, Vec2 size, const UIButtonStyle& style = {});

// The frame's shared draw state: one draw list, one origin/zoom, for everything that paints the
// world. Built once in GameRender and passed down, so no draw path re-derives the transform and
// picking can invert exactly what rendering used.
//
// TODO(cdc): This *SHOULD NOT* be here, this is more of "lower-level" rendering thing.
//			  For now it is fine.
struct DrawContext {
    // for live-resolving asset references at draw
    AssetRegistry* Registry = nullptr;
    ImDrawList* DrawList = nullptr;
    UILayer UI = {};

    Vec2 Origin = {};
    float Zoom = 1;
    float Time = 0;
    // While Control is held, outline each enemy/tower sprite quad so its bounds are visible.
    bool ShowBounds = false;

    void BeginFrame() { DrawList->ChannelsSplit(2); }
    void EnableUIChannel() { DrawList->ChannelsSetCurrent(1); }
    void EnableGameplayChannel() { DrawList->ChannelsSetCurrent(0); }
    void EndFrame() { DrawList->ChannelsMerge(); }

    ImVec2 WorldToScreen(const Vec2& p) const {
        return ImVec2(Origin.x + p.x * Zoom, Origin.y + p.y * Zoom);
    }

    // A tile-center in screen space. Shared by the grid pass and the path spine so they line up.
    ImVec2 TileCenter(const Hex& hex) const {
        return WorldToScreen(Hex::HexToWorld(kHexSize, hex));
    }
};

}  // namespace kdk
