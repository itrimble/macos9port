#include "embed_browser.h"

#include <sys/stat.h>

#include <chrono>

#include "browser_stats.h"
#include "embed_host.h"
#include "version_guard.h"
#include "network_policy.h"
#include "web_apps.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

static double NowMs() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

EmbedBrowser::EmbedBrowser(EmbedHost* host, int id, int width, int height, float scale)
    : host_(host), id_(id), width_(width), height_(height), scale_(scale),
      sink_(FrameSink::Create(id)) {}

void EmbedBrowser::Create(const std::string& url) {
  CefWindowInfo info;
  info.SetAsWindowless(sink_->ParentHandle());
#if defined(OS_MAC)
  info.shared_texture_enabled = true;  // GPU IOSurface frames only
#endif
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  settings.webgl = STATE_ENABLED;
  settings.javascript = STATE_ENABLED;
  settings.windowless_frame_rate = 60;
  CefBrowserHost::CreateBrowser(info, this, url, settings, nullptr, nullptr);
}

void EmbedBrowser::Close() {
  if (browser_) { browser_->GetHost()->SetFocus(false); browser_->GetHost()->CloseBrowser(true); }
}

// ---- Desktop messages -----------------------------------------------------

void EmbedBrowser::Handle(const Os999BrowserMsg& m) {
  std::string text(m.text, strnlen(m.text, sizeof m.text));
  if (m.kind == OS999B_TARGET) {
    sink_->Target(text);
    return;
  }
  if (m.kind == OS999B_FOCUS) focused_ = m.a != 0;
  if (m.kind == OS999B_SHOW) visible_ = m.a != 0;
  if (!browser_) return;
  auto host = browser_->GetHost();
  CefMouseEvent e;
  e.x = m.x;
  e.y = m.y;
  e.modifiers = m.modifiers;
  switch (m.kind) {
    case OS999B_RESIZE:
      width_ = m.a > 0 ? m.a : 1;
      height_ = m.b > 0 ? m.b : 1;
      if (m.x > 0) scale_ = m.x / 100.0f;
      host->NotifyScreenInfoChanged();
      host->WasResized();
      break;
    case OS999B_SHOW:
      visible_ = m.a != 0;
      host->WasHidden(!visible_);
      host->SetFocus(visible_ && focused_);
      if (visible_) host->Invalidate(PET_VIEW);
      break;
    case OS999B_MOUSE_MOVE: host->SendMouseMoveEvent(e, false); break;
    case OS999B_MOUSE_LEAVE: host->SendMouseMoveEvent(e, true); break;
    case OS999B_MOUSE_DOWN:
    case OS999B_MOUSE_UP: {
      cef_mouse_button_type_t b = m.a == 2 ? MBT_RIGHT : m.a == 1 ? MBT_MIDDLE : MBT_LEFT;
      host->SendMouseClickEvent(e, b, m.kind == OS999B_MOUSE_UP, m.b > 0 ? m.b : 1);
      break;
    }
    case OS999B_WHEEL: host->SendMouseWheelEvent(e, m.a, m.b); break;
    case OS999B_KEY_DOWN:
    case OS999B_KEY_UP: Key(m); break;
    case OS999B_CHAR: Text(text); break;
    case OS999B_IME_SET: ImeSet(text, m.a); break;
    case OS999B_IME_CANCEL:
      if (composing_) host->ImeCancelComposition();
      composing_ = false;
      break;
    case OS999B_NAVIGATE: browser_->GetMainFrame()->LoadURL(text); break;
    case OS999B_BACK: browser_->GoBack(); break;
    case OS999B_FORWARD: browser_->GoForward(); break;
    case OS999B_RELOAD: browser_->Reload(); break;
    case OS999B_STOP: browser_->StopLoad(); break;
    case OS999B_FOCUS: host->SetFocus(focused_ && visible_); break;
    case OS999B_APP_INSTALL:
    case OS999B_APP_REMOVE: {
      CefRefPtr<EmbedBrowser> self(this);
      auto operation = m.kind == OS999B_APP_INSTALL ? WebApps::Install : WebApps::Remove;
      operation(browser_, [self](bool ok, const std::string& message) {
        if (self->host_) self->host_->Send(OS999B_APP_RESULT, self->id_, ok, 0, message);
      });
      break;
    }
    case OS999B_EXIT_FULLSCREEN: host->ExitFullscreen(true); break;
    default: break;
  }
}

