#include "screens/sound_glossary.h"
#include <format>
#include <string>
#include <vector>
#include "audio.h"
#include "core/graph_builder.h"
#include "core/screen.h"
#include "core/strings.h"
#include "screens/in_game.h"
#include "screens/window_base.h"
#include "telegraph.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;
namespace {
bool g_open = false;

// One glossary entry: a label, the file under assets/audio, and how the game positions it (pan, rear shelf) --
// the wall tones sit in their ear-fixed slots, everything else is centred.
struct Entry { std::string_view label; const char* file; float pan = 0.0f; float shelf_db = 0.0f; float vol = 1.0f; bool above_master = false; float semitones = 0.0f; };
struct Section { const char* id; std::string_view title; std::vector<Entry> entries; };

std::vector<Section> sections() {
  std::string tg = std::string("telegraphs\\");
  std::string ms = "-" + std::to_string(200) + ".wav";   // the shipped default length; the 100 ms set is the same words
  static std::string tele[telegraph::kShapes];
  for (int i = 0; i < telegraph::kShapes; ++i) tele[i] = tg + telegraph::kShapeNames[i] + ms;
  return {
    // The wall loops play with per-file loudness trims in game; the glossary applies the same ones.
    {"walls", strings::kGlossaryWallTones, {
      {strings::kGlossaryWallAhead, "walltones\\2\\north.wav", 0.0f, 0.0f, walltones::trim_gain(0)},
      {strings::kGlossaryWallRight, "walltones\\2\\east.wav", 1.0f, 0.0f, walltones::trim_gain(1)},
      {strings::kGlossaryWallBehind, "walltones\\2\\south.wav", 0.0f, -10.0f, walltones::trim_gain(2)},
      {strings::kGlossaryWallLeft, "walltones\\2\\west.wav", -1.0f, 0.0f, walltones::trim_gain(3)}}},
    {"sonar", strings::kGlossarySonar, {
      {strings::kGlossaryEnemy, "interactables\\units-enemy.wav"},
      {strings::kGlossaryLoot, "interactables\\unknown.wav"},
      {strings::kGlossaryEntrance, "interactables\\transition.wav"},
      {strings::kGlossaryDestructible, "interactables\\destructible.wav"},
      {strings::kGlossaryShrine, "interactables\\shrine-ruined.wav"},
      {strings::kGlossaryInteractable, "interactables\\interactable.wav"}}},
    {"pings", strings::kGlossaryReviewPings, {
      {strings::kGlossaryPingStraight, "review_straight.wav"},
      {strings::kGlossaryPingPath, "review_path.wav"},
      {strings::kGlossaryPingUnreachable, "review_unreachable.wav"}}},
    {"hazards", strings::kGlossaryHazards, {   // src/hazard.cpp: painted damage ground (docs/hazards.md)
      {strings::kGlossaryHazardAhead, "hazards\\sizzle\\north.wav", 0.0f},
      {strings::kGlossaryHazardRight, "hazards\\sizzle\\east.wav", 1.0f},
      {strings::kGlossaryHazardBehind, "hazards\\sizzle\\south.wav", 0.0f, -10.0f},
      {strings::kGlossaryHazardLeft, "hazards\\sizzle\\west.wav", -1.0f},
      {strings::kGlossaryHazardInside, "hazards\\inside_low\\left.wav"},
      {strings::kGlossaryHazardExit, "hazards\\exit_pulse_preview.wav"}}},
    {"projectiles", strings::kGlossaryProjectiles, {   // src/projectiles.cpp: the live cue's level (0.6, above the master)
      {strings::kGlossaryProjectileTick, "projectile.wav", 0.0f, 0.0f, 0.6f, true},
      {strings::kGlossaryProjectileHigh, "projectile.wav", 0.0f, 0.0f, 0.6f, true, 5.0f}}},
    {"telegraphs", strings::kGlossaryTelegraphs, {
      {strings::kGlossaryTelegraphSwing, tele[0].c_str(), 0.0f, 0.0f, 1.0f, true},
      {strings::kGlossaryTelegraphStomp, tele[1].c_str(), 0.0f, 0.0f, 1.0f, true},
      {strings::kGlossaryTelegraphWave, tele[2].c_str(), 0.0f, 0.0f, 1.0f, true},
      {strings::kGlossaryTelegraphShot, tele[3].c_str(), 0.0f, 0.0f, 1.0f, true},
      {strings::kGlossaryTelegraphRing, tele[4].c_str(), 0.0f, 0.0f, 1.0f, true},
      {strings::kGlossaryTelegraphArea, tele[5].c_str(), 0.0f, 0.0f, 1.0f, true},
      {strings::kGlossaryTelegraphCharge, tele[6].c_str(), 0.0f, 0.0f, 1.0f, true},
      {strings::kGlossaryTelegraphAura, tele[7].c_str(), 0.0f, 0.0f, 1.0f, true},
      {strings::kGlossaryTelegraphBeam, tele[8].c_str(), 0.0f, 0.0f, 1.0f, true},
      {strings::kGlossaryTelegraphFrontal, tele[9].c_str(), 0.0f, 0.0f, 1.0f, true}}},
  };
}

constexpr int kGroup = 77;   // one group with replace: arrowing cuts the previous row's sound
void play(const Entry& e) {
  audio::play_sample(audio::module_dir() + "assets\\audio\\" + e.file, e.vol, e.pan, e.shelf_db, !e.above_master, kGroup, true, e.semitones);
}

class SoundGlossaryScreen : public Screen {
 public:
  std::string_view key() const override { return "sound_glossary"; }
  bool is_active() override { return g_open; }
  std::string screen_name() const override { return std::string(strings::kSoundGlossary); }
  int layer() const override { return 31; }   // above the list picker (30): opens over anything, main menu included
  bool exclusive() const override { return true; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { g_open = false; }}}; }
  void on_pop() override { g_open = false; audio::stop_group(kGroup); }

  void build(GraphBuilder& b) override {
    b.begin_stop("page");
    for (const Section& s : sections()) {
      b.begin_group(ControlId::structural(std::string("glossary.") + s.id), row_item(std::string(s.title)), std::nullopt, true);
      int i = 0;
      for (const Entry& e : s.entries) {
        Entry copy = e;
        auto v = row_item(std::string(e.label));
        v->hover_sound = [copy] { play(copy); };   // landing plays it; Enter does nothing
        b.add_item(ControlId::structural(std::format("glossary.{}.{}", s.id, i++)), v);
      }
      b.end_group();
    }
  }
};
}  // namespace

void open_sound_glossary() { g_open = true; }
std::unique_ptr<Screen> make_sound_glossary() { return std::make_unique<SoundGlossaryScreen>(); }
}  // namespace gd::screens
