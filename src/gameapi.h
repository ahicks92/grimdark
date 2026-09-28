#pragma once
// The in-world windows' MODEL, read and driven through Game.dll / Engine.dll exports (static survey in
// docs/ingame-ui-survey.md): quests, factions, the quickbar, the lore codex, bags and equipment, skills, the
// character sheet. Screens (src/screens/*) present these; nothing here reads drawn text or widget state.
// Every function is game-thread only and returns copies (no game pointer is held across frames except the
// opaque object pointers the screens re-validate through object_by_id on use). Faults inside the game's
// calls are caught (SEH) and logged; the call then reports empty / false.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "world.h"

namespace gd::gameapi {
void load();                  // resolve the exports (once); missing ones are logged and their features report empty
void* engine();               // *gGameEngine (exported data); null before the world
void* player();               // the main player (Character / Player)
void* controller();           // the main player's ControllerPlayer (captured by world.cpp)
// A localization tag -> the game's text (LocalizationManager::LocalizeWithoutParams), colour codes stripped;
// empty when the tag is unknown.
std::string localize(const std::string& tag);
std::vector<std::string> shrine_offerings(unsigned shrine_id);   // a ruined devotion shrine's required items, display names

// ---- objects by id (inventory items, notes, skills come back as ids) ----
void* object_by_id(unsigned id);   // null when no such object; the lookup is a cached ObjectManager sweep
void invalidate_objects();         // after an action that may have created/destroyed objects
unsigned object_id(const void* object);
std::string object_record(const void* object);   // Object::GetObjectName: the .dbr path

// ---- objectives / quests ----
std::vector<std::string> objectives();           // the HUD's objective tracker lines
// Quest2Objective::Satisfied, read off live quests 2026-08-26: 1 = satisfied, 2 = active and not yet, 0 = not evaluated (a
// later task's). Only 1 is "done" -- any-nonzero read every open objective as done (the user's codex report).
inline constexpr int kSatisfied = 1;
struct Objective { std::string text; int satisfied; bool done() const { return satisfied == kSatisfied; } };   // satisfied: the enum above, raw
struct Task { void* p; std::string name, description; int state; unsigned uid = 0; int reward_events = 0; std::vector<Objective> objectives; std::vector<std::string> rewards; };  // state 1 available, 2 in progress, 3 complete
struct Quest { void* p; unsigned id; std::string name, group; bool tracked, complete, in_progress; std::vector<Task> tasks; };
enum QuestFilter : int { kQuestsAll = 0, kQuestsInProgress = 1, kQuestsCompleted = 2, kQuestsTracked = 4 };
std::vector<Quest> quests(int filter);
bool set_quest_tracked(void* quest, bool on);
bool complete_quest_task(void* quest, int task_index);   // DEV ONLY: the game's own CompleteQuestTask on that task (rewards run, the reward window opens)

// ---- factions ----
struct Faction { int type; std::string tag, name, level_name; float value; int level, low, high; bool unlocked; };
std::vector<Faction> factions();                 // the player-visible factions in the game's enum order

// ---- the quickbar (hot slots) ----
struct HotSlot { unsigned index; std::string name; int type; unsigned skill_id; int cooldown_ms; int status; bool empty; };
constexpr unsigned kHotSlotCount = 47;
std::vector<HotSlot> hotslots();                 // every slot of the displayed set (index = the game's slot index)
unsigned displayed_skill_set();                  // PlayerHotSlotCtrl::GetDisplayedSkillSetIndex (the active weapon set's)
bool hotbar_assignable(const void* skill);       // the exe's quickbar-picker filter (exe+0x1e7860) for a number-bar slot
HotSlot hotslot(unsigned index);
HotSlot primary_slot();                          // left mouse
HotSlot secondary_slot();                        // right mouse
HotSlot health_potion_slot();                    // the R / health potion slot (read-only; the game auto-manages it)
HotSlot mana_potion_slot();                      // the E / energy potion slot (read-only)
std::vector<std::string> hotslot_tooltip(unsigned index);
bool assign_skill_to_slot(unsigned index, unsigned skill_id);   // a HotSlotOptionSkill built here (the game copies it)
bool set_primary_skill(unsigned skill_id);
bool set_secondary_skill(unsigned skill_id);
bool activate_hotslot(unsigned index);

// ---- the lore codex ----
struct Note { unsigned id; void* p; std::string title, heading; };
std::vector<Note> lore_notes();
// The FULL localized note text (the record's itemText tag), split into paragraphs; empty when the object has
// no text tag. The game's own tooltip truncates long notes; the reader uses this tag.
std::vector<std::string> note_full_text(void* note);
std::vector<std::string> note_text(void* note_item);   // ItemNote::GetUIDisplayText, one string per line

// ---- bags and equipment ----
struct BagItem { unsigned id; void* p; float x, y, w, h; std::string name; unsigned stack; bool component = false; bool augment = false; };   // component: a component is attached (the grid's icon badge); augment: an augment is attached
struct Bag { int index; std::string name; unsigned width, height; std::vector<BagItem> items; std::string debug; };   // items in reading order (rows, then columns)
std::vector<Bag> bags();
int selected_bag();
bool select_bag(int index);
struct EquipSlot { int loc; std::string label; unsigned item_id; void* item; std::string name; bool component = false; bool augment = false; bool inactive = false; };   // loc = EquipmentCtrlLocation 1..14; inactive = equipped but detached (requirements no longer met)
bool has_component(const void* item);            // ItemEquipment::HasRelic, after an is-a ItemEquipment check (false for anything else)
bool has_augment(const void* item);              // ItemEquipment::HasEnchantment, same guard (an augment = the game's "enchantment")
int item_classification(const void* item);       // Item::GetItemClassification(true) (virtual): 0 common, 1 magical, 2 rare, 3 epic, 4 legendary, 5 quest...; -1 unknown
// The Inventor's window (docs/inventor.md) -- the game's own gates and item moves:
unsigned dynamite_count();                       // Player::GetCurrentDynamite (the bags' quest_dynamite stacks)
bool dismantle_unlocked();                       // GameEngine::MainPlayerCanUseDismantle (the DISMANTLING_UNLOCKED token)
bool inventory_detach(unsigned id);              // PlayerInventoryCtrl::RemoveItem(id, true): the item leaves the grid but lives on (a station chamber holds it)
bool give_item_to_player(unsigned id);           // ControllerPlayer::GiveItemToPlayer(id, false): a detached item back into the bags (the exe's chamber-return call)
bool is_equipment(const void* item);             // is-a ItemEquipment
bool is_usable(const void* item);                // is-a OneShot or ItemNote: what UseItem may be given (anything else it would just remove from the bag)
std::string component_name(const void* item);    // the attached component's name (ItemEquipment::GetRelic), "" when none
// The window's own arithmetic, so the rows can say the price before the item is in the chamber (the game's panel
// recomputes it from the same inputs once it is): salvage = trunc(enchanterRecoveryFactor 0.05 x GetItemCost(false))
// (records/game/gameengine.dbr); dismantle = itemLevel*10+150 (dismantle_table.dbr's dismantleCost) + the salvage
// cost when a component is attached (the exe adds it, panel+0x11a0).
unsigned salvage_cost(const void* item);
unsigned dismantle_cost(const void* item);
std::vector<EquipSlot> equipment();
// Which requirements an item fails and by how much: label (level / the game's attribute names), what the character
// has (truncated like the sheet), the effective requirement (after the character's requirement reductions). Empty
// when the item is usable OR when the replica of the game's check disagrees with the game (then only the bare
// "requirements not met" is safe to say). See gameapi_items.cpp for the check.
struct Shortfall { std::string label; int have; int need; };
std::vector<Shortfall> requirement_shortfalls(const void* item);
bool alternate_weapons();                        // the weapon-swap set in use (EquipmentCtrl::GetIsAlternate)
bool can_equip(unsigned item_id, int loc);
std::vector<int> equip_slots_by_class(unsigned item_id);   // the slots its class fits, requirements ignored (Backslash compare on gear you can't wear yet)       // EquipmentCtrl::CanItemBePlaced -- the equip picker's filter
bool swap_weapon_set();                          // toggle the active weapon set (the two hands); returns new state (true = alternate)
unsigned money();                                // iron bits
bool dev_add_money(unsigned bits);               // dev only: Character::AddMoney
std::string item_name(const void* item);         // Item::GetGameDescription (virtual)
unsigned item_stack(const void* item);
std::vector<std::string> item_tooltip(const void* item, bool simple, bool details = false);   // Item::GetUIDisplayText(details = the Ctrl-held form) / GetSimpleUIDisplayText, virtual
bool item_requirements_met(const void* item);
// Actions (each is the game's own call; the screens re-snapshot afterwards).
bool use_item(unsigned id, int source);          // PlayerInventoryCtrl::UseItem (the bag's right-click: equip / drink / read)
bool drop_item(unsigned id);                     // ControllerCharacter::SendDropItemRandom
bool unequip(int loc);                           // EquipmentCtrl::RemoveItem on the slot's item
bool equip(unsigned id, int loc);                // EquipmentCtrl::PlaceItem(loc, id, ...)
bool pickup_item(unsigned id);                   // ControllerCharacter::PickupItem (the game's pickup command; no range check)
// Components/augments (records/items/materia): activating one in a bag opens the attach picker.
bool is_component(unsigned id);                                    // is this bag item a component/augment?
std::vector<unsigned> compatible_items(unsigned component_id);     // Player::GetCompatibleItems filtered to bags+equipped (the game's list adds the stashes)
bool attach_component(unsigned component_id, unsigned target_id, int source);   // Character::UseItemOn (attach + consume)

// ---- merchants and the caravan ----
struct MarketTab { int type; std::string name; std::vector<BagItem> items; };   // one per Market_TypeEnum the merchant stocks (probed 0..7)
std::vector<MarketTab> market_stock(unsigned market_id);
std::vector<MarketTab> market_stock_types(unsigned market_id, const std::vector<std::pair<int, std::string>>& tabs);   // explicit tab types (exe_ui::vendor_tabs); empty tabs kept
constexpr int kNoFaction = -1000;
int merchant_faction(unsigned market_id);              // Character::GetVisibleFaction of the merchant NPC, kNoFaction when unknown
float faction_level_value(const char* level_tag);      // GameEngine::GetFactionLevelValue(tagFactionStateFriend2) = the tier's reputation threshold
std::string market_price_text(unsigned market_id, unsigned item_id, bool buying);   // CreateUIPlayerBuyText / SellText, joined
bool buy(unsigned market_id, unsigned item_id);          // GameEngine::PlayerPurchaseRequest
bool sell(unsigned market_id, unsigned item_id);         // the bag's right-click-to-sell: PlayerSaleRequest + bag removal
// Partial stacks: split_stack clones `count` off a stack the game's way (a new item known to the character but
// in no bag grid, like the game's split-onto-cursor) and returns its id (0 = refused); sell_split sells that
// clone (a frame or more later, so the character-side add has run); unsplit_stack puts a clone into the bag,
// where the game merges it back into its stack (the recovery for a refused sale).
unsigned split_stack(unsigned item_id, unsigned count);
bool sell_split(unsigned market_id, unsigned item_id);
bool unsplit_stack(unsigned item_id);
std::string vendor_dump(unsigned market_id);              // dev: market map keys + market_stock(id) probe
struct UniqueId { uint8_t b[16] = {}; bool operator==(const UniqueId&) const = default; };   // GAME::UniqueId (4 ints)
struct ShrineUids { std::vector<UniqueId> discovered, restored; };
ShrineUids shrine_uids();                                // Player::GetDiscoveredShrineUIDs / GetShrineUIDs
std::vector<Bag> stash_sacks();                          // private stash sacks (index 0..) then transfer sacks (index 100..)
bool stash_to_bag(int sack_index, unsigned item_id);     // the stash grid's shift-click: to the bag
bool bag_to_stash(unsigned item_id);                     // the bag's shift-click while the caravan is open
bool bag_to_stash_any(unsigned item_id, bool shared);    // into the first sack with room (the selected one first)
const void* stash_sack_vector(bool shared);              // Player::GetPrivateStash / GameEngine::GetPlayerTransfer (mem::vector<InventorySack*>)
bool buy_stash_sack(bool shared, unsigned cost);         // SubtractMoney + AddSack / AddTransferSack (the exe's buy handler minus its UI)
// Quickbar layout (47 slots per weapon config): bars 1..4 start at 0, 14, 26, 36; left mouse 10 (config A) / 11
// (config B); right mouse 12 / 13; health potion 24; energy potion 25; evade 46.
unsigned quickbar_slot_index(int bar, int k);    // bar 0..3, k 1..10

// ---- skills ----
struct SkillInfo {
  void* p; unsigned id; std::string name, record; unsigned level, max_level, ultimate_level, mastery_id, mastery_req, tier;
  bool locked, is_mastery, enabled, modifier;   // modifier = a tree sub-skill of another skill (Skill_Modifier, or anything with a base skill: the pet modifiers)
  bool item_auto = false;   // Skill::IsItemSkillAuto: an auto-triggered item skill (a proc / chance-on-attack) -- not player-assignable
  unsigned mastery_level = 0;       // Skill::GetMasteryLevel: this skill's mastery bar level (for the requirement gate)
  unsigned modified_skill_id = 0;   // Skill::GetBaseSkills[0]: the base skill a sub-skill enhances (0 = none)
};
std::vector<SkillInfo> skills();                 // the UI skill list, in the game's order
unsigned skill_points();
unsigned skill_level(const void* skill);          // Skill::GetSkillLevel, live
unsigned experience();   // Character::GetExperiencePoints on the main player (current level's XP; resets on level-up)
unsigned default_skill_id(int role);             // SkillManager::GetDefaultSkillId (0 = left mouse basic attack, 1 = right); live, never cache
std::vector<unsigned> item_skill_ids();          // skills granted by equipped items (SkillManager::GetItemSkillList)
std::vector<SkillInfo> assignable_skills();      // UI skills + EQUIPPED-item granted skills (deduped); caller filters by skill_aim
std::string dump_item_skills();                  // dev: GetItemSkillList vs equipped-item granted skills
unsigned masteries_allowed();
std::vector<unsigned> mastery_ids();             // the masteries the character has
std::vector<std::string> skill_tooltip(const void* skill);   // GameEngine::GenerateUISkillText, no points / requirements block (pickers)
// The skills window's tooltip: the same text plus the points / requirements / reclaim lines, from a SkillReasons filled
// the way the window fills it (exe+0x2492b0), so "press to add unused skill points" only appears when a point can go in.
std::vector<std::string> skill_window_tooltip(const void* skill, bool reclaim);
// The same text with the skill temporarily at `level` (the game's own IncrementSkillLevel(n) / DecrementSkillLevel(n)
// pair around the call; a level at or below the current one reads as-is). Dev/documentation use only: the increment
// applies the skill's passive effects to the character for the duration of the call.
std::vector<std::string> skill_tooltip_at(const void* skill, unsigned level);
// dev: every mastery's tree with the tooltip at level 0 and at max (tools/gen_masteries_doc.py); `aim` (optional) labels a
// skill object's targeting (the caller supplies world::skill_aim -- gameapi does not depend on world).
std::string dump_masteries(std::string (*aim)(const void* skill) = nullptr);
std::string skill_name_by_id(unsigned skill_id);   // object_by_id -> Skill::CreateUISkillName (buff/debuff labels); "" if none
// "" = the skill can take a point now; otherwise a human reason (no points / mastery rank / base skill).
std::string can_learn_skill(const void* skill);
bool learn_skill(const void* skill);             // +1 level (a skill point); refuses unless can_learn_skill is ""
std::string can_reclaim_skill(const void* skill); // "" = can reclaim now; else the reason (mastery last point / not enough bits)
bool refund_skill(const void* skill);            // -1 level; only at a spirit guide (exe_ui::skills_reclaim_mode)
unsigned reclaim_cost();                          // SkillManager::GetCurrentSkillReclamationCost (iron bits for the next reclaim)

// Masteries offered for selection: the nine classes (tagSkillClassName01..09 / tagSkillClassDescription01..09; 07-09
// are the expansions', whose base-game tags read "?" and are skipped without the DLC);
// `enumeration` is the game's mastery index (0 = Soldier), also the pane index for exe_ui::skills_set_pane.
struct MasteryChoice { int enumeration; std::string name, description; };
std::vector<MasteryChoice> mastery_choices();
// The mastery skill (the "class training" skill) of a mastery enumeration, or null.
const SkillInfo* mastery_skill(const std::vector<SkillInfo>& list, int enumeration);

// ---- devotion (gameapi_devotion.cpp; docs/devotion.md). Structure from the exe's constellation graph
// (exe_ui::devotion_constellations), state and actions through Game.dll exports. ----
struct Affinity { int type; std::string name; unsigned value; };   // type = AffinityType 0..4, name localized
std::vector<Affinity> affinities();                                 // the five in enum order
std::string affinity_name(int type);                                // tagDevotionAffinity01..05
unsigned devotion_points();                                         // available (unspent)
std::string affinities_text();                                      // "Ascendant 3, Chaos 1" (nonzero ones) or "no affinity"
unsigned devotion_points_total();
unsigned devotion_points_max();
struct DevotionStar {
  void* star = nullptr;        // the exe's Star (re-resolved on use)
  void* skill = nullptr;       // the star's Skill object
  unsigned index = 0;          // 1-based position in the constellation
  unsigned skill_id = 0, host_id = 0;   // host = the skill a learned celestial power is bound to (0 = none)
  std::string name;            // a celestial power's own name; empty for a plain star ("star N" is the screen's label)
  std::string host_name;
  bool power = false, learned = false;
  unsigned dev_level = 0, dev_max = 0, experience = 0, next_experience = 0;   // a power's level/XP (dev_max 0 for a plain star)
  std::vector<int> links;      // 1-based indices of the stars this one hangs off (empty = the root)
};
struct DevotionConstellation {
  void* p = nullptr;
  std::string name, description;
  std::vector<std::pair<int, unsigned>> required, given;   // {AffinityType, amount}
  std::vector<DevotionStar> stars;
  unsigned learned = 0;
  bool complete = false, affinity_met = false;
};
std::vector<DevotionConstellation> constellations();
std::vector<unsigned> star_order(const DevotionConstellation& c);   // 1-based indices, breadth-first from the root
// "" = the star can take a point now; else the reason ("needs star 2", "needs Chaos 4", "no points", "learned").
std::string can_take_star(const DevotionConstellation& c, const DevotionStar& s);
// The window's own click: IncrementSkillLevel(1) + SubtractDevotionPoint (+ IncrementDevotionLevel), and the
// constellation's affinity bonus when this completes it. `completed` reports that. Re-resolves the live graph by skill id.
bool take_star(unsigned skill_id, bool& completed);
std::vector<std::string> star_tooltip(unsigned skill_id);           // GameEngine::GenerateUIDevotionText as the window passes it
std::vector<std::string> constellation_tooltip(const DevotionConstellation& c);
// Celestial powers: the skills a power may be bound to (the game's picker filter, learned only), and the binding.
std::vector<SkillInfo> power_host_candidates(unsigned power_skill_id);
bool bind_power(unsigned power_skill_id, unsigned host_skill_id, std::string* replaced_power = nullptr);   // host 0 = unbind
unsigned hosted_power_id(const void* host_skill);   // the celestial power bound to this skill (0 = none): HasAutocastSkill, not from the DBR, operation = power
// Reclaiming a devotion point (only in a spirit guide's reclaim mode, exe_ui::skills_reclaim_mode): the game's
// gates -- a learned star hanging off it, the affinity self-lock, iron bits + aether crystals -- as a spoken
// reason ("" = allowed), and the star map's own reclaim sequence.
std::string can_reclaim_star(const DevotionConstellation& c, const DevotionStar& s, const std::vector<DevotionConstellation>& all);
bool reclaim_star(unsigned skill_id, bool& uncompleted);
unsigned devotion_reclaim_cost();          // iron bits for the next reclaim (SkillManager::GetCurrentDevotionReclamationCost)
unsigned devotion_reclaim_aether_cost();   // aether crystals per reclaim
unsigned aether();                         // Player::GetCurrentAether
bool dev_add_aether(unsigned n);           // dev only
std::string dump_devotion();
bool dev_add_devotion(unsigned n);   // dev only: AddDevotionPoints + AddTotalDevotionPoints (what a shrine grant does, minus the clamp)

// ---- the character sheet ----
struct Stat { std::string label, value; int spend = 0; std::string desc; std::vector<std::string> columns; };   // spend 1..3 = the row takes an attribute point (Physique / Cunning / Spirit); desc = the game's tooltip (Space); columns = more cells on the row (Left / Right)
// The sheet's Armor Rating (the expected armor per hit: flat + sum of region chance% * region armor) and its breakdown
// per body region, in the game's rollover order (head, chest, arms, legs, feet, shoulders). gameapi_skills.cpp.
struct ArmorPart { std::string name; int armor = 0, chance = 0, absorption = 0; };
struct ArmorBreakdown { int combined = 0; float flat = 0; std::vector<ArmorPart> parts; };
bool armor_breakdown(ArmorBreakdown& out);
std::vector<Stat> character_sheet();
// The attribute "+" buttons (ControllerCharacter::IncrementCharacter*, + the life/energy increments, as the
// sheet's own handler does): which = 1 Physique, 2 Cunning, 3 Spirit. False when no points are left.
bool spend_attribute_point(int which);
unsigned attribute_points();

// ---- Lua (dev): run a chunk in the game's LuaJIT state (LuaManager::RunCode on *(gEngine+0x68)) ----
bool lua_run(const std::string& code);

// ---- dev dumps ----
std::string dump_quests(int filter);
std::string dump_factions();
std::string dump_hotslots();
std::string dump_lore();
std::string dump_bags();
std::string dump_equipment();
std::string dump_skills();
bool dev_add_experience(unsigned xp);   // dev only: SkillManager::AddExperience on the main player
bool dev_open_skill_reclaim();          // dev only: open the skills window in spirit-guide reclaim mode
std::string dump_sheet();
std::string dump_object(unsigned id);
std::string find_objects(const std::string& needle, size_t max);
std::string dump_objects_stats();

// ---- Pets (gameapi_pets.cpp; docs/re_pets_gamedll.md) ----
struct PetInfo {
  unsigned id = 0;
  std::string label;          // the pet's own name ("Hellhound")
  float life = 0, life_max = 0;
  unsigned skill_id = 0;      // the summoning skill (the pen's owner); stance is keyed by it
  std::string skill_name;
  int stance = 1;             // 0 normal, 1 aggressive (the game's default), 2 defensive
  world::Vec3 pos;
};
std::string_view pet_stance_name(int stance);   // strings.h words
std::vector<unsigned> pet_ids();                 // GameEngine::GetLocalPetList: HUD portrait order = F2..F6 order
std::vector<PetInfo> pets();
bool set_pet_stance(unsigned pet_id, int stance); // per summoning skill: every pet of that skill follows
bool pet_attack(unsigned pet_id, unsigned target_id);
bool pet_move(unsigned pet_id, const world::Vec3& world_pos);
bool release_pet(unsigned pet_id);               // Disband Pet
std::string dump_pets();

// ---- Loot filter (gameapi_loot.cpp; docs/loot-filter.md) ----
constexpr int kLootFilterOptions = 42, kLootFilterColumns = 4;
struct LootFilterOption {
  int index;            // the LootFilterOption enum value (the bit)
  const char* tag;      // caption tag (tooltip = tag + "Info")
  const char* fallback; // the base game's English, for when localization is not up yet
  int column;           // 0 Quality, 1 Type, 2 Damage, 3 Character (the window's columns)
  bool default_on;
};
const std::vector<LootFilterOption>& loot_filter_options();   // window order; option 39 only with expansion 3 loaded
const char* loot_filter_column_tag(int column);
const char* loot_filter_column_fallback(int column);
bool loot_filter(int option);                    // Player::GetLootFilter (clamped)
bool set_loot_filter(int option, bool on);       // Player::SetLootFilter -- in effect immediately, saved with the character
bool loot_filter_defaults(int column);           // column -1 = the game's SetLootFilterDefaults; else that column's factory bits
bool item_passes_loot_filter(const void* item);  // Item::PassLootFilter(0): would the game label it (true when unknown)
bool entity_hidden(const void* entity);          // Entity::GetVisibility() == 0: the game does not show it (a collected placed quest item)
std::string dump_loot_filter();

// ---- Crafting (gameapi_crafting.cpp; docs/crafting.md) ----
struct Reagent { unsigned id = 0; std::string name; int need = 0; int have = 0; };   // have = bags + materials + stashes
struct FormulaInfo {
  unsigned id = 0;               // the ItemArtifactFormula object
  unsigned result_id = 0;        // the unrolled template result item (tooltip = stat ranges)
  std::string result_name;
  int result_classification = -1;   // ItemClassification (docs/loot-filter.md): 0 common .. 4 legendary, 8 relic
  unsigned cost = 0;             // iron bits
  int max_craftable = 0;         // the window's "[N]"
  std::vector<Reagent> reagents; // Base first, then slots 1..6 that are used
};
bool is_formula(const void* obj);
std::optional<FormulaInfo> formula_info(unsigned formula_id);
std::string dump_formula(unsigned formula_id);
struct CrafterBonus { std::vector<std::string> blurb; std::vector<std::string> entries; };   // the smith's enhancementTag lines, then one game-rendered line per possible bonus
std::optional<CrafterBonus> crafter_bonus(unsigned npc_id);
bool install_crafting_hooks();                   // Player::GiveArtifactToCharacter (dllmain, with the other feature hooks)
void remove_crafting_hooks();
void set_craft_listener(std::function<void(void* item)> fn);   // called from the hook with the finished item (null fn = off)
}  // namespace gd::gameapi
