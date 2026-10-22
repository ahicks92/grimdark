# Enemy attack telegraphs: what the game has, what the data gives us (2026-09-01)

Grim Dawn has no telegraph system (no ground decals, cast bars or HUD markers for incoming attacks). What a
sighted player reads is the wind-up animation, a cast-start particle burst on some skills, the game's positional
sounds, and the persistent ground hazards (pools, clouds). All of it exists as data; this doc records the
mechanism (static RE + live capture through `src/casts.cpp`, dev route `/casts`) and the measured windows.

## The cast pipeline (verified live)
Every animated skill of every character runs, in this order:
1. `SkillActivated::StartAction(Character&, targetId, WorldVec3 target, uint, TargetLeadingData)` -- the
   animation starts. `Character::GetRemainingAnimationTime()` (ms) read right after it is the whole animation.
2. The animation's callback points fire through `Character::AnimationCallback(Name)` and, for the skill
   ones, `SkillManager::HandleSkillAnimationCallback(Name, uint slot, uint targetId, WorldVec3)` (`this` =
   Character+0x850; the `Character::` export of that name is a thunk the game never calls).
3. On a hit callback the skill's `HitAction(Character&, Name callback, targetId, WorldVec3)` runs, and the
   concrete class's `ActivateNow(...)` inside it does the geometry (wave, radius, projectile launch...).
4. `EndAction(Character&)` at the animation's `End` callback.

`GAME::Name` is a 32-bit **FNV-1a** digest of the string (case-sensitive; verified against RightHandHit /
LeftHandHit / R Footstep / End). The engine's callback vocabulary is fixed: the dispatcher compares against 34
strings, of which the hit names are `Hit`, `LeftHandHit`, `RightHandHit`, `FootHit`, `SpecialHit01..04`; the
rest are swipe on/off (weapon trails), PS1..3 Start/End (particles), SkillProp add/hide/remove, SkillSound1/2,
`SkillScript`, `End`. The shipped animations use only the first three hit names. Hit callbacks repeat freely
within one animation (max 5 in the shipped data: 121 of 967 hit-bearing animations are multi-hit), each
repetition is its own HitAction call.