void EmbedBrowser::Key(const Os999BrowserMsg& m) {
  bool down = m.kind == OS999B_KEY_DOWN;
  if (down && (m.modifiers & EVENTFLAG_COMMAND_DOWN)) {  // macOS edit shortcuts
    auto f = browser_->GetFocusedFrame();
    if (f) {
      switch (m.uch) {
        case 'c': f->Copy(); return;
        case 'v': f->Paste(); return;
        case 'x': f->Cut(); return;
        case 'a': f->SelectAll(); return;
        case 'z': (m.modifiers & EVENTFLAG_SHIFT_DOWN) ? f->Redo() : f->Undo(); return;
      }
    }
  }
  CefKeyEvent k;
  k.windows_key_code = m.x;  // Windows VKEY computed by the desktop
#if defined(OS_MAC)
  k.native_key_code = (int)m.keycode;
  k.type = down ? KEYEVENT_KEYDOWN : KEYEVENT_KEYUP;
#else
  k.native_key_code = 0;
  k.type = down ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
#endif
  k.character = (char16_t)m.ch;
  k.unmodified_character = (char16_t)m.uch;
  k.modifiers = m.modifiers;
  browser_->GetHost()->SendKeyEvent(k);
  // Return and Tab act through a char event (form submit, focus move).
  if (down && (m.ch == '\r' || m.ch == '\t')) {
    k.type = KEYEVENT_CHAR;
    browser_->GetHost()->SendKeyEvent(k);
  }
}

void EmbedBrowser::Text(const std::string& utf8) {
  CefString s(utf8);
  std::u16string u = s.ToString16();
  if (composing_ || u.size() > 1) {  // IME result or multi-unit text
    browser_->GetHost()->ImeCommitText(s, CefRange::InvalidRange(), 0);
    composing_ = false;
    return;
  }
  for (char16_t c : u) {
    CefKeyEvent k;
    k.type = KEYEVENT_CHAR;
    k.character = k.unmodified_character = c;
    browser_->GetHost()->SendKeyEvent(k);
  }
}

void EmbedBrowser::ImeSet(const std::string& utf8, int cursor) {
  CefString s(utf8);
  size_t len = s.ToString16().size();
  if (len == 0) {
    if (composing_) browser_->GetHost()->ImeCancelComposition();
    composing_ = false;
    return;
  }
  CefCompositionUnderline line;
  line.range = CefRange(0, (uint32_t)len);
  line.color = 0xFF000000;
  line.background_color = 0;
  line.thick = 0;
  line.style = CEF_CUS_SOLID;
  std::vector<CefCompositionUnderline> lines{line};
  uint32_t c = cursor < 0 || (size_t)cursor > len ? (uint32_t)len : (uint32_t)cursor;
  browser_->GetHost()->ImeSetComposition(s, lines, CefRange::InvalidRange(), CefRange(c, c));
  composing_ = true;
}

// ---- Rendering --------------------------------------------------------------

void EmbedBrowser::GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) {
  rect = CefRect(0, 0, width_, height_);
}

bool EmbedBrowser::GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) {
  info.device_scale_factor = scale_;
  info.rect = info.available_rect = CefRect(0, 0, width_, height_);
  return true;
}

void EmbedBrowser::OnPopupShow(CefRefPtr<CefBrowser>, bool show) {
  if (!show) popup_ = CefRect();
  sink_->Popup(show, CefRect((int)(popup_.x * scale_), (int)(popup_.y * scale_),
                             (int)(popup_.width * scale_), (int)(popup_.height * scale_)));
}

