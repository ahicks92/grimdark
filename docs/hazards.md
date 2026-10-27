# Painted damage ground (aetherfire, poison floors) -- mechanism, survey, sounds

Mapped 2026-09-13 from the Amalgamation's yard (Rotting Croplands, The Gruesome Harvest), where the player was
losing 12 % of max life a second standing next to a glowing "aetherfire hotspot" with the boss dead.

## The mechanism (RE, verified live)

The engine's per-cell **sector layers** (the container that also holds the HUD area names, `tools/gdmap/sectors.py`)
have a **damage layer, table index 7** (`GAME::DamageSectorData`; the type order is `SectorDataManager::NewSectorData`'s
switch: 0 fog, 1 name, 2 ambient, 3 riftgate, 4 day/night, 5 bloom, 6 climate, 7 damage, 8 adjustment, 9 level limit,
11 view distance, 12 challenge). Each painted entry is a GUID whose definition sits in the map header:
`[len][name][guid 16][8 floats][float amount][int type][len][fx record]`. The nine definitions in world001.map:

- Aether01 0.15 Aether (the standard aetherfire: Conflagration, Deadman's Gulch, Rotted Harvest, Blood Grove mine,
  Port Valbury), Aether02 0.30 (one patch in the Mountain Deeps), Aether Act3 Boss 0.12 (the Amalgamation's yard)
- Slith Lab Poison 0.15 Acid, RatDen Poison 0.02, DumpingGroundsToxin 0.10 (Festering Lair, Port Valbury)
- Fire 0.15, Poison 0.20, Chaos 0.30: defined, painted on no walkable ground found. Cells painted with an all-zero GUID
  (Sorrow's Bastion, the Void, a Conflagration chunk) have no definition and are inert.

**Application** is `TickManager::Tick` (Game.dll 0x56e5e0), interval 1.0 s (the constructor's `[this+4]`): per
character, `Level::GetSectorLayers` -> `SectorLayers::GetTargetId(7, x, z)` with the REGION-RELATIVE position
truncated to ints -> `SectorDataManager::GetSectorData(7, uid)` on the manager embedded at `gEngine+8`; skip if
`Character::IsInvincible` or `SkillManager::IsPurityActive`; damage = `[sd+0x58]` (the amount) x
`CharAttributeAccumulator::GetValue(4)` (max life by the numbers: 611 of 5093) x the interval, then
`CombatManager::ApplyDamage(amount, stats, CombatAttributeType [sd+0x5c], no attackers)`. ApplyDamage subtracts life,
books stats and primes the FX; **no resistance, armor or absorption is consulted** -- those live in the attack
resolution, which this path never enters. So the internet claim "aether hazards ignore aether resist" is TRUE for
painted ground (and false for skill hazards: the trap, the boss's geysers, the possessed's aura all go through the
normal pipeline; the trap's 8 % current-life component is the other unresistable piece). The damage type code table
(`Game.dll+0x10f2f0`, value-1 indexed): 1 PhysicalPierce, 2 Physical, 3 PierceRatio, 4 Pierce, 5 Cold, 6 Fire,
7 Acid, 8 Lightning, 9 Vitality, 10 Chaos, **11 Aether**, 12 ManaBurn, 13 Disruption, 14 PercentCurrentLife.
"Purity": `GameEngine::GetPuritySkills` is filled from a `puritySkillCheck` field of the game engine record that this
install does not set, and no record carries `PotionPurity` -- almost certainly a Titan Quest leftover (the geyser
markers are `CharonGeyserMarker`/`CerberusGeyserMarker`); nothing grants it.

The glowing hotspot decorations (`level art/supernatural/aetherfirehotspot_*`) are the picture; the paint under them
is the harm. The Amalgamation's own Aether Geyser (`Skill_CharonGeysers`: the 7 markers closest to the TARGET within 40 u
erupt, 3.25 u radius, 30 markers in the yard) is a separate, resistable mechanism.

## Survey (tools offline, `scratchpad hazards2.py` of the session; cells 1 u, walkability = rooms.db base plane)

Forced crossings (clean ground on both sides connected only through paint), shortest painted walk: Conflagration
90 u and Deadman's Gulch 66 u lead only into dead-end pockets (no exit to another region; 13 and 9 s at ~7 u/s =
lethal by intent); Festering Lair 44/23/12/3 u at 10 %/s (optional cave); Hargate's Lab 24 u at 15 %/s; everything
else <= 13 u = one or two ticks. The campaign never asks for more than two ticks on a route it requires.

## The mod (src/hazard.cpp, on by default; docs/controls.md has no key -- it is all sound)

`world::hazard_at(point, &rate, &type)` is the tick's own lookup for ANY world point (the player's chunk, or
`World::GetRegionContainingXZ` for a point outside its 128 u footprint; Region+0x68 = Level; SEH-guarded), cheap
enough to call hundreds of times a frame (no cache; the navmesh changes with quest gates anyway).
`world::mesh_contains` is the lane gate (closest-point within 0.2 u; `IsPointOnPathMesh` is a box test).

Three layers, all live-tunable at `/hazard` (`range vol lanes bedvol spread period gap cap radius pvol pms lo hi hyst
search on`; `?at=x,z` probes a point; `?time=N` times the passes):

1. **Lanes** (`assets/audio/hazards/sizzle/<dir>.wav`): the wall-tone rectangle (7 lanes 0.5 u apart per compass
   direction, world north = -z), each lane walked from the player to its wall (`free_distance_ray`) in 0.5 u steps,
   stopping at the first painted sample; the direction's distance is the NEAREST lane (any paint you could step onto
   counts, unlike a wall). Volume (1 - d/range)^2, range 10, loop ids 110..113, same pans as the walls. The bank has
   the wall tones' per-direction pitch bands (north ~1230 Hz, east/west ~830, south ~430) with a rough impulse texture
   (35/s, 3 ms bursts): same pitch code, different texture, so a wall lane and a hazard lane in the same direction read
   as two things. Chosen by ear over crackle (collided with the enemy sonar pulse), pulsed and whistle (collided with
   the walls). Generator: `tools/gen_hazard_cues.py` (constants at the top; loudness-matched to the walls, K-weighted).
