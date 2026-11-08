#include "web_apps.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <unistd.h>
#include "network_policy.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_request_context.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_byte_read_handler.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "web_app_resources.h"

namespace WebApps {
namespace {
std::mutex mutex;
std::string registry;
const char origin[] = "https://os999.invalid";
std::string Escape(const std::string& in) {
  std::string out;
  for (char c : in) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else if ((unsigned char)c >= 32) out += c;
  }
  return out;
}
std::string Resolve(const std::string& base, const std::string& relative) {
  CefString out;
  return CefResolveURL(base, relative, out) ? out.ToString() : "";
}
std::string Origin(const std::string& url) {
  CefURLParts p;
  if (!BrowserHttps(url) || !CefParseURL(url, p)) return "";
  std::string value = CefString(&p.origin).ToString();
  if (!value.empty() && value.back() == '/') value.pop_back();
  return value;
}
CefRefPtr<CefListValue> Read() {
  std::ifstream f(registry, std::ios::binary);
  std::string data;
  if (f) { f.seekg(0, std::ios::end); if (f.tellg() > 262144) return CefListValue::Create();
    f.seekg(0); data.assign(std::istreambuf_iterator<char>(f), {}); }
  auto value = CefParseJSON(data, JSON_PARSER_RFC);
  return value && value->GetType() == VTYPE_LIST ? value->GetList() : CefListValue::Create();
}
bool Save(CefRefPtr<CefListValue> list) {
  auto value = CefValue::Create(); value->SetList(list);
  auto data = CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  if (registry.empty() || data.size() > 262144) return false;
  std::string temp = registry + ".XXXXXX";
  int fd = mkstemp(temp.data());
  if (fd < 0) return false;
  FILE* f = fdopen(fd, "wb");
  if (!f) { close(fd); unlink(temp.c_str()); return false; }
  bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
  if (fflush(f) || fsync(fd)) ok = false;
  if (fclose(f)) ok = false;
  if (ok) ok = rename(temp.c_str(), registry.c_str()) == 0;
  if (!ok) unlink(temp.c_str());
  return ok;
}
std::string Library() {
  std::lock_guard<std::mutex> guard(mutex);
  std::string html = "<!doctype html><meta charset=utf-8><title>OS999 Apps</title>"
    "<style>body{font:18px system-ui;background:#eee;margin:40px}a{display:block;margin:20px 0}</style>"
    "<h1>Your web apps</h1><p>Open a secure app and choose Install (+), or press Cmd-Shift-I. "
    "Remove a shortcut from its start page with Cmd-Shift-U; site data is kept. Apps keep their site storage in this browser profile. Offline play depends on each app.</p>"
    "<a href='/play/' target='_blank'>Controller Arena — WebGL, WASM and controller check</a>";
  auto list = Read();
  for (size_t i = 0; i < list->GetSize(); ++i) {
    auto app = list->GetDictionary(i);
    if (!app || !app->HasKey("start_url") || !BrowserHttps(app->GetString("start_url"))) continue;
    html += "<a target='_blank' rel='noopener' href='" + Escape(app->GetString("start_url")) + "'>" +
            Escape(app->GetString("name")) + "</a>";
  }
  return html;
}
class Body : public CefBaseRefCounted {
 public:
  explicit Body(std::string text) : data(std::move(text)) {}
  std::string data;
  IMPLEMENT_REFCOUNTING(Body);
};
class Factory : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      const CefString&, CefRefPtr<CefRequest> request) override {
    CefURLParts parts; CefParseURL(request->GetURL(), parts);
    std::string path = CefString(&parts.path).ToString(), body, mime = "text/html";
    int status = 200;
    if (path == "/apps" || path == "/apps/") body = Library();
    else if (!WebAppResource(path, &body, &mime)) { status = 404; body = "Not found"; }
    CefRefPtr<Body> source = new Body(std::move(body));
    auto stream = CefStreamReader::CreateForHandler(new CefByteReadHandler(
      reinterpret_cast<const unsigned char*>(source->data.data()), source->data.size(), source));
    CefResponse::HeaderMap headers;
    headers.insert({"Content-Security-Policy", "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; object-src 'none'; frame-ancestors 'none'"});
    headers.insert({"X-Content-Type-Options", "nosniff"});
    headers.insert({"Cache-Control", "no-cache"});
    return new CefStreamResourceHandler(status, status == 200 ? "OK" : "Not Found", mime, headers, stream);
  }
  IMPLEMENT_REFCOUNTING(Factory);
};
class Installer : public CefDevToolsMessageObserver {
 public:
  Installer(CefRefPtr<CefBrowser> b, std::function<void(bool,const std::string&)> done)
      : browser(b), callback(std::move(done)), document(b->GetMainFrame()->GetURL()) {}
  void Start() {
    registration = browser->GetHost()->AddDevToolsMessageObserver(this);
    request = browser->GetHost()->ExecuteDevToolsMethod(0, "Page.getAppManifest", nullptr);
    if (!request) Finish(false, "Could not read the app manifest");
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<Installer> self) {
      if (self->callback) self->Finish(false, "The app manifest request timed out");
    }, CefRefPtr<Installer>(this)), 30000);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int id, bool success,
      const void* data, size_t size) override {
    if (id != request) return;
    if (!success || size > 262144 || browser->GetMainFrame()->GetURL() != document) {
      Finish(false, "App changed or its manifest could not be read"); return;
    }
    auto result = CefParseJSON(data, size, JSON_PARSER_RFC);
    auto dict = result ? result->GetDictionary() : nullptr;
    auto value = dict ? CefParseJSON(dict->GetString("data"), JSON_PARSER_RFC) : nullptr;
    auto manifest = value ? value->GetDictionary() : nullptr;
    if (!manifest) { Finish(false, "This page does not provide a web app manifest"); return; }
    std::string base = dict->GetString("url"), name = manifest->GetString("name");
    if (name.empty()) name = manifest->GetString("short_name");
    std::string start = manifest->HasKey("start_url") ? Resolve(base, manifest->GetString("start_url")) : document;
    std::string scope = Resolve(base, manifest->GetString("scope"));
    if (name.empty() || name.size() > 120 || start.size() > 2048 || scope.size() > 2048 || !BrowserHttps(start) || Origin(start) != Origin(document) ||
        Origin(base) != Origin(document)) {
      Finish(false, "App needs a name and a same-origin HTTPS start URL and manifest"); return;
    }
    if (scope.empty() || Origin(scope) != Origin(start)) scope = Origin(start) + "/";
    std::string app_id = manifest->GetString("id");
    app_id = app_id.empty() ? start : Resolve(start, app_id);
    if (app_id.size() > 2048 || Origin(app_id) != Origin(start)) { Finish(false, "App identity must stay on its origin"); return; }
    auto app = CefDictionaryValue::Create();
    app->SetString("name", name); app->SetString("start_url", start);
    app->SetString("scope", scope); app->SetString("id", app_id);
    app->SetString("display", manifest->GetString("display"));
    bool saved = false;
    {
      std::lock_guard<std::mutex> guard(mutex);
      auto list = Read(); size_t slot = list->GetSize();
      for (size_t i = 0; i < list->GetSize(); ++i) {
        auto old = list->GetDictionary(i);
        if (old && old->GetString("id") == app_id) { slot = i; break; }
      }
      if (slot < 64) { list->SetDictionary(slot, app); saved = Save(list); }
    }
    Finish(saved, saved ? "Installed. Open Apps to launch it." : "Could not save app (limit: 64 apps)");
  }
 private:
  void Finish(bool ok, const std::string& text) {
    CefRefPtr<Installer> keep(this);
    registration = nullptr;
    auto done = std::move(callback);
    if (done) done(ok, text);
  }
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<CefRegistration> registration;
  std::function<void(bool,const std::string&)> callback;
  std::string document;
  int request = 0;
  IMPLEMENT_REFCOUNTING(Installer);
};
}
const char* Home() { return "https://os999.invalid/apps/"; }
const char* Play() { return "https://os999.invalid/play/"; }
bool Internal(const std::string& url) { return Origin(url) == origin; }
void Register() {
  auto cache = CefRequestContext::GetGlobalContext()->GetCachePath().ToString();
  registry = cache.empty() ? "" : cache + "/os999-apps.json";
  CefRegisterSchemeHandlerFactory("https", "os999.invalid", new Factory());
}
void Remove(CefRefPtr<CefBrowser> browser, std::function<void(bool,const std::string&)> result) {
  if (!browser) { result(false, "No app is open"); return; }
  std::string url = browser->GetMainFrame()->GetURL();
  url = url.substr(0, url.find('#'));
  bool removed = false, saved = false;
  {
    std::lock_guard<std::mutex> guard(mutex);
    auto old = Read(), next = CefListValue::Create();
    for (size_t i = 0; i < old->GetSize(); ++i) {
      auto app = old->GetDictionary(i);
      std::string start = app ? app->GetString("start_url").ToString() : "";
      if (start.substr(0, start.find('#')) == url) removed = true;
      else if (app) next->SetDictionary(next->GetSize(), app);
    }
    if (removed) saved = Save(next);
  }
  result(saved, saved ? "App shortcut removed; site storage kept" :
         "Open the installed app's start page to remove its shortcut");
}
void Install(CefRefPtr<CefBrowser> browser, std::function<void(bool,const std::string&)> result) {
  if (!browser || browser->IsLoading() ||
      (!Internal(browser->GetMainFrame()->GetURL()) && !BrowserVerified(browser, false))) {
    result(false, "Load a verified HTTPS app before installing"); return;
  }
  CefRefPtr<Installer> install = new Installer(browser, std::move(result));
  install->Start();
}
}
