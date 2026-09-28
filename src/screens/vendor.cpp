#include "screens/vendor.h"
#include <format>
#include <optional>
#include "app.h"
#include "core/navigator.h"
#include "gameapi.h"
#include "hooks.h"
#include "screens/count_prompt.h"
#include "screens/window_base.h"

namespace gd::screens {
using namespace gd::core;

namespace {
void add_item_rows(GraphBuilder& b, const std::string& prefix, const std::vector<gameapi::BagItem>& items, std::function<std::string(const gameapi::BagItem&)> value,
                   std::function<void(unsigned)> activate) {
  if (items.empty()) { b.add_item(ControlId::structural(prefix + ".empty"), line_item(std::string(strings::kEmpty))); return; }
  for (const gameapi::BagItem& it : items) {
    unsigned id = it.id;
    std::string extra = value ? value(it) : std::string();
    auto v = row_item(item_label(it), extra.empty() ? std::function<std::string()>{} : [extra] { return extra; }, [activate, id] { activate(id); }, item_tip(id, false), {}, item_tip(id, true));
    v->on_compare = item_compare(id);   // Backslash: the equipped item in the slot this one fits
    b.add_item(ControlId::structural(std::format("{}.{}", prefix, id)), v);
  }
}
}  // namespace

class VendorScreen : public WindowScreen {
 public:
  VendorScreen() : WindowScreen("vendor", std::string(strings::kVendor), exe_ui::ingame::kMarket, 14) {}
  bool is_active() override { return exe_ui::available() && (exe_ui::ingame_window(exe_ui::ingame::kMarket).visible() || exe_ui::ingame_window(exe_ui::ingame::kFactionVendor).visible()); }
  exe_ui::WindowB active_window() const { exe_ui::WindowB w = exe_ui::ingame_window(exe_ui::ingame::kMarket); return w.visible() ? w : exe_ui::ingame_window(exe_ui::ingame::kFactionVendor); }
  void close() override { exe_ui::WindowB w = active_window(); if (w) w.show(false); }
  void on_focus() override { invalidate(); WindowScreen::on_focus(); }
  void on_tab_changed(int) override { invalidate(); }

  // Ctrl+Enter on a Sell row: "sell how many of N", then the game's split + sale of the piece.
  void sell_partial() {
    GraphNavigator* nav = app::navigator();
    std::optional<ControlId> fid = nav ? nav->focused_id() : std::nullopt;
    if (!fid || !fid->structural_key().is_string()) { speech::speak(strings::kNotAStack, true); return; }
    const std::string& k = fid->structural_key().text();
    if (k.rfind("vendor.sell.", 0) != 0) { speech::speak(strings::kNotAStack, true); return; }
    unsigned id = (unsigned)strtoul(k.c_str() + 12, nullptr, 10);
    const gameapi::BagItem* found = nullptr;
    for (const gameapi::Bag& bag : bags_.value) for (const gameapi::BagItem& it : bag.items) if (it.id == id) found = &it;
    if (!found || found->stack < 2) { speech::speak(strings::kNotAStack, true); return; }
    unsigned market = exe_ui::vendor_market_id(active_window());
    unsigned stack = found->stack;
    open_count_prompt(std::format("{} {}", strings::kSellHowMany, stack), stack, [this, market, id](unsigned n) {
      if (n >= stack_of(id)) { speech::speak(gameapi::sell(market, id) ? std::string(strings::kSold) : std::string(strings::kCannot), true); invalidate(); return; }
      unsigned clone = gameapi::split_stack(id, n);
      if (!clone) { speech::speak(strings::kCannot, true); return; }
      pending_clone_ = clone; pending_market_ = market; pending_ticks_ = 3;   // the sale runs after the character-side add
      invalidate();
    });
  }
  void on_update() override {
    if (pending_clone_ && --pending_ticks_ == 0) {
      unsigned clone = pending_clone_; pending_clone_ = 0;
      bool ok = gameapi::sell_split(pending_market_, clone);
      if (!ok) gameapi::unsplit_stack(clone);
      speech::speak(ok ? std::string(strings::kSold) : std::string(strings::kCannot), true);
      invalidate();
    }
  }
  unsigned stack_of(unsigned id) {
    for (const gameapi::Bag& bag : bags_.value) for (const gameapi::BagItem& it : bag.items) if (it.id == id) return it.stack;
    return 0;
  }

