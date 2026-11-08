// OS999 Browser embed mode: per-tab frame delivery to the OS999 desktop.
// macOS: GPU compose (view + popup) into owned IOSurfaces, sent as Mach ports.
// Linux: CPU compose into the desktop's shared-memory surface.
#ifndef OS999_EMBED_FRAMES_H_
#define OS999_EMBED_FRAMES_H_

#include <memory>
#include <string>

#include "include/cef_render_handler.h"

class FrameSink {
 public:
  static void Init(const std::string& mach_port_name);
  static std::unique_ptr<FrameSink> Create(int id);
  virtual ~FrameSink() = default;
  virtual void Accelerated(bool popup, const CefAcceleratedPaintInfo& info) = 0;
  virtual void Software(bool popup, const void* bgra, int width, int height) = 0;
  // |rect_px| is the popup widget rect in view pixels; hiding recomposes.
  virtual void Popup(bool show, const CefRect& rect_px) = 0;
  virtual void Target(const std::string& name) = 0;
  virtual double LastCopyMs() const = 0;
  // Parent handle for CefWindowInfo::SetAsWindowless (macOS: an off-screen
  // NSView owned by the sink; Linux: none).
  virtual CefWindowHandle ParentHandle() { return kNullWindowHandle; }
};

#endif
