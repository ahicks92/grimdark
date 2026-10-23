#include "cues.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include "settings.h"

namespace gd::cues {
namespace {
constexpr const char* kCueKey[kCues] = {"cue.walls", "cue.hazards", "cue.enemies", "cue.loot", "cue.entrances", "cue.breakables", "cue.shrines", "cue.interactables", "cue.projectiles"};
constexpr const char* kChannelKey[kChannels] = {"volume.walls", "volume.hazards", "volume.enemies", "volume.other", "volume.voice.mark", "volume.voice.zira", "volume.projectiles"};
bool g_on[kCues];
bool g_range_pitch = true, g_height_echo = false;
std::atomic<int> g_volume[kChannels];   // atomic: the voice worker reads while the game thread writes
}  // namespace

void init() {
  for (int i = 0; i < kCues; ++i) g_on[i] = settings::get_bool(kCueKey[i], true);
  for (int i = 0; i < kChannels; ++i) g_volume[i] = std::clamp(settings::get_int(kChannelKey[i], 100), 0, 100);
  g_range_pitch = settings::get_bool("cue.rangePitch", true);
  g_height_echo = settings::get_bool("cue.heightEcho", false);
}
bool range_pitch() { return g_range_pitch; }
void set_range_pitch(bool on) { g_range_pitch = on; settings::set_bool("cue.rangePitch", on); }
bool height_echo() { return g_height_echo; }
void set_height_echo(bool on) { g_height_echo = on; settings::set_bool("cue.heightEcho", on); }
float enemy_semitones(float dist) {
  if (!g_range_pitch) return 0.0f;
  return dist <= kMeleeRange ? 2.0f * kPitchStep : dist <= kNearRange ? kPitchStep : 0.0f;
}
float echo_semitones(float north) {
  if (!g_height_echo || std::fabs(north) < kEchoDeadzone) return 0.0f;
  return north > 0 ? kPitchStep : -kPitchStep;
}
bool enabled(Cue c) { return c >= 0 && c < kCues ? g_on[c] : true; }
void set_enabled(Cue c, bool on) {
  if (c < 0 || c >= kCues) return;
  g_on[c] = on;
  settings::set_bool(kCueKey[c], on);
}
int volume(Channel ch) { return ch >= 0 && ch < kChannels ? g_volume[ch].load() : 100; }
void set_volume(Channel ch, int percent) {
  if (ch < 0 || ch >= kChannels) return;
  g_volume[ch] = std::clamp(percent, 0, 100);
  settings::set_int(kChannelKey[ch], g_volume[ch]);
}
// Percent -> gain on a decibel scale: 100 = 0 dB, each 5 % step = 3 dB, 0 = silence (-60 dB would still be faintly
// audible on a loud cue). Equal steps sound equal, which a linear factor does not (50 % linear is only -6 dB).
float gain(Channel ch) {
  int v = volume(ch);
  if (v <= 0) return 0.0f;
  return std::pow(10.0f, kRangeDb * (v - 100) / 100.0f / 20.0f);
}
}  // namespace gd::cues
