# Milestone 02 — "The game loop + growing terrain"

M01 proved the sim (grid + pathing + entities + targeting + a draw path). It is not a *game* yet —
it is a level editor: you hand-place spawners and towers, enemies trickle continuously, and gold
accrues but is never spent. M02 turns that slice into an actual roguelike-TD loop and starts growing
the map underneath it. See `docs/rogue-tower.md` for the reference game and `docs/done/milestone-01.md`
for the slice this builds on.

**Definition of done:** launch the game with an empty map and some starting gold → spend gold to
place towers (you start with none) → press *Start Wave* → a finite wave of enemies spawns from the
map's outskirts and walks the flow field to the base → survive it and the next wave arms, scaled
harder → after a set of waves, choose a direction and the terrain grows outward, moving the spawn
origin further out → base health hitting 0 ends the run.

Split into **2.1 (the loop)** and **2.2 (terrain expansion)**. 2.1 is a concrete, buildable slice.
2.2 is the part we deliberately **iterate on** — the generator's shape is an open question, so it
ships as the smallest thing that works and grows from there.

---

## Guiding constraints (same discipline as M01)

- **No general ECS.** Phase state, waves, and economy are new fields on `World` / `GameState`, not a
  new architecture. Let the entity abstraction be *demanded* later, with evidence.
- **Spawn from the flow field, not from placed spawners.** The "outskirts" are already derivable:
  in the flow-field tree rooted at `Goal`, a spawn source is a path tile no other path tile points
  *into* (a leaf). `CalculatePath()` already produces this. Waves spawn from derived sources; the
  `ETileContent::Spawner` editor tool stays only as a debug override.
- **Health only, one enemy, one tower.** No Armor/Shield, no DoTs, no mana, no cards. Those are later
  milestones (M05–M07). M02 scales *quantity and HP*, nothing else.
- **No elevation yet.** `Tile` still has no height field; don't add one until a tower rule reads it
  (M04).

---

## 2.1 — The game loop  ✅ DONE

**Landed as:** a four-phase flow `PreGame → Build ⇄ Wave → GameOver` (`EGamePhase` on `GameState`).
PreGame is the terrain editor (the M01 `EOperationMode` tools) plus *Start Game*; `BeginRun` seeds
gold/health, strips towers, and derives spawn sources. Waves (`Wave` on `World`) drip
`kWaveBaseCount + N` enemies from flow-field-derived `SpawnSources` (drawn as green rings), scaling HP
`×(1 + 0.15·(N−1))`; the wave clears when nothing's left to spawn and no enemies remain. Towers cost
`kTowerCost`; you start with `kStartingGold` and none placed. HUD is a fixed left side-panel, with
*Start Wave* / *Restart Game* controls and a per-tower cooldown readout.

Extras folded in while making it playable: WASD camera pan + mouse-wheel zoom (the world<->pixel
split — sim in world units, renderer scales by `zoom`); projectiles fly to a target's `LastSeen`
point so a shot lands instead of vanishing when its target dies; tower `FireCooldown` clamped at 0
(no negative drift) and reset on entering Build.

The three planned additions below all shipped; details kept for reference.

Three additions, all greenfield on the existing `World` / `GameState`.

### 1. Phase state machine
Add `enum class EGamePhase { Build, Wave, GameOver }` (on `GameState` — it is game-flow, not sim
state). `GameUpdate` runs the sim (`UpdateEnemies/Towers/Projectiles`) only during `Wave`; `Build`
freezes it. `GameOver` when `BaseHealth` reaches 0.
- **Forces:** a top-level flow controller the render/HUD reads to decide what to show.
- **Done when:** the sim is visibly frozen in Build and running in Wave, and the run ends on 0 HP.

### 2. Waves (replaces continuous spawners)
A `Wave` descriptor on `World`: `int WaveNumber`, a `ToSpawn` countdown, and a spawn-cadence timer.
Build→Wave on a *Start Wave* button drips `ToSpawn` enemies from the derived source(s) on the
cadence. The wave ends when `ToSpawn == 0 && Enemies.Size == 0` → back to Build, `WaveNumber++`.
Trivial scaling: `count = base + wave`, `HP = base * (1 + 0.1 * wave)`. `UpdateSpawners` is deleted
or repurposed.
- **Forces:** derive spawn sources from the flow field; a finite, schedulable spawn sequence; a
  wave-complete condition.
