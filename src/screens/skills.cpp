#include "screens/skills.h"
#include <algorithm>
#include <cctype>
#include <format>
#include "app.h"
#include "core/navigator.h"
#include "gameapi.h"
#include "screens/list_picker.h"
#include "screens/window_base.h"

namespace gd::screens {
using namespace gd::core;

// The skill window (N). Tabs = the character's mastery slots (one per allowed mastery), then Constellations and,
// once a celestial power is learned, Celestial Powers (docs/devotion.md). A slot without a class shows the
// class-selection list (six masteries; Space reads the description, Enter puts that class's tree on the game's
// tab -- undoable until the mastery takes its first point); a slot with a class shows the skill-points line, the
// mastery bar, then the class's skills in tier order. Enter spends a point (refused with the game's reason when
// requirements -- mastery rank, a modifier's base skill -- aren't met), Space reads the game's skill text.
// Refunding is only possible AT A SPIRIT GUIDE (the game opens this window in reclaim mode,
// exe_ui::skills_reclaim_mode): then a hint row appears at the top of the list, each skill shows its iron-bit
// reclaim cost, and Backspace reclaims one point. Ctrl+1..0 / Ctrl+J / Ctrl+I assign the focused skill to the quickbar.
// Constellations: the points and affinity lines, then three Tab stops -- learned, available, unavailable -- each an
// alphabetical list of tree groups ("Bat (2/5), celestial power Twin Fangs, gives Chaos 3, Eldritch 2"; Space = the
// constellation's description and requirements; "empty" when a stop has none) holding
// its stars breadth-first from the root ("star 3, needs star 2"; Enter spends a devotion point, Space = the game's
// star tooltip). A learned celestial power's Enter opens the host picker. Celestial Powers: every learned power
// ("Twin Fangs, level 2 of 20, attached to Cadence, from Bat"), Enter = the host picker, Space = the tooltip.
class SkillsScreen : public WindowScreen, public AssignSource {
 public:
  SkillsScreen() : WindowScreen("skills", std::string(strings::kSkills), exe_ui::ingame::kSkills, 12) {}
  void on_focus() override { refresh(); WindowScreen::on_focus(); }
  void on_tab_changed(int) override { refresh(); }
  bool wrap() const override { return false; }

  void build(GraphBuilder& b) override {
    const std::vector<gameapi::SkillInfo>& list = skills_.get([] { return gameapi::skills(); }, 30);
    std::vector<unsigned> have = gameapi::mastery_ids();   // enumerations of the committed masteries
    unsigned allowed = gameapi::masteries_allowed();
    int slots = (int)allowed > (int)have.size() ? (int)allowed : (int)have.size();
    if (slots < 1) slots = 1;
    if (slots > 2) slots = 2;
    std::vector<std::string> labels;
    for (int t = 0; t < slots; ++t) {
      int e = t < (int)have.size() ? (int)have[(size_t)t] : chosen_[t];
      std::string name = e >= 0 ? mastery_name(e) : std::string(strings::kSelectClass);
      labels.push_back(name.empty() ? std::string(strings::kSelectClass) : name);
    }
    const std::vector<gameapi::DevotionConstellation>& cons = constellations_.get([] { return gameapi::constellations(); }, 30);
    labels.push_back(std::string(strings::kConstellations));
    bool any_power = false;
    for (const gameapi::DevotionConstellation& c : cons) for (const gameapi::DevotionStar& s : c.stars) if (s.power && s.learned) any_power = true;
    if (any_power) labels.push_back(std::string(strings::kCelestialPowers));
    add_tabs(b, labels);
    int t = tab();
    if (t >= slots) {
      if (t == slots) build_constellations(b, cons); else build_powers(b, cons);
      return;
    }
    bool committed = t < (int)have.size();
    int e = committed ? (int)have[(size_t)t] : chosen_[t];
    if (e < 0) { build_select(b, t); return; }
    build_tree(b, list, e, committed);
  }

