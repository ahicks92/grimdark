# Devotion at the Game.dll level (static RE 2026-08-27, offline only -- NOT verified in a running game)

All RVAs are Game.dll v1.3.0.8 unless prefixed `exe+` (the unpacked `build/GrimDawn.unpacked.bin`).
Names written `GAME::X::Y` are **exported** unless the line says "unexported"/"inline".
"verified" = read in the disassembly; "inferred" = deduced from surrounding code or the records.
Calling convention throughout: MSVC x64 (`this` in rcx; class-by-value returns take a hidden pointer as
the 2nd argument -- `GetTemplateAutoCast`, `GetAutoCastControllerName` are the ones that matter here).

## 0. The shape of the system (summary, verified)

- A **constellation** is a UI record (`database/templates/ingameui/devotionconstellation.tpl`), not a game
  object. It names 1..N **stars** (`devotionButton1..N` -> `records/ui/skills/devotion/tierX_YYz.dbr`), the
  affinity it **requires** and the affinity it **gives**.
- A **star** is a real `GAME::Skill` object, created from `records/skills/devotion/_devotiontree.dbr` (the
  player record's `devotionTree` field). 438 entries; each has `skillName<i>`, `skillLevel<i>`, `skillType<i>`.
  `skillType` is **`Passive` for 388 stars and `Effect` for 50** -- the 50 Effects are the celestial powers.
- Taking a star = `Skill::IncrementSkillLevel` on the star's Skill + `Character::SubtractDevotionPoint`.
  Completing every star of a constellation grants its affinity via `Character::AddAffinity`.
- A **celestial power** (`skillType = Effect`) must additionally be **assigned to one of your own skills**.
  The assignment is `Skill::SetAutocastSkill` on the HOST skill, plus the power's `devotionParent` field
  (`Skill+0x1d0`) recording the host's object id.
- An assigned celestial power **gains experience and levels** (`SkillManager::AddExperience`, driven by
  `Character::ReceiveExperience`); its devotion level then becomes its effective skill level.

## 1. Skill: the devotion fields

Field offsets on `GAME::Skill` (all verified by reading the one-instruction accessors):

```
+0x0c8  u32   base skill level                (Skill::GetCurrentLevel reads it)
+0x0d0  u32   devotion level                  Skill::GetDevotionLevel      0x47c080
+0x0d4  u32   devotion experience             Skill::GetDevotionExperience 0x47c0b0
+0x1d0  u32   devotion parent (object id)     Skill::GetDevotionParent     0x48d670
+0x1fc  u32   skill level augment             (added by GetCurrentLevel)
+0x2fb  u8    flag set when an autocast is attached
+0x2fd  u8    flag set by SetSkillOperation(!=0) and by SetAutocastSkill
+0x320  f32   autocast chance   (written by the exe from SkillAutoCastController::GetParams)
+0x324  u32   autocast trigger  (ditto)
+0x398  std::string  autocast controller name  Skill::GetAutoCastControllerName 0x479b90
+0x3b8  Skill*       attached autocast skill   Skill::GetAutoCastSkill          0x479b80
+0x3c0  u8           "has autocast in dbr"     Skill::HasAutocastInDbr          0x479be0
+0x450  std::string  template autocast name    Skill::GetTemplateAutoCast       0x52e9c0
+0x4a0  enum Skill_Operation                   Skill::GetSkillOperation         0x479e70
+0x520  mem::vector<pair<AffinityType,u32>> affinity DEPENDENCIES  GetAffinityDependencies 0x47c280
+0x538  mem::vector<pair<AffinityType,u32>> affinity BONUS         GetAffinityBonus        0x47c2a0
+0x550  mem::vector<std::string>            constellation deps     GetConstellationDependencies 0x47c2c0
+0x568  u8    constellation self locked                            GetConstellationSelfLocked   0x47c2e0
```
Setters: `SetDevotionLevel` 0x46dc70 (plain store to +0xd0), `SetDevotionParent` 0x52e9b0 (plain store to
+0x1d0), `SetAffinityDepedency` 0x47c270, `SetAffinityBonus` 0x47c290, `SetConstellationDependencies`
0x47c2b0, `SetConstellationSelfLocked` 0x47c2d0, `SetSkillOperation` 0x479e80 (also sets +0x2fd when != 0).

**`GetDevotionMaxLevel` is NOT a Skill field** (0x47c090, verified):
`return this->vt[0x578]()  /* SkillProfile */ ->[0x178];` -- `SkillProfile::GetDevotionMaxLevel` 0x3edb70.
`SkillProfile+0x178` is filled in `SkillProfile::LoadProfile+0x1ee` (0x5309be) as
**the element count of the DBR array `skillExperienceLevels`** (vector at profile+0x160..+0x168), and only
when that count is > 1. Verified. So a star with no `skillExperienceLevels` has devotionMaxLevel 0.
`records/skills/devotion/*_skill.dbr` (the celestial powers' actual skill records) DO carry
`skillExperienceLevels` (15 entries for tier2, 20 for tier1), so those are the ones that can level.

### Skill_Operation (verified end to end)
The save/tree string -> enum converter is the unexported body at Game.dll+0x52e8d0 (reached from
`SkillManager::LoadSkills`):
```
"Skill"   -> 1
"Passive" -> 2      (lea eax,[rdi-5] with rdi = strlen 7)
"Effect"  -> 3
anything else -> 1
```
Skills loaded through the non-devotion path get **0** (`LoadSkills` passes `r9d = 0`), which is why the exe
tests `GetSkillOperation() == 0` for "an ordinary class/item skill" (exe+0x1d4ca9). So:
**0 = ordinary skill, 1 = Skill, 2 = Passive (a devotion star), 3 = Effect (a celestial power).**
No `.dbr` in the shipped database sets `skillOperation`; the value comes from the devotion tree record's
`skillType<i>` field at load (Game.dll+0x52eb17 reads the field name `skillOperation` in a different,
save-stream context).

### `Skill::GetCurrentLevel` -- devotion level IS the power's level (verified, 0x47bf00, virtual vt+0x1c8)
```
lvl = this[0xc8];
if (lvl) lvl = min(lvl + this[0x1fc], profile[0x150] /* skillMaxLevel */);
dev = this[0xd0];
if (dev > 1) return min(dev, profile[0x178] /* devotionMaxLevel */);
return lvl;
```
So a celestial power whose devotion level has grown past 1 reports that as its skill level.

### `Skill::SkillLevelChange` (0x46d640, virtual vt+0x78) -- why devotion powers keep their own level
When the skill has an attached autocast (`this+0x3b8`), it pushes its own `GetCurrentLevel()` onto the
attached skill with `SetSkillLevel` -- **unless the attached skill's `SkillOperation == 3`**
(`cmp dword [rbx+0x4a0], 3; je skip` at 0x46d694). Verified. That is the explicit carve-out for celestial
powers: the host skill's level does not overwrite the power's devotion level.

### Devotion experience / levelling: `SkillManager::AddExperience(unsigned)` 0x52b690 (verified)
Only caller: `GAME::Character::ReceiveExperience+0x2be` (0x5fffe). Not imported by the exe.
For every skill in the manager's skill list (`SkillManager+0x20..+0x28`):
```
host = ObjectManager::GetObject(skill[0x1d0]);           // the devotion parent
require is-a Skill(host)
require skill[0xc8] != 0                                 // the star is taken
require profile[0x178] != 0                              // it has skillExperienceLevels
require host && host[0xc8] != 0                          // the host skill is learned
skill->vt[0x90](xp)                                      // Skill::AddExperience: skill[0xd4] += xp
while (skill[0xd0] < profile[0x178]) {
    threshold = profile.skillExperienceLevels[ clamp(GetCurrentLevel(), 0, max-1) ];
    if (skill[0xd4] < threshold) break;
    skill->vt[0xa0]();                                   // Skill::IncrementDevotionLevel
    ... UI notify gGameEngine[0x19b0]->vt[0x188](), banner via Engine::ShowCinematicText,
        'tagDevotionSkillMaxLevel' when the cap is reached
}
```
- `Skill::AddExperience` 0x46dc20 = `this[0xd4] += arg` (virtual vt+0x90).
- `Skill::ClampExperience` 0x46dc30 (virtual vt+0x98) = `this[0xd4] = GetRequiredExperience(devotionMaxLevel)`.
- `Skill::GetRequiredExperience(u32 level)` 0x47c0c0 indexes `profile.skillExperienceLevels[level-1]`
  clamped to the array.
- `Skill::IncrementDevotionLevel` 0x46dc60 (virtual, vt+0xa0) = `++this[0xd0]; tail-jmp vt[0x78]
  (Skill::SkillLevelChange)`.
- There is **no "AddDevotionExperience"** export: `SkillManager::AddExperience` is the whole path, and it is
  a share of the character's own XP gain (the amount it receives was not traced back through
  `Character::ReceiveExperience`).

### "Devotion parent" means: the skill this celestial power is bound to (VERIFIED, read both ways)
- **Written** inline by the exe (which is why `Skill::SetDevotionParent` has zero callers anywhere and is
  not even imported by the exe):
  - `exe+0x18695e`: `power[0x1d0] = Object::GetObjectId(newHostSkill)` right after
    `newHost->SetAutocastSkill(power, controllerName, false)`.
  - `exe+0x1868e9` / `exe+0x186bcc`: `power[0x1d0] = 0` when the power is detached.
- **Read** by `SkillManager::AddExperience` (above) and by the exe's full-reset pass
  (`exe+0x18cb22`: `esi = skill[0x1d0]; host = GetObject(esi); host->SetAutocastSkill(0, "", 0)`).
- It is NOT the constellation, and NOT the star's parent star.

## 2. Character: devotion points and affinity

Field offsets on `GAME::Character` (verified from the accessors):
```
+0x1770  u32   devotion points AVAILABLE (unspent)   Character::GetDevotionPoints       0x60610
+0x1774  u32   devotion points TOTAL earned          Character::GetTotalDevotionPoints  0x60820
+0x1778  u32   devotion points MAX                   Character::GetMaxDevotionPoints    0x60830
+0x177c  u32[5] affinity per AffinityType            Character::GetAffinity             0x60690
+0x16c0  u32   controller object id                  (existing project knowledge)
+0x16c4  s32   iron bits / money                     (confirmed here: Player::AddMoney path,
                                                      capped at 0x77359400 = 2,000,000,000)
```
- `Character::AddDevotionPoints(u32)` 0x607b0 = `+0x1770 += n`
- `Character::AddTotalDevotionPoints(u32)` 0x607c0 = `+0x1774 += n`
- `Character::RemoveDevotionPoints(u32)` 0x607d0 = saturating `+0x1770 -= n`
- `Character::RemoveTotalDevotionPoints(u32)` 0x607f0 = saturating `+0x1774 -= n`
- `Character::SubtractDevotionPoint()` 0x60810 = `--[+0x1770]` (**no underflow guard**)
- `Character::AddAffinity(AffinityType, u32)` 0x60620 = `+0x177c[t] += n` (ignored if t > 4)
- `Character::SubtractAffinity(AffinityType, u32)` 0x60640 = saturating subtract (ignored if t > 4)
- `GameEngine::DevotionPointsInUse()` 0x2b2ba0 = `mainPlayer[0x1770] < mainPlayer[0x1774]`
  (i.e. "at least one point is spent"). Verified.

`maxDevotionPoints = 50` in `records/creatures/pc/playerlevels.dbr`.

### AffinityType (5 values, 0..4 -- VERIFIED)
`GetAffinity`/`AddAffinity`/`SubtractAffinity` all begin `cmp edx,4; ja <bail>`, so the array is exactly 5
u32 at Character+0x177c..+0x178c. The names come from the exe's constellation-record parser
(exe+0x180864..0x1808da, `_stricmp` chain over `affinityGivenName%u` / `affinityRequiredName%u`):
```
0 = Ascendant     (the fall-through case)
1 = Chaos
2 = Eldritch
3 = Order
4 = Primordial
```
`GameEngine::GetAffinityBitmap(AffinityType)` 0x2e0f60 selects `gGameEngine+0x10b8 / +0x10c0 / +0x10c8 /
+0x10d0 / +0x10d8` for 0..4 -- which lines up with `records/game/gameengine.dbr`'s
`affinity1Icon..affinity5Icon` (`devotion_affinity01..05.tex`) and with the UI tags
`tagDevotionAffinity01..05`. The icon/tag ordinal <-> enum mapping is **inferred** (positional), the enum
values themselves are read from the string compares.

## 3. SkillManager: spent points and the reclamation (respec) economy

`GAME::SkillManager` offsets (verified; from `SkillManager::Load` 0x515df0 unless noted):
```
+0x018  Character*  owner                                   (read in UseDevotionReclamationPoints)
+0x020/+0x028  mem::vector<Skill*> THE skill list           SkillManager::GetSkillList 0x8ed60
+0x068  mem::vector<u32>  masteryIncrementLevel
+0x088/+0x0a0  reclamationPointTiers / reclamationPointCosts   (ordinary skill respec)
+0x0c0/+0x0c8  mem::vector<u32> devotionReclamationPointTiers
+0x0d8         mem::vector<u32> devotionReclamationPointCosts
+0x0f0  u32   devotionReclamationAetherCost   SkillManager::GetDevotionReclamationAetherCost 0x3edaa0
+0x0f4  u32   number of devotion reclamations performed so far (runtime counter)
+0x1a0  u32[] default skill ids
+0x228/+0x230  mem::vector<Skill*> class-skill list (the one GetNumRegularSkillPoints walks)
+0x390  SkillServices*
+0x410  u8    set to 1 while loading if any tree entry had skillType "Effect"
```
DBR source (`records/creatures/pc/malepc01.dbr` / `femalepc01.dbr`):
```
devotionReclamationAetherCost = 1
devotionReclamationPointTiers = [0,20,40,60,80,100,...,380]        (20 entries)
devotionReclamationPointCosts = [25,50,100,150,200,300,...,15000]  (20 entries)
```

- **`SkillManager::GetNumDevotionPointsSpent()` 0x51fc90** (verified): walks `+0x20..+0x28`, and for every
  skill with `SkillOperation != 0` adds `skill->vt[0x1c8]()` = `Skill::GetCurrentLevel()`. Since ordinary
  skills have operation 0 and every devotion-tree skill has 2 or 3, this is "sum of devotion star levels".
- **`SkillManager::GetCurrentDevotionReclamationCost()` 0x520050** (verified): finds the largest index `i`
  with `tiers[i] <= used(+0xf4)` (walking `+0xc0`, stopping when `used < tiers[i+1]`) and returns
  `costs(+0xd8)[i]`. Iron bits.
- **`SkillManager::GetDevotionReclamationAetherCost()` 0x3edaa0** = `+0xf0` (aether crystals per point).
- **`SkillManager::UseDevotionReclamationPoints(int n)` 0x51fe70** (verified, returns bool):
  - requires the owner (`+0x18`) to be a `Player`.
  - `n > 0` (spend n reclamations): `aether = [+0xf0] * n`; loops n times accumulating
    `bits += GetCurrentDevotionReclamationCost()` while incrementing `+0xf4` each iteration (so the price
    escalates within the batch); then requires `player[0x16c4] >= bits` and `Player::GetCurrentAether()
    >= aether`, else it restores `+0xf4 -= n`, returns **false**. On success it **negates** both totals and
    sends `ControllerCharacter::SendReclaimDevotionPointCmd(-aether, -bits)`.
  - `n < 0` (give reclamations back): decrements `+0xf4` |n| times, accumulates the refunds, and sends the
    command with **positive** values.
  - It does **not** touch devotion points or skill levels -- it is purely the cost side.
  - The sibling for ordinary skills is `SkillManager::UseReclamationPoints(int)` 0x51fd30.

## 4. The reclaim command

- **`GAME::ControllerCharacter::SendReclaimDevotionPointCmd(int aetherDelta, int moneyDelta)` 0x1194f0**
  (verified): builds an 0x18-byte `ReclaimDevotionPointConfigCmd` on the controller's actor id
  (`controller+0x30`) and dispatches it through the AI's `vt[0x350]`.
  So the two ints are **deltas applied to the player, not ids or counts**: negative = charge, positive = refund.
- **Layout** (from the ctor `ReclaimDevotionPointConfigCmd(unsigned actorId, int aether, int money)` 0xd6b50,
  vftable 0x6ac490):
```
+0x00  vptr
+0x08  u32  actorId   (ActorConfigCommand base)
+0x0c  u8   1
+0x10  s32  aether delta
+0x14  s32  money delta
```
- **`ReclaimDevotionPointConfigCmd::Execute()` 0xd6bc0** (verified): resolves the actor, requires is-a
  `Player`, then
```
if (aether > 0 && money > 0) { Player::AddAether(aether);      money = min(p[0x16c4]+money, 2e9); }
if (aether < 0 && money < 0) { Player::SubtractAether(|aether|); p[0x16c4] = max(p[0x16c4]-|money|, 0); }
```
  (mixed signs do nothing.) `Player::AddAether` 0x3d07c0, `SubtractAether` 0x3d0890,
  `GetCurrentAether` 0x3d0680. Aether = the "Aether Crystal" currency.

**There is no symmetric "spend a devotion point" command.** Grepped the whole export list: the only devotion
config command is the reclaim one (plus its packet). Spending and refunding of the *points and star levels*
is done client-side by the exe with plain exported calls:
```
spend  (exe+0x17ebfc..0x17ec3a, verified):
    if (Character::GetDevotionPoints(player) == 0) -> nothing
    star->vt[0x48](1)                      // Skill::IncrementSkillLevel  0x46d510
    Character::SubtractDevotionPoint(player)
    if (Skill::GetDevotionLevel(star) < 1) star->vt[0xa0]()   // IncrementDevotionLevel -> 1
    if (Skill::GetSkillOperation(star) == 3) { UnlockTutorialPage(0x3c); open the assign dialog }
refund (exe+0x17ec9b.., verified; only when the pane's reclaim flag [pane+0x2419] is set):
    if (!SkillManager::UseDevotionReclamationPoints(sm, 1)) -> nothing   // charges bits + aether
    if (--level == 0) star->vt[0x50](0)    // Skill::SetSkillLevel(0)  0x46d480
    if (Skill::GetSkillLevel(star)==0 && operation==3) { host = GetObject(widget[0x10c]);
                                                        host->SetAutocastSkill(0, "", 0);
                                                        widget[0x10c] = 0 }
```
Note the refund path visible at exe+0x17ec9b does not itself call `AddDevotionPoints`; the point is returned
by the pane's recompute pass (section 7).

## 5. Assigning a celestial power to a skill

**The assignment API is `Skill::SetAutocastSkill` on the HOST skill.**
```
public: virtual void GAME::Skill::SetAutocastSkill(GAME::Skill* autocast,
            std::string const& controllerName, bool)          0x479650, vtable slot +0x5e0
```
Verified body: it releases any previous `this[0x3b8]` (unregistering it from the skill manager via
`sm->vt[0x128]` and `oldAutocast->SetSkillLevel(1)`), stores `this[0x3b8] = autocast`, copies
`controllerName` into `this[0x398]`, sets `autocast[0x30] = the owning skill manager`,
`autocast[0x2fb] = autocast[0x2fd] = 1`, and calls `SkillAutoCastController::GetParams` (0x50e5d0) to read
the controller record's trigger/chance.

**The exe's assign function, `UIDevotionWindowPane::AssignPowerToSkill(this, u32 newHostSkillId)` at
exe+0x1867f0** (verified, quoted in full logic):
```
w = pane[0x2ab8];                       // the selected star widget; nothing to do if null
if (newHostSkillId == w[0x10c]) return; // already bound there
power   = ObjectManager::GetObject(w[0x108]);              // the STAR's Skill
tag     = Skill::GetTemplateAutoCast(power);               // controller name from the power's record
oldHost = ObjectManager::GetObject(w[0x10c]);
if (oldHost) { oldHost->SetAutocastSkill(0, "", 0); power[0x1d0] = 0; }
SetWidgetHost(w, newHostSkillId);                          // exe+0x17eec0: w[0x10c] = id, reload icon
newHost = ObjectManager::GetObject(newHostSkillId);
if (newHost && tag is non-empty) {
    newHost->SetAutocastSkill(power, tag, false);          // <-- THE assignment
    power[0x1d0] = Object::GetObjectId(newHost);           // <-- Skill::SetDevotionParent, written inline
}
pane[0x2ab8] = 0;
```
Detaching by host, `exe+0x1869b0(pane, u32 hostSkillId)` (verified): scans every constellation's star
widgets for one with `w[0x10c] == hostSkillId`, then `host->SetAutocastSkill(0, "", 0)`,
`power[0x1d0] = 0`, `w[0x10c] = 0`. The pane's update at exe+0x1887a6/0x1887b5 calls detach-then-assign
after a `DialogManager` yes/no confirmation (`tagDevotionConfirm`), with the pending host skill id parked
at `pane+0x2414`.

**Which skills a power may be assigned to** -- the game does not compute a filter list in Game.dll. What
exists is:
- `GameEngine::GenerateUIDevotionSearchText(Skill const*, mem::vector<GameTextLine>&, GameTextClass)`
  0x2d2b40, which emits `tagSkillUnusableNotLearned` and `tagExclusiveSkill` lines -- but the exe uses it
  only to build the devotion window's **search index** for a star widget (exe+0x17f440, called from the
  pane loader, result lowercased into `widget+0x148`).
