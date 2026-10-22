// The blacksmith's crafting window (InGameUI+0x3aa80; docs/crafting.md, docs/re_crafting_{exe,gamedll}.md), opened
// by the game when the player talks to a crafter. A shop whose price is the recipe: five tabs (the game's
// categories, pressed through the window's own radio registry), the rows straight from the window's list box
// (the game's grouping, order and "[N]"), each read "Name, can make N, cost iron bits, need reagents"; Enter on a
// makeable row selects it in the game's list box and presses its Combine (the only safe craft path -- the
// command itself validates nothing); Enter on "can make 0" says what is missing; Space = the result's tooltip
// (the unrolled template, so stat ranges). Escape = the window's Show(false) (which also autosaves).
#include "screens/crafting.h"
#include <format>
#include <map>
#include <optional>
#include "gameapi.h"
#include "screens/window_base.h"
#include "textcap.h"

namespace gd::screens {
using namespace gd::core;

class CraftingScreen : public WindowScreen {
 public:
  CraftingScreen() : WindowScreen("crafting", std::string(strings::kCrafting), exe_ui::ingame::kCrafting, 14) {}
  // Landing on a tab presses the game's tab button (the tree rebuilds on the game's next update).
  void on_tab_changed(int index) override {
    if (index != exe_ui::crafting_tab()) exe_ui::crafting_press_tab(index);
    invalidate();
  }
  // The finished item announces itself from the Player::GiveArtifactToCharacter hook: name + the ROLLED item's
  // tooltip (suffix and the smith's bonus included), not the template's ranges.
  void on_focus() override {
    invalidate();
    gameapi::set_craft_listener([this](void* item) {
      MessageBuilder m;
      m.fragment(std::string(strings::kCrafted));
      for (const std::string& l : gameapi::item_tooltip(item, false, false)) m.list_item().fragment(l);   // the first line is the name
      speech::speak(m.build(), true);
      gameapi::invalidate_objects();
      invalidate();
    });
    WindowScreen::on_focus();
  }
  void on_unfocus() override { gameapi::set_craft_listener(nullptr); }
  void on_pop() override { gameapi::set_craft_listener(nullptr); }
  void build(GraphBuilder& b) override {
    std::vector<std::string> labels;
    for (int i = 0; i < exe_ui::kCraftingTabs; ++i) labels.push_back(game_text(exe_ui::crafting_tab_tag(i), std::format("category {}", i + 1)));
    // The game's category is the truth for the selected tab (a real mouse click on a tab changes it too).
    int game_tab = exe_ui::crafting_tab();
    if (game_tab >= 0 && (game_tab != tab_ || tab_key_.empty())) { tab_ = game_tab; tab_key_ = labels[(size_t)game_tab]; }
    add_tabs(b, labels);
    // Regions = the game's group headers, so Ctrl+Up / Ctrl+Down jump from group to group (landing on the header);
    // the crafter's name and specialty line are a region of their own at the top.
    b.set_region("crafting.top");
    std::string npc = exe_ui::crafting_npc_name();
    if (!npc.empty()) b.add_item(ControlId::structural("crafting.npc"), line_item(npc));
    // The smith's specialty: his blurb lines, then "crafted ... get one of" + the game's own text for each bonus.
    const std::optional<gameapi::CrafterBonus>& bonus = bonus_.get([] { return gameapi::crafter_bonus(exe_ui::crafting_npc_id()); }, 600);
    if (bonus && (!bonus->blurb.empty() || !bonus->entries.empty())) {
      MessageBuilder m;
      for (const std::string& l : bonus->blurb) m.list_item().fragment(l);
      for (const std::string& e : bonus->entries) m.list_item().fragment(e);   // the blurb already ends "...one of the following properties:"
      b.add_item(ControlId::structural("crafting.bonus"), line_item(m.build()));
    }
    const std::vector<exe_ui::CraftingRow>& rows = rows_.get([] { return exe_ui::crafting_rows(); }, 30);
    bool any = false;
    for (const exe_ui::CraftingRow& r : rows) {
      if (r.header()) { std::string h = textcap::speakable(r.text); b.set_region("crafting.group." + h); b.add_item(ControlId::structural("crafting.head." + h), line_item(h)); continue; }
      any = true;
      unsigned fid = r.formula;
      std::optional<gameapi::FormulaInfo> fi = info(fid);
      std::string name = fi && !fi->result_name.empty() ? fi->result_name : strip_prefix(textcap::speakable(r.text));
      auto v = row_item(name, [this, fid] { return value_of(fid); }, [this, fid] { activate(fid); },
                        fi && fi->result_id ? item_tip(fi->result_id, false) : std::function<void()>{}, {},
                        fi && fi->result_id ? item_tip(fi->result_id, true) : std::function<void()>{});
      b.add_item(ControlId::structural(std::format("crafting.{}", fid)), v);
    }
    if (!any) b.add_item(ControlId::structural("crafting.none"), line_item(std::string(strings::kEmpty)));
  }

