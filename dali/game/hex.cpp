#include <dali/game/hex.h>

namespace kdk {

Hex Hex::Direction(int direction) {
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

int Hex::Distance(const Hex& a, const Hex& b) {
    Hex s = a.Substract(b);
    return (AbsI(s.Q) + AbsI(s.Q + s.R) + AbsI(s.R)) / 2;
}

Vec2 Hex::HexToWorld(float size, Hex h) {
    float x = size * (3.0f / 2.0f * (float)h.Q);
    float y = size * (kSqrt3 * ((float)h.Q / 2.0f + (float)h.R));
    return Vec2(x, y);
}

Hex Hex::WorldToHex(float size, Vec2 point) {
    Vec2 s = point / size;

    float q = 2.0f * s.x / 3.0f;
    float r = (-1.0f * s.x / 3.0f) + (kSqrt3 * s.y / 3.0f);

    return Hex::Round(Vec2(q, r));
}

Vec2 Hex::HexCorner(float size, Vec2 center, int i) {
    float angle = ToRadians(60.0f * (float)i);
    return Vec2(center.x + size * Cos(angle), center.y + size * Sin(angle));
}

Hex Hex::Round(Vec2 p) {
    float xgrid = kdk::Round(p.x);
    float ygrid = kdk::Round(p.y);

    float x = p.x - xgrid;
    float y = p.y - ygrid;

    // Using aritmetic instead of conditional branch (the multiply on the right).
    float dx = kdk::Round(x + 0.5 * y) * (x * x >= y * y);
    float dy = kdk::Round(y + 0.5 * x) * (x * x < y * y);
    return Hex{(int)(xgrid + dx), (int)(ygrid + dy)};
}

}  // namespace kdk
