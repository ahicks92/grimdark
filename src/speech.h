#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "ring.h"
namespace gd::speech {
bool init();            // loads prism, picks the best available backend
void shutdown();
void speak(std::string_view utf8, bool interrupt = true);
// Several lines as one utterance (list-joined); under collect_lines they stay separate lines.
void speak_lines(const std::vector<std::string>& lines, bool interrupt = true);
// Runs `fn` with this thread's speech collected instead of spoken (nothing reaches the backend, the log or the
// history), one entry per line of text. Other threads (the voice workers) speak as usual.
std::vector<std::string> collect_lines(const std::function<void()>& fn);
void stop();
const char* backend_name();
void set_muted(bool m);  // muted: nothing reaches the backend, but history still records
bool muted();
LineRing& history();     // every line ever passed to speak(), for the dev server
}  // namespace gd::speech