  unsigned focused_skill_id() override {
    GraphNavigator* nav = app::navigator();
    std::optional<ControlId> id = nav ? nav->focused_id() : std::nullopt;
    if (!id || !id->structural_key().is_string()) return 0;
    const std::string& k = id->structural_key().text();
    if (k.rfind("skills.s", 0) != 0) return 0;
    return (unsigned)strtoul(k.c_str() + 8, nullptr, 10);
  }
  std::string focused_label() override {
    unsigned id = focused_skill_id();
    for (const gameapi::SkillInfo& s : skills_.value) if (s.id == id) return s.name;
    return {};
  }

 private:
  void refresh() { skills_.invalidate(); choices_.invalidate(); constellations_.invalidate(); }
  std::string mastery_name(int e) { for (const gameapi::MasteryChoice& c : choices_.get([] { return gameapi::mastery_choices(); }, 600)) if (c.enumeration == e) return c.name; return {}; }

  void build_select(GraphBuilder& b, int t) {
    const std::vector<gameapi::MasteryChoice>& cs = choices_.get([] { return gameapi::mastery_choices(); }, 600);
    if (cs.empty()) { b.add_item(ControlId::structural("skills.nochoice"), line_item(std::string(strings::kEmpty))); return; }
    // A mastery already taken (committed on another tab, or chosen there but not yet committed) is not offered again:
    // the game refuses the same class twice.
    std::vector<unsigned> have = gameapi::mastery_ids();
    auto taken = [&](int e) {
      for (unsigned h : have) if ((int)h == e) return true;
      for (int o = 0; o < 2; ++o) if (o != t && chosen_[o] == e) return true;
      return false;
    };
    for (const gameapi::MasteryChoice& c : cs) {
      int e = c.enumeration; std::string desc = c.description;
      if (taken(e)) continue;
      auto choose = [this, t, e] {
        if (!exe_ui::skills_set_pane(t, e)) { speech::speak(strings::kCannot, true); return; }
        chosen_[t] = e;
        refresh();
        speech::speak(strings::kClassChosen, true);
      };
      auto describe = [desc] { speech::speak(desc.empty() ? std::string(strings::kNoTooltip) : desc, true); };
      b.add_item(ControlId::structural(std::format("skills.choice{}", e)), row_item(c.name, {}, choose, describe));
    }
  }

  void build_tree(GraphBuilder& b, const std::vector<gameapi::SkillInfo>& list, int e, bool committed) {
    bool reclaim = exe_ui::skills_reclaim_mode();   // a spirit guide opened the window: refunding is allowed
    { MessageBuilder m; strings::push_stat(m, strings::kSkillPointsLeft, std::format("{}", gameapi::skill_points())); b.add_item(ControlId::structural("skills.points"), line_item(m.build())); }
    if (reclaim) {   // a non-interactive hint row between the tabs and the skill list
      MessageBuilder m; m.fragment(strings::kSpiritGuide).list_item().fragment(strings::kReclaimHint);
      m.list_item().fragment(std::format("{}", gameapi::reclaim_cost())).fragment(strings::kIronBits).fragment(strings::kEach);
      b.add_item(ControlId::structural("skills.reclaim"), line_item(m.build()));
    }
    const gameapi::SkillInfo* mastery = gameapi::mastery_skill(list, e);
    if (mastery) add_skill(b, *mastery, list, reclaim);
    int t = tab();
    if (!committed && mastery && mastery->level == 0) {
      auto undo = [this, t] { if (exe_ui::skills_set_pane(t, exe_ui::kSkillsClassSelectPane)) { chosen_[t] = -1; refresh(); } };
      b.add_item(ControlId::structural("skills.undo"), row_item(std::string(strings::kUndoClassSelection), {}, undo));
    }
    // The window's own Undo Points button: reverts every skill point spent since the window opened (the game
    // keeps that per-button; we press its button rather than replay it). Present while the button is enabled.
    if (exe_ui::skills_undo_points_enabled(t)) {
      auto undo_points = [this, t] { speech::speak(exe_ui::skills_undo_points(t) ? std::string(strings::kUndoPoints) : std::string(strings::kCannot), true); refresh(); };
      b.add_item(ControlId::structural("skills.undopoints"), row_item(std::string(strings::kUndoPoints), {}, undo_points));
    }
    unsigned mid = mastery ? mastery->id : 0;
    std::vector<const gameapi::SkillInfo*> tree;
    for (const gameapi::SkillInfo& s : list) if (!s.is_mastery && mid && s.mastery_id == mid) tree.push_back(&s);
    std::stable_sort(tree.begin(), tree.end(), [](const gameapi::SkillInfo* a, const gameapi::SkillInfo* c) { return a->mastery_req != c->mastery_req ? a->mastery_req < c->mastery_req : a->tier < c->tier; });
    for (const gameapi::SkillInfo* s : tree) add_skill(b, *s, list, reclaim);
    if (!mastery) b.add_item(ControlId::structural("skills.nomastery"), line_item(std::string(strings::kEmpty)));
  }