void EmbedBrowser::OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) {
  popup_ = rect;
  sink_->Popup(true, CefRect((int)(rect.x * scale_), (int)(rect.y * scale_),
                             (int)(rect.width * scale_), (int)(rect.height * scale_)));
}

void EmbedBrowser::OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&,
                           const void* buffer, int width, int height) {
  sink_->Software(type == PET_POPUP, buffer, width, height);
  if (type == PET_VIEW) BrowserStats::Get().OnFrame(width, height, sink_->LastCopyMs());
}

void EmbedBrowser::OnAcceleratedPaint(CefRefPtr<CefBrowser>, PaintElementType type,
                                      const RectList&, const CefAcceleratedPaintInfo& info) {
  if (!painted_++) BrowserStats::Get().Log("tab " + std::to_string(id_) + " first frame");
  sink_->Accelerated(type == PET_POPUP, info);
  if (type == PET_VIEW)
    BrowserStats::Get().OnFrame(info.extra.visible_rect.width,
                                info.extra.visible_rect.height, sink_->LastCopyMs());
}

void EmbedBrowser::OnImeCompositionRangeChanged(CefRefPtr<CefBrowser>, const CefRange&,
                                                const RectList& bounds) {
  if (!host_ || bounds.empty()) return;
  const CefRect& r = bounds.back();
  host_->Send(OS999B_IME_RECT, id_, r.width, r.height, "", r.x, r.y);
}

// ---- Life span, display, load ------------------------------------------------

bool EmbedBrowser::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
                                 const CefString& url, const CefString&,
                                 cef_window_open_disposition_t d, bool, const CefPopupFeatures&,
                                 CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
                                 CefRefPtr<CefDictionaryValue>&, bool*) {
  // The desktop owns windows and tabs: it decides where the page opens.
  bool window = (browser_ && browser_->GetMainFrame()->GetURL() == WebApps::Home()) || d == CEF_WOD_NEW_WINDOW || d == CEF_WOD_NEW_POPUP;
  if (host_ && !url.empty()) host_->Send(OS999B_OPEN_URL, id_, window ? 1 : 0, 0, url.ToString());
  return true;
}

void EmbedBrowser::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  BrowserStats::Get().Log("tab " + std::to_string(id_) + " created " + std::to_string(width_) +
                          "x" + std::to_string(height_) + " visible " + std::to_string(visible_));
  browser_->GetHost()->WasHidden(!visible_);
  browser_->GetHost()->SetFocus(focused_ && visible_);
  browser_->GetHost()->Invalidate(PET_VIEW);
}

void EmbedBrowser::OnBeforeClose(CefRefPtr<CefBrowser>) {
  browser_ = nullptr;
  if (host_) host_->OnBrowserClosed(id_);
}

void EmbedBrowser::OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                                   const CefString& url) {
  if (host_ && frame->IsMain()) host_->Send(OS999B_ADDRESS, id_, 0, 0, url.ToString());
}

void EmbedBrowser::OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) {
  title_ = title.ToString();
  if (host_) host_->Send(OS999B_TITLE, id_, 0, 0, title.ToString());
}

bool EmbedBrowser::OnCursorChange(CefRefPtr<CefBrowser>, CefCursorHandle,
                                  cef_cursor_type_t type, const CefCursorInfo&) {
  if (host_) host_->Send(OS999B_CURSOR, id_, (int)type, 0, "");
  return true;
}

bool EmbedBrowser::OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t,
                                    const CefString& message, const CefString&, int) {
  std::string s = message.ToString();
  if (host_ && browser_ && browser_->GetMainFrame()->GetURL() == WebApps::Play() &&
      s.rfind("OS999_CAPABILITIES ", 0) == 0 && s.size() < OS999B_TEXT)
    host_->Send(OS999B_CAPABILITIES, id_, 0, 0, s.substr(19));
  if (s.rfind("OS999VIDEO ", 0) == 0) BrowserStats::Get().Log("video " + s.substr(11));
  return false;
}

