# The stash and the Materials and Relics store (static RE + live test 2026-11-01, build 1.3.1.1)

The game's caravan window has four panels: the private stash, the transfer ("shared") stash, and two views of one more
container, the **store**: `caravan_materialwindow.dbr` (18 boxes: scrap, dynamite, crafting materials) and
`caravan_relicwindow.dbr` (108 boxes: components). Each box names one record (`uireagentitem.tpl`, `reagentName`). The game calls
it "reagents" everywhere. The mod shows the two views as one tab, "materials and components".

## The store
- `GameEngine::GetPlayerReagents` = `mem::map<std::string record, ReagentData>` at GameEngine+0x36db0. Node +0x20 record, +0x40 the
  object id of the store's ONE item for that record, +0x44 a mirror of its count. The item's stack size IS the count. An emptied
  record keeps its node at count 0 (`gameapi::reagent_store` skips those).
- **Shared between characters**: `SaveReagents` / `LoadPlayerReagents` go through `GetSharedSavePath`. So there is no reason to keep
  an eligible item out of it, and crafting already counts it (`GetReagentNCount`). Enter sends an eligible item here.
- Eligible = `Item::IsReagentCompatible` (byte Item+0xc64).
- In: `AddItemToReagents(id)` copies the stack into the store and does not consume the source. The caller removes it from the bag
  (`PlayerInventoryCtrl::RemoveItem` + `SendRemoveItemFromInventory`), then `SaveReagents`. Read from
  `CursorHandlerItemMove::PrimaryReagentActivate`, Game.dll+0x186cd0.
- Out: the panel's mouse handler exe+0x132a30. Shift-click makes a new item from the store item's replica with count =
  min(count, `GetItemMaxStackSize`), `SendAddItemToInventory`, adds it to the bag, reduces the store item. We reduce it with
  `TakeItemFromReagents(id, n)`, the game's own decrement. Ctrl-click opens the stack-split window (not modelled); right-click on a
  component appears to start the attach cursor from the store (ItemRelic branch at exe+0x133028, ItemSource 7, not modelled).
- All at once: `PlayerInventoryCtrl::DepositReagents`, the inventory's Move Reagents button (`reagentDepositButton`, exe+0x2667d4). It
  works anywhere, needs no stash open, and is not gated on an expansion in the exe. The game asks first when the option Reagent
  Transfer Confirmation (Options bool 0x49) is on; the mod does not ask, the move is harmless and undoable.

## Traps
- **`Player::RemoveItemFromPrivateStash` and `GameEngine::RemoveItemFromTransfer` only look in the SELECTED tab.** The exe can use them
  because it only ever shows the selected tab. Our list shows every tab, so taking from another tab failed and was reported as "no
  room in your bags" with empty bags. `stash_to_bag` calls `InventorySack::RemoveItem` on the item's own sack.
- `AddItemToTransfer` does not check whether the item is allowed. The exe tests item vtable +0x408 first (`PrimaryTransferActivate`,
  `QuickDropInTransfer`); false = `tagTransferStashError`. Live: equipment passes, a soulbound necklace and a component are refused.
  The slot is hardcoded in `transfer_allowed`; check it with vtcmp on a patch.
- `SortSecondarySack` sorts the game's selected bag and does nothing when bag 1 is selected: `sort_bag` selects the bag for the call and
  restores the selection.
- **The selected bag must never be bag 1.** `PlayerInventoryCtrl::AddItem` (every pickup, and the stash's take): stacks merge, then bag
  1, then if the selection is bag 1 it fails, otherwise the selected bag and then all the others. `IsSpaceAvailable` searches every bag,
  so it can say yes to what `AddItem` refuses. Measured: bag 1 full, selection on bag 1, five empty bags: refused; selection on bag 2, 3
  or 4: placed there, and a full selected bag overflowed to bag 2. It is saved in the character file. The exe's tab buttons only pass
  1..5; the mod's old tab browsing and Ctrl+Enter could write 0. `gameapi::keep_bag_overflow` heals it from the world's tick. Add to
  the bag before removing from a stash, as the exe does, so a refusal loses nothing.
- Sorting takes a rising number that picks the arrangement, so pressing again reorders. `InventorySack::Sort` returns false when an
  arrangement does not fit; the exe's stash button tries five.

## Not modelled
The stash search box (`InventorySack::SearchItems`, name or stats), tab customization (`customButtonWindow`: name, icon, border,
colours; how the game opens it was not confirmed), partial stacks, and moving items between bags.
"Complete Components" (`tagAutoComplete`, `inventory_buttonautocomplete*`) is not missing: no master record uses those records, and
every component in the data has `completedRelicLevel` 1, so there are no partial components to combine.
