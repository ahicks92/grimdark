#include <doctest/doctest.h>
#include "core/message_builder.h"
#include "core/strings.h"

using gd::core::MessageBuilder;

TEST_CASE("fragments are space-joined") {
  MessageBuilder m;
  m.fragment("a").fragment("b").fragment("");
  CHECK(m.build() == "a b");
}

TEST_CASE("list items are comma-joined, fragments inside an item space-joined") {
  MessageBuilder m;
  m.list_item().fragment("Create").list_item().fragment("button").fragment("x").list_item().fragment("1 of 7");
  CHECK(m.build() == "Create, button x, 1 of 7");
}

TEST_CASE("no leading comma before the first item, empty fragments ignored") {
  MessageBuilder m;
  m.list_item().fragment("").list_item().fragment("only");
  CHECK(m.build() == "only");
}

TEST_CASE("single use") {
  MessageBuilder m;
  m.fragment("x");
  (void)m.build();
  CHECK_THROWS(m.fragment("y"));
  CHECK_THROWS(m.build());
}

TEST_CASE("a lone space fragment is rejected") {
  MessageBuilder m;
  CHECK_THROWS(m.fragment(" "));
}

TEST_CASE("push_control shape") {
  MessageBuilder m;
  gd::strings::push_control(m, "Delete", gd::strings::kButton, false, true);
  gd::strings::push_position(m, 2, 7);
  CHECK(m.build() == "Delete, button, disabled, 2 of 7");
}

TEST_CASE("push_item shape") {
  { MessageBuilder m; gd::strings::push_item(m, "Gladius", 7, 1, true, false); CHECK(m.build() == "Gladius with component"); }
  { MessageBuilder m; gd::strings::push_item(m, "Gladius", 7, 3, true, true); CHECK(m.build() == "Gladius with component augment x 3"); }
  { MessageBuilder m; gd::strings::push_item(m, "Scrap", 7, 5, false, false); CHECK(m.build() == "Scrap x 5"); }
  { MessageBuilder m; gd::strings::push_item(m, "", 1234, 1, false, false); CHECK(m.build() == "item 1234"); }
  { MessageBuilder m; gd::strings::push_item(m, "Gladius", 7, 1, false, true); CHECK(m.build() == "Gladius with augment"); }
}

TEST_CASE("push_walk_stopped shape") {
  { MessageBuilder m; gd::strings::push_walk_stopped(m, 0); CHECK(m.build() == "stopped"); }
  { MessageBuilder m; gd::strings::push_walk_stopped(m, 3); CHECK(m.build() == "stopped, 3 enemies near"); }
}