void EmbedBrowser::OnFullscreenModeChange(CefRefPtr<CefBrowser>, bool fullscreen) {
  if (host_) host_->Send(OS999B_FULLSCREEN, id_, fullscreen ? 1 : 0, 0, "");
}

void EmbedBrowser::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool loading, bool back,
                                        bool forward) {
  if (!security_denied_) Security(BrowserVerified(browser, loading));
  BrowserStats::Get().Log("tab " + std::to_string(id_) + " loading " + std::to_string(loading));
  if (host_) host_->Send(OS999B_LOADING, id_, loading, (back ? 1 : 0) | (forward ? 2 : 0), "");
}

// ---- Downloads: only into the OS999 volume's Downloads folder (B04) ----------

static std::string SafeName(const std::string& in) {
  std::string out;
  for (char c : in) out += (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 32) ? '_' : c;
  while (!out.empty() && out[0] == '.') out.erase(0, 1);
  return out.empty() ? "download" : out;
}

bool EmbedBrowser::OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>,
                                    const CefString& suggested_name,
                                    CefRefPtr<CefBeforeDownloadCallback> callback) {
  if (!host_ || host_->download_dir().empty()) return false;  // cancels
  std::string name = SafeName(suggested_name.ToString());
  std::string dir = host_->download_dir(), path = dir + "/" + name;
  struct stat st;
  for (int i = 2; stat(path.c_str(), &st) == 0 && i < 1000; i++) {
    size_t dot = name.rfind('.');
    std::string base = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    std::string ext = dot == std::string::npos || dot == 0 ? "" : name.substr(dot);
    path = dir + "/" + base + " " + std::to_string(i) + ext;
  }
  callback->Continue(path, false);
  return true;
}

void EmbedBrowser::OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
                                     CefRefPtr<CefDownloadItemCallback>) {
  if (!host_) return;
  std::string path = item->GetFullPath().ToString();
  std::string name = path.empty() ? item->GetSuggestedFileName().ToString()
                                  : path.substr(path.rfind('/') + 1);
  int state = item->IsComplete() ? 1 : (item->IsCanceled() || item->IsInterrupted()) ? 2 : 0;
  host_->Send(OS999B_DOWNLOAD, id_, item->GetPercentComplete(), state, name);
}

void EmbedBrowser::OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                               const CefString& text, const CefString& url) {
  if (frame->IsMain() && !security_denied_) Security(false, "Page load failed");
  if (frame->IsMain() && code != ERR_ABORTED) {
    load_error_ = code;
    if (load_diag_ && BrowserStats::Get().logging())
      BrowserStats::Get().Log("load error tab " + std::to_string(id_) + " net_error=" +
                              std::to_string(code) + " url=" +
                              load_diag::Sanitize(url.ToString(), 300));
  }
  BrowserStats::Get().Log("tab " + std::to_string(id_) + " load error " + std::to_string(code));
}

void EmbedBrowser::OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, TransitionType) {
  if (frame->IsMain()) {
    BrowserStats::Get().Log("tab " + std::to_string(id_) + " load start");
    load_error_ = 0;
  }
}

// ---- Page-load diagnostics (opt-in, OS999_LOAD_DIAG=1) ----------------------

void EmbedBrowser::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                             int http_status) {
  if (frame->IsMain() && load_diag_ && BrowserStats::Get().logging())
    LogLoad(browser, http_status, load_error_);
}

