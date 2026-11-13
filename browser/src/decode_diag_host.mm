#import "decode_diag_host.h"

#include <sys/stat.h>

#include <set>
#include <string>
#include <vector>

#include "browser_stats.h"
#include "decode_diag.h"
#include "include/cef_version.h"

namespace {
decode_diag::Log gLog;
std::string gSupport, gLastMedia, gLastGpu;
off_t gChromiumLogStart = 0;
std::vector<decode_diag::PlayerReport> gPlayers;
std::set<std::string> gSeen;
bool gFinished = false;

std::string Cap(const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) + "..." : s; }
}  // namespace

void DecodeDiagStart(NSString* support) {
  if (!decode_diag::Enabled() || !support.length) return;
  gSupport = support.UTF8String;
  if (!gLog.Open(gSupport + "/diagnostics/decode-diag.log", gSupport)) {
    fprintf(stderr, "decode diagnostics: cannot open log inside the private folder\n");
    return;
  }
  struct stat st;
  if (stat((gSupport + "/chromium.log").c_str(), &st) == 0) gChromiumLogStart = st.st_size;
  gLog.Append("start", std::string("cef ") + CEF_VERSION + " vmodule on, pid " +
                            std::to_string(getpid()));
}

bool DecodeDiagWantsScrape() {
#ifdef OS999_ENABLE_TEST_HOOKS
  return true;
#else
  return decode_diag::Enabled();
#endif
}

NSString* DecodeDiagScript() {
  return
      // chrome://media-internals: click each player, then dump its properties and log text.
      @"if(location.href.indexOf('chrome://media-internals')==0){"
       "var ls=[].slice.call(document.querySelectorAll('*')).filter(function(e){"
       "return e.children.length==0&&/^blob:/.test(e.textContent.trim());}),out=[];"
       "ls.forEach(function(e){e.click();if(e.parentElement)e.parentElement.click();"
       "var t=document.body.innerText,i=t.search(/kVideoDecoderName/);"
       "out.push(i<0?'no-decoder-row':t.substring(Math.max(0,i-1500),i+6000));});"
       "console.log('OS999MEDIA n='+ls.length+' '+out.join(' ||| ').replace(/\\n/g,' | '));}"
      // chrome://gpu: walk shadow roots; table rows become "name: value" lines.
       "if(location.href.indexOf('chrome://gpu')==0){"
       "function w(n){if(n.nodeType==3)return n.textContent.replace(/\\s+/g,' ').trim();"
       "if(n.tagName=='STYLE'||n.tagName=='SCRIPT')return '';"
       "var s=[];if(n.shadowRoot)s.push(w(n.shadowRoot));"
       "for(var c=n.firstChild;c;c=c.nextSibling){var x=w(c);if(x)s.push(x);}"
       "var tg=n.tagName||'';if(tg=='TR')return s.join(': ')+'\\n';"
       "if(/^(DIV|P|H1|H2|H3|LI|TABLE|UL|PRE|SECTION)$/.test(tg))return s.join(' ')+'\\n';"
       "return s.join(' ');}"
       "console.log('OS999GPU '+w(document.body));}";
}

void DecodeDiagConsole(NSString* message) {
  std::string msg = message.UTF8String ?: "";
  if (msg.rfind("OS999MEDIA ", 0) == 0) BrowserStats::Get().Log("media " + Cap(msg, 4000));
  if (!gLog.is_open()) return;
  if (msg.rfind("OS999MEDIA ", 0) == 0) {
    std::string body = msg.substr(11);
    if (body.rfind("n=", 0) == 0) body = body.substr(body.find(' ') == std::string::npos ? 0 : body.find(' ') + 1);
    if (body == gLastMedia) return;
    gLastMedia = body;
    auto players = decode_diag::ParseMediaInternals(body);
    gLog.Append("media-internals-raw", Cap(body, 20000));
    if (players.empty()) return;
    gPlayers = players;
    for (const auto& p : players) {
      std::string key = p.decoder_name + p.codec + std::to_string(p.width) + "x" + std::to_string(p.height) +
                        std::to_string(p.platform);
      if (!gSeen.insert(key).second) continue;  // each distinct player once
      gLog.Append("player", "decoder=" + p.decoder_name + " kIsPlatformVideoDecoder=" +
                                std::to_string(p.platform) + " codec=" + p.codec + " coded=" +
                                std::to_string(p.width) + "x" + std::to_string(p.height) +
                                " hardware=" + (p.hardware() ? "true" : "false") +
                                " | " + p.selection_line);
    }
  } else if (msg.rfind("OS999GPU ", 0) == 0) {
    std::string body = msg.substr(9);
    if (body == gLastGpu) return;
    gLastGpu = body;
    auto rows = decode_diag::ParseGpuFeatureStatus(body);
    if (rows.empty()) {  // page still loading, or the layout changed
      gLog.Append("gpu-raw-unparsed", Cap(body, 3000));
      return;
    }
    for (const auto& kv : rows) gLog.Append("gpu-feature", kv.first + ": " + kv.second);
    for (const auto& l : decode_diag::ParseVideoAcceleration(body)) gLog.Append("gpu-video-accel", l);
    gLog.Append("gpu-raw", Cap(body, 20000));
  }
}

void DecodeDiagFinish() {
  if (!gLog.is_open() || gFinished) return;
  gFinished = true;
  FILE* f = fopen((gSupport + "/chromium.log").c_str(), "r");
  if (f) {
    fseeko(f, gChromiumLogStart, SEEK_SET);
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (256u << 20)) text.append(buf, n);
    fclose(f);
    auto lines = decode_diag::ExtractDecoderLines(text);
    for (const auto& l : lines) gLog.Append("chromium-log", l);
    gLog.Append("chromium-log-summary", std::to_string(lines.size()) + " decoder lines in " +
                                            std::to_string(text.size()) + " bytes of this run's log");
  }
  gLog.Append("verdict", decode_diag::Verdict(gPlayers));
}
