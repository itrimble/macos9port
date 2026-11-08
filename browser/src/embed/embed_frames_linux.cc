#include "embed_frames.h"

#include <chrono>
#include <cstring>
#include <vector>

extern "C" {
#include "runtime_bridge.h"
}

// Linux/Raspberry Pi: CPU frames composed here and published into the
// desktop's shared-memory surface (the same bridge the classic runtime uses).
// GPU zero-copy (dmabuf import) is not implemented yet; see browser/README.md.
namespace {

class LinuxSink : public FrameSink {
 public:
  explicit LinuxSink(int) {}
  ~LinuxSink() override {
    if (bridge_) runtime_bridge_close(bridge_);
  }

  void Accelerated(bool, const CefAcceleratedPaintInfo&) override {}

  void Software(bool popup, const void* bgra, int w, int h) override {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint32_t>& dst = popup ? popup_ : view_;
    dst.assign((const uint32_t*)bgra, (const uint32_t*)bgra + (size_t)w * h);
    if (popup) {
      popup_w_ = w;
      popup_h_ = h;
    } else {
      view_w_ = w;
      view_h_ = h;
    }
    Publish();
    copy_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                   .count();
  }

  void Popup(bool show, const CefRect& r) override {
    popup_shown_ = show && r.width > 0;
    rect_ = r;
    Publish();
  }

  void Target(const std::string& name) override {
    if (bridge_) runtime_bridge_close(bridge_);
    bridge_ = name.empty() ? nullptr : runtime_bridge_open(name.c_str());
    Publish();
  }

  double LastCopyMs() const override { return copy_ms_; }

 private:
  void Publish() {
    if (!bridge_ || view_.empty()) return;
    out_ = view_;
    if (popup_shown_ && !popup_.empty()) {
      for (int y = 0; y < popup_h_; y++) {
        int ty = rect_.y + y;
        if (ty < 0 || ty >= view_h_) continue;
        for (int x = 0; x < popup_w_; x++) {
          int tx = rect_.x + x;
          if (tx >= 0 && tx < view_w_) out_[(size_t)ty * view_w_ + tx] = popup_[(size_t)y * popup_w_ + x];
        }
      }
    }
    // CEF BGRA bytes are 0xAARRGGBB words on little-endian: OS999's format.
    runtime_bridge_publish(bridge_, out_.data(), view_w_, view_h_);
  }

  RuntimeBridge* bridge_ = nullptr;
  std::vector<uint32_t> view_, popup_, out_;
  int view_w_ = 0, view_h_ = 0, popup_w_ = 0, popup_h_ = 0;
  bool popup_shown_ = false;
  CefRect rect_;
  double copy_ms_ = 0;
};

}  // namespace

void FrameSink::Init(const std::string&) {}

std::unique_ptr<FrameSink> FrameSink::Create(int id) {
  return std::make_unique<LinuxSink>(id);
}
