# Unreleased

- Fix: attaching a component or augment only offers items in your bags or equipped, no longer items in your personal and shared stashes.

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
