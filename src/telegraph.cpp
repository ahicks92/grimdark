#include "telegraph.h"
#include <algorithm>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include "audio.h"
#include "log.h"
#include "settings.h"

namespace gd::telegraph {
const char* const kShapeNames[kShapes] = {"swing", "stomp", "wave", "shot", "ring", "area", "charge"};
namespace {
Mode g_mode = Mode::All;
bool g_shape_on[kShapes] = {true, true, true, true, true, true, true};
float g_vol = 1.0f;
int g_variant = 200;                     // the 200 ms words; the only shipped set (a 100 ms set was tried and dropped, 2026-09-01)
float g_radius = 25.0f;                  // the sonar's radius: casts farther than this are not ours to hear
constexpr float kMeleeReach = 4.5f;      // a weapon attack started from farther away is a ranged weapon: a shot
std::map<unsigned, double> g_last;       // caster id -> time of the last cue (the weapon-pool wrapper and the basic
                                         // attack both StartAction in the same frame: one cue per caster per 80 ms)
std::deque<std::string> g_recent; std::mutex g_mu;
unsigned g_played = 0, g_skipped_shape = 0, g_skipped_far = 0, g_skipped_friend = 0, g_skipped_dup = 0, g_skipped_mode = 0, g_skipped_unknown = 0;
std::map<std::string, unsigned> g_unknown_classes;   // class -> casts seen; a new class from a patch shows up here, never as a guess
// "highest tier": the top MonsterClassification among the enemies in range, refreshed at most every 300 ms
// (a sphere query per cast would be too much in a pack).
int g_top_tier = -1; double g_top_tier_t = -1e9;

bool has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }
void note(std::string s) {
  std::lock_guard<std::mutex> l(g_mu);
  g_recent.push_back(std::move(s));
  while (g_recent.size() > 40) g_recent.pop_front();
}
int shape_index(const char* shape) {
  for (int i = 0; i < kShapes; ++i) if (std::string_view(kShapeNames[i]) == shape) return i;
  return -1;
}
int top_tier_nearby(double now) {
  if (now - g_top_tier_t > 0.3) {
    g_top_tier = -1;
    for (const world::ScanItem& it : world::scan(world::ScanGroup::Enemies, g_radius)) g_top_tier = std::max(g_top_tier, it.classification);
    g_top_tier_t = now;
  }
  return g_top_tier;
}
// Does the mode admit this caster? Target = the reviewed thing or the game's combat enemy; HighestTier = the
// caster's classification is the highest present (in a trash pack every monster is the highest, so all speak).
bool mode_admits(const Cast& c) {
  switch (g_mode) {
    case Mode::Off: return false;
    case Mode::All: return true;
    case Mode::Target: return c.caster_id == world::reviewed_id() || c.caster_id == world::current_target();
    case Mode::HighestTier: {
      float pct = 0; int level = 0, cls = -1;
      if (!world::enemy_vitals(c.caster_id, pct, level, cls)) return false;
      return cls >= top_tier_nearby(c.t);
    }
  }
  return true;
}
}  // namespace

