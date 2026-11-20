# Pets at the Game.dll level (static RE 2026-08-26, offline only -- NOT verified in a running game)

All RVAs are Game.dll v1.3.0.8 unless prefixed `exe+` (the unpacked `build/GrimDawn.unpacked.bin`).
Everything named `GAME::X::Y` below is an **exported** symbol unless the line says "unexported".
Confidence is stated per section; "verified" = read in the disassembly, "inferred" = deduced from
surrounding code or the records, never observed.

## 1. The pet list -- enumerate the player's pets

```
public: class mem::vector<unsigned int> const& GAME::GameEngine::GetLocalPetList(void) const   0x2b22c0
public: void  GAME::GameEngine::RegisterLocalPet(unsigned int)                                  0x2b0ec0
public: void  GAME::GameEngine::UnregisterLocalPet(unsigned int)                                0x2b2240
public: bool  GAME::GameEngine::IsLocalPet(unsigned int) const                                  0x2b0e60
public: void  GAME::GameEngine::ClearPetList(void)                                              0x2b0eb0
public: unsigned int GAME::GameEngine::GetMiniPetLimit(void) const                              0x2b0fb0
```
- `GetLocalPetList` is `lea rax,[rcx+0x36138]; ret` -- **gGameEngine+0x36138 is a `mem::vector<u32>`**
  ({begin,end,cap}, 4-byte entity ids). `IsLocalPet` is a linear scan of it; `ClearPetList` just sets
  end=begin; `RegisterLocalPet` appends if absent; `UnregisterLocalPet` erases by value. All verified.
- `GetMiniPetLimit` = `int` at gGameEngine+0x36150; the value comes from `records/game/gameengine.dbr`
  `miniPetLimit = 3` (inferred from the record; the load site was not read).
- **This list is exactly what the HUD's pet portrait bar shows and what F2-F6 index** (verified: the
  portrait container at exe+0x256b90 rebuilds itself from `GetLocalPetList` and drops any portrait whose
  id fails `IsLocalPet`).

### Who gets registered (verified in `GAME::Monster::JoinMe`, 0x3619f0, virtual, Monster vtable +0x620)
`public: virtual void GAME::Monster::JoinMe(unsigned int leaderId, int groupId, bool, bool questPet)`
registers the monster as a local pet only when **all** of:
- the 4th arg (`questPet`) is false,
- `leaderId` == the local main player's id (`gGameEngine->[+0x40e0]->vt[0x18]()->[+0x10]`),
- `groupId == -1`,
- `monster+0x5129 != 0`.

It then sets `monster+0x50b0 = 1`, calls `RegisterLocalPet(monsterId)` and unlocks tutorial page 0x22.
`monster+0x5129` is the pet record's **`showStatusWidgetWhenPet`** bool (verified: `Monster::Load` at
0x35fb0a loads the string at Game.dll+0x678818 = `"showStatusWidgetWhenPet"` and stores the result into
`[rdi+0x5129]`). So mini-pets / totems / traps with the flag off never appear in the list.

### Spawn / death detection
- **Spawn**: `GameEngine::RegisterLocalPet` (only caller: `Monster::JoinMe`+0x77).
- **Death**: `GAME::Monster::CharacterIsDying` (0x362100) sets `monster+0x4b4 = -1` and calls
  `UnregisterLocalPet(id)` (verified; it is the only caller).
- Both are exported, so a Detours hook on either gives a clean spawn/despawn event; polling the vector
  each frame is equally correct and simpler.

### Per-pet data
- Name: the Actor `GetGameDescription` slot the mod already uses (`GAME::Monster::GetGameDescription`
  0x366f30) -- pets are `GAME::Pet` / `PetPlayerScaling` / `PetNonScaling`, all `Monster` subclasses.
- Alive / health: `GAME::Character::IsAlive` 0x5ba90, `GAME::Character::GetCurrentLife` 0x6b010,
  `GetCurrentLifeInt` 0x6b020.
- Position: the existing `Entity::GetCoords` / `GetRegionBoundingBox` path.
- Owner: `public: virtual unsigned int GAME::Monster::GetLeader(void) const` 0x117a60 =
  `*(u32*)(monster+0x4ecc)`, **virtual slot +0x8d0 of the Monster vtable** (verified by reading
  `Monster::vftable` 0x745ce0 and `Pet::vftable` 0x755400). This is the "is this my pet" test the game
  itself uses (`HandlePetAction`, `Skill_PetAttack`).
