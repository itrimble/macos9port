#include "decode_diag.h"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <ctime>

namespace decode_diag {
namespace {

bool IsSep(char c) {
  return std::isspace(static_cast<unsigned char>(c)) || c == '|' || c == ':' || c == '"' || c == ',' ||
         c == '=';
}

// The value after `key`: skips separators, then reads until the next separator.
std::string ValueAfter(const std::string& text, const std::string& key, size_t from = 0,
                       size_t* end = nullptr) {
  size_t p = text.find(key, from);
  if (p == std::string::npos) return "";
  p += key.size();
  while (p < text.size() && IsSep(text[p])) ++p;
  size_t e = p;
  while (e < text.size() && !IsSep(text[e])) ++e;
  if (end) *end = e;
  return text.substr(p, e - p);
}

std::vector<std::string> SplitPlayers(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t p = text.find(" ||| ", start);
    if (p == std::string::npos) { out.push_back(text.substr(start)); break; }
    out.push_back(text.substr(start, p - start));
    start = p + 5;
  }
  return out;
}

std::string Trim(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n|");
  size_t b = s.find_last_not_of(" \t\r\n|");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace

bool Enabled() {
  const char* v = getenv("OS999_DECODE_DIAG");
  return v && std::string(v) == "1";
}

bool IsHardwareDecoderName(const std::string& name) {
  static const char* kHw[] = {"VideoToolbox", "VTVideoDecode", "D3D11VideoDecoder", "D3D12VideoDecoder",
                              "VaapiVideoDecoder", "V4L2", "MediaCodec", "VdaVideoDecoder"};
  for (const char* h : kHw)
    if (name.find(h) != std::string::npos) return true;
  return false;
}

bool PlayerReport::hardware() const { return platform == 1 && IsHardwareDecoderName(decoder_name); }

std::vector<PlayerReport> ParseMediaInternals(const std::string& text) {
  std::vector<PlayerReport> out;
  for (const std::string& seg : SplitPlayers(text)) {
    PlayerReport r;
    r.decoder_name = ValueAfter(seg, "kVideoDecoderName");
    if (r.decoder_name.empty()) continue;
    std::string plat = ValueAfter(seg, "kIsPlatformVideoDecoder");
    r.platform = plat == "true" ? 1 : plat == "false" ? 0 : -1;
    size_t sel = seg.find("Selected " + r.decoder_name);
    std::string scope = seg;
    if (sel != std::string::npos) {
      scope = seg.substr(sel, 700);
      size_t cut = scope.find("Selected ", 9);
      if (cut != std::string::npos) scope.resize(cut);
      r.selection_line = Trim(scope);
    }
    r.codec = ValueAfter(scope, "codec");
    size_t cs = scope.find("coded size");
    if (cs == std::string::npos) cs = seg.find("coded size");
    if (cs != std::string::npos) {
      const std::string& src = scope.find("coded size") != std::string::npos ? scope : seg;
      size_t b = src.find('[', cs);
      if (b != std::string::npos) {
        r.width = atoi(src.c_str() + b + 1);
        size_t c = src.find(',', b);
        if (c != std::string::npos) r.height = atoi(src.c_str() + c + 1);
      }
    }
    out.push_back(r);
  }
  return out;
}

std::vector<std::pair<std::string, std::string>> ParseGpuFeatureStatus(const std::string& text) {
  static const char* kFeatures[] = {"Canvas", "Direct Rendering Display Compositor", "Compositing",
                                    "Multiple Raster Threads", "OpenGL", "Rasterization", "Raw Draw",
                                    "Skia Graphite", "Video Decode", "Video Encode", "Vulkan", "WebGL",
                                    "WebGL2", "WebGPU", "WebNN"};
  std::vector<std::pair<std::string, std::string>> out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? text.size() : nl + 1;
    while (!line.empty() && (line[0] == '*' || line[0] == '-' || line[0] == ' ' || line[0] == '\t'))
      line.erase(0, 1);  // chrome://gpu prints "* Canvas: Hardware accelerated"
    for (const char* f : kFeatures) {
      std::string name(f);
      if (line.compare(0, name.size(), name) != 0) continue;
      size_t p = name.size();
      if (p < line.size() && !IsSep(line[p])) continue;  // "WebGL2" must not match "WebGL"
      while (p < line.size() && (IsSep(line[p]) || line[p] == '\t')) ++p;
      std::string status = Trim(line.substr(p));
      if (!status.empty()) out.emplace_back(name, status);
      break;
    }
  }
  return out;
}

std::vector<std::string> ParseVideoAcceleration(const std::string& text) {
  std::vector<std::string> out;
  size_t h = text.find("Video Acceleration Information");
  if (h == std::string::npos) return out;
  size_t pos = text.find('\n', h);
  while (pos != std::string::npos && pos < text.size() && out.size() < 80) {
    size_t nl = text.find('\n', pos + 1);
    std::string line = Trim(text.substr(pos + 1, nl == std::string::npos ? std::string::npos : nl - pos - 1));
    pos = nl;
    if (line.empty()) continue;
    if (line == "Vulkan Information" || line == "Driver Information" || line == "Diagnostics" ||
        line == "Log Messages" || line == "Display(s) Information")
      break;
    out.push_back(line);
  }
  return out;
}

bool IsDecoderSelectionLine(const std::string& line) {
  if (line.find("VideoToolbox") != std::string::npos ||
      line.find("VTVideoDecode") != std::string::npos)
    return true;
  size_t s = line.find("Selected ");
  return s != std::string::npos && line.find("Decoder", s) != std::string::npos;
}

std::vector<std::string> ExtractDecoderLines(const std::string& log_text, size_t max_lines) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (pos < log_text.size() && out.size() < max_lines) {
    size_t nl = log_text.find('\n', pos);
    std::string line = log_text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? log_text.size() : nl + 1;
    if (IsDecoderSelectionLine(line)) out.push_back(line.substr(0, 600));
  }
  return out;
}

