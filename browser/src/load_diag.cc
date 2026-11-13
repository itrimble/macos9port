#include "load_diag.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace load_diag {

bool Enabled() {
  const char* v = getenv("OS999_LOAD_DIAG");
  return v && std::string(v) == "1";
}

bool ValidNextUrl(const char* url) {
  if (!url) return false;
  size_t n = strnlen(url, 1001);
  if (n == 0 || n > 1000) return false;
  if (strncmp(url, "https://", 8) != 0 || n == 8) return false;
  for (size_t i = 0; i < n; i++)
    if ((unsigned char)url[i] <= 32 || url[i] == 127) return false;
  // No credentials in the authority.
  const char* host = url + 8;
  size_t auth = strcspn(host, "/?#");
  if (memchr(host, '@', auth)) return false;
  return auth > 0;
}

std::string Sanitize(const std::string& s, size_t max) {
  std::string out;
  for (unsigned char c : s) out += (c < 32 || c == 127) ? ' ' : (char)c;
  if (out.size() > max) out.resize(max);
  return out;
}

std::string Origin(const std::string& url) {
  size_t sep = url.find("://");
  if (sep == std::string::npos || sep == 0 || sep > 10) return "?";
  size_t start = sep + 3, end = url.find_first_of("/?#", start);
  std::string auth = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
  size_t at = auth.rfind('@');
  if (at != std::string::npos) auth = auth.substr(at + 1);
  if (auth.empty()) return "?";
  return Sanitize(url.substr(0, sep) + "://" + auth, 120);
}

std::string FormatLoad(const Load& l) {
  char head[64], tail[200];
  snprintf(head, sizeof head, "load #%d ", l.seq);
  snprintf(tail, sizeof tail,
           " tls=%s cert_status=0x%x cert_errors=%d net_error=%d load_ms=%.0f",
           l.tls_verified ? "verified" : "NOT_VERIFIED", l.cert_status, l.cert_errors,
           l.net_error, l.load_ms);
  return std::string(head) + "requested=" + Sanitize(l.requested, 300) +
         " final=" + Sanitize(l.final_url, 300) + " http=" + std::to_string(l.http_status) +
         " title=\"" + Sanitize(l.title, 120) + "\"" + tail;
}

std::string FormatResources(int seq, const Resources& r, double after_s) {
  char buf[220];
  snprintf(buf, sizeof buf,
           "resources #%d after %.1f s: total=%d ok=%d http_error=%d failed=%d canceled=%d bytes=%lld",
           seq, after_s, r.total(), r.ok, r.http_error, r.failed, r.canceled, r.bytes);
  return buf;
}

int Tracker::Begin(const std::string& requested, double now_ms) {
  std::lock_guard<std::mutex> g(mu_);
  res_ = Resources();
  cert_errors_ = 0;
  requested_ = requested;
  start_ms_ = now_ms;
  return ++seq_;
}

void Tracker::AddResource(Result r, int http_status, long long bytes) {
  std::lock_guard<std::mutex> g(mu_);
  switch (r) {
    case Result::kOk:
      if (http_status >= 400) res_.http_error++; else res_.ok++;
      break;
    case Result::kFailed: res_.failed++; break;
    case Result::kCanceled: res_.canceled++; break;
  }
  if (bytes > 0) res_.bytes += bytes;
}

void Tracker::AddCertError() {
  std::lock_guard<std::mutex> g(mu_);
  cert_errors_++;
}

Resources Tracker::Snapshot() {
  std::lock_guard<std::mutex> g(mu_);
  return res_;
}

int Tracker::cert_errors() {
  std::lock_guard<std::mutex> g(mu_);
  return cert_errors_;
}

}  // namespace load_diag
