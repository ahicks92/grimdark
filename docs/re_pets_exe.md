# Pets: the exe side (Grim Dawn v1.3.0.8 x64, exe timestamp 0x6a85fbec, image 0x482000)

Static RE on `build/GrimDawn.unpacked.bin` with `tools/exe_dis.py` / `tools/dll_dis.py` / `tools/dll_xref.py`,
2026-08-26. Nothing here has been exercised live. All addresses are RVAs (`exe+...`, `Game.dll+...`);
"verified" = read in disassembly, "inferred" is marked. Companion to `docs/exe-ui-layout.md` (frameworks A/B)
and `docs/ingame-ui-survey.md` (the InGameUI window map).

## The three objects

```
main_obj  = [exe+0x3ceef8]
X         = [main_obj+0x90]        the world/session UI host (holds InGameUI at X+0x2f0)
InGameUI  = [X+0x2f0]              and InGameUI+0x30 is the back-pointer to X (set by
                                   InGameUI vtable+0x88, exe+0x20a3c0: `mov [rcx+0x30], rdx`)
PetSel    = X+0x858                the pet SELECTION (a by-value member of X, no vtable seen)
Panel     = [X+0x878] = InGameUI+0x4de08     the HUD portrait panel (pets + party)
```

`X+0x878 = InGameUI+0x4de08` is written literally at **exe+0x20215**
(`mov rax,[rsi+0x2f0]; add rax,0x4de08; mov [rsi+0x878],rax`). `PetSel+0x20` is that same field
(0x858+0x20 = 0x878), so "the pet selection's panel pointer" and "InGameUI+0x4de08" are one object.

**Correction to `docs/ingame-ui-survey.md`**: `InGameUI+0x4de08` is the **portrait panel**, not the XP-bar
rollover. `InGameUI::Init` (exe+0x213840) hands it the `hud_mastertable.dbr` field **`hudStatusManager`**
at exe+0x2157ef..0x21584c — i.e. `records/ui/hud/hudstatus/hud_statusmanager.dbr`, whose fields are
`BackgroundImagePet`, `BackgroundImagePetHighlight`, `PetHealthBarX/Y/Width/Height`, `PetManaBar*`,
`PlayerNameText`, `initialPanelPositionX/Y` (read with `tools/arz.py`). The field `hudPlayerStatusIcons`
goes to `InGameUI+0x4b408` (exe+0x215788), which is what the survey called "status manager".

### PetSel (X+0x858) -- the selection
| offset | (abs on X) | meaning |
|---|---|---|
| +0x00 | X+0x858 | not read anywhere found |
| +0x08 | X+0x860 | `std::list<unsigned int>` head node -- the SELECTED pet object ids (MSVC node: `+0` next, `+8` prev, `+0x10` value) |
| +0x10 | X+0x868 | list size (the "any pets selected?" gate) |
| +0x18 | X+0x870 | the local player's object id (written at exe+0x20128 from `[main_obj+0xc0]`'s `Object::GetObjectId`) |
| +0x20 | X+0x878 | -> Panel (= InGameUI+0x4de08) |

So the selection is **a list of object ids, not a bitmask and not an index**. Order = selection order.

### Panel (InGameUI+0x4de08) -- ctor exe+0x256510, dtor exe+0x256640, vtables exe+0x31c0a0 (obj+0) / exe+0x31c188 (obj+8)
| offset | meaning |
|---|---|
| +0x90, +0x91 | two bytes, both 1 in the ctor (inferred: show-pets / show-party) |
| +0x94 | float, per-portrait x offset (0.0) |
| +0x98 | float, first-portrait y offset |
| +0x9c | float, row pitch = 60.0f |
| +0xb0/+0xb8 | `std::string` (the record path) |
| **+0xc0/+0xc8/+0xd0** | **`std::vector<Portrait*>` -- the PET portraits** |
| +0xd8/+0xe0/+0xe8 | `std::vector<Portrait*>` -- the PARTY portraits |