- **Done when:** pressing Start Wave spawns exactly N enemies from the outermost path tile(s), and
  the next wave is harder.

### 3. Funds (spend, not just earn)
Start with `Gold = kStartingGold` and **no towers** (drop any preplaced content from `InitLevel`).
Placing a tower deducts `kTowerCost`; placement is blocked when broke. During Build, a plain click on
an empty non-path tile buys a tower. The `EOperationMode` editor moves behind a debug toggle; a
minimal play HUD (wave #, gold, base health, Start Wave) takes the front.
- **Forces:** a spend path on the economy that already earns; buildable-tile validation gated on
  affordability.
- **Done when:** you start broke-ish, can only afford a few towers, and must survive to earn more.

---

## 2.2 — Terrain expansion  ← *the part to iterate on*

After every *N* waves (a "wave set"), enter a `Build`-adjacent **Expand** step: the player picks a
direction and terrain grows outward, pushing the spawn origin further from the base and opening new
buildable tiles. This is the Rogue Tower hook and the genuinely open design space — it ships minimal
and iterates.

**The one hard blocker:** `Grid.Tiles` is `FixedVector<Tile, 64>` and radius-3 is already 37 tiles.
A few expansions overflow it. MVP fix: bump the cap generously (~512–1024); the BFS frontier sizes
off `decltype(Tiles)::kMaxSize` so it follows automatically. `FindTile` stays a linear scan — fine at
a few hundred tiles; move to a `Hex`-keyed hash only if profiling complains. The grid stops being a
clean ring and becomes an arbitrary hex set — `FindTile` already handles that.

### Iteration 1 — single-lane extension (ship this first)
1. Take the current source tile `S` (outermost path).
2. Player picks one of 6 hex directions `d` (buttons for the valid outward ones).
3. Append `K` new path tiles stepping in `d` from `S`; the new source is the last one added.
4. Materialize each new path tile's 6 neighbours as non-path `Tile`s → buildable spots alongside the
   new lane.
5. Re-run `CalculatePath()`. Connectivity is structural: new path anchors to `S`, which routes to
   `Goal`, so nothing orphans (the doc's "must not orphan the path" holds by construction).
- **Done when:** picking a direction lengthens the path, waves now spawn from further out, and there
  is new ground to build on.

### Iteration 2+ — the things to experiment with
These are deliberately *not* specced — each is a knob to try, keep, or drop:
- **Twist:** random bends while extending, instead of a straight run, for that Rogue Tower feel.
- **Branching lanes:** grow a *second* lane from a mid-path tile → two flow-field leaves → two
  simultaneous spawn origins. **Near-free on the sim** (`CalculatePath` already merges lanes into one
  `Goal`); all the work is the generator deciding when and where to fork.
- **Chunk variety:** wider wedges, small clearings, dead-end buildable pockets off the lane.
- **Direction-choice UX:** ghost the candidate frontier and let the player click, vs. plain buttons.
- **Validation surfacing:** if a candidate direction would be degenerate, gray it out.

Each iteration is a self-contained change to the generator; the loop from 2.1 and the sim from M01
stay untouched.

---

## What this milestone hands us

- The real **phase/flow controller** shape, reused by every later milestone (card draws in M07 hang
  off the Build phase).
- Evidence on whether **typed pools + tile-content** still suffice once the map is large and growing,
  or whether tile storage (linear `FindTile`) is the first thing that needs a real spatial index.
- A working **procedural append** with orphan-safety — the foundation branching, twist, and elevation
  (M04) build on.
- Whether the derived-source model ("outskirts = flow-field leaves") holds up once lanes branch.

## Next milestones (named, not specced yet)

| ID  | Scope |
|-----|-------|
| M03 | Real 2D render layer (moves `glClear` / viewport out of `PlatformEndFrame`) |
| M04 | Elevation + range/damage bonus (may trigger 3D) |
| M05 | Layered HP (Shield → Armor → Health) + a second tower type |
| M06 | Status effects + DoTs |
| M07 | Card draw + tower unlocks |
