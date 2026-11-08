#ifndef OS999_WEB_APPS_H
#define OS999_WEB_APPS_H
#include <functional>
#include <string>
#include "include/cef_browser.h"
namespace WebApps {
const char* Home();
const char* Play();
bool Internal(const std::string& url);
void Register();
void Remove(CefRefPtr<CefBrowser> browser, std::function<void(bool, const std::string&)> result);
void Install(CefRefPtr<CefBrowser> browser, std::function<void(bool, const std::string&)> result);
}
#endif
