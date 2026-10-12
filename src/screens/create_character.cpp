#include "screens/create_character.h"
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "screens/controls.h"
#include "screens/edit_field.h"
#include "speech.h"

namespace gd::screens {
using namespace gd::core;
using exe_ui::MainMenu;
using exe_ui::WidgetA;

// The Create Character dialog, read from its window in the exe's menu tree (measured 2026-08-22): a title
// and "Name" text, the name edit box, the Male/Female radio toggles, the Hardcore check toggle, Next and
// Cancel. Every state (the typed name, the selected sex, hardcore, Next enabled) is the widget's own.
struct Form {
  WidgetA window, name;
  std::vector<WidgetA> toggles, buttons;  // toggles: Male, Female, Hardcore; buttons: Next, Cancel
  explicit operator bool() const { return window && name && toggles.size() >= 3 && buttons.size() >= 2; }
};
static Form form() {
  Form f;
  MainMenu mm = exe_ui::main_menu();
  void* win = mm ? mm.sub_window(MainMenu::kWinCreateCharacter) : nullptr;
  if (!win || exe_ui::window_hidden_flag(win, MainMenu::kCreateCharacterHidden)) return f;
  f.window = exe_ui::window_node(win);
  std::vector<WidgetA> edits = f.window.edits();
  if (!edits.empty()) f.name = edits[0];
  for (WidgetA b : f.window.buttons()) (b.is_toggle() ? f.toggles : f.buttons).push_back(b);
  return f;
}

class CreateCharacterScreen : public Screen {
 public:
  std::string_view key() const override { return "create_character"; }
  bool is_active() override { return exe_ui::available() && (bool)form() && !exe_ui::popup(); }
  std::string screen_name() const override { return "Create Character"; }
  int layer() const override { return 20; }
  bool exclusive() const override { return true; }
  bool captures_raw_input() const override { return edit_.active(); }
  bool allows_typeahead() const override { return !edit_.active(); }
  bool passes_key(int code, bool, bool, bool) const override { return edit_.passes_key(code); }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [] { Form f = form(); if (f) f.buttons[1].activate(); }}};
  }
  void on_push() override { edit_.stop(); }
  void on_unfocus() override { edit_.stop(); }
  void on_update() override { Form f = form(); edit_.tick(f.name); }

  void build(GraphBuilder& b) override {
    Form f = form();
    if (!f) return;
    b.begin_stop("form");
    b.add_item(ControlId::structural("create.name"), edit_item("Name", f.name, edit_));
    b.push_context("Sex", strings::kList);
    for (size_t i = 0; i < 2; ++i) {
      WidgetA t = f.toggles[i];
      std::string label = t.caption();
      bool selected = t.pressed();
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kRadioType;
      v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel),
                          NodeAnnouncement([selected] { return selected ? std::string(strings::kSelected) : std::string(); }, true, announcement_kinds::kSelected)};
      v->on_activate = [t, selected] { if (!selected) t.activate(); };  // a radio only flips on; the game unselects the other
      // Feedback reads the widget live: activation changed it within this frame.
      v->state_text = [t] { MessageBuilder m; m.fragment(t.pressed() ? strings::kSelected : strings::kNotSelected); return m.build(); };
      b.add_item(ControlId::structural(std::string("create.sex.") + label), v);
    }
    b.pop_context();
    {
      WidgetA t = f.toggles[2];
      std::string label = t.caption();
      bool on = t.pressed();
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kToggleType;
      v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel),
                          NodeAnnouncement([on] { return std::string(on ? strings::kOn : strings::kOff); }, true, announcement_kinds::kValue)};
      v->on_activate = [t] { t.activate(); };
      v->state_text = [t] { return std::string(t.pressed() ? strings::kOn : strings::kOff); };
      b.add_item(ControlId::structural("create.hardcore"), v);
    }
    b.begin_stop("buttons");
    b.start_row("buttons");
    b.add_item(ControlId::structural("create.Next"), widget_button(f.buttons[0]));    // disabled by the game until a name exists
    b.add_item(ControlId::structural("create.Cancel"), widget_button(f.buttons[1]));
    b.end_row();
  }

 private:
  EditSession edit_;
};

std::unique_ptr<Screen> make_create_character() { return std::make_unique<CreateCharacterScreen>(); }
}  // namespace gd::screens