- The picker itself is exe-side (the player clicks a learned skill in the skill window while a power is
  selected); the id then flows into `AssignPowerToSkill`. Its "learned" test is not in Game.dll.
  Practically: any `Skill` object the player owns whose level is > 0 (`Skill+0xc8 != 0`) is a legal host --
  that is exactly the condition `SkillManager::AddExperience` requires for the binding to do anything.
  (**Inferred**, from the AddExperience gate plus the absence of any other filter.)

Related, for completeness: `SkillManager::RegisterAutoCastSkills` 0x529340 walks the class-skill list and
registers only skills whose `HasAutocastInDbr` (+0x3c0) is set -- that is the item/record-driven proc path,
**not** the devotion path (devotion assignment registers directly inside `SetAutocastSkill`).

## 6. Shrines and other point sources

- **`GAME::StaticShrine::GetDevotionPoints()` 0x551830 = `shrine+0x550`**, loaded from the shrine record's
  `devotionPoints` field (`StaticShrine::Load+0x654`, 0x551004). **It has no callers** -- not in Game.dll
  (checked the xref cache) and the exe does not import it. Treat it as informational only.
- **The live grant is `GAME::ScriptableAction_GiveDevotion::Execute(Entity*)` 0x4566d0** (verified). It
  ignores the passed entity, takes the **main player** (`gGameEngine[0x40e0]->vt[0x18]()->[0x10]`), and with
  `amount = this[0x10]` (`GetAmount` 0x16810):
