#include "browser_stats.h"

#include <chrono>
#include <ctime>

static double NowSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

BrowserStats& BrowserStats::Get() {
  static BrowserStats stats;
  return stats;
}

void BrowserStats::SetLogPath(const std::string& path) {
  if (log_) fclose(log_);
  log_ = path.empty() ? nullptr : fopen(path.c_str(), "a");
}

void BrowserStats::OnFrame(int w, int h, double copy_ms) {
  if (window_start_ == 0) window_start_ = NowSeconds();
  frames_++;
  window_frames_++;
  copy_total_ += copy_ms;
  width_ = w;
  height_ = h;
}

bool BrowserStats::Sample(double* fps, double* avg_copy_ms, int* w, int* h) {
  double now = NowSeconds();
  if (window_start_ == 0) window_start_ = now;
  double span = now - window_start_;
  if (span < 1.0) return false;
  *fps = window_frames_ / span;
  *avg_copy_ms = window_frames_ ? copy_total_ / window_frames_ : 0;
  *w = width_;
  *h = height_;
  window_frames_ = 0;
  copy_total_ = 0;
  window_start_ = now;
  return true;
}

void BrowserStats::Log(const std::string& line) {
  if (!log_) return;
  char stamp[32];
  time_t t = time(nullptr);
  strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", localtime(&t));
  fprintf(log_, "%s %s\n", stamp, line.c_str());
  fflush(log_);
}
