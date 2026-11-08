// OS999 Browser: CEF client for one windowless (off-screen) browser.
// Frames arrive as GPU IOSurfaces via OnAcceleratedPaint; no CPU pixel path.
#ifndef OS999_BROWSER_CLIENT_H_
#define OS999_BROWSER_CLIENT_H_

#include <string>

#include "include/cef_client.h"

// Implemented by the Cocoa view that owns the browser.
class BrowserDelegate {
 public:
  virtual ~BrowserDelegate() = default;
  virtual void GetViewInfo(int* width, int* height, float* scale) = 0;
  virtual void ViewToScreen(int x, int y, int* sx, int* sy) = 0;
  virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
  virtual void OnBrowserClosed() = 0;
  virtual void OnGpuFrame(const CefAcceleratedPaintInfo& info, bool popup) = 0;
  virtual void OnPopup(bool show, const CefRect& rect) = 0;
  virtual void OnSecurity(bool secure) = 0;
  virtual void OnSecurityError(const std::string& text) = 0;
  virtual void OnAddress(const std::string& url) = 0;
  virtual void OnTitle(const std::string& title) = 0;
  virtual void OnLoading(bool loading, bool back, bool forward) = 0;
  virtual void OnCursor(CefCursorHandle cursor, int type) = 0;
  virtual void OnConsole(const std::string& message) = 0;
  virtual void OnFullscreen(bool fullscreen) = 0;
};

class BrowserClient : public CefClient,
                      public CefRenderHandler,
                      public CefLifeSpanHandler,
                      public CefDisplayHandler,
                      public CefLoadHandler,
                      public CefRequestHandler,
                     public CefResourceRequestHandler {
 public:
  explicit BrowserClient(BrowserDelegate* delegate) : delegate_(delegate) {}
  void Detach() { delegate_ = nullptr; }

  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool, bool,
      const CefString&, bool&) override { return this; }
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      CefRefPtr<CefRequest>, CefRefPtr<CefCallback>) override;
  bool OnCertificateError(CefRefPtr<CefBrowser>, cef_errorcode_t, const CefString&,
      CefRefPtr<CefSSLInfo>, CefRefPtr<CefCallback>) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool, bool) override;

  // CefRenderHandler
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  bool GetScreenPoint(CefRefPtr<CefBrowser> browser, int viewX, int viewY,
                      int& screenX, int& screenY) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override;
  void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
               const RectList& dirtyRects, const void* buffer, int width,
               int height) override;
  void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                          const RectList& dirtyRects,
                          const CefAcceleratedPaintInfo& info) override;

  // CefLifeSpanHandler
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     int popup_id, const CefString& target_url,
                     const CefString& target_frame_name,
                     cef_window_open_disposition_t target_disposition,
                     bool user_gesture, const CefPopupFeatures& popupFeatures,
                     CefWindowInfo& windowInfo, CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefDisplayHandler
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  bool OnCursorChange(CefRefPtr<CefBrowser> browser, CefCursorHandle cursor,
                      cef_cursor_type_t type,
                      const CefCursorInfo& custom_cursor_info) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                        const CefString& message, const CefString& source,
                        int line) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                              bool fullscreen) override;

  // CefLoadHandler
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading,
                            bool canGoBack, bool canGoForward) override;

  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, ErrorCode,
                   const CefString&, const CefString&) override;
 private:
  bool security_denied_ = false;
  BrowserDelegate* delegate_;
  CefRect popup_rect_;
  IMPLEMENT_REFCOUNTING(BrowserClient);
};

#endif