  // The faction vendor (same window class, its own master table): tabs 1-4 are the reputation tiers Friendly /
  // Respected / Honored / Revered, tab 5 is Buyback. The game says nothing about a tier's requirement except the
  // locked tab art and, per item, a red "Insufficient Faction Status" line -- so the tab itself carries it here:
  // "Respected, locked, requires Respected" with the merchant's faction and your standing on a line above.
  void build_faction(GraphBuilder& b, unsigned market) {
    static const char* kTierTitle[4] = {"tagFactionVendorTab01A", "tagFactionVendorTab02A", "tagFactionVendorTab03A", "tagFactionVendorTab04A"};
    static const char* kTierState[4] = {"tagFactionStateFriend1", "tagFactionStateFriend2", "tagFactionStateFriend4", "tagFactionStateFriend5"};   // gamefactions.dbr factionTag5..8
    std::vector<exe_ui::VendorTab> tabs = exe_ui::vendor_tabs(active_window());
    int ftype = gameapi::merchant_faction(market);
    std::string faction_name, standing; float value = 0.0f; bool have_faction = false;
    for (const gameapi::Faction& f : gameapi::factions()) if (f.type == ftype) { faction_name = f.name; standing = f.level_name; value = f.value; have_faction = true; }
    std::vector<std::string> labels, values; std::vector<std::pair<int, std::string>> types; std::vector<bool> locked;
    for (const exe_ui::VendorTab& t : tabs) {
      std::string title = t.index >= 0 && t.index < 4 ? hooks::localize(kTierTitle[t.index]) : t.index == 4 ? hooks::localize("tagVendorTab05A") : std::string();
      if (title.empty()) title = std::format("{} {}", strings::kBuy, t.index + 1);
      bool lock = have_faction && t.index >= 0 && t.index < 4 && value < gameapi::faction_level_value(kTierState[t.index]);
      std::string v;
      if (lock) { MessageBuilder m; m.fragment(std::string(strings::kLocked)).list_item().fragment(std::string(strings::kRequires)).fragment(title); v = m.build(); }
      labels.push_back(title); values.push_back(v); types.push_back({t.type, title}); locked.push_back(lock);
    }
    labels.push_back(std::string(strings::kSell)); values.push_back({});
    add_tabs(b, labels, values);
    if (have_faction) { MessageBuilder m; m.fragment(faction_name).list_item().fragment(std::string(strings::kYourStanding)).fragment(standing); b.add_item(ControlId::structural("vendor.faction"), line_item(m.build())); }
    { MessageBuilder m; strings::push_stat(m, strings::kIronBits, std::format("{}", gameapi::money())); b.add_item(ControlId::structural("vendor.money"), line_item(m.build())); }
    int t = tab();
    if (t < (int)types.size()) {
      const std::vector<gameapi::MarketTab>& stock = stock_.get([market, types] { return gameapi::market_stock_types(market, types); }, 60);
      if (t < (int)locked.size() && locked[(size_t)t]) b.add_item(ControlId::structural("vendor.locked"), line_item(values[(size_t)t]));
      if (t < (int)stock.size()) {
        const gameapi::MarketTab& mt = stock[(size_t)t];
        add_item_rows(b, std::format("vendor.buy{}", mt.type), mt.items,
                      [market](const gameapi::BagItem& it) { return gameapi::market_price_text(market, it.id, true); },
                      [this, market](unsigned id) { speech::speak(gameapi::buy(market, id) ? std::string(strings::kBought) : std::string(strings::kCannot), true); invalidate(); });
      }
    } else {
      build_sell(b, market);
    }
  }
  void build_sell(GraphBuilder& b, unsigned market) {
    const std::vector<gameapi::Bag>& bags = bags_.get([] { return gameapi::bags(); }, 30);
    std::vector<gameapi::BagItem> all;
    for (const gameapi::Bag& bag : bags) all.insert(all.end(), bag.items.begin(), bag.items.end());
    add_item_rows(b, "vendor.sell", all,
                  [market](const gameapi::BagItem& it) { return gameapi::market_price_text(market, it.id, false); },
                  [this, market](unsigned id) { speech::speak(gameapi::sell(market, id) ? std::string(strings::kSold) : std::string(strings::kCannot), true); invalidate(); });
  }
  void build(GraphBuilder& b) override {
    unsigned market = exe_ui::vendor_market_id(active_window());
    if (exe_ui::ingame_window(exe_ui::ingame::kFactionVendor).visible() && !exe_ui::ingame_window(exe_ui::ingame::kMarket).visible()) { build_faction(b, market); return; }
    const std::vector<gameapi::MarketTab>& stock = stock_.get([market] { return gameapi::market_stock(market); }, 60);
    std::vector<std::string> labels;
    // market_stock names each tab from the game's own vendor-tab tag (Melee Weapons, Armor, ...); "Buy N" is
    // only a fallback if a tag ever fails to localize.
    for (size_t i = 0; i < stock.size(); ++i) labels.push_back(stock[i].name.empty() ? std::format("{} {}", strings::kBuy, i + 1) : stock[i].name);
    labels.push_back(std::string(strings::kSell));
    add_tabs(b, labels);
    int t = tab();
    { MessageBuilder m; strings::push_stat(m, strings::kIronBits, std::format("{}", gameapi::money())); b.add_item(ControlId::structural("vendor.money"), line_item(m.build())); }
    if (t < (int)stock.size()) {
      const gameapi::MarketTab& mt = stock[(size_t)t];
      add_item_rows(b, std::format("vendor.buy{}", mt.type), mt.items,
                    [market](const gameapi::BagItem& it) { return gameapi::market_price_text(market, it.id, true); },
                    [this, market](unsigned id) { speech::speak(gameapi::buy(market, id) ? std::string(strings::kBought) : std::string(strings::kCannot), true); invalidate(); });
    } else {
      build_sell(b, market);
    }
  }
 private:
  void invalidate() { stock_.invalidate(); bags_.invalidate(); }
  Snapshot<std::vector<gameapi::MarketTab>> stock_;
  Snapshot<std::vector<gameapi::Bag>> bags_;
  unsigned pending_clone_ = 0, pending_market_ = 0; int pending_ticks_ = 0;
};

// The caravan window as three tabs (decided 2026-09-04): "your stash" and "shared stash" each hold two Tab stops --
// your items (Enter puts one into the first sack with room) and what the stash holds across all its sacks (Enter takes
// one back into your bags) -- and "manage stash" carries the two buy-a-tab rows with the game's own prices (the caravan
// window's cost arrays). Moves are the game's shift-click sequences; buying is its buy handler minus the UI, followed
// by the game's own tab-list rebuild so its window agrees. Not partial stacks (vanilla drags whole stacks here too).
class StashScreen : public WindowScreen {
 public:
  StashScreen() : WindowScreen("stash", std::string(strings::kStash), exe_ui::ingame::kCaravan, 14) {}
  void on_tab_changed(int) override { invalidate(); }
  void on_focus() override { invalidate(); WindowScreen::on_focus(); }
  void build(GraphBuilder& b) override {
    add_tabs(b, {std::string(strings::kYourStash), std::string(strings::kSharedStash), std::string(strings::kManageStash)});
    int t = tab();
    if (t == 2) { build_manage(b); return; }
    bool shared = t == 1;
    const std::vector<gameapi::Bag>& sacks = sacks_.get([] { return gameapi::stash_sacks(); }, 30);
    const std::vector<gameapi::Bag>& bags = bags_.get([] { return gameapi::bags(); }, 30);
    // ---- your items ----
    b.begin_stop("mine");
    b.push_context(strings::kYourItems, strings::kList);
    std::vector<gameapi::BagItem> mine;
    for (const gameapi::Bag& bag : bags) mine.insert(mine.end(), bag.items.begin(), bag.items.end());
    add_item_rows(b, std::format("stash.mine{}", t), mine, {},
                  [this, shared](unsigned id) { speech::speak(gameapi::bag_to_stash_any(id, shared) ? std::string(strings::kMoved) : std::string(strings::kStashFull), true); invalidate(); });
    b.pop_context();
    // ---- in the stash ----
    b.begin_stop("stash");
    b.push_context(strings::kInTheStash, strings::kList);
    std::vector<gameapi::BagItem> held; std::vector<int> sack_of;
    std::vector<std::string> sack_names;
    for (const gameapi::Bag& s : sacks) {
      if ((s.index >= 100) != shared) continue;
      std::string nm = s.name.empty() ? std::format("{} {}", shared ? strings::kSharedStash : strings::kYourStash, (s.index % 100) + 1) : s.name;
      for (const gameapi::BagItem& it : s.items) { held.push_back(it); sack_of.push_back(s.index); sack_names.push_back(nm); }
    }
    size_t k = 0;
    add_item_rows(b, std::format("stash.held{}", t), held,
                  [&](const gameapi::BagItem&) { return k < sack_names.size() ? sack_names[k++] : std::string(); },
                  [this, held, sack_of](unsigned id) {
                    int sack = -1;
                    for (size_t i = 0; i < held.size(); ++i) if (held[i].id == id) sack = sack_of[i];
                    speech::speak(sack >= 0 && gameapi::stash_to_bag(sack, id) ? std::string(strings::kMoved) : std::string(strings::kBagFull), true);
                    invalidate();
                  });
    b.pop_context();
  }
 private:
  void build_manage(GraphBuilder& b) {
    { MessageBuilder m; strings::push_stat(m, strings::kIronBits, std::format("{}", gameapi::money())); b.add_item(ControlId::structural("stash.money"), line_item(m.build())); }
    const std::vector<gameapi::Bag>& sacks = sacks_.get([] { return gameapi::stash_sacks(); }, 30);
    for (bool shared : {false, true}) {
      exe_ui::CaravanPanel panel = exe_ui::caravan_panel(shared);
      size_t owned = 0;
      for (const gameapi::Bag& s : sacks) if ((s.index >= 100) == shared) ++owned;
      size_t max_tabs = panel.costs.size();
      MessageBuilder label;
      label.fragment(std::string(shared ? strings::kSharedStash : strings::kYourStash)).list_item().fragment(std::to_string(owned)).fragment(std::string(strings::kOf)).fragment(std::to_string(max_tabs)).fragment(std::string(strings::kTabsOwned));
      std::function<std::string()> value;
      std::function<void()> act;
      if (max_tabs == 0) {
        value = [] { return std::string(strings::kCannot); };
      } else if (owned >= max_tabs) {
        value = [] { return std::string(strings::kAllTabsBought); };
      } else {
        unsigned cost = (unsigned)panel.costs[owned];
        value = [owned, cost] { MessageBuilder m; m.fragment(std::string(strings::kBuyTab)).fragment(std::to_string(owned + 1)).list_item().fragment(std::to_string(cost)).fragment(std::string(strings::kIronBits)); return m.build(); };
        act = [this, shared, cost, owned] {
          if (gameapi::money() < (int)cost) { speech::speak(strings::kTooExpensive, true); return; }
          bool ok = gameapi::buy_stash_sack(shared, cost);
          if (ok) exe_ui::caravan_refresh(shared, gameapi::stash_sack_vector(shared));
          MessageBuilder m;
          if (ok) m.fragment(std::string(strings::kBought)).list_item().fragment(std::string(shared ? strings::kSharedStash : strings::kYourStash)).fragment(std::to_string(owned + 1));
          else m.fragment(std::string(strings::kCannot));
          speech::speak(m.build(), true);
          invalidate();
        };
      }
      b.add_item(ControlId::structural(std::format("stash.buy{}", shared ? 1 : 0)), row_item(label.build(), value, act));
    }
  }
  void invalidate() { sacks_.invalidate(); bags_.invalidate(); }
  Snapshot<std::vector<gameapi::Bag>> sacks_;
  Snapshot<std::vector<gameapi::Bag>> bags_;
};

std::unique_ptr<Screen> make_vendor() { return std::make_unique<VendorScreen>(); }
void sell_partial_focused() {
  VendorScreen* v = dynamic_cast<VendorScreen*>(app::screens().current());
  if (v) v->sell_partial();   // a Windows-category chord: silent in the other windows
}
std::unique_ptr<Screen> make_stash() { return std::make_unique<StashScreen>(); }
}  // namespace gd::screens
