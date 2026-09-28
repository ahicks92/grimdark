#pragma once
// Shared shape of the in-world window screens (codex, factions, inventory, skills): the screen is showing
// while InGameUI's window object says IsVisible(); Escape closes it through the window's own Show(false);
// a tab row across the top (Left/Right, Enter; Ctrl+Tab / Ctrl+Shift+Tab from anywhere) and one vertical
// column below it. Content comes from src/gameapi.h snapshots refreshed every few frames and after every
// action (the graph is immediate-mode; the snapshot keeps the per-frame rebuild cheap).
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "core/graph_builder.h"
#include "core/message_builder.h"
#include "core/screen.h"
#include "core/strings.h"
#include "app.h"
#include "core/navigator.h"
#include "exe_ui.h"
#include "gameapi.h"
#include "hooks.h"
#include "screens/controls.h"
#include "speech.h"
#include "world.h"

namespace gd::screens {

inline const gd::core::ControlType kLineType{"text", {"label", "value"}, [] { return std::vector<gd::core::NodeAnnouncement>{}; }};
inline const gd::core::ControlType kItemType{"item", {"label", "value", "enabled", "position"}, [] { return std::vector<gd::core::NodeAnnouncement>{}; }};
inline const gd::core::ControlType kGroupType{"group", {"label", "value", "position"}, [] { return std::vector<gd::core::NodeAnnouncement>{}; }};

// A read-only line.
inline gd::core::NodeVtablePtr line_item(std::string text) {
  auto v = std::make_shared<gd::core::NodeVtable>();
  v->control_type = &kLineType;
  v->announcements = {gd::core::NodeAnnouncement([text] { return text; }, false, gd::core::announcement_kinds::kLabel)};
  return v;
}
// An actionable row: label, optional live value, Enter / Backspace / Space handlers.
inline gd::core::NodeVtablePtr row_item(std::string label, std::function<std::string()> value = {}, std::function<void()> activate = {},
                                        std::function<void()> tooltip = {}, std::function<void()> secondary = {}, std::function<void()> tooltip_detail = {}) {
  auto v = std::make_shared<gd::core::NodeVtable>();
  v->control_type = &kItemType;
  v->announcements = {gd::core::NodeAnnouncement([label] { return label; }, false, gd::core::announcement_kinds::kLabel)};
  if (value) v->announcements.push_back(gd::core::NodeAnnouncement(value, true, gd::core::announcement_kinds::kValue));
  v->on_activate = std::move(activate);
  v->on_tooltip = std::move(tooltip);
  v->on_tooltip_detail = std::move(tooltip_detail);
  v->on_secondary = std::move(secondary);
  return v;
}
// The label of an item row in any item list ("Gladius with component augment x 3"): every list of items builds its label
// here (strings::push_item), so what one list says about an item every list says. The by-id form reads the object.
inline std::string item_label(const gd::gameapi::BagItem& it) {
  gd::core::MessageBuilder m; gd::strings::push_item(m, it.name, it.id, it.stack, it.component, it.augment); return m.build();
}
inline std::string item_label(const gd::gameapi::EquipSlot& s) {   // an occupied slot's item (the caller says "empty")
  gd::core::MessageBuilder m; gd::strings::push_item(m, s.name, s.item_id, 1, s.component, s.augment); return m.build();
}
inline std::string item_label(unsigned id) {
  void* p = gd::gameapi::object_by_id(id);
  gd::core::MessageBuilder m; gd::strings::push_item(m, gd::gameapi::item_name(p), id, gd::gameapi::item_stack(p), gd::gameapi::has_component(p), gd::gameapi::has_augment(p)); return m.build();
}
// Speak a multi-line game text (a tooltip) as one interrupting utterance; "no tooltip" when empty.
inline void speak_lines(const std::vector<std::string>& lines) {
  if (lines.empty()) { speech::speak(gd::strings::kNoTooltip, true); return; }
  gd::core::MessageBuilder m;
  for (const std::string& l : lines) m.list_item().fragment(l);
  speech::speak(m.build(), true);
}
// Space / Ctrl+Space on an item: the game's tooltip, short and with its Ctrl-held details. The short form ends
// with our hint only when the detailed form really says more (starter gear: identical, no hint).
inline std::function<void()> item_tip(unsigned id, bool details) {
  return [id, details] {
    void* p = gd::gameapi::object_by_id(id);
    if (!p) { speech::speak(gd::strings::kNoTooltip, true); return; }
    // A lore note's tooltip is the note itself: the FULL text (the game's tooltip truncates it) and how to
    // file it (decided with the user 2026-08-23).
    if (gd::world::object_is_note(p)) {
      std::vector<std::string> text = gd::gameapi::note_full_text(p);
      if (!text.empty()) { text.push_back(std::string(gd::strings::kNoteUseHint)); speak_lines(text); return; }
    }
    std::vector<std::string> lines = gd::gameapi::item_tooltip(p, false, details);
    if (!details && !lines.empty() && gd::gameapi::item_tooltip(p, false, true) != lines) lines.push_back(std::string(gd::strings::kDetailsHint));
    speak_lines(lines);
  };
}

// Backslash on an item row: the game's comparison, spoken -- for every equipment slot this item fits
// (EquipmentCtrl::CanItemBePlaced, the equip picker's filter): "Right Hand, Splintered Club, <its tooltip>", or
// "nothing equipped"; "not equipment" when no slot takes it. The game shows the two tooltips side by side and
// computes no difference except weapon DPS (docs/re_item_components_compare.md).
inline std::function<void()> item_compare(unsigned id) {
  return [id] {
    if (!gd::gameapi::object_by_id(id)) { speech::speak(gd::strings::kNothingToCompare, true); return; }
    gd::core::MessageBuilder m;
    bool any = false;
    // The game's own placement test first; when it refuses every slot (requirements not met -- the case a player
    // most wants to compare), the item's class decides which slots it would take.
    std::vector<int> fallback;
    bool any_placeable = false;
    std::vector<gd::gameapi::EquipSlot> slots = gd::gameapi::equipment();
    for (const gd::gameapi::EquipSlot& s : slots) if (s.item_id != id && gd::gameapi::can_equip(id, s.loc)) any_placeable = true;
    if (!any_placeable) fallback = gd::gameapi::equip_slots_by_class(id);
    for (const gd::gameapi::EquipSlot& s : slots) {
      if (s.item_id == id) continue;
      bool fits = any_placeable ? gd::gameapi::can_equip(id, s.loc) : std::find(fallback.begin(), fallback.end(), s.loc) != fallback.end();
      if (!fits) continue;
      any = true;
      m.list_item().fragment(s.label);
      if (!s.item_id) { m.list_item().fragment(gd::strings::kNothingEquipped); continue; }
      for (const std::string& l : gd::gameapi::item_tooltip(s.item, false, false)) m.list_item().fragment(l);   // its first line is the name
    }
    speech::speak(any ? m.build() : std::string(gd::strings::kNotEquipment), true);
  };
}

// A snapshot refreshed every `ttl` frames or on demand.
template <class T> struct Snapshot {
  T value{};
  uint64_t frame = ~0ull;
  bool stale = true;
  const T& get(std::function<T()> load, uint64_t ttl = 20) {
    uint64_t f = gd::hooks::frame();
    if (stale || frame == ~0ull || f - frame > ttl) { value = load(); frame = f; stale = false; }
    return value;
  }
  void invalidate() { stale = true; }
};

class WindowScreen : public gd::core::Screen {
 public:
  WindowScreen(std::string key, std::string name, unsigned window_off, int layer) : key_(std::move(key)), name_(std::move(name)), off_(window_off), layer_(layer) {}
  std::string_view key() const override { return key_; }
  std::string screen_name() const override { return name_; }
  int layer() const override { return layer_; }
  bool is_active() override { return gd::exe_ui::available() && window().visible(); }
  // UI navigation, the window hotkeys, then the game's lifted Ctrl chords (so Ctrl+N still switches to the skills
  // window from the inventory). NOT InGame: the world's plain letter keys (review groups, F, G, Q, K...) are what
  // type-ahead types, and a window takes the keyboard from the world.
  std::vector<gd::core::InputCategory> input_categories() const override { return {gd::core::InputCategory::UI, gd::core::InputCategory::Windows, gd::core::InputCategory::Lifted}; }
  bool allows_typeahead() const override { return true; }
  std::vector<gd::core::ScreenAction> actions() override {
    return {{std::string(gd::core::action_ids::Back), [this] { close(); }}};
  }
  gd::exe_ui::WindowB window() const { return gd::exe_ui::ingame_window(off_); }
  virtual void close() { gd::exe_ui::WindowB w = window(); if (w) w.show(false); }

