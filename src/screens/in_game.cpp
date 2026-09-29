#include "screens/in_game.h"
#include <windows.h>
#include <cmath>
#include <format>
#include "app.h"
#include "audio.h"
#include "casts.h"
#include "combat.h"
#include "cues.h"
#include "hazard.h"
#include "rooms.h"
#include "screens/hotbar_manager.h"
#include "screens/pets.h"
#include "screens/quickbar.h"
#include "sonar.h"
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "log.h"
#include "speech.h"
#include "exe_ui.h"
#include "hooks.h"
#include "textcap.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;

namespace walltones {
// The wotr wall tones, ported as-is: four AUTHORED loops (assets/audio/walltones/2: north.wav ahead and
// south.wav behind both centred, east.wav hard right, west.wav hard left), volume = (1 - d/range)^2 of the
// free distance in that direction, applied directly (no smoothing, like wotr's WallTones channel). Probed
// every frame (section below). Directions are WORLD north/east/south/west: the camera is
// pinned at yaw 0 (world::pin_camera), so screen-up is -z (the grid axis the tones call north; the game's own north is 50 deg off, docs/compass.md) and the tones no longer read the camera yaw
// (2026-09-01). One bank only: wotr's set 1 (walls) + set 2 (obstacles) split is gone -- the only classifier we
// had (walkable mesh 2-4 u beyond the stop = "obstacle") measured thickness, not what the blocker is, and most
// blockers fell on the obstacle side. Set 2 is the bank kept: the user heard it as clearly louder than set 1 even
// after the dB(A) match (brighter, 1-4 kHz energy; set 1 is near-pure 248-940 Hz tones), so it plays at its
// on-disk level (K -19.3..-20.7 LKFS, peaks -7 dBFS, no trims). Set 1 stays vendored in walltones/1.
// Range: wotr's default is 15 ft (4.6 units; a Grim Dawn unit is about a metre), tuned for dungeon corridors.
// Grim Dawn's outdoors are wide open, so 10 by the user's ear on 2026-08-21 (live-tunable: /walltones?range=).
// The probe (2026-09-22, docs/re_wall_sliding.md section 8): each direction's distance is how far HOLDING THAT KEY
// would carry you along it -- the game's own WASD request + snap + fallback simulated from the player
// (world::walk_distance) -- before the walk stops or leaves a truncated cone (half-width g_h0 at the feet, widening
// g_deg per side). A wall you would slide along, or a bridge that bends, reads open. The cone's width at the feet
// keeps a hop round a corner beside you from reading as a wall: 1.5 u is the half-width the old lane rectangle
// established at DC (83.5, 57.8) on 2026-09-01 (pressing A hopped the character 1.2 u north round a corner and on west).
// The rectangle (parallel straight navmesh raycasts, the farthest lane wins) was the probe 2026-09-01..09-22; it read
// the rail of the Four Hills rope bridge as a wall 3.3 u north of a walkable bridge and was removed.
static float g_range = 10.0f;
static float g_gain = 1.0f;  // wotr's wall-tone volume setting (its default is 60 %; start hot, tune by ear)
constexpr const char* kFile[4] = {"north.wav", "east.wav", "south.wav", "west.wav"};  // world north, east, south, west
constexpr float kDirs[4][2] = {{0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}};  // (x, z) per kFile
void set_range(float units) { if (units > 0.5f) g_range = units; }
void set_gain(float gain) { g_gain = gain < 0 ? 0 : gain > 1 ? 1 : gain; }
constexpr float kPan[4] = {0.0f, 1.0f, 0.0f, -1.0f};
constexpr int kToneId = 100;
static bool g_enabled = true;
static bool g_loaded = false;
static float g_dist[4] = {g_range, g_range, g_range, g_range};
static float g_h0 = 1.5f, g_deg = 20.0f;
void set_shape(float h0, float deg) {
  if (h0 >= 0.0f) g_h0 = h0;
  if (deg >= 0.0f && deg < 89.0f) g_deg = deg;
}
static float probe_distance(int i) { return world::walk_distance(kDirs[i][0], kDirs[i][1], g_range, g_h0, g_deg); }
static double g_last = 0;

// Per-file loudness trims in dB, order = kFile (north, east, south, west). Default flat (2026-09-01): the set 2
// files are RMS-matched at -20 dBFS and the 2026-08-22 dB(A) match (set 2 got -3.8/-2.7/+1.0/-2.7 to meet set 1
// north) made them too quiet for the user -- tune by ear, not by meter. South +3 dB by the user's ear
// (2026-09-01; it is the lowest-pitched file, 478 Hz). Headroom to clipping is ~7 dB before the trim.
// Live: /walltones?trim=off zeroes them, ?trim=default restores, ?trim=<n|e|s|w>,<dB> sets one.
constexpr float kDefaultTrimDb[4] = {0.0f, 0.0f, 3.0f, 0.0f};
static float g_trim_db[4] = {0.0f, 0.0f, 3.0f, 0.0f};
static float db_to_gain(float db) { return std::pow(10.0f, db / 20.0f); }
static void apply_trims() {
  for (int i = 0; i < 4; ++i) audio::set_loop_gain(kToneId + i, db_to_gain(g_trim_db[i]));
}
float trim_gain(int dir) { return dir >= 0 && dir < 4 ? db_to_gain(g_trim_db[dir]) : 1.0f; }
void set_trim(int dir, float db) {   // dir 0..3 = north east south west; -1 = all off, -2 = defaults
  if (dir == -1) { for (float& t : g_trim_db) t = 0.0f; }
  else if (dir == -2) { for (int i = 0; i < 4; ++i) g_trim_db[i] = kDefaultTrimDb[i]; }
  else if (dir >= 0 && dir < 4) g_trim_db[dir] = db;
  if (g_loaded) apply_trims();
}
static void ensure_loaded() {
  if (g_loaded) return;
  g_loaded = true;
  std::string dir = audio::module_dir() + "assets\\audio\\walltones\\2\\";
  for (int i = 0; i < 4; ++i) audio::load_loop(kToneId + i, dir + kFile[i], kPan[i]);
  apply_trims();
}
static void silence() { for (int i = 0; i < 4; ++i) audio::set_loop_volume(kToneId + i, 0.0f); }
void set_enabled(bool on) { g_enabled = on; if (!on) silence(); }
bool enabled() { return g_enabled; }
static void tick() {
  // Every frame, like wotr (the 100 ms throttle was audible as lag); ~0.7 ms for the four walks (section 8).
  double t = app::now();
  g_last = t;
  if (!g_enabled || !world::in_world() || !cues::enabled(cues::WallTones)) { silence(); return; }   // the player's switch (Ctrl+T)
  ensure_loaded();
  // Follow the game's own focus behaviour: it mutes when its window is not the foreground, so do we.
  HWND fg = GetForegroundWindow();
  bool audible = fg && fg == FindWindowA("Grim Dawn", nullptr);
  float user = cues::gain(cues::Walls);   // the player's channel volume on top of the dev gain
  for (int i = 0; i < 4; ++i) {
    float d = probe_distance(i);
    g_dist[i] = d;
    float v = d >= g_range ? 0.0f : 1.0f - d / g_range;
    audio::set_loop_volume(kToneId + i, audible ? v * v * g_gain * user : 0.0f);
  }
}
std::string probe_timing(int iters) {   // dev: time the probing part of one tick (no audio writes)
  if (iters < 1) iters = 1;
  if (!world::in_world()) return "not in world\n";
  LARGE_INTEGER freq, t0, t1;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&t0);
  for (int n = 0; n < iters; ++n)
    for (int i = 0; i < 4; ++i) probe_distance(i);
  QueryPerformanceCounter(&t1);
  double us = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)freq.QuadPart / iters;
  return std::format("range={:.1f} shape h0={:.2f} deg={:.1f} iters={} avg={:.1f} us/tick ({:.3f} ms)\n", g_range, g_h0, g_deg, iters, us, us / 1000.0);
}
std::string status() {
  return std::format("enabled={} shape h0={:.2f} deg={:.1f} range={:.2f} vol={:.2f} north={:.1f} east={:.1f} south={:.1f} west={:.1f}\n"
                     "trim dB (north east south west): {:+.1f} {:+.1f} {:+.1f} {:+.1f}\n",
                     g_enabled, g_h0, g_deg, g_range, g_gain, g_dist[0], g_dist[1], g_dist[2], g_dist[3],
                     g_trim_db[0], g_trim_db[1], g_trim_db[2], g_trim_db[3]);
}
}  // namespace walltones

