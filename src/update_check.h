#pragma once
// The update check (2026-10-02): once per load, on a worker thread, ask GitHub whether a newer release (or, for a CI
// build, a newer CI build) exists than the version embedded in this DLL, and say so through the screen reader. Silent
// on a dev build, when the setting is off, offline or on any error. Decisions in core/update_check.h.
#include <string_view>

namespace gd::update {
void start();      // after the mod is up; no-op for a dev build or with the setting off
// Per frame (game thread): speaks the result once the main menu (or the world) has been current for a few seconds.
void tick(std::string_view screen_key, double now);
void shutdown();   // cancels a request in flight and joins the worker (grimdark_unload, never DllMain)
bool enabled();    // settings "updatecheck", default on
void set_enabled(bool on);
}  // namespace gd::update
