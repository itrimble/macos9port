// OS999 Browser embed mode: owns the desktop socket and every tab.
#ifndef OS999_EMBED_HOST_H_
#define OS999_EMBED_HOST_H_

#include <map>
#include <string>
#include <thread>

#include "embed_browser.h"
#include "os999_browser_ipc.h"

class EmbedHost {
 public:
  EmbedHost(int fd, std::string download_dir);
  void Start();  // UI thread
  // UI thread only.
  void Send(uint32_t kind, int id, int a, int b, const std::string& text, int x = 0, int y = 0);
  void OnBrowserClosed(int id);
  const std::string& download_dir() const { return download_dir_; }

 private:
  void ReadLoop();
  void Handle(Os999BrowserMsg m);
  void Tick();
  void Shutdown();

  int fd_;
  std::string download_dir_;
  std::thread reader_;
  std::map<int, CefRefPtr<EmbedBrowser>> browsers_;
  bool shutting_down_ = false;
};

#endif
