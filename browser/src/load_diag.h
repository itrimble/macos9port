// OS999 Browser: opt-in page-load diagnostics (demo A, checklist B07).
// Pure C++17 with no CEF dependency so it can be unit tested headlessly.
// Enabled only by OS999_LOAD_DIAG=1 in the environment; off otherwise. It only
// reads what Chromium reports (status, URL, title, TLS state, resource counts)
// and writes lines through BrowserStats::Log. It changes no sandbox, TLS or
// keychain policy.
#ifndef OS999_LOAD_DIAG_H_
#define OS999_LOAD_DIAG_H_

#include <mutex>
#include <string>

namespace load_diag {

// True when OS999_LOAD_DIAG is "1".
bool Enabled();

// A URL the follow-up navigation hook (OS999_LOAD_DIAG_NEXT) may load: https,
// no credentials, no control characters or spaces, at most 1000 bytes.
bool ValidNextUrl(const char* url);

// Control characters become spaces, then the text is cut to `max` bytes.
std::string Sanitize(const std::string& s, size_t max);

// "scheme://host" of a URL, nothing after it (no path, query or credentials); "?" if unparsable.
std::string Origin(const std::string& url);

// Outcome of one sub-resource request as the helper sees it.
enum class Result { kOk, kFailed, kCanceled };

struct Resources {
  int ok = 0;          // completed with HTTP status < 400
  int http_error = 0;  // completed with HTTP status >= 400
  int failed = 0;      // network-level failure
  int canceled = 0;    // canceled (including blocked by our own policy)
  long long bytes = 0;
  int total() const { return ok + http_error + failed + canceled; }
};

struct Load {
  int seq = 0;
  std::string requested, final_url, title;
  int http_status = 0;
  int net_error = 0;          // 0 when the main frame loaded
  bool tls_verified = false;  // https, secure connection, cert status none, normal content
  unsigned cert_status = 0;   // CefSSLStatus::GetCertStatus bit set
  int cert_errors = 0;        // OnCertificateError callbacks during this load
  double load_ms = 0;         // main-frame start to load end
};

// "load #1 requested=... final=... http=200 title=... tls=verified ..."
std::string FormatLoad(const Load& l);
// "resources #1 after 10.0 s: total=N ok=N http_error=N failed=N canceled=N bytes=N"
std::string FormatResources(int seq, const Resources& r, double after_s);

// Thread-safe counters: resource callbacks arrive on the IO thread.
class Tracker {
 public:
  // Starts a new load; returns its sequence number (1, 2, ...) and clears counters.
  int Begin(const std::string& requested, double now_ms);
  void AddResource(Result r, int http_status, long long bytes);
  void AddCertError();
  Resources Snapshot();
  int cert_errors();
  int seq() const { return seq_; }
  double start_ms() const { return start_ms_; }
  const std::string& requested() const { return requested_; }

 private:
  std::mutex mu_;
  Resources res_;
  int cert_errors_ = 0;
  int seq_ = 0;
  double start_ms_ = 0;
  std::string requested_;
};

}  // namespace load_diag

#endif
