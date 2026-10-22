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

struct Track {
  unsigned owner = 0; std::string who, cls, rec;
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
}  // namespace

void tick() {
  if (!g_on) return;
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
      k.owner = r.owner; k.cls = world::object_class_name(e); k.rec = r.rec;
      std::string rel = r.owner == pid ? "you" : world::is_foe(r.owner) ? "foe" : "other";
      (rel == "you" ? g_spawns_you : rel == "foe" ? g_spawns_foe : g_spawns_other)++;
      ++g_spawns;
      k.who = std::format("[{} #{} \"{}\"]", rel, r.owner, r.owner == pid ? std::string("you") : world::label_of(r.owner));
      k.t0 = t; k.p0 = p;
      note(g_lines, std::format("{:.3f} SPAWN #{} {} {} {} at {} d={:.1f} dy={:+.1f} speed={:.1f} hit={:.0f} miss={:.0f}", t, r.id, k.who, k.cls,
                                k.rec, pos(p), d, p.y - me.y, r.speed, r.hit, r.miss), 5000);
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
    if (d < k.min_d) { k.min_d = d; k.min_dy = p.y - me.y; k.t_min = t; k.me_min = me; }
    k.p_last = p; k.t_last = t; ++k.frames;
    if (g_with_samples) note(g_samples, std::format("{:.3f} #{} {} d={:.1f}", t, r.id, pos(p), d), 20000);
  }
  for (auto it = g_live.begin(); it != g_live.end();) {
    if (seen.count(it->first)) { ++it; continue; }
    gone(it->first, it->second, t);
    it = g_live.erase(it);
  }
  g_max_live = std::max(g_max_live, g_live.size());
  ++g_frames;
  double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - c0).count();
  g_sweep_us_total += us; g_sweep_us_max = std::max(g_sweep_us_max, us);
}

void set_recording(bool on) {
  if (g_on && !on) { double t = app::now(); for (auto& [id, k] : g_live) gone(id, k, t); g_live.clear(); }
  g_on = on;
  log::writef("projectiles: recording {}", on ? "on" : "off");
}
bool recording() { return g_on; }
void set_radius(float r) { g_radius = std::clamp(r, 5.0f, 60.0f); }
void set_samples(bool on) { g_with_samples = on; }

std::string dump(int max_lines) {
  std::string s = std::format("recording={} radius={:.0f} samples={} frames={} spawns={} (foe {} / you {} / other {}) live={} max_live={} faulted={} sweep avg={:.0f}us max={:.0f}us\n",
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
