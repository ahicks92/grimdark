#pragma once
// Player-facing sound cue settings (the Ctrl+T overlay, screens/cue_settings.cpp): every positioned cue can
// be switched off on its own, and four channel volumes scale what stays on -- wall tones, harmful ground, the sonar's
// enemy ping, the sonar's other pings, and the two positional voices. Persisted through settings (key=value, %LOCALAPPDATA%\Grimdark\settings.txt)
// like the T overlay. The dev knobs (/walltones?vol=, /sonar?vol=, /hazard?vol=) stay separate: these multiply them.
namespace gd::cues {
enum Cue { WallTones = 0, HarmfulGround, Enemies, Loot, Entrances, Breakables, Shrines, Interactables, Projectiles, kCues };   // HarmfulGround = all three hazard layers (enemy ground effects too)
enum Channel { Walls = 0, Hazards, EnemyChannel, Other, VoiceMark, VoiceZira, ProjectileChannel, kChannels };   // the two voices: Mark at the enemy, Zira = the player
constexpr int kVolumeStep = 5;   // percent per Left/Right
constexpr float kRangeDb = 60.0f;   // 0 % .. 100 % spans -60 dB .. 0 dB (0 % = silence)

void init();   // after settings::init
bool enabled(Cue c);
void set_enabled(Cue c, bool on);   // persists
int volume(Channel ch);             // percent 0..100 (default 100)
void set_volume(Channel ch, int percent);   // clamped, persists
float gain(Channel ch);             // percent on a -60..0 dB scale as a linear factor (any thread: the voice worker reads it)
// Two pitch options (2026-09-23), persisted like the rest:
// - range pitch (on by default): the sonar's enemy ping rises a major third within kNearRange and another within
//   kMeleeRange;
// - height echo (off by default): the route ping (; and ') plays a second copy just after it, a major third up for a
//   target to the north (the mod's compass, screen-up) and down for one to the south, nothing within kEchoDeadzone.
constexpr float kNearRange = 20.0f;      // world units
constexpr float kMeleeRange = 3.0f;      // gameengine.dbr meleeTargetDistance 2.4 + the 0.5 skill-use tolerance, rounded
constexpr float kPitchStep = 4.0f;       // semitones: a major third
constexpr float kEchoDeadzone = 2.0f;    // world units north/south
constexpr float kEchoDelayMs = 30.0f;     // after the first copy starts (90 at first: heard as a separate second ping)
constexpr float kEchoGainDb = -12.0f;    // relative to the first copy (it was equal at first: too loud)
bool range_pitch();
void set_range_pitch(bool on);
bool height_echo();
void set_height_echo(bool on);
float enemy_semitones(float dist);       // 0 / +4 / +8 with range pitch on, else 0
float echo_semitones(float north);       // +4 north, -4 south, 0 = no echo (off, or within the deadzone)
}  // namespace gd::cues
