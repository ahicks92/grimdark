# Skills, the quickbar, and how the game targets (RE 2026-08-23)

How a blind player is told what a skill will do, and the hot-slot / mouse-slot machinery behind the quickbar
keys. Player-facing keys are in `docs/controls.md`; the model layer is `src/gameapi*.cpp`; the aim resolver is
`world::skill_aim` in `src/world.cpp`.

## How the game decides player-position vs mouse

Every player skill instance is a `GAME::SkillActivated` subclass. It carries a target-type field at `+0x5c0`,
returned by the virtual `SkillActivated::GetTargetType()` (Game.dll export; the game itself calls it through
`SkillManager::GetTargetType`, dispatching the skill object's vtable). The body is trivial:

    SkillActivated::GetTargetType()  ->  return *(int*)(this + 0x5c0);

and it is **never overridden** (only `SkillActivated` defines it), so once an object is confirmed to be a
`SkillActivated` the export can be called directly on it. It returns a `SkillTargetType`.

**The runtime values are NOT the DBR `targetingMode` enum.** The DBR field's authoring order is
`Default;Point;Object;Target`, but the *runtime* `+0x5c0` is a resolved value the class sets, and it was read
straight off the game (2026-08-23, `/hotbar` over known Soldier skills):

- **1 = self / buff** — cast on you (or allies), no aiming. Overguard (`Skill_BuffSelfDuration`), the health /
  energy potions (`Skill_ChargePotion`), Field Command (`Skill_BuffRadiusToggled`).
- **2 = offensive** — the game aims it at your target / where you're facing. Basic Weapon Attack, Cadence,
  Blitz, Forcewave, **and player-centred AoE like War Cry** (`Skill_AttackRadius`).
- **3 = a ground point** — go to / place at a location. Evade, Move To are actually `0` (they came back as
  "target" only through the old miscoded fallback); `3` is inferred for cursor-placed skills, not yet seen on a
  Soldier — treat as unconfirmed.
- **0 = not applicable** — passives, and `SkillActivated` skills that expose nothing. Menhir's Will
  (`Skill_PassiveOnLifeBuffSelf`) is not a `SkillActivated` at all → `None`.

`2` is the one that needs splitting for the readout: a player-centred AoE (War Cry) is a `2` like a targeted
attack, so it is separated by the skill's concrete RTTI class name (each `Skill_*` class has its own
`GetStaticClassInfo`, so `GetRTTIClassInfo` returns e.g. `Skill_AttackRadius`, `Skill_WeaponPool_ChargedFinale`):
class name contains `Radius` → **around you**, else **at a target**.

The player-facing buckets (`world::SkillAim`, spoken by `screens::speak_slot` / `speak_mouse`):

- **self** — value 1 (Overguard, potions, stances).
- **around you** — value 2 with a `Radius` class (War Cry).
- **at a target** — value 2 otherwise (Weapon Attack, Cadence, Blitz, Forcewave).
- **at a spot** — value 3 (cursor-placed; unconfirmed on Soldier) and value 4 (a pure cursor point: Vire's Might,
  Nullification, the GDX2 medal runes; added 2026-10-02 -- before that they read as passive and the hotbar manager
  hid them). Direct aim hands them the locked enemy or the locked point / free cursor like any other skill.
- (nothing spoken) — value 0 or not a `SkillActivated`.