  void add_skill(GraphBuilder& b, const gameapi::SkillInfo& s, const std::vector<gameapi::SkillInfo>& list, bool reclaim) {
    std::string label = s.name.empty() ? s.record : s.name;
    unsigned level = s.level, max = s.max_level, req = s.mastery_req; bool locked = s.locked, mastery = s.is_mastery, modifier = s.modifier;
    std::string modifies;   // a modifier says which base skill it enhances (Skill::GetBaseSkills)
    if (modifier && s.modified_skill_id) for (const gameapi::SkillInfo& x : list) if (x.id == s.modified_skill_id) { modifies = x.name; break; }
    unsigned cost = reclaim ? gameapi::reclaim_cost() : 0;
    auto value = [level, max, req, locked, mastery, modifier, modifies, reclaim, cost] {
      MessageBuilder m;
      strings::push_skill_level(m, level, max);
      if (mastery) m.list_item().fragment(strings::kMastery);
      if (modifier) { if (!modifies.empty()) m.list_item().fragment(strings::kModifies).fragment(modifies); else m.list_item().fragment(strings::kModifier); }
      if (req) m.list_item().fragment(strings::kRequiresMastery).fragment(std::format("{}", req));
      if (locked) m.list_item().fragment(strings::kLocked);
      if (reclaim && level > 0 && !mastery) m.list_item().fragment(std::format("{}", cost)).fragment(strings::kIronBits).fragment(strings::kToReclaim);
      return m.build();
    };
    void* p = s.p; unsigned id = s.id;
    // Enter learns, gated by the game's requirements (mastery rank, modifier base, points); the reason is spoken.
    // Spending goes through the pane's OWN icon click when the pane shows the skill (exe_ui::skills_press_skill):
    // the game gates, spends and records the pending delta its Undo Points button reverts. The direct call is
    // the fallback (no pane -- e.g. a mastery slot the game has no pane for yet).
    auto learn = [this, p, id] {
      void* q = gameapi::object_by_id(id); if (!q) q = p;
      std::string why = gameapi::can_learn_skill(q);
      if (!why.empty()) { speech::speak(why, true); return; }
      unsigned before = gameapi::skill_level(q);
      bool ok = exe_ui::skills_press_skill(id) && gameapi::skill_level(q) > before;
      if (!ok) ok = gameapi::learn_skill(q);
      speech::speak(ok ? std::string(strings::kPointSpent) : std::string(strings::kCannot), true);
      refresh();
    };
    // Backspace reclaims a point -- ONLY at a spirit guide (reclaim mode); wired only then, so it does nothing otherwise.
    std::function<void()> refund;
    if (reclaim) refund = [this, p, id] {
      void* q = gameapi::object_by_id(id); if (!q) q = p;
      // The game's whole reclaim gate (modifiers / hosted power / mastery dependants / bits) is replicated there; the
      // pane's icon is greyed for the same reasons, and the direct fallback re-checks it, so nothing slips past.
      std::string why = gameapi::can_reclaim_skill(q);
      if (!why.empty()) { speech::speak(why, true); return; }
      unsigned before = gameapi::skill_level(q);   // the pane's click reclaims in reclaim mode (and records the undo delta)
      if ((exe_ui::skills_press_skill(id) && gameapi::skill_level(q) < before) || gameapi::refund_skill(q)) { speech::speak(strings::kReclaimed, true); refresh(); return; }
      speech::speak(std::string(strings::kCannot), true);   // left: a greyed icon for a reason we don't model (e.g. no expansion 1 for the mastery)
    };
    auto tooltip = [p, reclaim] { speak_lines(gameapi::skill_window_tooltip(p, reclaim)); };
    auto v = row_item(label, value, learn, tooltip, refund);
    v->state_text = [id] { for (const gameapi::SkillInfo& x : gameapi::skills()) if (x.id == id) { MessageBuilder m; strings::push_skill_level(m, x.level, x.max_level); return m.build(); } return std::string(); };
    b.add_item(ControlId::structural(std::format("skills.s{}", s.id)), v);
  }

