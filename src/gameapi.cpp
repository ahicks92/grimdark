#include "gameapi.h"
#include "gameapi_internal.h"
#include <format>
#include <unordered_map>
#include "hooks.h"
#include "world.h"

namespace gd::gameapi {
using namespace gd::names;
using namespace gd::gameapi::detail;
namespace {
struct Api {
  void** gGameEngine = nullptr;
  void* (*Loc_Instance)() = nullptr;
  const char16_t* (*Loc_LocalizeWithoutParams)(void*, const char*) = nullptr;
  void* (*GetMainPlayer)(const void*) = nullptr;
  void* (*ObjectManager_Get)() = nullptr;
  void (*ObjectManager_GetObjectList)(const void*, MemVec*) = nullptr;
  bool (*ObjectManager_IsObjectIdOnDeletedList)(void*, unsigned) = nullptr;
  unsigned (*Object_GetObjectId)(const void*) = nullptr;
  const char* (*Object_GetObjectName)(const void*) = nullptr;
  const MemVec* (*GetObjectives)(const void*) = nullptr;
  // quests
  void* (*Quest2Repository_Get)() = nullptr;
  void (*Quest2Repository_GetQuests)(void*, MemVec*, int) = nullptr;
  const MsvcStringW* (*Quest2_GetName)(const void*) = nullptr;
  const MsvcStringW* (*Quest2_GetGroup)(const void*) = nullptr;
  unsigned (*Quest2_GetId)(const void*) = nullptr;
  bool (*Quest2_IsTracked)(const void*) = nullptr;
  void (*Quest2_SetTracked)(void*, bool) = nullptr;
  bool (*Quest2_IsComplete)(const void*, bool) = nullptr;
  bool (*Quest2_InProgress)(const void*, bool) = nullptr;
  unsigned (*Quest2_GetNumTasks)(const void*) = nullptr;
  void* (*Quest2_GetTaskByIndex)(const void*, int) = nullptr;
  const MsvcStringW* (*Task_GetName)(const void*) = nullptr;
  const MsvcStringW* (*Task_GetDescription)(const void*) = nullptr;
  int (*Task_GetState)(const void*) = nullptr;
  const MemVec* (*Task_GetObjectives)(const void*) = nullptr;
  const MemVec* (*Task_GetRewards)(const void*) = nullptr;
  void (*Objective_GetText)(const void*, MsvcStringW*) = nullptr;
  int (*Objective_IsSatisfied)(const void*) = nullptr;
  void (*Event_GetText)(const void*, MsvcStringW*) = nullptr;
  unsigned (*Task_GetUid)(const void*) = nullptr;
  const MsvcStringA* (*Quest2_GetFileName)(const void*) = nullptr;
  void (*Quest2Repository_CompleteQuestTask)(void*, const MsvcStringA*, unsigned) = nullptr;   // dev: the game's own task completion
  // factions
  const void* (*GetFactionPack)(const void*) = nullptr;
  MsvcStringA* (*FactionPack_GetFactionTag)(MsvcStringA*, int) = nullptr;   // static, std::string by value: hidden pointer FIRST
  float (*FactionPack_GetValue)(const void*, int) = nullptr;
  bool (*FactionPack_IsUnlocked)(const void*, int) = nullptr;
  bool (*IsFactionPlayerVisible)(void*, int) = nullptr;   // a hardcoded 7-entry base-game whitelist: false for every DLC faction (not what the window uses)
  bool (*FactionPack_IsModified)(const void*, int) = nullptr;   // byte[pack+0xc4+type]: the player has touched this faction
  bool (*IsStartingFaction)(const void*, int) = nullptr;        // gamefactions.dbr startingFactions
  bool (*IsHiddenFaction)(const void*, int) = nullptr;
  void (*GetFactionLevelName)(const void*, float, MsvcStringW*) = nullptr;
  void (*GetFactionLevelBounds)(const void*, float, int*, int*) = nullptr;
  int (*FactionValueToLevel)(const void*, float) = nullptr;
  // hot slots
  void* (*GetPlayerHotSlotCtrl)(void*) = nullptr;
  void* (*HS_GetHotSlotOption)(const void*, unsigned) = nullptr;
  int (*HS_GetSlotStatus)(const void*, unsigned) = nullptr;
  void* (*HS_GetPrimarySlot)(const void*) = nullptr;
  void* (*HS_GetSecondarySlot)(const void*) = nullptr;
  void* (*HS_GetHealthPotionSlot)(const void*) = nullptr;
  void* (*HS_GetManaPotionSlot)(const void*) = nullptr;
  void (*HS_SetHotSlot)(void*, unsigned, const void*) = nullptr;
  void (*HS_SetPrimarySkillId)(void*, unsigned) = nullptr;
  void (*HS_SetSecondarySkillId)(void*, unsigned) = nullptr;
  void (*HS_SetPrimarySlot)(void*, const void*) = nullptr;     // what the HUD's drop does (slot 10/11 by weapon config)
  void (*HS_SetSecondarySlot)(void*, const void*) = nullptr;   // slot 12/13
  unsigned (*HS_GetDisplayedSkillSetIndex)(const void*) = nullptr;
  void (*HS_ActivateHotSlot)(void*, unsigned, bool, bool) = nullptr;
  void (*Opt_GetDisplayName)(const void*, MsvcStringW*, bool) = nullptr;   // virtual (slot via the vftable)
  void (*Opt_GetRolloverText)(const void*, MemVec*) = nullptr;             // virtual
  int (*Opt_GetType)(const void*) = nullptr;
  unsigned (*Opt_GetSkillId)(const void*) = nullptr;                      // virtual
  int (*Opt_GetCooldownRemaining)(void*) = nullptr;                       // virtual
  void (*Opt_SetPlayer)(void*, void*) = nullptr;
  void** HotSlotOption_vftable = nullptr;
  void** HotSlotOptionSkill_vftable = nullptr;
  int (*OptSkill_GetCooldownRemaining)(void*) = nullptr;
  void* (*HotSlotOptionSkill_ctor)(void*, unsigned) = nullptr;
  // lua (dev)
  void** gEngine = nullptr;
  bool (*Lua_RunCode)(void*, const char*) = nullptr;
  // lore
  const MemVec* (*GetLoreCodex)(const void*) = nullptr;
  const MsvcStringA* (*Note_GetCodexTitleTag)(const void*) = nullptr;
  const MsvcStringA* (*Note_GetCodexSubHeadingTag)(const void*) = nullptr;
  const MsvcStringA* (*Item_GetItemTextTag)(const void*) = nullptr;
  bool loaded = false;
} g;
int g_slot_display_name = -1, g_slot_rollover = -1, g_slot_skill_id = -1, g_slot_cooldown = -1;

// ---- objects by id ----
std::unordered_map<unsigned, void*> g_objects;
uint64_t g_objects_frame = 0;
bool g_objects_valid = false;
unsigned g_sweeps = 0;
size_t g_last_count = 0;

void sweep_objects() {
  g_objects.clear();
  g_objects_valid = false;
  if (!g.ObjectManager_Get || !g.ObjectManager_GetObjectList || !g.Object_GetObjectId) return;
  void* om = g.ObjectManager_Get();
  if (!om) return;
  // GetObjectList reserves size + [om+0x40] only when the capacity is short (Engine.dll+0x17ef30): a buffer
  // sized from that count keeps the whole sweep in our memory.
  size_t count = rd_or<size_t>(om, 0x40, 0);
  if (count > (1u << 20)) { log::writef("gameapi: implausible object count {}", count); return; }
  VecBuffer<void*> buf(count + 1024);
  guarded("ObjectManager::GetObjectList", [&] { g.ObjectManager_GetObjectList(om, buf.vec()); });
  std::vector<void*> objs = buf.take("GetObjectList");
  g_objects.reserve(objs.size());
  guarded("Object::GetObjectId sweep", [&] { for (void* o : objs) if (o) g_objects[g.Object_GetObjectId(o)] = o; });
  g_objects_valid = true;
  g_objects_frame = hooks::frame();
  g_last_count = objs.size();
  ++g_sweeps;
}
}  // namespace

void load() {
  if (g.loaded) return;
  g.loaded = true;
  GAPI_LOAD(g, gGameEngine, gGameEngine);
  GAPI_LOAD(g, Loc_Instance, LocalizationManager_Instance);
  GAPI_LOAD(g, Loc_LocalizeWithoutParams, LocalizationManager_LocalizeWithoutParams);
  GAPI_LOAD(g, GetMainPlayer, GameEngine_GetMainPlayer);
  GAPI_LOAD(g, ObjectManager_Get, ObjectManager_Get);
  GAPI_LOAD(g, ObjectManager_GetObjectList, ObjectManager_GetObjectList);
  GAPI_LOAD(g, ObjectManager_IsObjectIdOnDeletedList, ObjectManager_IsObjectIdOnDeletedList);
  GAPI_LOAD(g, Object_GetObjectId, Object_GetObjectId);
  GAPI_LOAD(g, Object_GetObjectName, Object_GetObjectName);
  GAPI_LOAD(g, GetObjectives, GameEngine_GetObjectives);
  GAPI_LOAD(g, Quest2Repository_Get, Quest2Repository_Get);
  GAPI_LOAD(g, Quest2Repository_GetQuests, Quest2Repository_GetQuests);
  GAPI_LOAD(g, Quest2_GetName, Quest2_GetName);
  GAPI_LOAD(g, Quest2_GetGroup, Quest2_GetGroup);
  GAPI_LOAD(g, Quest2_GetId, Quest2_GetId);
  GAPI_LOAD(g, Quest2_IsTracked, Quest2_IsTracked);
  GAPI_LOAD(g, Quest2_SetTracked, Quest2_SetTracked);
  GAPI_LOAD(g, Quest2_IsComplete, Quest2_IsComplete);
  GAPI_LOAD(g, Quest2_InProgress, Quest2_InProgress);
  GAPI_LOAD(g, Quest2_GetNumTasks, Quest2_GetNumTasks);
  GAPI_LOAD(g, Quest2_GetTaskByIndex, Quest2_GetTaskByIndex);
  GAPI_LOAD(g, Task_GetName, Quest2Task_GetName);
  GAPI_LOAD(g, Task_GetDescription, Quest2Task_GetDescription);
  GAPI_LOAD(g, Task_GetState, Quest2Task_GetState);
  GAPI_LOAD(g, Task_GetObjectives, Quest2Task_GetObjectives);
  GAPI_LOAD(g, Task_GetRewards, Quest2Task_GetRewards);
  GAPI_LOAD(g, Objective_GetText, Quest2Objective_GetText);
  GAPI_LOAD(g, Objective_IsSatisfied, Quest2Objective_IsSatisfied);
  GAPI_LOAD(g, Event_GetText, Quest2Event_GetText);
  GAPI_LOAD(g, Task_GetUid, Quest2Task_GetUid);
  GAPI_LOAD(g, Quest2_GetFileName, Quest2_GetFileName);
  GAPI_LOAD(g, Quest2Repository_CompleteQuestTask, Quest2Repository_CompleteQuestTask);
  GAPI_LOAD(g, GetFactionPack, Character_GetFactionPack);
  GAPI_LOAD(g, FactionPack_GetFactionTag, FactionPack_GetFactionTag);
  GAPI_LOAD(g, FactionPack_GetValue, FactionPack_GetValue);
  GAPI_LOAD(g, FactionPack_IsUnlocked, FactionPack_IsUnlocked);
  GAPI_LOAD(g, IsFactionPlayerVisible, GameEngine_IsFactionPlayerVisible);
  GAPI_LOAD(g, FactionPack_IsModified, FactionPack_IsModified);
  GAPI_LOAD(g, IsStartingFaction, GameEngine_IsStartingFaction);
  GAPI_LOAD(g, IsHiddenFaction, GameEngine_IsHiddenFaction);
  GAPI_LOAD(g, GetFactionLevelName, GameEngine_GetFactionLevelName);
  GAPI_LOAD(g, GetFactionLevelBounds, GameEngine_GetFactionLevelBounds);
  GAPI_LOAD(g, FactionValueToLevel, GameEngine_FactionValueToLevel);
  GAPI_LOAD(g, GetPlayerHotSlotCtrl, Player_GetPlayerHotSlotCtrl);
  GAPI_LOAD(g, HS_GetHotSlotOption, HotSlotCtrl_GetHotSlotOption);
  GAPI_LOAD(g, HS_GetSlotStatus, HotSlotCtrl_GetSlotStatus);
  GAPI_LOAD(g, HS_GetPrimarySlot, HotSlotCtrl_GetPrimarySlot);
  GAPI_LOAD(g, HS_GetSecondarySlot, HotSlotCtrl_GetSecondarySlot);
  GAPI_LOAD(g, HS_GetHealthPotionSlot, HotSlotCtrl_GetHealthPotionSlot);
  GAPI_LOAD(g, HS_GetManaPotionSlot, HotSlotCtrl_GetManaPotionSlot);
  GAPI_LOAD(g, HS_SetHotSlot, HotSlotCtrl_SetHotSlot);
  GAPI_LOAD(g, HS_SetPrimarySkillId, HotSlotCtrl_SetPrimarySkillId);
  GAPI_LOAD(g, HS_SetSecondarySkillId, HotSlotCtrl_SetSecondarySkillId);
  GAPI_LOAD(g, HS_SetPrimarySlot, HotSlotCtrl_SetPrimarySlot);
  GAPI_LOAD(g, HS_SetSecondarySlot, HotSlotCtrl_SetSecondarySlot);
  GAPI_LOAD(g, HS_GetDisplayedSkillSetIndex, HotSlotCtrl_GetDisplayedSkillSetIndex);
  GAPI_LOAD(g, HS_ActivateHotSlot, HotSlotCtrl_ActivateHotSlot);
  GAPI_LOAD(g, Opt_GetDisplayName, HotSlotOption_GetDisplayName);
  GAPI_LOAD(g, Opt_GetRolloverText, HotSlotOption_GetRolloverText);
  GAPI_LOAD(g, Opt_GetType, HotSlotOption_GetType);
  GAPI_LOAD(g, Opt_GetSkillId, HotSlotOption_GetSkillId);
  GAPI_LOAD(g, Opt_GetCooldownRemaining, HotSlotOption_GetCooldownRemaining);
  GAPI_LOAD(g, Opt_SetPlayer, HotSlotOption_SetPlayer);
  GAPI_LOAD(g, HotSlotOption_vftable, HotSlotOption_vftable);
  GAPI_LOAD(g, HotSlotOptionSkill_vftable, HotSlotOptionSkill_vftable);
  GAPI_LOAD(g, OptSkill_GetCooldownRemaining, HotSlotOptionSkill_GetCooldownRemaining);
  GAPI_LOAD(g, HotSlotOptionSkill_ctor, HotSlotOptionSkill_ctor);
  GAPI_LOAD(g, gEngine, gEngine);
  GAPI_LOAD(g, Lua_RunCode, LuaManager_RunCode);
  GAPI_LOAD(g, GetLoreCodex, Player_GetLoreCodex);
  GAPI_LOAD(g, Note_GetCodexTitleTag, ItemNote_GetCodexTitleTag);
  GAPI_LOAD(g, Note_GetCodexSubHeadingTag, ItemNote_GetCodexSubHeadingTag);
  GAPI_LOAD(g, Item_GetItemTextTag, Item_GetItemTextTag);
  g_slot_display_name = vslot(g.HotSlotOption_vftable, (const void*)g.Opt_GetDisplayName);
  g_slot_rollover = vslot(g.HotSlotOption_vftable, (const void*)g.Opt_GetRolloverText);
  // GetSkillId / GetCooldownRemaining / GetNumberAvailable / GetRolloverText share folded base bodies, so the
  // slots come from the 2026-08-22 vtable readout (HotSlotOption: +0x28 GetCooldownRemaining, +0x78
  // GetRolloverText, +0x80 GetDisplayName, +0xa0 GetSkillId), cross-checked where an export is unique.
  g_slot_skill_id = 0xa0 / 8;
  g_slot_rollover = 0x78 / 8;
  g_slot_cooldown = vslot(g.HotSlotOptionSkill_vftable, (const void*)g.OptSkill_GetCooldownRemaining);
  if (g_slot_cooldown < 0) g_slot_cooldown = 0x28 / 8;
  if (g_slot_display_name < 0) g_slot_display_name = 0x80 / 8;
  log::writef("gameapi: HotSlotOption slots name={} rollover={} skill={} cooldown={}", g_slot_display_name, g_slot_rollover, g_slot_skill_id, g_slot_cooldown);
}

void* engine() {
  void* e = g.gGameEngine ? rdp(g.gGameEngine, 0) : nullptr;
  return e ? e : world::game_engine();
}
void* player() { void* e = engine(); return e && g.GetMainPlayer ? g.GetMainPlayer(e) : nullptr; }
void* controller() { return world::controller(); }

std::string localize(const std::string& tag) {
  if (tag.empty() || !g.Loc_Instance || !g.Loc_LocalizeWithoutParams) return {};
  std::string out;
  guarded("LocalizeWithoutParams", [&] {
    void* lm = g.Loc_Instance();
    const char16_t* t = lm ? g.Loc_LocalizeWithoutParams(lm, tag.c_str()) : nullptr;
    if (t) out = textcap::speakable(std::u16string_view(t));
  });
  return out;
}

// ---- objects ----
// The cache holds POINTERS across frames (up to 600), which the "hold ids, re-resolve" rule forbids for a reason:
// an entity destroyed since the last sweep (the enemy just killed, a projectile, a summon) was still handed out,
// and entity_position -> GetCoords on the freed object -> GetWorldPosition on its garbage region crashed the game
// (2026-09-20 18:51). Every hit is now checked against the ObjectManager's own deleted list before it is returned.
void* object_by_id(unsigned id) {
  if (!id) return nullptr;
  uint64_t f = hooks::frame();
  if (!g_objects_valid || f - g_objects_frame > 600) sweep_objects();
  auto it = g_objects.find(id);
  if (it == g_objects.end() && f - g_objects_frame > 5) { sweep_objects(); it = g_objects.find(id); }  // a miss re-sweeps (rate-limited)
  if (it == g_objects.end()) return nullptr;
  bool deleted = false;
  if (g.ObjectManager_Get && g.ObjectManager_IsObjectIdOnDeletedList)
    guarded("ObjectManager::IsObjectIdOnDeletedList", [&] { void* om = g.ObjectManager_Get(); if (om) deleted = g.ObjectManager_IsObjectIdOnDeletedList(om, id); });
  if (deleted) { g_objects.erase(it); return nullptr; }
  return it->second;
}
void invalidate_objects() { g_objects_valid = false; }
unsigned object_id(const void* o) { unsigned id = 0; if (o && g.Object_GetObjectId) guarded("GetObjectId", [&] { id = g.Object_GetObjectId(o); }); return id; }
std::string object_record(const void* o) {
  std::string r;
  if (o && g.Object_GetObjectName) guarded("GetObjectName", [&] { const char* n = g.Object_GetObjectName(o); if (n) r = n; });
  return r;
}
std::string dump_objects_stats() { return std::format("objects: {} ids, sweeps {}, last count {}, valid {}, frame {}\n", g_objects.size(), g_sweeps, g_last_count, g_objects_valid, g_objects_frame); }
// Dev: every object whose record path contains `needle`, with its world position when it is a placed entity.
std::string find_objects(const std::string& needle, size_t max) {
  sweep_objects();
  std::string out;
  size_t n = 0;
  for (const auto& [id, o] : g_objects) {
    std::string rec = object_record(o);
    if (rec.find(needle) == std::string::npos) continue;
    world::Vec3 p;
    bool has = world::entity_position(id, p);
    out += has ? std::format("id={} at ({:.1f}, {:.1f}, {:.1f}) '{}'\n", id, p.x, p.y, p.z, rec) : std::format("id={} (no position) '{}'\n", id, rec);
    if (++n >= max) { out += "...\n"; break; }
  }
  return out.empty() ? "no match\n" : out;
}
std::string dump_object(unsigned id) {
  void* o = object_by_id(id);
  if (!o) return std::format("object {}: not found\n", id);
  return std::format("object {}: {} record '{}'\n", id, o, object_record(o));
}

// ---- objectives ----
std::vector<std::string> objectives() {
  std::vector<std::string> out;
  void* e = engine();
  if (!e || !g.GetObjectives) return out;
  guarded("GetObjectives", [&] {
    const MemVec* v = g.GetObjectives(e);
    std::vector<MsvcStringA> items = vec_items<MsvcStringA>(v, 256);
    // Entries the game feeds its HUD tracker to LocalizationManager::Localize: a tag ("tag...") must be
    // resolved, already-resolved text passes through verbatim.
    for (const MsvcStringA& s : items) {
      std::string raw = a_text(&s);
      std::string t = raw.rfind("tag", 0) == 0 ? localize(raw) : std::string{};
      if (t.empty()) t = textcap::speakable(raw);
      if (!t.empty()) out.push_back(t);
    }
  });
  return out;
}

// ---- quests ----
std::vector<Quest> quests(int filter) {
  std::vector<Quest> out;
  if (!g.Quest2Repository_Get || !g.Quest2Repository_GetQuests) return out;
  void* repo = g.Quest2Repository_Get();
  if (!repo) return out;
  VecBuffer<void*> buf(4096);
  guarded("Quest2Repository::GetQuests", [&] { g.Quest2Repository_GetQuests(repo, buf.vec(), filter); });
  std::vector<void*> qs = buf.take("GetQuests");
  for (void* q : qs) {
    if (!q) continue;
    Quest info{q};
    bool ok = guarded("quest readout", [&] {
      info.id = g.Quest2_GetId(q);
      info.name = u16_text(g.Quest2_GetName(q));
      info.group = u16_text(g.Quest2_GetGroup(q));
      info.tracked = g.Quest2_IsTracked(q);
      info.complete = g.Quest2_IsComplete(q, false);
      info.in_progress = g.Quest2_InProgress(q, false);
      unsigned n = g.Quest2_GetNumTasks(q);
      for (unsigned i = 0; i < n && i < 64; ++i) {
        void* t = g.Quest2_GetTaskByIndex(q, (int)i);
        if (!t) continue;
        Task task{t};
        task.name = u16_text(g.Task_GetName(t));
        task.description = u16_text(g.Task_GetDescription(t));
        task.state = g.Task_GetState(t);
        task.uid = g.Task_GetUid ? g.Task_GetUid(t) : 0;
        task.reward_events = (int)vec_items<void*>(g.Task_GetRewards(t), 32).size();
        for (void* ob : vec_items<void*>(g.Task_GetObjectives(t), 64)) {
          if (!ob) continue;
          MsvcStringW s; init_u16(s);
          g.Objective_GetText(ob, &s);
          task.objectives.push_back({take_u16(s), g.Objective_IsSatisfied(ob)});
        }
        for (void* ev : vec_items<void*>(g.Task_GetRewards(t), 32)) {
          if (!ev) continue;
          MsvcStringW s; init_u16(s);
          g.Event_GetText(ev, &s);
          std::string text = take_u16(s);
          if (!text.empty()) task.rewards.push_back(text);
        }
        info.tasks.push_back(std::move(task));
      }
    });
    if (ok) out.push_back(std::move(info));
  }
  return out;
}
bool set_quest_tracked(void* quest, bool on) {
  if (!quest || !g.Quest2_SetTracked) return false;
  return guarded("SetTracked", [&] { g.Quest2_SetTracked(quest, on); });
}
// Dev: complete a task the way the game's quest command does (Quest2Repository::CompleteQuestTask -> GetTaskByUid ->
// Quest2Task::Complete: rewards run, the quest-completed event fires, the reward window opens). `quest` is a Quest2*
// from /quests; task_index counts the quest's tasks from 0. Game thread.
bool complete_quest_task(void* quest, int task_index) {
  if (!quest || !g.Quest2Repository_Get || !g.Quest2Repository_CompleteQuestTask || !g.Quest2_GetFileName || !g.Quest2_GetTaskByIndex || !g.Task_GetUid) return false;
  return guarded("CompleteQuestTask", [&] {
    void* t = g.Quest2_GetTaskByIndex(quest, task_index);
    if (!t) return;
    unsigned uid = g.Task_GetUid(t);
    const MsvcStringA* file = g.Quest2_GetFileName(quest);
    void* repo = g.Quest2Repository_Get();
    if (repo && file) g.Quest2Repository_CompleteQuestTask(repo, file, uid);
  });
}
std::string dump_quests(int filter) {
  std::string out;
  for (const Quest& q : quests(filter)) {
    out += std::format("quest {} id={} '{}' group='{}' tracked={} complete={} inprogress={} tasks={}\n", q.p, q.id, q.name, q.group, q.tracked, q.complete, q.in_progress, q.tasks.size());
    for (const Task& t : q.tasks) {
      out += std::format("  task {} uid={} state={} reward_events={} '{}' desc='{}'\n", t.p, t.uid, t.state, t.reward_events, t.name, t.description.substr(0, 120));
      for (const Objective& o : t.objectives) out += std::format("    objective sat={} '{}'\n", o.satisfied, o.text);
      for (const std::string& r : t.rewards) out += std::format("    reward '{}'\n", r);
    }
  }
  if (out.empty()) out = "no quests\n";
  return out;
}

// ---- factions ----
std::vector<Faction> factions() {
  std::vector<Faction> out;
  void* p = player(); void* e = engine();
  if (!p || !e || !g.GetFactionPack || !g.FactionPack_GetFactionTag) return out;
  guarded("factions", [&] {
    const void* pack = g.GetFactionPack(p);
    if (!pack) return;
    for (int t = -3; t <= 46; ++t) {   // FactionType runs -3..46 (the 50-entry jump table in GetFactionTag)
      // The game's own window (exe+0x1c1ba0) shows a faction iff the player has touched it or it is a starting
      // faction, and it is not hidden. NOT GameEngine::IsFactionPlayerVisible: that export is a hardcoded
      // base-game whitelist (types 1, 6, 8, 10, 11, 13, 14) and says no to every expansion faction (User9..User16).
      if (g.FactionPack_IsModified && g.IsStartingFaction && g.IsHiddenFaction) {
        if (!g.FactionPack_IsModified(pack, t) && !g.IsStartingFaction(e, t)) continue;
        if (g.IsHiddenFaction(e, t)) continue;
      } else if (g.IsFactionPlayerVisible && !g.IsFactionPlayerVisible(e, t)) continue;
      MsvcStringA tag; init_a(tag);
      g.FactionPack_GetFactionTag(&tag, t);
      Faction f{t, take_a(tag)};
      if (f.tag.empty()) continue;
      f.name = localize(f.tag);
      f.value = g.FactionPack_GetValue ? g.FactionPack_GetValue(pack, t) : 0.0f;
      f.unlocked = g.FactionPack_IsUnlocked ? g.FactionPack_IsUnlocked(pack, t) : true;
      f.level = g.FactionValueToLevel ? g.FactionValueToLevel(e, f.value) : 0;
      if (g.GetFactionLevelName) { MsvcStringW s; init_u16(s); g.GetFactionLevelName(e, f.value, &s); f.level_name = take_u16(s); }
      if (g.GetFactionLevelBounds) g.GetFactionLevelBounds(e, f.value, &f.low, &f.high);
      out.push_back(std::move(f));
    }
  });
  return out;
}
std::string dump_factions() {
  std::string out;
  for (const Faction& f : factions()) out += std::format("faction {} tag={} '{}' value={:.0f} level={} '{}' bounds {}..{} unlocked={}\n", f.type, f.tag, f.name, f.value, f.level, f.level_name, f.low, f.high, f.unlocked);
  return out.empty() ? "no factions\n" : out;
}

// ---- hot slots ----
namespace {
void* hotslot_ctrl() { void* p = player(); return p && g.GetPlayerHotSlotCtrl ? g.GetPlayerHotSlotCtrl(p) : nullptr; }
HotSlot read_option(unsigned index, void* opt, int status) {
  HotSlot s{index, {}, -1, 0, 0, status, true};
  if (!opt) return s;
  guarded("hotslot option", [&] {
    if (g.Opt_GetType) s.type = g.Opt_GetType(opt);
    if (auto f = (unsigned (*)(const void*))vfn(opt, g_slot_skill_id)) s.skill_id = f(opt);
    if (auto f = (int (*)(void*))vfn(opt, g_slot_cooldown)) s.cooldown_ms = f(opt);
    if (auto f = (void (*)(const void*, MsvcStringW*, bool))vfn(opt, g_slot_display_name)) { MsvcStringW n; init_u16(n); f(opt, &n, false); s.name = take_u16(n); }
    s.empty = s.name.empty() && s.skill_id == 0;
  });
  return s;
}
}  // namespace
HotSlot hotslot(unsigned index) {
  void* c = hotslot_ctrl();
  if (!c || !g.HS_GetHotSlotOption) return HotSlot{index, {}, -1, 0, 0, -1, true};
  void* opt = nullptr; int status = -1;
  guarded("GetHotSlotOption", [&] { opt = g.HS_GetHotSlotOption(c, index); if (g.HS_GetSlotStatus) status = g.HS_GetSlotStatus(c, index); });
  return read_option(index, opt, status);
}
std::vector<HotSlot> hotslots() {
  std::vector<HotSlot> out;
  if (!hotslot_ctrl()) return out;
  for (unsigned i = 0; i < kHotSlotCount; ++i) out.push_back(hotslot(i));
  return out;
}
HotSlot primary_slot() { void* c = hotslot_ctrl(); void* o = nullptr; if (c && g.HS_GetPrimarySlot) guarded("GetPrimarySlot", [&] { o = g.HS_GetPrimarySlot(c); }); return read_option(~0u, o, -1); }
HotSlot secondary_slot() { void* c = hotslot_ctrl(); void* o = nullptr; if (c && g.HS_GetSecondarySlot) guarded("GetSecondarySlot", [&] { o = g.HS_GetSecondarySlot(c); }); return read_option(~1u, o, -1); }
HotSlot health_potion_slot() { void* c = hotslot_ctrl(); void* o = nullptr; if (c && g.HS_GetHealthPotionSlot) guarded("GetHealthPotionSlot", [&] { o = g.HS_GetHealthPotionSlot(c); }); return read_option(~2u, o, -1); }
HotSlot mana_potion_slot() { void* c = hotslot_ctrl(); void* o = nullptr; if (c && g.HS_GetManaPotionSlot) guarded("GetManaPotionSlot", [&] { o = g.HS_GetManaPotionSlot(c); }); return read_option(~3u, o, -1); }
std::vector<std::string> hotslot_tooltip(unsigned index) {
  std::vector<std::string> out;
  void* c = hotslot_ctrl();
  if (!c || !g.HS_GetHotSlotOption) return out;
  TextLineBuffer buf;
  guarded("hotslot rollover", [&] {
    void* opt = g.HS_GetHotSlotOption(c, index);
    if (auto f = (void (*)(const void*, MemVec*))(opt ? vfn(opt, g_slot_rollover) : nullptr)) f(opt, buf.vec());
  });
  for (TextLine& l : buf.take("GetRolloverText")) out.push_back(std::move(l.text));
  return out;
}
bool assign_skill_to_slot(unsigned index, unsigned skill_id) {
  void* c = hotslot_ctrl(); void* p = player();
  if (!c || !p || !g.HS_SetHotSlot || !g.HotSlotOptionSkill_ctor || !g.Opt_SetPlayer) return false;
  // The option is built with the game's ctor in our memory; SetHotSlot deep-copies it (Game.dll+0x3d8da0,
  // 2026-08-22 readout), so our copy is freed afterwards.
  void* opt = calloc(1, 128);
  if (!opt) return false;
  bool ok = guarded("assign hot slot", [&] {
    g.HotSlotOptionSkill_ctor(opt, skill_id);
    g.Opt_SetPlayer(opt, p);
    g.HS_SetHotSlot(c, index, opt);
  });
  free(opt);
  log::writef("gameapi: assign skill {} to hot slot {} ok={}", skill_id, index, ok);
  return ok;
}
// The mouse slots take an option (SetPrimarySlot / SetSecondarySlot, which pick the weapon config's slot);
// SetPrimarySkillId did not change the slot when tried live (2026-08-22).
namespace {
bool set_mouse_slot(bool primary, unsigned skill_id) {
  void* c = hotslot_ctrl(); void* p = player();
  auto set = primary ? g.HS_SetPrimarySlot : g.HS_SetSecondarySlot;
  if (!c || !p || !set || !g.HotSlotOptionSkill_ctor || !g.Opt_SetPlayer) return false;
  void* opt = calloc(1, 128);
  if (!opt) return false;
  bool ok = guarded(primary ? "SetPrimarySlot" : "SetSecondarySlot", [&] { g.HotSlotOptionSkill_ctor(opt, skill_id); g.Opt_SetPlayer(opt, p); set(c, opt); });
  free(opt);
  log::writef("gameapi: set {} mouse slot to skill {} ok={}", primary ? "left" : "right", skill_id, ok);
  return ok;
}
}  // namespace
bool set_primary_skill(unsigned skill_id) { return set_mouse_slot(true, skill_id); }
bool set_secondary_skill(unsigned skill_id) { return set_mouse_slot(false, skill_id); }
bool activate_hotslot(unsigned index) { void* c = hotslot_ctrl(); return c && g.HS_ActivateHotSlot && guarded("ActivateHotSlot", [&] { g.HS_ActivateHotSlot(c, index, false, false); }); }
unsigned quickbar_slot_index(int bar, int k) {
  static const unsigned base[4] = {0, 14, 26, 36};
  if (bar < 0) bar = 0; if (bar > 3) bar = 3; if (k < 1) k = 1; if (k > 10) k = 10;
  return base[bar] + (unsigned)(k - 1);
}
namespace {
const char* aim_name(world::SkillAim a) {
  switch (a) {
    case world::SkillAim::SelfCast: return "self";
    case world::SkillAim::AroundYou: return "around";
    case world::SkillAim::AtPoint: return "point";
    case world::SkillAim::AtTarget: return "target";
    default: return "-";
  }
}
const char* slot_aim(unsigned skill_id) { return skill_id ? aim_name(world::skill_aim(object_by_id(skill_id))) : "-"; }
}  // namespace
unsigned displayed_skill_set() {
  void* c = hotslot_ctrl();
  unsigned set = 0; if (c && g.HS_GetDisplayedSkillSetIndex) guarded("GetDisplayedSkillSetIndex", [&] { set = g.HS_GetDisplayedSkillSetIndex(c); });
  return set;
}
std::string dump_hotslots() {
  std::string out;
  void* c = hotslot_ctrl();
  if (!c) return "no hot slot ctrl\n";
  unsigned set = displayed_skill_set();
  out += std::format("hotslot ctrl {} displayed set {}\n", c, set);
  for (const HotSlot& s : hotslots()) if (!s.empty || s.status != -1) out += std::format("  slot {:2} type={} skill={} aim={} cd={} status={} '{}'\n", s.index, s.type, s.skill_id, slot_aim(s.skill_id), s.cooldown_ms, s.status, s.name);
  HotSlot p = primary_slot(), q = secondary_slot();
  out += std::format("  primary: type={} skill={} aim={} '{}'\n  secondary: type={} skill={} aim={} '{}'\n", p.type, p.skill_id, slot_aim(p.skill_id), p.name, q.type, q.skill_id, slot_aim(q.skill_id), q.name);
  return out;
}

// ---- Lua (dev) ----
bool lua_run(const std::string& code) {
  load();
  void* eng = g.gEngine ? rdp(g.gEngine, 0) : nullptr;
  void* mgr = eng ? rdp(eng, 0x68) : nullptr;   // the LuaManager (GameEngine::PostLuaInitialize reads it here)
  if (!mgr || !g.Lua_RunCode) { log::writef("gameapi: lua: no manager (engine {} mgr {})", eng, mgr); return false; }
  bool r = false;
  bool ok = guarded("LuaManager::RunCode", [&] { r = g.Lua_RunCode(mgr, code.c_str()); });
  log::writef("gameapi: lua RunCode -> {} (call ok {}): {}", r, ok, code.substr(0, 120));
  return ok && r;
}

// ---- lore ----
std::vector<Note> lore_notes() {
  std::vector<Note> out;
  void* p = player();
  if (!p || !g.GetLoreCodex) return out;
  std::vector<unsigned> ids;
  guarded("GetLoreCodex", [&] { ids = vec_items<unsigned>(g.GetLoreCodex(p), 4096); });
  for (unsigned id : ids) {
    Note n{id, object_by_id(id)};
    if (n.p && g.Note_GetCodexTitleTag) guarded("note tags", [&] {
      n.title = localize(a_text(g.Note_GetCodexTitleTag(n.p)));
      if (g.Note_GetCodexSubHeadingTag) n.heading = localize(a_text(g.Note_GetCodexSubHeadingTag(n.p)));
    });
    out.push_back(std::move(n));
  }
  return out;
}
std::vector<std::string> note_text(void* note) { return item_tooltip(note, false); }
std::vector<std::string> note_full_text(void* note) {
  std::vector<std::string> out;
  if (!note || !g.Item_GetItemTextTag) return out;
  std::string tag;
  guarded("GetItemTextTag", [&] { tag = a_text(g.Item_GetItemTextTag(note)); });
  if (tag.empty()) return out;
  std::string text = localize(tag);
  if (text.empty()) return out;
  // The tag text uses the game's markup: {^n} / ^n are line breaks; other ^x codes are colors -- drop them.
  std::string line;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '{' && i + 3 < text.size() && text[i + 1] == '^' && text[i + 3] == '}') { i += 3; if (tolower(text[i - 1]) == 'n') { if (!line.empty()) out.push_back(std::move(line)); line.clear(); } continue; }
    if (text[i] == '^' && i + 1 < text.size()) { ++i; if (tolower(text[i]) == 'n') { if (!line.empty()) out.push_back(std::move(line)); line.clear(); } continue; }
    line += text[i];
  }
  if (!line.empty()) out.push_back(std::move(line));
  return out;
}
std::string dump_lore() {
  std::string out;
  for (const Note& n : lore_notes()) out += std::format("note id={} {} '{}' heading='{}'\n", n.id, n.p, n.title, n.heading);
  return out.empty() ? "no lore notes\n" : out;
}
}  // namespace gd::gameapi
