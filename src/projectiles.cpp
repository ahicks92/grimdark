#include "projectiles.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <set>
#include "app.h"
#include "audio.h"
#include "casts.h"
#include "cues.h"
#include "gd_names.h"
#include "log.h"
#include "wave_effects.h"
#include "world.h"

namespace gd::projectiles {
namespace {
using namespace gd::names;

// Exports (gd_names.h), resolved on first use. GetHitTime / GetMissTime are virtual; the base bodies only read two
// int fields (+0x570 / +0x574, Game.dll 0x406fc0), so calling them directly is the base reading, not a dispatch.
const void* (*g_proj_ci)() = nullptr;
const void* (*g_area_ci)() = nullptr;
unsigned (*g_get_id)(const void*) = nullptr;
unsigned (*g_get_owner)(const void*) = nullptr;
const char* (*g_obj_name)(const void*) = nullptr;
float (*g_speed)(void*) = nullptr;
float (*g_hit_time)(const void*) = nullptr;
float (*g_miss_time)(const void*) = nullptr;
float (*g_get_distance)(const void*) = nullptr;
float (*g_launch_angle)(const void*) = nullptr;   // the throw angle (+0x578, Game.dll 0x406fe0), clamped: 10 = none   // the flight range (+0x538, Game.dll 0x406e50): projectileDistance
bool g_loaded = false;
template <class F> void load(F& f, const char* dll, const char* name) { f = (F)GetProcAddress(GetModuleHandleA(dll), name); }
void load_api() {
  if (g_loaded) return;
  g_loaded = true;
  load(g_proj_ci, ProjectileBase_GetStaticClassInfo_DLL, ProjectileBase_GetStaticClassInfo);
  load(g_area_ci, ProjectileAreaEffect_GetStaticClassInfo_DLL, ProjectileAreaEffect_GetStaticClassInfo);
  load(g_get_id, Object_GetObjectId_DLL, Object_GetObjectId);
  load(g_get_owner, Entity_GetOwner_DLL, Entity_GetOwner);
  load(g_obj_name, Object_GetObjectName_DLL, Object_GetObjectName);
  load(g_speed, ProjectileBase_GetTotalSpeed_DLL, ProjectileBase_GetTotalSpeed);
  load(g_hit_time, ProjectileBase_GetHitTime_DLL, ProjectileBase_GetHitTime);
  load(g_miss_time, ProjectileBase_GetMissTime_DLL, ProjectileBase_GetMissTime);
  load(g_get_distance, ProjectileBase_GetDistance_DLL, ProjectileBase_GetDistance);
  load(g_launch_angle, ProjectileBase_GetLaunchAngle_DLL, ProjectileBase_GetLaunchAngle);
  log::writef("projectiles: exports class={} id={} owner={} name={} speed={} hit={} miss={}", (void*)g_proj_ci, (void*)g_get_id,
              (void*)g_get_owner, (void*)g_obj_name, (void*)g_speed, (void*)g_hit_time, (void*)g_miss_time);
}

// One projectile's readout under an SEH guard (the sphere query returns every Object in range; plain data only).
struct Raw { unsigned id = 0, owner = 0; float speed = -1, hit = -1, miss = -1, range = -1, angle = 0; char rec[160] = {}; };
bool read_raw(void* e, Raw& r) {
  __try {
    r.id = g_get_id(e);
    r.owner = g_get_owner ? g_get_owner(e) : 0;
    if (g_speed) r.speed = g_speed(e);
    if (g_hit_time) r.hit = g_hit_time(e);
    if (g_miss_time) r.miss = g_miss_time(e);
    if (g_get_distance) r.range = g_get_distance(e);
    if (g_launch_angle) r.angle = g_launch_angle(e);
    const char* n = g_obj_name ? g_obj_name(e) : nullptr;
    if (n && !IsBadStringPtrA(n, 512)) strncpy_s(r.rec, n, sizeof r.rec - 1);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Any entity's id, owner and record under an SEH guard (the cast-window probe: not necessarily a projectile).
bool read_basic(void* e, Raw& r) {
  __try {
    r.id = g_get_id(e);
    r.owner = g_get_owner ? g_get_owner(e) : 0;
    const char* n = g_obj_name ? g_obj_name(e) : nullptr;
    if (n && !IsBadStringPtrA(n, 512)) strncpy_s(r.rec, n, sizeof r.rec - 1);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// How a projectile is cued, decided at spawn from the game's own speed (ProjectileBase::GetTotalSpeed matched the
// measured flight within ~5 %, 2026-10-22). Fast = the crossbow bolts (38 u/s from ~12 u: ~0.27 s from appearing to
// arriving, faster than a reaction plus a step, so their tick says "a shot from there", once); slow = what can still
// be stepped around (deathbolts 20, Nomos Dred's orbs 16); still = a ground effect placed as a projectile (a sigil,
// speed 0), which is the hazard layer's to sound, not this one's.
enum class Pace : char { Still, Slow, Fast };
// The radius probe (dev, while recording): the area-effect radius has no export, and the same visual record serves
// skills of different radii (the sigil: 2.5 Balrazar / 3.2 Skeletal Warlock / up to 5.5 the player's), so it cannot be
// tabled by record either. On each new enemy ProjectileAreaEffect, every float in the object's first kProbeBytes that
// looks like a radius (0.5..50, two decimals) is logged as off=value; matching the logs of two records with known radii
// (Kilrian's Conflagration 3.0, the warlock's sigil 3.2) names the field.
constexpr size_t kProbeBytes = 0x1000;
struct ProbeBuf { float f[kProbeBytes / 4]; };
bool read_probe(const void* e, ProbeBuf& out) {
  __try { memcpy(&out, e, sizeof out); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::string probe_line(const void* e) {
  if (IsBadReadPtr(e, kProbeBytes)) return "unreadable";
  static ProbeBuf b;
  if (!read_probe(e, b)) return "faulted";
  std::string s;
  for (size_t i = 0; i < kProbeBytes / 4; ++i) {
    float v = b.f[i];
    if (!(v >= 0.5f && v <= 50.0f)) continue;
    float r = std::round(v * 100.0f) / 100.0f;
    if (std::fabs(v - r) > 1e-5f) continue;
    s += std::format(" {:#x}={:g}", i * 4, r);
  }
  return s;
}

// The area's damage radius (static RE 2026-10-22): the projectile's targeting pass (Game.dll 0x408a00, the two
// GameEngine::GetTargetsInRadius calls at +0x408a88 / +0x40c576) walks a vector of pointers at +0xd18 (begin) /
// +0xd20 (end) and queries targets within [entry + 4] (float) of the position at +0x748. The probe of the object's own
// floats never found the Elemental Seal's 3.5: the radius lives behind those pointers. The largest entry is the area.
// -1 when the vector is empty, unreadable or implausible (outside 0.3..20 u).
float read_area_radius_raw(const void* e) {
  __try {
    const char* o = (const char*)e;
    void* const* b = *(void* const* const*)(o + 0xd18);
    void* const* en = *(void* const* const*)(o + 0xd20);
    if (!b || en <= b || en - b > 64) return -1;
    float best = -1;
    for (void* const* it = b; it < en; ++it) {
      if (!*it) continue;
      float r = *(const float*)((const char*)*it + 4);
      if (r > best) best = r;
    }
    return best;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
float area_radius(const void* e) {
  if (IsBadReadPtr(e, 0xd28)) return -1;
  float r = read_area_radius_raw(e);
  return r >= 0.3f && r <= 20.0f ? r : -1;
}

// The target probe (dev, while recording, 2026-10-22): the Grand Priest's fireballs (ProjectileExploding) end at the
// point their cast was aimed at, the deathbolt (ProjectileFireballLike) flies through it. To predict from where a
// projectile is GOING instead of per-class rules (the user), find whether the projectile stores that point: on each new
// enemy projectile whose owner released an aimed skill in the last 0.6 s, search its first kProbeBytes for the cast's
// target point in region-relative coordinates (x and z within 0.3 u, y free -- the point may be floored differently)
// and log every offset that matches.
std::string target_probe_line(const void* e, const world::Vec3& target) {
  alignas(16) unsigned char wv[64] = {};
  if (!world::world_vec3_at(target, wv)) return " no-region";
  float rel[3]; memcpy(rel, wv + 8, sizeof rel);   // WorldVec3 = Region* (+0) + region-relative Vec3 (+8)
  if (IsBadReadPtr(e, kProbeBytes)) return " unreadable";
  static ProbeBuf b;
  if (!read_probe(e, b)) return " faulted";
  std::string s = std::format(" rel=({:.1f},{:.1f},{:.1f})", rel[0], rel[1], rel[2]);
  int hits = 0;
  for (size_t i = 0; i + 2 < kProbeBytes / 4; ++i) {
    if (std::fabs(b.f[i] - rel[0]) < 0.3f && std::fabs(b.f[i + 2] - rel[2]) < 0.3f) {
      s += std::format(" {:#x}=({:.1f},{:.1f},{:.1f})", i * 4, b.f[i], b.f[i + 1], b.f[i + 2]);
      if (++hits >= 8) break;
    }
  }
  return hits ? s : s + " none";
}

// The destination (2026-10-22): the target probe found every enemy projectile's cast target point at +0x738 -- the Vec3
// of a WorldVec3 whose Region* is +0x730 -- for arrows, fireballs, grenades and a sigil alike; the probes that "missed"
// had their target near a region edge, stored relative to the projectile's own region, not the target's. Read whole and
// converted to world coordinates; false when the region pointer is null or the read faults.
bool read_dest_raw(const void* e, world::Vec3& out) {
  __try {
    if (IsBadReadPtr((const char*)e + 0x730, 0x18)) return false;
    return world::world_point((const char*)e + 0x730, out);
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct Track {
  unsigned owner = 0; bool foe = false; Pace pace = Pace::Slow; std::string who, cls, rec;
  bool area = false;      // a ProjectileAreaEffect: once it stops moving it is ground harm (a sigil, a landed pool)
  int still_frames = 0;   // consecutive frames it has not moved
  bool probed = false;
  float radius = -1;      // the area's own radius once landed (area_radius); -1 = not read / implausible
  double next_cue = 0;   // when this projectile's next tick plays (foes only; a fast one never ticks again)
  float range = -1;       // its flight range (GetDistance); -1 = unknown: it flies on, as far as the prediction knows
  float flown = 0;        // path length since first seen
  int ticks = 0, accented = 0;
  bool has_dest = false; world::Vec3 dest;   // where it is going (read_dest_raw at first sight)
  bool wave = false;      // a ground wave's travelling effect (an EffectEntity, not a projectile): none of the
                          // ProjectileBase reads apply (destination, blast radius, area); the owner is the caster
  bool lobbed = false;    // thrown in an arc (launch angle > 0) at a real destination: it lands THERE
  float blast = -1;       // its blast radius (area_radius: the +0xd18 targeting entries), -1 until read
  world::Vec3 me_last;    // the player's position the last frame it was seen (where the player stood when it ended)
  double t0 = 0, t_last = 0, t_min = 0;
  world::Vec3 p0, p_last, me_min;
  float min_d = 1e9f, min_dy = 0;
  int frames = 0;
  bool predicted = false; float pred_miss = -1, pred_t = -1;   // from the first two positions: the player's distance from
                                                               // the line of travel, and when it passes closest
};
std::map<unsigned, Track> g_live;
std::deque<std::string> g_lines, g_samples;
std::mutex g_mu;
// Recording starts on by itself in a dev launch (GRIMDARK_PORT set: inject.ps1 -Launch, launch.sh), so a session played
// without anyone switching it on through /projectiles is not lost; a player build stays off.
bool g_on = GetEnvironmentVariableW(L"GRIMDARK_PORT", nullptr, 0) > 0, g_with_samples = false;
// The cue (2026-10-22 prototype): every slow enemy projectile ticks projectile.wav from where it is, every
// g_cue_interval while it flies -- all of them, not only the ones aimed at you, so stepping away from one never walks
// you into the next unheard (the user's rule). One that will come within g_accent_miss of you ticks g_accent_st
// semitones higher (within its blast radius + g_body when readable): a lobbed one (Track::lobbed) when you stand
// within that of where it lands; a straight one by its
// current line of travel, still approaching, cut short at its range -- past it the projectile explodes where it is
// (explodeOnMiss), so the end point counts, not the line beyond it. 2.5 u = a fireball's 1.5-1.8
// explosion + the body + margin: the 1.5 u centre-to-centre first cut missed the Flame Revenant's fireballs that hit
// (the user, 2026-10-22; 16 of 123 came within 1.5-3 u). A fast one (g_fast_speed and up) ticks once, at launch, at
// g_fast_vol of the level: by the time a second tick could play it has arrived.
bool g_cue = true;
float g_cue_vol = 0.6f, g_cue_interval = 0.15f, g_accent_miss = 2.5f, g_accent_st = 5.0f;
float g_fast_speed = 25.0f, g_fast_vol = 0.6f;
// Projectiles that appear in the same frame (a nova: Rolderathis's 14 ice spikes) start their ticks g_stagger apart,
// wrapped within one g_cue_interval: in lockstep, 14 identical ticks from nearly one place fused into what sounded like
// a single projectile (the user, 2026-10-22). Timing only -- a per-projectile pitch flanged in a pack (CLAUDE.md).
float g_stagger = 0.02f;
// The near-miss zone: a projectile hurts the player when its blast reaches the body, so the zone is its own blast
// radius plus g_body; g_accent_miss when the radius is not readable. Rolderathis's freeze blast is 2.5 u: the fixed
// 2.5 u zone (centre to centre) missed the 2.5..3 band where it still hit (the user, 2026-10-22).
float g_body = 0.75f;
uint64_t g_ticks = 0, g_accents = 0, g_fast_ticks = 0;
// Enemy ground effects -> the hazard sounds (2026-10-22, the user: "the ground hazard sounds aren't playing for ground
// AoEs like that sigil"). The hazard layer reads only the map's painted damage cells (docs/hazards.md); a skill's ground
// effect is a ProjectileAreaEffect entity instead. Every enemy one that has stopped moving becomes a world::GroundZone
// for as long as the entity lives (the game owns the duration). The radius is not read from the entity yet (no export;
// ProcessFriendsInArea's +0xd88 list is the ALLY buffs' radii): g_zone_radius stands in -- the sigil's record says 3.2,
// Kilrian's Conflagration 3.0, the pool templates' median 3.
bool g_zones = true;
float g_zone_radius = 3.0f;
size_t g_zone_count = 0, g_zone_max = 0;
float g_radius = 30.0f;
// The cast-window probe (watch_cast): after an enemy cast of a watched class, for g_watch_s seconds every non-projectile
// entity that was not in the sweep at the window's first frame is tracked and logged. g_watch_all = every foe cast.
bool g_watch_all = false;
double g_watch_s = 4.0, g_watch_until = -1;
bool g_watch_baseline = false;   // the next frame records the ids already present
std::set<unsigned> g_watch_old;
struct Seen { std::string cls, rec; unsigned owner = 0; world::Vec3 p0, p_last; double t0 = 0, t_last = 0; float flown = 0, min_d = 1e9f; int frames = 0; };
std::map<unsigned, Seen> g_watch_new;
std::string g_watch_what;   // the cast that opened the window, for the log
// Ground waves (2026-10-23): a Skill_AttackWave sends an EffectEntity along the ground (Voldrak's Groundbreaker: about
// 1.1 s after the cast starts, 7-14 u in 1.4 s, ~6-10 u/s) and no projectile. The entity has no owner, and its record can
// belong to a player's skill too, so it counts only when it appears within kWaveWindow s of a foe's cast of a skill that
// names it (src/wave_effects.h, generated): then it is tracked and ticked like a slow projectile owned by that caster.
struct WaveWait { std::string effect; unsigned caster = 0; double until = 0; };
std::vector<WaveWait> g_wave_wait;
constexpr double kWaveWindow = 3.0;
uint64_t g_waves = 0;
std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
uint64_t g_frames = 0, g_spawns = 0, g_faulted = 0, g_spawns_foe = 0, g_spawns_you = 0, g_spawns_other = 0;
size_t g_max_live = 0;
double g_sweep_us_total = 0, g_sweep_us_max = 0;

// The recording also goes to %LOCALAPPDATA%\Grimdark\projectiles.log, APPENDED: grimdark.log is truncated on each
// load and the in-memory ring dies with the game, which lost a whole evening of probe lines (2026-10-22). One header
// line per session; flushed per line so a crash keeps what came before it.
FILE* g_persist = nullptr;
bool g_persist_tried = false;
void persist(const std::string& s) {
  if (!g_persist_tried) {
    g_persist_tried = true;
    std::wstring path = log::path();
    size_t slash = path.find_last_of(L"\\/");
    path = (slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1)) + L"projectiles.log";
    g_persist = _wfopen(path.c_str(), L"a");
    if (g_persist) {
      SYSTEMTIME st; GetLocalTime(&st);
      fprintf(g_persist, "=== session %04d-%02d-%02d %02d:%02d:%02d (times below are app seconds) ===\n", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);
    }
  }
  if (!g_persist) return;
  fputs(s.c_str(), g_persist); fputc('\n', g_persist); fflush(g_persist);
}
void note(std::deque<std::string>& q, std::string s, size_t cap) {
  if (&q == &g_lines) persist(s);
  std::lock_guard<std::mutex> l(g_mu);
  q.push_back(std::move(s));
  while (q.size() > cap) q.pop_front();
}
float flat(const world::Vec3& a, const world::Vec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }
std::string pos(const world::Vec3& p) { return std::format("({:.1f},{:.1f},{:.1f})", p.x, p.y, p.z); }

void watch_gone(unsigned id, const Seen& s, double t, const char* why) {
  note(g_lines, std::format("{:.3f} WGONE #{} {} {} owner=#{} life={:.2f}s frames={} {} -> {} travelled={:.1f}u flown={:.1f}u min_d={:.1f} ({})", t, id, s.cls, s.rec,
                            s.owner, s.t_last - s.t0, s.frames, pos(s.p0), pos(s.p_last), flat(s.p0, s.p_last), s.flown, s.min_d, why), 5000);
}
// One frame of the cast-window probe over the sweep's entities.
void watch_frame(const std::vector<void*>& ents, const void* proj_ci, const world::Vec3& me, double t) {
  if (g_watch_until < 0) return;
  if (t > g_watch_until) {
    for (auto& [id, s] : g_watch_new) watch_gone(id, s, t, "window closed");
    g_watch_new.clear(); g_watch_old.clear(); g_watch_until = -1;
    note(g_lines, std::format("{:.3f} WATCH end", t), 5000);
    return;
  }
  std::set<unsigned> seen;
  for (void* e : ents) {
    if (world::object_is_a(e, proj_ci)) continue;   // projectiles have their own track
    Raw r;
    if (!read_basic(e, r) || !r.id) continue;
    if (g_watch_baseline) { g_watch_old.insert(r.id); continue; }
    if (g_watch_old.count(r.id)) continue;
    world::Vec3 p;
    if (!world::entity_position(r.id, p)) continue;
    seen.insert(r.id);
    auto it = g_watch_new.find(r.id);
    if (it == g_watch_new.end()) {
      Seen s; s.cls = world::object_class_name(e); s.rec = r.rec; s.owner = r.owner; s.p0 = s.p_last = p; s.t0 = t;
      note(g_lines, std::format("{:.3f} WNEW #{} {} {} owner=#{}{} at {} d={:.1f} dy={:+.1f}", t, r.id, s.cls, s.rec, r.owner,
                                r.owner ? std::format(" \"{}\"", world::label_of(r.owner)) : std::string(), pos(p), flat(p, me), p.y - me.y), 5000);
      it = g_watch_new.emplace(r.id, std::move(s)).first;
    }
    Seen& s = it->second;
    s.flown += flat(p, s.p_last);
    s.min_d = std::min(s.min_d, flat(p, me));
    s.p_last = p; s.t_last = t; ++s.frames;
  }
  g_watch_baseline = false;
  for (auto it = g_watch_new.begin(); it != g_watch_new.end();) {
    if (seen.count(it->first)) { ++it; continue; }
    watch_gone(it->first, it->second, t, "gone");
    it = g_watch_new.erase(it);
  }
}
void gone(unsigned id, const Track& k, double t) {
  float travelled = flat(k.p0, k.p_last);
  double life = k.t_last - k.t0;
  std::string pred = k.predicted ? std::format(" pred_miss={:.1f} pred_t=+{:.2f}s", k.pred_miss, k.pred_t) : " pred=none";
  note(g_lines, std::format("{:.3f} GONE #{} {} {} life={:.2f}s frames={} {} -> {} travelled={:.1f}u avg={:.1f}u/s min_d={:.1f} dy={:+.1f} at +{:.2f}s{} range={:.1f} flown={:.1f} accented={}/{} end_from_dest={:.1f} launch_to_dest={:.1f} blast={:.1f} you_from_end={:.1f} you_from_dest={:.1f} (unseen {:.0f}ms)",
                            t, id, k.who, k.cls, life, k.frames, pos(k.p0), pos(k.p_last), travelled, life > 0 ? travelled / life : 0.0,
                            k.min_d, k.min_dy, k.t_min - k.t0, pred, k.range, k.flown, k.accented, k.ticks, k.has_dest ? flat(k.p_last, k.dest) : -1.0f,
                            k.has_dest ? flat(k.p0, k.dest) : -1.0f, k.blast, flat(k.me_last, k.p_last), k.has_dest ? flat(k.me_last, k.dest) : -1.0f,
                            (t - k.t_last) * 1000), 5000);
}
// Level by distance: full within 3 u, linear down to 0.15 at 30 u (the sweep radius).
float cue_gain(float d) { return d <= 3.0f ? 1.0f : std::max(0.15f, 1.0f - 0.85f * (d - 3.0f) / 27.0f); }
void play_cue(const world::Vec3& p, float d, bool accent, float level = 1.0f) {
  if (!cues::enabled(cues::Projectiles)) return;   // the player's switch (Ctrl+T); g_cue is the dev one
  level *= cues::gain(cues::ProjectileChannel);
  float pan = 0, gain = 1, ahead = 0;
  world::ear_frame(p, pan, gain, &ahead);
  audio::play_sample(audio::module_dir() + "assets\\audio\\projectile.wav", g_cue_vol * level * cue_gain(d), pan, world::rear_shelf_db(ahead), false, 0,
                     false, accent ? g_accent_st : 0.0f);
  ++g_ticks;
  if (accent) ++g_accents;
}
}  // namespace

void tick() {
  if (!g_on && !g_cue && !g_zones) { if (g_zone_count) { world::set_ground_zones({}); g_zone_count = 0; } return; }
  load_api();
  if (!g_proj_ci || !g_get_id) return;
  world::Vec3 me;
  if (!world::player_position(me)) { world::set_ground_zones({}); g_zone_count = 0; return; }
  std::vector<world::GroundZone> zones;
  unsigned pid = world::player_id();
  double t = app::now();
  auto c0 = std::chrono::steady_clock::now();
  const void* ci = g_proj_ci();
  std::set<unsigned> seen;
  int spawned_now = 0;   // new projectiles this frame, for the tick stagger
  const std::vector<void*> ents = world::entities_near(g_radius);
  if (g_on) watch_frame(ents, ci, me, t);
  std::erase_if(g_wave_wait, [&](const WaveWait& w) { return t > w.until; });
  bool waves_live = std::any_of(g_live.begin(), g_live.end(), [](const auto& kv) { return kv.second.wave; });
  for (void* e : ents) {
    Raw r;
    bool wave = false;
    if (world::object_is_a(e, ci)) {
      if (!read_raw(e, r)) { ++g_faulted; continue; }
    } else {
      if (g_wave_wait.empty() && !waves_live) continue;
      if (!read_basic(e, r) || !r.id) continue;
      auto lt = g_live.find(r.id);
      if (lt != g_live.end()) {
        if (!lt->second.wave) continue;
        r.owner = lt->second.owner;
      } else {
        std::string rec = lower(r.rec);
        auto w = std::find_if(g_wave_wait.rbegin(), g_wave_wait.rend(), [&](const WaveWait& x) { return x.effect == rec; });
        if (w == g_wave_wait.rend()) continue;
        r.owner = w->caster;
        ++g_waves;
      }
      wave = true;
    }
    if (!r.id) continue;
    world::Vec3 p;
    if (!world::entity_position(r.id, p)) continue;
    seen.insert(r.id);
    float d = flat(p, me);
    auto it = g_live.find(r.id);
    if (it == g_live.end()) {
      Track k;
      k.owner = r.owner; k.rec = r.rec;
      k.foe = r.owner != pid && world::is_foe(r.owner);
      std::string rel = r.owner == pid ? "you" : k.foe ? "foe" : "other";
      (rel == "you" ? g_spawns_you : rel == "foe" ? g_spawns_foe : g_spawns_other)++;
      ++g_spawns;
      k.t0 = t; k.p0 = p;
      k.next_cue = t + std::fmod(spawned_now++ * g_stagger, g_cue_interval);
      k.wave = wave;
      k.pace = wave ? Pace::Slow : r.speed <= 0.5f ? Pace::Still : r.speed < g_fast_speed ? Pace::Slow : Pace::Fast;
      k.range = r.range > 0.5f && r.range < 200.0f ? r.range : -1.0f;
      k.has_dest = !wave && read_dest_raw(e, k.dest);
      // How it will fly, from its own state (2026-10-22, after the Grand Priest): a launch angle above zero is a throw in
      // an arc that lands at the destination (his fireballs 35, the ice grenades 25: both ended at their cast's aim
      // point); zero is a straight shot that flies on through the aim point to its range (the deathbolt). A destination
      // equal to the launch point is no aim at all (a fragment of a burst: Rolderathis's ice spikes), so straight.
      // GetLaunchAngle CLAMPS: a projectile with no launch angle reads 10 (the floor; the Frost Revenant's straight ice
      // spikes were taken for lobbed and missed their warning, 2026-10-22). Every lobbed record has 18 or more.
      k.lobbed = r.angle > 12.0f && k.has_dest && flat(p, k.dest) > 0.5f;
      if (g_on && k.foe && !wave) {
        world::Vec3 aim;
        if (casts::recent_target(r.owner, t, 0.6, aim))
          note(g_lines, std::format("{:.3f} TARGET #{} {} {} aim=({:.1f},{:.1f},{:.1f}) launch {:.1f}u from it: dest {} -> {:.2f}u from aim;{}", t, r.id,
                                    world::object_class_name(e), r.rec, aim.x, aim.y, aim.z, flat(p, aim),
                                    k.has_dest ? pos(k.dest) : std::string("none"), k.has_dest ? flat(k.dest, aim) : -1.0f, target_probe_line(e, aim)), 5000);
      }
      k.area = !wave && g_area_ci && world::object_is_a(e, g_area_ci());
      if (g_on) {   // the names only while recording: the label lookup is the expensive part of a spawn
        k.cls = wave ? "wave:" + world::object_class_name(e) : world::object_class_name(e);
        k.who = std::format("[{} #{} \"{}\"]", rel, r.owner, r.owner == pid ? std::string("you") : world::label_of(r.owner));
        note(g_lines, std::format("{:.3f} SPAWN #{} {} {} {} at {} d={:.1f} dy={:+.1f} speed={:.1f} range={:.1f} angle={:.0f}{} dest={} dest_from_launch={:.1f} dest_from_you={:.1f}",
                                t, r.id, k.who, k.cls, k.rec, pos(p), d, p.y - me.y, r.speed, r.range, r.angle, k.lobbed ? " LOBBED" : "", k.has_dest ? pos(k.dest) : std::string("none"),
                                k.has_dest ? flat(p, k.dest) : -1.0f, k.has_dest ? flat(me, k.dest) : -1.0f), 5000);
      }
      it = g_live.emplace(r.id, std::move(k)).first;
    }
    Track& k = it->second;
    if (k.frames > 0) {
      float step = flat(p, k.p_last);
      k.still_frames = step < 0.02f ? k.still_frames + 1 : 0;
      k.flown += step;
    }
    bool landed = k.area && k.still_frames >= 2;   // a ground effect from here on: the hazard sounds own it
    if (!k.wave && k.blast < 0 && k.frames < 10) k.blast = area_radius(e);
    k.me_last = me;
    const float zone = k.blast > 0 ? k.blast + g_body : g_accent_miss;
    if (landed && k.radius < 0) k.radius = area_radius(e);
    if (g_zones && k.foe && landed) zones.push_back({p, k.radius > 0 ? k.radius : g_zone_radius});
    if (g_on && (k.foe || k.owner == pid) && landed && !k.probed) {   // at landing: the skill may set the radius after creating it;
                                                                    // the player's own too (Elemental Seal 3.5 vs Conflagration 3.0)
      k.probed = true;
      note(g_lines, std::format("{:.3f} PROBE #{} {} radius={:g} raw={:g}:{}", t, r.id, k.rec, k.radius, IsBadReadPtr(e, 0xd28) ? -2.0f : read_area_radius_raw(e),
                                probe_line(e)), 5000);
    }
    // The first movement fixes the line of travel: how far from it the player stands, and when it passes closest.
    if (!k.predicted && k.frames > 0) {
      float vx = p.x - k.p0.x, vz = p.z - k.p0.z, dt = (float)(t - k.t0), len = std::hypot(vx, vz);
      if (len > 0.05f && dt > 0) {
        float ux = vx / len, uz = vz / len, rx = me.x - k.p0.x, rz = me.z - k.p0.z;
        float along = rx * ux + rz * uz;
        k.pred_miss = std::fabs(rx * uz - rz * ux);
        k.pred_t = along / (len / dt);
        k.predicted = true;
      }
    }
    if (g_cue && k.foe && k.pace == Pace::Fast && t >= k.next_cue) {
      play_cue(p, d, false, g_fast_vol);   // once, where it was launched
      ++g_fast_ticks;
      k.next_cue = 1e300;
    } else if (g_cue && k.foe && k.pace == Pace::Slow && !landed && t >= k.next_cue) {
      // Accent from the CURRENT line of travel (last frame's step), still approaching: the closest point of the line,
      // or the end of the flight when the range runs out first. Before it has moved, no accent.
      bool accent = false;
      float vx = p.x - k.p_last.x, vz = p.z - k.p_last.z, len = std::hypot(vx, vz);
      if (k.lobbed) {
        // It lands at its destination, whatever its path looks like now -- but only while it is in flight: a throw blocked
        // at the hand sits still at the launch point and bursts there (6 of Rolderathis's 8 grenades, 0.0 u travelled,
        // 2026-10-22), which warned a player standing at its destination for nothing.
        accent = k.frames > 0 && len > 0.01f && flat(me, k.dest) < zone;
      } else if (k.frames > 0 && len > 0.01f) {
        float ux = vx / len, uz = vz / len, rx = me.x - p.x, rz = me.z - p.z;
        float along = rx * ux + rz * uz;
        if (along > 0) {
          float miss = std::fabs(rx * uz - rz * ux);
          if (k.range > 0) {
            float left = std::max(0.0f, k.range - k.flown);
            if (along > left) miss = std::hypot(rx - ux * left, rz - uz * left);   // it explodes short of the closest point
          }
          accent = miss < zone;
        }
      }
      ++k.ticks;
      if (accent) ++k.accented;
      // A lobbed one in flight sounds from WHERE IT WILL LAND (the user, 2026-10-22): the arc's position says nothing
      // about which way to step out of the blast. Not moving (a blocked throw) = it may burst where it is: sound there.
      if (k.lobbed && k.frames > 0 && len > 0.01f) play_cue(k.dest, flat(me, k.dest), accent);
      else play_cue(p, d, accent);
      k.next_cue = t + g_cue_interval;
    }
    if (d < k.min_d) { k.min_d = d; k.min_dy = p.y - me.y; k.t_min = t; k.me_min = me; }
    k.p_last = p; k.t_last = t; ++k.frames;
    if (g_with_samples) note(g_samples, std::format("{:.3f} #{} {} d={:.1f}", t, r.id, pos(p), d), 20000);
  }
  for (auto it = g_live.begin(); it != g_live.end();) {
    if (seen.count(it->first)) { ++it; continue; }
    if (g_on) gone(it->first, it->second, t);
    it = g_live.erase(it);
  }
  g_zone_count = zones.size();
  g_zone_max = std::max(g_zone_max, g_zone_count);
  world::set_ground_zones(std::move(zones));
  g_max_live = std::max(g_max_live, g_live.size());
  ++g_frames;
  double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - c0).count();
  g_sweep_us_total += us; g_sweep_us_max = std::max(g_sweep_us_max, us);
}

void set_cue(bool on) { g_cue = on; if (!on && !g_on && !g_zones) g_live.clear(); }
void set_zones(bool on, float radius) {
  g_zones = on;
  if (radius > 0) g_zone_radius = std::clamp(radius, 0.5f, 15.0f);
}
bool cue() { return g_cue; }
void set_cue_params(float vol, float interval, float accent_miss, float accent_st) {
  if (vol >= 0) g_cue_vol = std::clamp(vol, 0.0f, 2.0f);
  if (interval > 0) g_cue_interval = std::clamp(interval, 0.05f, 2.0f);
  if (accent_miss >= 0) g_accent_miss = accent_miss;
  if (accent_st > -100) g_accent_st = accent_st;
}
void set_stagger(float s) { if (s >= 0) g_stagger = std::clamp(s, 0.0f, 1.0f); }
void set_fast_params(float speed, float vol) {
  if (speed > 0) g_fast_speed = speed;
  if (vol >= 0) g_fast_vol = std::clamp(vol, 0.0f, 2.0f);
}

void set_recording(bool on) {
  if (g_on && !on) { double t = app::now(); for (auto& [id, k] : g_live) gone(id, k, t); g_live.clear(); }
  if (!g_on && on) g_live.clear();   // tracks the cue opened carry no names: start the recording fresh
  g_on = on;
  log::writef("projectiles: recording {}", on ? "on" : "off");
}
bool recording() { return g_on; }
void set_radius(float r) { g_radius = std::clamp(r, 5.0f, 60.0f); }
void set_samples(bool on) { g_with_samples = on; }

std::string dump(int max_lines) {
  std::string s = std::format("cue={} vol={:.2f} interval={:.2f}s accent<{:.1f}u +{:.0f}st fast>={:.0f}u/s at x{:.2f} stagger={:.0f}ms ticks={} (fast {}) accents={}\n", g_cue,
                              g_cue_vol, g_cue_interval, g_accent_miss, g_accent_st, g_fast_speed, g_fast_vol, g_stagger * 1000, g_ticks, g_fast_ticks, g_accents);
  s += std::format("ground zones={} radius={:.1f} live={} max={}\n", g_zones, g_zone_radius, g_zone_count, g_zone_max);
  s += std::format("ground waves: {} ticked, {} awaited\n", g_waves, g_wave_wait.size());
  s += std::format("cast watch: {} for {:.1f}s{}\n", g_watch_all ? "every foe cast" : "Skill_AttackWave", g_watch_s, g_watch_until >= 0 ? " (open)" : "");
  s += std::format("recording={} radius={:.0f} samples={} frames={} spawns={} (foe {} / you {} / other {}) live={} max_live={} faulted={} sweep avg={:.0f}us max={:.0f}us\n",
                              g_on, g_radius, g_with_samples, g_frames, g_spawns, g_spawns_foe, g_spawns_you, g_spawns_other, g_live.size(), g_max_live,
                              g_faulted, g_frames ? g_sweep_us_total / g_frames : 0.0, g_sweep_us_max);
  for (auto& [id, k] : g_live) s += std::format("  live #{} {} {} {} frames={} at {}\n", id, k.who, k.cls, k.rec, k.frames, pos(k.p_last));
  std::lock_guard<std::mutex> l(g_mu);
  size_t n = std::min(g_lines.size(), (size_t)std::max(0, max_lines));
  for (size_t i = g_lines.size() - n; i < g_lines.size(); ++i) s += g_lines[i] + "\n";
  if (g_with_samples) {
    size_t m = std::min(g_samples.size(), (size_t)std::max(0, max_lines));
    s += std::format("--- samples (last {} of {})\n", m, g_samples.size());
    for (size_t i = g_samples.size() - m; i < g_samples.size(); ++i) s += g_samples[i] + "\n";
  }
  return s;
}
void watch_cast(unsigned caster_id, const std::string& skill_class, const std::string& record) {
  double t = app::now();
  if (g_cue && skill_class == "Skill_AttackWave") {
    static const std::multimap<std::string, std::string> by_skill = [] {
      std::multimap<std::string, std::string> m;
      for (const WaveEffect& w : kWaveEffects) m.emplace(w.skill, w.effect);
      return m;
    }();
    auto [a, b] = by_skill.equal_range(lower(record));
    for (auto it = a; it != b; ++it) g_wave_wait.push_back({it->second, caster_id, t + kWaveWindow});
    if (g_wave_wait.size() > 64) g_wave_wait.erase(g_wave_wait.begin(), g_wave_wait.end() - 64);
  }
  if (!g_on || (!g_watch_all && skill_class != "Skill_AttackWave")) return;
  bool fresh = g_watch_until < 0;
  g_watch_until = t + g_watch_s;   // a second cast inside the window extends it (keeps the same baseline)
  if (fresh) { g_watch_baseline = true; g_watch_old.clear(); g_watch_new.clear(); }
  note(g_lines, std::format("{:.3f} WATCH {} {} by #{} \"{}\" for {:.1f}s", t, skill_class, record, caster_id, world::label_of(caster_id), g_watch_s), 5000);
}
void set_watch(int all, float seconds) {
  if (all >= 0) g_watch_all = all != 0;
  if (seconds > 0) g_watch_s = std::clamp(seconds, 0.5f, 20.0f);
}
void clear() {
  std::lock_guard<std::mutex> l(g_mu);
  g_lines.clear(); g_samples.clear();
  g_frames = g_spawns = g_faulted = g_spawns_foe = g_spawns_you = g_spawns_other = 0;
  g_max_live = 0; g_sweep_us_total = g_sweep_us_max = 0;
}
}  // namespace gd::projectiles
