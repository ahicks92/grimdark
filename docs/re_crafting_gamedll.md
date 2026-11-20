# Crafting (the blacksmith / Inventor window) at the Game.dll level (static RE 2026-08-29, offline only)

All RVAs are Game.dll v1.3.0.8 unless prefixed `exe+` (the unpacked `build/GrimDawn.unpacked.bin`).
Names in `GAME::X::Y` form are **exported** unless a line says otherwise. "Verified" = read in the
disassembly / the records; "inferred" = deduced from surrounding code, the records or the window's captions.
Nothing here has been exercised in a running game.
Companion: the exe-side crafting window (its widgets, tabs, focus) is a separate document; this file only
peeks into the exe where the call arguments or the intersection rules live.

## 0. The one-paragraph model

A **formula** ("blueprint") is an `ItemArtifactFormula` — a real `Item` subclass, `Item_Type` **12**. Its
record (`database/templates/itemartifactformula.tpl`, 362 of them, mostly under
`records/items/crafting/blueprints/**`) names a **result** (`artifactName` / `forcedRandomArtifactName`),
**seven reagent slots** (`reagentBase` + `reagent1..6`, each a record-path list plus a quantity) and a
**cost** (`artifactCreationCost`). The player's *known* formulas are a map of live formula-item ids on the
GameEngine. A `NpcCrafter` (the blacksmith) contributes its own `defaultRecipes` and decides whether the
player's own formulas are usable at all (`restrictRecipes`). Crafting = fill a
`CreateArtifactConfigInfo` (result replica + up to 7 `{record, count}` reagent entries + cost + one rolled
"Forgemaster" affix) and hand it to `ControllerCharacter::SendCreateArtifactCmd`, whose `Execute` **takes,
charges and gives with no validation of its own** — every check is on the caller's side.

---

## 1. The formula object

### 1.1 Class and record

`GAME::ItemArtifactFormula` : `Item`. classInfo `0xa2b2a0`, primary vtable `0x73b628`,
`GetItemType()` = **12** (`Item_Type::Formula`, verified: `0x189770` is `mov eax,0xc; ret`, COMDAT-folded
with `WeaponArmor_Offhand::GetWeaponType`). Subclasses that share the layout: `ItemSetFormula`,
`ItemRandomSetFormula`, `ItemRerollFormula`, `ItemAscensionFormula`, `AscendantAltarFormula`.

Union of the record fields across all 362 `itemartifactformula.tpl` records (`tools/arz.py`), with the
Game.dll offset `ItemArtifactFormula::Load` (0x319080) writes them to (all verified):

| record field | offset | type | notes |
|---|---|---|---|
| `artifactName` | +0x1068 | `std::string` | the result: usually a **loot table** (`records/items/loottables/...`) |
| `forcedRandomArtifactName` | +0x10b8 | `std::string` | 124/362 records; a concrete item record. **Wins over `artifactName`** |
| `forcedRelicCompletion` | +0x10d8 | bool | 60/362; `GetForceRelicComplete()` 0x394c0 |
| `artifactFormulaBitmapName` | +0x10e8 | `std::string` | the blueprint icon; texture cached at +0x10e0 |
| `artifactCreationCost` | +0x1088 (int) **and** +0x1090 (equation) | | see §2.3 |
| `artifactCreateQuantity` | +0xd10 | int | `GetArtifactCreateQuantity()` 0x31a690 |
| `reagentBaseQuantity` | +0xd14 | int | `GetReagentBaseQuantityForFormula()` 0x31a6a0 |
| `reagentBaseBaseName` | +0xc68 | `mem::vector<std::string>` | loaded only if the quantity != 0 |
| `reagent1Quantity` | +0xd18 | `mem::vector<int>` | `GetReagent1QuantityForFormula()` returns `*begin`, 0 if empty |
| `reagent1BaseName` | +0xc80 | `mem::vector<std::string>` | loaded only if the quantity vector is non-empty |
| `reagent2..6Quantity` | +0xd30, +0xd48, +0xd60, +0xd78, +0xd90 | | stride **0x18** |
| `reagent2..6BaseName` | +0xc98, +0xcb0, +0xcc8, +0xce0, +0xcf8 | | stride **0x18** |
| `itemLevel`, `levelRequirement`, `itemClassification`, `itemCost`, `description`, `itemText`, `soulbound` | | | plain `Item` fields (the blueprint item itself, not the result) |

