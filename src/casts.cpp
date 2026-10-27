#include "casts.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <vector>
#include "app.h"
#include "gd_names.h"
#include "hooks.h"
#include "log.h"
#include "telegraph.h"
#include "world.h"

namespace gd::casts {
namespace {
using namespace gd::names;

enum class Kind : char { Start = 'S', Hit = 'H', End = 'E', Now = 'N', Callback = 'C' };
// The hook body keeps plain data only (SEH rule). Everything read FROM the caster / skill objects (id, class names,
// record path, animation time) is read here, inside the hook, while they are alive: tick() runs on the next
// in-world frame, which can be minutes later (a slow machine loading + the intro cutscene), and by then the game
// has freed them. Reading the class through a freed object's vtable ran garbage and corrupted the heap (a VM
// report, 2026-09-14). `skill` is kept as an opaque key for matching Start/Hit/End and is never dereferenced.
struct Raw {
  Kind kind; const char* which; double t = 0;
  unsigned caster_id = 0; const void* skill = nullptr; unsigned target = 0;
  bool has_pt = false; world::Vec3 pt; int anim_ms = -1; unsigned a = 0, b = 0; bool ok = true;
  char name[80] = {};
  char cls[64] = "?", scls[64] = "?", rec[200] = "?";
};
std::deque<Raw> g_pending;              // game thread only
std::deque<std::string> g_lines, g_cb_lines; std::mutex g_mu;
std::atomic<uint64_t> g_n_start{0}, g_n_hit{0}, g_n_end{0}, g_n_now{0}, g_n_cb{0}, g_dropped{0};
std::vector<gd::hooks::Hook> g_hooks;
unsigned (*g_get_id)(const void*) = nullptr;
int (*g_anim_left)(const void*) = nullptr;
const char* (*g_obj_name)(const void*) = nullptr;
struct Open { double t; const char* which; int hits; };
std::map<std::pair<unsigned, const void*>, Open> g_open;   // (caster id, skill) -> the cast in progress
struct Aim { double t; world::Vec3 pt; };
std::map<unsigned, Aim> g_last_aim;   // caster id -> its latest released skill's target point (game thread)

bool bad_ptr(const void* p, size_t n) { return !p || IsBadReadPtr(p, n) != 0; }
bool read_id(const void* obj, unsigned& out) {
  __try { out = g_get_id && obj ? g_get_id(obj) : 0; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool read_anim_left(const void* ch, int& out) {
  __try { out = g_anim_left && ch ? g_anim_left(ch) : -1; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { out = -2; return false; }
}
bool read_point(const void* wv, world::Vec3& out) {
  __try { return wv && !bad_ptr(wv, 24) && world::world_point(wv, out); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// GAME::Name is a 32-bit FNV-1a digest of the string (case-sensitive; verified 2026-09-01 against the animation
// callback names RightHandHit / LeftHandHit / R Footstep / End). The engine's own callback vocabulary (the 34
// strings SkillManager::HandleSkillAnimationCallback compares against) plus the names the shipped .anm files
// use; anything else prints as #digest.
struct KnownName { unsigned digest; const char* name; };
constexpr KnownName kNames[] = {
  {0xcf93764au, "End"},
  {0x655d504au, "Hit"},
  {0x4b6dbb86u, "SwipeRightOff"},
  {0xedee0256u, "SwipeBoth"},
  {0xdfcdd954u, "SwipeLeft"},
  {0x7cf72981u, "SwipeRight"},
  {0x3bc21b59u, "Swipe"},
  {0xf943f7c9u, "SkillProp1Add"},
  {0xd0c4f1dbu, "SkillPropRemove"},
  {0x254032a7u, "SkillPropHide"},
  {0x4cb99802u, "SkillPropAdd"},
  {0xbe7d4d75u, "SkillSound2"},
  {0xbb7d48bcu, "SkillSound1"},
  {0xf65e4db3u, "SwipeBothOff"},
  {0x5b5b70f9u, "SwipeLeftOff"},
  {0x30e73b2au, "PS2Start"},
  {0x9df8ebccu, "PS1End"},
  {0x09ef7f95u, "PS1Start"},
  {0x3c33ae83u, "SkillProp2Remove"},
  {0xf22b1dafu, "SkillProp2Hide"},
  {0x73e8943au, "SkillProp2Add"},
  {0x0bfd22f6u, "SkillProp1Remove"},
  {0x8ead1842u, "SkillProp1Hide"},
  {0xeb5db57au, "SpecialHit01"},
  {0x2be8effeu, "FootHit"},
  {0x6b60d2d6u, "LeftHandHit"},
  {0x5575bbf5u, "RightHandHit"},
  {0x9a8f71eau, "PS3End"},
  {0x3f80ca1fu, "PS3Start"},
  {0x0e15f57fu, "PS2End"},
  {0x2eb54c35u, "SkillScript"},
  {0xe85db0c1u, "SpecialHit04"},
  {0xe95db254u, "SpecialHit03"},
  {0xea5db3e7u, "SpecialHit02"},
  {0xeca64835u, "AllowInterrupt"},
  {0x737ed03du, "voxSound"},
  {0x54fe31e5u, "R Footstep"},
  {0x4b9ef4afu, "L Footstep"},
  {0xac475420u, "genericSound1"},
  {0xaf4758d9u, "genericSound2"},
  {0x7a54c578u, "HideRightHand"},
  {0x829b09abu, "ShowRightHand"},
  {0x5feb2879u, "HideLeftHand"},
  {0x61029bc8u, "ShowLeftHand"},
  {0x2a3dae87u, "TurnEnd"},
  {0x76835c42u, "TurnStart"},
  {0xe8e97de6u, "specialAttackSound1"},
  {0xe7e97c53u, "specialAttackSound2"},
  {0xe6e97ac0u, "specialAttackSound3"},
  {0xede985c5u, "specialAttackSound4"},
  {0x668bccb4u, "DisplaceGrass"},
  {0xa947e23cu, "show"},
  {0x43260eebu, "StartJump"},
  {0x4aaf986du, "StopJump"},
  {0xd1a3606au, "dissolve"},
  {0xf279c70bu, "spawnDeathActor"},
  {0xe6abf8beu, "Vox"},
  {0x31989363u, "voiceSound1"},
  {0x329894f6u, "voiceSound2"},
  {0x116aa949u, "PSEnd"},
  {0x856f3aa5u, "Pickup"},
  {0xd5ecee5cu, "skillSound1"},
  {0x32f4eb24u, "immortal"},
  {0x1bbaeebeu, "mortal"}
};
bool read_name(const void* name, char* out, size_t n) {
  __try {
    if (bad_ptr(name, 4)) return false;
    unsigned d; memcpy(&d, name, sizeof d);
    for (const KnownName& k : kNames) if (k.digest == d) { snprintf(out, n, "%s", k.name); return true; }
    snprintf(out, n, "#%08x", d);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool read_record(const void* obj, char* out, size_t n) {
  __try {
    const char* s = g_obj_name && obj ? g_obj_name(obj) : nullptr;
    if (s && !IsBadStringPtrA(s, 256)) { strncpy_s(out, n, s, _TRUNCATE); return true; }
    return false;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool read_class(const void* obj, char* out, size_t n) {
  __try {
    const void* ci = obj ? world::object_rtti(obj) : nullptr;
    if (!ci || bad_ptr(ci, 16)) return false;
    const char* s; memcpy(&s, (const char*)ci + 8, sizeof s);
    if (s && !IsBadStringPtrA(s, 64)) { strncpy_s(out, n, s, _TRUNCATE); return true; }
    return false;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void push(Raw& r) {
  r.t = app::now();
  if (g_pending.size() < 512) g_pending.push_back(r); else ++g_dropped;
}
// The object reads, all of them, while the hook has the live objects.
void read_objects(Raw& r, const void* ch, const void* skill) {
  r.skill = skill;
  read_id(ch, r.caster_id);
  read_class(ch, r.cls, sizeof r.cls);
  if (skill) { read_class(skill, r.scls, sizeof r.scls); read_record(skill, r.rec, sizeof r.rec); }
}
void on_start(const char* which, void* skill, void* ch, unsigned tid, const void* wv, bool ok) {
  ++g_n_start;
  Raw r{Kind::Start, which}; r.target = tid; r.ok = ok;
  read_objects(r, ch, skill); read_anim_left(ch, r.anim_ms); r.has_pt = read_point(wv, r.pt);
  push(r);
}
void on_hit(const char* which, void* skill, void* ch, const void* name, unsigned tid, const void* wv) {
  ++g_n_hit;
  Raw r{Kind::Hit, which}; r.target = tid;
  read_objects(r, ch, skill); read_anim_left(ch, r.anim_ms); r.has_pt = read_point(wv, r.pt); read_name(name, r.name, sizeof r.name);
  push(r);
}
void on_end(const char* which, void* skill, void* ch, bool ok) {
  ++g_n_end;
  Raw r{Kind::End, which}; r.ok = ok;
  read_objects(r, ch, skill); read_anim_left(ch, r.anim_ms);
  push(r);
}
void on_now(const char* which, void* skill, void* ch, const void* name, unsigned tid, const void* wv) {
  ++g_n_now;
  Raw r{Kind::Now, which}; r.target = tid;
  read_objects(r, ch, skill); r.has_pt = read_point(wv, r.pt); read_name(name, r.name, sizeof r.name);
  push(r);
}
void on_callback(void* ch, const void* name, unsigned a, unsigned b, const void* wv, bool ok) {
  ++g_n_cb;
  Raw r{Kind::Callback, a == 0xffffffff ? "char" : "skill"}; r.a = a; r.b = b; r.ok = ok;
  read_objects(r, ch, nullptr); read_anim_left(ch, r.anim_ms); r.has_pt = read_point(wv, r.pt); read_name(name, r.name, sizeof r.name);
  push(r);
}

// The game's behaviour first, always; then the record.
typedef bool (*Start_t)(void*, void*, unsigned, const void*, unsigned, const void*);
#define START_HOOK(FN, WHICH) static Start_t FN##_orig; static bool FN(void* s, void* c, unsigned tid, const void* wv, unsigned u, const void* lead) { bool ok = FN##_orig(s, c, tid, wv, u, lead); on_start(WHICH, s, c, tid, wv, ok); return ok; }
// Game.dll folds identical bodies: the base StartAction/HitAction/ActivateNow are shared stubs (never hook, see
// hooks.cpp), BuffSelf::StartAction == Suicide::StartAction, and BuffOther/BuffSelf/Weapon/Suicide::HitAction are one
// thunk re-dispatching vtable slot 0x6d8 -- hooked once each (resolved with pefile + capstone, 2026-09-01).
START_HOOK(start_buffother, "BuffOther") START_HOOK(start_buffself, "BuffSelf|Suicide") START_HOOK(start_spell, "Spell")
START_HOOK(start_weapon, "Weapon") START_HOOK(start_weaponpool, "WeaponPool")
typedef void (*Hit_t)(void*, void*, const void*, unsigned, const void*);
#define HIT_HOOK(FN, WHICH) static Hit_t FN##_orig; static void FN(void* s, void* c, const void* nm, unsigned tid, const void* wv) { FN##_orig(s, c, nm, tid, wv); on_hit(WHICH, s, c, nm, tid, wv); }
HIT_HOOK(hit_thunk, "Buff|Weapon|Suicide") HIT_HOOK(hit_spell, "Spell") HIT_HOOK(hit_weaponpool, "WeaponPool")
typedef bool (*End_t)(void*, void*);
#define END_HOOK(FN, WHICH) static End_t FN##_orig; static bool FN(void* s, void* c) { bool ok = FN##_orig(s, c); on_end(WHICH, s, c, ok); return ok; }
END_HOOK(end_base, "base") END_HOOK(end_weapon, "Weapon") END_HOOK(end_weaponpool, "WeaponPool")
typedef void (*Now_t)(void*, void*, const void*, unsigned, const void*);
#define NOW_HOOK(FN, WHICH) static Now_t FN##_orig; static void FN(void* s, void* c, const void* nm, unsigned tid, const void* wv) { FN##_orig(s, c, nm, tid, wv); on_now(WHICH, s, c, nm, tid, wv); }
NOW_HOOK(now_radius, "AttackRadius") NOW_HOOK(now_wave, "AttackWave") NOW_HOOK(now_areaeffect, "AttackProjectileAreaEffect")
NOW_HOOK(now_weapon, "AttackWeapon") NOW_HOOK(now_projectile, "AttackProjectile") NOW_HOOK(now_burst, "AttackProjectileBurst")
// SkillManager::HandleSkillAnimationCallback: this = the character's SkillManager (Character+0x850); the
// Character:: export of the same name is only a thunk onto it (never called by the game, 2026-09-01).
typedef bool (*AnimCb_t)(void*, const void*, unsigned, unsigned, const void*);
static AnimCb_t animcb_orig;
static bool animcb(void* sm, const void* nm, unsigned a, unsigned b, const void* wv) { bool ok = animcb_orig(sm, nm, a, b, wv); on_callback((char*)sm - 0x850, nm, a, b, wv, ok); return ok; }
// Character::AnimationCallback(Name const&): every callback point of every animation (footsteps included).
typedef bool (*CharCb_t)(void*, const void*);
static CharCb_t charcb_orig;
static bool charcb(void* c, const void* nm) { bool ok = charcb_orig(c, nm); on_callback(c, nm, 0xffffffff, 0, nullptr, ok); return ok; }

void note(std::string s, bool cb = false) {
  std::lock_guard<std::mutex> l(g_mu);
  auto& q = cb ? g_cb_lines : g_lines;
  q.push_back(std::move(s));
  while (q.size() > 3000) q.pop_front();
}
}  // namespace

bool install() {
  g_get_id = (unsigned (*)(const void*))GetProcAddress(GetModuleHandleA(Object_GetObjectId_DLL), Object_GetObjectId);
  g_obj_name = (const char* (*)(const void*))GetProcAddress(GetModuleHandleA(Object_GetObjectName_DLL), Object_GetObjectName);
  g_anim_left = (int (*)(const void*))GetProcAddress(GetModuleHandleA(Character_GetRemainingAnimationTime_DLL), Character_GetRemainingAnimationTime);
  g_hooks = {
    GD_HOOK(SkillActivatedBuffOther_StartAction, start_buffother), GD_HOOK(SkillActivatedBuffSelf_StartAction, start_buffself),
    GD_HOOK(SkillActivatedSpell_StartAction, start_spell), GD_HOOK(SkillActivatedWeapon_StartAction, start_weapon),
    GD_HOOK(SkillActivatedWeaponPool_StartAction, start_weaponpool),
    GD_HOOK(SkillActivatedBuffSelf_HitAction, hit_thunk), GD_HOOK(SkillActivatedSpell_HitAction, hit_spell),
    GD_HOOK(SkillActivatedWeaponPool_HitAction, hit_weaponpool),
    GD_HOOK(SkillActivated_EndAction, end_base), GD_HOOK(SkillActivatedWeapon_EndAction, end_weapon), GD_HOOK(SkillActivatedWeaponPool_EndAction, end_weaponpool),
    GD_HOOK(Skill_AttackRadius_ActivateNow, now_radius), GD_HOOK(Skill_AttackWave_ActivateNow, now_wave),
    GD_HOOK(Skill_AttackProjectileAreaEffect_ActivateNow, now_areaeffect), GD_HOOK(Skill_AttackWeapon_ActivateNow, now_weapon),
    GD_HOOK(Skill_AttackProjectile_ActivateNow, now_projectile), GD_HOOK(Skill_AttackProjectileBurst_ActivateNow, now_burst),
    GD_HOOK(SkillManager_HandleSkillAnimationCallback, animcb), GD_HOOK(Character_AnimationCallback, charcb),
  };
  return gd::hooks::attach_hooks(g_hooks) == 0;
}
void remove() { gd::hooks::detach_hooks(g_hooks); g_pending.clear(); g_open.clear(); }

void tick() {
  if (g_pending.empty()) return;
  world::Vec3 me; bool have_me = world::player_position(me);
  unsigned pid = world::player_id();
  while (!g_pending.empty()) {
    Raw r = g_pending.front(); g_pending.pop_front();
    const char *cls = r.cls, *scls = r.scls, *rec = r.rec;   // read in the hook; the objects may be gone by now
    std::string label = r.caster_id == pid ? "PLAYER" : world::label_of(r.caster_id);
    world::Vec3 cp; float dist = -1;
    if (have_me && world::entity_position(r.caster_id, cp)) dist = std::sqrt((cp.x - me.x) * (cp.x - me.x) + (cp.z - me.z) * (cp.z - me.z));
    std::string pt = r.has_pt ? std::format(" pt=({:.1f},{:.1f},{:.1f})", r.pt.x, r.pt.y, r.pt.z) : "";
    if (r.has_pt && have_me) pt += std::format(" [{:.1f}u from me]", std::sqrt((r.pt.x - me.x) * (r.pt.x - me.x) + (r.pt.z - me.z) * (r.pt.z - me.z)));
    std::string who = std::format("{} #{} \"{}\" {:.1f}u", cls, r.caster_id, label, dist);
    std::string sk = r.skill ? std::format(" {} {}", scls, rec) : "";
    std::string line;
    auto key = std::make_pair(r.caster_id, r.skill);
    switch (r.kind) {
      case Kind::Start:
        g_open[key] = Open{r.t, r.which, 0};
        if (r.ok) {
          telegraph::Cast c; c.caster_id = r.caster_id; c.caster_class = cls; c.skill_class = scls; c.record = rec;
          c.target_id = r.target; c.dist = dist; c.anim_ms = r.anim_ms; c.has_pos = dist >= 0; c.caster_pos = cp; c.t = r.t;
          telegraph::on_cast(c);
        }
        line = std::format("{:.3f} START[{}] {}{} -> #{}{} anim_left={}ms{}", r.t, r.which, who, sk, r.target, pt, r.anim_ms, r.ok ? "" : " REFUSED");
        break;
      case Kind::Hit: {
        auto it = g_open.find(key);
        std::string dt = it != g_open.end() ? std::format("+{:.3f}s hit#{}", r.t - it->second.t, ++it->second.hits) : "(no start)";
        line = std::format("{:.3f} HIT[{}] {} cb={} {}{} -> #{}{} anim_left={}ms", r.t, r.which, dt, r.name, who, sk, r.target, pt, r.anim_ms);
        break;
      }
      case Kind::End: {
        auto it = g_open.find(key);
        std::string dt = it != g_open.end() ? std::format("+{:.3f}s after {} hits", r.t - it->second.t, it->second.hits) : "(no start)";
        line = std::format("{:.3f} END[{}] {} {}{} anim_left={}ms{}", r.t, r.which, dt, who, sk, r.anim_ms, r.ok ? "" : " false");
        if (it != g_open.end()) g_open.erase(it);
        break;
      }
      case Kind::Now:
        if (r.has_pt) { g_last_aim[r.caster_id] = Aim{r.t, r.pt}; if (g_last_aim.size() > 512) g_last_aim.clear(); }
        line = std::format("{:.3f} NOW[{}] anim={} {}{} -> #{}{}", r.t, r.which, r.name, who, sk, r.target, pt);
        break;
      case Kind::Callback:
        line = std::format("{:.3f} CB[{}] {} a={} b={} {}{} anim_left={}ms{}", r.t, r.which, r.name, r.a, r.b, who, pt, r.anim_ms, r.ok ? "" : " ->false");
        break;
    }
    note(line, r.kind == Kind::Callback && r.which[0] == 'c');
  }
}

std::string dump(int max_lines, bool callbacks) {
  std::lock_guard<std::mutex> l(g_mu);
  std::string out = std::format("casts: start={} hit={} end={} now={} callbacks={} dropped={} lines={} cb_lines={}\n", g_n_start.load(), g_n_hit.load(), g_n_end.load(), g_n_now.load(), g_n_cb.load(), g_dropped.load(), g_lines.size(), g_cb_lines.size());
  auto& q = callbacks ? g_cb_lines : g_lines;
  size_t from = max_lines > 0 && q.size() > (size_t)max_lines ? q.size() - max_lines : 0;
  for (size_t i = from; i < q.size(); ++i) out += q[i] + "\n";
  return out;
}
bool recent_target(unsigned caster_id, double now, double max_age, world::Vec3& out) {
  auto it = g_last_aim.find(caster_id);
  if (it == g_last_aim.end() || now - it->second.t > max_age || now < it->second.t) return false;
  out = it->second.pt;
  return true;
}

void clear() { std::lock_guard<std::mutex> l(g_mu); g_lines.clear(); g_cb_lines.clear(); }
}  // namespace gd::casts