void speak_where() {
  world::Vec3 p;
  if (!world::player_position(p)) { speech::speak(strings::kNotInWorld, true); return; }
  MessageBuilder m;
  strings::push_where(m, p.x, p.z, world::region_name(), world::life(), world::life_max());
  speech::speak(m.build(), true);
}

class InGameScreen : public Screen {
 public:
  std::string_view key() const override { return "in_game"; }
  // The player is live AND the exe's world screen holds its InGameUI (null at the menus; the engine-side
  // player/controller pointers outlive a trip back to the main menu).
  bool is_active() override { return world::in_world() && exe_ui::ingame_ui() != nullptr; }
  std::string screen_name() const override { return std::string(strings::kInGame); }
  int layer() const override { return 0; }
  // We own the keyboard in the world: the frequent game keys pass straight through (movement, quickbar,
  // evade, potions, interact, the menu key, the show-items modifier, pets), every other game function is
  // lifted to Ctrl + its default key by app.cpp's "game.*" actions (which inject the plain key), and the
  // rest of the keyboard is ours. See docs/controls.md for the game's defaults.
  bool owns_keyboard() const override { return true; }
  bool passes_key(int code) const override {
    static const int direct[] = {0x11, 0x1e, 0x1f, 0x20,                                       // W A S D
                                 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,   // 1..9 0 quickbar slots
                                 0x15,                                                         // Y quickbar switch (the game's own; we announce the page in quickbar_tick)
                                 0x39, 0x12, 0x13, 0x16, 0x01,                                 // Space evade, E energy, R health, U interact, Escape menu
                                 0x38, 0x76,                                                   // Alt / Right Alt: show items (held). F2..F7 (the game's pet selection) and Backspace (pet display) are OURS (screens/pets.cpp)
                                 0x2a, 0x36};                                                  // Shift: the game's Hold Position (exe key action sets X+0x848 on down, clears on up; 2026-09-23)
    for (int k : direct) if (k == code) return true;
    return false;
  }
  bool allows_typeahead() const override { return false; }
  bool start_unfocused() const override { return true; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::InGame, InputCategory::Lifted}; }
  void build(GraphBuilder&) override {}
  // A mouse-button key (J/Enter = left, I = right) that was ALREADY held the moment we regained focus is a
  // carry-over from the screen that just closed (e.g. Enter activated a map-marker row, which closed the map
  // and re-exposed us with Enter still physically down) -- not a fresh world click. Latch it and ignore it
  // until it is released once. Protects every close-to-world screen (map, pause menu, conversation), not just
  // the map. Fresh holds (incl. hold-to-attack) are unaffected.
  void on_focus() override {
    Screen::on_focus();
    const KeySource& ks = hooks::key_source();
    suppress_left_ = ks.held(0x24) || ks.held(0x1c);   // J or Enter
    suppress_right_ = ks.held(0x17);                    // I
  }
  void on_update() override {
    world::pin_camera();
    world::tick();
    combat::tick();
    casts::tick();
    rooms::tick();
    quickbar_tick();
    weapon_swap_tick();
    pets_tick();
    world::reping_tick();   // re-sound the review ping when the target's route (straight/path/unreachable) changes
    world::walk_tick();     // L's stall report and P's "explored"
    world::show_all_tick();
    sonar::tick();
    walltones::tick();
    hazard::tick();   // painted damage ground: lanes + bed + exit pointer (src/hazard.cpp)
    // The mouse buttons as keys, with real hold semantics: J (or Enter) = left, I = right, for as long as the
    // key is down and neither Ctrl nor Alt is held (Ctrl+J/I are lifted game keys). Shift is allowed: it reaches the
    // game as its Hold Position key, so Shift+J / Shift+I attack or cast without moving, as Shift+click does.
    constexpr int kJ = 0x24, kI = 0x17, kEnter = 0x1c;
    const KeySource& ks = hooks::key_source();
    bool mods = ks.ctrl() || ks.alt();
    bool left = !mods && (ks.held(kJ) || ks.held(kEnter));
    bool right = !mods && ks.held(kI);
    if (suppress_left_) { if (!left) suppress_left_ = false; else left = false; }     // clear on release, else swallow
    if (suppress_right_) { if (!right) suppress_right_ = false; else right = false; }
    world::mouse_key(1, left);
    world::mouse_key(2, right);
  }
  void on_unfocus() override { walltones::silence(); hazard::silence(); world::mouse_key(1, false); world::mouse_key(2, false); }
  void on_pop() override { walltones::silence(); hazard::silence(); world::mouse_key(1, false); world::mouse_key(2, false); rooms::reset(); sonar::reset(); quickbar_reset(); pets_reset(); }

 private:
  bool suppress_left_ = false;   // a left-mouse key held over from the screen that just closed
  bool suppress_right_ = false;
};

std::unique_ptr<Screen> make_in_game() { return std::make_unique<InGameScreen>(); }
}  // namespace gd::screens