2. **Bed** (`inside_low/left.wav` + `right.wav`, ids 114/115): while the player's own cell is painted, a 260 Hz sizzle
   pair from independent seeds, played at pan -spread/+spread (0.5; two decorrelated sources read as "surrounded",
   one centred mono reads as a lane). The mixer decodes every file to mono, hence a pair, not a stereo file
   (`preview.wav` is the pair as stereo for auditioning only).
3. **Pointer**: while inside, every 250 ms a breadth-first walk from the player's cell through painted on-mesh cells
   (radius 15) collects the clean on-mesh cells it reaches; those are flooded among clean cells (never back through
   paint) into ISLANDS, each with an entry = the exit cell of the shortest painted walk. Islands are pinged in turn
   (period 0.5 s, then a 0.8 s gap so the count is audible; cap 4, nearest first) with a synthesized 90 ms triangle
   pulse (`audio::pulse`): pitch 220 Hz at due south to 880 at due north (log), pan = east/west. Hysteresis is on the
   ORDER: an island keeps its slot while it persists (identity = shared cells) and overtakes its predecessor only when
   nearer by more than 2 u. Cost measured live: lanes 0.23 ms/frame, a search 4.9 ms (586 clean cells).

Pitch neighbours the pointer competes with: interactable ping 524 Hz, unknown 800, review "unreachable" 876; the
enemy sonar pulse is at 14 kHz. If it collides by ear, `lo`/`hi` move it.

Player settings (2026-09-13): the Ctrl+T overlay (`src/cues.h`, `screens/cue_settings.cpp`) switches the
three layers together and scales them by one "harmful ground" volume; both persist in settings.txt and sit on
top of the dev knobs. Not done: pets are not
warned, and the search treats paint as passable at a cost without rounding to ticks (a 6 u and a 9 u exit both cost one
second at a run).

**Enemy ground effects** (2026-10-22): a skill's ground harm (a Skeletal Warlock's sigil, a landed pool) is a
`ProjectileAreaEffect` entity, not paint. `src/projectiles.cpp` turns every enemy one that has stopped moving into a
`world::GroundZone` (its own radius: the +0xd18 pointer vector, `[entry + 4]`; docs/devlog.md 2026-10-22) and
`world::hazard_at` answers true inside any zone (type -1) before the sector lookup, so all three layers sound it.