  // ---- tabs ----
  // The screen declares its tab labels per render; the selected index is ours (or the game's when the
  // subclass maps it). Ctrl+Tab cycles; landing on a tab (arrows along the strip) opens it. A change speaks "label, tab".
  int tab() const { return tab_; }
  bool switch_tab(int dir) override {
    int n = (int)tab_labels_.size();
    if (n == 0) return false;
    select_tab(((tab_ + dir) % n + n) % n, true);
    return true;
  }
  virtual void on_tab_changed(int /*index*/) {}
  // A window opens on its first tab (2026-09-04: the remembered tab of the last visit was announced as selected while
  // the game had reopened on its first). Screens that mirror the game's own tab (crafting, inventor) re-sync in build().
  void on_push() override { tab_ = 0; tab_key_.clear(); }
  void select_tab(int index, bool speak) {
    if (index < 0 || index >= (int)tab_labels_.size()) return;
    tab_ = index;
    tab_key_ = tab_labels_[(size_t)index];   // remember WHICH tab, not its position
    on_tab_changed(index);
    // Ctrl+Tab from inside a page: the old row is gone after the rebuild and focus used to fall to the stop landing (the
    // first tab's node), which then spoke a tab other than the one just opened (2026-09-04). Land on the new tab's node.
    if (speak) { if (gd::core::GraphNavigator* nav = gd::app::navigator()) nav->focus_node(gd::core::ControlId::structural(key_ + ".tab" + std::to_string(index)), false); }
    if (speak) {
      gd::core::MessageBuilder m;
      std::string label = tab_labels_[(size_t)index];
      if ((size_t)index < tab_values_.size() && !tab_values_[(size_t)index].empty()) { gd::core::MessageBuilder v; v.fragment(label).list_item().fragment(tab_values_[(size_t)index]); label = v.build(); }
      gd::strings::push_control(m, label, gd::strings::kTab, false, false);
      speech::speak(m.build(), true);
    }
  }
  // `values`: an optional state per tab ("secondary" on a bag), spoken after the label as a live value; it is NOT part
  // of the label the tab is re-found by, so a state change never loses the selection.
  void add_tabs(gd::core::GraphBuilder& b, std::vector<std::string> labels, std::vector<std::string> values = {}) {
    tab_labels_ = std::move(labels);
    tab_values_ = std::move(values);
    // Re-resolve the selection by its label: when the tab SET changes membership (a merchant's empty
    // category, or buying the last item in a category so its tab vanishes -- 2026-08-23) a bare index would
    // silently point at a different tab. Fall back to clamping when the remembered tab is gone.
    if (!tab_key_.empty()) {
      auto it = std::find(tab_labels_.begin(), tab_labels_.end(), tab_key_);
      if (it != tab_labels_.end()) tab_ = (int)(it - tab_labels_.begin());
    }
    if (tab_ < 0 || tab_ >= (int)tab_labels_.size()) tab_ = 0;
    if (tab_key_.empty() && !tab_labels_.empty()) tab_key_ = tab_labels_[(size_t)tab_];
    b.begin_stop("tabs");
    b.start_row("tabs");
    for (size_t i = 0; i < tab_labels_.size(); ++i) {
      std::string label = tab_labels_[i];
      bool selected = (int)i == tab_;
      auto v = std::make_shared<gd::core::NodeVtable>();
      v->control_type = &kTabType;
      // Landing on a tab OPENS it (2026-08-26: arrowing along the strip switches pages, like Ctrl+Tab), so a tab
      // has no "selected" state to speak -- the one under the cursor is always the open one. Enter is kept as a no-op
      // re-select for muscle memory.
      (void)selected;
      v->announcements = {gd::core::NodeAnnouncement([label] { return label; }, false, gd::core::announcement_kinds::kLabel)};
      if (i < tab_values_.size() && !tab_values_[i].empty()) { std::string val = tab_values_[i]; v->announcements.push_back(gd::core::NodeAnnouncement([val] { return val; }, true, gd::core::announcement_kinds::kValue)); }
      v->on_focus = [this, i] { if (tab_ != (int)i) select_tab((int)i, false); };
      v->on_activate = [this, i] { select_tab((int)i, false); };
      b.add_item(gd::core::ControlId::structural(key_ + ".tab" + std::to_string(i)), v);
    }
    b.end_row();
    b.begin_stop("page");
  }

 protected:
  std::string key_, name_;
  std::vector<std::string> tab_values_;
  unsigned off_;
  int layer_;
  int tab_ = 0;
  std::string tab_key_;      // the selected tab's label; the index is re-resolved from it each render
  std::vector<std::string> tab_labels_;
};

}  // namespace gd::screens
