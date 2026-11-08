#include "browser_client.h"

#include "include/wrapper/cef_helpers.h"
#include "browser_stats.h"
#include "version_guard.h"
#include "network_policy.h"

void BrowserClient::GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) {
  int w = 800, h = 600;
  float scale = 1;
  if (delegate_) delegate_->GetViewInfo(&w, &h, &scale);
  rect = CefRect(0, 0, w > 0 ? w : 1, h > 0 ? h : 1);
  static int logged;
  if (logged++ < 3)
    BrowserStats::Get().Log("view rect " + std::to_string(rect.width) + "x" +
                            std::to_string(rect.height) + " scale " + std::to_string(scale));
}

bool BrowserClient::GetScreenPoint(CefRefPtr<CefBrowser>, int viewX, int viewY,
                                   int& screenX, int& screenY) {
  if (!delegate_) return false;
  delegate_->ViewToScreen(viewX, viewY, &screenX, &screenY);
  return true;
}

bool BrowserClient::GetScreenInfo(CefRefPtr<CefBrowser> browser,
                                  CefScreenInfo& info) {
  if (!delegate_) return false;
  int w = 0, h = 0;
  float scale = 1;
  delegate_->GetViewInfo(&w, &h, &scale);
  info.device_scale_factor = scale;
  info.rect = CefRect(0, 0, w, h);
  info.available_rect = info.rect;
  return true;
}

void BrowserClient::OnPopupShow(CefRefPtr<CefBrowser>, bool show) {
  if (!show) popup_rect_ = CefRect();
  if (delegate_) delegate_->OnPopup(show, popup_rect_);
}

void BrowserClient::OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) {
  popup_rect_ = rect;
  if (delegate_) delegate_->OnPopup(true, rect);
}

void BrowserClient::OnPaint(CefRefPtr<CefBrowser>, PaintElementType,
                            const RectList&, const void*, int, int) {
  // Software frames are deliberately ignored: shared_texture_enabled is set,
  // and a CPU fallback would hide a failed GPU path (checklist B09).
  static int reported;
  if (!reported++) BrowserStats::Get().Log("software paint received; GPU path unavailable");
}

void BrowserClient::OnAcceleratedPaint(CefRefPtr<CefBrowser>,
                                       PaintElementType type, const RectList&,
                                       const CefAcceleratedPaintInfo& info) {
  if (delegate_) delegate_->OnGpuFrame(info, type == PET_POPUP);
}

bool BrowserClient::OnBeforePopup(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, int,
    const CefString& target_url, const CefString&, cef_window_open_disposition_t, bool,
    const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
    CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) {
  // One window per browser for now: open popups in place.
  if (!target_url.empty()) browser->GetMainFrame()->LoadURL(target_url);
  return true;
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) delegate_->OnBrowserCreated(browser);
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser>) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) delegate_->OnBrowserClosed();
}

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser>,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  if (delegate_ && frame->IsMain()) delegate_->OnAddress(url.ToString());
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser>,
                                  const CefString& title) {
  if (delegate_) delegate_->OnTitle(title.ToString());
}

bool BrowserClient::OnCursorChange(CefRefPtr<CefBrowser>,
                                   CefCursorHandle cursor, cef_cursor_type_t type,
                                   const CefCursorInfo&) {
  if (delegate_) delegate_->OnCursor(cursor, type);
  return true;
}

bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t,
                                     const CefString& message, const CefString&,
                                     int) {
  if (delegate_) delegate_->OnConsole(message.ToString());
  return false;
}

void BrowserClient::OnFullscreenModeChange(CefRefPtr<CefBrowser>,
                                           bool fullscreen) {
  if (delegate_) delegate_->OnFullscreen(fullscreen);
}

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading,
                                         bool canGoBack, bool canGoForward) {
  if (delegate_) { delegate_->OnLoading(isLoading, canGoBack, canGoForward);
    delegate_->OnSecurity(!security_denied_ && BrowserVerified(browser, isLoading)); }
}

bool BrowserClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, bool, bool) {
  const auto url = request->GetURL().ToString();
  auto& guard = VersionGuard::Get();
  if (frame->IsMain() && delegate_) { security_denied_ = false; delegate_->OnSecurity(false); delegate_->OnSecurityError(""); }
  if (guard.ShouldBlock(browser, url)) {
    if (frame->IsMain()) frame->LoadURL(guard.BlockPageURL());
    return true;
  }
  if (BrowserNavigationAllowed(url, guard.BlockPageURL())) return false;
  if (frame->IsMain()) security_denied_ = true;
  if (frame->IsMain() && delegate_) delegate_->OnSecurityError("Blocked: HTTPS required");
  return true;
}

CefResourceRequestHandler::ReturnValue BrowserClient::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback>) {
  return BrowserResourceAllowed(request->GetURL()) ? RV_CONTINUE : RV_CANCEL;
}
bool BrowserClient::OnCertificateError(CefRefPtr<CefBrowser>, cef_errorcode_t,
    const CefString&, CefRefPtr<CefSSLInfo>, CefRefPtr<CefCallback>) {
  security_denied_ = true;
  if (delegate_) { delegate_->OnSecurity(false); delegate_->OnSecurityError("Blocked: certificate verification failed"); }
  return false;  // Never continue or store a certificate exception.
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
    ErrorCode, const CefString&, const CefString&) {
  if (frame->IsMain() && delegate_) delegate_->OnSecurity(false);
}
