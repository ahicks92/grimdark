// Pets: the player's summoned minions through Game.dll exports (docs/re_pets_gamedll.md, static RE 2026-08-26).
// The local pet list (gGameEngine's mem::vector<u32>, the HUD portrait order = the F2-F6 index order), the pet pen
// (pet id -> the summoning skill's id), stance (per SUMMONING SKILL: Player's map keyed by skill id, applied to the
// live pet with Monster::UseController), per-pet commands (Character::RequestAttack / RequestMove -- what the exe's
// portrait click and Skill_PetAttack bottom out in) and disband (ControllerPlayer::ReleasePet).
#include "gameapi.h"
#include "gameapi_internal.h"
#include "world.h"
#include "core/strings.h"
#include <format>

namespace gd::gameapi {
using namespace gd::names;
using namespace gd::gameapi::detail;
namespace {
struct Api {
  const MemVec* (*GetLocalPetList)(const void*) = nullptr;
  void* (*GetPetPen)(void*) = nullptr;
  unsigned (*GetPetOwner)(const void*, unsigned) = nullptr;           // pet id -> summoning skill id (0 = not penned)
  int (*GetPetControllerType)(const void*, unsigned) = nullptr;        // Player, skill id -> 0 Normal / 1 Aggressive / 2 Defensive (missing = 1)
  void (*SetPetControllerType)(void*, unsigned, int) = nullptr;
  void (*UseController)(void*, int) = nullptr;                         // Monster::UseController(type) on the pet
  void (*RequestAttack)(void*, unsigned, unsigned) = nullptr;          // Character::RequestAttack(requester, target)
  void (*RequestMove)(void*, unsigned, const void*) = nullptr;         // Character::RequestMove(requester, WorldVec3 const&)
  bool (*ReleasePet)(void*, unsigned, bool) = nullptr;                 // ControllerPlayer::ReleasePet(pet, false) = Disband
  double (*GetCurrentLife)(const void*) = nullptr;
  float (*GetLifeLimit)(const void*) = nullptr;
  unsigned (*GetObjectId)(const void*) = nullptr;
  bool loaded = false;
} g;
void load_pets() {
  if (g.loaded) return;
  g.loaded = true;
  GAPI_LOAD(g, GetLocalPetList, GameEngine_GetLocalPetList);
  GAPI_LOAD(g, GetPetPen, Character_GetPetPen);
  GAPI_LOAD(g, GetPetOwner, PetPen_GetPetOwner);
  GAPI_LOAD(g, GetPetControllerType, Player_GetPetControllerType);
  GAPI_LOAD(g, SetPetControllerType, Player_SetPetControllerType);
  GAPI_LOAD(g, UseController, Monster_UseController);
  GAPI_LOAD(g, RequestAttack, Character_RequestAttack);
  GAPI_LOAD(g, RequestMove, Character_RequestMove);
  GAPI_LOAD(g, ReleasePet, ControllerPlayer_ReleasePet);
  GAPI_LOAD(g, GetCurrentLife, Character_GetCurrentLife);
  GAPI_LOAD(g, GetLifeLimit, Character_GetLifeLimit);
  GAPI_LOAD(g, GetObjectId, Object_GetObjectId);
}
unsigned owner_skill(unsigned pet_id) {
  void* p = player();
  unsigned skill = 0;
  if (p && g.GetPetPen && g.GetPetOwner) guarded("PetPen::GetPetOwner", [&] { void* pen = g.GetPetPen(p); if (pen) skill = g.GetPetOwner(pen, pet_id); });
  return skill;
}
}  // namespace

std::string_view pet_stance_name(int stance) {
  switch (stance) {
    case 0: return gd::strings::kStanceNormal;
    case 1: return gd::strings::kStanceAggressive;
    case 2: return gd::strings::kStanceDefensive;
    default: return gd::strings::kUnknown;
  }
}
std::vector<unsigned> pet_ids() {
  load_pets();
  void* e = engine();
  std::vector<unsigned> ids;
  if (!e || !g.GetLocalPetList) return ids;
  const MemVec* v = nullptr;
  guarded("GetLocalPetList", [&] { v = g.GetLocalPetList(e); });
  if (v) ids = vec_items<unsigned>(v, 64);
  return ids;
}
std::vector<PetInfo> pets() {
  load_pets();
  std::vector<PetInfo> out;
  void* p = player();
  for (unsigned id : pet_ids()) {
    PetInfo pi; pi.id = id;
    pi.label = world::label_of(id);
    void* obj = object_by_id(id);
    if (obj) guarded("pet life", [&] {
      if (g.GetCurrentLife) pi.life = (float)g.GetCurrentLife(obj);
      if (g.GetLifeLimit) pi.life_max = g.GetLifeLimit(obj);
    });
    pi.skill_id = owner_skill(id);
    if (pi.skill_id) pi.skill_name = skill_name_by_id(pi.skill_id);
    pi.stance = 1;
    if (p && g.GetPetControllerType) guarded("GetPetControllerType", [&] { pi.stance = g.GetPetControllerType(p, pi.skill_id); });
    world::entity_position(id, pi.pos);
    out.push_back(pi);
  }
  return out;
}
bool set_pet_stance(unsigned pet_id, int stance) {
  load_pets();
  void* p = player();
  void* pet = object_by_id(pet_id);
  unsigned skill = owner_skill(pet_id);
  if (!p || !pet || !g.SetPetControllerType || !g.UseController || stance < 0 || stance > 2) return false;
  // The exe's portrait menu sequence (exe+0x252cd1): the skill-keyed map first, then every live pet of that skill
  // switches controller. The map is what the game persists and what a resummon reads.
  bool ok = guarded("SetPetControllerType", [&] { g.SetPetControllerType(p, skill, stance); });
  for (unsigned id : pet_ids()) {
    if (owner_skill(id) != skill) continue;
    void* o = object_by_id(id);
    if (o) ok = guarded("Monster::UseController", [&] { g.UseController(o, stance); }) && ok;
  }
  log::writef("gameapi: pet {} (skill {}) stance -> {} ok={}", pet_id, skill, pet_stance_name(stance), ok);
  return ok;
}
bool pet_attack(unsigned pet_id, unsigned target_id) {
  load_pets();
  void* p = player();
  void* pet = object_by_id(pet_id);
  if (!p || !pet || !g.RequestAttack || !g.GetObjectId || !target_id) return false;
  unsigned me = 0;
  if (!guarded("GetObjectId", [&] { me = g.GetObjectId(p); })) return false;
  return guarded("Character::RequestAttack", [&] { g.RequestAttack(pet, me, target_id); });
}
bool pet_move(unsigned pet_id, const world::Vec3& world_pos) {
  load_pets();
  void* p = player();
  void* pet = object_by_id(pet_id);
  if (!p || !pet || !g.RequestMove || !g.GetObjectId) return false;
  alignas(16) unsigned char wv[64] = {};
  if (!world::world_vec3_at(world_pos, wv)) return false;
  unsigned me = 0;
  if (!guarded("GetObjectId", [&] { me = g.GetObjectId(p); })) return false;
  return guarded("Character::RequestMove", [&] { g.RequestMove(pet, me, wv); });
}
bool release_pet(unsigned pet_id) {
  load_pets();
  void* c = controller();
  if (!c || !g.ReleasePet) return false;
  bool r = false;
  bool ok = guarded("ControllerPlayer::ReleasePet", [&] { r = g.ReleasePet(c, pet_id, false); });
  log::writef("gameapi: release pet {} ok={} returned={}", pet_id, ok, r);
  return ok && r;
}
std::string dump_pets() {
  std::string out;
  std::vector<PetInfo> ps = pets();
  out += std::format("pets: {}\n", ps.size());
  int i = 0;
  for (const PetInfo& p : ps)
    out += std::format("  {} id={} '{}' life={:.0f}/{:.0f} skill={} '{}' stance={} {} at ({:.1f}, {:.1f}, {:.1f})\n", ++i, p.id, p.label, p.life, p.life_max, p.skill_id, p.skill_name, p.stance, pet_stance_name(p.stance), p.pos.x, p.pos.y, p.pos.z);
  return out;
}
}  // namespace gd::gameapi