```
if (amount > 0) {
    if (total(+0x1774) + amount > max(+0x1778)) amount = max - total;   // clamp to 50
    if (amount <= 0) return;
    player[0x1770] += amount;      // available   <- written INLINE, not via Character::AddDevotionPoints
    player[0x1774] += amount;      // total
    banner: 'tagDevotionPointAwarded' / 'tagDevotionPointsAwarded' via Engine::ShowCinematicText
}
else  /* amount < 0 */ { symmetric removal path at 0x4568c1 }
```
  So a shrine grants devotion by running this scriptable action from its level script / quest, which is why
  `Character::AddDevotionPoints` has **no** callers inside Game.dll at all. Its only callers are exe-side:
  a Lua/binding thunk at exe+0x10f50-ish and the devotion pane's reset pass (section 7).
- `GAME::PlayStats::UnlockedDevotionShrine()` 0x3ea450 = `++playStats[0x140]` -- a statistic counter, no
  callers in Game.dll.
- `GAME::GameEngine::DevotionPointsInUse()` 0x2b2ba0 -- see section 2; it is the gate for the reset item.

## 7. The reset item

```
public: virtual void GAME::ItemDevotionReset::Use(GAME::Character*)                  0x322130
public: virtual bool GAME::ItemDevotionReset::AllowUse(bool&) const                  0x3221d0
public: virtual GAME::CursorHandler* GAME::ItemDevotionReset::CreateSecondaryCursorHandler(
                                        GAME::Character const*)                      0x322390
        GAME::ItemDevotionReset::classInfo                                           0xa27ee0
        GAME::CursorHandlerDevotionReset::`vftable'                                  0x71a030
