#import <Foundation/Foundation.h>

#include "embed_diag.h"

#include <cstdlib>
#include <vector>

#include "decode_diag.h"
#import "decode_diag_host.h"
#include "embed_frames.h"
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_render_handler.h"
#include "include/wrapper/cef_helpers.h"

namespace embed_diag {
namespace {

// A windowless browser that paints nowhere: only its console output matters.
class DiagBrowser : public CefClient,
                    public CefRenderHandler,
                    public CefLifeSpanHandler,
                    public CefDisplayHandler {
 public:
  explicit DiagBrowser(std::function<void()> closed) : closed_(std::move(closed)) {}
  void Open(const std::string& url) {
    sink_ = FrameSink::Create(0);  // only for the off-screen parent view; never used to send frames
    CefWindowInfo info;
    info.SetAsWindowless(sink_->ParentHandle());
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserSettings settings;
    settings.windowless_frame_rate = 1;
    CefBrowserHost::CreateBrowser(info, this, url, settings, nullptr, nullptr);
  }
  void Scrape() {
    if (browser_)
      browser_->GetMainFrame()->ExecuteJavaScript(DecodeDiagScript().UTF8String, "os999://decode-diag", 0);
  }
  void Close() {
    if (browser_) browser_->GetHost()->CloseBrowser(true);
  }
  bool open() const { return browser_ != nullptr; }
  bool pending() const { return browser_ != nullptr || creating_; }

  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override { rect = CefRect(0, 0, 900, 700); }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList&, const void*, int, int) override {}
  void OnAfterCreated(CefRefPtr<CefBrowser> b) override { browser_ = b; creating_ = false; }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    browser_ = nullptr;
    if (closed_) closed_();
  }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message,
                        const CefString&, int) override {
    NSString* s = [NSString stringWithUTF8String:message.ToString().c_str()];
    if (s && ([s hasPrefix:@"OS999MEDIA "] || [s hasPrefix:@"OS999GPU "])) DecodeDiagConsole(s);
    return true;
  }
  void MarkCreating() { creating_ = true; }

 private:
  std::function<void()> closed_;
  std::unique_ptr<FrameSink> sink_;
  CefRefPtr<CefBrowser> browser_;
  bool creating_ = false;
  IMPLEMENT_REFCOUNTING(DiagBrowser);
};

std::vector<CefRefPtr<DiagBrowser>> gBrowsers;
std::function<void()> gOnClosed;
bool gStarted = false, gOpened = false, gStopped = false;
int gTicks = 0, gDelay = 30;

}  // namespace

void Start(std::function<void()> on_closed) {
  CEF_REQUIRE_UI_THREAD();
  if (!decode_diag::Enabled() || gStarted) return;
  gStarted = true;
  gOnClosed = std::move(on_closed);
  // Chromium has to have created its players before media-internals is useful; the
  // delay can be changed with OS999_DECODE_DIAG_DELAY (seconds, 1..3600).
  gDelay = decode_diag::ParseDelaySeconds(getenv("OS999_DECODE_DIAG_DELAY"), gDelay);
}

void Tick() {
  if (!gStarted || gStopped) return;
  ++gTicks;
  if (!gOpened && gTicks >= gDelay) {
    gOpened = true;
    for (const char* url : {"chrome://media-internals", "chrome://gpu"}) {
      CefRefPtr<DiagBrowser> b(new DiagBrowser(gOnClosed));
      b->MarkCreating();
      gBrowsers.push_back(b);
      b->Open(url);
    }
  }
  for (auto& b : gBrowsers) b->Scrape();
}

void Stop() {
  if (!gStarted || gStopped) return;
  gStopped = true;
  DecodeDiagFinish();
  for (auto& b : gBrowsers) b->Close();
}

bool Busy() {
  for (auto& b : gBrowsers)
    if (b->pending()) return true;
  return false;
}

}  // namespace embed_diag