- Tooltip: `public: virtual void GAME::Monster::CreateUISummaryText(enum GAME::GameTextClass,
  mem::vector<GAME::GameTextLine>&) const` 0x3685b0. **The base `Character::CreateUISummaryText` (0x15880)
  is COMDAT-folded with dozens of empty bodies -- it is a no-op**; dispatch through the object's own
  vtable (the project's existing "ambiguous slot" handling applies).
- Barks: `GAME::Monster::GetPetAttackSound()` 0x3612d0, `GetPetAcknowledgeSound()` 0x3612e0, both return
  `SoundPak*` (the exe plays the attack one at the pet's coords when a command is issued).
- Quest pets: `public: bool GAME::Character::IsQuestPet(void) const` 0x20750 = byte at Character+0x2080;
  `SetQuestPet()` 0x4f50a0 sets it. Quest pets never enter the local pet list (the JoinMe gate).
- `public: void GAME::Pet::GetPetTypeNames(mem::vector<std::string>&)` 0x3b3520 -- copies the string
  vector at `Pet+0x5398` (the pet-type tags used for pet-bonus matching, not a display name).

## 2. The pet pen -- which skill owns which pet

```
public: class GAME::PetPen&       GAME::Character::GetPetPen(void)        0x7ded0   -> Character+0x2050
public: class GAME::PetPen&       GAME::Character::GetQuestPetPen(void)   0x4f57d0  -> Character+0x2068
public: unsigned int GAME::PetPen::GetPetOwner(unsigned int petId) const  0x3b55b0
```
`PetPen::GetPetOwner` is the **only** exported `PetPen` member.

**PetPen layout (0x18 bytes, verified):**
```
+0x00  ?                        (never read in anything disassembled)
+0x08  std::list head node ptr  \  MSVC std::list<PetPenEntry>
+0x10  size_t size              /
```
`Character::GetPetPen` = +0x2050 and `GetQuestPetPen` = +0x2068 -> stride 0x18. Confirmed independently:
the exe passes `player + 0x2050` straight to `PetPen::GetPetOwner` (exe+0x252b44), and `Skill_PetAttack`
copies the list object at `caster + 0x2058`.

**PetPenEntry (list node value at node+0x10, 0x18 bytes, verified via the unexported `PetPen::AddPet`
at 0x3b5520):**
```
+0x00 u32  petId              <- GetPetOwner matches on this
+0x04 u32  skillId            <- GetPetOwner RETURNS this
+0x08 u32  skillReferenceNum  (SkillManager::GetSkillReferenceNumber, save-stable)
+0x0c u8   flag (0 on insert; Skill_PetAttack SKIPS entries with it set -> "released/dead")
+0x10 u32  0xea60 (60000) on insert -- a timer/lifetime, purpose not established
+0x14 u8   the bool passed to PostPetSpawn
```

**So `PetPen::GetPetOwner(petId)` does NOT return an entity id -- it returns the id of the `Skill` object
that summoned the pet.** Verified end to end: `Skill_TargetedSpawnPet::PostPetSpawned(u32 casterId,
WorldVec3 const&, u32 petId, u32 skillId)` at 0x4afd10 -> `GameEngineOutboundInterface::PostPetSpawn(
casterId, coords, petId, skillId, skillRef, bool)` -> `Character::PostPetSpawn(coords, petId, skillId,
skillRef, bool)` (0x6ca50, a thunk onto `PetPen::AddPet`); and `Player::PostPetSpawn` (0x3d2560) resolves
arg2 through ObjectManager and checks is-a `Pet::classInfo`, while arg4 goes to
`SkillManager::GetSkillIdFromReference`.

## 3. Stance (Aggressive / Normal / Defensive)

**The enum is `GAME::Monster::ControllerType` and its values are (verified):**
```
0 = Normal      1 = Aggressive      2 = Defensive
```
Two independent proofs:
- The exe's portrait context-menu handler (exe+0x252980..0x252d34) compares the menu item's tag string and
  passes `0` for `tagPortraitNormal`, `1` for `tagPortraitAggressive`, `2` for `tagPortraitDefensive`.
- `GAME::Monster::UseController(ControllerType)` (0x2fe690 -> real body 0x36a2b0) selects
  `monster+0x50b4` for 0, `+0x50b8` for 1, `+0x50bc` for 2, and `Monster::Load` fills those from the pet
  record's `controller` / `controllerAggressive` / `controllerDefensive` fields (verified at 0x35ff30 /
  0x35fe7c / 0x35ff1c; the field-name strings are Game.dll+0x678818/0x678850/0x678998). The records exist
  as `records/controllers/pets/controller_<name>_{normal,aggressive,defensive}.dbr`, referenced from
  `Class = Pet` records under `records/skills/.../pets/` (e.g. `pet_hellhound_a02.dbr`).
- `Monster::UseController` also writes the chosen id into `monster+0x16c0` (the ACTIVE controller id) and
  does nothing when the selected slot is 0.

### Reading a pet's stance
Two ways:
1. `public: enum GAME::Monster::ControllerType GAME::Player::GetPetControllerType(unsigned int) const`
   0x3d2510 -- a `std::map<u32,u32>` lookup on **Player+0x4ca0** (MSVC `_Tree_node`: `_Isnil` at +0x19,
   key u32 at +0x1c, value u32 at +0x20). **The key is the pen's `skillId`, not the pet id**, so the exe
   always calls it as `GetPetControllerType(GetPetOwner(petId))` (exe+0x255ae2 / 0x255aed). **A missing
   key returns 1 (Aggressive)** -- that is the default stance (verified at 0x3d2554).
   Consequence: stance is **per summoning skill**, shared by every pet of that skill and remembered
   across resummons.
2. Compare the pet's active controller id `monster+0x16c0` against `+0x50b4 / +0x50b8 / +0x50bc`.
   Verified in the disassembly, not exercised.

### Setting a pet's stance -- exactly what the game does
```
u32 skillId = PetPen::GetPetOwner(player->GetPetPen(), petId);        // pen = Character+0x2050
Player::SetPetControllerType(player, skillId, type);                  // 0x3d2460, inserts if absent
if (Engine::IsNetworkEnabled() && !GameEngine::IsServerOrSingle())
    GameEngine::MonsterUseController(gGameEngine, petId, type);       // 0x2df420, net path
else
    Monster::UseController(petMonster, type);                         // 0x2fe690, local path
```
(verbatim from exe+0x252b41..0x252d1a, three times, once per stance). For a single-player mod only the
`else` branch matters. `Player::SetPetControllerType(u32 key, ControllerType)` is a real `map[key] = value`
(verified: the not-found path at 0x3d24c4 inserts).

Exports:
```
public: enum GAME::Monster::ControllerType GAME::Player::GetPetControllerType(unsigned int) const   0x3d2510
public: void GAME::Player::SetPetControllerType(unsigned int, enum GAME::Monster::ControllerType)   0x3d2460
public: void GAME::Monster::UseController(enum GAME::Monster::ControllerType)                       0x2fe690
public: void GAME::GameEngine::MonsterUseController(unsigned int, enum GAME::Monster::ControllerType) 0x2df420
```
UI strings: `tagPortraitAggressive` / `tagPortraitNormal` / `tagPortraitDefensive` (exe+0x303bd0 /
0x303c30 / 0x303c48), fetched through `LocalizationManager` -- the mod's `hooks::localize` path.

**Nothing else carries stance**: `ReleasePetConfigCmd(u32,u32,bool)`, `RemovePetConfigCmd(u32,u32)` and
`AttachPetAutocastConfigCmd(u32,u32,u32,u32,std::string const&)` are release / autocast-attach only, and
there is no Mode / Stance / Behavior / Attitude / Leash / Follow export anywhere in Game.dll (grepped the
full export list).

The exe portrait's own cached stance lives at `portrait+0x908` (portrait widget size 0x9a0, its pet id at
`+0xac`); its context-menu ids are `0xd` and `0xf` = cycle stance backwards/forwards, `0xe` = disband.

