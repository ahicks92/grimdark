# The in-world windows: what they are built from and what exports feed them (static RE, 2026-08-22)

Two surveys (exe side with `tools/exe_dis.py`, export side with `tools/exports/*.txt` + `tools/dll_dis.py`),
cross-checked against the export listings. "Verified" = read in disassembly; "inferred" is marked. Nothing
below has been exercised live yet. RVAs are `exe+`; window offsets are from `InGameUI` (`[[main_obj+0x90]+0x2f0]`).
`docs/exe-ui-layout.md` stays the reference for framework A and the already-modelled windows.

## Cross-cutting findings (these change existing assumptions)

- **`GameEngine` IS an exported data symbol**: `?gGameEngine@GAME@@3PEAVGameEngine@1@EA` (Game.dll) and
  `?gEngine@GAME@@3PEAVEngine@1@EA` (Engine.dll). `GetProcAddress` + one dereference; the per-frame
  `GameEngine::Update` hook is then only a tick, not a pointer source. (CLAUDE.md's "not an exported singleton"
  is wrong; fix when switching.)
- **Localization needs no hook**: `LocalizationManager::Instance()` and `LocalizeWithoutParams(char const*)`
  (returns `unsigned short const*`, no hidden pointer) are public exports; also `Localize(tag, ...)`,
  `LocalizeStripColorTags`.
- **`GameTextLine` is the universal tooltip currency**, stride 0x40 (verified from `GameTextLineToString`):
  `+0x00` `GameTextClass` (dword), `+0x08` `basic_string<unsigned short>` (our `MsvcStringW` shape), `+0x28`
  bool, `+0x30` `GraphicsTexture const*` (inline icon), `+0x38` float indent. Every `GetUIDisplayText` /
  `GenerateUI*Text` builder appends to a `mem::vector<GameTextLine>` (pass a zeroed `{begin,end,cap}`).
  The vector and its strings come from the game's allocator (no exported free; free thunk Game.dll+0x5c4d4c
  inferred) -- build on demand from a key press, not per frame.
- **Call the `GetUIDisplayText` / `GetGameDescription` family virtually** (the export is the base impl;
  ItemEquipment, ItemRelic, ItemNote, OneShot_Potion... override). Same vtable-slot trick as
  `Object::GetRTTIClassInfo`.
- **Object id -> pointer is the one gap.** Inventory, equipment, lore and skill APIs hand back `unsigned int`
  ids; `ObjectManager::GetObject(id)` is NOT exported (Game.dll+0x19d20 internal, reached from
  `Singleton<ObjectManager>::Get`, verified from `Skill::ResolveValidateTarget`). Options: the exported
  `ObjectManager::GetObjectList(mem::vector<Object const*>&)` sweep + `Object::GetObjectId()` (pure export,
  O(all objects), cache per window open) or the RVA call (fast, patch-fragile). The sphere scan in
  `world.cpp` does not see inventory items (not in the region).
- Framework B vtable (every InGameUI window): `+0x10` Init, `+0x18` SetRecord(std::string), `+0x20` Render,
  `+0x38` HandleMouseEvent, `+0x48` Update, `+0x58` HandleKeyEvent (`return false` stub except the minimap),
  `+0xb0` Show(bool), `+0xb8` IsVisible, `+0xf0` OnControlEvent. Every window has a listener sub-object at
  `window+0x90` (its slot 0 = the same OnControlEvent); buttons register with `exe+0x12a800(registry,
  control, window+0x90)`. `InGameUI`'s real ctor is `exe+0x205a70` (constructs every member window);
  `Init` `exe+0x213840` hands each its `.dbr` path (field names of `records/ui/hud/hud_mastertable.dbr`).
- `Engine::GetAreaNameTag()` (via `gEngine`) is the minimap's area name (CLAUDE.md lists it as uncaptured).

## Which key opens which window (`InGameUI::HandleKeyAction` exe+0x211980, jump table on action id)

- 1 Character Window (C / I): HUD button `+0x93e8` through host `+0x7338` -> inventory/character `+0xbbf0`.
- 2 Skill Window (N): if devotion `+0x813a0` visible -> Show(false); else button `+0x9db0` -> skills `+0x3fc20`.
- 3 Codex (Q): button `+0x9730` -> codex `+0x285a0` (refused while its disabled byte `+0x99b1` is set).
- 4 Map (M): `+0xa0f0` -> minimap `+0x42260`. 5 Chat (Enter): `+0xaab8` -> chat `+0x4df00`.
- 6 Group (K): `+0xa430` -> party `+0x4b540` (network only). 7 Game Menu (G): `+0x9a70` via host `+0x72f8` -> exit `+0x4a300`.
- 8 Help (H): toggles the codex and presses its helpButton `+0x1278` through the window's registry `+0x220`.
- 0x21 Riftgate (L), 0x22 Switch Weapons, 0x2c..0x31 pets, 0x34 Factions (J): `+0xadf0` -> `+0x6c9b8`,
  0x35 Achievements (V): `+0xa778` -> `+0x7d150`, 0x36 Interact, 0x37 Pickup, 0x38 Loot Filter (O): `+0x8940` via `+0x72f8` -> `+0xab410`,
  0x3c Toggle UI (]): flips the byte `+0xac990` (ctor sets 1 = shown; `exe_ui::ui_visible`, announced by
  `quickbar_tick`). Read ONLY by the render pass exe+0x20a860: while 0 it skips every window and the HUD except the
  exit window `+0x4a300`, the options host `+0x4def8`, the prompt box `+0x7378` (and the dev item-spawn window);
  Update / key / mouse handling never look at it, so hidden windows still take keys and the HUD still eats clicks
  (VERIFIED live 2026-09-17: with the byte 0 a hover on the game-menu button set its rollover byte, a click opened the
  exit window and no HandleActionFromMouse fired -- identical to the shown state; the "behind the interface" refusal stays).
  Not persisted. 0x3d = the Ascendant Altar window (expansion 3 only).
