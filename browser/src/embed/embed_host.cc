#include "embed_host.h"

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "browser_stats.h"
#include "version_guard.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

EmbedHost::EmbedHost(int fd, std::string download_dir)
    : fd_(fd), download_dir_(std::move(download_dir)) {}

void EmbedHost::Start() {
  CEF_REQUIRE_UI_THREAD();
  reader_ = std::thread(&EmbedHost::ReadLoop, this);
  reader_.detach();
  Send(OS999B_READY, 0, 0, 0, "");
  VersionGuard::Get().Start([this](const VersionResult& r) {
    Send(OS999B_SECURITY, 0, (int)r.verdict, r.block ? 1 : 0, r.message);
  });
  CefPostDelayedTask(TID_UI, base::BindOnce(&EmbedHost::Tick, base::Unretained(this)), 1000);
}

void EmbedHost::ReadLoop() {
  for (;;) {
    Os999BrowserMsg m;
    size_t got = 0;
    while (got < sizeof m) {
      ssize_t n = read(fd_, (char*)&m + got, sizeof m - got);
      if (n <= 0) {  // the desktop went away: close everything and exit
        CefPostTask(TID_UI, base::BindOnce(&EmbedHost::Shutdown, base::Unretained(this)));
        return;
      }
      got += (size_t)n;
    }
    m.text[OS999B_TEXT - 1] = 0;
    CefPostTask(TID_UI, base::BindOnce(&EmbedHost::Handle, base::Unretained(this), m));
  }
}

void EmbedHost::Send(uint32_t kind, int id, int a, int b, const std::string& text, int x,
                     int y) {
  Os999BrowserMsg m;
  memset(&m, 0, sizeof m);
  m.kind = kind;
  m.id = id;
  m.a = a;
  m.b = b;
  m.x = x;
  m.y = y;
  strncpy(m.text, text.c_str(), sizeof m.text - 1);
  const char* p = (const char*)&m;
  size_t left = sizeof m;
  while (left > 0) {
    ssize_t n = write(fd_, p, left);
    if (n <= 0) return;
    p += n;
    left -= (size_t)n;
  }
}

void EmbedHost::Handle(Os999BrowserMsg m) {
  if (shutting_down_) return;
  if (m.kind == OS999B_SHUTDOWN) {
    Shutdown();
    return;
  }
  if (m.id <= 0 || m.id >= OS999B_MAX_ID) return;
  auto it = browsers_.find(m.id);
  if (m.kind == OS999B_CREATE) {
    if (it != browsers_.end()) return;
    float scale = m.x > 0 ? m.x / 100.0f : 1.0f;
    BrowserStats::Get().Log("create tab " + std::to_string(m.id));
    CefRefPtr<EmbedBrowser> b = new EmbedBrowser(this, m.id, m.a, m.b, scale);
    browsers_[m.id] = b;
    b->Create(m.text[0] ? m.text : "about:blank");
    return;
  }
  if (it == browsers_.end()) return;
  if (m.kind == OS999B_DESTROY) {
    it->second->Close();
    return;
  }
  if (m.kind == OS999B_FOCUS && m.a) {
    auto blur = m; blur.a = 0;
    for (auto& entry : browsers_) if (entry.first != m.id) entry.second->Handle(blur);
  }
  it->second->Handle(m);
}

void EmbedHost::OnBrowserClosed(int id) {
  auto it = browsers_.find(id);
  if (it != browsers_.end()) {
    it->second->Detach();
    browsers_.erase(it);
  }
  Send(OS999B_CLOSED, id, 0, 0, "");
  if (shutting_down_ && browsers_.empty()) CefQuitMessageLoop();
}

void EmbedHost::Shutdown() {
  if (shutting_down_) return;
  shutting_down_ = true;
  if (browsers_.empty()) {
    CefQuitMessageLoop();
    return;
  }
  // Closing can finish synchronously and erase from browsers_: iterate a copy.
  std::vector<CefRefPtr<EmbedBrowser>> all;
  for (auto& kv : browsers_) all.push_back(kv.second);
  for (auto& b : all) b->Close();
  // Never outlive the desktop: give pages a moment, then leave regardless.
  CefPostDelayedTask(TID_UI, base::BindOnce(&CefQuitMessageLoop), 3000);
}

void EmbedHost::Tick() {
  if (shutting_down_) return;
  double fps, copy;
  int w, h;
  if (BrowserStats::Get().Sample(&fps, &copy, &w, &h)) {
    char line[160];
    snprintf(line, sizeof line, "GPU %.0f fps  %dx%d px  copy %.2f ms", fps, w, h, copy);
    Send(OS999B_STATUS, 0, 0, 0, line);
    BrowserStats::Get().Log(std::string("frame ") + line);
  }
#ifdef OS999_ENABLE_TEST_HOOKS
  if (BrowserStats::Get().logging()) {
    // Measurement only: the page's own <video> statistics, optional quality.
    const char* q = getenv("OSPI_BROWSER_QUALITY");
    std::string js = std::string(
        "(function(){var v=document.querySelector('video');if(!v)return;"
        "var p=document.getElementById('movie_player'),q='") + (q ? q : "") +
        "';if(q&&p&&p.setPlaybackQualityRange&&p.getPlaybackQuality()!=q)"
        "p.setPlaybackQualityRange(q,q);var s=v.getVideoPlaybackQuality(),"
        "n=p&&p.getStatsForNerds?p.getStatsForNerds():{};console.log('OS999VIDEO '+"
        "JSON.stringify({t:+v.currentTime.toFixed(1),paused:v.paused,muted:v.muted,"
        "w:v.videoWidth,h:v.videoHeight,dec:s.totalVideoFrames,drop:s.droppedVideoFrames,"
        "ql:p&&p.getPlaybackQuality?p.getPlaybackQuality():'',fmt:n.codecs||''}))})()";
    for (auto& kv : browsers_)
      if (kv.second->visible() && kv.second->browser())
        kv.second->browser()->GetMainFrame()->ExecuteJavaScript(js, "os999://stats", 0);
  }
#endif
  CefPostDelayedTask(TID_UI, base::BindOnce(&EmbedHost::Tick, base::Unretained(this)), 1000);
}
