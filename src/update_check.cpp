#include "update_check.h"
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include "core/message_builder.h"
#include "core/strings.h"
#include "core/update_check.h"
#include "grimdark_version.h"
#include "log.h"
#include "settings.h"
#include "speech.h"

namespace gd::update {
namespace {
constexpr const char* kSettingKey = "updatecheck";
constexpr size_t kMaxBody = 1 << 20;
std::thread g_worker;
std::mutex g_mu;
HINTERNET g_request = nullptr;   // the request in flight (closing it from shutdown() aborts the blocking calls)
std::atomic<bool> g_cancel{false};

std::wstring widen(std::string_view s) { return std::wstring(s.begin(), s.end()); }   // ASCII paths and versions

// GET https://api.github.com<path>; the body, or "" on any failure (logged).
std::string fetch(const std::string& path) {
  std::wstring agent = L"Grimdark/" + widen(kGrimdarkVersion);
  HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) { log::writef("update: WinHttpOpen failed ({})", GetLastError()); return {}; }
  WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
  HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", widen(path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
  std::string body;
  DWORD status = 0;
  if (request) {
    { std::lock_guard<std::mutex> l(g_mu); if (g_cancel) { WinHttpCloseHandle(request); request = nullptr; } else g_request = request; }
  }
  if (request) {
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    bool ok = WinHttpSendRequest(request, headers, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
    DWORD size = sizeof status;
    if (ok) ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    while (ok && !g_cancel) {
      DWORD avail = 0;
      if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
      size_t at = body.size();
      if (at + avail > kMaxBody) { body.clear(); break; }
      body.resize(at + avail);
      DWORD got = 0;
      if (!WinHttpReadData(request, body.data() + at, avail, &got)) { body.clear(); break; }
      body.resize(at + got);
    }
    if (!ok) log::writef("update: request failed ({})", GetLastError());
    std::lock_guard<std::mutex> l(g_mu);
    if (g_request) { WinHttpCloseHandle(g_request); g_request = nullptr; }   // else shutdown() closed it
  }
  if (connect) WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  if (status != 200) { if (status) log::writef("update: GitHub answered {} for {}", status, path); return {}; }
  return body;
}

void run() {
  std::string_view mine = kGrimdarkVersion;
  core::update::Channel ch = core::update::channel_of(mine);
  std::string body = fetch(core::update::request_path(ch));
  if (g_cancel || body.empty()) return;
  core::update::Verdict v = core::update::decide(mine, body);
  log::writef("update: installed {}, newer={} latest='{}'", mine, v.newer, v.latest);
  if (!v.newer) return;
  core::MessageBuilder m;
  m.list_item().fragment(ch == core::update::Channel::Ci ? strings::kCiUpdateAvailable : strings::kUpdateAvailable).fragment(v.latest);
  m.list_item().fragment(strings::kYouHave).fragment(mine);
  m.list_item().fragment(strings::kRunInstallerToUpdate);
  speech::speak(m.build(), false);
}
}  // namespace

bool enabled() { return settings::get_bool(kSettingKey, true); }
void set_enabled(bool on) { settings::set_bool(kSettingKey, on); }

void start() {
  log::writef("grimdark: version {}", kGrimdarkVersion);
  if (core::update::channel_of(kGrimdarkVersion) == core::update::Channel::None || !enabled() || g_worker.joinable()) return;
  g_cancel = false;
  g_worker = std::thread(run);
}

void shutdown() {
  g_cancel = true;
  { std::lock_guard<std::mutex> l(g_mu); if (g_request) { WinHttpCloseHandle(g_request); g_request = nullptr; } }
  if (g_worker.joinable()) g_worker.join();
}
}  // namespace gd::update
