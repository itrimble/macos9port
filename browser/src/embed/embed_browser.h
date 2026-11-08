// OS999 Browser embed mode: one tab (CEF windowless browser) driven entirely
// by messages from the OS999 desktop.
#ifndef OS999_EMBED_BROWSER_H_
#define OS999_EMBED_BROWSER_H_

#include <memory>
#include <string>

#include "embed_frames.h"
#include "include/cef_client.h"
#include "os999_browser_ipc.h"

class EmbedHost;

class EmbedBrowser : public CefClient,
                     public CefRenderHandler,
                     public CefLifeSpanHandler,
                     public CefDisplayHandler,
                     public CefLoadHandler,
                     public CefDownloadHandler,
                     public CefRequestHandler,
                     public CefFocusHandler,
                     public CefResourceRequestHandler {
 public:
  EmbedBrowser(EmbedHost* host, int id, int width, int height, float scale);
  void Create(const std::string& url);
  void Handle(const Os999BrowserMsg& m);  // input, navigation, resize, ...
  void Close();
  void Detach() { host_ = nullptr; }
  CefRefPtr<CefBrowser> browser() const { return browser_; }
  bool visible() const { return visible_; }
  int id() const { return id_; }

  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }
  bool OnSetFocus(CefRefPtr<CefBrowser>, FocusSource) override { return !focused_ || !visible_; }
  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
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

  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override;
  void OnPopupShow(CefRefPtr<CefBrowser>, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&,
               const void* buffer, int width, int height) override;
  void OnAcceleratedPaint(CefRefPtr<CefBrowser>, PaintElementType type,
                          const RectList&, const CefAcceleratedPaintInfo& info) override;
  void OnImeCompositionRangeChanged(CefRefPtr<CefBrowser>, const CefRange&,
                                    const RectList& bounds) override;

  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
                     const CefString& target_url, const CefString&,
                     cef_window_open_disposition_t disposition, bool,
                     const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
                     CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&,
                     bool*) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) override;
  bool OnCursorChange(CefRefPtr<CefBrowser>, CefCursorHandle, cef_cursor_type_t type,
                      const CefCursorInfo&) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t,
                        const CefString& message, const CefString&, int) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser>, bool fullscreen) override;

  void OnLoadingStateChange(CefRefPtr<CefBrowser>, bool loading, bool back,
                            bool forward) override;
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code,
                   const CefString& text, const CefString& url) override;
  void OnLoadStart(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                   TransitionType) override;

  bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
                         CefRefPtr<CefDownloadItemCallback>) override;

 private:
  void Security(bool verified, const std::string& message = "");
  bool security_denied_ = false;
  void Key(const Os999BrowserMsg& m);
  void Text(const std::string& utf8);
  void ImeSet(const std::string& utf8, int cursor);

  EmbedHost* host_;
  int id_, width_, height_;
  float scale_;
  bool visible_ = true, composing_ = false, focused_ = false;
  int painted_ = 0;
  CefRect popup_;
  CefRefPtr<CefBrowser> browser_;
  std::unique_ptr<FrameSink> sink_;
  IMPLEMENT_REFCOUNTING(EmbedBrowser);
};

#endif