// The concrete Skill_* class -> the reaction, by EXACT class name (2026-09-20). The first cut matched substrings, and
// the game's class vocabulary composes: "AttackRadius" is also inside the toggled and timed auras, the rains at a
// point and the on-hit retaliations, "Projectile" inside drops, orbiters, mine layers and teleports -- about one
// active monster skill in ten got a cue that sent the player the wrong way (the Ancient Shambler's avalanche read
// as "stomp": step away from the caster, into the rain). Every class of every nonplayerskills record is listed
// (tools/arz.py survey, 70 classes); "" = deliberately silent (no wind-up moment, or not a threat geometry);
// a class not in the table is silent AND counted in g_unknown_classes (/telegraph) instead of guessed.
//   swing  weapon attack in reach -- get out of reach (from beyond kMeleeReach it is a ranged weapon: shot)
//   charge a charge or a blink-strike: it closes the distance itself -- brace / break line, running does not help
//   stomp  a radius around the caster at the hit frame -- step away from the caster
//   wave   a directional wave / cone / line -- get off the line
//   shot   a projectile, a spell at your position (the lightning bolts: no flight, but the same counterplay),
//          a beam, a pool launch -- keep moving / sidestep
//   ring   projectiles in all directions -- run outward
//   area   something that keeps landing on a spot: rains, drops, orbiters, geysers, timed auras -- leave the area
const char* shape_of(const std::string& k, float dist) {
  static const std::map<std::string, const char*> table = {
    // weapon attacks
    {"Skill_AttackWeapon", "swing"}, {"Skill_WPAttack_BasicAttack", "swing"}, {"Skill_WeaponPool_BasicAttack", "swing"},
    {"Skill_WeaponPool_ChargedFinale", "swing"}, {"Skill_WeaponPool_ChargedLinear", "swing"}, {"Skill_AttackWeaponRadius", "swing"},
    {"Skill_AttackWeaponKick", "swing"}, {"Skill_AttackInherent", "swing"},
    {"Skill_AttackWeaponCharge", "charge"}, {"Skill_AttackWeaponBlink", "charge"},
    // radius around the caster at the hit frame
    {"Skill_AttackRadius", "stomp"}, {"SkillSecondary_AttackRadius", "stomp"}, {"SkillActivated_Suicide", "stomp"},
    // directional
    {"Skill_AttackWave", "wave"}, {"Skill_AttackSpellCone", "wave"}, {"Skill_AttackProjectileLineFan", "wave"},
    // aimed at you
    {"Skill_AttackProjectile", "shot"}, {"Skill_AttackProjectileBurst", "shot"}, {"Skill_AttackProjectileFan", "shot"},
    {"Skill_AttackProjectileDebuf", "shot"}, {"Skill_AttackProjectileAreaEffect", "shot"}, {"Skill_AttackSpell", "shot"},
    {"Skill_AttackSpellChaos", "shot"}, {"Skill_AttackChain", "shot"}, {"SkillSecondary_ChainLightning", "shot"}, {"ChaosBeam", "shot"},
    {"Skill_AttackRadiusLightning", "shot"}, {"Skill_AttackRadiusLightning2", "shot"}, {"Skill_AttackPattern", "shot"}, {"Skill_Telekinesis", "shot"},
    {"Skill_AttackProjectileRing", "ring"},
    // keeps landing on a spot
    {"Skill_BuffAttackRadiusDrop", "area"}, {"Skill_AttackProjectileDrop", "area"}, {"Skill_AttackProjectileOrbiting", "area"},
    {"Skill_AttackProjectileSpawnPet", "area"}, {"Skill_CharonGeysers", "area"}, {"Skill_BuffAttackRadiusDuration", "area"},
    // no wind-up moment or no threat geometry: auras, passives, buffs, curses, summons, moves, markers
    {"Skill_Passive", ""}, {"SkillBuff_Passive", ""}, {"SkillBuff_PassiveShield", ""}, {"Skill_PassiveOnLifeBuffSelf", ""},
    {"Skill_PassiveOnHitBuffSelf", ""}, {"Skill_OnHitAttackRadius", ""}, {"Skill_BuffRadiusToggled", ""}, {"Skill_BuffAttackRadiusToggled", ""},
    {"Skill_BuffAttackRadiusLightning", ""}, {"Skill_BuffSelfDuration", ""}, {"Skill_BuffSelfToggled", ""}, {"Skill_BuffSelfShield", ""},
    {"Skill_BuffSelfImmobilize", ""}, {"Skill_BuffSelfColossus", ""}, {"Skill_BuffOther", ""}, {"Skill_BuffRadius", ""}, {"Skill_GiveBonus", ""},
    {"SkillBuff_Debuf", ""}, {"SkillBuff_DebufTrap", ""}, {"SkillBuff_DebufFreeze", ""}, {"SkillBuff_Contageous", ""}, {"SkillBuff_DispelMagic", ""},
    {"Skill_DispelMagic", ""}, {"Skill_AttackBuff", ""}, {"Skill_AttackBuffRadius", ""}, {"SkillSecondary_ChainBonus", ""},
    {"Skill_SpawnPet", ""}, {"Skill_SpawnPetMonster", ""}, {"Skill_TargetedSpawnPet", ""}, {"Skill_MonsterGenerator", ""}, {"Skill_OnDeathSpawnActor", ""},
    {"Skill_AttackSpellTeleportSelf", ""}, {"Skill_AttackSpellTeleport", ""}, {"Skill_AktaiosMirage", ""}, {"CharonGeyserMarker", ""},
    {"Skill_ProjectileModifier", ""}, {"Monster", ""}, {"AttributePak", ""},
  };
  auto it = table.find(k);
  if (it == table.end()) {
    ++g_skipped_unknown;
    if (g_unknown_classes[k]++ == 0) log::writef("telegraph: unknown skill class '{}' (silent; add it to the table)", k);
    return nullptr;
  }
  if (!*it->second) return nullptr;
  if (std::string_view(it->second) == "swing" && dist > kMeleeReach) return "shot";   // a weapon attack from afar = ranged
  return it->second;
}

