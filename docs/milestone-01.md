# Milestone 01 — "One tile, one crawler, one ballista"

The first playable vertical slice. Its job is not to be fun — it is to be the *smallest* thing that
exercises grid + pathing + entities + targeting + a draw path all at once, so it flushes out what
those subsystems actually need instead of us guessing. See `docs/rogue-tower.md` for the reference
game these requirements come from.

**Definition of done:** launch the game → see a short hex path drawn on screen → a single enemy
crawls from the spawn tile toward the base → one ballista in range auto-fires and kills it → a gold
counter ticks up. All driven from the hot-reloadable game DLL.

---

## Guiding constraints (avoid the Kandinsky trap)

- **No general ECS.** Typed fixed pools on a `World` struct (`FixedVector<Enemy>`,
  `FixedVector<Tower>`, `FixedVector<Projectile>`). Let a real entity architecture be *demanded* by a
  later milestone, with evidence — not pre-built.
- **No procedural generation.** Hardcode a straight path of ~8 tiles. Path gen is `M02`.
- **2D top-down first.** Elevation is a number on a tile that nothing reads yet. Don't build a 3D
  renderer to satisfy a damage-bonus rule not yet implemented.
- **Health only.** One HP pool. No Armor / Shield layers, no DoTs, no mana.

---

## Steps

Each step forces exactly one engine subsystem.

### 1. `World` + GameState lifecycle
Build a `World` struct; `OnGameInit` allocates it into `Memory.PermanentArena` and stores the
pointer in `ps->GameState`. `OnGameUpdate` / `OnGameRender` cast it back.
- **Forces:** GameState-survives-reload wiring (arenas already exist; this is their first real user).
- **Done when:** store a counter in `World`, hot-reload the DLL, and the counter is preserved.

### 2. Hex grid + hardcoded path
Hex coordinate math (axial coords, `TileToWorld(q, r) -> Vec2`), a `Tile { i32 Elevation; bool
IsPath; ... }`, a `Grid` holding a fixed span of tiles, and a hardcoded ordered list of ~8 path
tiles ending at the base tile.
- **Forces:** hex math + tile/grid data structures + an ordered path representation enemies follow.
- **Done when:** a unit test converts axial↔world and confirms path tiles are contiguous neighbors.

### 3. Debug draw of the grid  ← *the render decision point*
Draw the hex tiles and the path. **Start with ImGui's background draw list**
(`ImGui::GetBackgroundDrawList()->AddLine / AddConvexPolyFilled`) — zero new engine code, lets steps
4–6 proceed immediately. Port to a real 2D line/quad debug renderer as the *first* deliberate piece
of the render layer once this slice reveals which primitives we actually need (almost certainly:
lines, filled convex polys, later textured quads).
- **Forces:** nothing engine-side yet — that's the point; it *defers* the render-layer build until
  requirements are visible.
- **Done when:** the hardcoded path is visible on screen.

### 4. One enemy walking the path
`Enemy { f32 PathDistance; f32 Speed; f32 Health; Vec2 Position; }` in a pool; movement integrates
`PathDistance += Speed * dt` and resolves to a world position by interpolating between path tiles.
Uses the existing time system for `dt`.
- **Forces:** entity pool + path-follow movement + delta-time integration + path→position sampling.
- **Done when:** a dot crawls from spawn to base and despawns (or damages the base) on arrival.

### 5. One tower with range + targeting
`Tower { Vec2 Position; f32 Range; f32 FireRateHz; f32 _Cooldown; }`; each frame it queries enemies
within `Range` and picks a target (start: closest-to-base, the Rogue Tower default).
- **Forces:** spatial range query + target-priority selection + fire-cooldown timing.
- **Done when:** the tower marks its current target; debug-draw a line to it.

### 6. Projectile + damage + gold
On fire, spawn a `Projectile` (a simple traveling dot; hitscan is also fine) that applies `Damage`
to the target's Health on hit. On death: remove the enemy, `World.Gold += reward`.
- **Forces:** projectile sim (or hitscan resolution) + single-layer damage resolution + kill/reward
  economy.
- **Done when:** the crawler dies mid-path and gold increments.

### 7. Minimal HUD
An ImGui window showing Gold, alive-enemy count, and a "spawn enemy" debug button. (ImGui is already
wired end to end.)
- **Done when:** you can click-spawn crawlers and watch the tower farm them.

---

## What this milestone hands us

- A concrete answer to **"2D or 3D renderer?"** — whether top-down reads well enough to defer 3D, or
  whether elevation forces it sooner.
- The real shape of the **spatial query** reused later for auras / AoE.
- Whether typed pools suffice or an **entity abstraction** is starting to hurt — the trigger to
  design one, with evidence.
- The exact **debug-draw primitives** to build first in the render layer, replacing the temporary
  ImGui draw-list.

## Next milestones (named, not specced yet)

| ID  | Scope |
|-----|-------|
| M02 | Procedural path expansion + branching |
| M03 | Real 2D render layer (moves `glClear` / viewport out of `PlatformEndFrame`) |
| M04 | Elevation + range/damage bonus (may trigger 3D) |
| M05 | Layered HP (Shield → Armor → Health) + a second tower type |
| M06 | Status effects + DoTs |
| M07 | Card draw + tower unlocks |