## 4. Selecting pets and commanding them

### The game's own path (all exe-side; verified)
- The pet bar lives at `[InGameUI+0x30] + 0x858`. It holds its own selection as a `std::list<u32>` at
  `bar+0x08` (value at node+0x10), the owner's player id at `bar+0x18`, and the portrait container at
  `bar+0x20`.
- **Actions 0x2c..0x30 = "Select Pet 1..5"**, `0x31 = "Select All"` (verified by decoding
  `InGameUI::HandleKeyAction`'s jump table at exe+0x211980; index byte table exe+0x21215c, dword offset
  table exe+0x2120f4, index = `action - 1`):
```
  0x2c -> exe+0x211dcf   SelectPetByIndex(bar, 0)     ; SelectPetByIndex = exe+0x205170
  0x2d -> exe+0x211de6   SelectPetByIndex(bar, 1)
  0x2e -> exe+0x211e00   SelectPetByIndex(bar, 2)
  0x2f -> exe+0x211e1a   SelectPetByIndex(bar, 3)
  0x30 -> exe+0x211e34   SelectPetByIndex(bar, 4)
  0x31 -> exe+0x211e4e   ClearSelection(bar) [exe+0x205490] then indices 0..4
```
  `SelectPetByIndex` builds the id list from the portrait container and calls `SelectPet(bar, petId)`
  (exe+0x205200 -> exe+0x205280). `ClearSelection` walks the list calling `Monster::RemoveControlBanner`.