```
- **`Use(Character* user)`** (verified): no-op unless `user` is the main player AND
  `player[0x1770] < player[0x1774]` (points are actually spent). Then it opens the devotion UI through
  `gGameEngine[0x19b0]->vt[0xe8]()` and raises the item's own dialog (`item[0xc68]->vt[0x28](1, 0)`) --
  the `tagDevotionResetConfirmation` prompt. **It does not itself refund anything.**
- `AllowUse` (verified): `*out = (item->vt[0x618]() <= 1)`; returns true only when some devotion point is
  spent (same `+0x1770 < +0x1774` test).
- `CursorHandlerDevotionReset` is a 0x40-byte cursor handler (`IsInventoryCapable` true, everything else
  false), whose `SecondaryInitialize` (0x16dfa0) re-resolves the item by id and checks is-a
  `ItemDevotionReset`, and whose `Update` (0x16e170) watches for UI window id `0x1a` in
  `gGameEngine+0x38`'s list. It is the "click the item, then click a target" plumbing, not the reset itself.
- **The actual reset is exe-side, `exe+0x18c9d0`** (verified): for every star widget of every constellation
  with `Skill::GetSkillLevel(star) != 0` and the pane NOT in reclaim mode:
```
wasComplete = ConstellationIsComplete(c)     // exe+0x181690
star->vt[0x50](0)                            // Skill::SetSkillLevel(0)
if (wasComplete && !ConstellationIsComplete(c)) RemoveAffinity(c)     // exe+0x181910
if (Skill::GetSkillLevel(star)==0 && GetSkillOperation(star)==3) {
    host = GetObject(star[0x1d0]);           // the devotion parent
    host->SetAutocastSkill(0, "", 0);        // detach the celestial power
    ... widget[0x10c] = 0, icon reload
}
... finally:
avail = Character::GetDevotionPoints(p); total = Character::GetTotalDevotionPoints(p);
Character::AddDevotionPoints(p, min(total - avail, total));      // hand every spent point back
```

## 8. Constellation affinity (exe-side, but it drives Character::AddAffinity)

- `ConstellationIsComplete(c)` exe+0x181690: every star widget in `c[0x78]..c[0x80]` must have
  `Skill::GetSkillLevel == Skill::GetMaxLevel`.
- `GrantAffinity(c, bool showBanner)` exe+0x181870: requires `c[0x32]` (the record-loaded "gives affinity"
  flag), then for each `{u32 type, u32 amount}` pair in `c[0xa8]..c[0xb0]`
  `Character::AddAffinity(mainPlayer, type, amount)`.
- `RemoveAffinity(c)` exe+0x181910: the same loop with `Character::SubtractAffinity`.
- The pane loader (exe+0x184a30) copies the constellation's requirement/bonus vectors onto **every star
  Skill of that constellation**: `Skill::SetAffinityDepedency(star, c+0x90)` and
  `Skill::SetAffinityBonus(star, c+0xa8)`. So `Skill::GetAffinityDependencies()` on any star = the
  constellation's `affinityRequired*`, and `Skill::GetAffinityBonus()` = its `affinityGiven*`.
  `Skill::SetConstellationDependencies` / `SetConstellationSelfLocked` are called from the same layer
  (exe+0x1850b5 / exe+0x1850c6) for the "requires another constellation" / "exclusive" rules.

## 9. `GameEngine::GenerateUIDevotionText` -- what the arguments mean

```
public: static void GAME::GameEngine::GenerateUIDevotionText(
            GAME::Skill const* a,                        // the STAR's Skill  (widget[0x108])
            GAME::Skill const* b,                        // the ASSIGNED HOST Skill (widget[0x10c]), may be null
            mem::vector<GAME::GameTextLine>& out,
            GAME::SkillReasons const* reasons,           // widget+0x160 -- the widget's cached flag block
            bool  arg5,                                  // false at the only call site
            bool  reclaimMode,                           // pane[0x2419]
            int   reclaimCostBits,                       // SkillManager::GetCurrentDevotionReclamationCost()
            int   reclaimCostAether,                     // SkillManager::GetDevotionReclamationAetherCost()
            GAME::GameTextClass cls)                     // 0x31 at the call site
            0x2d12c0