**Every reagent slot is a LIST of acceptable records**, not one record: the field is read with the
LoadTable's string-array getter (`[vt+0xa8]`), and both the count and the take resolve over the whole list
(§2.2, §3.2). In the shipped data each list is length 1, but the code does not assume it.

**`reagentBase` is the PRIMARY reagent** — the big left-hand slot of the window (verified end to end: the
exe's reagent panel fills that slot from `GetReagentBase*` before the 2x3 grid, exe+0x19a9d6..0x19aa40).
Worked example, `records/items/crafting/blueprints/weapon/craft_weapon_master_1hsword01.dbr`:
`reagentBaseBaseName = records/items/questitems/scrapmetal.dbr`, `reagentBaseQuantity = 2` →
the live "0/2"; `reagent1BaseName = records/items/materia/compa_chippedclaw.dbr`, `reagent1Quantity = 1` →
"0/1"; `artifactCreationCost = 3000` → "Cost: 3,000".

### 1.2 Lazy per-slot runtime state

`LoadReagentBase` (0x31d410) / `LoadReagent1..6` (0x31d830 + 0x280*i) create a **template item object** from
the slot's first record and cache, per slot i (0 = base, 1..6):

```
+0xda8 + 0x20*i   std::basic_string<u16>  the reagent's NAME      (item vt+0x340 Item::GetGameDescription)
+0xe88 + 0x20*i   std::basic_string<u16>  its HINT text          (item vt+0x478 Item::GetHintTag, SimpleStringFormat'd)
+0xf68 + 0x20*i   std::string             its bitmap name        (item vt+0x430 Item::GetBitmapName)
+0x1048 + 4*i     u32                     the template item's OBJECT ID
+0x1110           mem::vector<Item*>      the template objects, owned by the formula
```
`GetReagentNDisplayName(bool)` returns **+0xda8** for `false` (the name — what the exe passes) and
**+0xe88** for `true` (the hint). Each getter force-loads the slot on first use, so they are safe to call
cold. `LoadReagentBase` also forces `reagentBaseQuantity = 1` when the template's `GetItemType() != 19`
(`QuestItem`) — i.e. only quest-item reagents (scrap metal, dynamite, ...) may need more than one of the
base slot (verified; not obviously intentional, but it is what runs).

**`GetReagentNId()` is not a record path** — it is the object id of that private template item. It is only
useful as "is this slot used at all" (`!= 0`, which is exactly the exe's test) and as a handle for the
item's own tooltip/icon calls.

### 1.3 The result item

`LoadArtifact` (0x31d6b0, virtual +0x6a8) builds an `ItemReplicaInfo` whose record is
**`forcedRandomArtifactName` if non-empty, else `artifactName`**, calls `Item::CreateItem` (0x30fd00) and
caches the object at **+0x1108**.
- `GetArtifact()` (0x31e9a0) = force-load, then `Object::GetObjectId(+0x1108)` → **an item object id**.
- `GetArtifactInfo(ItemReplicaInfo&)` (0x31e730) — the first dword of the filled struct is that same id.
- `IsValidArtifact()` (0x394d0) = `+0x1108 != 0`.
- `IsBluePrintValid()` (0x31e9e0, virtual +0x6a0) = at least one reagent list is non-empty **or**
  `reagentBaseQuantity != 0`, **and** the artifact loads non-null.

The cached artifact is an **unrolled template**, which is why its tooltip reads as ranges. See §5.1.

## 2. Ownership: who knows which formulas

### 2.1 The player's known formulas

```
public: class mem::map<unsigned int,unsigned int> const& GAME::GameEngine::GetPlayerFormulas(void) const   0x2c76f0
```
= `lea rax,[rcx+0x36d60]; ret` — **gGameEngine+0x36d60**, an MSVC `_Tree` (`_Isnil` at node+0x19,
u32 key at node+0x1c, u32 value at node+0x20).

**The key is the object id of a live `ItemArtifactFormula` instance** the engine keeps for the character;
the value is whatever `AddItemToFormulas`'s 2nd argument was (0 on the learn path). To enumerate the
player's blueprints: walk the map, `ObjectManager::GetObject(key)`, is-a `ItemArtifactFormula::classInfo`.
That is exactly what the window does (exe+0x197c50).

Identity is **by record path**, not by id: `GameEngine::IsItemInFormulas(std::string const& record)`
(0x2c7b90) linearly resolves every key and compares `Object::GetObjectName()`, returning the existing key
(0 = not known). Related exports: `AddItemToFormulas(u32 itemId, u32)` 0x2c7700, `RemoveItemFromFormulas`
0x2c78e0/0x2c7a90, `ClearFormulas` 0x2c7d00, `BackupFormulas` 0x2c7e10, `SaveFormulas` 0x2c6120,
`LoadPlayerFormulas` 0x2c80a0, `StreamPlayerFormulas` 0x2c8700, `LogPlayerFormulas` 0x2db470,
`PlayLearnFormulaSound` 0x2b32a0. Formulas are saved per character (`SaveFormulas` is called on every add).

**Learning a blueprint** = `ItemArtifactFormula::Use(Character*)` (0x31ea90, virtual): only for the local
main player, only when `IsItemInFormulas(record) == 0`; it clones the formula item and calls
`AddItemToFormulas(cloneId, 0)`, then consumes the blueprint. `AllowUse(bool& out)` (0x31eee0) returns
`IsBluePrintValid() && !IsItemInFormulas(record)` — that is the "[Right-Click to Learn]"
(`tagCraftingLearn`) gate. There is **no level or faction gate on learning or crafting** anywhere in this
path; `levelRequirement` on the record is the plain `Item` pick-up/use requirement and the crafted item's
own "Required Player Level" comes from the result item, not the formula.

### 2.2 The crafter's own recipes

`GAME::NpcCrafter` : `Npc`. classInfo `0xa28240`. `NpcCrafter::Load` (0x3a8470) reads (verified, with the
record fields of `records/creatures/npcs/merchants/blacksmitha01.dbr` = Angrim / `blacksmitha02.dbr` = Duncan):

```
+0x4b48  std::string               enhancementTable    GetEnhancementTableName()  0x3a8410 (folded w/ NpcMerchant::GetMarketName)
+0x4b68  mem::vector<std::string>  enhancementTag      GetEnhancementTags()       0x3a8420
+0x4b80  std::string               crafterBitmap       GetCrafterBitmapName()     0x3a8430
+0x4ba0  mem::vector<std::string>  defaultRecipes      GetRecipes()               0x3a8440
+0x4bb8  bool                      restrictRecipes     RestrictsRecipes()         0x3a8450
+0x4bb9  bool                      awakenedRecipes     LoadAwakenedRecipes()      0x3a8460
```
`GetRecipes()` returns **record paths** of `itemartifactformula.tpl` records (Angrim: four
`craft_weapon_master_*`; Duncan: four `craft_weapon_apprentice_*`). Neither blacksmith sets
`restrictRecipes` or `awakenedRecipes` (the fields are absent → false).

Other members: `NpcCrafter::IsOfInterest()` folds to `mov al,1; ret` (crafters always qualify for the mod's
object/interactable groups); `OnPlayerInteract(id,bool,bool)` (0x3a8580) checks the interactor is the local
main player and calls the UI outbound interface's `[vt+0x78]` — which is exactly what
`GameEngine::DisplayCrafterWindow(u32 npcId)` (0x2c3650) is: `jmp [ [gGameEngine+0x19b0] -> vt+0x78 ]`.
So `DisplayCrafterWindow(crafterId)` is the whole "open the Inventor window on this blacksmith" call.

### 2.3 How the two are intersected (exe+0x19d900 open, exe+0x19a060 refresh — verified)

On open, for each path in `GetRecipes()`: `ObjectManager::CreateObjectFromFile` → keep it only if
`obj->vt[0x6a0]()` (`IsBluePrintValid`) is true, then insert `{objectId -> 1}` into the window's own map at
**window+0x3178**; otherwise `DestroyObjectEx` it. `RestrictsRecipes()` → window+0x335c,
`LoadAwakenedRecipes()` → window+0x335d.

On refresh the displayed list is built by calling the same "append a map of formula ids" helper
(exe+0x197c50) with, in order:

```
window+0x3178                                 always   (the crafter's own defaultRecipes)
GameEngine::GetAwakenedRecipes()              if window+0x335d (awakenedRecipes)     [mem::map<u32,u32>, 0x2d77d0]
GameEngine::GetPlayerFormulas()               if NOT window+0x335c (restrictRecipes)
window+0x3168                                 if NOT window+0x335c
```

So: **`restrictRecipes = true` means "this crafter makes only its own list"; false (both blacksmiths) means
"its own list plus every blueprint the player has learned".** `awakenedRecipes` additionally folds in the
engine-wide awakened-recipe map.

## 3. Per formula: everything a row needs

### 3.1 The row itself (exe+0x197c50 + exe+0x19a520, verified)

```
formula   = ObjectManager::GetObject(mapKey)            is-a ItemArtifactFormula
result    = ObjectManager::GetObject(formula->GetArtifact())    is-a Item      (skip the row if not)
name      = result->vt[0x340] (Item::GetGameDescription)        u16 string
group     = result->vt[0x550] (Item::GetItemType)               stored per row -> the "AXES"/"BELTS" header
N         = formula->vt[0x690] (GetMaximumCraftable(character))
colour    = GameEngine::GetItemColorText(out, Item::GetDropClassification(result))
row text  = "[" + N + "] " + colour + name
```
That is the live "[N] Name" with the green/yellow/cyan colouring, exactly. Note the colour comes from
`Item::GetDropClassification`, not `GetItemClassification`.

The **group headers** are localized `tagCrafting*` tags, created by the window in a fixed order
(exe+0x1986f0..0x198d00). The full vocabulary (from `tools/arc_unpack.py`):
`EMPOWERED RELICS` / `TRANSCENDENT RELICS` / `MYTHICAL RELICS` (`tagCraftingRelicsTier01..03`),
`AXES`, `BLUNT`, `SWORDS`, `IMPLEMENTS`, `TWO-HANDED MELEE`, `SPEARS`, `STAVES`,
`ONE-HANDED RANGED`, `TWO-HANDED RANGED`, `BELTS`, `HELMS`, `CHEST`, `SHOULDERS`, `LEGS`, `HANDS`, `FEET`,
`FOCI`, `SHIELDS`, `COMPONENTS`, `ACCESSORIES`, `CONSUMABLES`, `MATERIALS`, plus `tagCraftingRunes`.
The result item's `GetItemType()` (and, for weapons, `Weapon::GetWeaponType()`) is what a row is bucketed
by; the five top tabs are a further exe-side grouping of those buckets and are **not** modelled here.

### 3.2 "have / need"

**Need** = `GetReagentBaseQuantityForFormula()` / `GetReagentNQuantityForFormula()`.

**Have** = `GetReagentBaseCount(Character const&, bool)` (0x31b060) / `GetReagentNCount` (0x31b260 + 0x200*i):
for **every** record in the slot's list it calls the character's virtual **`[vt+0x5f0]` =
`Player::GetItemCountInStashes`** (0x3cfe10) with all four trailing bools `true` and **sums** the results.
The `bool` argument of `GetReagentNCount` is **ignored** in this build (verified: never read).

`Player::GetItemCountInStashes(record, prefixes, suffixes, outIds, b1..b4)` (0x3cfe10, exported, virtual
`Player` vtable +0x5f0; base `Character::GetItemCountInStashes` too) = (verified)

```
Character::GetItemCount(record, ...)                      0x63940   the character's Inventory (bags);
                                                                    EquipManager (equipped) is SKIPPED
                                                                    because the 7th bool is true
+ if this == GameEngine::GetMainPlayer():
    GameEngine::GetReagentItemCount(record, outIds)        0x2cef40  the Materials / reagent tab
  + for each sack in Player+0x4820 (mem::vector<InventorySack*>):
      InventorySack::GetItemCount(record, ...)             0x30e5b0  the personal stash tabs
  + GameEngine::GetTransferItemCount(record, ...)          0x2c4320  the shared transfer stash
```
So the window's "have" counts bags + materials tab + personal stash + shared stash, and **not** equipped
items. (`GameEngine::GetPlayerReagents()` 0x2c63b0 = gGameEngine+0x36d80, a
`mem::map<std::string, ReagentData>` keyed by record path with an item id at node+0x40 — the materials tab.)

The exe's per-slot panel (exe+0x19a9c0) is literally: skip the slot if `GetReagentNId() == 0`;
`need = GetReagentNQuantityForFormula()`; `have = GetReagentNCount(player, true)`;
name = `GetReagentNDisplayName(false)`; the "have/need" caption is `tagCraftingQuantity` = `{%d0}/{%d1}`
(or `tagCraftingLargeQuantity` = `99+/{%d0}`), and the red/normal styling is `have < need`.

### 3.3 "[N] how many can be made" — `GetMaximumCraftable`

```
public: virtual int const GAME::ItemArtifactFormula::GetMaximumCraftable(GAME::Character const*)   0x31a950  (vt +0x690)
```
Verified body:
1. Clear the `mem::vector` at **formula+0x1150** and, for each of the 7 slots that has records, push a
   0x28-byte entry `{ std::basic_string<u16> name (0x20); int have (+0x20); int need (+0x24) }` built from
   `GetReagentNDisplayName(false)`, `GetReagentNCount(character)` and the slot's quantity.
2. **Merge entries with the same name**, summing their `need` (so two slots asking for the same reagent
   count once, correctly).
3. `N = min over entries of (have / need)` (integer division; clamped at 0 if negative).
4. If `GetCreationCost(character) != 0`, also cap: `N = min(N, character.money / cost)` where money is
   `Character+0x16c4`.

The side-effect vector at +0x1150 is not exported, but the same numbers are available slot by slot from
the getters above, so a screen does not need it.

### 3.4 Cost

```
public: unsigned int GAME::ItemArtifactFormula::GetCreationCost(GAME::Character const*) const   0x31d300
```
Verified: stashes `character+0x1760` into `formula+0x1098`, then evaluates the equation object at
`formula+0x1090` (parsed from the record's `artifactCreationCost` **as an equation string** — the load error
message is `"-=- Equation load failure : artifactCostEquation : %s "`), clamps and rounds it; if there is no
equation object it returns the plain int at `formula+0x1088`. `ResolveEquationVariable` (0x31efd0) knows one
variable, **`playerLevel`**, and answers it from `formula+0xb68`. Caption `tagCraftingCost` = "Cost:";
the shipped records all use plain integers (1500..12000 range), so in practice cost = `artifactCreationCost`
in iron bits. `Character::GetMoney` / `Character+0x16c4` is the affordability check the game itself makes.

(`GetRerollCost(u32)` 0x31d360 and `GetReagentNQuantityForReroll(u32)` 0x31a770+ read parallel per-tier
vectors at +0x10a0/+0x10a8 — the Enchanter/reroll tab, not the blacksmith. Not investigated.)

## 4. The craft command

```
public: void GAME::ControllerCharacter::SendCreateArtifactCmd(struct GAME::CreateArtifactConfigInfo const&)  0x1190d0
```
Verified: resolves the controller's AI (`ControllerAI::GetAI`), allocates a **0x2d0-byte**
`CreateArtifactConfigCmd`, writes `cmd+0x08 = controller+0x30` (the owning character's id),
copy-assigns the info into `cmd+0x10`, sets `cmd+0x0c = 1`, and dispatches through the character's
`vt[0x350]` config-command send. Single player executes it immediately and locally.

### 4.1 `CreateArtifactConfigInfo` layout (0x2c0 bytes; offsets relative to the info, verified against both `CreateArtifactConfigCmd::Execute` 0xd5080 and the exe's Combine at exe+0x19e400)

```
+0x000  u32    quantity                       = GetArtifactCreateQuantity()
+0x008  .. +0x188   an embedded ItemReplicaInfo for the RESULT:
        +0x008  u32 new object id             = ObjectManager::CreateObjectID()
        +0x010  std::string  the item record  (resolved from the formula's loot table / forcedRandomArtifactName)
        +0x030, +0x050        prefix / suffix records
        +0x070  u32 random seed               = GameEngine::GetRandomSeed()
        +0x078  std::string  the ENHANCEMENT affix   <- the "Forgemaster" bonus, §5
        +0x098, +0x0b8, +0x0e0, +0x108, +0x128  further strings; +0x0d8, +0x100, +0x104, +0x184, +0x188 ints
+0x198  u32    "consume this exact item id"   (0 for crafting; see below)
+0x1a0  7 x { std::string record (0x20); u32 count; pad }  stride 0x28  -- slot 0 = reagentBase, 1..6 = reagent1..6
        i.e. record at +0x1a0 + 0x28*i, count at +0x1c0 + 0x28*i
+0x2b4  u32    cost                           = GetCreationCost(character)
+0x2b8  bool   forceRelicCompletion           = formula+0x10d8
+0x2b9  bool   give as ARTIFACT               <- the blacksmith path sets this
+0x2ba  bool   give as SET item
+0x2bb  bool   give as REROLL item
+0x2bc  bool   give as ASCENDED item
```
The exe fills the seven reagent records with **`GetReagentBase(character, out)` / `GetReagentN(character, out)`**
(0x31c260, 0x31c4b0 + 0x250*i), which pick the **first record in the slot's list that the character actually
has any of** (`GetItemCountInStashes != 0`), and leave the string empty if none — so a multi-record slot
consumes what you own. Counts come from `GetReagentNQuantityForFormula()`.

### 4.2 What `Execute` does (0xd5080, verified) — and does not

1. `cmd+0x08` → `ObjectManager::GetObject` → must be-a `Player`. (Nothing else is checked.)
2. `Item::CreateItem(replicaInfo)` (0x30fd00) → the new item. If it is an `ItemRelic` and
   `forceRelicCompletion`, call its `vt[0x6a0]`.
3. **Charge**: `player.money(+0x16c4) = max(0, money - cost)`. Clamped at zero, never refused.
4. **Take the reagents**: for each of the 7 entries, `TakeReagents(record, count)` (0xd4f50, private) —
   unless `info+0x198 != 0 && slot0.record.empty()`, in which case it instead does
   `Character::TakeItemFromCharacter(itemId, -1, ...)` on that one specific item (the reroll/upgrade flavour;
   the blacksmith path never uses it).
   `TakeReagents` consumes in this order, each taking only the remainder (verified):
   ```
   Character::TakeItemFromCharacter(record, count, ...)   0x6009c0   bags
   if this is the local main player:
     GameEngine::TakeItemFromReagents(record, rem)        0x2cf220   materials tab
     Player::TakeItemFromPrivateStash(record, rem, ...)   0x3cffb0   personal stash
     GameEngine::TakeItemFromTransfer(record, rem, ...)   0x2c4420   shared stash
   ```
   — the exact mirror of the "have" sources in §3.2.
5. **Give**: whichever of the four flags is set picks a `Player` virtual —
   `+0x580 GiveArtifactToCharacter` (crafting), `+0x5a8 GiveSetItemToCharacter`,
   `+0x5b0 GiveRerollItemToCharacter`, `+0x5b8 GiveAscendedItemToCharacter`. If none is set the item is
   created and never handed over. `Player::GiveArtifactToCharacter` (0x3bfc10) does
   `Inventory::AddItemToInventory(player+0xd80, itemId, 0)` then notifies the controller
   (`ControllerPlayer::GiveItemToPlayer`) — **straight into the bag**, not onto the cursor; the controller
   path is what handles a full bag.
6. If `quantity > 1`, loop creating and giving `quantity - 1` more items (each a fresh `CreateObjectID`).

**Failure paths: there are none inside `Execute`.** Missing reagents → it simply takes as many as exist.
Not enough money → the balance clamps to 0. Full bag → handled downstream by
`ControllerPlayer::GiveItemToPlayer`, not by a refusal. Every guard is on the caller:

- the exe's Combine gate is only `formula->IsValidArtifact()` **and** a non-empty result record
  (else it logs `UIEnchanterArtifactTab: Artifact Formula (%s) contained an invalid artifact`);
- the Combine **button's** enabled state is the window's own affordability/reagent check —
  `tagCraftingButtonInfo` = "This button will be highlighted when all required materials are added".
  `GetMaximumCraftable(player) > 0` is the single call that reproduces it (it folds both the reagent
  minimum and `money / cost`).

**A mod must therefore refuse to send the command itself** — `GetMaximumCraftable(player) >= 1` — or it
will silently burn iron bits and partial reagents.

## 5. The blacksmith bonus ("Forgemaster")

`NpcCrafter::GetEnhancementTableName()` is a record path to a `LootRandomizerTable`
(`lootrandomizertabledynamic.tpl`), e.g.

```
records/items/lootaffixes/crafting/completionbonus_master.dbr      (Angrim)
  randomizerName1 = .../ad05_pierceresist.dbr   weight 100
  randomizerName2 = .../ad06_protection.dbr     weight 100
  randomizerName3 = .../ac05_physique.dbr       weight  35
records/items/lootaffixes/crafting/completionbonus_apprentice.dbr  (Duncan)
  ac04_energyregen (100), ad08_da (100), ac05_physique (35)
```

- **At window open** the exe loads that table and renders **every** entry as rollover text
  (`LootRandomizerTable::GetAllEntries` → `AttributeRange::LoadAffix` → `AttributeRange::CreateText`,
  exe+0x19de02..0x19dfa6), under the crafter's `enhancementTag` lines. Those tags are the visible blurb —
  for Angrim: `tagStoreTitle_SmithA01` = **"Forgemaster"**, then "Practiced Skill",
  "Angrim uses his many years of experience to forge deadly weapons and fortify armor.", "",
  "Crafted Weapons, Armor and Accessories are imbued with one of the following properties:".
  Duncan is "Arcane Blacksmith" / "Arcane Forging". So **yes, the window shows it** — as a text block on the
  crafter's icon panel, not per row.
- **At Combine** the table is re-created and asked for **one** entry (`table->vt[0x30](&out, &level)`,
  exe+0x19ea93), and the resulting affix record path is written into the result's
  `ItemReplicaInfo` **enhancement slot (info+0x78)**. The exe does this unconditionally whenever
  `GetEnhancementTableName()` is non-empty; there is no per-item-class test on the exe side.
- Which classes actually keep it is decided by the affix/item load, not here. The localized text says
  "Weapons, Armor and Accessories", so relics/components/consumables presumably drop it — **inferred, not
  verified.**

### 5.1 The result tooltip (the ranged one)

The row's result is the formula's cached **template** item (`GetArtifact()` → object id → `Item*`). Its
tooltip is the item's own virtual `[vt+0x458]` = **`ItemEquipment::GetUIDisplayText(Character const*,
mem::vector<GameTextLine>&, bool)`** (0x3298c0, exported). Because the template was never rolled, its
attributes render as ranges — that is exactly the live
"Ranger's Ribbon / Rare Medal / +52 Health [52-78] / 8% Pierce Resistance [8-12] / +Random Stat(s) /
Required Player Level: 14". The "+Random Stat(s)" line is `tagCraftingRandom` = `{^E}+Random Stat(s)`,
the window's own line standing in for the unrolled affixes.

The **formula's own** tooltip (the blueprint item in your bag) is
`ItemArtifactFormula::GetUIDisplayText` (0x3199b0, virtual): base header, the record's `description` tag,
the reagent list, and it flips a "already known" cache byte at formula+0x1120 via `IsItemInFormulas`.

## 6. Search and sort

- **Search**: the window builds, per row, a lowercased haystack from the RESULT item's full summary —
  `result->vt[0x458]` (`GetUIDisplayText`) → each `GameTextLine` through
  `LocalizationManager::LocalizerFormatStrip` → `towlower_l` per character (exe+0x197e60..0x197f1c),
  stored as a `mem::vector<u16string>` on the row. So the box matches **name and stats**, which is what
  `tagCraftingSearchBoxInfo` promises ("filter Blueprints based on item names or desired stats").
- **Sort/grouping**: rows are bucketed by the result's `Item::GetItemType()` into the `tagCrafting*` header
  groups of §3.1, in the window's fixed group order. There is no sort field on the formula record.

## 7. Suggested mod surface

```
open      : GameEngine::DisplayCrafterWindow(crafterNpcId)          0x2c3650   (or model the window ourselves)
crafter   : NpcCrafter::GetRecipes()          record paths           -> create + IsBluePrintValid + keep
            NpcCrafter::RestrictsRecipes()    true  => crafter list only
            NpcCrafter::LoadAwakenedRecipes() true  => + GameEngine::GetAwakenedRecipes()
            NpcCrafter::GetEnhancementTags()  the Forgemaster blurb tags (localize each)
            NpcCrafter::GetEnhancementTableName()  -> LootRandomizerTable::GetAllEntries for the bonus list
known     : GameEngine::GetPlayerFormulas()   mem::map<formulaItemId, u32> at gGameEngine+0x36d60
per row   : formula->GetArtifact() -> Item*   name  = Item::GetGameDescription (vt +0x340)
                                              group = Item::GetItemType       (vt +0x550)
                                              colour= GetItemColorText(Item::GetDropClassification(it))
                                              tip   = ItemEquipment::GetUIDisplayText (vt +0x458)
            formula->GetMaximumCraftable(player)   (vt +0x690)  = the "[N]"  AND the craftable gate
            formula->GetCreationCost(player)                     iron bits; compare Character+0x16c4
reagents  : for i in {base,1..6}: GetReagentNId() != 0 ?
              need = GetReagentNQuantityForFormula()
              have = GetReagentNCount(player, false)
              name = GetReagentNDisplayName(false)              (u16, already localized)
craft     : build CreateArtifactConfigInfo (§4.1) and ControllerCharacter::SendCreateArtifactCmd
            ** only if GetMaximumCraftable(player) >= 1 -- Execute validates nothing **
```
Everything above is the export-driven world layer except the window offsets (window+0x3168/0x3178/0x335c/
0x335d) and the Combine site, which are exe-layer and die on any exe relink (AGENTS.md "Game patches").
A mod that models the crafting screen from Game.dll exports only — reading `GetRecipes` + `GetPlayerFormulas`
itself and building the info struct itself — needs **no** exe offsets at all.

## 8. Unverified / open

- Whether the rolled enhancement affix is silently dropped for relics/components/consumables (§5).
- The remaining `ItemReplicaInfo` fields inside `CreateArtifactConfigInfo` (+0x098, +0x0b8, +0x0d8, +0x0e0,
  +0x100, +0x104, +0x108, +0x128, +0x184, +0x188). Building the struct from scratch means either mirroring
  the exe's Combine byte for byte, or — safer — starting from `GetArtifactInfo(info)` and overwriting only
  the id/seed/enhancement fields.
- The mapping from `Item_Type` group buckets to the window's five top tabs (exe-side).
- `CreateArtifactConfigInfo+0x198`'s exact role (a specific item id consumed instead of slot 0's record);
  read only as "the reroll/upgrade flavour", never exercised.
- `GetRerollCost` / `GetReagentNQuantityForReroll` (the Enchanter tab).
- `mem::map` value semantics of `GetPlayerFormulas` (always 0 on the learn path; `AddItemToFormulas`'s
  2nd argument is otherwise unexplained).
- Nothing here has been exercised in a live game.
