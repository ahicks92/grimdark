#pragma once
// Projectiles in flight as data (dev recon, 2026-10-22): while recording, every in-world frame lists the
// ProjectileBase entities near the player (world::entities_near) and tracks each by object id -- who owns it,
// where it went, how close it came to the player and when it vanished. /projectiles reads it back.
// The cue (on by default): every ENEMY projectile ticks assets/audio/projectile.wav from its position while it flies,
// pitched up when its line of travel passes close to the player; the player's own and allies' stay silent.
#include <string>
namespace gd::projectiles {
void tick();                          // game thread, in-world frames; a no-op unless recording or the cue is on
void set_cue(bool on);
bool cue();
void set_cue_params(float vol, float interval, float accent_miss, float accent_st);   // negative = keep (accent_st: <= -100)
void set_recording(bool on);
bool recording();
void set_radius(float r);             // the sweep radius around the player (default 30)
void set_samples(bool on);            // also keep a per-frame position line for every live projectile
std::string dump(int max_lines);      // live tracks + the recent lines (oldest first) + counters
void clear();
}  // namespace gd::projectiles
