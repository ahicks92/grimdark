// The loot filter (docs/loot-filter.md; static RE docs/re_lootfilter_gamedll.md, 2026-08-29): 42 per-character
// LootFilterOption bits behind Player::Get/SetLootFilter (NO range check in the game -- clamped here), the
// factory reset, the game's own pass/hide predicate Item::PassLootFilter (what decides a floating label and the
// Pickup key), and Entity::GetVisibility -- a placed quest item the player already collected stays in the world
// as an entity with visibility 0 (QuestItem::InitialUpdate -> SetVisibility(GetQuestVisibility())), which is what
// made the mod present a "Strange Key" the player already had.
#include "gameapi.h"
#include "gameapi_internal.h"
#include <format>

namespace gd::gameapi {
using namespace gd::names;
using namespace gd::gameapi::detail;
namespace {
struct Api {
  bool (*GetLootFilter)(void*, int) = nullptr;
  void (*SetLootFilter)(void*, int, bool) = nullptr;
  void (*SetLootFilterDefaults)(void*) = nullptr;
  bool (*PassLootFilter)(const void*, int) = nullptr;   // Item, ItemIgnore (0 = apply the filter as configured)
  int (*GetVisibility)(const void*) = nullptr;          // Entity: the stored enum (0 = hidden, 3 = shown)
  bool (*IsExpansion3Loaded)(const void*) = nullptr;    // Engine
  void** gEngine = nullptr;
  bool loaded = false;
} g;
void load_loot() {
  if (g.loaded) return;
  g.loaded = true;
  GAPI_LOAD(g, GetLootFilter, Player_GetLootFilter);
  GAPI_LOAD(g, SetLootFilter, Player_SetLootFilter);
  GAPI_LOAD(g, SetLootFilterDefaults, Player_SetLootFilterDefaults);
  GAPI_LOAD(g, PassLootFilter, Item_PassLootFilter);
  GAPI_LOAD(g, GetVisibility, Entity_GetVisibility);
  GAPI_LOAD(g, IsExpansion3Loaded, Engine_IsExpansion3Loaded);
  GAPI_LOAD(g, gEngine, gEngine);
}
// The window's own order (four columns, ctor exe+0x1c7da0), each option's caption tag, its column and the
// factory default (Player::Player: 0..17 and 39 on). The English is the base game's text for the fallback only;
// the spoken label is the localized tag. Tag numbers are NOT index+1 past 17 (later additions were appended to
// the enum but numbered by window position); 39 is built only with expansion 3 loaded.
constexpr LootFilterOption kOptions[] = {
    {0, "tagLootFilter01", "Common", 0, true},          {1, "tagLootFilter02", "Magic", 0, true},
    {2, "tagLootFilter03", "Rare", 0, true},            {3, "tagLootFilter04", "Monster Infrequent", 0, true},
    {39, "tagLootFilter40", "Ascendant", 0, true},      {4, "tagLootFilter05", "Epic", 0, true},
    {5, "tagLootFilter06", "Legendary", 0, true},       {6, "tagLootFilter07", "Sets", 0, true},
    {7, "tagLootFilter08", "Always Show Uniques", 0, true}, {38, "tagLootFilter39", "Always Show Double Rare", 0, false},
    {8, "tagLootFilter09", "1h Melee", 1, true},        {9, "tagLootFilter10", "2h Melee", 1, true},
    {10, "tagLootFilter11", "1h Ranged", 1, true},      {11, "tagLootFilter12", "2h Ranged", 1, true},
    {12, "tagLootFilter13", "Dagger/Scepter", 1, true}, {13, "tagLootFilter14", "Caster Off-Hand", 1, true},
    {14, "tagLootFilter15", "Shield", 1, true},         {15, "tagLootFilter16", "Armor", 1, true},
    {16, "tagLootFilter17", "Accessories", 1, true},    {17, "tagLootFilter38", "Components", 1, true},
    {18, "tagLootFilter18", "Physical", 2, false},      {19, "tagLootFilter19", "Pierce", 2, false},
    {20, "tagLootFilter20", "Fire", 2, false},          {21, "tagLootFilter21", "Cold", 2, false},
    {22, "tagLootFilter22", "Lightning", 2, false},     {23, "tagLootFilter23", "Acid", 2, false},
    {24, "tagLootFilter24", "Vitality", 2, false},      {25, "tagLootFilter25", "Aether", 2, false},
    {26, "tagLootFilter26", "Chaos", 2, false},         {27, "tagLootFilter27", "Bleed", 2, false},
    {28, "tagLootFilter28", "Pet Bonuses", 2, false},
    {29, "tagLootFilter29", "My Masteries", 3, false},  {30, "tagLootFilter30", "Other Masteries", 3, false},
    {31, "tagLootFilter31", "Speed", 3, false},         {32, "tagLootFilter32", "Cooldown Reduction", 3, false},
    {33, "tagLootFilter33", "Crit Damage", 3, false},   {34, "tagLootFilter34", "Offensive Ability", 3, false},
    {35, "tagLootFilter35", "Defensive Ability", 3, false}, {40, "tagLootFilter41", "Health", 3, false},
    {41, "tagLootFilter42", "Health Regeneration", 3, false}, {36, "tagLootFilter36", "Resistances", 3, false},
    {37, "tagLootFilter37", "Retaliation", 3, false},
};
constexpr const char* kColumnTags[kLootFilterColumns] = {"tagLootFilterTitle01", "tagLootFilterTitle02", "tagLootFilterTitle03", "tagLootFilterTitle04"};
constexpr const char* kColumnFallback[kLootFilterColumns] = {"Quality", "Type", "Damage", "Character"};
bool expansion3() {
  load_loot();
  void* eng = g.gEngine ? rdp(g.gEngine, 0) : nullptr;
  bool v = false;
  if (!eng || !g.IsExpansion3Loaded) return false;
  guarded("IsExpansion3Loaded", [&] { v = g.IsExpansion3Loaded(eng); });
  return v;
}
}  // namespace

const std::vector<LootFilterOption>& loot_filter_options() {
  static std::vector<LootFilterOption> all, base;
  if (all.empty()) for (const LootFilterOption& o : kOptions) { all.push_back(o); if (o.index != 39) base.push_back(o); }
  return expansion3() ? all : base;
}
const char* loot_filter_column_tag(int column) { return column >= 0 && column < kLootFilterColumns ? kColumnTags[column] : ""; }
const char* loot_filter_column_fallback(int column) { return column >= 0 && column < kLootFilterColumns ? kColumnFallback[column] : ""; }

bool loot_filter(int option) {
  load_loot();
  void* p = player();
  bool v = false;
  if (!p || !g.GetLootFilter || option < 0 || option >= kLootFilterOptions) return false;
  guarded("GetLootFilter", [&] { v = g.GetLootFilter(p, option); });
  return v;
}
bool set_loot_filter(int option, bool on) {
  load_loot();
  void* p = player();
  if (!p || !g.SetLootFilter || option < 0 || option >= kLootFilterOptions) return false;
  return guarded("SetLootFilter", [&] { g.SetLootFilter(p, option, on); });
}
bool loot_filter_defaults(int column) {
  load_loot();
  void* p = player();
  if (!p) return false;
  if (column < 0) return g.SetLootFilterDefaults && guarded("SetLootFilterDefaults", [&] { g.SetLootFilterDefaults(p); });
  bool ok = true;
  for (const LootFilterOption& o : loot_filter_options()) if (o.column == column) ok = set_loot_filter(o.index, o.default_on) && ok;
  return ok;
}
bool item_passes_loot_filter(const void* item) {
  load_loot();
  bool v = true;
  if (!item || !g.PassLootFilter) return true;   // no predicate: never hide anything
  if (!guarded("PassLootFilter", [&] { v = g.PassLootFilter(item, 0); })) return true;
  return v;
}
bool entity_hidden(const void* e) {
  load_loot();
  int v = 3;
  if (!e || !g.GetVisibility) return false;
  if (!guarded("GetVisibility", [&] { v = g.GetVisibility(e); })) return false;
  return v == 0;
}
std::string dump_loot_filter() {
  std::string out = std::format("expansion3={} options={}\n", expansion3(), loot_filter_options().size());
  for (const LootFilterOption& o : loot_filter_options())
    out += std::format("  col={} opt={:2} {}={} tag={} '{}' default={}\n", o.column, o.index, o.index, loot_filter(o.index) ? "on " : "off", o.tag, o.fallback, o.default_on);
  return out;
}
}  // namespace gd::gameapi