**Value 2 does NOT require an enemy** (static RE 2026-09-15, `docs/masteries.md` run over all 289 class skills: no
class skill reads 3; Nullification and Vire's Might read **4** = a pure cursor point, unknown to `skill_aim`). The
request takes the combat enemy (`[controller+0x468]`, `re_movement_skills.md` s.2) and then
`SkillActivatedWeapon::GetValidTarget` (Game.dll 0x505e20) -> `Skill::GetValidMeleeTarget` (0x4829a0) /
`GetValidRangedTarget` (0x482b00): with target id 0 these would search `GameEngine::GetTargetsInRadius` around the
point, but only for a radius > 0, and `DefaultRequestSkillAction` always passes 0 (xmm9, zeroed at 0x1509c9, passed at
0x150dcb; corrected 2026-09-23) -- so with keyboard and mouse there is NO search: id stays 0 and, unless the
require-enemy flag is set, it returns true -- the skill fires at the point. The only tolerance around the cursor is the
exe's own pick (exe+0x30b30): a sub-frustum around the cursor pixel, half-width `[pick+0x48] * 0.024 + 0.011` of the
window width (square in pixels; about 18-56 px, 0.5-1.5 u at our zoom; `[pick+0x48]` not identified) (Blade Arc swings toward it, Forcewave travels toward it). A cursor entity that
fails `ValidateEnemy` (an ally, a non-targetable prop) is likewise cleared to a point, not refused. Only the charge
classes (Blitz `Skill_AttackWeaponCharge`, Shadow Strike `..Blink`) come with the flag set / their own validator and
are refused without an enemy. Mod-side consequence: the virtual cursor only ever sits on a reviewed entity or an exit
point (`world::lock_point`), so a ground aim for these skills is possible in the game but has no key yet.
**Cursor-placed summons are type 2 too** (2026-09-16): the 16 `Skill_TargetedSpawnPet` class skills (Inquisitor Seal,
Wendigo/Storm Totem, Wind Devil, Blade Spirit, Mortar Trap, Thermite Mine, the Runes, Raise Skeletons, the Familiar /
Hellhound / Briarthorn / Primal Spirit / Blight Fiend / Guardian summons) have DBR `targetingMode = Point` and an
`ActivateNow(..., WorldVec3 const&)` that spawns at the point, but their runtime `GetTargetType()` is 2 and they take the
base `SkillActivatedSpell::GetValidTarget` -> `Skill::GetValidRangedTarget` (0x482b00): id 0 -> `GetTargetsInRadius`
near the cursor point, else the point itself -- so "at a target" is a misnomer for them (`docs/masteries.md` labels them
"placed at the cursor"); the spoken word is still `kAimTarget`, a wording decision pending.

The game does the actual targeting natively (the number keys pass straight through); the mod only *says* which
bucket a slot is, so the player knows before firing. Verify with `/hotbar` (each slot prints `aim=`).

Verified live (2026-08-23, test character, `/hotbar` with skills assigned to slots): Cadence / Weapon Attack /
Blitz / Forcewave → `target`, War Cry → `around`, Overguard / Field Command / potions → `self`, Menhir's Will
→ `-`. The read handlers themselves were confirmed with `/action read.leftMouse` → "left mouse Weapon Attack,
at a target".

**Input note:** `Ctrl+<digit>` is the read chord, but a digit is also a passthrough key (bare digits activate
the game's slots). `in_game`'s passthrough is by code alone, so `app.cpp`'s game-key filter additionally
swallows `0x02..0x0b` while Ctrl is held — otherwise a real Ctrl+1 would read *and* activate slot 1. (Synthetic
`/key` events bypass that filter entirely and always reach the game, so in-world Ctrl-chords can't be verified
through `/key`; use `/action` for the mod side.)

## Hot slots (the quickbar model)

- `Player::GetPlayerHotSlotCtrl` → `HotSlotCtrl`; `GetHotSlotOption(index)` → a `HotSlotOption`. Subclasses
  (`HotSlotOption::GetType`): Skill, Potion, PotionSkill, Scroll, Evade. Only `HotSlotOptionSkill` carries a
  skill id (`GetSkillId`); potions/scrolls/evade have none, so their aim is `-`.
- 47 slots per weapon config. The four displayed bars start at index **0 / 14 / 26 / 36** (10 slots each; the
  gap holds the config's own mouse/extra entries). `quickbar_slot_index(bar, k)` maps them; the HUD's current
  bar is `InGameUI+0x72f0` (`exe_ui::quickbar_page()`, 0..3), cycled by the game's own **Y = Quickbar Switch**.
- The two **mouse** slots are separate: `HotSlotCtrl::GetPrimarySlot` (left) / `GetSecondarySlot` (right).
- `HotSlotOptionSkill::kAlternateEquipmentFlag/Mask` — a slotted skill can be tied to a weapon set.
- Assigning a skill: build a `HotSlotOptionSkill` with its ctor in our memory, `SetPlayer`, then `SetHotSlot`
  (bar) / `SetPrimarySlot` / `SetSecondarySlot` (mouse); the game deep-copies our option. `SetPrimarySkillId`
  did **not** move the slot when tried (2026-08-22) — use the slot setters.

## When a skill can / cannot be on the mouse

Tested live (2026-08-23): the assignment path (`SetPrimarySlot` / `SetSecondarySlot`, what
`gameapi::set_primary_skill` uses) accepts **any learned skill** on the mouse buttons — a pure self-buff
(Overguard) took the left mouse and reads "left mouse Overguard, self". So there is **no self-buff restriction
at the API level**; the belief that the mouse rejects buffs was a red herring. The only thing that fails to
stick is an **unlearned** skill (level 0), which the game drops regardless of the slot (bar or mouse).

Caveat: this is the direct API, which the game's own drag-and-drop UI may gate more tightly, and I did not
click a mouse-slotted self-buff to confirm it actually casts. But for the mod (which assigns through the API),
every learned skill is mouse-assignable, and the readout reports whatever is there with the right aim.

## Learning requirements, modifiers, and spirit-guide reclamation (RE + live 2026-08-24)

The skills-window click handler (exe+0x248380) branches on a reclaim-mode flag: nonzero -> a click reclaims a
point (`DecrementSkillLevel` + `SkillManager::UseReclamationPoints`), zero -> it learns (`IncrementSkillLevel` +
`SubtractSkillPoint`). The **learn branch only checks points>0 and level<max** -- the requirement gate lives in
the icon-enable logic (the SkillReasons builder exe+0x2492b0), which computes, per skill: byte0 no skill points,
byte1 `Skill::GetMasteryLevel < GetMasteryLevelRequirement` (mastery bar too low), byte2 modifier's base skill
not enabled, plus a mastery-slot rule. Driving `IncrementSkillLevel` directly (as the mod does) bypasses that
gate, which is why learning ignored requirements.

- **`gameapi::can_learn_skill(skill)`** replicates it with exports: points>0, level<max, `GetMasteryLevel >=
  GetMasteryLevelRequirement`, and the game's own **`Skill::IsBaseSkillEnabled`** (byte 2 verbatim: true when the
  skill has no base skills, else when one of them has a level). Returns "" or the spoken reason
  ("needs mastery N", "requires <base>", "no points"). `learn_skill` refuses on a non-empty reason. The mastery
  ("class training") skill has req 0 / no base, so it is always learnable (raising the bar); choosing a *new*
  class stays a separate flow (`skills_set_pane`).
- **Sub-skill -> base link (corrected 2026-09-13).** `Skill::GetModifiedSkillId` (`*(uint*)(this+0x1e0)`) is a
  *different* (transform/replace) relationship and reads **0** for tree skills. The tree keeps THREE id vectors on a
  `Skill`: `GetModifiers()` (+0x140, the `Skill_Modifier` sub-skills), `GetSecondarySkills()` (+0x170, the
  `SkillSecondary` sub-skills) and, on the sub-skill itself, **`GetBaseSkills()`** (+0x1b8, the forward link, which
  `IsBaseSkillEnabled` walks). `skills()` now fills `SkillInfo::modified_skill_id` from `GetBaseSkills` (reverse of
  the other two only as a fallback) and `SkillInfo::modifier` is `IsSkillModifier || has a base` (NOT `Skill::IsSecondary`, byte +0xad: live it is true on Summon Familiar and false on Storm Spirit).
  - Why: the Occultist's pet modifiers (Mend Flesh, Storm Spirit, Lightning Strike; Ember Claw, Hellfire, Infernal
    Breath) are `SkillSecondary_PetModifier` records, NOT `Skill_Modifier` -- `IsSkillModifier` (an is-a test
    against `Skill_Modifier`) said false, and the reverse of `GetModifiers` never saw them (verified live: Summon
    Familiar's modifier vector empty, its secondary vector = the three ids). So they read as plain skills, could be
    learned without the summon, and -- the spirit-guide bug -- the summon's last point could be reclaimed under
    them, because `can_reclaim_skill`'s dependants pass keys off `modified_skill_id`. The Shaman's Corrupted Storm
    was fine only because `Skill_SpawnPetTransmuter` is-a `Skill_Modifier` despite its `_petmodifier` file name.
    The exe's byte 0xa uses `Skill::GetSkillDependancies` + `Character::FindSkillId`, the same relation by record
    name. Built; NOT yet verified live (a character orphaned by the old gate -- Storm Spirit 2, Summon Familiar 0 --
    stays that way until a point goes back into the summon or Storm Spirit is reclaimed).
- **Reclaim mode = a spirit guide.** The NPC class is `NpcSkillReallocator`; talking to one calls
  `GameEngine::DisplaySkillReallocationWindow` (forwards through `[GameEngine+0x19b0]` vtable+0x60), which opens
  the skills window with the reclaim flag set. That flag is **skills window +0x1f4c** (the handler reads it as
  `[controller+0x1e1c]`, controller = window+0x130; verified live: only that path flips window +0x1f49/+0x1f4c
  0->1 and writes a controller pointer at +0x2634). `exe_ui::skills_reclaim_mode()` reads +0x1f4c.
  **Corrected 2026-09-20** (a one-class character's guide opened a plain window): the opener exe+0x21a6f0 writes the
  WINDOW byte +0x2639 = 1 (+0x2634 = the npc id, not a controller), calls vt+0xa8(1) on both pane slots (UISkillPane
  sets its +0x1e4c; the class-selection pane's slot is a bare ret), then Show(true); the window reads +0x2639 itself
  and clears it on teardown. `skills_reclaim_mode()` now reads +0x2639, else either mastery pane's +0x1e4c, else the
  old +0x1f4c proxy. The game rests a one-class character's window on tab 1 (the class-selection pane), which is why
  the per-current-tab read found no flag; `skills_press_skill` now searches both panes for the skill and the Undo
  Points helpers take the screen's tab.
  - `refund_skill` (Backspace) is only wired by the screen in reclaim mode -- outside a guide it does nothing
    (fixing the old "refund anywhere, silently charging iron bits" bug). Cost is
    `SkillManager::GetCurrentSkillReclamationCost()` (`gameapi::reclaim_cost()`), the same for every skill and
    rising as you reclaim; there is **no clear-all**, it is one point at a time.
  - **Where the game validates a reclaim (RE 2026-08-30):** NOT in Game.dll and NOT in the click. Base
    `Skill::DecrementSkillLevel` (Game.dll 0x46d520) is level > 0 -> subtract -> notify -> return true, no checks;
    the exe's "-" branch (exe+0x248459) only refuses a mastery at level 1. The gate is the pane update greying the
    icon (`[icon+0x281] = 1`, exe+0x247bf6..0x247cde) from the SkillReasons block at icon+0x4c0: byte 8 cost > money,
    byte 0xa level 1 with a skill whose `GetSkillDependancies` names it still holding points (tagReclaimBase), byte 7
    level 1 hosting a celestial power (autocast not from the DBR, operation 3; tagReclaimDevotion), byte 0xb level 0;
    for the mastery additionally `Engine::IsExpansion1Loaded`, byte 0xd (a learned skill of the pane with
    `GetMasteryLevelRequirement >= the bar`, helper exe+0x2491a0) and level == 1. `WidgetB::press` refuses a greyed
    icon, but the screen's fallback `refund_skill` called `DecrementSkillLevel` directly -- which is how a base
    skill's last point could be reclaimed under its modifiers. Now `can_reclaim_skill` replicates every byte
    ("remove points from its modifiers first, Discord", "detach its celestial power first, Twin Fangs",
    "Cadence needs mastery 1" for the bar) and `refund_skill` refuses unless it passes; only the expansion check
    is not modelled (a greyed icon then says "cannot"). `gameapi::hosted_power_id` is the byte-7 test as a helper.
  - `gameapi::can_reclaim_skill` gives the refusal reason before trying: **the mastery bar reclaims down to 1
    like any skill** (base `Skill::DecrementSkillLevel`, the same vtable slot as a normal skill -- verified
    5->4->...->1 live), but the game blocks its **last** point (can't drop the class to 0 ->
    `tagDecreaseMasteryError`); reclaiming costs iron bits, so `reclaim_cost() > money()` -> "not enough iron
    bits" (this was the real cause of an earlier mis-read that "masteries can't be reclaimed").
  - Dev: `/reclaim` opens the skills window in reclaim mode without a guide (`gameapi::dev_open_skill_reclaim`);
    `/cheat?bits=N` (`Character::AddMoney`) tops up iron bits to test affordable reclaims.

Attribute points (Physique/Cunning/Spirit, the character sheet) are **never refundable** in Grim Dawn -- the
stats-tab rows wire no reclaim path and `ResetAttributePointsConfigCmd` is never called. The stats tab also now
carries the game's own tooltips (Space): `tagCharAttributeDescription0X`, `tagCharStats{OA,DA,DPS}Description`,
`tagStatsResistance0XDesc`.
