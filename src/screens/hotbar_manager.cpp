#include "screens/hotbar_manager.h"
#include <format>
#include <functional>
#include <string>
#include <vector>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/screen.h"
#include "core/strings.h"
#include "gameapi.h"
#include "screens/list_picker.h"
#include "screens/quickbar.h"
#include "screens/window_base.h"
#include "speech.h"
#include "world.h"

namespace gd::screens {
using namespace gd::core;

namespace { bool g_open = false; int g_swap_ticks = 0; bool g_swap_alt = false; }
void open_hotbar_manager() { g_open = true; }

// F: swap the active weapon set (only the two hands change). The controller applies the swap to the equipment
// view on a later tick, so the announce ("weapon set N, <right>, <left>") is deferred a few frames -- read the
// hands too early and they are still the old set's (measured 2026-08-23).
void swap_weapons() {
  g_swap_alt = gameapi::swap_weapon_set();
  g_swap_ticks = 3;
}
void weapon_swap_tick() {
  if (g_swap_ticks <= 0 || --g_swap_ticks > 0) return;
  MessageBuilder m;
  m.fragment(strings::kWeaponSet).fragment(std::format("{}", g_swap_alt ? 2 : 1));
  for (const gameapi::EquipSlot& s : gameapi::equipment())
    if (s.loc == 9 || s.loc == 10) m.list_item().fragment(s.name.empty() ? std::string(strings::kEmptySlot) : s.name);
  speech::speak(m.build(), true);
}

// A skill picker for one slot: `first_label` at id 0 (the slot's "clear" / "default"), then every assignable
// skill. Filters: the game's own quickbar-picker filter (gameapi::hotbar_assignable: learned, primary/secondary,
// not auto-toggled, on the displayed weapon set's skill set unless global), plus item_auto, the basic-attack/potion
// defaults and world::skill_aim -- passives / masteries / procs (Ice Spike) are dropped. `assign` is run with the
// picked id (0 for the first entry). Space reads the skill's text.
static void open_skill_picker(std::string label, std::string_view first_label, std::function<void(unsigned)> assign) {
  std::vector<PickerItem> items;
  items.push_back({0, std::string(first_label), {}});
  for (const gameapi::SkillInfo& s : gameapi::assignable_skills()) {
    if (!s.id || s.level == 0 || s.item_auto || !gameapi::hotbar_assignable(s.p)) continue;
    if (s.record.rfind("records/skills/default/", 0) == 0 && s.record != "records/skills/default/defaultpetattack.dbr") continue;   // Pet Attack is the one assignable default
    if (world::skill_aim(gameapi::object_by_id(s.id)) == world::SkillAim::None) continue;
    items.push_back({s.id, s.name, {}});
  }
  open_picker(std::move(label), std::move(items), std::move(assign),
              [](unsigned id, bool) { if (void* s = gameapi::object_by_id(id)) speak_lines(gameapi::skill_tooltip(s)); });
}
static void say_assigned(bool ok, bool cleared) { speech::speak(ok ? std::string(cleared ? strings::kCleared : strings::kAssigned) : std::string(strings::kCannot), true); }

class HotbarManagerScreen : public Screen {
 public:
  std::string_view key() const override { return "hotbar_manager"; }
  bool is_active() override { return g_open && world::in_world(); }
  std::string screen_name() const override { return std::string(strings::kHotbar); }
  // Just above the bare world, BELOW the game windows (inventory 11, vendor 14): opening a window covers the
  // manager, which then closes itself (below). Its own skill picker (layer 30) covers it without closing it.
  int layer() const override { return 1; }
  std::vector<InputCategory> input_categories() const override { return {InputCategory::UI}; }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [] { g_open = false; }}}; }
  void on_pop() override { g_open = false; }
  void on_unfocus() override { if (!picker_open()) g_open = false; }   // covered by a game window (not our picker) -> close

  void build(GraphBuilder& b) override {
    b.begin_stop("page");
    auto content_of = [](const gameapi::HotSlot& s) { return s.empty ? std::string(strings::kEmptySlot) : s.name; };
    // The current weapon set's two number bars (indices 0-9 and 14-23). Enter opens the skill picker; the
    // first entry "clear" empties the slot (assign_skill_to_slot with 0).
    const std::vector<gameapi::HotSlot> all = gameapi::hotslots();
    for (int bar = 1; bar <= 2; ++bar)
      for (int k = 1; k <= 10; ++k) {
        unsigned idx = gameapi::quickbar_slot_index(bar - 1, k);
        std::string content = (idx < all.size()) ? content_of(all[idx]) : std::string(strings::kEmptySlot);
        std::string label = std::format("{} {} {} {}", strings::kBar, bar, strings::kSlot, k % 10);
        b.add_item(ControlId::structural(std::format("hb.{}", idx)),
                   row_item(label, [content] { return content; }, [idx, label] {
                     open_skill_picker(label, strings::kClear, [idx](unsigned id) { say_assigned(gameapi::assign_skill_to_slot(idx, id), id == 0); });
                   }));
      }
    // The two mouse buttons (they hold skills, but don't cycle with Y). Enter opens the same picker; the first
    // entry "default" restores the game's basic attack (SkillManager::GetDefaultSkillId), so you always have a
    // way back to a working left click.
    b.add_item(ControlId::structural("hb.primary"),
               row_item(std::string(strings::kLeftMouse), [c = content_of(gameapi::primary_slot())] { return c; }, [] {
                 open_skill_picker(std::string(strings::kLeftMouse), strings::kDefault, [](unsigned id) { say_assigned(gameapi::set_primary_skill(id ? id : gameapi::default_skill_id(0)), false); });
               }));
    b.add_item(ControlId::structural("hb.secondary"),
               row_item(std::string(strings::kRightMouse), [c = content_of(gameapi::secondary_slot())] { return c; }, [] {
                 open_skill_picker(std::string(strings::kRightMouse), strings::kDefault, [](unsigned id) { say_assigned(gameapi::set_secondary_skill(id ? id : gameapi::default_skill_id(1)), false); });
               }));
    // The R / E potion slots, read-only: the game auto-manages which potion is here (no select API), so this is
    // just so they're visible and their current potion can be read.
    b.add_item(ControlId::structural("hb.health"), row_item(std::string(strings::kHealthPotion), [c = content_of(gameapi::health_potion_slot())] { return c; }));
    b.add_item(ControlId::structural("hb.energy"), row_item(std::string(strings::kEnergyPotion), [c = content_of(gameapi::mana_potion_slot())] { return c; }));
  }
};

std::unique_ptr<Screen> make_hotbar_manager() { return std::make_unique<HotbarManagerScreen>(); }

}  // namespace gd::screens