 private:
  static std::string game_text(const char* tag, std::string fallback) {
    std::string t = textcap::speakable(hooks::localize(tag));
    return t.empty() ? fallback : t;
  }
  // "[0] Ranger's Badge" -> "Ranger's Badge" (the game's own count prefix; we say it as "can make N").
  static std::string strip_prefix(const std::string& text) {
    size_t close = text.find("] ");
    return text.size() > 1 && text[0] == '[' && close != std::string::npos ? text.substr(close + 2) : text;
  }
  void invalidate() { rows_.invalidate(); infos_.clear(); }
  std::optional<gameapi::FormulaInfo> info(unsigned fid) {
    auto it = infos_.find(fid);
    if (it != infos_.end()) return it->second;
    std::optional<gameapi::FormulaInfo> fi = gameapi::formula_info(fid);
    infos_[fid] = fi;
    return fi;
  }
  // "can make 2, 3,000 iron bits, 2 Aether Crystal, 1 Scrap"
  std::string value_of(unsigned fid) {
    std::optional<gameapi::FormulaInfo> fi = info(fid);
    if (!fi) return std::string();
    MessageBuilder m;
    m.list_item().fragment(std::string(strings::kCanMake)).fragment(std::to_string(fi->max_craftable));
    m.list_item().fragment(std::to_string(fi->cost)).fragment(std::string(strings::kIronBits));
    for (const gameapi::Reagent& r : fi->reagents) m.list_item().fragment(std::to_string(r.need)).fragment(r.name);
    return m.build();
  }
  void activate(unsigned fid) {
    std::optional<gameapi::FormulaInfo> fi = gameapi::formula_info(fid);   // fresh: money / bags may have changed
    if (!fi) { speech::speak(strings::kNoRecipe, true); return; }
    if (fi->max_craftable < 1) {
      // What is missing: every short reagent (need - have) and the iron-bits shortfall.
      MessageBuilder m;
      m.fragment(std::string(strings::kMissing));
      bool any = false;
      for (const gameapi::Reagent& r : fi->reagents) if (r.have < r.need) { m.list_item().fragment(std::to_string(r.need - r.have)).fragment(r.name); any = true; }
      unsigned money = gameapi::money();
      if (money < fi->cost) { m.list_item().fragment(std::to_string(fi->cost - money)).fragment(std::string(strings::kIronBits)); any = true; }
      speech::speak(any ? m.build() : std::string(strings::kCannot), true);
      return;
    }
    if (!exe_ui::crafting_craft(fid)) speech::speak(strings::kCannot, true);   // the hook speaks the result
  }
  Snapshot<std::vector<exe_ui::CraftingRow>> rows_;
  Snapshot<std::optional<gameapi::CrafterBonus>> bonus_;
  std::map<unsigned, std::optional<gameapi::FormulaInfo>> infos_;
};

std::unique_ptr<Screen> make_crafting() { return std::make_unique<CraftingScreen>(); }
}  // namespace gd::screens
