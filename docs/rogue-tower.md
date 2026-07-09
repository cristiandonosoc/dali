# Rogue Tower — Reference Game

Dali is a tower-defense game. Rather than design mechanics in a vacuum (the mistake that stalled
Kandinsky — building engine systems with no game to demand them), we close-clone a concrete
reference and let *its* features drive engine requirements. That reference is **Rogue Tower**
(Rusto Games, 2022).

This document describes what Rogue Tower is and, for each system, what it demands from the engine.
It is a requirements source, not a spec — the actual build order lives in the milestone docs
(`docs/milestone-02.md` is current; finished ones move to `docs/done/`).

Sources: [Steam](https://store.steampowered.com/app/1843760/Rogue_Tower/) ·
[Wiki: Hit Points](https://rogue-tower.fandom.com/wiki/Hit_Points) ·
[Wiki: Status Effects](https://rogue-tower.fandom.com/wiki/Status_Effects) ·
[Wiki: Towers](https://rogue-tower.fandom.com/wiki/Towers) ·
[GamePretty guide](https://gamepretty.com/rogue-tower-beginners-guide-game-mechanics-strategies/).

---

## What it is

A **roguelike tower defense** whose hook is that *you build the map as you play*. There is no fixed
track. Every level you choose a direction and the game procedurally extends, twists, and splits the
enemy path across a **hex-tile grid with elevation**. Enemies walk the ever-growing path toward your
central Tower; you place towers on non-path tiles to kill them before they arrive. A run ends when
the Tower dies. Between runs, XP unlocks more cards, so the pool of possible towers/upgrades grows.

It is endless — you play for survival / score, not a win screen.

## Core loop

1. **Build phase (untimed):** choose a path-expansion tile, draw upgrade/unlock **cards**, spend
   **gold** to place or upgrade towers.
2. **Wave phase:** enemies spawn and traverse the whole path; towers auto-fire; kills drop gold.
3. Repeat. Enemies scale each wave; the path keeps growing.

---

## Systems

### Map / path
Hex tiles, procedurally appended each level, each with an **elevation** that affects gameplay:
higher towers get more range *and* more base damage. Paths **branch**, so several lanes can
converge on the Tower. The path grows far longer than a typical TD, which is what makes placement
matter.

> **Engine demand:** hex-grid data structure; procedural tile append with validation (a new tile
> must not orphan the path); per-tile height; path-distance queries; range / line-of-sight checks
> that account for elevation. This is the single biggest departure from a stock TD and the first
> thing worth prototyping.

### Towers
Distinct archetypes with different firing models:
- **Ballista** — basic single-target; strong vs. Health. Starting tower.
- **Mortar** — slow ballistic AoE; strong vs. Armor; good against clumps.
- **Frost Keep** — radius slow aura (constant 2 mana/s); value is the slow, not the damage.
- **Tesla** — short-range pulse, consumes mana per pulse; strong vs. Shields; wants to overlap
  multiple path tiles.
- **Encampment** — drops mines at random points inside its radius; effectiveness scales with how
  many path tiles the radius covers.
- **Obelisk / Particle Cannon** — further archetypes (Armor / Shield specialists).

Each tower has: base damage; **separate multipliers vs. Health / Armor / Shield**; range; fire
rate; a projectile model (instant, ballistic-with-travel-time, radius aura, or mine-spawner); and a
**target-priority** system with hidden scoring (ties break toward the enemy closest to the Tower).
Towers gain **XP even while only aiming** (not just firing), and that XP levels individual stats.
Elevation raises range and base damage. There are 400+ cards total.

> **Engine demand:** data-driven tower definitions (400+ cards must be *data*, not code — a natural
> fit for the X-macro type-registration pattern); range + priority targeting queries; projectile
> simulation; aura / radius effects; per-entity stat + XP tracking.

### Enemies
Up to **three layered HP pools: Shield → Armor → Health**, depleted strictly in that order (Shield
must be gone before Armor takes damage, Armor before Health). Types vary in speed and abilities:
healers, sprinters, ranged attackers that hit the Tower, and spawners that create more enemies.

> **Engine demand:** layered-HP damage resolution; path movement with variable speed (for slows);
> a per-type ability / behavior hook.

### Damage & status
Damage against the current layer is `base × typeMultiplier`. Three damage-over-time effects, each
strong against a different layer and each halting a different regeneration:

| DoT    | Strong vs. | Halts regen of |
|--------|------------|----------------|
| Bleed  | Health     | Health         |
| Burn   | Armor      | Armor          |
| Poison  | Shield     | Shield         |

(Armor takes full Burn, half Bleed/Poison; Health takes full Bleed, half Burn/Poison; etc.) DoTs
stack and can add slow.

> **Engine demand:** a stacking status-effect system (duration, per-tick application); a small
> `damageType × hpLayer` multiplier matrix (pure data); regen timers gated by active statuses.

### Resources
- **Gold** — earned from kills, spent on placing/upgrading towers.
- **Mana** — some towers consume it (per shot or per second); supplied by mana-generating buildings.
  Mana banks vs. siphons trade efficiency. This couples tower placement to support buildings.
- **XP / research** — meta-currency; unlocks cards between runs.

> **Engine demand:** a few scalar pools with production/consumption. Cheap, but the mana economy
> creates a real placement dependency.

### Progression
In-run **card draws** unlock new towers or upgrade stats/abilities; prerequisites gate what can
appear, and unlocking cards dilutes the draw pool. Between runs, permanent unlocks persist.

> **Engine demand:** a card/definition registry, a prerequisite graph, and weighted random draw
> from an unlocked pool.

---

## What actually drives the engine

Most of the above is cheap glue. The requirements *specific* to this game — the ones worth letting
dictate architecture — are:

1. A procedural **hex grid with elevation and a growable, branch-capable path**.
2. **Entities on a path** with layered HP and stacking status effects.
3. A **data-driven definition registry** for towers / enemies / cards, so content is data not code.
4. **Spatial queries** (range, priority, auras) over that grid.

The forcing function Kandinsky lacked: build a vertical slice of *just* "append a hex tile, walk one
enemy down it, one ballista shoots it" and you exercise grid, pathing, targeting, projectiles, and
the render path together. That slice is `docs/done/milestone-01.md`.