Panel screen origin is `InGameUI+0xac1a8` (x) / `+0xac1ac` (y) -- the render/mouse dispatch at exe+0x20c95f
and exe+0x20d1d5 pass it in. Vtable slots (obj+0 base exe+0x31c0a0): `+0x38` HandleMouseEvent exe+0x257920,
`+0x48` Update exe+0x257c40, `+0x58` HandleKeyEvent = `exe+0x32340` (a stub), `+0xb0` Show, `+0xb8` IsVisible.

`Panel::Update` (exe+0x257c40) = rebuild pets (exe+0x256b90) -> rebuild party (exe+0x2570a0) -> lay both
vectors out at `y = +0x98 + i * +0x9c`, writing each portrait's `+0x50c/+0x510`, then `portrait->vt[0x48]()`.

**Pet rebuild** (exe+0x256b90, verified):
1. `GameEngine::GetLocalPetList()` (Game.dll export, `mem::vector<unsigned int>` at `GameEngine+0x36138`).
   If empty -> destroy every pet portrait and return.
2. Drop each existing portrait whose `+0xac` id fails `GameEngine::IsLocalPet(id)`.
3. For each id in the list with no portrait yet: allocate **0x9a0** bytes, `Portrait::ctor` (exe+0x251c10),
   `vt[0x18] SetRecord(panel+0xa0)`, `+0x98 = panel`, `vt[0x88] SetOwner(X)`, `exe+0x255a50(portrait, id)`,
   then `+0xa0 = 2` if the entity is-a `Hireling` else `1`.

**There is no cap**: one portrait per entry of the local pet list. The 5 keys index the first five
(so a Raise-Skeletons army shows N portraits but only 1..5 are key-selectable). Grouping of same-type pets:
none -- one portrait per pet object.

