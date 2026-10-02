# Devotion: how it works and how the mod can reach it (mapped 2026-08-27, static RE only)

Synthesis of three notes: `docs/re_devotion_data.md` (database.arz + localization), `docs/re_devotion_gamedll.md`
(Game.dll exports, offsets, the XP/reclaim/assignment paths) and `docs/re_devotion_exe.md` (the devotion window:
object graph, click semantics, the celestial-power picker, text). The notes hold the evidence; this file holds
the model. Sections 1-3 are static RE (section 4 is the built, live-verified screen); everything marked "read" was read in disassembly or data,
"inferred" is marked. Game v1.3.0.8, base game only (no gdx1/2/3 databases in this install, so every count is
base-game).

## 1. The player-facing rules (as the game implements them)

- **Devotion points** come from restoring devotion shrines (30 shrine records, `devotionPoints = 1` each,
  restorable once per difficulty; 18 "ruined" ones take offerings, 12 "corrupted" ones are cleared by killing a
  spawn) and from quest/Lua grants. Both run `ScriptableAction_GiveDevotion::Execute`, which adds to the
  character's available AND total counters, clamped to `maxDevotionPoints = 50`
  (`records/creatures/pc/playerlevels.dbr`). Counters: `Character+0x1770` available, `+0x1774` total,
  `+0x1778` max; exported getters `GetDevotionPoints / GetTotalDevotionPoints / GetMaxDevotionPoints`.
- **Five affinities**, `AffinityType` 0 Ascendant, 1 Chaos, 2 Eldritch, 3 Order, 4 Primordial (read off the
  exe's `_stricmp` chain that parses the constellation records; the UI order `tagDevotionAffinity01..05` is the
  same). Stored as `u32[5]` at `Character+0x177c`; `Character::GetAffinity(type)`.
- **86 pickable constellations** (87 records; `constellation87` is background art with no stars), 438 stars,
  1..8 stars each. A star is a real `GAME::Skill` object from `records/skills/devotion/_devotiontree.dbr`
  (388 `Passive` = plain stat stars, 50 `Effect` = **celestial powers**). Stars have no individual names: a
  plain star's `skillDisplayName` is the constellation's tag; a power has its own (`tagDevotionEffectA01` =
  "Twin Fangs"). 8 of the 50 powers are shells whose name/XP table live in a `_skill_buff` companion via
  `buffSkillName` (read the companion or they come out nameless).
- **Gates.** A constellation requires affinity (`affinityRequired*`: tier 1 needs 1 of one affinity, tier 2
  needs 4..10, tier 3 needs e.g. Ascendant 20 + Order 7). Within a constellation the stars form a tree
  (`devotionLinksK = J`: star K's parent is J; star 1 is the root); a star is takeable when its parent is
  learned (the root always). One point per star (stars are `Skill_Operation` 2; the exe's "spend again" branch
  exists only for operation 1, which no shipped star uses). Five **Crossroads** tier-0 constellations
  (one star, no requirement) give 1 affinity each, one per affinity -- the only way to start.
- **Completion bonus** = the constellation's `affinityGiven*` (up to 2 affinities; tier 1 gives 3..6 total,
  tier 2 gives 1..5, tier 3 gives nothing). Granted by `Character::AddAffinity` the moment the last star is
  taken; revoked by `SubtractAffinity` when a constellation stops being complete.
- **Celestial powers must be bound to one of the character's learned active skills** (`tagQuickTip61`); the
  bound skill's use earns the power experience and it levels 1..cap (cap = the length of the power's
  `skillExperienceLevels` array: 20 for tier 1, 15 for tier 2, 10 for tier 3). Its proc is the power's
  `templateAutoCast` controller record (`records/controllers/itemskills/cast_@<target><trigger>_<chance>%.dbr`:
  AttackEnemy / AttackEnemyCrit / AnyHit / MeleeHit / Block / LowHealth and a chance). A power sits at the
  last star in 45 of 49 constellations that have one; Rhowan's Crown, Affliction and Crab put it at star 3,
  Abomination has two (stars 5 and 8). **Read `skillType`, never assume "last star".**
