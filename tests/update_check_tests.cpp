#include <doctest/doctest.h>
#include "core/update_check.h"

using namespace gd::core::update;

TEST_CASE("update: channel of the embedded version") {
  CHECK(channel_of("v0.4.0") == Channel::Release);
  CHECK(channel_of("v1.10.2-rc1") == Channel::Release);
  CHECK(channel_of("ci-03c9462") == Channel::Ci);
  CHECK(channel_of("dev") == Channel::None);
  CHECK(channel_of("ci-") == Channel::None);
  CHECK(channel_of("ci-xyz") == Channel::None);
  CHECK(channel_of("0.4.0") == Channel::None);
  CHECK(request_path(Channel::Release) == "/repos/ahicks92/grimdark/releases/latest");
  CHECK(request_path(Channel::Ci) == "/repos/ahicks92/grimdark/releases/tags/ci-latest");
  CHECK(request_path(Channel::None).empty());
}

TEST_CASE("update: json string fields") {
  const char* release = "{\"url\":\"x\",\"tag_name\": \"v0.5.0\",\"name\":\"a \\\"q\\\"\"}";
  const char* escaped = "{\"name\":\"a \\\"q\\\" \\\\ b\"}";
  CHECK(json_string(release, "tag_name") == "v0.5.0");
  CHECK(json_string(escaped, "name") == "a \"q\" \\ b");
  CHECK(json_string(R"({"tag_name":5})", "tag_name").empty());
  CHECK(json_string(R"({"other":"v"})", "tag_name").empty());
  CHECK(json_string(R"({"tag_name":"unterminated)", "tag_name").empty());
}

TEST_CASE("update: releases compare by semver") {
  CHECK(decide("v0.4.0", R"({"tag_name":"v0.5.0"})").newer);
  CHECK(decide("v0.4.0", R"({"tag_name":"v0.5.0"})").latest == "v0.5.0");
  CHECK(decide("v0.4.9", R"({"tag_name":"v0.10.0"})").newer);   // numeric, not text
  CHECK_FALSE(decide("v0.5.0", R"({"tag_name":"v0.5.0"})").newer);
  CHECK_FALSE(decide("v0.6.0", R"({"tag_name":"v0.5.0"})").newer);
  CHECK_FALSE(decide("v0.4.0", R"({"message":"Not Found"})").newer);
}

TEST_CASE("update: CI builds compare by commit") {
  const char* json = R"({"tag_name":"ci-latest","target_commitish":"26ab37012345678901234567890123456789abcd"})";
  CHECK_FALSE(decide("ci-26ab370", json).newer);
  Verdict v = decide("ci-4d25d72", json);
  CHECK(v.newer);
  CHECK(v.latest == "26ab370");
  CHECK_FALSE(decide("ci-4d25d72", R"({"target_commitish":"main"})").newer);   // not a sha
  CHECK_FALSE(decide("dev", json).newer);
}
