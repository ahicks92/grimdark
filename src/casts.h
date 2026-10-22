#pragma once
// Enemy skill activations as data (dev instrumentation, 2026-09-01): hooks on the exported SkillActivated
// StartAction / HitAction / EndAction / ActivateNow overrides and on Character::HandleSkillAnimationCallback
// record who cast what, when the animation's hit callbacks landed and where it was aimed. /casts reads it back.
#include <string>
#include "world.h"
namespace gd::casts {
bool install();
void remove();
void tick();                      // game thread: resolve the pending raw records into lines
std::string dump(int max_lines, bool callbacks = false);  // the recent lines (oldest first) + counters; callbacks = the per-animation callback ring
void clear();
// The world point the caster's latest released skill (an ActivateNow) was aimed at, if it was at most max_age seconds
// ago (app::now() clock). Game thread. The projectile layer uses it to find where a projectile is going.
bool recent_target(unsigned caster_id, double now, double max_age, world::Vec3& out);
}
