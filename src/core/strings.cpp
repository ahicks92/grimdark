#include "strings.h"
#include <format>

namespace gd::strings {
using gd::core::MessageBuilder;

MessageBuilder& push_position(MessageBuilder& m, int index1, int count) {
  m.list_item().fragment(std::format("{} of {}", index1, count));
  return m;
}

MessageBuilder& push_control(MessageBuilder& m, std::string_view label, std::string_view role, bool selected, bool disabled) {
  m.list_item().fragment(label);
  if (!role.empty()) m.list_item().fragment(role);
  if (selected) m.list_item().fragment(kSelected);
  if (disabled) m.list_item().fragment(kDisabled);
  return m;
}

MessageBuilder& push_where(MessageBuilder& m, float x, float z, std::string_view region, double life, float life_max) {
  m.fragment(std::format("x {:.0f}", x));
  m.list_item().fragment(std::format("z {:.0f}", z));
  m.list_item().fragment(std::format("life {:.0f} of {:.0f}", life, life_max));
  if (!region.empty()) m.list_item().fragment(std::format("region {}", region));
  return m;
}
MessageBuilder& push_scan_item(MessageBuilder& m, std::string_view label, float distance, int clock_hour, int index1, int count, bool distant, std::string_view note) {
  m.list_item().fragment(label);         // the first list item gets no comma; the label must BE an item for "label, N away"
  if (!note.empty()) m.list_item().fragment(note);
  if (distant) m.list_item().fragment(kDistant);
  push_distance_bearing(m, distance, clock_hour);
  m.list_item();
  push_position(m, index1, count);
  return m;
}
MessageBuilder& push_scan_self(MessageBuilder& m, std::string_view label, int index1, int count) {
  m.list_item().fragment(label).list_item().fragment(kYou).list_item();
  push_position(m, index1, count);
  return m;
}
MessageBuilder& push_unsupported_build(MessageBuilder& m, std::string_view exe_ts, const std::vector<std::string_view>& supported) {
  m.fragment(kModName).list_item().fragment(kUnsupportedBuild).list_item().fragment(kExeTimestamp).fragment(exe_ts);
  m.list_item().fragment(kSupported);
  for (std::string_view s : supported) m.fragment(s);
  m.list_item().fragment(kModOff);
  return m;
}
MessageBuilder& push_pet_event(MessageBuilder& m, std::string_view label, std::string_view event) {
  m.fragment(label).fragment(event);
  return m;
}
std::string_view classification_word(int classification) {
  switch (classification) {
    case 1: return "champion";
    case 2: return "hero";
    case 3: return "boss";
    case 4: return "quest";
    case 5: return "super boss";
    default: return {};   // 0 Common / -1 unknown: no rarity word
  }
}
MessageBuilder& push_enemy_label(MessageBuilder& m, std::string_view name, int level, int classification) {
  m.fragment(name).fragment(std::format("level {}", level));
  std::string_view w = classification_word(classification);
  if (!w.empty()) m.fragment(w);
  return m;
}
MessageBuilder& push_target_inspect(MessageBuilder& m, int health_percent, const std::vector<std::string>& effects) {
  m.fragment(std::format("{}", health_percent)).fragment(kPercent).fragment(kHealth);
  for (const std::string& e : effects) m.list_item().fragment(e);
  return m;
}
MessageBuilder& push_character_summary(MessageBuilder& m, unsigned level, std::string_view class_name, bool hardcore) {
  m.list_item().fragment(std::format("level {}", level));
  if (!class_name.empty()) m.fragment(class_name);
  if (hardcore) m.list_item().fragment(kHardcore);
  return m;
}
MessageBuilder& push_distance_bearing(MessageBuilder& m, float distance, int clock_hour) {
  m.list_item().fragment(std::format("{:.0f} away", distance));
  m.list_item().fragment(std::format("{} o'clock", clock_hour));
  return m;
}
MessageBuilder& push_place(MessageBuilder& m, std::string_view region, std::string_view subregion, std::string_view room) {
  if (!region.empty()) m.list_item().fragment(region);
  if (!subregion.empty()) m.list_item().fragment(subregion);
  if (!room.empty()) m.list_item().fragment(room);
  return m;
}
MessageBuilder& push_nothing_nearby(MessageBuilder& m, std::string_view group_plural) {
  m.fragment("no").fragment(group_plural).fragment("nearby");
  return m;
}
MessageBuilder& push_cursor_mode(MessageBuilder& m, bool polar) {
  m.fragment(kCursorMode).fragment(polar ? kCursorPolar : kCursorGrid);
  return m;
}
MessageBuilder& push_speech(MessageBuilder& m, std::string_view speaker, std::string_view speech) {
  if (!speaker.empty()) m.fragment(std::string(speaker) + ":");
  m.fragment(speech);
  return m;
}
MessageBuilder& push_count(MessageBuilder& m, int count, std::string_view singular, std::string_view plural) {
  m.list_item().fragment(std::format("{} {}", count, count == 1 ? singular : plural));
  return m;
}

MessageBuilder& push_vitals(MessageBuilder& m, double life, float life_max, float energy, float energy_max) {
  m.list_item().fragment(kHealth).fragment(std::format("{:.0f} of {:.0f}", life, life_max));
  m.list_item().fragment(kEnergy).fragment(std::format("{:.0f} of {:.0f}", energy, energy_max));
  return m;
}
MessageBuilder& push_health_percent(MessageBuilder& m, int percent) {
  m.fragment(kHealth).fragment(std::format("{}", percent)).fragment(kPercent);
  return m;
}
MessageBuilder& push_combat_hit(MessageBuilder& m, std::string_view number, bool crit) {
  m.fragment(number);
  if (crit) m.fragment(kCrit);
  return m;
}
MessageBuilder& push_combat_word(MessageBuilder& m, std::string_view word) {
  m.fragment(word);
  return m;
}
MessageBuilder& push_kills(MessageBuilder& m, int count, uint64_t xp) {
  if (count > 1) {   // a pack: "3 killed", "3 killed, 300 exp"
    m.fragment(std::format("{}", count)).fragment(kKilled);
    if (xp > 0) m.list_item().fragment(std::format("{}", xp)).fragment(kExp);
  } else {   // a single kill drops "killed" -- just the exp, always ("0 exp" when it gave none)
    m.fragment(std::format("{}", xp)).fragment(kExp);
  }
  return m;
}

MessageBuilder& push_quest_objective(MessageBuilder& m, std::string_view quest, std::string_view objective) {
  return m.fragment(std::string(quest) + ":").fragment(objective);
}
MessageBuilder& push_range_hint(MessageBuilder& m, unsigned lo, unsigned hi) { return m.fragment("enter").fragment(std::format("{} to {}", lo, hi)); }
MessageBuilder& push_stack(MessageBuilder& m, std::string_view name, unsigned stack) {
  m.fragment(name);
  if (stack > 1) m.list_item().fragment(std::format("x {}", stack));
  return m;
}
MessageBuilder& push_item(MessageBuilder& m, std::string_view name, unsigned id, unsigned stack, bool component, bool augment) {
  if (name.empty()) m.fragment(kItem).fragment(std::to_string(id));
  else m.fragment(name);
  if (component || augment) m.fragment(kWith);
  if (component) m.fragment(kComponent);
  if (augment) m.fragment(kAugment);
  if (stack > 1) m.list_item().fragment(std::format("x {}", stack));
  return m;
}
MessageBuilder& push_stat(MessageBuilder& m, std::string_view label, std::string_view value) { return m.fragment(label).fragment(value); }
MessageBuilder& push_armor_part(MessageBuilder& m, std::string_view region, int armor, std::string_view hit_label, int chance,
                                std::string_view absorption_label, int absorption) {
  m.list_item().fragment(region).fragment(std::to_string(armor));
  m.list_item().fragment(hit_label).fragment(percent(chance));
  return m.list_item().fragment(absorption_label).fragment(percent(absorption));
}
std::string strip_markup(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '{' && i + 3 < text.size() && text[i + 1] == '^' && text[i + 3] == '}') {   // {^x}
      if (text[i + 2] == 'n' || text[i + 2] == 'N') out += ' ';
      i += 3;
      continue;
    }
    if (c == '^' && i + 1 < text.size()) {   // ^x
      if (text[i + 1] == '^') { out += '^'; ++i; continue; }
      if (text[i + 1] == 'n' || text[i + 1] == 'N') out += ' ';
      ++i;
      continue;
    }
    out += c;
  }
  return out;
}
MessageBuilder& push_skill_level(MessageBuilder& m, unsigned level, unsigned max_level) { return m.list_item().fragment(kLevel).fragment(std::format("{} of {}", level, max_level)); }
MessageBuilder& push_shortfall(MessageBuilder& m, std::string_view label, int have, int need) { return m.list_item().fragment(label).fragment(std::format("{} of {}", have, need)); }
MessageBuilder& push_faction(MessageBuilder& m, std::string_view name, std::string_view level_name, float value, int low, int high) {
  m.list_item().fragment(name);
  if (!level_name.empty()) m.list_item().fragment(level_name);
  m.list_item().fragment(std::format("{:.0f} of {}", value - (float)low, high - low));
  return m;
}
}  // namespace gd::strings