```
Established by reading the sole caller, exe+0x17f0d0 (the star widget's tooltip builder), argument by
argument -- verified. Inside the body (0x2d12c0):
- `b` is dereferenced at 0x2d1444 (`cmp byte [rsi+0x108], 0`) immediately before localizing
  **`tagDevotionAttached`** -- i.e. the "Attached to <skill>" line. That is the direct proof that `b` is the
  bound host skill, not the constellation.
- `Skill::GetRequiredExperience` + **`tagDevotionExperienceFormat`** produce the experience line (0x2d17bb).
- The `reasons` block is read at `+1`, `+3`, `+0xb` for `tagDecreaseMasteryError` / `tagReclaimNoPoints`
  style refusal lines -- the same `SkillReasons` struct the skills window uses.
- The two ints are formatted through the number formatter at 0x57d0a0 for the reclaim cost lines.

The widget fills that `SkillReasons` block every frame at exe+0x17de48 (verified):
```
reasons+0x00 (w+0x160) = (skillLevel == 0 && Character::GetDevotionPoints == 0)   "no points"
reasons+0x02 (w+0x162) = w[0x134]        (locked)
reasons+0x03 (w+0x163) = GetDevotionLevel() >= GetDevotionMaxLevel()
reasons+0x08 (w+0x168) = GetCurrentDevotionReclamationCost() > Character::GetCurrentMoney()
reasons+0x09 (w+0x169) = (a caller-supplied byte)
reasons+0x0a (w+0x16a) = w[0x134]
reasons+0x0b (w+0x16b) = (skillLevel == 0)
reasons+0x0c (w+0x16c) = w[0x135]        (dependency failed)
```

`GameEngine::GenerateUIDevotionSearchText(Skill const*, mem::vector<GameTextLine>&, GameTextClass)`
0x2d2b40 takes only the star and is used for the window's search box (section 5).

## 10. exe-side layout reference (dies on any exe relink -- see AGENTS.md "Game patches")

Devotion window pane (`hudDevotionWindow`):
```
pane+0x00a8/+0x00b0  mem::vector<Constellation*>
pane+0x2380/+0x2384  (the pane's own drag/anchor state)
pane+0x2410  u32   the player's object id
pane+0x2414  u32   pending assign target skill id (awaiting the tagDevotionConfirm dialog)
pane+0x2419  u8    RECLAIM MODE (set at a spirit guide; makes clicks refund instead of spend)
pane+0x2ab8  Widget* the currently selected celestial-power star widget
```
Constellation object:
```
c+0x0032  u8    "gives affinity" flag
c+0x0078/+0x0080  mem::vector<StarWidget*>
c+0x0090  mem::vector<pair<AffinityType,u32>>  affinityRequired
c+0x00a8/+0x00b0  mem::vector<pair<AffinityType,u32>>  affinityGiven
c+0x0220  the banner/notification widget
```
Star widget:
```
w+0x0030  Pane*
w+0x0038  Constellation*
w+0x0048  std::string  the star's .dbr path (resolved with SkillManager::FindSkillId)
w+0x0108  u32   the star's Skill object id
w+0x010c  u32   the HOST skill object id the power is assigned to (0 = unassigned)
w+0x0130  u32   points spent on this star (the pane's own counter)
w+0x0134  u8    locked
w+0x0135  u8    dependency failed
w+0x0148  the lowercased search text
w+0x0160  SkillReasons block (see section 9)
```
Key exe functions: 0x17de48 per-frame star state; 0x17ea10 star mouse handler (spend / refund / open
assign); 0x17eec0 set the widget's host + icon; 0x17f0d0 tooltip; 0x17f440 search text; 0x180fd0
availability + affinity-name parsing; 0x181690/0x181870/0x181910 constellation complete / grant / remove
affinity; 0x184a30 pane load (binds star records to Skill ids); 0x1867f0 assign; 0x1869b0 detach by host;
0x188700 pane update + confirm dialog; 0x18bd50 the reset button handler; 0x18c9d0 full reset.

## 11. Suggested surface for the mod (all export-driven, no exe layer needed)

```
enumerate stars  : SkillManager::GetSkillList()  (SkillManager+0x20) filtered by
                   Skill::GetSkillOperation() != 0     (2 = star, 3 = celestial power)
star state       : Skill::GetCurrentLevel() / Skill::GetMaxLevel()
                   Skill::GetDevotionLevel() / GetDevotionMaxLevel() / GetDevotionExperience()
                   Skill::GetRequiredExperience(level)
points           : Character::GetDevotionPoints / GetTotalDevotionPoints / GetMaxDevotionPoints
affinity         : Character::GetAffinity(0..4)  = Ascendant/Chaos/Eldritch/Order/Primordial
                   Skill::GetAffinityDependencies() (required) / GetAffinityBonus() (given)
spend a point    : Skill::IncrementSkillLevel(1) + Character::SubtractDevotionPoint()
                   + Skill::IncrementDevotionLevel() when GetDevotionLevel() == 0
refund a point   : SkillManager::UseDevotionReclamationPoints(1)  [charges bits + aether, returns false
                   if unaffordable] then Skill::SetSkillLevel(level-1)
                   + Character::AddDevotionPoints(1)  (the exe does this in its recompute pass)
reclaim cost     : SkillManager::GetCurrentDevotionReclamationCost() (iron bits)
                   SkillManager::GetDevotionReclamationAetherCost()  (aether crystals)
                   Player::GetCurrentAether(), Character+0x16c4 (money)
which power is
  bound to what  : Skill::GetDevotionParent()  -> the HOST skill's object id (0 = unassigned)
                   Skill::GetAutoCastSkill() on the host -> the power  (inverse direction)
assign a power   : host->SetAutocastSkill(power, power->GetTemplateAutoCast(), false)
                   + power->SetDevotionParent(hostSkillId)
unassign         : host->SetAutocastSkill(nullptr, "", false)
                   + power->SetDevotionParent(0)
tooltip          : GameEngine::GenerateUIDevotionText(star, host, out, reasons, false, reclaimMode,
                                                      bitsCost, aetherCost, 0x31)
any points spent : GameEngine::DevotionPointsInUse()
```

## 12. Still unknown / unverified

- Nothing here has been exercised in a live game.
- `GenerateUIDevotionText`'s 5th argument (the first `bool`) -- the only call site passes `false`; its
  meaning inside the body was not traced.
- `SkillReasons+0x09` (the byte the widget copies from a caller register, exe+0x17df10).
- How much XP `Character::ReceiveExperience` forwards to `SkillManager::AddExperience` (the argument was
  not traced back).
- Whether `SkillManager+0x410` (set when the tree contains an `Effect` entry) gates anything at runtime.
- `StaticShrine::GetDevotionPoints` / `PlayStats::UnlockedDevotionShrine`: both are exported and both are
  callerless in the shipped binaries. Which shrines actually run `ScriptableAction_GiveDevotion` was not
  traced (it lives in level scripts, not in the DLLs).
- The exe's rule for which skills appear in the assignment picker (section 5) is inferred, not read.
- The `tagDevotionAffinity01..05` <-> AffinityType mapping is positional/inferred; the enum values
  themselves are verified from the exe's `_stricmp` chain.