void EmbedBrowser::LogLoad(CefRefPtr<CefBrowser> browser, int http_status, int net_error) {
  load_diag::Load l;
  l.seq = tracker_.seq();
  l.requested = tracker_.requested();
  l.final_url = browser->GetMainFrame()->GetURL().ToString();
  l.title = title_;
  l.http_status = http_status;
  l.net_error = net_error;
  l.cert_errors = tracker_.cert_errors();
  l.load_ms = NowMs() - tracker_.start_ms();
  auto entry = browser->GetHost()->GetVisibleNavigationEntry();
  auto ssl = entry ? entry->GetSSLStatus() : nullptr;
  l.cert_status = ssl ? (unsigned)ssl->GetCertStatus() : 0;
  l.tls_verified = BrowserVerified(browser, false);
  BrowserStats::Get().Log(load_diag::FormatLoad(l));
  // Sub-resources keep arriving after the load event; summarise again later.
  int seq = l.seq;
  CefRefPtr<EmbedBrowser> self(this);
  CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<EmbedBrowser> b, int s) {
    if (b->tracker_.seq() == s)
      BrowserStats::Get().Log(load_diag::FormatResources(s, b->tracker_.Snapshot(), 10.0));
  }, self, seq), 10000);
#ifdef OS999_ENABLE_TEST_HOOKS
  // Test hook: after the first good load, navigate once to this https URL.
  const char* next = getenv("OS999_LOAD_DIAG_NEXT");
  if (!next_done_ && http_status == 200 && load_diag::ValidNextUrl(next)) {
    next_done_ = true;
    std::string url = next;
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<EmbedBrowser> b, std::string u) {
      if (b->browser_) b->browser_->GetMainFrame()->LoadURL(u);
    }, self, url), 4000);
  }
#endif
}

void EmbedBrowser::OnResourceLoadComplete(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                                          CefRefPtr<CefRequest> request,
                                          CefRefPtr<CefResponse> response,
                                          URLRequestStatus status, int64_t received) {
  if (!load_diag_) return;
  load_diag::Result r = status == UR_SUCCESS ? load_diag::Result::kOk
                        : status == UR_CANCELED ? load_diag::Result::kCanceled
                                                : load_diag::Result::kFailed;
  tracker_.AddResource(r, response ? response->GetStatus() : 0, received);
  // Name the first few failures by origin only (no path or query).
  auto res = tracker_.Snapshot();
  if (r != load_diag::Result::kOk && res.failed + res.canceled <= 25 && request)
    BrowserStats::Get().Log(std::string("resource ") +
        (r == load_diag::Result::kFailed ? "failed " : "canceled ") + "load #" +
        std::to_string(tracker_.seq()) + " " + load_diag::Origin(request->GetURL().ToString()));
}

bool EmbedBrowser::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, bool, bool is_redirect) {
  const auto url = request->GetURL().ToString();
  if (load_diag_ && frame->IsMain() && !is_redirect) {
    if (tracker_.seq() > 0 && BrowserStats::Get().logging())  // final counts of the previous load
      BrowserStats::Get().Log(load_diag::FormatResources(
          tracker_.seq(), tracker_.Snapshot(), (NowMs() - tracker_.start_ms()) / 1000.0) +
          " (at next navigation)");
    tracker_.Begin(url, NowMs());
  }
  auto& guard = VersionGuard::Get();
  if (frame->IsMain()) { security_denied_ = false; Security(false); }
  if (guard.ShouldBlock(browser, url)) {
    if (frame->IsMain()) frame->LoadURL(guard.BlockPageURL());
    return true;
  }
  if (BrowserNavigationAllowed(url, guard.BlockPageURL())) return false;
  if (frame->IsMain()) { security_denied_ = true; Security(false, "Blocked: HTTPS required"); }
  return true;
}

CefResourceRequestHandler::ReturnValue EmbedBrowser::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback>) {
  return BrowserResourceAllowed(request->GetURL()) ? RV_CONTINUE : RV_CANCEL;
}
bool EmbedBrowser::OnCertificateError(CefRefPtr<CefBrowser>, cef_errorcode_t,
    const CefString&, CefRefPtr<CefSSLInfo>, CefRefPtr<CefCallback>) {
  if (load_diag_) tracker_.AddCertError();
  security_denied_ = true; Security(false, "Blocked: certificate verification failed");
  return false;  // Never continue or store a certificate exception.
}

void EmbedBrowser::Security(bool verified, const std::string& message) {
  if (host_) host_->Send(OS999B_TLS, id_, verified ? 1 : 0, security_denied_ ? 1 : 0, message);
}
