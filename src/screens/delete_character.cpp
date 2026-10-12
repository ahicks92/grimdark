#include "screens/delete_character.h"
#include "core/graph_builder.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "screens/controls.h"
#include "screens/edit_field.h"
#include "speech.h"
#include "textcap.h"

namespace gd::screens {
using namespace gd::core;
using exe_ui::MainMenu;
using exe_ui::WidgetA;

static const ControlType kTextType{"text", {"value"}, [] { return std::vector<NodeAnnouncement>{}; }};

// Measured 2026-08-22: TEXT 'DELETE' (title), TEXT the prompt, EDIT, A1 Accept (disabled until the box reads
// DELETE), A1 Cancel.
struct Form {
  WidgetA window, box;
  std::vector<WidgetA> texts, buttons;
  explicit operator bool() const { return window && box && buttons.size() >= 2; }
};
static Form form() {
  Form f;
  MainMenu mm = exe_ui::main_menu();
  void* win = mm ? mm.sub_window(MainMenu::kWinDeleteCharacter) : nullptr;
  if (!win || mm.current_sub_window() != win) return f;
  f.window = exe_ui::window_node(win);
  if (!f.window.active()) return Form{};
  std::vector<WidgetA> edits = f.window.edits();
  if (!edits.empty()) f.box = edits[0];
  f.texts = f.window.texts();
  f.buttons = f.window.buttons();
  return f;
}

class DeleteCharacterScreen : public Screen {
 public:
  std::string_view key() const override { return "delete_character"; }
  bool is_active() override { return exe_ui::available() && (bool)form() && !exe_ui::popup(); }
  std::string screen_name() const override { return "Delete Character"; }
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
  void on_update() override { Form f = form(); edit_.tick(f.box); }

  void build(GraphBuilder& b) override {
    Form f = form();
    if (!f) return;
    b.begin_stop("form");
    {
      // The game's prompt ("Are you sure ... Type "DELETE" to confirm:"), the last text widget.
      std::string prompt = f.texts.empty() ? std::string() : textcap::speakable(f.texts.back().text());
      auto v = std::make_shared<NodeVtable>();
      v->control_type = &kTextType;
      v->announcements = {NodeAnnouncement([prompt] { return prompt; }, false, announcement_kinds::kValue)};
      b.add_item(ControlId::structural("delete.prompt"), v);
    }
    b.add_item(ControlId::structural("delete.confirm"), edit_item(std::string(strings::kConfirmation), f.box, edit_));
    b.begin_stop("buttons");
    b.start_row("buttons");
    b.add_item(ControlId::structural("delete.Accept"), widget_button(f.buttons[0]));
    b.add_item(ControlId::structural("delete.Cancel"), widget_button(f.buttons[1]));
    b.end_row();
  }

 private:
  EditSession edit_;
};

std::unique_ptr<Screen> make_delete_character() { return std::make_unique<DeleteCharacterScreen>(); }
}  // namespace gd::screens
