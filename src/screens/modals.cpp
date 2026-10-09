#include "screens/modals.h"
#include <format>
#include "gameapi.h"
#include "quest_rewards.h"
#include "screens/reward_list.h"
#include "screens/window_base.h"

namespace gd::screens {
using namespace gd::core;
using exe_ui::WidgetB;

namespace {
// A window's text element / button at an offset (by-value members; docs/ingame-ui-survey.md).
WidgetB at(const exe_ui::WindowB& w, unsigned off) { return {w ? (char*)w.p + off : nullptr}; }
void add_text(GraphBuilder& b, const std::string& id, const WidgetB& t) {
  std::string s = textcap::speakable(t.text());
  if (!s.empty()) b.add_item(ControlId::structural(id), line_item(s));
}
void add_button(GraphBuilder& b, const std::string& id, const WidgetB& btn, void* registry, std::string fallback_label = {}) {
  std::string label = textcap::speakable(btn.text());
  if (label.empty()) label = fallback_label;
  bool enabled = btn.enabled();
  auto v = std::make_shared<NodeVtable>();
  v->control_type = &kButtonType;
  v->announcements = {NodeAnnouncement([label] { return label; }, false, announcement_kinds::kLabel),
                      NodeAnnouncement([enabled] { return enabled ? std::string() : std::string(strings::kDisabled); }, true, announcement_kinds::kEnabled)};
  v->on_activate = [btn, registry, enabled] { if (!enabled) { speech::speak(strings::kDisabled, true); return; } btn.press(registry); };
  b.add_item(ControlId::structural(id), v);
}
}  // namespace

// Quest reward (InGameUI+0x8efd8, UIQuestRewardWindow): questTitleString +0x1b8 ("Quest Complete" / "Quest
// Progress"), questNameString +0x2b0, XPValue +0x7e0, the Close button +0x388 through registry +0x738. The game
// opens it from the quest-completed event while the conversation window is still up and positions it beside the
// dialog, so this screen sits ABOVE the conversation (30). Shown-ness is the base control's +0x28 byte (the
// window's handler sets it directly and its Close routine exe+0x228770 clears it); the generic IsVisible slot
// reads +0x68 and stays 0 for this window (verified live 2026-09-06, "Old Scars"). The reward rows are what the
// task handed out, captured from the game's reward pipeline (src/quest_rewards.cpp) -- the window itself only
// draws icons for them; the XP line is the window's own text.
class QuestRewardScreen : public WindowScreen {
 public:
  QuestRewardScreen() : WindowScreen("quest_reward", std::string(strings::kQuestReward), exe_ui::ingame::kQuestReward, 32) {}
  bool is_active() override { exe_ui::WindowB w = window(); return exe_ui::available() && w && WidgetB{w.p}.visible(); }
  bool exclusive() const override { return true; }
  void build(GraphBuilder& b) override {
    exe_ui::WindowB w = window();
    if (!w) return;
    b.begin_stop("page");
    std::string name = textcap::speakable(at(w, 0x2b0).text());
    std::vector<std::string> rows;
    quest_rewards::Record rec;
    if (quest_rewards::latest_for(name, rec))
      for (const quest_rewards::Reward& r : rec.rewards)
        if (r.kind != quest_rewards::Kind::Experience) rows.push_back(r.text());   // the window's own XP line is a header
    add_reward_lines(b, "reward", {textcap::speakable(at(w, 0x1b8).text()), name, textcap::speakable(at(w, 0x7e0).text())}, rows);
    add_button(b, "reward.close", at(w, 0x388), (char*)w.p + 0x738, std::string(strings::kClose));
  }
  void close() override { exe_ui::WindowB w = window(); if (w) at(w, 0x388).press((char*)w.p + 0x738); }
  std::vector<ScreenAction> actions() override { return {{std::string(action_ids::Back), [this] { close(); }}}; }
};

// Shrine: title +0x540, info +0x638, offering boxes +0x8e0 / +0xbd0 / +0xec0 (their text elements), shrine
// button +0x11f8, cancel +0x15a8, close +0x1958, registry +0x11b0, the shrine object's id +0xa4. TWO windows of
// this shape exist (found live 2026-08-28): the ruined shrine (InGameUI+0x7da50, "Offer" + offerings) and the
// desecrated one (InGameUI+0x7f6f8, its own class with "shrineCorruptedBitmap", info = tagShrineConfirmProxy
// "Summon what is trapped within?", button "Start"). The game shows the inventory window alongside both; this
// screen's layer (24) outranks it. Button labels come from the widgets' own captions.
class ShrineScreen : public WindowScreen {
 public:
  ShrineScreen(const char* key, unsigned window_off) : WindowScreen(key, std::string(strings::kShrine), window_off, 24) {}
  void build(GraphBuilder& b) override {
    exe_ui::WindowB w = window();
    if (!w) return;
    b.begin_stop("page");
    add_text(b, "shrine.title", at(w, 0x540));
    add_text(b, "shrine.info", at(w, 0x638));
    // What it asks for: the game's own offering names off the shrine object the window shows (its id at +0xa4,
    // read by the exe's fill at exe+0x1d187a). The three offering boxes (+0x8e0/+0xbd0/+0xec0) are item icons;
    // their text elements read empty (the user's report 2026-08-26).
    unsigned shrine_id = 0; exe_ui::peek_u32((char*)w.p + 0xa4, shrine_id);
    int i = 0;
    for (const std::string& name : gameapi::shrine_offerings(shrine_id)) {
      MessageBuilder m; m.fragment(strings::kOffering).fragment(std::format("{}", ++i)).list_item().fragment(name);
      b.add_item(ControlId::structural(std::format("shrine.offer{}", i)), line_item(m.build()));
    }
    if (!i) for (unsigned off : {0x8e0u, 0xbd0u, 0xec0u}) add_text(b, std::format("shrine.offer{}", i++), at(w, off));
    void* reg = (char*)w.p + 0x11b0;
    add_button(b, "shrine.use", at(w, 0x11f8), reg, std::string(strings::kOffer));
    add_button(b, "shrine.cancel", at(w, 0x15a8), reg, std::string(strings::kClose));
  }
  std::vector<ScreenAction> actions() override {
    return {{std::string(action_ids::Back), [this] { exe_ui::WindowB w = window(); if (w && !at(w, 0x1958).press((char*)w.p + 0x11b0)) w.show(false); }}};
  }
};

std::unique_ptr<Screen> make_quest_reward() { return std::make_unique<QuestRewardScreen>(); }
std::unique_ptr<Screen> make_shrine() { return std::make_unique<ShrineScreen>("shrine", exe_ui::ingame::kShrine); }
std::unique_ptr<Screen> make_corrupted_shrine() { return std::make_unique<ShrineScreen>("shrine_corrupted", exe_ui::ingame::kShrineCorrupted); }
}  // namespace gd::screens