  // ---- devotion ----
  // "2 available, 5 of 50" and the affinity line, both live.
  void add_devotion_header(GraphBuilder& b) {
    MessageBuilder m;
    m.fragment(strings::kDevotionPoints).list_item().fragment(std::format("{}", gameapi::devotion_points())).fragment(strings::kAvailable);
    m.list_item().fragment(std::format("{} of {}", gameapi::devotion_points_total(), gameapi::devotion_points_max()));
    b.add_item(ControlId::structural("skills.devpoints"), line_item(m.build()));
    MessageBuilder a; a.fragment(strings::kAffinities).list_item().fragment(gameapi::affinities_text());
    b.add_item(ControlId::structural("skills.affinities"), line_item(a.build()));
  }
  static std::string constellation_value(const gameapi::DevotionConstellation& c) {
    MessageBuilder m;
    m.list_item();
    if (c.complete) m.fragment(strings::kComplete);
    else if (c.learned) {}   // the count is in the name: "Bat (2/5)"
    else if (c.affinity_met) m.fragment(strings::kAvailable);
    else {
      MessageBuilder r;
      for (const auto& [type, amount] : c.required) r.list_item().fragment(gameapi::affinity_name(type)).fragment(std::format("{}", amount));
      m.fragment(strings::kNeeds).fragment(r.build());
    }
    for (const gameapi::DevotionStar& s : c.stars) if (s.power && !s.name.empty()) m.list_item().fragment(strings::kCelestialPower).fragment(s.name);
    if (!c.given.empty()) {
      MessageBuilder g;
      for (const auto& [type, amount] : c.given) g.list_item().fragment(gameapi::affinity_name(type)).fragment(std::format("{}", amount));
      m.list_item().fragment(strings::kGives).fragment(g.build());
    }
    return m.build();
  }
  static std::string star_label(const gameapi::DevotionStar& s) { return s.power && !s.name.empty() ? s.name : std::format("{} {}", strings::kStar, s.index); }
  static std::string star_value(const gameapi::DevotionConstellation& c, const gameapi::DevotionStar& s, bool reclaim, unsigned cost) {
    MessageBuilder m;
    m.list_item();
    if (s.learned) {
      if (s.power) {
        m.fragment(strings::kCelestialPower);
        strings::push_skill_level(m, s.dev_level, s.dev_max);
        if (!s.host_name.empty()) m.list_item().fragment(strings::kAttachedTo).fragment(s.host_name); else m.list_item().fragment(strings::kNotAttached);
      } else m.fragment(strings::kLearned);
      if (reclaim) m.list_item().fragment(std::format("{}", cost)).fragment(strings::kIronBits).fragment(strings::kToReclaim);
      return m.build();
    }
    if (s.power) m.fragment(strings::kCelestialPower).list_item();
    std::string why = gameapi::can_take_star(c, s);
    m.fragment(why.empty() || why == strings::kNoPoints ? std::string(strings::kAvailable) : why);   // "no points" is spoken on Enter, not on every row
    return m.build();
  }
  void open_host_picker(const gameapi::DevotionStar& s) {
    unsigned power = s.skill_id;
    std::vector<PickerItem> items;
    items.push_back({0, std::string(strings::kNone), {}});
    for (const gameapi::SkillInfo& k : gameapi::power_host_candidates(power)) {
      std::string has;
      for (const gameapi::DevotionConstellation& c : constellations_.value) for (const gameapi::DevotionStar& o : c.stars)
        if (o.power && o.learned && o.host_id == k.id && o.skill_id != power && !o.name.empty()) { MessageBuilder m; m.fragment(strings::kHas).fragment(o.name); has = m.build(); }
      items.push_back({k.id, k.name, has});
    }
    std::string t = std::format("{} {} {}", strings::kAssign, s.name, strings::kTo);   // "assign Twin Fangs to"
    auto pick = [this, power](unsigned host) {
      std::string replaced;
      bool ok = gameapi::bind_power(power, host, &replaced);
      MessageBuilder m;
      if (!ok) m.fragment(strings::kCannot);
      else if (!host) m.fragment(strings::kCleared);
      else { m.fragment(strings::kAssigned); if (!replaced.empty()) m.list_item().fragment(strings::kReplaced).fragment(replaced); }
      speech::speak(m.build(), true);
      refresh();
    };
    open_picker(t, std::move(items), pick, [](unsigned id, bool) { if (void* k = gameapi::object_by_id(id)) speak_lines(gameapi::skill_tooltip(k)); else speech::speak(strings::kNoTooltip, true); });
  }
  void add_star(GraphBuilder& b, const std::string& group_id, const gameapi::DevotionConstellation& c, const gameapi::DevotionStar& s, bool reclaim, unsigned cost) {
    unsigned sid = s.skill_id;
    std::string value = star_value(c, s, reclaim, cost);
    gameapi::DevotionStar star = s;
    bool power = s.power, learned = s.learned;
    std::string why = learned ? std::string() : gameapi::can_take_star(c, s);
    auto activate = [this, sid, star, power, learned, why] {
      if (learned) {
        if (power) { open_host_picker(star); return; }
        speech::speak(strings::kLearned, true);
        return;
      }
      if (!why.empty()) { speech::speak(why, true); return; }
      bool completed = false;
      if (!gameapi::take_star(sid, completed)) { speech::speak(strings::kCannot, true); refresh(); return; }
      MessageBuilder m; m.fragment(strings::kPointSpent);
      if (completed) m.list_item().fragment(strings::kConstellationComplete);
      speech::speak(m.build(), true);
      refresh();
      if (power) {   // the window opens its host picker right after a power is taken; so do we
        for (const gameapi::DevotionConstellation& c2 : gameapi::constellations()) for (const gameapi::DevotionStar& s2 : c2.stars) if (s2.skill_id == sid && s2.learned) { open_host_picker(s2); return; }
      }
    };
    auto tooltip = [sid] { speak_lines(gameapi::star_tooltip(sid)); };
    // Backspace reclaims -- only in a spirit guide's reclaim mode (wired only then, like the skill rows).
    std::function<void()> refund;
    if (reclaim) refund = [this, sid] {
      std::vector<gameapi::DevotionConstellation> all = gameapi::constellations();
      for (const gameapi::DevotionConstellation& c2 : all) for (const gameapi::DevotionStar& s2 : c2.stars) if (s2.skill_id == sid) {
        std::string why = gameapi::can_reclaim_star(c2, s2, all);
        if (!why.empty()) { speech::speak(why, true); return; }
        bool uncompleted = false;
        bool ok = gameapi::reclaim_star(sid, uncompleted);
        MessageBuilder m; m.fragment(ok ? strings::kReclaimed : strings::kCannot);
        if (ok && uncompleted) m.list_item().fragment(strings::kConstellationComplete).fragment(strings::kLost);
        speech::speak(m.build(), true);
        refresh();
        return;
      }
      speech::speak(strings::kCannot, true);
    };
    auto v = row_item(star_label(s), [value] { return value; }, activate, tooltip, refund);
    b.add_item(ControlId::structural(std::format("{}.star{}", group_id, s.index)), v);
  }
  void build_constellations(GraphBuilder& b, const std::vector<gameapi::DevotionConstellation>& cons) {
    add_devotion_header(b);
    bool reclaim = exe_ui::skills_reclaim_mode();   // a spirit guide opened the window: stars can be reclaimed
    unsigned cost = reclaim ? gameapi::devotion_reclaim_cost() : 0;
    if (reclaim) {   // the hint row, as on the mastery tabs: "spirit guide, Backspace to reclaim a devotion point, 25 iron bits and 1 aether crystals each"
      MessageBuilder m; m.fragment(strings::kSpiritGuide).list_item().fragment(strings::kReclaimDevotionHint);
      m.list_item().fragment(std::format("{}", cost)).fragment(strings::kIronBits).fragment(strings::kAnd).fragment(std::format("{}", gameapi::devotion_reclaim_aether_cost())).fragment(strings::kAetherCrystals).fragment(strings::kEach);
      b.add_item(ControlId::structural("skills.devreclaim"), line_item(m.build()));
      add_clear_all(b);
    }
    // Three Tab stops -- learned (a star taken; complete ones included), available (open to take), unavailable (locked
    // behind an affinity) -- alphabetical within each, "empty" for a stop with nothing in it (as the inventory's bags).
    // The name carries the progress: "Bat (2/5)". Reclaim mode (a spirit guide) changes only the rows: the hint row
    // above and each star's cost + Backspace, the same in every stop.
    struct Stop { const char* key; std::string_view title; std::vector<const gameapi::DevotionConstellation*> items; };
    Stop stops[3] = {{"learned", strings::kLearned, {}}, {"available", strings::kAvailable, {}}, {"unavailable", strings::kUnavailable, {}}};
    for (const gameapi::DevotionConstellation& c : cons) stops[c.learned ? 0 : c.affinity_met ? 1 : 2].items.push_back(&c);
    auto lower = [](std::string t) { for (char& ch : t) ch = (char)tolower((unsigned char)ch); return t; };
    for (Stop& st : stops) {
      std::stable_sort(st.items.begin(), st.items.end(), [&](const gameapi::DevotionConstellation* a, const gameapi::DevotionConstellation* c) { return lower(a->name) < lower(c->name); });
      b.begin_stop(std::string("skills.") + st.key);
      b.push_context(st.title, strings::kList);
      if (st.items.empty()) b.add_item(ControlId::structural(std::format("skills.{}.empty", st.key)), line_item(std::string(strings::kEmpty)));
      for (const gameapi::DevotionConstellation* cp : st.items) add_constellation(b, *cp, reclaim, cost);
      b.pop_context();
    }
  }
  // "clear all constellations, 13 devotion points, 425 iron bits and 13 aether crystals": every learned star at once,
  // ignoring the lock order, at the price of reclaiming them one by one; a yes/no picker repeats the cost first.
  static void push_clear_cost(MessageBuilder& m, const gameapi::DevotionClearCost& c) {
    m.list_item().fragment(std::format("{}", c.points)).fragment(strings::kDevotionPoints);
    m.list_item().fragment(std::format("{}", c.bits)).fragment(strings::kIronBits).fragment(strings::kAnd).fragment(std::format("{}", c.aether)).fragment(strings::kAetherCrystals);
  }
  void add_clear_all(GraphBuilder& b) {
    gameapi::DevotionClearCost cost = gameapi::devotion_clear_cost();
    if (!cost.ok || cost.points == 0) return;
    MessageBuilder m; m.list_item().fragment(strings::kClearAllConstellations); push_clear_cost(m, cost);
    auto activate = [this] {
      gameapi::DevotionClearCost c = gameapi::devotion_clear_cost();
      if (!c.ok || c.points == 0) { speech::speak(strings::kCannot, true); return; }
      MessageBuilder t; t.list_item().fragment(strings::kClearAllQuestion); push_clear_cost(t, c);
      open_picker(t.build(), {{0, std::string(strings::kNo), {}}, {1, std::string(strings::kYes), {}}}, [this](unsigned id) {
        if (id != 1) return;
        std::string why = gameapi::clear_all_devotion();
        speech::speak(why.empty() ? std::string(strings::kAllConstellationsCleared) : why, true);
        refresh();
      });
    };
    b.add_item(ControlId::structural("skills.devclearall"), row_item(m.build(), {}, activate));
  }
  void add_constellation(GraphBuilder& b, const gameapi::DevotionConstellation& c, bool reclaim, unsigned cost) {
    std::string id = std::format("skills.con{:x}", (uintptr_t)c.p);
    std::string name = std::format("{} ({}/{})", c.name, c.learned, c.stars.size()), value = constellation_value(c);
    gameapi::DevotionConstellation copy = c;
    auto v = std::make_shared<NodeVtable>();
    v->control_type = &kGroupType;
    v->announcements = {NodeAnnouncement([name] { return name; }, false, announcement_kinds::kLabel),
                        NodeAnnouncement([value] { return value; }, true, announcement_kinds::kValue)};
    v->on_tooltip = [copy] { speak_lines(gameapi::constellation_tooltip(copy)); };
    b.begin_group(ControlId::structural(id), v);
    for (unsigned i : gameapi::star_order(c)) for (const gameapi::DevotionStar& s : c.stars) if (s.index == i) add_star(b, id, c, s, reclaim, cost);
    b.end_group();
  }
  void build_powers(GraphBuilder& b, const std::vector<gameapi::DevotionConstellation>& cons) {
    bool any = false;
    for (const gameapi::DevotionConstellation& c : cons)
      for (const gameapi::DevotionStar& s : c.stars) {
        if (!s.power || !s.learned) continue;
        any = true;
        unsigned sid = s.skill_id;
        gameapi::DevotionStar star = s;
        std::string from = c.name;
        MessageBuilder m;
        strings::push_skill_level(m, s.dev_level, s.dev_max);
        if (!s.host_name.empty()) m.list_item().fragment(strings::kAttachedTo).fragment(s.host_name); else m.list_item().fragment(strings::kNotAttached);
        m.list_item().fragment(strings::kFrom).fragment(from);
        std::string value = m.build();
        auto v = row_item(s.name.empty() ? star_label(s) : s.name, [value] { return value; }, [this, star] { open_host_picker(star); }, [sid] { speak_lines(gameapi::star_tooltip(sid)); });
        b.add_item(ControlId::structural(std::format("skills.power{}", sid)), v);
      }
    if (!any) b.add_item(ControlId::structural("skills.nopowers"), line_item(std::string(strings::kNoCelestialPowers)));
  }

  Snapshot<std::vector<gameapi::SkillInfo>> skills_;
  Snapshot<std::vector<gameapi::MasteryChoice>> choices_;
  Snapshot<std::vector<gameapi::DevotionConstellation>> constellations_;
  int chosen_[2] = {-1, -1};   // the class put on each tab this session but not yet committed (ours; the game's pane follows)
};

std::unique_ptr<Screen> make_skills() { return std::make_unique<SkillsScreen>(); }
}  // namespace gd::screens