- **Removing points** is only possible in reclaim mode. **A spirit guide opens it** (verified live 2026-08-27:
  the guide's skills window gets a Devotion tab that calls `DisplayDevotionWindow(reallocatorId)`, setting the
  reclaim byte `+0x2419` and parking the id at `+0x2440` -- the exe note's "tonic only" reading was wrong; the id
  is whatever opened reclaim mode, an NPC or an item). A **Tonic of Clarity**
  (`records/items/misc/potions/potion_devotionreset.dbr`, class `ItemDevotionReset`; blueprint 150000 iron bits +
  8 aether shards) is the full reset. Each reclaimed point costs iron bits
  on an escalating tier table (`devotionReclamationPointTiers/Costs` on the PC record, 25 .. 15000) plus
  `devotionReclamationAetherCost = 1` aether crystal. A star cannot be reclaimed while a learned star rests on
  it (link), nor when losing this constellation's `affinityGiven` would drop any other learned constellation
  (or itself: "self-locked", `tagRemoveBase = Devotion Point Cannot be Removed`) below its requirement.
  68 of the 86 constellations require an affinity they also give, so self-locks are common.
- The tonic's other use is **reset everything** (`GameUIInterface vt[0xe8]` -> exe+0x18c9d0): every star to
  level 0, affinities revoked, powers unbound, points refunded.

## 2. The runtime model (Game.dll, all exported unless noted)

- Star / power state on `Skill`: `+0xc8` base level (`GetSkillLevel`, 0 or 1 for a star), `+0xd0` devotion level
  (`GetDevotionLevel`), `+0xd4` devotion experience (`GetDevotionExperience`), `+0x1d0` **devotion parent =
  the object id of the HOST skill a power is bound to, 0 = unbound** (`Get/SetDevotionParent`; NOT the link
  parent, NOT the constellation), `+0x4a0` `Skill_Operation` (`GetSkillOperation`: 0 ordinary skill, 1 "Skill",
  2 "Passive" star, 3 "Effect" power), `+0x3b8` the attached power on a HOST (`GetAutoCastSkill`,
  `HasAutocastSkill`), `+0x450` the power's controller record name (`GetTemplateAutoCast`, std::string by
  hidden pointer). `GetDevotionMaxLevel` is `SkillProfile+0x178` = the `skillExperienceLevels` count (0 for a
  plain star). `Skill::GetCurrentLevel` returns the devotion level once it exceeds 1 -- a power's devotion
  level IS its skill level, and `SkillLevelChange` refuses to overwrite an attached operation-3 skill's level.
- Enumerating stars: `SkillManager::GetSkillList()` (the vector at `SkillManager+0x20/+0x28`) filtered by
  `GetSkillOperation() != 0`. `GetNumDevotionPointsSpent()` = the sum of those `GetCurrentLevel()`s.
- **Experience** (read): `Character::ReceiveExperience` -> `SkillManager::AddExperience(xp)`; for every star
  whose devotion parent resolves to a learned skill and that has an XP table: `Skill::AddExperience` (vt+0x90,
  `+0xd4 += xp`) then `IncrementDevotionLevel` (vt+0xa0) while `+0xd4 >= skillExperienceLevels[level]`, with a
  banner and `tagDevotionSkillMaxLevel` at the cap. The XP share forwarded was not traced.
- **Reclaim economy**: `SkillManager::GetCurrentDevotionReclamationCost()` (iron bits, from the tier table by
  the reclaims-so-far counter `SkillManager+0xf4`), `GetDevotionReclamationAetherCost()`,
  `UseDevotionReclamationPoints(n)` charges (n > 0) or refunds (n < 0) money + aether, returns false when
  unaffordable, and sends `ControllerCharacter::SendReclaimDevotionPointCmd(aetherDelta, moneyDelta)` --
  **the two ints are currency deltas, not ids**; `ReclaimDevotionPointConfigCmd::Execute` only moves currency.
  There is NO command for spending or refunding the points/levels themselves: the exe does that client-side
  with plain calls, and the window wraps the session in `GameEngine::SetSaveEnabled(false)` on open /
  `AutoSave()` on close.
- **Assignment** = `host->SetAutocastSkill(power, power->GetTemplateAutoCast(), false)` (virtual, vt+0x5e0)
  + `power->SetDevotionParent(hostId)`; unbind = `host->SetAutocastSkill(nullptr, "", false)` +
  `SetDevotionParent(0)`. The exe re-validates the pair every frame (`Star::ValidateAutocastBinding`,
  exe+0x17dfc0) and clears it if the host's controller name or class no longer matches.
