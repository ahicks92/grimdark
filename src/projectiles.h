#pragma once
// Projectiles in flight as data (dev recon, 2026-10-22): while recording, every in-world frame lists the
// ProjectileBase entities near the player (world::entities_near) and tracks each by object id -- who owns it,
// where it went, how close it came to the player and when it vanished. /projectiles reads it back.
// The cue (on by default): every slow ENEMY projectile ticks assets/audio/projectile.wav from its position while it
// flies, pitched up when its line of travel passes close to the player; a fast one (a crossbow bolt) ticks once at
// launch; the player's own and allies' stay silent. An enemy ground effect (a ProjectileAreaEffect that has stopped
// moving: a sigil, a landed pool) is handed to the hazard sounds as a world::GroundZone instead of ticking.
#include <string>
namespace gd::projectiles {
void tick();                          // game thread, in-world frames; a no-op unless recording, the cue or the zones are on
void set_cue(bool on);
bool cue();
void set_cue_params(float vol, float interval, float accent_miss, float accent_st);   // negative = keep (accent_st: <= -100)
void set_stagger(float seconds);   // the tick offset between same-frame spawns; negative = keep
void set_fast_params(float speed, float vol);   // the fast/slow cut-off (u/s) and a fast one's level; negative = keep
void set_zones(bool on, float radius);   // enemy ground effects as hazard zones; radius <= 0 keeps it
void set_recording(bool on);
bool recording();
void set_radius(float r);             // the sweep radius around the player (default 30)
void set_samples(bool on);            // also keep a per-frame position line for every live projectile
std::string dump(int max_lines);      // live tracks + the recent lines (oldest first) + counters
void clear();
// The cast-window probe (dev recon, 2026-10-23): an enemy ground wave (Skill_AttackWave: Voldrak's Groundbreaker) makes
// no ProjectileBase, so while recording, every OTHER entity that appears near the player in the seconds after such a
// cast is logged (WNEW / WGONE: class, record, owner, path). telegraph::on_cast calls this for every foe cast; it also
// arms the ground-wave cue: the travelling effect the cast's skill names (src/wave_effects.h) ticks like a projectile.
void watch_cast(unsigned caster_id, const std::string& skill_class, const std::string& record);
void set_watch(int all, float seconds);   // all: 1 = every foe cast, 0 = waves only, -1 keep; seconds <= 0 keeps
}  // namespace gd::projectiles
