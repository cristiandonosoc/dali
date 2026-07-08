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

    static Hex Direction(int direction) {
        // clang-format off
		switch (direction) {
			case 0: return Hex{+1,  0};
			case 1: return Hex{+1, -1};
			case 2: return Hex{ 0, -1};
			case 3: return Hex{-1,  0};
			case 4: return Hex{-1, +1};
			case 5: return Hex{ 0, +1};
		}
        // clang-format on

        ASSERT(false);
        return {};
    }

    static int Distance(const Hex& a, const Hex& b) {
        Hex s = a.Substract(b);
        return (AbsI(s.Q) + Abs(s.Q + s.R) + AbsI(s.R)) / 2;
    }

    // Flat-top orientation (https://www.redblobgames.com/grids/hexagons/#hex-to-pixel). |size| is
    // the distance from a hex center to any of its corners, in world units.
    static Vec2 HexToWorld(float size, Hex h) {
        float x = size * (3.0f / 2.0f * (float)h.Q);
        float y = size * (kSqrt3 * ((float)h.Q / 2.0f + (float)h.R));
        return Vec2(x, y);
    }

    // The inverse of HexToWorld: which hex covers |point|.
    static Hex WorldToHex(float size, Vec2 point) {
        Vec2 s = point / size;

        float q = 2.0f * s.x / 3.0f;
        float r = (-1.0f * s.x / 3.0f) + (kSqrt3 * s.y / 3.0f);

        return Hex::Round(Vec2(q, r));
    }

    // Corner |i| (0..5) of a flat-top hex centered at |center| with radius |size|, in world units.
    static Vec2 HexCorner(float size, Vec2 center, int i) {
        float angle = ToRadians(60.0f * (float)i);
        return Vec2(center.x + size * Cos(angle), center.y + size * Sin(angle));
    }

    static Hex Round(Vec2 p) {
        float xgrid = kdk::Round(p.x);
        float ygrid = kdk::Round(p.y);

        float x = p.x - xgrid;
        float y = p.y - ygrid;

        // Using aritmetic instead of conditional branch (the multiply on the right).
        float dx = kdk::Round(x + 0.5 * y) * (x * x >= y * y);
        float dy = kdk::Round(y + 0.5 * x) * (x * x < y * y);
        return Hex{(int)(xgrid + dx), (int)(ygrid + dy)};
    }

    Hex Add(const Hex& h) const { return Hex{Q + h.Q, R + h.R}; }
    Hex Substract(const Hex& h) const { return Hex{Q - h.Q, R - h.R}; }

    Hex Neighbour(int direction) const { return Add(Direction(direction)); }
};

}  // namespace kdk