Not through the pipeline: instant activations (`ActivateNow` called directly -- death explosions such as the
Burning Dead's `detonate.dbr` arrive with callback name `End`, area pools tick on their own, buffs) and the
Titan Quest-era boss controllers (Cerberus/Graeae/Megalesios/Ormenos/Terracotta, unused by GD content). Lua can
make a monster cast (`UseSkillAction`, still the pipeline) or kill outright; it has no damage call. Everything
still lands in `CombatManager::ApplyDamage` on the victim.

**Hooking rules learned the hard way** (the game exited silently at start, twice): Game.dll folds identical
bodies. `SkillActivated::HitAction` / `::ActivateNow` are one `ret` stub shared by 1,574 exports and
`::StartAction` a `xor al,al; ret` shared by 525 -- detouring them detours every empty virtual in the game.
`hooks.cpp attach_all` now refuses bare stubs and duplicate targets. Also folded: BuffSelf::StartAction ==
Suicide::StartAction; BuffOther/BuffSelf/Weapon/Suicide::HitAction are one vtable-redispatch thunk.

## Wind-up windows
Static (Creatures.arc .anm trailers: `CallbackPoint { name, frame }`, 30 fps, 1,770 files, 375 enemy
attack/special/cast animations with a hit callback), base rate before the monster's attack/cast speed
multiplier (1.0 for most, 0.75..1.4 across the bestiary):
- basic melee attacks (158): median 0.57 s, quarter under 0.43, 90 % under 1.0
- specials, stomps, slams (71): median 0.80 s, quarter over 1.03, slowest 1.87
- casts/spells (94): median 0.63 s, tail to 2.4 s (Loghorrean) and 2.9
- overall 3 % under 0.3 s, 27 % under 0.5 s, 77 % under 1.0 s; follow-up hits in a multi-hit land 0.15..1.0 s apart

Live (`/casts`, Lower Crossing, spawned monsters; first hit after StartAction, median):
- zombie basic 0.56 / 0.93 (two anims); Flesh Hulk basic 0.67; Warden basic 0.70; skeleton 0.70; rifthound 0.53
- gunman (ranged basic) 0.23, then projectile flight; dermapteran reaver 0.27; groble tracker 0.10
- Warden aether arc (wave) 0.53, aether wave 1.53, charge 0.55; Flesh Hulk aether smash (wave) 0.90, charge 0.06
- groble fireball (projectile) 1.05; vile brew 0.28; ice spike burst 0.31; wisp zap (tendril) 1.17 + 1.53 (2 hits)
- self buff 0.46; groble heal (buff other) 0.30
EndAction follows at 0.9..2.3 s. Ranges match the static numbers scaled by the monster's speed.

## The shape taxonomy (concrete `Skill_*` class of the skill object, readable live via RTTI)
Monster skill records by template (nonplayerskills, base game): attackwave 120, attackradius 114,
attackprojectileburst 100, attackweapon 93, attackprojectile 92, attackprojectilering 80, spawnpet 65,
attackprojectileareaeffect 64, monstergenerator 48, attackweaponcharge 38, attackspellchaos 14 (plus
passives/buffs). Geometry fields per template (`tools/arz.py`):
- **Melee swing** (WeaponPool basic, AttackWeapon, AttackWeaponCharge): reach = weapon, `skillTargetAngle`
  20..240 (charges 90), `skillTargetNumber` 1..6. A charge closes distance first (`characterRunSpeedModifier`).
- **Radius around the caster** (AttackRadius): `skillTargetRadius` 1..35, median 5; profile Melee/Short.
- **Directional wave** (AttackWave): `waveDistance` 8..16 (median 9), start width ~2.5 -> end width ~3 (up to
  18), `waveTime` ~1 s of travel. Caster facing matters.
- **Aimed projectile** (AttackProjectile, ProjectileBurst 1..6 shots, ProjectileRing 8..18 shots in all
  directions): flight time, `projectileExplosionRadius` 0.1..6 (median 2.5). Rings are "everywhere near".
- **Ground pool** (AttackProjectileAreaEffect): a projectile that leaves a `ProjectileAreaEffect` entity,
  radius 1.8..8 (median 3), `skillActiveDuration` 5..16 s (mostly 6..8). The Warden's ring: 3 u for 6 s.
- **Beam/tendril** (AttackSpellChaos): multi-hit line to the target, `Maximum` range.
- **Summon** (SpawnPet, MonsterGenerator; the Warden's aether trap is a summoned monster with a detonate
  AttackRadius).
- **Buffs** (BuffSelf*, GiveBonus, BuffRadius*): no threat geometry.

The Warden: 12 skills -- 4 passives, a rally, the aether trap summon, the aether ring (dying skill too), charge,
aether wave (8 u, 2->4 wide, 5 s cooldown), aether arc, aether zap (tendril).

## Dev
`/casts?lines=N` (cast/hit/end/instant lines), `/casts?cb=1` (every animation callback incl. footsteps),
`/casts?clear=1`. Spawn a monster at the character: `/lua?code=local p = Player.Get(<id>); local o =
Entity.Create('records/creatures/enemies/<x>.dbr'); o:SetCoords(p:GetCoords())` (Character.Create failed);
`p:ToggleInvincible(true)` first. Ranged AI needs 12+ u: spawn, then `/teleport` the character away.

## Built: the cue layer (2026-09-01, `src/telegraph.cpp`, fed by the StartAction hooks; verified firing live, not yet heard)
Five cues, one per reaction, from the skill object's concrete class (`telegraph::shape_of`): **swing** (weapon attacks,
charges, kicks -- get out of reach), **stomp** (AttackRadius -- step away from the caster), **wave** (AttackWave, spell
cones, line fans -- get off the line), **shot** (projectiles, bursts, spells, beams, pool launches, AND a weapon attack
started from more than 4.5 u = a ranged weapon -- sidestep), **ring** (ProjectileRing -- run outward). Buffs, summons,
moves and unknown classes are silent; pools are a hazard for the sonar/world layer, not a telegraph. Fires only for foes
(`world::is_foe`) within 25 u, once per caster per 80 ms (the weapon-pool wrapper and the basic attack both StartAction
in the same frame). Positioned with `world::ear_frame` + the rear shelf, no ramp yet.
The sounds are the five words spoken by Zira ("Microsoft Zira Desktop" through SAPI, the in-game positional voice),
silence-trimmed and time-compressed with ffmpeg `atempo` (pitch kept) to 200 ms (a 100 ms set was tried and dropped: too clipped to read), K-loudness-matched to the sonar's enemy cue:
`assets/audio/telegraphs/<shape>-<ms>.wav`, regenerated by `uv run tools/gen_telegraph_cues.py`.
Dev: `/telegraph?on=1|off=1&vol=&ms=100|200&test=<shape>` prints the recent cues and skip counters.
Player control: T in the world opens the announcement toggles (outgoing / incoming / telegraph cues), persisted in
settings.txt. Level: the files are K-loudness-matched to `units-enemy.wav` (-13.9 LKFS) and played above the master
volume on the voice rolloff (`world::voice_gain`); the first attempt (ping curve x master) was ~8 dB too quiet by ear.
Modes (2026-09-01): off / your target (`world::reviewed_id()` or the game's combat enemy) / highest tier (the caster's
MonsterClassification >= the top classification among enemies within 25 u, refreshed every 300 ms -- a trash pack
all speaks, a hero's adds do not) / all; plus a per-shape enable. Settings keys `telegraph.mode` (0..3) and
`telegraph.shape.<name>`. Dev: `/telegraph?mode=&shape=<name>,0|1`.

## Class table (2026-09-20): exact names, seven reactions, unknown = silent + counted

The first cut keyed the reaction on substrings of the class name. A survey of every nonplayerskills record (about 1300
active skills across 70 classes, `tools/arz.py`) showed the vocabulary composes: "AttackRadius" is also inside the
toggled damage auras (39 records), the timed auras (22), the rains at a point (14, the Ancient Shambler's avalanche:
6 rocks every 2.5 s for 10 s over a 10 u drop radius, aimed at the ground up to moderate range), the lightning bolts
(21: an instant strike at the point picked at cast start, 1.5-2 u radius, long range -- no projectile, same counterplay
as a shot) and the on-hit retaliations (8); "Projectile" inside drops, orbiters, mine layers and teleports (31). About
one active skill in ten got a cue that sent the player the wrong way ("stomp" = step away from the caster, into the
rain). `telegraph::shape_of` is now a table by exact class name -- every class of the survey is listed -- with two more
reactions: **area** (rains, drops, orbiters, geysers, mine layers, timed auras: leave the spot) and **charge** (charges
and blink-strikes: it closes the distance itself; running does not help). Toggled auras, passives, buffs, curses,
summons, moves and markers are deliberately silent (no wind-up moment). A class not in the table is silent and
counted (`/telegraph` lists `unknown class <name> xN`, the log has one line per class), so a game patch adding a class
shows up instead of being guessed. Cue words: `tools/gen_telegraph_cues.py` (Zira, 200 ms, loudness-matched).
Not covered, by decision (2026-09-20): dying skills. RE of the timing: on death the controller enters
`ControllerMonsterStateDying`; its ctor copies the record's `lootDropCallbackPoint` / `dyingSkillCallbackPoint`
(controllermonster.tpl, default "", no creature record overrides either) into two `Name`s, and `OnBegin` runs
`StartDyingSkill` at once when the name equals Engine's `Name::noName` (zero-initialized .data, 0 live) -- an empty
field leaves the Name 0, so EVERY monster fires its dying skill in the first frame of death, never on a death-animation
callback (`HandleEvent` is the other caller, for a named callback point nobody uses). The activation is direct (the
hit arrives with callback name `End`), so the Ancient Shambler's 8 u burst lands on whoever is in reach at the killing
blow; the 83-frame death animation is only the picture. Nothing a telegraph can do; the only useful player-facing
thing would be a per-record "explodes on death" note (545 monsters carry a dying skill, many of them chest / loot
spawns) -- left alone for now.

## Reactions revised (2026-10-22): "area" = a spot, "aura" = on the caster, per-record shapes
The user reviewed the whole table (every class with its counts and geometry, from a `tools/arz.py` survey) and found
three cues misleading:
- **Pools and lightning strikes are "area", not "shot"**: `Skill_AttackProjectileAreaEffect` and
  `Skill_AttackRadiusLightning`/`2` land on the point picked at CAST START (verified live on Kilrian's Conflagration:
  the target point is unchanged at release even with the player 4.3 u away; the pool appears there ~0.65 s after the
  cast starts, at speed 0, no flight). The counter is "leave the spot you stood on", which is what "area" says. The
  skill data has no warning marker on the ground for either (only impact effects): a sighted player sees only the
  caster's casting animation, shared by all its spells (Kilrian: one `sHandedSpellAttackAnim` for every spell).
- **New "aura"** (`assets/audio/telegraphs/aura-200.wav`, same generator): harm centred on the caster that moves with
  it -- `Skill_BuffAttackRadiusDuration` (timed auras, Kilrian's Ring of Fire, which also auto-casts Fireblast with no
  animation), `Skill_AttackProjectileOrbiting`, `Skill_BuffAttackRadiusLightning` (was silent). "area" had read as a
  spot on the ground. The always-on toggled auras stay silent at cast (they are switched on once): a zone following
  the monster is the open thread.
- **Per-record shapes before the class**: of every Ring/Burst/Fan record, two "rings" are narrow aimed fans
  (bloodclaw_bloodyhook 6 over 35 degrees, baldim_chaoswave 8 over 90) and play "wave" (a frontal cone: step sideways), as
  does the whole `Skill_AttackProjectileFan` class (12 skills, 3-5 projectiles over 18-65 degrees); the 180-degree bursts launch
  2 projectiles, one to each side, and stay "shot". `Skill_WeaponPool_Default` (the pool wrapper started with the basic
  attack it picks) is listed as silent instead of counting as unknown.
- Still "shot" but known imperfect: beams (`Skill_AttackSpellChaos`/`ChaosBeam`: per-frame `Update` +
  `GetValidTarget(target id, range)` + `StopSkill` suggest a channel locked on its target -- break range or line of
  sight, unverified live) and chains (`Skill_AttackChain`: no projectile, `GrowChain`/`CreateLightning` bolt by bolt,
  up to 3 jumps within 8 u, requires line of sight; whether the first bolt goes to the target id or the cast-start
  point is unverified).
- **"beam" and "frontal"** (later the same day): `Skill_AttackSpellChaos`/`ChaosBeam` play "beam" -- live, an Aether
  Crystal Cluster's `aethercrystalsmall_zap` (level 8 champion) targets the player id, hits once ~0.42 s after its start
  and ends at ~0.9 s; the player's log has a "hit" matching the zap that landed with the player 3.6 u from the
  cast-start point (the 6.37 s gap between the two hits fits zaps 2-3, 6.43 s, over 1-2, 6.22 s), so it is locked on,
  not aimed at a point. "frontal" (the user, after WoW) = a spread covering 90 to 180 degrees in front (45 either side at
  least; wider than a half circle is a ring -- "frontal" past 180 would confuse, the user); `tools/gen_telegraph_records.py` writes the per-record shapes to
  `src/telegraph_records.h` from each skill's widest level (projectileLaunchRotation; a wave's side angle from its
  length and widths -- no ground wave reaches 90, the widest is 45). Correction: the 180-degree bursts launch up to 7
  projectiles at their top levels, not 2 (the first survey read level 1). Melee arcs (special weapon attacks up to 200
  degrees, charges up to 240) stay swing / charge for now.
