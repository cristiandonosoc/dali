#pragma once

#include <dali/core/defines.h>
#include <dali/core/math.h>

namespace kdk {

// Our hex representation is using Axial coordinates:
// https://www.redblobgames.com/grids/hexagons/#coordinates

struct Hex {
    int Q;
    int R;

    // sqrt(3), precomputed (see the TODO that used to live in WorldToHex).
    static constexpr float kSqrt3 = 1.7320508075688772f;

    static Hex Direction(int direction);
    static int Distance(const Hex& a, const Hex& b);

    // Flat-top orientation (https://www.redblobgames.com/grids/hexagons/#hex-to-pixel). |size| is
    // the distance from a hex center to any of its corners, in world units.
    static Vec2 HexToWorld(float size, Hex h);
    // The inverse of HexToWorld: which hex covers |point|.
    static Hex WorldToHex(float size, Vec2 point);
    // Corner |i| (0..5) of a flat-top hex centered at |center| with radius |size|, in world units.
    static Vec2 HexCorner(float size, Vec2 center, int i);
    static Hex Round(Vec2 p);

    Hex Add(const Hex& h) const { return Hex{Q + h.Q, R + h.R}; }
    Hex Substract(const Hex& h) const { return Hex{Q - h.Q, R - h.R}; }

    Hex Neighbour(int direction) const { return Add(Direction(direction)); }

    bool operator==(const Hex& h) const { return Q == h.Q && R == h.R; }
    bool operator!=(const Hex& h) const { return !(*this == h); }
};

}  // namespace kdk
