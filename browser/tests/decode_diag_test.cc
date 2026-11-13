#include "decode_diag.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace decode_diag;

static std::string Slurp(const std::string& p) {
  std::ifstream f(p);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

int main() {
  // Media-internals text for a hardware VP9 player plus an audio-only player.
  std::string hw =
      "kVideoDecoderName | VideoToolboxVideoDecoder | kIsPlatformVideoDecoder | true | "
      "Selected VideoToolboxVideoDecoder for video decoding, config: codec: vp9, profile: vp9 profile0, "
      "coded size: [3840,2160], visible rect: [0,0,3840,2160]";
  std::string sw =
      "kVideoDecoderName: Dav1dVideoDecoder kIsPlatformVideoDecoder: false "
      "Selected Dav1dVideoDecoder for video decoding, config: codec: av1, coded size: [1280,720]";
  std::string audio = "kAudioDecoderName | OpusAudioDecoder | kIsPlatformAudioDecoder | false";
  auto r = ParseMediaInternals(hw + " ||| " + audio + " ||| " + sw);
  assert(r.size() == 2);  // audio-only player skipped
  assert(r[0].decoder_name == "VideoToolboxVideoDecoder");
  assert(r[0].platform == 1 && r[0].hardware());
  assert(r[0].codec == "vp9" && r[0].width == 3840 && r[0].height == 2160);
  assert(r[0].selection_line.find("Selected VideoToolboxVideoDecoder") == 0);
  assert(r[1].decoder_name == "Dav1dVideoDecoder" && !r[1].hardware());
  assert(r[1].codec == "av1" && r[1].width == 1280 && r[1].height == 720);

  // Verdict: only a platform hardware decoder with a size counts.
  assert(Verdict(r).rfind("verified: VideoToolboxVideoDecoder", 0) == 0);
  assert(Verdict({r[1]}).rfind("not verified:", 0) == 0);
  assert(Verdict({}).rfind("not verified:", 0) == 0);
  PlayerReport nameonly = r[0];  // name says VideoToolbox but Chromium says not platform
  nameonly.platform = 0;
  assert(!nameonly.hardware() && Verdict({nameonly}).rfind("not verified:", 0) == 0);
  PlayerReport nosize = r[0];
  nosize.width = nosize.height = 0;
  assert(Verdict({nosize}).find("no video size") != std::string::npos);
  assert(ParseMediaInternals("nothing here").empty());

  // chrome://gpu rows.
  std::string gpu =
      "Graphics Feature Status\nCanvas: Hardware accelerated\nCompositing: Hardware accelerated\n"
      "Video Decode: Hardware accelerated\nWebGL: Hardware accelerated\nWebGL2: Hardware accelerated\n"
      "Vulkan: Disabled\nProblems Detected\nVideo Acceleration Information\nDecoding\n"
      "Decode h264 baseline  up to 4096x4096 pixels\nDecode vp9 profile0 up to 8192x8192 pixels\n\n"
      "Driver Information\nGL_RENDERER: Apple M4 Max\n";
  auto g = ParseGpuFeatureStatus(gpu);
  bool vd = false, webgl2 = false;
  for (auto& kv : g) {
    if (kv.first == "Video Decode") vd = kv.second == "Hardware accelerated";
    if (kv.first == "WebGL2") webgl2 = kv.second == "Hardware accelerated";
  }
  assert(vd && webgl2 && g.size() == 6);
  auto bullets = ParseGpuFeatureStatus("Graphics Feature Status\n=====\n* Canvas: Hardware accelerated\n"
                                       "* Video Decode: Hardware accelerated\n* WebGL2: Disabled\n");
  assert(bullets.size() == 3 && bullets[1].second == "Hardware accelerated" && bullets[2].second == "Disabled");
  auto va = ParseVideoAcceleration(gpu);
  assert(va.size() == 3 && va[2].find("vp9") != std::string::npos);
  assert(ParseVideoAcceleration("no section").empty());

  // Chromium log filtering.
  assert(IsDecoderSelectionLine("[1:2:VERBOSE1:vt_video_decoder.cc(10)] VideoToolbox session created"));
  assert(IsDecoderSelectionLine("I Selected VideoToolboxVideoDecoder for video decoding"));
  assert(!IsDecoderSelectionLine("Selected input device"));
  assert(!IsDecoderSelectionLine("unrelated line"));
  auto lines = ExtractDecoderLines("a\nSelected Dav1dVideoDecoder for x\nb\nVTVideoDecode ok\n");
  assert(lines.size() == 2);
  assert(ExtractDecoderLines("Selected FooDecoder\nSelected BarDecoder\n", 1).size() == 1);

  // Paths stay inside the private folder.
  assert(PathInsideRoot("/p/priv/diag/x.log", "/p/priv"));
  assert(PathInsideRoot("/p/priv/diag/x.log", "/p/priv/"));
  assert(!PathInsideRoot("/p/priv/../etc/x.log", "/p/priv"));
  assert(!PathInsideRoot("/p/private/x.log", "/p/priv"));
  assert(!PathInsideRoot("/p/priv", "/p/priv"));
  assert(!PathInsideRoot("rel/x.log", "/p/priv"));
  assert(!PathInsideRoot("/p/priv/..", "/p/priv"));

  // Log writer: refuses outside paths, writes 0600, flattens newlines.
  char tmpl[] = "/tmp/decode_diag_test.XXXXXX";
  std::string root = mkdtemp(tmpl);
  std::string path = root + "/diagnostics/decode-diag.log";
  {
    Log bad;
    assert(!bad.Open("/tmp/decode_diag_outside.log", root) && !bad.is_open());
    bad.Append("x", "ignored");  // must not crash while closed
    Log log;
    assert(log.Open(path, root) && log.is_open());
    log.Append("gpu", "Video Decode: Hardware accelerated\nsecond line");
  }
  std::string body = Slurp(path);
  assert(body.find(" gpu Video Decode: Hardware accelerated | second line\n") != std::string::npos);
  struct stat st;
  assert(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
  unlink(path.c_str());
  rmdir((root + "/diagnostics").c_str());
  rmdir(root.c_str());

  // Off by default.
  unsetenv("OS999_DECODE_DIAG");
  assert(!Enabled());
  setenv("OS999_DECODE_DIAG", "1", 1);
  assert(Enabled());
  setenv("OS999_DECODE_DIAG", "yes", 1);
  assert(!Enabled());
  assert(ParseDelaySeconds(nullptr, 30) == 30 && ParseDelaySeconds("", 30) == 30);
  assert(ParseDelaySeconds("45", 30) == 45 && ParseDelaySeconds("1", 30) == 1);
  assert(ParseDelaySeconds("3600", 30) == 3600 && ParseDelaySeconds("3601", 30) == 30);
  assert(ParseDelaySeconds("0", 30) == 30 && ParseDelaySeconds("-5", 30) == 30);
  assert(ParseDelaySeconds("12s", 30) == 30 && ParseDelaySeconds("abc", 30) == 30);
  puts("decode diagnostics: ok");
}
