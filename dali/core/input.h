#pragma once

#include <dali/core/defines.h>
#include <dali/core/math.h>

#include <bitset>

namespace kdk {

// Input state, part of the platform<->game contract (lives on PlatformState). Deliberately
// SDL-free: EKey/EMouseButton are Dali-owned enums whose integer values MIRROR the SDL scancode /
// mouse-button values, so the platform's poll loop indexes the bitsets with the raw SDL codes and
// needs no translation table. That mirroring is verified with static_asserts in the platform TU
// (dali/platform/platform.cpp) — the one place that sees both this header and <SDL3/SDL.h>. If a
// second backend or remapping ever lands, swap the identity for a real table there; the game never
// sees the change.

// Values match SDL_SCANCODE_*. Not exhaustive — add entries as gameplay needs them (safe: additive).
enum class EKey : u16 {
    Invalid = 0,

    A = 4,
    B = 5,
    C = 6,
    D = 7,
    E = 8,
    F = 9,
    G = 10,
    H = 11,
    I = 12,
    J = 13,
    K = 14,
    L = 15,
    M = 16,
    N = 17,
    O = 18,
    P = 19,
    Q = 20,
    R = 21,
    S = 22,
    T = 23,
    U = 24,
    V = 25,
    W = 26,
    X = 27,
    Y = 28,
    Z = 29,

    Num1 = 30,
    Num2 = 31,
    Num3 = 32,
    Num4 = 33,
    Num5 = 34,
    Num6 = 35,
    Num7 = 36,
    Num8 = 37,
    Num9 = 38,
    Num0 = 39,

    Return = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,

    Slash = 56,

    F1 = 58,
    F2 = 59,
    F3 = 60,
    F4 = 61,
    F5 = 62,
    F6 = 63,
    F7 = 64,
    F8 = 65,
    F9 = 66,
    F10 = 67,
    F11 = 68,
    F12 = 69,

    Home = 74,
    PageUp = 75,
    Delete = 76,
    End = 77,
    PageDown = 78,
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,

    LCtrl = 224,
    LShift = 225,
    LAlt = 226,
    LGui = 227,
    RCtrl = 228,
    RShift = 229,
    RAlt = 230,
};

// Values match SDL_BUTTON_*.
enum class EMouseButton : u8 {
    Left = 1,
    Middle = 2,
    Right = 3,
    X1 = 4,
    X2 = 5,
};

// Upper bound on scancodes (== SDL_SCANCODE_COUNT). Named without SDL so the contract stays clean.
constexpr u32 kKeyCount = 512;
// Mouse-button bitsets are indexed by EMouseButton value (1..5); 8 covers them with slack.
constexpr u32 kMouseButtonCount = 8;

struct InputState {
    // Per-frame edges (reset each poll) vs. held level (persists). See PollWindowEvents.
    std::bitset<kKeyCount> KeyPressed = {};
    std::bitset<kKeyCount> KeyDown = {};
    std::bitset<kKeyCount> KeyReleased = {};

    std::bitset<kMouseButtonCount> MousePressed = {};
    std::bitset<kMouseButtonCount> MouseDown = {};
    std::bitset<kMouseButtonCount> MouseReleased = {};

    // Window coords. MousePositionGL has Y flipped (origin bottom-left, as GL wants).
    Vec2 MousePosition = {};
    Vec2 MousePositionGL = {};
    // Relative motion this frame; cleared when no motion event arrived.
    Vec2 MouseMove = {};
    // Wheel accumulator this frame.
    Vec2 MouseScroll = {};

    // Set from ImGui's WantCapture* each frame. When true, the gated reads below return false so a
    // focused ImGui widget swallows the input. Read the bitsets directly to bypass (tools/debug).
    bool KeyboardOverride = false;
    bool MouseOverride = false;

    bool IsKeyDown(EKey key) const { return !KeyboardOverride && KeyDown[(u32)key]; }
    bool IsKeyPressed(EKey key) const { return !KeyboardOverride && KeyPressed[(u32)key]; }
    bool IsKeyReleased(EKey key) const { return !KeyboardOverride && KeyReleased[(u32)key]; }

    bool IsMouseDown(EMouseButton button) const {
        return !MouseOverride && MouseDown[(u32)button];
    }
    bool IsMousePressed(EMouseButton button) const {
        return !MouseOverride && MousePressed[(u32)button];
    }
    bool IsMouseReleased(EMouseButton button) const {
        return !MouseOverride && MouseReleased[(u32)button];
    }
};

}  // namespace kdk