- **Hover-to-select while the Target-Pet modifier is held**: in the mouse handler at exe+0x21745, when
  `[InGameUI+0x849]` is set the exe calls `GAME::ControllerPlayer::GetCombatAlly()` (0x14ae50 =
  `*(u32*)(controller+0x46c)`; setter `SetCombatAlly` 0x14ae30) and selects that pet. `GetCombatAlly` is
  the ally twin of `GetCombatEnemy` -- the pet under the cursor.
- **The command**, exe+0x20556c (`CommandSelected(bar, u32 targetId, WorldVec3 const* pt)`), called from
  the mouse handler at exe+0x2269c and immediately followed by `ClearSelection`: for each selected pet it
  plays `Monster::GetPetAttackSound()` at the pet's coords, then
  - `targetId != 0`: `RequestAllyAttackConfigCmd(petId, bar->playerId, targetId)` -> dispatch,
  - else: `RequestAllyMoveConfigCmd(petId, bar->playerId, pt)` -> dispatch.

  Those config commands' `Execute` (0xd7b00 / 0xd7c60) do nothing but call, on the **pet's** Character:
```
  public: void GAME::Character::RequestAttack(unsigned int requesterId, unsigned int targetId)     0x6da30
  public: void GAME::Character::RequestMove  (unsigned int requesterId, WorldVec3 const&)          0x6dad0
```
  which resolve the pet's controller (`Character+0x16c0` -> ObjectManager -> is-a `ControllerAI`), take
  its current state (`controller+0x2f0 ? (*(void**)(controller+0x2e8))[0x10] : controller+0x2a0`) and call
  state vtable `+0x88` (attack) / `+0x90` (move). Both are exported and the exe imports them directly
  (exe+0x2d7b60 / 0x2d7b68), so **`Character::RequestAttack/RequestMove` on each pet is the whole command
  API** for a single-player mod -- no config command, no selection state needed.

  Config-command layouts (for reference): `RequestAllyAttackConfigCmd` 0x18 bytes
  `{vptr, +0x08 petId, +0x10 requesterId, +0x14 targetId}`; `RequestAllyMoveConfigCmd` 0x30 bytes
  `{vptr, +0x08 petId, +0x10 requesterId, +0x18 WorldVec3}`.

