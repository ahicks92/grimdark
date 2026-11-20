#include "screens/loading.h"
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/strings.h"
#include "exe_ui.h"
#include "speech.h"
#include "textcap.h"

namespace gd::screens {
using namespace gd::core;

// The loading screen's tip line ("^gTip: ^h..." -> "Tip: ..." once colour codes are stripped).
static bool find_tip(std::string& out) {
  for (const textcap::Item& it : textcap::snapshot()) {
    std::string t = textcap::speakable(it.text);
    if (t.rfind("Tip:", 0) == 0) { out = t; return true; }
  }
  return false;
}

class LoadingScreen : public Screen {
 public:
  std::string_view key() const override { return "loading"; }
  // App state 10 = entering the world (App::ApplyPendingState, exe+0xbe4f0); it holds until the world is up.
  bool is_active() override { return exe_ui::available() && exe_ui::app_state() == 10; }
  std::string screen_name() const override { return std::string(strings::kLoading); }
  int layer() const override { return 40; }
  bool exclusive() const override { return true; }
  bool owns_keyboard() const override { return false; }  // nothing to do here; let the game keep its keys
  std::vector<InputCategory> input_categories() const override { return {}; }
  bool start_unfocused() const override { return true; }
  void build(GraphBuilder&) override {}
  void on_focus() override {
    Screen::on_focus();  // "loading"
    std::string tip;
    if (find_tip(tip)) speech::speak(tip, false);  // game text, verbatim
  }
};

std::unique_ptr<Screen> make_loading() { return std::make_unique<LoadingScreen>(); }
}  // namespace gd::screens