void on_cast(const Cast& c) {
  if (g_mode == Mode::Off) return;
  // Players first, before the class lookup: the table is the ENEMY vocabulary, and a player's own class skills (Vire's
  // Might = Skill_AttackPathCharge) must not land in the unknown-class list. Any Player, not just ours (multiplayer).
  if (c.caster_class == "Player") { ++g_skipped_friend; return; }
  const char* shape = shape_of(c.skill_class, c.dist);
  if (!shape) { ++g_skipped_shape; return; }
  int si = shape_index(shape);
  if (si >= 0 && !g_shape_on[si]) { ++g_skipped_shape; return; }
  if (!world::is_foe(c.caster_id)) { ++g_skipped_friend; note(std::format("{:.3f} skip friend {} {} #{} {:.1f}u", c.t, shape, c.caster_class, c.caster_id, c.dist)); return; }
  if (!c.has_pos || c.dist < 0 || c.dist > g_radius) { ++g_skipped_far; note(std::format("{:.3f} skip far {} #{} {:.1f}u", c.t, shape, c.caster_id, c.dist)); return; }
  auto it = g_last.find(c.caster_id);
  if (it != g_last.end() && c.t - it->second < 0.08) { ++g_skipped_dup; return; }
  g_last[c.caster_id] = c.t;
  if (g_last.size() > 256) g_last.clear();
  if (!mode_admits(c)) { ++g_skipped_mode; note(std::format("{:.3f} skip mode {} {} #{} {:.1f}u", c.t, shape, c.caster_class, c.caster_id, c.dist)); return; }
  // Level like the voices, not the pings: above the 0.6 master, full out to 9 u (world::voice_gain), so a
  // 100 ms blip next to you is as loud as the file (peak -1 dBFS x g_vol). The ping curve x master was ~-8 dB.
  float pan = 0, gain = 1, ahead = 0;
  world::ear_frame(c.caster_pos, pan, gain, &ahead);
  audio::play_sample(audio::module_dir() + "assets\\audio\\telegraphs\\" + shape + "-" + std::to_string(g_variant) + ".wav",
                     g_vol * world::voice_gain(c.dist), pan, world::rear_shelf_db(ahead), false);
  ++g_played;
  note(std::format("{:.3f} {} {} {} #{} {:.1f}u anim={}ms", c.t, shape, c.skill_class, c.caster_class, c.caster_id, c.dist, c.anim_ms));
}

std::string status() {
  std::lock_guard<std::mutex> l(g_mu);
  std::string shapes;
  for (int i = 0; i < kShapes; ++i) shapes += std::format("{}={} ", kShapeNames[i], g_shape_on[i] ? "on" : "off");
  std::string s = std::format("telegraph: mode={} ({}) shapes: {}variant={}ms vol={:.2f} radius={:.0f} played={} skipped: shape={} unknown={} friend={} far={} dup={} mode={}\n",
                              (int)g_mode, mode_name(g_mode), shapes, g_variant, g_vol, g_radius, g_played, g_skipped_shape, g_skipped_unknown, g_skipped_friend, g_skipped_far, g_skipped_dup, g_skipped_mode);
  for (auto& [cls, n] : g_unknown_classes) s += std::format("  unknown class {} x{}\n", cls, n);
  for (auto& r : g_recent) s += r + "\n";
  return s;
}
void init() {
  g_mode = (Mode)std::clamp(settings::get_int("telegraph.mode", (int)Mode::All), 0, 3);
  for (int i = 0; i < kShapes; ++i) g_shape_on[i] = settings::get_bool(std::string("telegraph.shape.") + kShapeNames[i], true);
}
Mode mode() { return g_mode; }
void set_mode(Mode m) { g_mode = m; settings::set_int("telegraph.mode", (int)m); }
std::string_view mode_name(Mode m) {
  switch (m) { case Mode::Off: return "off"; case Mode::Target: return "your target"; case Mode::HighestTier: return "highest tier"; case Mode::All: return "all"; }
  return "?";
}
bool shape_enabled(int shape) { return shape >= 0 && shape < kShapes && g_shape_on[shape]; }
void set_shape_enabled(int shape, bool on) {
  if (shape < 0 || shape >= kShapes) return;
  g_shape_on[shape] = on;
  settings::set_bool(std::string("telegraph.shape.") + kShapeNames[shape], on);
}
void set_volume(float v) { g_vol = v; }
void set_variant(int ms) { g_variant = ms; }
void test(const std::string& shape) {
  audio::play_sample(audio::module_dir() + "assets\\audio\\telegraphs\\" + shape + "-" + std::to_string(g_variant) + ".wav", g_vol, 0.0f, 0.0f, false);
}
}  // namespace gd::telegraph
