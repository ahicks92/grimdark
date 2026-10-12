#pragma once
#include <memory>
#include "core/screen.h"

namespace gd::screens {
// The character / inventory window (C or I): tabs Equipment, one per bag, Stats. Equipment rows read
// "slot: item" (Enter unequips into the bag, Space = the game's tooltip); bag rows read "item, x N" in
// reading order (Enter = the bag's right-click: equip / drink / read; Space = tooltip); Stats is the
// character sheet. Model: src/gameapi.h. The game's own bag selection is set only by Ctrl+Enter on a bag tab.
std::unique_ptr<gd::core::Screen> make_inventory();
// Ctrl+Enter on a bag tab: that bag becomes the game's selected bag = where pickups go once bag 1 is full.
void set_receiving_bag_focused();
void drop_focused();   // Delete on a bag item: drop it (or, soulbound / untradeable, confirm destroying it)
// Dev: the ItemSource value passed to PlayerInventoryCtrl::UseItem for a bag item (until RE settles it).
void set_bag_item_source(int source);
int bag_item_source();
}  // namespace gd::screens
