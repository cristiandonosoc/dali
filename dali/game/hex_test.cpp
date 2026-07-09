#include <dali/game/game.h>
#include <dali/game/hex.h>

#include <catch2/catch_test_macros.hpp>

using namespace kdk;

// Milestone-01 step 2 done-when: axial<->world round-trips and path tiles are contiguous neighbours.

TEST_CASE("Hex axial<->world round-trips", "[hex]") {
    constexpr float kSize = 60.0f;

    // Every hex within radius 3 (the M01 grid) must survive HexToWorld -> WorldToHex unchanged.
    for (int q = -3; q <= 3; ++q) {
        for (int r = -3; r <= 3; ++r) {
            if (AbsI(q + r) > 3) {
                continue;  // outside the radius-3 hex neighbourhood
            }
            Hex hex{q, r};
            Hex round_tripped = Hex::WorldToHex(kSize, Hex::HexToWorld(kSize, hex));
            REQUIRE(round_tripped == hex);
        }
    }
}

TEST_CASE("Each hex direction yields a distance-1 neighbour", "[hex]") {
    Hex origin{0, 0};
    for (int dir = 0; dir < 6; ++dir) {
        REQUIRE(Hex::Distance(origin, origin.Neighbour(dir)) == 1);
    }
}

TEST_CASE("A straight path is a chain of contiguous neighbours", "[hex]") {
    World world = {};
    world.BuildStraightPath(Hex{-3, 0}, 0, 6);

    REQUIRE(world.Path.Size == 7);  // start tile + 6 steps
    for (i32 i = 1; i < world.Path.Size; ++i) {
        REQUIRE(Hex::Distance(world.Path[i - 1], world.Path[i]) == 1);
    }
}
