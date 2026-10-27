#include "screens/cue_settings.h"
#include <cmath>
#include <string>
#include "audio.h"
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "cues.h"
#include "screens/in_game.h"
#include "screens/window_base.h"
#include "speech.h"
#include "voice.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;
namespace {
bool g_open = false;

struct CueRow { const char* id; cues::Cue cue; std::string_view label; };
const CueRow kCueRows[] = {
  {"walls", cues::WallTones, strings::kCueWallTones},
  {"hazards", cues::HarmfulGround, strings::kCueHarmfulGround},
  {"enemies", cues::Enemies, strings::kCueEnemies},
  {"loot", cues::Loot, strings::kCueLoot},
  {"entrances", cues::Entrances, strings::kCueEntrances},
  {"breakables", cues::Breakables, strings::kCueBreakables},
  {"shrines", cues::Shrines, strings::kCueShrines},
  {"interactables", cues::Interactables, strings::kCueInteractables},
  {"projectiles", cues::Projectiles, strings::kCueProjectiles},
};
struct VolumeRow { const char* id; cues::Channel channel; std::string_view label; };
const VolumeRow kVolumeRows[] = {
  {"walls", cues::Walls, strings::kVolumeWalls},
  {"hazards", cues::Hazards, strings::kVolumeHazards},
  {"enemies", cues::EnemyChannel, strings::kVolumeEnemies},
  {"other", cues::Other, strings::kVolumeOther},
  {"voice.mark", cues::VoiceMark, strings::kVolumeMark},
  {"voice.zira", cues::VoiceZira, strings::kVolumeZira},
  {"projectiles", cues::ProjectileChannel, strings::kVolumeProjectiles},
};

constexpr int kPreviewGroup = 78;   // one group with replace: the next step cuts the previous preview
inline float db_to_gain(float db) { return std::pow(10.0f, db / 20.0f); }
// A representative sound of the channel at its new level, so the slider is set by ear: the same file, trim and
// master rule the live cue uses (wall tone ahead, harmful ground ahead, the enemy ping, the loot ping), or a
// short line in the voice (the voice worker applies the channel volume itself).
void preview(cues::Channel ch) {
  std::string base = audio::module_dir() + "assets\\audio\\";
  float user = cues::gain(ch);
  switch (ch) {
    case cues::Walls: audio::play_sample(base + "walltones\\2\\north.wav", walltones::trim_gain(0) * user, 0.0f, 0.0f, true, kPreviewGroup, true); break;
    case cues::Hazards: audio::play_sample(base + "hazards\\sizzle\\north.wav", user, 0.0f, 0.0f, true, kPreviewGroup, true); break;
    case cues::EnemyChannel: audio::play_sample(base + "interactables\\units-enemy.wav", user, 0.0f, 0.0f, true, kPreviewGroup, true); break;
    case cues::Other: audio::play_sample(base + "interactables\\unknown.wav", db_to_gain(4.3f) * user, 0.0f, 0.0f, true, kPreviewGroup, true); break;   // the loot ping at the sonar's trim
    case cues::VoiceMark: { voice::Say s; s.voice = voice::Which::Mark; s.text = std::string(strings::kVoicePreviewMark); s.policy = voice::Policy::Replace; s.group = voice::kGroupEnemy; voice::say(std::move(s)); break; }
    case cues::VoiceZira: { voice::Say s; s.voice = voice::Which::Zira; MessageBuilder m; strings::push_health_percent(m, 70); s.text = m.build(); s.policy = voice::Policy::Replace; s.group = voice::kGroupSelf; voice::say(std::move(s)); break; }
    case cues::ProjectileChannel: audio::play_sample(base + "projectile.wav", 0.6f * user, 0.0f, 0.0f, false, kPreviewGroup, true); break;   // the live cue's level, above the master
    default: break;
  }
}
void step_volume(cues::Channel ch, int sign) {   // Left/Right: 5 % steps, clamped; the navigator speaks the new value
  cues::set_volume(ch, cues::volume(ch) + sign * cues::kVolumeStep);
  preview(ch);
}

class CueSettingsScreen : public Screen {
 public:
  std::string_view key() const override { return "cue_settings"; }
  bool is_active() override { return g_open && world::in_world(); }
  std::string screen_name() const override { return std::string(strings::kCueSettings); }
  int layer() const override { return 1; }   // like the T overlay: a game window covers and closes it
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { g_open = false; }}}; }
  void on_pop() override { g_open = false; audio::stop_group(kPreviewGroup); }
  void on_unfocus() override { g_open = false; }

  void build(GraphBuilder& b) override {
    b.begin_stop("cues");
    b.push_context(strings::kCueSwitches, strings::kList);
    for (const CueRow& r : kCueRows) {
      cues::Cue c = r.cue;
      b.add_item(ControlId::structural(std::string("cue.") + r.id),
                 row_item(std::string(r.label), [c] { return std::string(cues::enabled(c) ? strings::kOn : strings::kOff); },
                          [c] { cues::set_enabled(c, !cues::enabled(c)); }));   // Enter: flip + persist
    }
    // The two pitch options (cues.h), each with a Space tooltip saying what it does.
    b.add_item(ControlId::structural("cue.rangePitch"),
               row_item(std::string(strings::kCueRangePitch), [] { return std::string(cues::range_pitch() ? strings::kOn : strings::kOff); },
                        [] { cues::set_range_pitch(!cues::range_pitch()); },
                        [] { speech::speak(strings::kCueRangePitchTip, true); }));
    b.add_item(ControlId::structural("cue.heightEcho"),
               row_item(std::string(strings::kCueHeightEcho), [] { return std::string(cues::height_echo() ? strings::kOn : strings::kOff); },
                        [] { cues::set_height_echo(!cues::height_echo()); },
                        [] { speech::speak(strings::kCueHeightEchoTip, true); }));
    b.pop_context();

    b.begin_stop("volumes");
    b.push_context(strings::kCueVolumes, strings::kList);
    for (const VolumeRow& r : kVolumeRows) {
      cues::Channel ch = r.channel;
      auto row = row_item(std::string(r.label), [ch] { return strings::percent(cues::volume(ch)); },
                          [ch] {   // Enter: the next step up, wrapping to 0 after 100 (Left/Right are the usual way)
                            int v = cues::volume(ch) + cues::kVolumeStep;
                            cues::set_volume(ch, v > 100 ? 0 : v);
                            preview(ch);
                          });
      row->on_adjust = [ch](int sign, bool) { step_volume(ch, sign); };
      b.add_item(ControlId::structural(std::string("volume.") + r.id), row);
    }
    b.pop_context();
  }
};
}  // namespace

void open_cue_settings() { g_open = true; }
std::unique_ptr<Screen> make_cue_settings_overlay() { return std::make_unique<CueSettingsScreen>(); }
}  // namespace gd::screens
