#include "screens/mod_options.h"
#include <string>
#include <vector>
#include "core/graph_builder.h"
#include "core/screen.h"
#include "core/strings.h"
#include "devserver.h"
#include "log.h"
#include "screens/window_base.h"
#include "settings.h"
#include "update_check.h"

namespace gd::screens {
using namespace gd::core;
namespace {
bool g_open = false;

// The row shows the LIVE state (the dev loop turns the server on through GRIMDARK_PORT whatever the setting says);
// Enter flips it now and remembers the choice for the next launch.
void toggle_devserver() {
  bool on = !dev::running();
  if (on) dev::start(dev::port()); else dev::stop(false);   // never join from the game thread
  settings::set_bool("devserver", on);
  log::writef("mod options: dev server {}", on ? "on" : "off");
}

class ModOptionsScreen : public Screen {
 public:
  std::string_view key() const override { return "mod_options"; }
  bool is_active() override { return g_open; }
  std::string screen_name() const override { return std::string(strings::kModOptions); }
  int layer() const override { return 31; }   // replaces the mod menu, like the glossary
  bool exclusive() const override { return true; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { g_open = false; }}}; }
  void on_pop() override { g_open = false; }

  void build(GraphBuilder& b) override {
    b.begin_stop("options");
    b.add_item(ControlId::structural("modopt.devserver"),
               row_item(std::string(strings::kDevServer), [] { return std::string(dev::running() ? strings::kOn : strings::kOff); },
                        [] { toggle_devserver(); }));   // the navigator speaks the row's new value itself
    b.add_item(ControlId::structural("modopt.updatecheck"),
               row_item(std::string(strings::kCheckForUpdates), [] { return std::string(update::enabled() ? strings::kOn : strings::kOff); },
                        [] { update::set_enabled(!update::enabled()); }));   // takes effect at the next load
  }
};
}  // namespace

void open_mod_options() { g_open = true; }
std::unique_ptr<Screen> make_mod_options() { return std::make_unique<ModOptionsScreen>(); }
}  // namespace gd::screens
