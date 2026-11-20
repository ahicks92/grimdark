#pragma once
// Typing into one of the game's own edit boxes (framework A, exe_ui::WidgetA::is_edit()). The box gets the
// game's keyboard focus from a click inside its own rectangle and handles the keys itself; while the session
// is active the owning screen passes every key through except our exit keys, captures raw input (no
// navigation dispatch, no type-ahead), echoes typed characters, and reads the box's text back after a
// Backspace. Enter/Escape end the session; Tab/arrows end it and move.
#include <string>
#include "app.h"
#include "core/navigator.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "hooks.h"
#include "log.h"
#include "screens/controls.h"
#include "speech.h"

namespace gd::screens {

class EditSession {
 public:
  bool active() const { return editing_; }
  bool passes_key(int code) const {
    if (!editing_) return false;
    return code != keys::Escape && code != keys::Enter && code != keys::Tab && code != keys::Up && code != keys::Down;
  }
  void start(const exe_ui::WidgetA& box) {
    if (!box) return;
    editing_ = true;
    exe_ui::Rect r = box.abs_rect();
    hooks::click(r.x + r.w / 2, r.y + r.h / 2);  // the edit box's mouse handler (exe+0xed980) takes focus
    speech::speak(strings::kEditing, true);
  }
  void stop() {
    if (!editing_) return;
    editing_ = false;
    if (core::GraphNavigator* nav = app::navigator()) nav->announce_current();
  }
  // Per frame while active; `box` is the session's edit box resolved this frame (null ends the session).
  void tick(const exe_ui::WidgetA& box) {
    if (!editing_) return;
    if (!box) { editing_ = false; return; }
    const core::KeySource& ks = hooks::key_source();
    for (char16_t c : hooks::typed_chars()) {
      std::string ch = log::utf8(std::u16string_view(&c, 1));
      speech::speak(c == u' ' ? std::string(strings::kSpace) : ch, true);
    }
    if (ks.just_pressed(keys::Backspace)) {
      std::string now = box.text();
      speech::speak(now.empty() ? std::string(strings::kEmpty) : last_char(now), true);
    }
    core::GraphNavigator* nav = app::navigator();
    if (ks.just_pressed(keys::Enter) || ks.just_pressed(keys::Escape)) stop();
    else if (ks.just_pressed(keys::Tab)) { stop(); if (nav) nav->on_action(ks.shift() ? core::ui_actions::Prev : core::ui_actions::Next); }
    else if (ks.just_pressed(keys::Up)) { stop(); if (nav) nav->on_action(core::ui_actions::Up); }
    else if (ks.just_pressed(keys::Down)) { stop(); if (nav) nav->on_action(core::ui_actions::Down); }
  }

 private:
  struct keys { static constexpr int Escape = 0x01, Tab = 0x0f, Enter = 0x1c, Backspace = 0x0e, Up = 0x79, Down = 0x7e; };
  static std::string last_char(const std::string& s) {
    size_t i = s.size() - 1;
    while (i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80) --i;
    std::string c = s.substr(i);
    return c == " " ? std::string(strings::kSpace) : c;
  }
  bool editing_ = false;
};

// The edit box as a graph item: label, value (or "empty"), Enter toggles the session.
inline core::NodeVtablePtr edit_item(std::string label, const exe_ui::WidgetA& box, EditSession& session) {
  auto v = std::make_shared<core::NodeVtable>();
  v->control_type = &kEditType;
  std::string value = box.text();
  v->announcements = {core::NodeAnnouncement([label] { return label; }, false, core::announcement_kinds::kLabel),
                      core::NodeAnnouncement([value] { return value.empty() ? std::string(strings::kEmpty) : value; }, false, core::announcement_kinds::kValue)};
  v->on_activate = [box, &session] { if (session.active()) session.stop(); else session.start(box); };
  return v;
}
}  // namespace gd::screens
