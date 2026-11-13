// OS999 Browser: opt-in video decoder diagnostics (checklist B02 / demo B).
// Pure C++17 with no CEF dependency so it can be unit tested headlessly.
// Enabled only by the environment variable OS999_DECODE_DIAG=1; off otherwise.
#ifndef OS999_DECODE_DIAG_H_
#define OS999_DECODE_DIAG_H_

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace decode_diag {

// True when OS999_DECODE_DIAG is set to "1" in the environment.
bool Enabled();

// One video player as reported by chrome://media-internals.
struct PlayerReport {
  std::string decoder_name;    // kVideoDecoderName, e.g. "VideoToolboxVideoDecoder"
  int platform = -1;           // kIsPlatformVideoDecoder: 1 true, 0 false, -1 absent
  std::string codec;           // from "codec: vp9" in the selection line
  int width = 0, height = 0;   // coded size
  std::string selection_line;  // the raw "Selected ..." text, if present
  bool hardware() const;       // platform decoder AND a known hardware decoder name
};

// Parse the text the media-internals scraper logs (players separated by " ||| ").
// Players with no kVideoDecoderName (audio-only, or never decoded) are skipped.
std::vector<PlayerReport> ParseMediaInternals(const std::string& text);

// True for decoder names that Chromium backs with an OS/GPU decoder.
bool IsHardwareDecoderName(const std::string& name);

// chrome://gpu "Graphics Feature Status" rows as (feature, status) pairs.
std::vector<std::pair<std::string, std::string>> ParseGpuFeatureStatus(const std::string& text);

// Lines of the "Video Acceleration Information" section (decode/encode profiles).
std::vector<std::string> ParseVideoAcceleration(const std::string& text);

// Filter a Chromium log for decoder-selection lines (VideoToolbox, "Selected ...Decoder").
bool IsDecoderSelectionLine(const std::string& line);
std::vector<std::string> ExtractDecoderLines(const std::string& log_text, size_t max_lines = 200);

// "verified: ..." only when a hardware PlayerReport has a video size; otherwise
// "not verified: <reason>". Never inferred from frame rate or compositing.
std::string Verdict(const std::vector<PlayerReport>& players);

// The path must be absolute, have no ".." component, and lie inside root.
bool PathInsideRoot(const std::string& path, const std::string& root);

// Append-only log, created 0600 in a 0700 directory. Newlines in text become " | ".
class Log {
 public:
  ~Log();
  // Returns false (and stays closed) if path is outside root or cannot be opened.
  bool Open(const std::string& path, const std::string& root);
  bool is_open() const { return file_ != nullptr; }
  void Append(const std::string& tag, const std::string& text);

 private:
  std::FILE* file_ = nullptr;
};

}  // namespace decode_diag
#endif
