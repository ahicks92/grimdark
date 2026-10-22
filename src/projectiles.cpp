#include "projectiles.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <set>
#include "app.h"
#include "audio.h"
#include "gd_names.h"
#include "log.h"
#include "world.h"

namespace gd::projectiles {
namespace {
using namespace gd::names;

// Exports (gd_names.h), resolved on first use. GetHitTime / GetMissTime are virtual; the base bodies only read two
// int fields (+0x570 / +0x574, Game.dll 0x406fc0), so calling them directly is the base reading, not a dispatch.
const void* (*g_proj_ci)() = nullptr;
unsigned (*g_get_id)(const void*) = nullptr;
unsigned (*g_get_owner)(const void*) = nullptr;
const char* (*g_obj_name)(const void*) = nullptr;
float (*g_speed)(void*) = nullptr;
float (*g_hit_time)(const void*) = nullptr;
float (*g_miss_time)(const void*) = nullptr;
bool g_loaded = false;
template <class F> void load(F& f, const char* dll, const char* name) { f = (F)GetProcAddress(GetModuleHandleA(dll), name); }
void load_api() {
  if (g_loaded) return;
  g_loaded = true;
  load(g_proj_ci, ProjectileBase_GetStaticClassInfo_DLL, ProjectileBase_GetStaticClassInfo);
  load(g_get_id, Object_GetObjectId_DLL, Object_GetObjectId);
  load(g_get_owner, Entity_GetOwner_DLL, Entity_GetOwner);
  load(g_obj_name, Object_GetObjectName_DLL, Object_GetObjectName);
  load(g_speed, ProjectileBase_GetTotalSpeed_DLL, ProjectileBase_GetTotalSpeed);
  load(g_hit_time, ProjectileBase_GetHitTime_DLL, ProjectileBase_GetHitTime);
  load(g_miss_time, ProjectileBase_GetMissTime_DLL, ProjectileBase_GetMissTime);
  log::writef("projectiles: exports class={} id={} owner={} name={} speed={} hit={} miss={}", (void*)g_proj_ci, (void*)g_get_id,
              (void*)g_get_owner, (void*)g_obj_name, (void*)g_speed, (void*)g_hit_time, (void*)g_miss_time);
}

// One projectile's readout under an SEH guard (the sphere query returns every Object in range; plain data only).
struct Raw { unsigned id = 0, owner = 0; float speed = -1, hit = -1, miss = -1; char rec[160] = {}; };
bool read_raw(void* e, Raw& r) {
  __try {
    r.id = g_get_id(e);
    r.owner = g_get_owner ? g_get_owner(e) : 0;
    if (g_speed) r.speed = g_speed(e);
    if (g_hit_time) r.hit = g_hit_time(e);
    if (g_miss_time) r.miss = g_miss_time(e);
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
struct Track {
  unsigned owner = 0; bool foe = false; Pace pace = Pace::Slow; std::string who, cls, rec;
  double next_cue = 0;   // when this projectile's next tick plays (foes only; a fast one never ticks again)
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
bool g_on = false, g_with_samples = false;
// The cue (2026-10-22 prototype): every slow enemy projectile ticks projectile.wav from where it is, every
// g_cue_interval while it flies -- all of them, not only the ones aimed at you, so stepping away from one never walks
// you into the next unheard (the user's rule). One whose current line of travel passes within g_accent_miss of you,
// still approaching, ticks g_accent_st semitones higher. A fast one (g_fast_speed and up) ticks once, at launch, at
// g_fast_vol of the level: by the time a second tick could play it has arrived.
bool g_cue = true;
float g_cue_vol = 0.6f, g_cue_interval = 0.15f, g_accent_miss = 1.5f, g_accent_st = 5.0f;
float g_fast_speed = 25.0f, g_fast_vol = 0.6f;
uint64_t g_ticks = 0, g_accents = 0, g_fast_ticks = 0;
float g_radius = 30.0f;
uint64_t g_frames = 0, g_spawns = 0, g_faulted = 0, g_spawns_foe = 0, g_spawns_you = 0, g_spawns_other = 0;
size_t g_max_live = 0;
double g_sweep_us_total = 0, g_sweep_us_max = 0;

void note(std::deque<std::string>& q, std::string s, size_t cap) {
  std::lock_guard<std::mutex> l(g_mu);
  q.push_back(std::move(s));
  while (q.size() > cap) q.pop_front();
}
float flat(const world::Vec3& a, const world::Vec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }
std::string pos(const world::Vec3& p) { return std::format("({:.1f},{:.1f},{:.1f})", p.x, p.y, p.z); }

void gone(unsigned id, const Track& k, double t) {
  float travelled = flat(k.p0, k.p_last);
  double life = k.t_last - k.t0;
  std::string pred = k.predicted ? std::format(" pred_miss={:.1f} pred_t=+{:.2f}s", k.pred_miss, k.pred_t) : " pred=none";
  note(g_lines, std::format("{:.3f} GONE #{} {} {} life={:.2f}s frames={} {} -> {} travelled={:.1f}u avg={:.1f}u/s min_d={:.1f} dy={:+.1f} at +{:.2f}s{} (unseen {:.0f}ms)",
                            t, id, k.who, k.cls, life, k.frames, pos(k.p0), pos(k.p_last), travelled, life > 0 ? travelled / life : 0.0,
                            k.min_d, k.min_dy, k.t_min - k.t0, pred, (t - k.t_last) * 1000), 5000);
}
// Level by distance: full within 3 u, linear down to 0.15 at 30 u (the sweep radius).
float cue_gain(float d) { return d <= 3.0f ? 1.0f : std::max(0.15f, 1.0f - 0.85f * (d - 3.0f) / 27.0f); }
void play_cue(const world::Vec3& p, float d, bool accent, float level = 1.0f) {
  float pan = 0, gain = 1, ahead = 0;
  world::ear_frame(p, pan, gain, &ahead);
  audio::play_sample(audio::module_dir() + "assets\\audio\\projectile.wav", g_cue_vol * level * cue_gain(d), pan, world::rear_shelf_db(ahead), false, 0,
                     false, accent ? g_accent_st : 0.0f);
  ++g_ticks;
  if (accent) ++g_accents;
}
}  // namespace

void tick() {
  if (!g_on && !g_cue) return;
  load_api();
  if (!g_proj_ci || !g_get_id) return;
  world::Vec3 me;
  if (!world::player_position(me)) return;
  unsigned pid = world::player_id();
  double t = app::now();
  auto c0 = std::chrono::steady_clock::now();
  const void* ci = g_proj_ci();
  std::set<unsigned> seen;
  for (void* e : world::entities_near(g_radius)) {
    if (!world::object_is_a(e, ci)) continue;
    Raw r;
    if (!read_raw(e, r)) { ++g_faulted; continue; }
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
      k.t0 = t; k.p0 = p; k.next_cue = t;
      k.pace = r.speed <= 0.5f ? Pace::Still : r.speed < g_fast_speed ? Pace::Slow : Pace::Fast;
      if (g_on) {   // the names only while recording: the label lookup is the expensive part of a spawn
        k.cls = world::object_class_name(e);
        k.who = std::format("[{} #{} \"{}\"]", rel, r.owner, r.owner == pid ? std::string("you") : world::label_of(r.owner));
        note(g_lines, std::format("{:.3f} SPAWN #{} {} {} {} at {} d={:.1f} dy={:+.1f} speed={:.1f} hit={:.0f} miss={:.0f}", t, r.id, k.who, k.cls,
                                k.rec, pos(p), d, p.y - me.y, r.speed, r.hit, r.miss), 5000);
      }
      it = g_live.emplace(r.id, std::move(k)).first;
    }
    Track& k = it->second;
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
    } else if (g_cue && k.foe && k.pace == Pace::Slow && t >= k.next_cue) {
      // Accent from the CURRENT line of travel (last frame's step): it passes within g_accent_miss of the player and
      // is still approaching. Before it has moved, no accent.
      bool accent = false;
      float vx = p.x - k.p_last.x, vz = p.z - k.p_last.z, len = std::hypot(vx, vz);
      if (k.frames > 0 && len > 0.01f) {
        float ux = vx / len, uz = vz / len, rx = me.x - p.x, rz = me.z - p.z;
        accent = rx * ux + rz * uz > 0 && std::fabs(rx * uz - rz * ux) < g_accent_miss;
      }
      play_cue(p, d, accent);
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
  g_max_live = std::max(g_max_live, g_live.size());
  ++g_frames;
  double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - c0).count();
  g_sweep_us_total += us; g_sweep_us_max = std::max(g_sweep_us_max, us);
}

void set_cue(bool on) { g_cue = on; if (!on && !g_on) g_live.clear(); }
bool cue() { return g_cue; }
void set_cue_params(float vol, float interval, float accent_miss, float accent_st) {
  if (vol >= 0) g_cue_vol = std::clamp(vol, 0.0f, 2.0f);
  if (interval > 0) g_cue_interval = std::clamp(interval, 0.05f, 2.0f);
  if (accent_miss >= 0) g_accent_miss = accent_miss;
  if (accent_st > -100) g_accent_st = accent_st;
}
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
  std::string s = std::format("cue={} vol={:.2f} interval={:.2f}s accent<{:.1f}u +{:.0f}st fast>={:.0f}u/s at x{:.2f} ticks={} (fast {}) accents={}\n", g_cue,
                              g_cue_vol, g_cue_interval, g_accent_miss, g_accent_st, g_fast_speed, g_fast_vol, g_ticks, g_fast_ticks, g_accents);
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
void clear() {
  std::lock_guard<std::mutex> l(g_mu);
  g_lines.clear(); g_samples.clear();
  g_frames = g_spawns = g_faulted = g_spawns_foe = g_spawns_you = g_spawns_other = 0;
  g_max_live = 0; g_sweep_us_total = g_sweep_us_max = 0;
}
}  // namespace gd::projectiles