std::string Verdict(const std::vector<PlayerReport>& players) {
  if (players.empty()) return "not verified: no video decoder reported by media-internals";
  for (const PlayerReport& p : players) {
    if (p.hardware() && p.width > 0 && p.height > 0)
      return "verified: " + p.decoder_name + " (platform decoder) codec " +
             (p.codec.empty() ? "unknown" : p.codec) + " " + std::to_string(p.width) + "x" +
             std::to_string(p.height);
  }
  for (const PlayerReport& p : players)
    if (p.hardware()) return "not verified: hardware decoder named but no video size reported";
  return "not verified: decoder is " + players[0].decoder_name + " (platform=" +
         std::to_string(players[0].platform) + "), not a hardware decoder";
}

bool PathInsideRoot(const std::string& path, const std::string& root) {
  if (path.empty() || root.empty() || path[0] != '/' || root[0] != '/') return false;
  if (path.find("/../") != std::string::npos || path.find("/./") != std::string::npos) return false;
  if (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0) return false;
  std::string r = root;
  while (r.size() > 1 && r.back() == '/') r.pop_back();
  return path.size() > r.size() + 1 && path.compare(0, r.size(), r) == 0 && path[r.size()] == '/';
}

Log::~Log() { if (file_) std::fclose(file_); }

bool Log::Open(const std::string& path, const std::string& root) {
  if (file_) { std::fclose(file_); file_ = nullptr; }
  if (!PathInsideRoot(path, root)) return false;
  std::string dir = path.substr(0, path.rfind('/'));
  mkdir(root.c_str(), 0700);
  if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return false;
  file_ = std::fopen(path.c_str(), "a");
  if (file_) chmod(path.c_str(), 0600);
  return file_ != nullptr;
}

void Log::Append(const std::string& tag, const std::string& text) {
  if (!file_) return;
  char stamp[32];
  time_t t = time(nullptr);
  strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", localtime(&t));
  std::string body = text;
  for (size_t p; (p = body.find('\n')) != std::string::npos;) body.replace(p, 1, " | ");
  std::fprintf(file_, "%s %s %s\n", stamp, tag.c_str(), body.c_str());
  std::fflush(file_);
}

}  // namespace decode_diag
