// Devotion: constellations, stars, affinities and celestial powers (docs/devotion.md; static RE in
// docs/re_devotion_{data,gamedll,exe}.md). The GRAPH (which stars form which constellation, their links,
// requirements and bonuses) is the exe's own object graph, read by exe_ui::devotion_constellations; every
// STATE read and every ACTION is a Game.dll export -- the same calls the devotion window's click handler makes
// (Star::HandleMouseEvent exe+0x17ea10, BindCelestialPower exe+0x1867f0). Spends apply immediately; the game's
// window merely holds the autosave while open, so nothing here needs it shown. Reclaiming (a Tonic of Clarity)
// is not modelled.
#include "gameapi.h"
#include "gameapi_internal.h"
#include <deque>
#include <format>
#include <set>
#include "core/message_builder.h"
#include "core/strings.h"
#include "exe_ui.h"

namespace gd::gameapi {
using namespace gd::names;
using namespace gd::gameapi::detail;
namespace {
struct Api {
  unsigned (*GetDevotionPoints)(const void*) = nullptr;
  unsigned (*GetTotalDevotionPoints)(const void*) = nullptr;
  unsigned (*GetMaxDevotionPoints)(const void*) = nullptr;
  void (*SubtractDevotionPoint)(void*) = nullptr;
  void (*AddDevotionPoints)(void*, unsigned) = nullptr;
  void (*AddTotalDevotionPoints)(void*, unsigned) = nullptr;
  unsigned (*GetAffinity)(const void*, int) = nullptr;
  void (*AddAffinity)(void*, int, unsigned) = nullptr;
  void (*SubtractAffinity)(void*, int, unsigned) = nullptr;
  unsigned (*SM_GetCurrentDevotionReclamationCost)(const void*) = nullptr;
  unsigned (*SM_GetDevotionReclamationAetherCost)(const void*) = nullptr;
  bool (*SM_UseDevotionReclamationPoints)(void*, int) = nullptr;
  unsigned (*Player_GetCurrentAether)(const void*) = nullptr;
  void (*Player_AddAether)(void*, unsigned) = nullptr;
  void (*Skill_SetSkillLevel)(void*, unsigned) = nullptr;           // virtual
  unsigned (*Skill_GetCurrentLevel)(const void*) = nullptr;         // virtual (dev dump)
  const MemVec* (*Char_GetSkillList)(const void*) = nullptr;
  const MemVec* (*Char_GetItemSkillList)(const void*) = nullptr;
  const MemVec* (*Char_GetItemSkillCache)(const void*) = nullptr;
  const void* (*GetSkillManager)(const void*) = nullptr;
  void (*SM_GetSkillMasteryIds)(const void*, MemVec*) = nullptr;
  bool (*SM_IsDefaultSkill)(const void*, unsigned) = nullptr;
  unsigned (*SM_GetNumDevotionPointsSpent)(const void*) = nullptr;
  unsigned (*Skill_GetSkillLevel)(const void*) = nullptr;
  unsigned (*Skill_GetMaxLevel)(const void*) = nullptr;
  unsigned (*Skill_GetDevotionLevel)(const void*) = nullptr;
  unsigned (*Skill_GetDevotionMaxLevel)(const void*) = nullptr;
  unsigned (*Skill_GetDevotionExperience)(const void*) = nullptr;
  unsigned (*Skill_GetRequiredExperience)(const void*, unsigned) = nullptr;
  unsigned (*Skill_GetDevotionParent)(const void*) = nullptr;
  void (*Skill_SetDevotionParent)(void*, unsigned) = nullptr;
  void (*Skill_IncrementDevotionLevel)(void*) = nullptr;            // virtual
  void (*Skill_IncrementSkillLevel)(void*, unsigned) = nullptr;     // virtual
  void (*Skill_SetAutocastSkill)(void*, void*, const MsvcStringA*, bool) = nullptr;   // virtual
  MsvcStringA* (*Skill_GetTemplateAutoCast)(const void*, MsvcStringA*) = nullptr;     // std::string by value (hidden pointer)
  void* (*Skill_GetAutoCastSkill)(const void*) = nullptr;
  bool (*Skill_HasAutocastSkill)(const void*) = nullptr;
  bool (*Skill_HasAutocastInDbr)(const void*) = nullptr;
  int (*Skill_GetSkillOperation)(const void*) = nullptr;
  bool (*Skill_IsSkillA)(const void*, const void*) = nullptr;
  bool (*Skill_IsSkillBlackListed)(const void*, MsvcStringA) = nullptr;   // std::string by value
  bool (*Skill_IsSkillTheMasterySkill)(const void*) = nullptr;
  bool (*Skill_IsSkillModifier)(const void*) = nullptr;
  unsigned (*Skill_GetMasteryId)(const void*) = nullptr;
  void** Skill_vftable = nullptr;
  unsigned (*Object_GetObjectId)(const void*) = nullptr;
  void (*GenerateUIDevotionText)(const void*, const void*, MemVec*, const void*, bool, bool, int, int, int) = nullptr;
  bool loaded = false;
} g;
int g_slot_inc_dev = -1, g_slot_inc = -1, g_slot_autocast = -1, g_slot_setlvl = -1, g_slot_curlvl = -1;
constexpr int kOpPower = 3;   // Skill_Operation: 0 ordinary skill, 1 "Skill", 2 "Passive" (a plain star), 3 "Effect" (a celestial power)

void load_devotion() {
  if (g.loaded) return;
  g.loaded = true;
  GAPI_LOAD(g, GetDevotionPoints, Character_GetDevotionPoints);
  GAPI_LOAD(g, GetTotalDevotionPoints, Character_GetTotalDevotionPoints);
  GAPI_LOAD(g, GetMaxDevotionPoints, Character_GetMaxDevotionPoints);
  GAPI_LOAD(g, SubtractDevotionPoint, Character_SubtractDevotionPoint);
  GAPI_LOAD(g, AddDevotionPoints, Character_AddDevotionPoints);
  GAPI_LOAD(g, AddTotalDevotionPoints, Character_AddTotalDevotionPoints);
  GAPI_LOAD(g, GetAffinity, Character_GetAffinity);
  GAPI_LOAD(g, AddAffinity, Character_AddAffinity);
  GAPI_LOAD(g, SubtractAffinity, Character_SubtractAffinity);
  GAPI_LOAD(g, SM_GetCurrentDevotionReclamationCost, SkillManager_GetCurrentDevotionReclamationCost);
  GAPI_LOAD(g, SM_GetDevotionReclamationAetherCost, SkillManager_GetDevotionReclamationAetherCost);
  GAPI_LOAD(g, SM_UseDevotionReclamationPoints, SkillManager_UseDevotionReclamationPoints);
  GAPI_LOAD(g, Player_GetCurrentAether, Player_GetCurrentAether);
  GAPI_LOAD(g, Player_AddAether, Player_AddAether);
  GAPI_LOAD(g, Skill_SetSkillLevel, Skill_SetSkillLevel);
  GAPI_LOAD(g, Skill_GetCurrentLevel, Skill_GetCurrentLevel);
  GAPI_LOAD(g, Char_GetSkillList, Character_GetSkillList);
  GAPI_LOAD(g, Char_GetItemSkillList, Character_GetItemSkillList);
  GAPI_LOAD(g, Char_GetItemSkillCache, Character_GetItemSkillCache);
  GAPI_LOAD(g, GetSkillManager, Character_GetSkillManager);
  GAPI_LOAD(g, SM_GetSkillMasteryIds, SkillManager_GetSkillMasteryIds);
  GAPI_LOAD(g, SM_IsDefaultSkill, SkillManager_IsDefaultSkill);
  GAPI_LOAD(g, SM_GetNumDevotionPointsSpent, SkillManager_GetNumDevotionPointsSpent);
  GAPI_LOAD(g, Skill_GetSkillLevel, Skill_GetSkillLevel);
  GAPI_LOAD(g, Skill_GetMaxLevel, Skill_GetMaxLevel);
  GAPI_LOAD(g, Skill_GetDevotionLevel, Skill_GetDevotionLevel);
  GAPI_LOAD(g, Skill_GetDevotionMaxLevel, Skill_GetDevotionMaxLevel);
  GAPI_LOAD(g, Skill_GetDevotionExperience, Skill_GetDevotionExperience);
  GAPI_LOAD(g, Skill_GetRequiredExperience, Skill_GetRequiredExperience);
  GAPI_LOAD(g, Skill_GetDevotionParent, Skill_GetDevotionParent);
  GAPI_LOAD(g, Skill_SetDevotionParent, Skill_SetDevotionParent);
  GAPI_LOAD(g, Skill_IncrementDevotionLevel, Skill_IncrementDevotionLevel);
  GAPI_LOAD(g, Skill_IncrementSkillLevel, Skill_IncrementSkillLevel);
  GAPI_LOAD(g, Skill_SetAutocastSkill, Skill_SetAutocastSkill);
  GAPI_LOAD(g, Skill_GetTemplateAutoCast, Skill_GetTemplateAutoCast);
  GAPI_LOAD(g, Skill_GetAutoCastSkill, Skill_GetAutoCastSkill);
  GAPI_LOAD(g, Skill_HasAutocastSkill, Skill_HasAutocastSkill);
  GAPI_LOAD(g, Skill_HasAutocastInDbr, Skill_HasAutocastInDbr);
  GAPI_LOAD(g, Skill_GetSkillOperation, Skill_GetSkillOperation);
  GAPI_LOAD(g, Skill_IsSkillA, Skill_IsSkillA);
  GAPI_LOAD(g, Skill_IsSkillBlackListed, Skill_IsSkillBlackListed);
  GAPI_LOAD(g, Skill_IsSkillTheMasterySkill, Skill_IsSkillTheMasterySkill);
  GAPI_LOAD(g, Skill_IsSkillModifier, Skill_IsSkillModifier);
  GAPI_LOAD(g, Skill_GetMasteryId, Skill_GetMasteryId);
  GAPI_LOAD(g, Skill_vftable, Skill_vftable);
  GAPI_LOAD(g, Object_GetObjectId, Object_GetObjectId);
  GAPI_LOAD(g, GenerateUIDevotionText, GameEngine_GenerateUIDevotionText);
  // The exe dispatches these virtually (a subclass may override); resolve the slots from the exported base.
  g_slot_inc_dev = vslot(g.Skill_vftable, (const void*)g.Skill_IncrementDevotionLevel);
  g_slot_inc = vslot(g.Skill_vftable, (const void*)g.Skill_IncrementSkillLevel);
  g_slot_autocast = vslot(g.Skill_vftable, (const void*)g.Skill_SetAutocastSkill);
  g_slot_setlvl = vslot(g.Skill_vftable, (const void*)g.Skill_SetSkillLevel);
  g_slot_curlvl = vslot(g.Skill_vftable, (const void*)g.Skill_GetCurrentLevel);
  log::writef("gameapi: devotion slots inc_dev={} inc={} autocast={} setlvl={}", g_slot_inc_dev, g_slot_inc, g_slot_autocast, g_slot_setlvl);
}
unsigned level_of(const void* s) { unsigned n = 0; if (s && g.Skill_GetSkillLevel) guarded("GetSkillLevel", [&] { n = g.Skill_GetSkillLevel(s); }); return n; }
unsigned obj_id(const void* o) { unsigned n = 0; if (o && g.Object_GetObjectId) guarded("GetObjectId", [&] { n = g.Object_GetObjectId(o); }); return n; }
bool is_power(const void* s) { int op = 0; if (s && g.Skill_GetSkillOperation) guarded("GetSkillOperation", [&] { op = g.Skill_GetSkillOperation(s); }); return op == kOpPower; }
unsigned affinity_of(int type) { load_devotion(); void* p = player(); unsigned n = 0; if (p && g.GetAffinity && type >= 0 && type < exe_ui::kAffinityCount) guarded("GetAffinity", [&] { n = g.GetAffinity(p, type); }); return n; }
// A std::string the game reads (by value or const&): SSO for short text, else a UCRT-heap buffer (freed by free_a).
MsvcStringA make_a(const std::string& t) {
  MsvcStringA s; init_a(s);
  if (t.size() < 16) { memcpy(s.u.buf, t.data(), t.size()); s.u.buf[t.size()] = 0; s.size = t.size(); return s; }
  s.u.ptr = (char*)malloc(t.size() + 1);
  if (!s.u.ptr) return s;
  memcpy(s.u.ptr, t.data(), t.size() + 1);
  s.size = t.size(); s.capacity = t.size();
  return s;
}
void free_a(MsvcStringA& s) { if (s.capacity >= 16 && s.u.ptr) free(s.u.ptr); init_a(s); }
std::string template_autocast(const void* power) {
  std::string out;
  if (!power || !g.Skill_GetTemplateAutoCast) return out;
  guarded("GetTemplateAutoCast", [&] { MsvcStringA s; init_a(s); g.Skill_GetTemplateAutoCast(power, &s); out = take_a(s); });
  return out;
}
// The affinity pairs of a constellation, as "Chaos 4, Eldritch 2".
std::string pairs_text(const std::vector<std::pair<int, unsigned>>& pairs) {
  core::MessageBuilder m;
  for (const auto& [type, amount] : pairs) m.list_item().fragment(affinity_name(type)).fragment(std::format("{}", amount));
  return m.build();
}
}  // namespace

std::string affinity_name(int type) { return localize(std::format("tagDevotionAffinity{:02}", type + 1)); }
std::vector<Affinity> affinities() {
  std::vector<Affinity> out;
  for (int t = 0; t < exe_ui::kAffinityCount; ++t) out.push_back({t, affinity_name(t), affinity_of(t)});
  return out;
}
std::string affinities_text() {
  core::MessageBuilder m;
  bool any = false;
  for (const Affinity& a : affinities()) if (a.value) { any = true; m.list_item().fragment(a.name).fragment(std::format("{}", a.value)); }
  return any ? m.build() : std::string(strings::kNoAffinity);
}
unsigned devotion_points_total() { load_devotion(); void* p = player(); unsigned n = 0; if (p && g.GetTotalDevotionPoints) guarded("GetTotalDevotionPoints", [&] { n = g.GetTotalDevotionPoints(p); }); return n; }
unsigned devotion_points_max() { load_devotion(); void* p = player(); unsigned n = 0; if (p && g.GetMaxDevotionPoints) guarded("GetMaxDevotionPoints", [&] { n = g.GetMaxDevotionPoints(p); }); return n; }

std::vector<DevotionConstellation> constellations() {
  load_devotion();
  std::vector<DevotionConstellation> out;
  for (const exe_ui::DevotionConstellationB& cb : exe_ui::devotion_constellations()) {
    DevotionConstellation c;
    c.p = cb.p;
    c.name = localize(cb.name_tag);
    if (c.name.empty()) c.name = cb.name_tag;
    c.description = localize(cb.info_tag);
    c.required = cb.required;
    c.given = cb.given;
    for (const exe_ui::DevotionStarB& sb : cb.stars) {
      DevotionStar s;
      s.star = sb.p; s.index = sb.index; s.skill_id = sb.skill_id; s.host_id = sb.host_id; s.links = sb.links;
      s.skill = object_by_id(sb.skill_id);
      if (!s.skill) { c.stars.push_back(std::move(s)); continue; }
      guarded("star readout", [&] {
        s.learned = level_of(s.skill) > 0;
        s.power = is_power(s.skill);
        if (s.power) {
          s.name = skill_name_by_id(sb.skill_id);
          s.dev_level = g.Skill_GetDevotionLevel ? g.Skill_GetDevotionLevel(s.skill) : 0;
          s.dev_max = g.Skill_GetDevotionMaxLevel ? g.Skill_GetDevotionMaxLevel(s.skill) : 0;
          s.experience = g.Skill_GetDevotionExperience ? g.Skill_GetDevotionExperience(s.skill) : 0;
          if (g.Skill_GetRequiredExperience && s.dev_max && s.dev_level < s.dev_max) s.next_experience = g.Skill_GetRequiredExperience(s.skill, s.dev_level + 1);
          // The binding: the Skill's own devotion parent is the truth; the exe's Star mirrors it.
          unsigned host = g.Skill_GetDevotionParent ? g.Skill_GetDevotionParent(s.skill) : 0;
          if (host) s.host_id = host;
          if (s.host_id) s.host_name = skill_name_by_id(s.host_id);
        }
      });
      if (s.learned) ++c.learned;
      c.stars.push_back(std::move(s));
    }
    c.complete = !c.stars.empty() && c.learned == c.stars.size();
    out.push_back(std::move(c));
  }
  // Affinity is NOT saved by the game: it is derived from the complete constellations whenever the star map is
  // shown (verified live 2026-08-27: a loaded character with a learned Crossroads read affinity 0 until the game's
  // map opened once). Our path never shows the map, so reconcile the counters here the same way (to value, so it is
  // idempotent with the game's own recompute).
  void* p = player();
  if (p && g.AddAffinity && g.SubtractAffinity) {
    unsigned expected[exe_ui::kAffinityCount] = {};
    for (const DevotionConstellation& c : out) if (c.complete) for (const auto& [type, amount] : c.given) expected[type] += amount;
    for (int t = 0; t < exe_ui::kAffinityCount; ++t) {
      unsigned have = affinity_of(t);
      if (have == expected[t]) continue;
      log::writef("gameapi: affinity {} reconciled {} -> {}", t, have, expected[t]);
      guarded("reconcile affinity", [&] { if (have < expected[t]) g.AddAffinity(p, t, expected[t] - have); else g.SubtractAffinity(p, t, have - expected[t]); });
    }
  }
  // The five Crossroads share one display tag (the game tells them apart by icon colour): a shared name gets the
  // affinity it gives appended -- "Crossroads, Primordial".
  for (DevotionConstellation& c : out) {
    int same = 0;
    for (const DevotionConstellation& o : out) if (o.name == c.name) ++same;
    if (same > 1 && !c.given.empty()) { core::MessageBuilder m; m.fragment(c.name).list_item().fragment(affinity_name(c.given.front().first)); c.name = m.build(); }
  }
  for (DevotionConstellation& c : out) {
    c.affinity_met = true;
    for (const auto& [type, amount] : c.required) if (affinity_of(type) < amount) c.affinity_met = false;
  }
  return out;
}
// Breadth-first from the root (the star with no links), children in index order; anything unreached (a malformed
// record) is appended in index order so no star is ever hidden.
std::vector<unsigned> star_order(const DevotionConstellation& c) {
  std::vector<unsigned> out;
  std::set<unsigned> seen;
  std::deque<unsigned> q;
  for (const DevotionStar& s : c.stars) if (s.links.empty()) { q.push_back(s.index); seen.insert(s.index); }
  while (!q.empty()) {
    unsigned cur = q.front(); q.pop_front();
    out.push_back(cur);
    for (const DevotionStar& s : c.stars)
      if (!seen.count(s.index)) for (int l : s.links) if ((unsigned)l == cur) { seen.insert(s.index); q.push_back(s.index); break; }
  }
  for (const DevotionStar& s : c.stars) if (!seen.count(s.index)) out.push_back(s.index);
  return out;
}
std::string can_take_star(const DevotionConstellation& c, const DevotionStar& s) {
  load_devotion();
  if (s.learned) return std::string(strings::kLearned);
  // The window's RefreshEligibility gate: a linked star must be learned (the root has none), then the affinity.
  if (!s.links.empty()) {
    bool ok = false;
    for (int l : s.links) for (const DevotionStar& o : c.stars) if ((int)o.index == l && o.learned) ok = true;
    if (!ok) return std::format("{} {}", strings::kNeedsStar, s.links.front());
  }
  if (!c.affinity_met) {
    std::vector<std::pair<int, unsigned>> unmet;
    for (const auto& pr : c.required) if (affinity_of(pr.first) < pr.second) unmet.push_back(pr);
    core::MessageBuilder m; m.fragment(strings::kNeeds).fragment(pairs_text(unmet));
    return m.build();
  }
  void* p = player();
  unsigned pts = 0; if (p && g.GetDevotionPoints) guarded("GetDevotionPoints", [&] { pts = g.GetDevotionPoints(p); });
  if (pts == 0) return std::string(strings::kNoPoints);
  return {};
}
// Star::HandleMouseEvent's SPEND branch (exe+0x17ebfc): IncrementSkillLevel(1), SubtractDevotionPoint,
// IncrementDevotionLevel when the devotion level is still 0, then GrantAffinity if the constellation just completed.
bool take_star(unsigned skill_id, bool& completed) {
  load_devotion();
  completed = false;
  void* p = player();
  if (!p || !g.SubtractDevotionPoint || !g.GetDevotionPoints) return false;
  for (const DevotionConstellation& c : constellations()) {
    for (const DevotionStar& s : c.stars) {
      if (s.skill_id != skill_id) continue;
      if (!s.skill || !can_take_star(c, s).empty()) return false;
      bool was_complete = c.complete;
      bool ok = false;
      guarded("take star", [&] {
        if (g.GetDevotionPoints(p) == 0) return;
        auto inc = (void (*)(void*, unsigned))vfn(s.skill, g_slot_inc);
        if (!inc) return;
        inc(s.skill, 1);
        g.SubtractDevotionPoint(p);
        if (g.Skill_GetDevotionLevel && g.Skill_GetDevotionLevel(s.skill) < 1)
          if (auto incd = (void (*)(void*))vfn(s.skill, g_slot_inc_dev)) incd(s.skill);
        ok = true;
      });
      if (!ok) return false;
      if (!was_complete && c.learned + 1 == c.stars.size()) {   // GrantAffinity (exe+0x181870): the completion bonus
        completed = true;
        if (g.AddAffinity) guarded("AddAffinity", [&] { for (const auto& [type, amount] : c.given) g.AddAffinity(p, type, amount); });
      }
      log::writef("gameapi: devotion point on skill {} ({}) completed={}", skill_id, c.name, completed);
      return true;
    }
  }
  return false;
}
// The star rollover exactly as the window builds it (Star::BuildRollover exe+0x17f0d0), with the 14-byte
// SkillReasons filled the way Star::UpdateState does: +0 unlearned and no points, +2/+0xa link unmet, +3 maxed,
// +9 affinity met, +0xb unlearned, +0xc affinity unmet (+8 reclaim cost, never set outside reclaim mode).
std::vector<std::string> star_tooltip(unsigned skill_id) {
  load_devotion();
  std::vector<std::string> out;
  if (!g.GenerateUIDevotionText) return out;
  for (const DevotionConstellation& c : constellations()) {
    for (const DevotionStar& s : c.stars) {
      if (s.skill_id != skill_id || !s.skill) continue;
      alignas(16) unsigned char reasons[64] = {};
      std::string why = can_take_star(c, s);
      bool link_unmet = !s.learned && why.rfind(strings::kNeedsStar, 0) == 0;
      reasons[0] = !s.learned && why == strings::kNoPoints;
      reasons[2] = reasons[0xa] = link_unmet;
      reasons[3] = s.power && s.dev_max && s.dev_level >= s.dev_max;
      reasons[9] = c.affinity_met;
      reasons[0xb] = !s.learned;
      reasons[0xc] = !c.affinity_met;
      void* host = s.host_id ? object_by_id(s.host_id) : nullptr;
      TextLineBuffer buf;
      guarded("GenerateUIDevotionText", [&] { g.GenerateUIDevotionText(s.skill, host, buf.vec(), reasons, false, false, 0, 0, 0x31); });
      for (TextLine& l : buf.take("devotion text")) out.push_back(std::move(l.text));
      // The game's "Complete Constellation Bonus:" block is amounts beside affinity ICONS -- bare numbers to a
      // reader. Replace it with the spoken pairs.
      std::string bonus_header = localize("tagDevotionAffinityBonus");
      for (size_t i = 0; i < out.size(); ++i)
        if (!bonus_header.empty() && out[i] == bonus_header) { out.resize(i); if (!c.given.empty()) { core::MessageBuilder m; m.fragment(strings::kGives).fragment(pairs_text(c.given)); out.push_back(m.build()); } break; }
      return out;
    }
  }
  return out;
}
// The constellation rollover (Constellation::BuildRollover exe+0x180fd0): name, description, what it requires
// (with what the character has), what completing it gives.
std::vector<std::string> constellation_tooltip(const DevotionConstellation& c) {
  std::vector<std::string> out;
  out.push_back(c.name);
  if (!c.description.empty()) out.push_back(c.description);
  if (!c.required.empty()) {
    core::MessageBuilder m; m.fragment(strings::kRequires);
    for (const auto& [type, amount] : c.required) m.list_item().fragment(affinity_name(type)).fragment(std::format("{}", amount)).list_item().fragment(strings::kHave).fragment(std::format("{}", affinity_of(type)));
    out.push_back(m.build());
  }
  if (!c.given.empty()) { core::MessageBuilder m; m.fragment(strings::kGives).fragment(pairs_text(c.given)); out.push_back(m.build()); }
  return out;
}
// The picker's candidate filter (SkillSelect::AddCandidates exe+0x1d4af0): the character's class skills (of its
// own masteries), item-granted skills and the item skill cache; not a mastery, a default attack, a modifier, a
// skill with its own record autocast, or a devotion skill (operation != 0); the power's own class filter
// (IsSkillA on the host's RTTI) and record blacklist. The window lists unlearned ones greyed; here only
// learned skills are offered (an unlearned host can't be picked there either).
std::vector<SkillInfo> power_host_candidates(unsigned power_skill_id) {
  load_devotion();
  std::vector<SkillInfo> out;
  void* p = player();
  void* power = object_by_id(power_skill_id);
  if (!p || !power) return out;
  const void* sm = g.GetSkillManager ? g.GetSkillManager(p) : nullptr;
  std::vector<unsigned> masteries;
  if (sm && g.SM_GetSkillMasteryIds) { VecBuffer<unsigned> buf(16); guarded("GetSkillMasteryIds", [&] { g.SM_GetSkillMasteryIds(sm, buf.vec()); }); masteries = buf.take("GetSkillMasteryIds"); }
  std::set<unsigned> seen;
  auto consider = [&](void* s, bool class_list) {
    if (!s) return;
    unsigned id = obj_id(s);
    if (!id || seen.count(id)) return;
    bool keep = false;
    guarded("candidate filter", [&] {
      if (level_of(s) == 0) return;
      if (g.Skill_IsSkillTheMasterySkill && g.Skill_IsSkillTheMasterySkill(s)) return;
      if (sm && g.SM_IsDefaultSkill && g.SM_IsDefaultSkill(sm, id)) return;
      if (g.Skill_IsSkillModifier && g.Skill_IsSkillModifier(s)) return;
      if (g.Skill_HasAutocastInDbr && g.Skill_HasAutocastInDbr(s)) return;
      if (g.Skill_GetSkillOperation && g.Skill_GetSkillOperation(s) != 0) return;
      if (g.Skill_IsSkillA && !g.Skill_IsSkillA(power, world::object_rtti(s))) return;
      // By-value std::string: in the MSVC x64 ABI the CALLEE destroys the argument copy, so the buffer (UCRT heap, the
      // game's allocator) is handed over, never freed here (a free_a here double-freed and killed the game, 2026-08-27).
      if (g.Skill_IsSkillBlackListed) { MsvcStringA rec = make_a(object_record(s)); if (g.Skill_IsSkillBlackListed(power, rec)) return; }
      if (class_list && g.Skill_GetMasteryId && !masteries.empty()) { unsigned m = g.Skill_GetMasteryId(s); bool mine = false; for (unsigned x : masteries) if (x == m) mine = true; if (!mine) return; }
      keep = true;
    });
    if (!keep) return;
    seen.insert(id);
    for (const SkillInfo& k : skills()) if (k.id == id) { out.push_back(k); return; }
    for (const SkillInfo& k : assignable_skills()) if (k.id == id) { out.push_back(k); return; }
    SkillInfo k; k.p = s; k.id = id; k.name = skill_name_by_id(id); k.level = level_of(s); out.push_back(k);
  };
  if (g.Char_GetSkillList) { std::vector<void*> v; guarded("Character::GetSkillList", [&] { v = vec_items<void*>(g.Char_GetSkillList(p), 1024); }); for (void* s : v) consider(s, true); }
  if (g.Char_GetItemSkillList) { std::vector<void*> v; guarded("Character::GetItemSkillList", [&] { v = vec_items<void*>(g.Char_GetItemSkillList(p), 256); }); for (void* s : v) consider(s, false); }
  if (g.Char_GetItemSkillCache) { std::vector<void*> v; guarded("Character::GetItemSkillCache", [&] { v = vec_items<void*>(g.Char_GetItemSkillCache(p), 256); }); for (void* s : v) consider(s, false); }
  return out;
}
namespace {
// Detach `power` from whatever host it is on (SetAutocastSkill(null, "", false) on the host + SetDevotionParent(0)),
// mirroring the exe's Star when known.
void unbind(void* power, void* star) {
  unsigned host_id = 0;
  guarded("GetDevotionParent", [&] { host_id = g.Skill_GetDevotionParent ? g.Skill_GetDevotionParent(power) : 0; });
  void* host = host_id ? object_by_id(host_id) : nullptr;
  guarded("unbind power", [&] {
    if (host) if (auto f = (void (*)(void*, void*, const MsvcStringA*, bool))vfn(host, g_slot_autocast)) { MsvcStringA empty; init_a(empty); f(host, nullptr, &empty, false); }
    if (g.Skill_SetDevotionParent) g.Skill_SetDevotionParent(power, 0);
  });
  if (star) exe_ui::devotion_set_star_host(star, 0);
}
}  // namespace
// BindCelestialPower (exe+0x1867f0): the power's template autocast name goes onto the host with SetAutocastSkill,
// the host's id into the power's devotion parent. A host that already carries another power loses it first
// (the window asks "replace?"; the caller speaks `replaced_power` instead).
bool bind_power(unsigned power_skill_id, unsigned host_skill_id, std::string* replaced_power) {
  load_devotion();
  void* power = object_by_id(power_skill_id);
  if (!power || !is_power(power) || level_of(power) == 0) return false;
  void* my_star = nullptr;
  void* other_power = nullptr; void* other_star = nullptr;
  for (const exe_ui::DevotionConstellationB& c : exe_ui::devotion_constellations())
    for (const exe_ui::DevotionStarB& s : c.stars) {
      if (s.skill_id == power_skill_id) my_star = s.p;
      if (host_skill_id && s.skill_id != power_skill_id) {
        void* o = object_by_id(s.skill_id);
        unsigned parent = 0; if (o && g.Skill_GetDevotionParent) guarded("GetDevotionParent", [&] { parent = g.Skill_GetDevotionParent(o); });
        if (parent == host_skill_id) { other_power = o; other_star = s.p; }
      }
    }
  unbind(power, my_star);
  if (!host_skill_id) { log::writef("gameapi: devotion power {} unbound", power_skill_id); return true; }
  void* host = object_by_id(host_skill_id);
  if (!host) return false;
  std::string name = template_autocast(power);
  if (name.empty()) { log::writef("gameapi: power {} has no template autocast", power_skill_id); return false; }
  if (other_power) { if (replaced_power) *replaced_power = skill_name_by_id(obj_id(other_power)); unbind(other_power, other_star); }
  bool ok = false;
  guarded("bind power", [&] {
    auto f = (void (*)(void*, void*, const MsvcStringA*, bool))vfn(host, g_slot_autocast);
    if (!f) return;
    MsvcStringA n = make_a(name);
    f(host, power, &n, false);
    free_a(n);
    if (g.Skill_SetDevotionParent) g.Skill_SetDevotionParent(power, host_skill_id);
    ok = true;
  });
  if (ok && my_star) exe_ui::devotion_set_star_host(my_star, host_skill_id);
  log::writef("gameapi: devotion power {} -> host {} ({}) ok={}", power_skill_id, host_skill_id, name, ok);
  return ok;
}
// ---- reclaiming (a spirit guide's reclaim mode; the star map's own RECLAIM branch, re_devotion_exe.md 2.2/2.3) ----
unsigned devotion_reclaim_cost() { load_devotion(); void* p = player(); const void* sm = p && g.GetSkillManager ? g.GetSkillManager(p) : nullptr; unsigned n = 0; if (sm && g.SM_GetCurrentDevotionReclamationCost) guarded("devotion reclaim cost", [&] { n = g.SM_GetCurrentDevotionReclamationCost(sm); }); return n; }
// The celestial power a skill hosts, the way the skills window's SkillReasons byte 7 (exe+0x249445..) decides it: an
// autocast that is NOT the skill's own DBR autocast and whose operation is a power. 0 when it hosts none.
unsigned hosted_power_id(const void* host_skill) {
  load_devotion();
  unsigned id = 0;
  if (!host_skill || !g.Skill_HasAutocastSkill || !g.Skill_HasAutocastInDbr || !g.Skill_GetAutoCastSkill) return 0;
  guarded("hosted_power_id", [&] {
    if (!g.Skill_HasAutocastSkill(host_skill) || g.Skill_HasAutocastInDbr(host_skill)) return;
    void* ac = g.Skill_GetAutoCastSkill(host_skill);
    if (ac && is_power(ac)) id = obj_id(ac);
  });
  return id;
}
unsigned devotion_reclaim_aether_cost() { load_devotion(); void* p = player(); const void* sm = p && g.GetSkillManager ? g.GetSkillManager(p) : nullptr; unsigned n = 0; if (sm && g.SM_GetDevotionReclamationAetherCost) guarded("devotion aether cost", [&] { n = g.SM_GetDevotionReclamationAetherCost(sm); }); return n; }
unsigned aether() { load_devotion(); void* p = player(); unsigned n = 0; if (p && g.Player_GetCurrentAether) guarded("GetCurrentAether", [&] { n = g.Player_GetCurrentAether(p); }); return n; }
bool dev_add_aether(unsigned n) { load_devotion(); void* p = player(); bool ok = p && g.Player_AddAether && guarded("AddAether", [&] { g.Player_AddAether(p, n); }); log::writef("gameapi: dev aether +{} ok={}", n, ok); return ok; }
// RefreshEligibility's reclaim-mode gates: a learned star that links to this one blocks it; then ComputeReclaimBlockers
// (exe+0x18c3f0): with this constellation's affinityGiven subtracted (only if it is complete -- the bonus is only
// held while complete), every constellation with a learned star must still meet its affinityRequired; the failing
// one being this constellation is the "self-lock" (tagRemoveBase). Then the costs.
std::string can_reclaim_star(const DevotionConstellation& c, const DevotionStar& s, const std::vector<DevotionConstellation>& all) {
  load_devotion();
  if (!s.learned) return std::string(strings::kNothingToReclaim);
  for (const DevotionStar& o : c.stars)
    if (o.learned && o.index != s.index) for (int l : o.links) if ((unsigned)l == s.index) return std::format("{} {}", strings::kNeededByStar, o.index);
  if (c.complete && !c.given.empty()) {
    unsigned aff[exe_ui::kAffinityCount];
    for (int t = 0; t < exe_ui::kAffinityCount; ++t) aff[t] = affinity_of(t);
    for (const auto& [type, amount] : c.given) aff[type] = aff[type] > amount ? aff[type] - amount : 0;
    for (const DevotionConstellation& o : all) {
      if (!o.learned) continue;
      for (const auto& [type, amount] : o.required)
        if (aff[type] < amount) {
          if (o.p == c.p) { std::string t = localize("tagRemoveBase"); return t.empty() ? std::string(strings::kCannot) : t; }
          return std::format("{} {}", strings::kWouldLock, o.name);
        }
    }
  }
  if (devotion_reclaim_cost() > money()) return std::string(strings::kNotEnoughBits);
  if (devotion_reclaim_aether_cost() > aether()) return std::string(strings::kNotEnoughAether);
  return {};
}
// The RECLAIM branch of Star::HandleMouseEvent: UseDevotionReclamationPoints(1) is the charge (false = refused),
// SetSkillLevel(0), RevokeAffinity if the constellation stops being complete, unbind a power, AddDevotionPoints(1).
bool reclaim_star(unsigned skill_id, bool& uncompleted) {
  load_devotion();
  uncompleted = false;
  void* p = player();
  const void* sm = p && g.GetSkillManager ? g.GetSkillManager(p) : nullptr;
  if (!p || !sm || !g.SM_UseDevotionReclamationPoints || !g.AddDevotionPoints) return false;
  std::vector<DevotionConstellation> all = constellations();
  for (const DevotionConstellation& c : all) {
    for (const DevotionStar& s : c.stars) {
      if (s.skill_id != skill_id) continue;
      if (!s.skill || !can_reclaim_star(c, s, all).empty()) return false;
      bool ok = false;
      guarded("reclaim star", [&] {
        if (!g.SM_UseDevotionReclamationPoints((void*)sm, 1)) return;
        auto setlvl = (void (*)(void*, unsigned))vfn(s.skill, g_slot_setlvl);
        if (!setlvl) { g.SM_UseDevotionReclamationPoints((void*)sm, -1); return; }   // refund the charge
        setlvl(s.skill, 0);
        ok = true;
      });
      if (!ok) return false;
      if (c.complete) {   // RevokeAffinity (exe+0x181910)
        uncompleted = true;
        if (g.SubtractAffinity) guarded("SubtractAffinity", [&] { for (const auto& [type, amount] : c.given) g.SubtractAffinity(p, type, amount); });
      }
      if (s.power) unbind(s.skill, s.star);
      guarded("AddDevotionPoints", [&] { g.AddDevotionPoints(p, 1); });
      log::writef("gameapi: devotion point reclaimed from skill {} ({}) uncompleted={}", skill_id, c.name, uncompleted);
      return true;
    }
  }
  return false;
}
// ---- clear all (a spirit guide's reclaim mode; mod-only: vanilla has no such button, docs/devotion.md) ----
// The iron-bit price of reclaiming `n` points in a row, read-only: GetCurrentDevotionReclamationCost (Game.dll 0x520050)
// is a pure function of the reclaims-so-far counter SkillManager+0xf4 and two vectors, the tier thresholds +0xc0 and
// the costs +0xd8 (the PC record's devotionReclamationPointTiers / Costs): the tier is the first i with tiers[i] <=
// counter < tiers[i+1], else the last. UseDevotionReclamationPoints(n) charges exactly this sum, stepping the counter.
static bool devotion_reclaim_bits_for(const void* sm, unsigned n, unsigned long long& total) {
  total = 0;
  unsigned counter = 0;
  std::vector<unsigned> tiers, costs;
  if (!read_mem((const char*)sm + 0xf4, &counter, sizeof counter)) return false;
  tiers = vec_items<unsigned>((const MemVec*)((const char*)sm + 0xc0), 256);
  costs = vec_items<unsigned>((const MemVec*)((const char*)sm + 0xd8), 256);
  if (tiers.empty() || costs.size() < tiers.size()) return false;
  for (unsigned k = 0; k < n; ++k, ++counter) {
    size_t idx = 0;
    if (tiers.size() > 1) {
      size_t i = 0;
      for (; i + 1 < tiers.size(); ++i) {
        if (counter < tiers[i]) { ++idx; continue; }
        if (counter < tiers[i + 1]) break;
        ++idx;
      }
    }
    total += costs[idx];
  }
  return true;
}
DevotionClearCost devotion_clear_cost() {
  load_devotion();
  DevotionClearCost out;
  for (const DevotionConstellation& c : constellations()) out.points += c.learned;
  void* p = player();
  const void* sm = p && g.GetSkillManager ? g.GetSkillManager(p) : nullptr;
  unsigned long long bits = 0;
  if (!sm || !devotion_reclaim_bits_for(sm, out.points, bits)) { out.ok = false; return out; }
  // Sanity: the first point's price must be the game's own answer, or the layout moved (a game patch).
  { unsigned long long first = 0; if (out.points && (!devotion_reclaim_bits_for(sm, 1, first) || first != devotion_reclaim_cost())) { out.ok = false; return out; } }
  out.bits = bits;
  out.aether = (unsigned long long)devotion_reclaim_aether_cost() * out.points;
  return out;
}
// Reclaim every learned star at once, ignoring the lock order (the reason this exists: a self-sustaining set cannot be
// unwound star by star without spare points). One UseDevotionReclamationPoints(n) charges the whole escalating price
// all-or-nothing; then each star gets reclaim_star's steps without the per-star charge.
std::string clear_all_devotion() {
  load_devotion();
  void* p = player();
  const void* sm = p && g.GetSkillManager ? g.GetSkillManager(p) : nullptr;
  if (!p || !sm || !g.SM_UseDevotionReclamationPoints || !g.AddDevotionPoints || g_slot_setlvl < 0) return std::string(strings::kCannot);
  std::vector<DevotionConstellation> all = constellations();
  unsigned n = 0;
  for (const DevotionConstellation& c : all) n += c.learned;
  if (!n) return std::string(strings::kNothingToReclaim);
  DevotionClearCost cost = devotion_clear_cost();
  if (!cost.ok) return std::string(strings::kCannot);
  if (cost.bits > money()) return std::string(strings::kNotEnoughBits);
  if (cost.aether > aether()) return std::string(strings::kNotEnoughAether);
  bool charged = false;
  guarded("clear all charge", [&] { charged = g.SM_UseDevotionReclamationPoints((void*)sm, (int)n); });
  if (!charged) return std::string(strings::kCannot);
  unsigned cleared = 0;
  for (const DevotionConstellation& c : all) {
    if (!c.learned) continue;
    for (const DevotionStar& s : c.stars) {
      if (!s.learned || !s.skill) continue;
      if (s.power) unbind(s.skill, s.star);
      bool ok = false;
      guarded("clear star", [&] { if (auto setlvl = (void (*)(void*, unsigned))vfn(s.skill, g_slot_setlvl)) { setlvl(s.skill, 0); ok = true; } });
      if (ok) ++cleared;
    }
    if (c.complete && g.SubtractAffinity) guarded("SubtractAffinity", [&] { for (const auto& [type, amount] : c.given) g.SubtractAffinity(p, type, amount); });
  }
  guarded("AddDevotionPoints", [&] { g.AddDevotionPoints(p, cleared); });
  if (cleared != n) guarded("refund", [&] { g.SM_UseDevotionReclamationPoints((void*)sm, -(int)(n - cleared)); });
  log::writef("gameapi: devotion cleared {} of {} stars, {} bits + {} aether", cleared, n, cost.bits, cost.aether);
  return {};
}
std::string dump_devotion() {
  unsigned spent = 0;
  { void* p = player(); const void* sm = p && g.GetSkillManager ? g.GetSkillManager(p) : nullptr; if (sm && g.SM_GetNumDevotionPointsSpent) guarded("GetNumDevotionPointsSpent", [&] { spent = g.SM_GetNumDevotionPointsSpent(sm); }); }
  // GetNumDevotionPointsSpent (Game.dll 0x51fc90) sums Skill::GetCurrentLevel (vt+0x1c8) over the devotion skills, and a
  // celestial power star's current level is its POWER level: a level-7 Turtle Shell counts 7. Not points; dev reading only.
  unsigned learned = 0;
  for (const DevotionConstellation& c : constellations()) learned += c.learned;
  std::string out = std::format("devotion points {} available, {} of {}, {} stars learned (game's GetNumDevotionPointsSpent {}: counts power levels); affinities {}; reclaim {} bits + {} aether (have {} bits, {} aether)\n", devotion_points(), devotion_points_total(), devotion_points_max(), learned, spent, affinities_text(), devotion_reclaim_cost(), devotion_reclaim_aether_cost(), money(), aether());
  for (const DevotionConstellation& c : constellations()) {
    out += std::format("  {} ({}) learned {}/{} complete={} affinity_met={} requires [{}] gives [{}]\n", c.name, c.p, c.learned, c.stars.size(), c.complete, c.affinity_met, pairs_text(c.required), pairs_text(c.given));
    for (unsigned i : star_order(c)) for (const DevotionStar& s : c.stars) if (s.index == i) {
      std::string links; for (int l : s.links) links += std::format("{} ", l);
      unsigned lvl = level_of(s.skill), max = 0, cur = 0;
      if (s.skill && g.Skill_GetMaxLevel) guarded("GetMaxLevel", [&] { max = g.Skill_GetMaxLevel(s.skill); });
      if (s.skill) guarded("GetCurrentLevel", [&] { if (auto f = (unsigned (*)(const void*))vfn(s.skill, g_slot_curlvl)) cur = f(s.skill); });
      out += std::format("    star {} id={} {} power={} learned={} skill lvl {}/{} cur {} dev lvl {}/{} xp {}/{} host={} '{}' links[{}] {}\n", s.index, s.skill_id, s.name, s.power, s.learned, lvl, max, cur, s.dev_level, s.dev_max, s.experience, s.next_experience, s.host_id, s.host_name, links, can_take_star(c, s));
    }
  }
  return out;
}
bool dev_add_devotion(unsigned n) {
  load_devotion(); void* p = player();
  if (!p || !g.AddDevotionPoints || !g.AddTotalDevotionPoints) return false;
  unsigned room = devotion_points_max() > devotion_points_total() ? devotion_points_max() - devotion_points_total() : 0;
  if (n > room) n = room;
  bool ok = n && guarded("dev devotion", [&] { g.AddDevotionPoints(p, n); g.AddTotalDevotionPoints(p, n); });
  log::writef("gameapi: dev devotion +{} ok={}", n, ok);
  return ok;
}
unsigned devotion_points() { load_devotion(); void* p = player(); unsigned n = 0; if (p && g.GetDevotionPoints) guarded("GetDevotionPoints", [&] { n = g.GetDevotionPoints(p); }); return n; }
}  // namespace gd::gameapi