- Text: `GameEngine::GenerateUIDevotionText(star, host /*may be null*/, lines, SkillReasons const*, false,
  reclaimMode, ironCost, aetherCost, 0x31)` is the star tooltip (name, bonuses, XP/level for a power, the
  "attached to X" line from `host`, requirement/cost reasons from the 14-byte `SkillReasons`: +0 no points,
  +2/+0xa link unmet, +3 maxed, +8 can't afford, +9 affinity met, +0xb unlearned, +0xc affinity unmet).
  `GenerateUIDevotionSearchText(star, lines, 0x31)` is a compact "what this star is" block (the window's
  search corpus). A constellation's rollover is composable from exports alone: `LocalizeWithoutParams` of
  `constellationDisplayTag` / `constellationInfoTag`, the required pairs vs `GetAffinity`, the given pairs.
  `GetAffinityBitmap(type)` exists for icons; affinity names are `tagDevotionAffinity01..05`.

## 3. The exe window (`InGameUI+0x813a0`, ctor exe+0x185640, vtable exe+0x315d80; details in re_devotion_exe.md)

- **The survey's "HARD, rebuild from .arz" verdict is wrong.** The exe builds the whole graph at load:
  `window+0xa8/+0xb0` = `std::vector<Constellation*>` (110 slots, 87 filled); `Constellation` (0x228) has
  the name/info tags at `+0x38/+0x58`, required pairs `+0x90`, given pairs `+0xa8` (each `{int type, int
  amount}`), stars `+0x78` (`std::vector<Star*>`), cached flags `+0x30` affinity met / `+0x31` any learned /
  `+0x32` complete; `Star` (0x190) has the Skill id `+0x108`, the bound host id `+0x10c`, 1-based link indices
  `+0x118`, map position, `+0x134` link-blocked / `+0x135` affinity-blocked (refreshed by
  `RefreshEligibility` exe+0x184db0), pending-change count `+0x130`, and the ready `SkillReasons` at `+0x160`.
- Window pointer without a new RVA: `GameEngine::GetUI()` returns `InGameUI+0x98`; its vtable exe+0x31a680 has
  `vt[0xf0]` = devotion window, `vt[0xf8]` = skills window, **`vt[0x90]` = `DisplayDevotionWindow(resetItemId)`**
  (0 = plain open; an item id = reclaim mode: the ONLY writer of the reclaim byte `window+0x2419`, refused with
  `tagReclaimNoPoints` when nothing is spent), `vt[0xe8]` = reset all.
- Opening: the skills window's Devotion tab = `SkillsWindow::SetTab(w, 2)` (exe+0x27c6f0) -> `DisplayDevotionWindow(0)`
  after `CloseAllWindows` (so the skills window hides). The window's own two mastery tabs go back via
  exe+0x18c360 (guarded by a `tagConfirmSkillChanges` prompt, InterestedParty 0x16, when changes are pending).
  N (Skill Window key) only closes it. Escape presses the close button. Close = `Show(false)` (vt+0xb0), which
  clears reclaim mode, consumes the tonic, and autosaves.
- **Clicking a star** (`Star::HandleMouseEvent` exe+0x17ea10, on left UP): blocked flags -> nothing; else
  spend = `IncrementSkillLevel(1)` (vt+0x48) + `Character::SubtractDevotionPoint` + `IncrementDevotionLevel`
  (vt+0xa0, only when devotion level < 1) + `AddAffinity` per given pair if the constellation just completed;
  a power then opens the picker. Reclaim mode = `UseDevotionReclamationPoints(1)` (the charge; bail on false)
  + `SetSkillLevel(0)` (vt+0x50, NOT Decrement) + `SubtractAffinity` if it stops being complete + unbind the
  power + `AddDevotionPoints(1)`. **Everything is applied immediately**; the Undo button (exe+0x18be02..) replays
  every pending change backwards using the per-star `+0x130` counters. exe+0x18c0a0 (the survey's "commit
  path") is the middle of Undo.
- **The celestial-power picker** is a modal owned by the devotion window (`window+0x25d0`, vtable exe+0x318448),
  not the skills window and not a drag. Candidates = `Character::GetSkillList` + `GetItemSkillList` +
  `GetItemSkillCache`, keeping a skill only if: not the current host, `!IsSkillTheMasterySkill`,
  `!SkillManager::IsDefaultSkill`, `!IsSkillModifier`, `!HasAutocastInDbr`, `Skill+0x2fa == 0` (inferred:
  not itself an autocast), `GetSkillOperation == 0`, `Skill::IsSkillA(power, S->GetRTTIClassInfo())` (the
  power's class filter -- e.g. the weapon-requirement powers), `!Skill::IsSkillBlackListed(power,
  GetObjectName(S))`, and (class skills) its mastery is one of the character's. Rows are greyed when
  `GetSkillState` says unlearned and flagged when the skill already carries a power (choosing it prompts
  `tagDevotionConfirm`, InterestedParty 0x19, then detaches the old power first). A pick = exe+0x1867f0
  `BindCelestialPower(window, hostId)` = the four-call sequence in section 2. All ten predicates are exports,
  so the mod can rebuild the list without showing the picker.
- Info panel `window+0x2408`: points available / total text, five affinity gauges in enum order, money +
  aether in reclaim mode. Search box `window+0x1e88` filters stars by substring over their
  `GenerateUIDevotionSearchText` lines. 36 byte signatures for an `available()` check are in the exe note.

## 4. The mod's screen (built 2026-08-27, verified through the loop)

Decided with the user: no separate devotion window -- the SKILLS window gets two tabs, and the devotion window
itself is never shown. `src/gameapi_devotion.cpp` (model) + `src/screens/skills.cpp` (tabs); dev route `/devotion`
(`?take= ?tip= ?hosts= ?bind=&host=`), `/cheat?devotion=N`.
- Structure from the exe graph (`exe_ui::devotion_constellations`: window+0xa8, Constellation +0x38/+0x58/+0x78/
  +0x90/+0xa8, Star +0x108/+0x10c/+0x118; two byte signatures added to `available()`), everything else exports.
  Eligibility is recomputed from exports (links + `GetSkillLevel`, `GetAffinity` vs the required pairs), not read
  from the Star flags (those are only refreshed while the game's window is shown).
- Constellations tab: points + affinities lines, then three Tab stops (learned incl. complete / available / unavailable,
  2026-09-18), each an alphabetical list of tree groups named "Bat (2/5)", "empty" when it has none; stars breadth-first from the root, "star N" (powers by name), value learned / needs star K /
  needs <affinity> / available. Enter = `take_star` (the window's spend sequence + the completion bonus), Space =
  `GenerateUIDevotionText` with our 14-byte SkillReasons (the icon-only "Complete Constellation Bonus" block is
  replaced by spoken pairs); group Space = name, description, "requires X n, have m", "gives ...".
- Celestial Powers tab (appears once a power is learned): name, level, attached-to, constellation; Enter = the host
  picker (`power_host_candidates` = the game's ten-predicate filter, learned skills only, "has <power>" on a host
  already carrying one; `bind_power` = SetAutocastSkill + SetDevotionParent, the exe Star mirrored; replacing
  detaches the other power first). The character sheet has an "affinities" row.
- Verified live on the test char: Crossroads -> affinity, Bat 1..5 -> Twin Fangs, bind/unbind/rebind to Cadence,
  Cadence's tooltip shows "Celestial Power: Twin Fangs", Akeron's Scorpion star 1 from the screen.
- **ABI lesson**: `Skill::IsSkillBlackListed(std::string)` takes its string BY VALUE and in the MSVC x64 ABI the
  callee destroys it -- freeing our buffer afterwards double-freed and killed the game.
- Reclaiming (2026-08-27, verified live): in a spirit guide's reclaim mode (`exe_ui::skills_reclaim_mode`, read off
  the mastery pane -- the game's own map is never shown) the Constellations tab gets the hint row, per-star costs and
  Backspace = `reclaim_star` (the map's RECLAIM branch: `UseDevotionReclamationPoints(1)` charges, `SetSkillLevel(0)`,
  `SubtractAffinity` when the constellation stops being complete, unbind a power, `AddDevotionPoints(1)`), gated by
  `can_reclaim_star` = RefreshEligibility's reclaim rules (a learned star linking to it; ComputeReclaimBlockers'
  self-lock / "would lock X" with this constellation's bonus subtracted) plus the bits and aether costs. Dev:
  `/devotion?why=<id>` prints both gates, `?reclaim=<id>`, `/cheat?aether=N`. The aether cost line is absent from
  the game's sidebar while you hold none, but the charge is real (1 per point here).
- **Affinity is not saved**: the game derives it from complete constellations when its map is shown; `constellations()`
  reconciles the counters the same way (to value) so a loaded character gates correctly without the map.
- Not modelled: the game's devotion Undo button. The Tonic of Clarity is used from the bag like any usable item
  (`ItemDevotionReset::Use` -> the full reset; untried live).
- **Clear all** (2026-10-02, mod-only, verified live): a spirit guide's Constellations tab gets "clear all constellations,
  N devotion points, X iron bits and Y aether crystals" -> a yes/no picker -> `gameapi::clear_all_devotion`: one
  `UseDevotionReclamationPoints(N)` (all-or-nothing, escalating per point, 1 aether each) then every learned star
  `SetSkillLevel(0)`, powers unbound, `SubtractAffinity` once per complete constellation, `AddDevotionPoints(N)`. It
  ignores the lock order on purpose: vanilla has no way out of a self-sustaining set at a spirit guide (no apply
  button, no batch reclaim; the exe's only multi-point `UseDevotionReclamationPoints` call is Undo's refund, exe+0x18c142)
  except re-adding support first, which needs a spare point. The price preview is read-only: `GetCurrentDevotionReclamationCost`
  is a pure function of `SkillManager+0xf4` (reclaims so far), `+0xc0` tier thresholds and `+0xd8` costs, replicated per
  point and checked against the game's own first-point price (a mismatch hides the row).

## 4a. The earlier design sketch

- **Model**: read the exe graph (two new offsets beyond the known window: `+0xa8` and the Constellation/Star
  layouts) for structure, order and the game's own eligibility flags; everything else through exports. Or,
  exe-free: the structure from `assets`-shipped tables generated offline by `tools/arz.py` (constellation ->
  star records -> `SkillManager::FindSkillId` by record path) with the gates recomputed from
  `GetAffinity` / `GetSkillLevel` -- the exe graph is nicer (positions, cached flags, SkillReasons) but dies on
  a relink like every window layout; the export path survives. Recommendation: exe graph for the list and
  flags with the export path as the fallback for the tree (the data is static and small: 86 x up to 8 ids).
- **Screen** (`WindowScreen`, active = the devotion window's IsVisible): a list of constellations (nearest to
  affordable first? or tiers), each expanding to its stars in link order; a star row speaks its
  `GenerateUIDevotionText` lines (a plain star: the constellation name + stat lines; a power: its own name,
  level/XP, "attached to X"), Space = the constellation rollover (name, required vs have, given), Enter =
  spend (the section-3 sequence with the gate replicated, then `RefreshEligibility(window)` for one RVA so the
  game's flags and tooltips stay right), Enter on a learned power = the host picker (our `list_picker` over the
  ten-predicate candidate list; first entry "none"), Backspace = reclaim only while `window+0x2419` is set
  (mirrors the skills screen's spirit-guide rule), a header row with points available / total and the five
  affinities, and search via type-ahead over the search text.
- **Elsewhere**: the hotbar/skill readouts could append "with <power>" from `Skill::GetAutoCastSkill(host)`;
  "<power> reached level N" is a banner the game already shows (`Engine::ShowCinematicText`, capturable);
  sonar already cues shrines.

## 5. Open / unverified (all static; needs the loop)

- Nothing exercised live: the spend/reclaim/bind sequences, the vtable slots (+0x48/+0x50/+0xa0/+0x5e0), the
  Constellation/Star offsets, and `GetUI()->vt[0x90]`.
- `Skill+0x2fa` / `+0x2f9` (picker filter and row flag) have no accessor found; inferred meaning.
- How much of the character's XP `ReceiveExperience` forwards to `SkillManager::AddExperience`.
- `GenerateUIDevotionText`'s first bool (the exe always passes false).
- Which shrines are placed per difficulty (data says 61 restorations possible, cap 50; placement is in the
  level files, not the database).
- Whether the exe's per-frame `ValidateAutocastBinding` would undo a mod-made binding whose host fails
  `IsSkillA` -- i.e. the ten predicates must be replicated exactly, not approximated.
