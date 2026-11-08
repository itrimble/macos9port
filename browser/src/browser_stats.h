// OS999 Browser: frame counting and an optional measurement log.
#ifndef OS999_BROWSER_STATS_H_
#define OS999_BROWSER_STATS_H_

#include <cstdint>
#include <cstdio>
#include <string>

class BrowserStats {
 public:
  static BrowserStats& Get();
  void SetLogPath(const std::string& path);
  bool logging() const { return log_ != nullptr; }
  // Called for each GPU frame copied into the OS999 compositor.
  void OnFrame(int pixel_width, int pixel_height, double copy_ms);
  // Returns true about once per second with the measured compositor rate.
  bool Sample(double* fps, double* avg_copy_ms, int* w, int* h);
  void Log(const std::string& line);

 private:
  FILE* log_ = nullptr;
  uint64_t frames_ = 0, window_frames_ = 0;
  double window_start_ = 0, copy_total_ = 0;
  int width_ = 0, height_ = 0;
};

#endif