### Portrait -- ctor exe+0x251c10, size 0x9a0, vtables exe+0x31c1a8 (obj+0) / exe+0x31c1a0 (obj+8)
| offset | meaning |
|---|---|
| +0x30 | -> X (framework-B "owner", set by vtable+0x88) |
| +0x90 | listener sub-object; its vtable is exe+0x31c190, slot 0 = **exe+0x252470** (the context-menu action handler). Its `this` is `Portrait+0x90`, so its `+0x10` = Portrait+0xa0 and `+0x1c` = Portrait+0xac |
| +0x98 | -> Panel |
| **+0xa0** | kind: **0 = party player, 1 = pet, 2 = hireling** |
| +0xa4 | byte, 1 when life == 0 or max life == 0 (no bar) |
| **+0xac** | the entity's object id (pet monster id, or player id for kind 0); 0 = the object no longer resolves |
| +0x1d8 | byte, **selected** (recomputed each Update by scanning PetSel's list) |
| +0x1e0 | -> the open context menu (0xe8 bytes, ctor exe+0x191910), null when closed |
| +0x508 | int ms, damage-flash timer (set to 0x1f4 = 500 when life drops) |
| +0x50c/+0x510 | float x,y position (written by Panel::Update) |
| +0x52c | float, last seen current life |
| +0x628, +0x530 | text elements (name / level), written via `vt[0xb8]` |
| +0x7a0/+0x7a4/+0x7a8/+0x7ac | float hit rect x, y, w, h (absolute) |
| +0x7b0/+0x7b8, +0x7c8/+0x7d0 | vectors of status-icon controls (stride 8 and 0x28) |
| **+0x908** | int, the pet's stance = `Monster::ControllerType` (0 normal, 1 aggressive, 2 defensive) |
| +0x90c/+0x910 | last mouse position |

`Portrait::Update` (exe+0x254c20, pet branch at exe+0x2552fc, verified) reads, per frame:
`Character::GetActiveStatusSkills` -> `SkillManager::FindSkillId` -> `Skill::GetResourceName` ->
`SkillProfile::GetUpBitmapName` (the buff icons); `Character::GetCharacterBio` then
`CharacterBio::GetAttribute(4)` = max life, `GetAttribute(5)` = max mana, `GetCurrentLife()` (double),
`GetCurrentMana()`; `Character::GetPortraitName` -> `LocalizationManager::ToWChar` (the displayed name).
`exe+0x255a50` (set id) additionally does `Player::GetPetControllerType(GetPetOwner(petId))` -> `+0x908`
and `Monster::GetStatusIconRed` / `Monster::GetStatusIcon`.

## 1. The six key actions (`InGameUI::HandleKeyAction` = exe+0x211980)

Dispatch: `index = action - 1`, byte table at **exe+0x21215c** (0x3d entries), dword target table at
**exe+0x2120f4** (image-base-relative). Decoded:

| action | tag (binding table `[exe+0x3d02d0] + 8*action`) | localized name | case |
|---|---|---|---|
| 0x2c | tagSelectPet1 | Select Pet 1 (F2) | exe+0x211dcf |
| 0x2d | tagSelectPet2 | Select Pet 2 (F3) | exe+0x211de6 |
| 0x2e | tagSelectPet3 | Select Pet 3 (F4) | exe+0x211e00 |
| 0x2f | tagSelectPet4 | Select Pet 4 (F5) | exe+0x211e1a |
| 0x30 | tagSelectPet5 | Select Pet 5 (F6) | exe+0x211e34 |
| 0x31 | tagSelectAllPets | Select All Pets (F7) | exe+0x211e4e |

There is no sixth pet action: 0x2c..0x30 are pets 1..5 and 0x31 is "all". The two neighbours the controls
table lists are **not** in this jump table: **Target Pet (Hold Key and Click) = tagKeybind35 = action 0x26**
and **Toggle Pet Display = tagKeybind38 = action 0x29** (Toggle Party Display = tagKeybind39 = 0x2a).

Bodies (verified):
```
0x2c..0x30:  exe+0x205170([X+0x30] + 0x858, n)          n = 0..4
0x31:        exe+0x205490(PetSel);  for (i=0; i<5; ++i) exe+0x205170(PetSel, i)
```
Note `[rsi+0x30]` with rsi = InGameUI is exactly X, so the argument is `PetSel`.

### exe+0x205170 -- `SelectPetByIndex(PetSel*, int index)`
Builds a `mem::vector<uint>` on the stack via **exe+0x256a90(Panel, &vec)** -- which walks the panel's PET
vector (`Panel+0xc0/+0xc8`) and pushes each portrait's `+0xac` -- then, if `index < count`,
`exe+0x205200(PetSel, ids[index])`, then frees the vector. Out-of-range does nothing.

### exe+0x205200 -- `TogglePetById(PetSel*, uint id)`
`ObjectManager::GetObject(id)` (exe+0x9ee0), is-a `Monster` guard (`Monster::classInfo` + the parent walk at
exe+0x2a3210); non-Monster becomes null; tail-calls exe+0x205280.

### exe+0x205280 -- `TogglePet(PetSel*, Monster*)` -- the whole selection semantics
```
id = Object::GetObjectId(m)
if (id is in PetSel's list):            // DESELECT
    unlink the node, --size, free it
    Monster::RemoveControlBanner(m)     // Game.dll export
else:                                   // SELECT
    if (m->vt[0x8d0]() != PetSel->owner_id) return    // vt+0x8d0 = Monster::GetLeader (verified
                                                      // against Game.dll's Monster vftable{for Object})
    s = Monster::GetPetAcknowledgeSound(m)            // Game.dll export
    if (s) s->vt[0x30](s, Entity::GetCoords(m), 0, true)   // positional acknowledge bark
    push_back(id)
    exe+0x2053e0(PetSel, m):
        name = GameEngine::GetDatabase()->vt[0x28]("petBannerName", "")
        Monster::SetControlBanner(m, name)            // Game.dll export
```
`petBannerName` is **absent from `records/game/gameengine.dbr` in 1.3.0.8**, so the banner string is empty
(the highlight the player sees is the portrait's `BackgroundImagePetHighlight`, drawn off `Portrait+0x1d8`).

Selecting is therefore a **toggle**: F2 twice deselects pet 1. Only pets whose `Monster::GetLeader()` equals
the local player id can be selected.

### exe+0x205490 -- `ClearSelection(PetSel*)`
For every id in the list: resolve, is-a Monster, `Monster::RemoveControlBanner`; then empty the list
(size = 0) and free the nodes.

## What a selection changes downstream (verified)

In the exe's world cursor/click function **exe+0x21010** (the same one that does `SetCombatEnemy(0)` at
exe+0x218eb / `SetCombatEnemy(id)` at exe+0x21b9c -- see AGENTS.md), at the point where a click would be
turned into a player action:

```
exe+0x225d0:  if ([X+0x868] != 0)                     // any pets selected
                  exe+0x205560(PetSel, targetId, movePoint)   // command them
                  exe+0x205490(PetSel)                        // and clear the selection
              else
                  ControllerPlayer::HandleActionFromMouse(ctrl, isLeft, [X+0x848], ...,
                                                          [X+0x849] /* stack arg */, ...)
```
So **while any pet is selected the click does not reach the player at all** -- no attack, no move, no skill:
it is consumed to command the pets, and the selection is one-shot (cleared immediately after).

### exe+0x205560 -- `CommandSelected(PetSel*, uint targetId, WorldVec3 const* movePoint)`
For each selected id, resolve + is-a Monster, then:
```
s = Monster::GetPetAttackSound(m); if (s) s->vt[0x30](s, Entity::GetCoords(m), 0, true)
cmd = targetId ? new(0x18) RequestAllyAttackConfigCmd(petId, PetSel->owner_id, targetId)
                : new(0x30) RequestAllyMoveConfigCmd (petId, PetSel->owner_id, *movePoint)
cmd->vptr = exe+0x30da08 (attack) / exe+0x30d9f0 (move)   // the exe's own copies of the vtables
m->vt[0x350](m, cmd)                                       // = Engine.dll GAME::Actor::Enqueue
```
Both ConfigCmd ctors are Game.dll exports; `Actor::Enqueue` is an Engine.dll export. Returns true if any
pet was commanded.

### The Target Pet (Ctrl) hold
`X+0x848..0x850` are the exe's hold flags, set on key down at exe+0x27020.. and cleared on key up at
exe+0x26c41.. through a second jump table (`index = action - 0x15`, byte table exe+0x27334, targets
exe+0x27300), in the world key handler exe+0x26920:

| action | tag | flag |
|---|---|---|
| 0x26 | tagKeybind35 Target Pet (Hold Key and Click) | **X+0x849** |
| 0x27 | tagKeybind36 Stationary Attack / Hold Position | X+0x848 |
| 0x32 | tagForceMove | X+0x84a |
| 0x33 | tagMove | X+0x84b |

With that flag set, the click path takes a different branch (exe+0x2173c, verified):
```
ControllerPlayer::SetCommandRepeated(ctrl, false)
if ([X+0x849]) { exe+0x205200(PetSel, ControllerPlayer::GetCombatAlly(ctrl)); X+0x1d0 = 0 }
else           { X+0x1d0 = 1 }
```
i.e. **Ctrl+click on a pet in the world toggles that pet's selection**, using the combat *ally* the exe
re-resolves from the cursor every frame (`SetCombatAlly(0)` at exe+0x218f0, `SetCombatAlly(id)` at
exe+0x21c45 -- the exact mirror of the combat-enemy pattern the mod already relies on).
`X+0x849` is also passed to `HandleActionFromMouse` as its stack argument (exe+0x225fd), and
`X+0x848` as `r9d`.

**"Click a portrait, then click a target"** is the same mechanism with a different entry point: the portrait's
own mouse handler toggles the selection (below), and the next world click is eaten by the `X+0x868 != 0`
branch above.

## 2. The portrait context menu (right-click)

`Portrait::HandleMouseEvent` = **exe+0x254570** (vtable exe+0x31c1a8 + 0x38). In order:

1. `+0xac == 0` -> return false (empty portrait).
2. Store the mouse position at `+0x90c`. Resolve the entity.
3. **Skill-on-pet path**: if `PlayerHotSlotCtrl::IsAnySlotActive()` and `[hotslotctrl+0x28] == 0`
   (a skill is armed on the cursor) and the event is a button *down* (type 1 or 2) inside `+0x7a0..+0x7ac`:
   `ControllerPlayer::SetCombatAlly(ctrl, petId)`, `SetCombatEnemy(ctrl, 0)`,
   `ControllerPlayer::HandleActionFromMouse(ctrl, 0, isLeft, true, true, Entity::GetCoords(pet), &petId, 0)`.
   That is how a sighted player casts a buff on a pet by clicking its portrait.
4. Right-click (type 2) inside the rect -> `exe+0x253f90(portrait, &localPos)` = open the context menu.
5. **Left-click (type 1) inside the rect -> `exe+0x205200([Portrait+0x30] + 0x858, petId)`** -- the same
   toggle the F-keys use.

### Menu build -- exe+0x253f90
Destroys `+0x98` if set, allocates 0xe8 bytes (ctor exe+0x191910) into `Portrait+0x1e0`, sets
`menu+0x30` = position, `menu+0xc0 = Portrait+0x90` (the listener), `menu+0xc8 = InGameUI`. Then
`exe+0x191bd0(menu, title, styleRecord)` adds a title and `exe+0x191d00(menu, tag)` adds one item per tag
(the helper localizes the tag with `LocalizationManager::Localize("SimpleStringFormat", tag)` and keeps the
**tag** as the item's identity).

- `Portrait+0xa0 == 0` (party player): title = player name + `PlayerManagerClient::GetPlayerClass`, items
  `tagPortraitRemove` (only when not auto-party and you are the party leader), `tagInspectOption`,
  `tagPortraitTrade`, `tagPortraitChat`, ... (exe+0x253fca..0x2543c0).
- `Portrait+0xa0 != 0` (pet/hireling, exe+0x2543c5): **`tagPortraitAggressive`, `tagPortraitNormal`,
  `tagPortraitDefensive`, [`tagPortraitHireling` only when kind == 2], `tagPortraitDisband`.**

Localized (Text_EN.arc): Set to Aggressive / Set to Normal / Set to Defensive / Inspect / Disband Pet.

### Menu action -- exe+0x252470 (`this` = Portrait+0x90, arg = `std::string const& tag`)
The pet half (exe+0x252a79) resolves `pet = ObjectManager::GetObject(this+0x1c)` and
`player = GameEngine::GetMainPlayer()`, then compares the tag:

| tag | what it does (all Game.dll exports unless noted) |
|---|---|
| `tagPortraitDisband` | `ctrlId = Character::GetControllerId(player)`; `ctrl = ObjectManager::GetObject(ctrlId)`; `ctrl->vt[0x250](ctrl, petId, false)`. **vtable +0x250 on `ControllerPlayer::vftable` is `ControllerPlayer::ReleasePet(unsigned int, bool)`** (checked against Game.dll's vftable; the body forwards to the current state's `RequestReleasePet`, vt+0x1d8). Exported -- callable by name. |
| `tagPortraitAggressive` | `owner = PetPen::GetPetOwner(player+0x2050, petId)`; `Player::SetPetControllerType(player, owner, 1)`; then, on a network client that is not server/single, `GameEngine::MonsterUseController(gGameEngine, petId, 1)`, else `Monster::UseController(pet, 1)`; finally `Portrait+0x908 = 1`. |
| `tagPortraitNormal` | same with **0**. |
| `tagPortraitDefensive` | same with **2**. |
| `tagPortraitHireling` | (kind 2 only) not read; the tag localizes to "Inspect". |

So the stance enum is `GAME::Monster::ControllerType` = **0 Normal, 1 Aggressive, 2 Defensive**, stored per
*pet owner key* on the Player (`Player::Get/SetPetControllerType(ownerId, type)`, where `ownerId` comes from
`PetPen::GetPetOwner(petId)` -- the pen groups pets of the same summon), and applied to the individual
monster with `Monster::UseController`. `player+0x2050` is the inline `PetPen`; `Character::GetPetPen()` is
exported, so the mod never needs that offset.

**Everything the menu does is reachable from exports alone** -- the mod does not need the drop menu:
```
read  : Player::GetPetControllerType(player, PetPen::GetPetOwner(Character::GetPetPen(player), petId))
write  : Player::SetPetControllerType(player, owner, type); Monster::UseController(pet, type)
disband: ControllerPlayer::ReleasePet(ctrl, petId, false)
```

## 3. How many pets

- The panel makes **one portrait per entry of `GameEngine::GetLocalPetList()`** -- uncapped, no grouping.
- The keys reach only the **first 5** (`SelectPetByIndex` with n = 0..4); "Select All Pets" clears and then
  toggles indices 0..4, so it selects at most 5 too.
- `GameEngine::GetMiniPetLimit()` (= `miniPetLimit` 3 in `records/game/gameengine.dbr`) and the per-skill
  `Skill::GetPetLimit(uint)` are the *spawn* limits; they do not gate the UI.

`GameEngine::GetLocalPetList()` = `mem::vector<unsigned int>` at `GameEngine+0x36138` (Game.dll+0x2b22c0 is
`lea rax,[rcx+0x36138]; ret`). Maintenance (verified in Game.dll):
- `GameEngine::RegisterLocalPet(id)` Game.dll+0x2b0ec0 -- called from `Monster::JoinMe`.
- `GameEngine::UnregisterLocalPet(id)` Game.dll+0x2b2240 -- **called from `Monster::CharacterIsDying`**.
- `GameEngine::ClearPetList()` Game.dll+0x2b0eb0 -- and `GameEngine::ExitPlayingMode` touches the vector.
- `GameEngine::Update` (+0x5ff) sweeps the list every frame.
All four plus `IsLocalPet` are exports.

## 4. Pet events

**There is no announcement.** No `GameEngine::AddUINotification`, no `ControllerPlayer::SetUserText`, no
localized tag for a pet dying or being summoned exists anywhere near this code (`tagPet*` in Text_EN.arc is
only character-sheet "Pet Bonuses" stat lines). The only feedback the game gives is visual (the portrait
appears / blanks / its bar drops) plus two positional barks the mod can reuse:

- `Monster::GetPetAcknowledgeSound(pet)` -- played at the pet's coords when it becomes selected.
- `Monster::GetPetAttackSound(pet)` -- played at the pet's coords when it is commanded.

**Observables for the mod** (all per-frame polls, no hooks needed):
- pet appeared / died: membership change in `GameEngine::GetLocalPetList()` (a pet leaves the list inside
  `Monster::CharacterIsDying`, i.e. at the start of the death, not when the corpse is freed);
- health: `CharacterBio::GetCurrentLife()` / `GetAttribute(4)` on `Character::GetCharacterBio(pet)`
  (the same numbers the portrait draws) -- the existing `core/threshold_watcher` applies directly;
- name: `Character::GetPortraitName(pet)` (what the portrait shows) or the mod's existing
  `world::label_of(id)` (`Monster::GetGameDescription`);
- stance: `Player::GetPetControllerType(player, PetPen::GetPetOwner(pen, petId))`;
- selected: the `PetSel` list at `X+0x860` (or `Portrait+0x1d8`).

## Recommended mod path (no new screen space, minimum new RVAs)

1. **Selection**: call `exe_ui::ingame_key_action` (already wired, signature-checked, exe+0x211980) with
   action **0x2c..0x30** for pets 1..5 and **0x31** for all. That covers the whole select/deselect model with
   zero new RVAs. For "select the pet the review cursor is on", the direct call is
   `exe+0x205200(X+0x858, petId)`.
2. **Read the list of pets**: `GameEngine::GetLocalPetList()` (export) -- do not walk the portrait vector.
   Portrait order and list order are the same (the rebuild appends in list order), so index i in the list is
   the pet that F(i+2) selects, as long as no portrait was left over; if exactness matters, mirror the exe and
   read `Panel+0xc0/+0xc8` -> `+0xac` (exe+0x256a90 does this).
3. **Command**: with a selection live, the mod's existing left-click path (J / Enter through the game's mouse
   poll, which reaches exe+0x21010) is automatically consumed as the pet command against the virtual cursor's
   target. The direct call is `exe+0x205560(X+0x858, targetId, &worldVec3)`; passing `targetId = 0` is
   "move here". Note this is the ONE case where the mod's `ControllerPlayer::ItemAction` shortcut for ground
   items would bypass the pet command.
4. **Stance / disband**: exports only, as listed above. No menu, no clicks.
5. **Announcing**: poll the pet list; there is nothing to hook.

## Byte signatures (first 16 bytes, for `exe_ui::available()`-style checks)

```
exe+0x205170 SelectPetByIndex      40 57 48 83 ec 40 48 c7 44 24 20 fe ff ff ff 48
exe+0x205200 TogglePetById         48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 8b
exe+0x205280 TogglePet(Monster*)   48 85 d2 0f 84 48 01 00 00 48 89 6c 24 20 56 48
exe+0x205490 ClearSelection        48 89 5c 24 18 56 48 83 ec 20 48 8b 41 08 48 8b
exe+0x205560 CommandSelected       40 55 56 57 41 54 41 55 41 56 41 57 48 83 ec 70
exe+0x256a90 PanelPetIds           40 53 55 56 48 83 ec 20 48 8b 02 33 ed 48 89 42
exe+0x256b90 PanelRebuildPets      48 89 4c 24 08 53 55 56 57 41 54 41 55 41 56 41
exe+0x257c40 PanelUpdate           48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48
exe+0x256510 PanelCtor             33 d2 c7 41 10 00 00 00 00 66 c7 41 14 00 00 48
exe+0x251c10 PortraitCtor          48 8b c4 48 89 48 08 55 56 57 41 54 41 55 41 56
exe+0x255a50 PortraitSetId         48 89 5c 24 10 48 89 6c 24 18 56 48 83 ec 20 8b
exe+0x254570 PortraitHandleMouse   48 89 5c 24 18 55 56 41 54 41 56 41 57 48 81 ec
exe+0x254c20 PortraitUpdate        48 8b c4 89 50 10 48 89 48 08 55 53 56 57 41 54
exe+0x253f90 PortraitOpenMenu      48 8b c4 55 48 8d 68 a1 48 81 ec b0 00 00 00 48
exe+0x252470 PortraitMenuAction    40 55 56 57 41 54 41 55 41 56 41 57 48 8d 6c 24
exe+0x211980 InGameUI::HandleKeyAction  48 8b c4 57 41 54 41 55 41 56 41 57 48 83 ec 40
```

## Game.dll / Engine.dll exports this code uses (all resolvable by decorated name)

`GameEngine::GetLocalPetList`, `IsLocalPet`, `RegisterLocalPet`, `UnregisterLocalPet`, `ClearPetList`,
`MonsterUseController`, `GetMiniPetLimit`; `Monster::GetLeader`, `UseController`, `SetControlBanner`,
`RemoveControlBanner`, `GetPetAcknowledgeSound`, `GetPetAttackSound`, `GetStatusIcon`, `GetStatusIconRed`;
`Player::Get/SetPetControllerType`, `Character::GetPetPen`, `PetPen::GetPetOwner`;
`ControllerPlayer::ReleasePet`, `Get/SetCombatAlly`, `HandlePetAction`, `HandleActionFromMouse`,
`SetCommandRepeated`; `Character::GetControllerId`, `GetCharacterBio`, `GetPortraitName`,
`GetActiveStatusSkills`; `CharacterBio::GetCurrentLife/GetCurrentMana/GetAttribute`;
`RequestAllyAttackConfigCmd::ctor`, `RequestAllyMoveConfigCmd::ctor`, `Actor::Enqueue` (Engine.dll).

## Open / not resolved

- **Toggle Pet Display (action 0x29) / Toggle Party Display (0x2a)**: not in `InGameUI::HandleKeyAction`'s
  table (they fall through to the default at exe+0x211f78, which forwards the action to every child widget's
  `HandleKeyEvent`, vt+0x58) and not in the world key-down table at exe+0x26f62. The panel's own key slot is
  the `exe+0x32340` stub, so some other widget owns it. Cosmetic; irrelevant to the mod.
- `Panel+0x90`/`+0x91` as "show pets"/"show party" is **inferred** from the ctor (`0x0101`), not from a use.
- `tagPortraitHireling` ("Inspect") handling in exe+0x252470 was not read (party half of the chain).
- The exe's own ConfigCmd vtables (exe+0x30da08 attack, exe+0x30d9f0 move) look like exe-local copies of the
  Game.dll classes; **inferred**, and a reason to prefer `exe+0x205560` over hand-building the commands.
