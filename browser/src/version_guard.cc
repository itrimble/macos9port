#include "version_guard.h"

#include <cstdlib>
#include <ctime>

#include "browser_stats.h"
#include "web_apps.h"
#include "include/cef_parser.h"
#include "include/cef_urlrequest.h"
#include "include/cef_version.h"

#ifndef OS999_CEF_RELEASE_DATE
#define OS999_CEF_RELEASE_DATE "1970-01-01"
#endif

namespace {

#if defined(OS_MAC)
#if defined(ARCH_CPU_ARM64)
const char kPlatform[] = "mac_arm64";
#else
const char kPlatform[] = "mac";
#endif
#else
const char kPlatform[] = "linux";
#endif

class Fetch : public CefURLRequestClient {
 public:
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    bool ok = request->GetRequestStatus() == UR_SUCCESS && request->GetResponse() &&
              request->GetResponse()->GetStatus() == 200;
    VersionGuard::Get().Finish(body_, ok && !overflow_);
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest>, const void* data, size_t n) override {
    if (n > (1u << 20) - body_.size()) { overflow_ = true; return; }
    if (!overflow_) body_.append((const char*)data, n);
  }
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&,
                          CefRefPtr<CefAuthCallback>) override {
    return false;
  }

 private:
  std::string body_;
  bool overflow_ = false;
  IMPLEMENT_REFCOUNTING(Fetch);
};

ChromeVersion Ours() {
  ChromeVersion v;
  v.part[0] = CHROME_VERSION_MAJOR;
  v.part[1] = CHROME_VERSION_MINOR;
  v.part[2] = CHROME_VERSION_BUILD;
  v.part[3] = CHROME_VERSION_PATCH;
  return v;
}

}  // namespace

VersionGuard& VersionGuard::Get() {
  static VersionGuard guard;
  return guard;
}

void VersionGuard::Start(std::function<void(const VersionResult&)> on_result) {
  on_result_ = std::move(on_result);
#ifdef OS999_ENABLE_TEST_HOOKS
  const char* override_value = getenv("OS999_ALLOW_OUTDATED");
  allow_outdated_ = override_value && std::string(override_value) == "1";
#endif
  pin_age_ = DaysSince(OS999_CEF_RELEASE_DATE, (long long)time(nullptr));
  // Until the online answer arrives, apply the offline (age-only) rule.
  result_ = EvaluateVersion(Ours(), {}, pin_age_, allow_outdated_);
  CefRefPtr<CefRequest> req = CefRequest::Create();
  req->SetURL(std::string("https://versionhistory.googleapis.com/v1/chrome/platforms/") +
              kPlatform + "/channels/stable/versions/all/releases?filter=endtime=none");
  req->SetMethod("GET");
  // No cookies or credentials: this request identifies nothing about the user.
  req->SetFlags(UR_FLAG_DISABLE_CACHE);
  pending_ = true;
  CefURLRequest::Create(req, new Fetch(), nullptr);
}

void VersionGuard::Finish(const std::string& json, bool ok) {
  std::vector<ServedRelease> served;
  CefRefPtr<CefValue> root = ok ? CefParseJSON(json, JSON_PARSER_RFC) : nullptr;
  CefRefPtr<CefDictionaryValue> dict = root ? root->GetDictionary() : nullptr;
  CefRefPtr<CefListValue> list =
      dict && dict->HasKey("releases") ? dict->GetList("releases") : nullptr;
  long long now = (long long)time(nullptr);
  for (size_t i = 0; list && i < list->GetSize(); i++) {
    CefRefPtr<CefDictionaryValue> rel = list->GetDictionary(i);
    if (!rel) continue;
    ServedRelease s;
    if (!ChromeVersion::Parse(rel->GetString("version").ToString(), &s.version)) continue;
    CefRefPtr<CefDictionaryValue> serving = rel->GetDictionary("serving");
    int days = serving ? DaysSince(serving->GetString("startTime").ToString(), now) : 0;
    s.days_serving = days < 0 ? 0 : days;
    served.push_back(s);
  }
  result_ = EvaluateVersion(Ours(), served, pin_age_, allow_outdated_);
  BrowserStats::Get().Log("version check: " + std::string(ok ? "ok" : "failed") + " served=" +
                          std::to_string(served.size()) + " verdict=" +
                          std::to_string((int)result_.verdict) + " block=" +
                          std::to_string(result_.block) + " " + result_.message);
  pending_ = false;
  std::vector<CefRefPtr<CefBrowser>> seen;
  seen.swap(seen_while_pending_);
  if (result_.block) {
    for (auto& b : seen) {
      if (!b->IsValid()) continue;
      std::string url = b->GetMainFrame()->GetURL().ToString();
      if (url.empty() || url.rfind("http", 0) == 0) b->GetMainFrame()->LoadURL(BlockPageURL());
    }
  }
  if (on_result_) on_result_(result_);
}

bool VersionGuard::ShouldBlock(CefRefPtr<CefBrowser> browser, const std::string& url) {
  if (WebApps::Internal(url)) return false;  // Bundled app, not a network destination.
  bool web = url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
  if (web && pending_ && browser) {
    bool known = false;
    for (auto& b : seen_while_pending_) known |= b->IsSame(browser);
    if (!known) seen_while_pending_.push_back(browser);
  }
  return web && result_.block;
}

std::string VersionGuard::BlockPageURL() const {
  std::string html =
      "<!doctype html><meta charset=utf-8><title>Update required</title>"
      "<body style='font:16px -apple-system,sans-serif;margin:40px;max-width:640px'>"
      "<h1>OS999 Browser is out of date</h1><p>" + result_.message +
      "</p><p>Web pages are blocked because this Chromium version no longer receives "
      "security fixes. Update the pinned version in <code>browser/cef-pin.txt</code>, then run "
      "<code>sh browser/scripts/build.sh</code>.</p><p>HTTPS browsing resumes after updating.</p>";
  return "data:text/html;base64," + CefBase64Encode(html.data(), html.size()).ToString();
}