- `InGameUI::OnWindowVisibilityChanged` exe+0x213110 keeps each HUD button's pressed byte in sync, so
  **"is window X open" = a byte**: inventory `+0x966a`, codex `+0x99b2`, skills `+0xa032`, minimap `+0xa372`,
  chat `+0xad3a`, factions `+0xb072`, party `+0xa6b2`, achievements `+0xa9fa`, loot filter `+0x8bc2`,
  exit/prompt `+0x9cf2`. (Or the window's own `IsVisible`, vtable +0xb8.)
- Input priority (exe+0x212590): prompt box, exit, stack split, devotion, shrine, inventory, skills, codex,
  minimap, achievements, factions, loot filter, vendor, faction vendor, caravan, transmuter, enchanter,
  quest reward, trade, crafting, chat, inspect, party, altar, potions, item ascension, status manager.

## Window offsets (InGameUI+), ctor, vtable, size

- inventory/character `+0xbbf0` (ctor exe+0x263580, vt exe+0x31cc98, 0x1c9b0) -- MISSING from exe-ui-layout.md.
  `+0x52258` is a second instance of the same base class (inferred: the multiplayer Inspect window).
- codex/quest `+0x285a0` (exe+0x22ce50, vt exe+0x31b170, 0x1728); skills `+0x3fc20` (exe+0x279290, vt
  exe+0x31d8a8, 0x2640); devotion `+0x813a0` (exe+0x185640, vt exe+0x315d80, 0x2b38); factions `+0x6c9b8`
  (exe+0x1beda0, vt exe+0x317338, 0x10798); potions `+0x8a300` (exe+0x275910, vt exe+0x31d7a0, 0x17a8);
  minimap `+0x42260` (exe+0x1f41a0, vt exe+0x319eb8, 0x80a0); exit `+0x4a300` (as documented).
- trade `+0x29cc8` (exe+0x25ef90, vt exe+0x31c998); vendor `+0x2b538` and faction vendor `+0x2e188` (same
  class, exe+0x2716c0, vt exe+0x31d6a0, 0x2c50); caravan/stash `+0x4fd08` (exe+0x134590, vt exe+0x314788);
  enchanter `+0x30dd8` (exe+0x26bd00); crafting `+0x3aa80` (exe+0x269e80); transmuter `+0x85378`
  (exe+0x27d5c0); altar `+0x87628` (exe+0x115dd0); item ascension `+0x8baa8` (exe+0x110a90); shrine
  `+0x7da50` + `+0x7f6f8` (exe+0x1d0d20).
- party `+0x4b540` (exe+0x1ff2b0, vt exe+0x31a3c8); achievements `+0x7d150` (exe+0x10bca0, vt exe+0x312b80);
  quest reward `+0x8efd8` (exe+0x225440, vt exe+0x31ae08); objective tracker `+0x90390` (exe+0x1cfa40, vt
  exe+0x318038); stack split `+0x83ed8` (exe+0x1db730, vt exe+0x3188b0); loot filter `+0xab410`
  (exe+0x1c7c30, vt exe+0x317640); hot-slot select `+0x4fb78`; chat `+0x4df00`; notification manager
  `+0x4dbd8`; player status icons `+0x4b408`; HUD status manager (pet + party portraits, `hud_statusmanager.dbr`; docs/re_pets_exe.md) `+0x4de08`; survival `+0x90890`; endless dungeon
  `+0x91488`; challenge area `+0x92260`; gamepad HUD `+0x92570`; network address `+0x8fba8`; item spawn
  (dev) `+0x8e078`; info icon `+0x90298`.
- `+0xb138`/`+0xb158` are the `hudNpcDialogWindow` / `hudEndGameDialogWindow` record paths (the conversation
  window is built on demand, as documented).

## Per window: content source, actions, verdict

### Objective tracker (`+0x90390`) -- EASY, no widgets
Update exe+0x1d00f0 = `GameEngine::GetObjectives()` -> `mem::vector<std::string> const&`. Tracked quests =
`Quest2Repository::GetQuests(vec, 0x4)`. Nothing to press.

### Codex / quest log (`+0x285a0`) -- EASY
- Tabs: `+0x598` questButton, `+0x8d0` questCompleteButton, `+0xc08` codexButton (lore), `+0xf40`
  devotionButton, `+0x1278` helpButton (tutorials), through registry `+0x220`; close `+0x260` (registry
  `+0x1d8`); title `+0x98`. Pages are heap model/view pairs at `+0x190..+0x1d0`.
- Model, all by reference (no hidden-pointer returns at all): `Singleton<Quest2Repository>::Get()` ->
  `GetQuests(mem::vector<Quest2*>&, Filter)` (appends; filter bits: 0 all, 1 task in progress, 2 a completed
  task, 4 in progress and tracked, 8 blocked (inferred)); `Quest2::GetName/GetGroup/GetText(int)/IsTracked/
  SetTracked(bool)/IsComplete(bool)/InProgress(bool)/GetNumTasks/GetTaskByIndex`; `Quest2Task::GetName/
  GetDescription/GetState()` (1 available, 2 in progress, 3 complete; field `+0x90`, hidden byte `+0xc1`) /
  `GetObjectives()` / `GetRewards()`; `Quest2Objective::GetText(u16string&)`, `IsSatisfied()`;
  `Quest2Event::GetText(u16string&)`. Only action: `SetTracked`. (`Complete/Reset/BeginTask/Debug*` are
  server/dev mutators -- never call.)
- Lore tab: `Player::GetLoreCodex()` -> `mem::vector<unsigned> const&` of `ItemNote` object ids (Player
  `+0x4838`); `ItemNote::GetCodexTitleTag()` (`+0xca8`), `GetCodexSubHeadingTag()` (`+0xcc8`), body via
  `ItemNote::GetUIDisplayText` (virtual) or the .dbr `itemText` tag localized. Needs id -> object.
- Tutorials tab: `GameEngine::ShowTutorialPage / UnlockTutorialPage`.

### Factions (`+0x6c9b8`) -- EASY, read-only
`Character::GetFactionPack()` (Character `+0x19f0`); `FactionType` runs -3..46 (50-entry jump table in
`GetFactionTag`); per faction `FactionPack::GetFactionTag(type)` (static, std::string by value: hidden pointer
is the FIRST arg), `GetFactionInfoTag`, `GetValue`, `IsUnlocked` (`byte[pack+0xf3+type]`), `IsModified`,
`IsPositiveBoosted/IsNegativeBoosted`; visibility = `FactionPack::IsModified(type) || GameEngine::IsStartingFaction(type)` and not
`GameEngine::IsHiddenFaction(type)` (the window's own filter; `GameEngine::IsFactionPlayerVisible` is a hardcoded
base-game whitelist that is false for every expansion faction, 2026-09-19); tiers `GameEngine::GetNumFactionLevels`, `FactionValueToLevel`, `GetFactionLevelName(float,
u16string&)`, `GetFactionLevelBounds(float, int&, int&)`, `GetFactionTierReward`. Trap: `FactionPack::
GetDisplay(type)` ignores its argument (returns `byte[+0x4c8]`). 8 tiers in `records/game/gamefactions.dbr`.
Window: rows rebuilt each frame in Update exe+0x1c1ba0 from exactly these calls; close `+0xc58`.

### Skill bar / quickbar -- EASY, no window
`Player::GetPlayerHotSlotCtrl()`; 47 `HotSlotOption*` per skill set (`SkillSet+8`; health potion = 24, mana
potion = 25, evade = 46; sets in a map at ctrl `+0x88`, active `+0x98`, displayed `+0x9c`).
`GetHotSlotOption(i)`, `GetSlotStatus(i)`, `GetPrimarySlot/GetSecondarySlot`, `GetDisplayedSkillSetIndex`,
`NextDisplayedSkillSet`; per option `HotSlotOption::GetDisplayName(u16string&, bool)` (out-param, virtual),
`GetRolloverText(vector<GameTextLine>&)`, `GetType()` (SLOT_TYPE: 0 skill, 2/3 health/mana potion
(which is which inferred), 4 scroll, 5 evade; empty = skill option with id 0), `GetSkillId`,
`GetCooldownRemaining/GetCooldownCompletion`, `GetNumberAvailable`, `GetActiveCharges/GetMaxCharges`.
Act: `ActivateHotSlot(i, bool, bool)`, `ActivateHealthPotionSlot/ActivateManaPotionSlot/ActivateEvadeSlot(bool)`,
`SetHotSlot(i, option)` with the exported option ctors (`HotSlotOptionSkill(id)` 0x20 bytes,
`HotSlotOptionPotion(SLOT_TYPE)`, ...) + `HotSlotOption::SetPlayer`. HUD buttons exist (`InGameUI+0x70d0`,
40 pointers) but are not needed.

### Loot on the ground -- EASY, no window
`Region::GetEntitiesInSphere` (existing path) filtered to `Item` with `Item::PassLootFilter` + `IsPickupOk`;
names via `Item::GetGameDescription` (virtual) / `GetItemTextTag`; the exe's name helper is exe+0x1686f0,
pickup action exe+0x21c6c0. Hover name + health line builder is exe+0x10e940.

### Potions (`+0x8a300`) -- EASY
`PlayerInventoryCtrl::GetTotalPotions(SLOT_TYPE)`, `GetUniquePotionsOfType`, `GetPotionText`,
`GetPotionType`, `GetCurrentCooldown/GetTotalCooldown`, `UsePotionOfType`, `IsOneShotReady`. Controls:
`+0x120`/`+0x650` potions1/2 buttons, `+0xbe0` skillSelect, `+0x11d0` close, `+0x1508` title, rows from
templates `potionsContainer%u`/`potionsModifier%u`.

### Quest reward (`+0x8efd8`, UIQuestRewardWindow) -- DONE 2026-09-06 (`screens/modals.cpp`, `src/quest_rewards.cpp`)
`+0x1b8` questTitleString (tagQuestRewardWindowTitle "Quest Complete" when `Quest2::IsComplete`, else
tagQuestRewardWindowInterim "Quest Progress"), `+0x2b0` questNameString, `+0x388` the Close button (caption
tagQuestRewardAcceptButton = "Close", registry `+0x738`), `+0x7e0` XPValue ("XP: %d", tagQuestRewardXP; drawn when
`+0x808`), reward icon entries `vector<Control*>` at `+0x8d8/+0x8e0` (one widget class per reward kind, capacity
`+0xbc0`). **Shown-ness is the base control byte `+0x28`**, not the generic IsVisible slot (+0x68 stays 0): the
window is its own event listener (sub-vtable exe+0x31adc0; handler exe+0x228d80 takes the quest-completed event
{quest id +4, vector<task uid> +8}), fills itself and sets `+0x28`; Close (exe+0x2285e0, via the +0x388 listener
adapter exe+0x2295e0) clears it. It opens while the conversation window is still up and positions itself beside
it (exe+0x229552 reads the conversation window's rect), and outlives the dialog. The exe builds the icon list from
`Quest2Task::GetRewards()` (complete) / `GetIncentives()` -> `Quest2Event::Evaluate` + `GetActions` ->
`ScriptableActionCollection::GetActions` -> `ScriptableAction::IsReward` (types 4-8, 15, 18, 20, 25, 26) ->
per class: `ScriptableAction_Give{Money,Level,Experience,SkillPoint,AttribPoint,Devotion,Tribute,Faction}::GetAmount`,
`GiveItem::GetNumItems/GetInfoItem(i)/GetCount`, `GiveFaction::GetFactionTag` (std::string by value; ->
`FactionPack::GetFactionFromString` -> `FactionPack::GetFactionTag(type)` -> localize). `Quest2Event::GetText` is
empty in practice (every task in `/quests` prints no reward text), so the codex's reward lines are vestigial.
The mod's rows come from the pipeline instead: `Quest2Task::Complete(bool)` (the single choke point: objectives
met, conversation turn-in and `Quest2Repository::CompleteQuestTask` all end there; it runs `AdjustRewards`, then
each reward event's `Evaluate` + `Execute`) -> `Quest2Event::Execute` = OnActivate each action (a GiveItem
generates its items here), `ScriptableActionCollection::Execute`, OnDeactivate each. Hooking the collection's
Execute inside a Complete and reading the reward actions BEFORE it runs gives the amounts and the generated item
names; the class is the object's vptr against the exported `vftable' symbols (GiveFaction's is not exported =
the remaining reward kind). Nothing to choose, only Close. Verified live 2026-09-06 (Bounty Table turn-ins: XP, iron bits,
an item, +/- reputation). Dev: `/rewards`; `/quests?complete=<questptr>&task=<i>` runs `Quest2Repository::CompleteQuestTask`
(the task must be in progress for its rewards to run: Lua `Game.GetLocalPlayer():GrantQuest(questId, taskUid)` first).

### Stack split (`+0x83ed8`) -- EASY, modal
`+0x160` ok, `+0x510` cancel, `+0xbf8` decrease, `+0xf30` increase; `GameEngine::GetItemMaxStackSize`.

### Shrine (`+0x7da50` ruined, `+0x7f6f8` desecrated) -- DONE (both verified live 2026-08-28)
`+0x540` title, `+0x638` info, `+0x8e0/+0xbd0/+0xec0` offering boxes, `+0x11f8` shrine button, `+0x15a8`
cancel, `+0x1958` close, `+0xa4` the shrine object's id. **Two windows of this shape**: the ruined shrine
(`+0x7da50`, vt exe+0x318230, "Offer" + offerings) and the desecrated shrine (`+0x7f6f8`, its own class, vt
exe+0x318128, loader exe+0x1d3ec0 with "shrineCorruptedBitmap"; info = `tagShrineConfirmProxy` "This shrine is
corrupted and needs to be cleansed. Summon what is trapped within?", button `tagShrineButton02` "Start"). The
game opens the inventory window alongside either; the desecrated one was invisible to the mod until 2026-08-28
(the user's "can't do desecrated shrines"), so the inventory screen won. Start closes both windows and spawns
the record's `normalMonsterSpawn` proxy (verified: Flooded Passage a04). The title (vt exe+0x31c2b0, u16 at
+0x40) and info (vt exe+0x31b830, u16 at +0x38) elements are their own classes, now read by `WidgetB::text`. Model: `StaticShrine::GetDevotionPoints/GetXpReward/IsCleansed/IsLocked/
IsActiveForMainPlayer`, `GetOffering1..3Id`, `GetOffering1..3DisplayName` (u16 by value); act
`ControllerCharacter::SendCleanseShrineCmd` / `StaticShrine::RequestToUse(id)`.

### Inventory / character (`+0xbbf0`) -- EASY-TO-MEDIUM content, MEDIUM actions; the biggest win
- Base class (shared with the inspect window; base ctor exe+0x1e09a0, loader exe+0x1e21f0, Show exe+0x1e16a0):
  `+0x100` characterView, `+0x238` close, `+0x5c0..+0x620` equip-slot control pointers (head, neck, artifact,
  chest, legs, feet, hands, right hand, finger1, finger2, waist, shoulders, medal; left hand in the same
  array), `+0x6e8`/`+0x82f8`/`+0x14bd8` the three stat-tab panes, `+0x196b8/+0x19998/+0x19c78` tab buttons,
  `+0x1a6d0` difficulty indicator.
- Inventory part (loader exe+0x264420): bag tab buttons 1..5 `+0x1a970/+0x1ac50/+0x1af30/+0x1b210/+0x1b4f0`
  (registry `+0x1c950`), sort `+0x1b7d0/+0x1bb08`, deposit components `+0x1be40`, weapon swap `+0x1c2b8`,
  title `+0x1c750`, gold text `+0x1c848`; grids = `std::vector<Grid*>` at `+0x1a760/+0x1a768`.
  Update exe+0x265a50 / Render exe+0x2650b0 re-read `ControllerPlayer::GetInventoryCtrl` every frame
  (nothing cached in widgets). Listener exe+0x2661d0: `SetSelectedSackNumber`, `SortPrimarySack/
  SortSecondarySack`, `DepositReagents`, `Player::SetAlternateConfig`.
- Model: `Player::GetSack(int)` / `PlayerInventoryCtrl::GetSack(int)` (max 6 sacks; vector at ctrl `+0x20`);
  `GetInventoryInSack(int)` -> `mem::map<unsigned itemId, Rect> const&`; `InventorySack::GetGridWidth/
  GetGridHeight/GetItemPosition/GetItemUnderPoint/GetButtonName` (tab label, u16 const&); `GetNumberOfSacks`,
  `GetSelectedSackNumber/SetSelectedSackNumber`; stash `Player::GetPrivateStash`, transfer
  `GameEngine::GetPlayerTransfer`.
- Equipment: two enums. `EquipmentCtrlLocation` (paperdoll) 1..14 = Head, Neck, Chest, Legs, Feet, Ring1,
  Ring2, Hands, RightHand, LeftHand, Relic, Waist, Shoulders, Medal, labels via static
  `EquipmentCtrl::GetEquipmentLocationTag` -> `tagSlotHead`...; `EquipLocations` (mesh attach) 0..14 = R Hand,
  L Hand, Neck, Medal, Finger, Finger, Waist, Head, Chest, Shoulders, Hands, Legs, Feet, Artifact, Clothing;
  convert with `EquipManager::GetEquipLocation/GetEquipCtrlLocation`. Contents `EquipmentCtrl::GetItemId(loc)`
  (`ControllerCharacter::GetEquipmentCtrl()`), `EquipManager::GetItemId`, `Character::GetAttachedItems()` ->
  `vector<EquipManagerContainer{Item*, EquipLocations, bool}>` (gives pointers directly), `GetIsAlternate()`.
- Item text: `Item::GetUIDisplayText(Character const*, vector<GameTextLine>&, bool)` (full tooltip, virtual),
  `GetSimpleUIDisplayText`, `GetFullItemDescription(u16string&)` (flat string), `GetUIRequirementText`,
  `GetUIQualityDescription`, `GetGameDescription(bool,bool)` (name), `GameEngine::GetItemColorText(classification)`
  (rarity word), `GetStackSize/GetMaxStackSize`, `GetItemCost(bool)`, `GetLevelRequirement`,
  `AreRequirementsMet(Character const*)`, sockets `ItemEquipment::HasRelic/GetRelic/HasEnchantment`, sets
  `ItemSet::GetUIDisplayText`. Money `Character::GetCurrentMoney()`.
- Actions: the game's drag state machine `CursorHandler` / `CursorHandlerItemMove` (`+0x28` market id,
  `+0x30` carried item id): `Item::CreatePrimaryCursorHandler(Character const*)`, `SetPlayer/SetId/SetSource/
  SetEquipId`, `PrimaryInitialize`, then `PrimaryInventoryActivate`, `PrimaryEquipActivate(loc)`,
  `PrimaryStashActivate(Vec2)`, `PrimaryMarketActivate` (= sell), `QuickDropInInventory(sack)` /
  `QuickDropInStash(sack)` / `QuickDropInTransfer(sack)` (the shift-click shortcuts: sack index, no cell),
  capability queries `IsEquipCapable/IsInventoryCapable/...`, `Cancel/Escape/IsComplete/DeleteThis`. No
  exported accessor for the game's CURRENT cursor handler. Direct alternatives: `ControllerCharacter::
  SendEquipAttachAction(id, EquipLocations, bool)` / `SendEquipDetachAction(id)`, `PlayerInventoryCtrl::
  UseItem/AddItem/RemoveItem`, `ControllerPlayer::UseItem(id, ItemSource, bool)`, `SendDropItemRandom(id)`,
  `InventorySack::Sort`.
- Obstacles: presenting a 2-D grid linearly; item moves are a sequenced state machine; id -> Item* sweep.
- **Requirements** (static RE 2026-09-11, from a live "Devil's Grin" refusal at sheet Physique 392 / requirement 392):
  `ItemEquipment::AreRequirementsMet` (Game.dll+0x32f810) = base `Item::AreRequirementsMet` (level: `[char+0x1760]`
  vs vt+0x528 `GetLevelRequirement`, reduction attr 0x36) then physique / cunning / spirit = accumulator attrs 1/2/3
  (`Character::GetTotalCharAttribute` IS `CharAttributeAccumulator::GetValue` on `char+0xf78`) vs vt+0x530/+0x538/
  +0x540 (`GetStrength/Dexterity/IntelligenceRequirement`, TQ names). Each requirement is reduced by the character's
  reduction attributes -- per item class, ids stored on the item at `+0xdec..+0xe0c` (three per attribute; the first
  read unconditionally) plus attr 0x35 "Reduction to Attribute Requirements" on all three when positive -- as
  `effective = (int)(req - req*0.01*red + 0.5)` and compared against the RAW float (`Item::MeetsRequirements(value,
  req, red)`, exported). Attributes are fractional (391.7 fails 392), so the mod's sheet now TRUNCATES.
  `gameapi::requirement_shortfalls` replicates this and self-checks against the game's verdict; the bag's Enter says
  "requirements not met, Physique 391 of 392". **Losing a requirement later does not unequip**:
  `EquipmentCtrl::AttributesHaveChanged` -> `Sift` per slot clears the `EquipmentInfo` attached byte (+8; hands:
  their own sub-object, item +0x28 / attached +0x30) and sends `SendEquipDetachAction`; the item stays in the slot,
  greyed, contributing nothing, and re-attaches when the requirement is met again. `EquipmentCtrl::IsItemAttached(id)`
  reads the byte for all 14 slots -> `EquipSlot::inactive` -> "<name>, inactive" on the equipment tab. Consequence: a
  temporary stat boost to equip something is undone the moment the boost goes. The tooltip's requirement lines are
  the same text met or not (the itembox colours them by `failsRequirementsColor*`), so the text cannot tell.

### Character sheet (stat tabs of the same window) -- MEDIUM (numbers yes, layout ours)
No single sheet builder exists (`Character::CreateUISummaryText` is the PET summary). Numbers:
`Character::GetTotalCharAttribute(CharAttributeType)` (58 members, dense index; name map needs one live
probe), `GetModifierPoints()` (unspent), `GetPointsSpent(type)`, `GetCharLevel/GetExperiencePoints/
GetNextLevelExperience`, `Player::CalculateDps(float&, unsigned)`, `GetAttackSpeed/GetRunSpeed/GetSpellCastSpeed`,
`CombatManager::DesignerCalculateOffensiveAbility/DefensiveAbility/CriticalChance`. Resistances/armor by
ENUMERATION: `CombatAttributeAccumulator acc` (exported ctor/dtor, size unknown -- allocate 512 zeroed bytes);
`Character::GetAllDefenseAttributes(acc)`; `acc.GetDefense()` -> `vector<CombatAttribute*>`; per attribute
vtable `+0x10` GetType, `+0x50` GetTotalDefense(float&, float&), `+0x58` GetTotalAbsorption (type 0x27 =
armor); localization tag at attribute `+0x58` (`char const*`). Class name `Player::GetClassNameA()` (u16 by
value), `GetPlayerName()` (raw u16 pointer). Act: `IncrementBaseStrengthConfigCmd(playerId)` etc. +
`Execute()`, `ResetAttributePointsConfigCmd`. Labels: hand-authored row order + `LocalizeWithoutParams`.

### Skills (`+0x3fc20`) -- MEDIUM
Controls: `+0x140` skillWheel, `+0x1568/+0x1918/+0x1cc8` mastery tabs, `+0x2108` close, `+0x2440` title;
skill icons = vector at `+0x128/+0x130`, stride 0x20, 0x51 entries (`skillCtrlPane1..80` + masterySelect).
Model: `Character::GetSkillManager()` (= Character `+0x850`); `GetSkillList()` -> `vector<Skill*>`,
`GetUISkillList()` ids, `GetSkillMasteries/Active/Allowed`, `Skill_Mastery::GetEnumeration()`,
`Character::GetSkillPoints()`, `GetNumMasteryPoints`, `GetCurrentSkillReclamationCost`; per skill (Skills are
Engine Objects: `GetObjectName` = record path) `GetSkillLevel/GetMaxLevel/GetUltimateLevel/GetMasteryLevelRequirement/
IsLocked/GetModifiers/GetSecondarySkills/GetCooldownRemaining/GetManaCost`, names `Skill::CreateUISkillName(bool)`
(u16 by value), `GetDisplayNameTag()`. Full tooltip: static `GameEngine::GenerateUISkillText(skill, out,
SkillReasons const*, bool noRequirements, bool reclaimMode, int reclaimCost, GameTextClass 0x31, true)`
(call sites exe+0x2425c4, exe+0x242b92, both passing the icon's stored reasons at button+0x4c0). A null SkillReasons
or noRequirements skips the points/requirements block; a ZEROED one means "nothing blocks" and prints "press to add
unused skill points" (the bug fixed 2026-09-28). The window fills it in exe+0x2492b0; the byte map is at
`gameapi_skills.cpp fill_skill_reasons`. The quickbar rollover uses `GenerateUISkillInfo` instead (no points text),
and the quickbar picker's filter is exe+0x1e7860 (`gameapi::hotbar_assignable`). The `int` is NOT a level override (corrected 2026-09-15): the exe passes
`SkillManager::GetCurrentSkillReclamationCost()` and the builder only prints it (Game.dll+0x2d0575, the reclaim line);
the text is always for the skill's CURRENT level (+ the next), so a tooltip "at level N" needs the skill put there --
`gameapi::skill_tooltip_at` wraps the call in the game's own `IncrementSkillLevel(n)` / `DecrementSkillLevel(n)` pair
(dev route `/masteries`, `tools/gen_masteries_doc.py` -> `docs/masteries.md`). Act (the exe's click handler exe+0x2483d5..): learn =
`Skill::IncrementSkillLevel(1)` (vtable `+0x48`) + `Character::SubtractSkillPoint()`; refund =
`DecrementSkillLevel(1)` (`+0x58`) + `SkillManager::UseReclamationPoints(1)`; or `SkillManager::IncrementSkill(id,
levels)` + `RecalculateSkills()`. Tree ORDER is data: `records/ui/skills/skills_mastertable.dbr` ->
`classNN/classtable.dbr` (`tabSkillButtons`) -> `skillNN.dbr` `skillName` path, joined with
`SkillManager::FindSkillId(path)` (`tools/arz.py`).

### Vendor (`+0x2b538`, faction vendor `+0x2e188`) -- MEDIUM
Tabs `+0x550/+0x888/+0xbc0/+0xef8/+0x1230`, grid `+0x2410`, search box `+0x2708`, close `+0x1b50`. Stock:
`GameEngine::GetMarketInventorySack(marketId, Market_TypeEnum)` (an ordinary `InventorySack`; enum values
unknown -- probe 0..N for non-null; market id inferred = merchant NPC object id; map at GameEngine `+0x40e0`),
`GetMarketItemStatus`, `GetMarketIsItemAffordable`; price text `CreateUIPlayerBuyText(marketId, itemId, out)` /
`CreateUIPlayerSellText`. Buy: `GameEngine::PlayerPurchaseRequest(marketId, itemId)`. Sell: only via
`CursorHandlerItemMove::SetMarketId + SetId + PrimaryMarketActivate()`.

### Caravan / stash (`+0x4fd08`) -- MEDIUM
Six tab buttons `+0xf58..+0xff8`, sub-windows `+0x13c8` stash, `+0x13d0` transfer, `+0x13d8` relic, `+0x13e0`
material, search `+0x1738`, close `+0x168`. `Player::GetPrivateStash`, `GameEngine::GetPlayerTransfer()` ->
`vector<InventorySack*>`, `SetTransferOpen`; moves via `QuickDropInStash/QuickDropInTransfer`.

### Achievements (`+0x7d150`) -- EASY-TO-MEDIUM
`+0x98` groupsList, `+0x7a8` achievementList, `+0x698` totalUnlocked, close `+0x220`.
`GameEngine::GetAchievementManager()` (reference) -> `GetGroups()` -> `vector<Group*>` (Group is opaque: one
struct to measure); `Achievement::GetTitle/GetDescription`, `IsUnlocked` (`byte[+0xc9]`), `GetHidden` (`+0xc8`).

### Party (`+0x4b540`) -- EASY, multiplayer only
`GameEngine::GetPartyManager()` -> `GetPartyMembers`, `IsPartyLeader`; `PlayerManagerClient::GetPlayerName/
GetPlayerLevel/GetPlayerPing(id)` (u16 by value). Trade (`+0x29cc8`): `GameEngine::GetTradeManager()` ->
`GetMyTradeState/GetHisTradeState` -> `TradeState::GetInventorySack/GetGoldAmount/GetFinalized`.

### Loot filter (`+0xab410`) -- MAPPED 2026-08-29, see `docs/loot-filter.md`
Superseded: the window keeps a `std::map<CheckBox*, LootFilterOption>` at `+0xd58` (41 boxes here; option 39 is
expansion-3 only), each box a TextButton with the checked byte at `+0x282` and its caption tag at `+0x338`, so
every option IS labelled from the window. Model: 42-bit bitset at Player `+0x4c00` (defaults at `+0x4c20`, no
range check), `Item::PassLootFilter` = Quality AND Type AND (Damage|Character if any on). Details:
`docs/re_lootfilter_exe.md`, `docs/re_lootfilter_gamedll.md`.

### Stations -- MEDIUM-HARD (enchanter `+0x30dd8`: tabs Recover/Dismantle/Convert/Reroll `+0xa38/+0x1af8/+0x2d98/+0x5970`,
tab buttons `+0x8c08..+0x95b0`; crafting `+0x3aa80`; transmuter `+0x85378`; altar `+0x87628`; ascension `+0x8baa8`)
Model: `NpcCrafter::GetRecipes`, `GameEngine::GetPlayerFormulas`, `ItemArtifactFormula::GetReagent1..6Id/
Count/DisplayName`, `GetCreationCost`, `ControllerCharacter::SendCreateArtifactCmd`; enchanter
`MainPlayerCanUseDismantle/Reroll/Convert`, `SendEnchanterDismantleCmd/RecoveryCmd/TinkerCmd`; transmutes
`GameEngine::GetPlayerTransmutes()`, `SendTransmuteItemsCmd`; altar `GetAltarInclusiveRecipes`,
`SendAltarOfferCmd`, `AscendantAltarFormula::GetReagentText`. Opened by `GameEngine::Display*Window(npcId)`.
**Crafting (`+0x3aa80`) is MAPPED 2026-08-29 -- see `docs/re_crafting_exe.md`** (ctor exe+0x269e80, vt
exe+0x31cf38, size 0x51a0; the crafter npc id at `window+0x9c`, the current tab category at `+0x19a8`, and one
big sub-object, the crafting panel at `window+0x1e40`, holding the search box, the recipe list box
(`panel+0x2fa0`, rows at `panel+0x3070`, `row+0x64` = the formula id), the 7 reagent slots + the result slot,
the cost line and Combine (`panel+0x1ea8`, enabled iff `panel+0x2129 == 0`). Selected recipe =
`panel+0x60`. Row label = `GetMaximumCraftable` + the result's rarity colour; reagent "0/2" =
`GetReagentNCount` / `GetReagentNQuantityForFormula`.) Note the crafting AND enchanter windows share the
record `records/ui/inventor/inventor_mastertable.dbr`; there is no `hudCraftingWindow` field.
**The enchanter (`+0x30dd8`) is the Inventor and is MAPPED + BUILT 2026-08-30 -- see `docs/inventor.md` /
`docs/re_inventor_exe.md`** (tabs Salvage `+0xa38` / Dismantle `+0x1af8` / Convert `+0x2d98` / Reroll `+0x5970`; the
Convert/Reroll panels are expansion-only and unmapped). The transmuter `+0x85378` is the Illusionist (item
illusions, `tagTransmute*`), not an Inventor service.

### Devotion (`+0x813a0`) -- MAPPED 2026-08-27, see `docs/devotion.md`
Superseded: the exe DOES build a full object graph (constellations at `window+0xa8/+0xb0`, each with its Star
objects carrying skill id, bound host id, link indices and eligibility flags), clicks apply immediately (no
commit path; exe+0x18c0a0 is the Undo button), and celestial powers are bound through a picker owned by this
window via `Skill::SetAutocastSkill` + `SetDevotionParent`. Details: `docs/re_devotion_{data,gamedll,exe}.md`.

### Minimap (`+0x42260`) -- HARD (raster); riftgate list maybe EASY
`+0xb08` aerialMap, `+0x7940` riftGateMap sub-object (the only window overriding HandleKeyEvent,
exe+0x1f4ea0). The riftgate destination list is unexamined.

## Readouts and corrections from the implementation pass (2026-08-22, evening)
Confirmed in disassembly by two further passes and, where marked, live through the dev loop.
- **Skills window**: `+0x128/+0x130` is a vector of record-path strings (stride 0x20, 0x51 entries:
  `skillCtrlPane1..80`, then `masterySelectWindow` at 0x50), not controls. Panes: `+0x100` (tab 0), `+0x108`
  (tab 1); `+0x2630` current tab; `+0x98` player id; `+0x1528` tab registry with buttons `+0x1568/+0x1918/+0x1cc8`
  = tab 0 / tab 1 / Devotion (captions at button+0x358). **`SkillsWindow::SetPane = exe+0x27c580(window, tab,
  paneIndex)`** (paneIndex = mastery enumeration, 0x50 = the class-selection pane) is what clicking a mastery
  does; nothing is granted until the mastery skill takes a point (`Skill::IncrementSkillLevel(1)` vtable +0x48
  after `ReleasePets` +0x80, then `Character::SubtractSkillPoint`); `Undo Class Selection` = SetPane(tab, 0x50),
  offered while the mastery skill's level is 0. Mastery enumeration N <-> `records/ui/skills/class{N+1:02}/
  classtable.dbr` <-> `records/skills/playerclass{N+1:02}/_classtraining_class{N+1:02}.dbr`; the six choices are
  `tagSkillClassName01..06` / `tagSkillClassDescription01..06`. First mastery allowed at level 1, second at 10
  (`masteryIncrementLevel` in the pc records). The window's only dialog is `tagConfirmSkillChanges` (party 0x16).
  The HUD skills button (`+0x9db0`) is disabled at level 1 (verified live: N shows the "Using Skills" tip instead).
- **Undo buttons (seen live 2026-08-27; the skills one is modelled)**: the mastery pane has an **Undo Points** button under
  Undo Class Selection (`records/ui/skills/classcommon/skills_classpanelconfiguration.dbr` `undoButton` ->
  `skills_buttonundopointallocation.dbr`, `tagSkillUndoPoints`; loader string at exe+0x24539a), the devotion
  window has **Undo** (`devotion_mastertable.dbr` `undoButton`, top-left next to the tabs) and the character
  sheet has an attribute undo (`charinfo_mastertable_tab1.dbr` `tab1AttributeChangeUndoButton`). All three revert
  the points spent since the window opened -- spends apply immediately, the window just holds the save.
  The pane is a heap `UISkillPane` (0x1ea8, vtable exe+0x31bd18) at skills window +0x100 / +0x108 per tab (the
  class-selection pane, 0x3d0 / vtable exe+0x31a1d8, sits in the same slot): +0x80 its registry, +0x820 Undo Class
  Selection, +0xbd0 Undo Points (enabled iff +0x1e45 "pending changes"), +0x1e4c reclaim mode (the REAL flag;
  window +0x1f4c read before is the Devotion tab button's state byte), +0x68/+0x70 the icon entries (stride 0x78:
  +0 control, +0x10 pending delta, +0x50 skill id). `exe_ui::skills_press_skill` presses an icon through the
  registry = the game's own learn/reclaim click, which records the delta Undo Points reverts.
- **Skills window, reclaim + requirements (2026-08-24, verified live)**: the icon click handler exe+0x248380
  branches on the reclaim flag at **skills window +0x1f4c** (its `this` is the embedded controller at
  window+0x130, so the handler reads `[this+0x1e1c]`): set -> reclaim (`DecrementSkillLevel` +
  `SkillManager::UseReclamationPoints`), clear -> learn. A spirit guide (`NpcSkillReallocator`) sets it via
  `GameEngine::DisplaySkillReallocationWindow` (forwards through `[GameEngine+0x19b0]` vtable +0x60; the only
  path that flips window +0x1f49/+0x1f4c 0->1 and writes a controller ptr at +0x2634). The requirement gate is
  the SkillReasons builder exe+0x2492b0 (byte0 no points, byte1 `GetMasteryLevel < GetMasteryLevelRequirement`,
  byte2 modifier base not enabled, byte4 mastery-slot, byte8 reclaim cost > money via
  `GetCurrentSkillReclamationCost`). Modifier -> base uses the base's `Skill::GetModifiers()` reversed
  (`GetModifiedSkillId` reads 0 for tree modifiers). `dev_open_skill_reclaim` / route `/reclaim` open reclaim
  mode without a guide.
- **Character sheet** (pane 1 Update exe+0x13d870): CharAttributeType 1 Physique, 2 Cunning, 3 Spirit, 4 health
  max, 5 energy max (labels `tagCharAttributeName02/01/03/04/05`); OA/DA = `Character::DesignerCalculate
  OffensiveAbility/DefensiveAbility(float)`; resistances by defense type -- Fire 6, Cold 5, Lightning 8,
  Poison 7, Piercing 4, Bleeding 15, Vitality 9, Aether 11, Physical 2, Chaos 10 (labels `tagStatsResistance01..10`
  in that order) through `CombatAttributeAccumulator::GetTotalDefenseType`; the "+" buttons =
  `ControllerCharacter::IncrementCharacterStrength/Dexterity/Intelligence()` + `IncrementCharacterLife((int)
  Character::Get{Strength,Dexterity,Intelligence}LifeIncrement())` (+ `IncrementCharacterMana()` for Spirit),
  gated on `Character::GetModifierPoints()`. Verified live: Physique 55 -> 63 after one point.
- **Items**: `ItemSource` 1 = bag, 2 = private stash, 3 = transfer, 4 = trade, 5 = station slot, 7 = caravan
  reagents; equipment slots are addressed by `SetEquipId`, not a source. Bag right-click = `PlayerInventoryCtrl::
  UseItem(id, 1)` for consumables, else `EquipmentCtrl::SmartAutoInsert(id, displaced&, false)` + `PlayerInventoryCtrl::
  RemoveItem(id, true)` + `AddItem(displaced, true, false)` (verified live). A two-hander over weapon + off-hand: SmartAutoInsert
  returns true with both listed and places NOTHING -- the caller swaps (`gameapi::swap_in_two_hander`, devlog 2026-10-05). Unequip = `AddItem(id, true, false)`
  then `EquipmentCtrl::RemoveItem(id)` (RemoveItem alone orphans the item -- verified live). `PlaceItem(loc, id,
  suppressSound, alt)` returns the displaced id and sends the attach/detach commands itself. The live cursor
  handler is `[[main_obj+0x90]+0x108]`. `mem::map<unsigned, Rect>` nodes: key +0x1c, Rect +0x20 (pixels, 32 per
  cell); the market map (pointer values) has key +0x20, value +0x28.
- **Hot slots** (47 per weapon config): bar 1 = 0..9, left mouse 10 (config A) / 11 (B), right mouse 12/13,
  bar 2 = 14..23, health potion 24, energy potion 25, bar 3 = 26..35, bar 4 = 36..45, evade 46; the HUD's bar
  page at `InGameUI+0x72f0`. `SetHotSlot` deep-copies the option; `SetPrimarySlot/SetSecondarySlot(option*)`
  set the mouse slots (`SetPrimarySkillId` did nothing live). `HotSlotOption` vtable: +0x28 GetCooldownRemaining,
  +0x30 GetStatus, +0x78 GetRolloverText, +0x80 GetDisplayName, +0xa0 GetSkillId; object +0x08 Player*, +0x10
  SLOT_TYPE (0 skill, 2 health potion, 3 energy potion, 4 scroll, 5 evade), +0x18 skill id. Status: 0 none, 1 ready,
  2 cooldown, 3 cooldown with charges, 4 no energy, 5 none left, 6 one-shot not ready, 7 wrong weapon, 8 wrong stance.
- **Pickup**: the Pickup key (exe+0x21c6c0) = nearest `Item` within 10 units passing IsOfInterest / visible /
  ownership / loot filter, then `ControllerPlayer::ItemAction(false, false, coords, item)` (walks to it);
  `ControllerCharacter::PickupItem(id)` is the id-only command with no range check. Calling
  `InGameUI::HandleKeyAction(ui, 0x37, true, false, false)` (exe+0x211980) runs the key's own path (verified
  live; an empty matching slot auto-equips). Auto-pickup is in `Player::UpdateSelf` (potions, gold, quest items ...).
- **Stack split** (`+0x83ed8`): opens on Ctrl+click of a stack (`exe+0x21ad70(ui, itemId, rect)`); `+0xb0` item
  id, `+0x1438` count, `+0x1268` edit box (text `+0x12d8`), ok `+0x160`, cancel `+0x510`, registry `+0x118`,
  visible `+0x68`; OK creates the split stack ON THE CURSOR. The window is not modelled; its OK handler
  (exe+0x1dcb70, read 2026-08-26) is replicated by `gameapi::split_stack`: byte-copy the item's inline
  `ItemReplicaInfo` (Item+0x538, 0x190 bytes; +0 object id -> 0 = allocate, +0x178 = count),
  `Item::CreateItem(info)` (static export) -> the clone, `ControllerCharacter::SendAddItemToInventory(clone)`
  (all the cursor handler's `CreateAndStackIds` does), `Item::SetStackSize(src, orig-N)` +
  `SendUpdateItemStack(src, orig-N)`. The clone is deliberately NOT `PlayerInventoryCtrl::AddItem`ed: the grid
  add merges a stackable back into its source stack and destroys it (lost 2 items live before this was
  understood). The vendor's Ctrl+Enter sells the clone (`sell_split`: PlayerSaleRequest + SendRemoveItemFromInventory)
  3 ticks later; a refused sale `unsplit_stack`s it (the grid add's merge is the recovery). Verified: 3 -> 1 in
  the bag, +80 bits, the merchant's buyback lists "Serrated Spike (2)".
- **Registries**: quest reward `+0x738` (Accept `+0x388`); shrine `+0x11b0` (Offer `+0x11f8`, Cancel `+0x15a8`,
  Close `+0x1958`). Framework-B vtable `+0xf0` is NOT always OnControlEvent (the stack window uses +0xe8..+0x100
  for setters and presses arrive at the window+0x90 listener on event code 0).
- **Dev XP**: `GameEngine::CharacterExperienceOutbound(engine, playerId, xp)` is what the script action
  `GiveExperience` calls (`/cheat?xp=`; verified: level 1 -> 2 with 69 XP). `SkillManager::AddExperience` is
  per-skill experience, not the character's.
- `GameEngine::GetObjectives()` is empty in the campaign; the objective tracker is the tracked quests'
  in-progress tasks' open objectives, then the next available task's.

## Dead record fields
`hudTeleportWindow`, `hudTutorialWindow`, `hudSlotConfigWindow`, `hudHealthPotionSlot`, `hudManaPotionSlot`,
`hudScoreText` appear in `hud_mastertable.dbr` but nowhere in the exe.
