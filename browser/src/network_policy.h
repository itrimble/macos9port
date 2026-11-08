#ifndef OS999_NETWORK_POLICY_H
#define OS999_NETWORK_POLICY_H
#include "include/cef_browser.h"
#include "include/cef_parser.h"

// Shared by the standalone and embedded clients. Internal version-block pages
// are allowed only by exact match at the caller, never arbitrary data URLs.
inline bool BrowserHttps(const CefString& url) {
  CefURLParts p;
  return CefParseURL(url, p) && CefString(&p.scheme) == "https" &&
         CefString(&p.username).empty() && CefString(&p.password).empty();
}
inline bool BrowserNavigationAllowed(const std::string& url, const std::string& block_page) {
  return BrowserHttps(url) || url == "about:blank" || url == block_page ||
         url == "chrome://gpu/" || url == "chrome://media-internals/";
}
inline bool BrowserResourceAllowed(const CefString& url) {
  CefURLParts p;
  if (!CefParseURL(url, p)) return false;
  auto scheme = CefString(&p.scheme).ToString();
  if (!CefString(&p.username).empty() || !CefString(&p.password).empty()) return false;
  return scheme != "http" && scheme != "ftp" && scheme != "file" && scheme != "ws";
}
inline bool BrowserVerified(CefRefPtr<CefBrowser> browser, bool loading) {
  auto entry = browser->GetHost()->GetVisibleNavigationEntry();
  auto ssl = entry ? entry->GetSSLStatus() : nullptr;
  return !loading && entry && BrowserHttps(entry->GetURL()) && ssl &&
         ssl->IsSecureConnection() && ssl->GetCertStatus() == CERT_STATUS_NONE &&
         ssl->GetContentStatus() == SSL_CONTENT_NORMAL_CONTENT;
}
#endif
