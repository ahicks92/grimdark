#pragma once
// Attack telegraphs (2026-09-01, docs/telegraphs.md): the game draws none, so at the start of a hostile cast we play
// one short positioned cue per SHAPE of the attack -- swing / stomp / wave / shot / ring, the five reactions --
// derived from the skill object's concrete class. Fed by the StartAction hooks in src/casts.cpp.
// Player control (the T overlay, persisted): a four-state mode -- off / your target (the reviewed or combat
// target only) / highest tier (only casters of the highest monster classification present nearby, so a pack's
// boss speaks and its adds do not) / all -- and a per-shape enable.
#include <string>
#include "world.h"

namespace gd::telegraph {
struct Cast {
  unsigned caster_id = 0; std::string caster_class, skill_class, record;
  unsigned target_id = 0; float dist = -1; int anim_ms = -1;
  bool has_pos = false; world::Vec3 caster_pos;
  double t = 0;
};
enum class Mode { Off = 0, Target = 1, HighestTier = 2, All = 3 };
constexpr int kShapes = 8;
extern const char* const kShapeNames[kShapes];   // "swing", "stomp", "wave", "shot", "ring", "area", "charge", "aura"

void on_cast(const Cast& c);          // game thread
const char* shape_of(const std::string& skill_class, float caster_dist, const std::string& record = {});   // one of kShapeNames or null
std::string status();
void init();                          // load mode + shapes from settings
Mode mode();
void set_mode(Mode m);                // persists
std::string_view mode_name(Mode m);   // "off" / "your target" / "highest tier" / "all"
bool shape_enabled(int shape);
void set_shape_enabled(int shape, bool on);   // persists
void set_volume(float v);
void set_variant(int ms);             // 100 or 200: which rendering of the words to play
void test(const std::string& shape);  // play one cue centred, for listening
}
