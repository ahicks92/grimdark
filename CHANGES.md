# Unreleased

- At a spirit guide, the constellations tab has a new "clear all constellations" row that says what it costs. Clicking it asks yes or no, then gives back every devotion point at once for the same price as reclaiming them one by one. This gets you out of constellations that hold themselves up, which the game otherwise only lets you undo with a Tonic of Clarity. Celestial powers need binding again afterwards.
- New key `ctrl apostrophe`: follow whatever you are reviewing instead of a map marker. After that, `apostrophe` puts your review cursor back on it and pings it like `semicolon`, even if it moved or you have reviewed other things since, so you can keep targeting one enemy in a boss fight. It says "gone" once the thing no longer exists. Picking a map marker or pressing `p` replaces it.
- The stats tab now tells you how to spend attribute points. The attribute points line always says to click Physique, Cunning or Spirit, and while you have points left each of those says "click to assign an attribute point".
- Fix: Vire's Might, Nullification and the medal runes that charge, leap or teleport to a spot now show up in the hotbar manager. They fire at your target, or at the spot you have selected.
- Fix: attaching a component or augment only offers items in your bags or equipped, no longer items in your personal and shared stashes.
- Items now say "with component", "with augment" or "with component augment" in every list, not just the inventory: the vendor's buy and sell tabs, both stash lists, the equip and attach pickers, and the inventor.
- `ctrl p` pauses the game in single player. It always worked, but it wasn't written down anywhere. The README has a new Pausing section on what you can and can't do while paused (short version: no acting in the world, but windows and the mod's own keys work).
- `l` now walks to whatever you selected last: the thing you found with the finding keys, a map marker you picked or followed with `apostrophe`, or the unexplored area from `p`. Targets beyond about 200 units may not walk until you get closer.
- New key `p`: find the nearest unexplored area you can walk to, within about 200 units. `apostrophe` pings it and `l` walks there; it says "explored" once you've seen it. Meant for when you're stuck and don't know where to go.
- Fix: the hotbar manager's skill list now uses the game's own rules, so it no longer offers skills the game won't put on a bar, such as auto-toggled skills or skills from the weapon set you aren't holding.
- Fix: skill tooltips in the skills screen no longer claim you can add points when you can't. They now say why, just like the game: no points, mastery too low, skill maxed, needs another skill first, and so on. Tooltips in the hotbar and celestial power pickers no longer talk about skill points at all.

# 0.4.0 (2026-09-27)

- Wall tones are now smoother.  When hugging a wall the wall will seem flatter/straighter.
- A number of critical but rare incorrect wall tone cases have been fixed.
- With `alt dot` and the other enemy targeting keys, the mod now prefers to target the nearest enemy you can fire at when enemies are bunched together, rather than grabbing one behind a wall.  Turn this off in `t`'s new targeting section.
- We now explicitly support classic casting.  This mode, enabled in the game's options menu, walks you into range of enemies before using ranged abilities. Note that the options menu requires tabbing to Apply and pressing it; note also that this mode does not give you control over how you'll move.
  - Hold shift in classic casting if you want to avoid moving (vanilla behavior, not something we added).
- `l` is the "move to" key. Anything you can target with the review cursor that is reachable can be moved to.  This explicitly excludes map icons.  Notably it includes adjacent room entrances, in which case it'll move you to the approximate center of the target room.
  - IMPORTANT: sometimes, rarely, a room is divided by a wall and the path to it thus goes through other rooms, possibly lots of them. You'll walk it, but maybe into a crowd of enemies.
- `j`, `i`, and the other skill use/clicking keys no longer care about the game's HUD or camera.
- When enemies are in range of ranged abilities, their sonar ping goes up in pitch. When they are in melee range, it goes up in pitch again. Turn this off in `ctrl t` if you don't want it.
- If you have trouble telling if enemies and other objects are north or south of you, a new option in `ctrl t`, "north south echo on route pings", may help by changing the ping you hear when using `semicolon`, `apostrophe`, and the other targeting keys.
- Fix: automatic loot pickups are now announced through Zira.
- Untested fix: Unicode in the map list may now render properly, and Unicode when using type-ahead may now work.
- Fix: do not play mod-provided sounds for shrines that are not enabled on your difficulty.
- Fix/improvement: rework how the 3 pings you get when cycling through entities work out reachability, to better match both how you use them and what's really going on.
- Fix/improvement: the stats screen now shows armor breakdown
- Fix: `alt comma` works again, jumping to the nearest of the highest-rarity enemies like `alt` with the other review keys.
