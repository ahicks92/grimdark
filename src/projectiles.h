#pragma once
// Projectiles in flight as data (dev recon, 2026-10-22): while recording, every in-world frame lists the
// ProjectileBase entities near the player (world::entities_near) and tracks each by object id -- who owns it,
// where it went, how close it came to the player and when it vanished. /projectiles reads it back. Nothing is
// spoken or played: this is the measurement that the projectile cues will be designed from.
#include <string>
namespace gd::projectiles {
void tick();                          // game thread, in-world frames; a no-op unless recording
void set_recording(bool on);
bool recording();
void set_radius(float r);             // the sweep radius around the player (default 30)
void set_samples(bool on);            // also keep a per-frame position line for every live projectile
std::string dump(int max_lines);      // live tracks + the recent lines (oldest first) + counters
void clear();
}  // namespace gd::projectiles