### `Skill_PetAttack` -- the "Pet Attack" default skill (commands ALL pets at once)
Record `records/skills/default/defaultpetattack.dbr`, `Class = Skill_PetAttack`,
`skillDisplayName = tagSkillDefaultPetAttack`, `skillMaxLevel = 1`, `isPetDisplayable = False`.
Only two virtuals are overridden (verified):
```
public: virtual bool const GAME::Skill_PetAttack::GetValidTarget(GAME::Character const& caster,
            unsigned int& targetId, GAME::WorldVec3 const&, bool, float) const                    0x4eea30
public: virtual bool       GAME::Skill_PetAttack::SetAvailability(GAME::Character&, bool, bool)   0x4eeca0
```
- `GetValidTarget` copies `caster + 0x2058` (the pen's list) and, for each entry whose `+0x0c` flag is
  clear, resolves `petId`, checks is-a `Monster` and `pet->GetLeader() == caster->GetObjectId()`, then
  issues `RequestAllyAttackConfigCmd(petId, ..., *targetId)` if `*targetId != 0`, else
  `RequestAllyMoveConfigCmd(petId, ..., point)`. **It takes either an entity id or a ground point** -- the
  id wins. (Despite the name it is not a pure query; the command is issued from inside it.)
- `SetAvailability` writes `skill+0xb0` = 0 when at least one owned live pet exists, else 6 (greyed).
- The skill id: `GAME::SkillManager::GetDefaultSkillId(enum)` (0x519c70) accepts **only 0 and 1** and
  returns `*(u32*)(skillManager + 0x1a0 + 4*enum)`. Index 0 is the default weapon attack (already used by
  the mod as `gameapi::default_skill_id(0)`); **index 1 is almost certainly the pet attack** -- inferred
  from the two-slot table plus the two default skill records, NOT verified. Check
  `gameapi::default_skill_id(1)` against `tagSkillDefaultPetAttack` in-game before relying on it.

### The dormant `ControllerPlayer` pet API
```
public: void GAME::ControllerPlayer::SetPet(unsigned int)                                         0x123ea0
public: bool GAME::ControllerPlayer::HandlePetAction(GAME::Character& player, bool, bool,
             GAME::WorldVec3 const& pt, unsigned int& targetId)                                   0x14c9e0
```
- `SetPet` is `mov [rcx+0x508], edx; ret`. (It is COMDAT-folded with
  `ControllerMonster::SetBuffSelf3Skill`; folding requires byte-identical bodies, so +0x508 really is
  ControllerPlayer's field.)
- `HandlePetAction` reads `controller+0x508`, returns false if it is 0 or equals `*targetId`, **clears
  it**, verifies the pet's `GetLeader() == player id`, then issues exactly the same
  `RequestAllyAttackConfigCmd` / `RequestAllyMoveConfigCmd` pair as above. Single pet, one shot.
- **Caveat: neither is used by the shipping game.** Nothing in Game.dll calls them (checked
  `build/xref_Game.dll.txt`) and the exe does not import them (the decorated names do not appear anywhere
  in the unpacked image). They are still callable, but `Character::RequestAttack` / `RequestMove` is the
  path the game itself exercises and is the safer choice.

## 5. Disband (`tagPortraitDisband`)
The exe's portrait menu (exe+0x252acb..0x252b02) does:
```
u32 cid   = Character::GetControllerId(player);          // 0x621b0 -> Character+0x16c0
Object* c = ObjectManager::GetObject(cid);
c->vt[0x250](petId, /*bool*/ false);
```
`ControllerPlayer::vftable` (0x6af400) slot **+0x250 is `ControllerPlayer::ReleasePet`** (verified by
reading the vtable):
```
public: virtual bool GAME::ControllerPlayer::ReleasePet(unsigned int petId, bool)                 0x14bdd0
```
It forwards to the current state's vtable +0x1d8 (`RequestReleasePet`), whose default body is
```
protected: bool GAME::ControllerPlayerState::DefaultRequestReleasePetAction(unsigned int, bool)   0x151af0
```
which allocates a 0x18-byte `ReleasePetConfigCmd` (`{vptr, +0x08 ownerId, +0x0c bool 1, +0x10 petId,
+0x14 bool arg}`; ctor exported as `ReleasePetConfigCmd::ReleasePetConfigCmd(unsigned,unsigned,bool)`) and
dispatches it through the player's `vt[0x350]` config-command send. Verified. So calling the exported
`ControllerPlayer::ReleasePet(petId, false)` is the whole disband operation.

Related: `public: virtual void GAME::Skill::ReleasePets(void)` (releases every pet of one skill),
`GAME::Skill::GetPetLimit(unsigned int) const` 0x47edc0, `GAME::Skill::IsPetDisplayable(void) const`
0x48a620 (= `skill->vt[0x578]()` SkillProfile, byte +0x6c0 = the record's `isPetDisplayable`).

## 6. What the mod can do with this (suggested surface)
```
enumerate : gGameEngine + 0x36138  (mem::vector<u32>)   or GameEngine::GetLocalPetList()
name/hp   : Monster::GetGameDescription / Character::GetCurrentLife / IsAlive / Entity::GetCoords
owner     : Monster::GetLeader()                       (== player id for our pets)
stance rd : Player::GetPetControllerType(PetPen::GetPetOwner(player+0x2050, petId))   default 1
stance wr : SetPetControllerType(same key, t) + Monster::UseController(petMonster, t)
command   : Character::RequestAttack(pet, playerId, targetId)
            Character::RequestMove  (pet, playerId, worldVec3)
disband   : ControllerPlayer::ReleasePet(petId, false)
events    : hook GameEngine::RegisterLocalPet / UnregisterLocalPet
hover pet : ControllerPlayer::GetCombatAlly()          (controller+0x46c)
```
Stance is per SUMMONING SKILL, so a mod readout that says "Skeleton, aggressive" is really reporting the
stance of `Summon Skeletons`; setting it on one skeleton is expected to move all of them.

Only the world layer (exports + Engine/Game object offsets) is involved in all of the above except the
F2-F6 action ids and the pet-bar offsets, which are exe-layer and die on any exe relink (see AGENTS.md
"Game patches"). The mod does not need the exe layer for pets.

## 7. Still unknown / unverified
- Whether `GetDefaultSkillId(1)` is the pet attack (section 4). Everything else about `Skill_PetAttack`
  was read from its body.
- `PetPen+0x00`, `PetPenEntry+0x10` (0xea60) and `PetPenEntry+0x14`.
- What clears `PetPenEntry+0x0c` (the "released" flag) and where dead entries are pruned.
- The exe's Target-Pet modifier flag `[InGameUI+0x849]` -- the key that sets it was not traced.
- `RequestAttack` / `RequestMove`: whether the pet's controller state validates `requesterId` against the
  leader (the state vtable +0x88 / +0x90 bodies were not read). The game always passes the owning player's
  id; the Lua bindings at exe+0x51000 / 0x51040 pass the character's own id.
- Nothing here has been exercised in a live game.

## 8. Proximity-mine pets: `deathFromEnemyRange` / `deathFromEnemyDelay` (Rune of Hagarrad, 2026-09-20)

Static RE of Game.dll v1.3.0.8. The Inquisitor runes (`pets/icerune_NN.dbr`, `concussiverune_NN.dbr`, controller
`controller_wizardtrap.dbr` = `ControllerStationaryMonster`) are one-shot mines, not turrets: the pet record's
`deathFromEnemyRange` / `deathFromEnemyDelay` (2.0 units / 1.0 s for both runes) make the game KILL the pet when a live
hostile monster stands inside the range, and the burst is the pet's `dyingSkillName` (`petskill_icerune_icering`,
5 pierce+cold projectiles in a ring). "Lives for 30 seconds" is only the timeout; "takes 1 second to arm" is the delay.

- `Monster::Load` (0x35fbba..0x35fbe2) stores the two fields at `Monster+0x4f18` (range) and `+0x4f1c` (delay);
  getters `Monster::GetDeathFromEnemyRange` / `GetDeathFromEnemyDelay` (0x3681c0 / 0x3681d0).
- `ControllerMonster::Update(dt_ms)` (0x121bc0): on a fresh controller (`this+0x607` set) the arm timer
  `this+0x678 = delay * 1000` ms. Then, only when range != 0 (0x122381): the arm timer counts down by dt; a poll timer
  `this+0x674` counts down and reloads to 200 ms (0xc8); on each poll with the arm timer <= 0 it calls
  `ControllerMonster::DieIfEnemyInRange` (0x1291f0). So the check runs every 200 ms after the arm delay.
- `DieIfEnemyInRange`: sphere of radius `deathFromEnemyRange` at the pet's `Entity::GetCoords` in its region
  (query at 0x48df0, unexported); for each result that is-a `Monster` (players are not), hostile by
  `FactionManager::GetPvpIds` / `PartyManager::AreInPartyTogether` / `IsCharacterInNonPvpArea` / `FactionPack::GetValue`
  < 0, and `Character::IsAlive` (vtable +0x460): calls the controller's `ControllerCombat::KillMe(byte monster+0x2081)`
  (vtable +0x110, 0x119b30) and stops. `KillMe` refuses an `Npc`, sets death reason 3 (character vtable +0x430) and
  transitions the controller (vtable +0x180) into `Dying`, which is where `dyingSkillName` fires.
- Not verified: whether the stationary controller also runs the pet's `specialAttack` (`skillName2` is the same ice
  ring at `ShortRange` = 4 units, 300 ms delay, 1 s cooldown) on enemies that stay between 2 and 4 units. If it does,
  the rune shoots repeatedly at that band and only self-destructs when something steps within 2 units.
