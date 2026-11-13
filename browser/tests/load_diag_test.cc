#include "load_diag.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace load_diag;

int main() {
  setenv("OS999_LOAD_DIAG", "1", 1);
  assert(Enabled());
  setenv("OS999_LOAD_DIAG", "0", 1);
  assert(!Enabled());
  unsetenv("OS999_LOAD_DIAG");
  assert(!Enabled());

  assert(ValidNextUrl("https://www.thurrott.com/windows/123/article"));
  assert(!ValidNextUrl(nullptr));
  assert(!ValidNextUrl(""));
  assert(!ValidNextUrl("http://thurrott.com/"));
  assert(!ValidNextUrl("https://"));
  assert(!ValidNextUrl("https:///path"));
  assert(!ValidNextUrl("https://user:pw@thurrott.com/"));
  assert(!ValidNextUrl("https://thurrott.com/a b"));
  assert(!ValidNextUrl("https://thurrott.com/\n"));
  assert(ValidNextUrl("https://thurrott.com/?q=a@b"));  // '@' after the authority is fine
  assert(!ValidNextUrl(("https://t.com/" + std::string(1000, 'a')).c_str()));

  assert(Sanitize("a\nb\x01" "c", 10) == "a b c");
  assert(Sanitize("abcdef", 3) == "abc");

  assert(Origin("https://cdn.example.com/a/b.js?x=1#f") == "https://cdn.example.com");
  assert(Origin("http://user:pw@ads.example.net:8080/p") == "http://ads.example.net:8080");
  assert(Origin("https://host") == "https://host");
  assert(Origin("data:text/plain,hi") == "?" && Origin("") == "?" && Origin("https:///x") == "?");

  Tracker t;
  assert(t.Begin("https://thurrott.com/", 1000) == 1);
  t.AddResource(Result::kOk, 200, 1000);
  t.AddResource(Result::kOk, 304, 0);
  t.AddResource(Result::kOk, 404, 50);
  t.AddResource(Result::kFailed, 0, 0);
  t.AddResource(Result::kCanceled, 0, 0);
  t.AddCertError();
  Resources r = t.Snapshot();
  assert(r.ok == 2 && r.http_error == 1 && r.failed == 1 && r.canceled == 1);
  assert(r.total() == 5 && r.bytes == 1050 && t.cert_errors() == 1);
  assert(FormatResources(1, r, 10.0) ==
         "resources #1 after 10.0 s: total=5 ok=2 http_error=1 failed=1 canceled=1 bytes=1050");
  // A new load clears the counters and bumps the sequence number.
  assert(t.Begin("https://thurrott.com/x", 2000) == 2);
  assert(t.Snapshot().total() == 0 && t.cert_errors() == 0 && t.start_ms() == 2000);

  Load l;
  l.seq = 1;
  l.requested = "https://thurrott.com";
  l.final_url = "https://www.thurrott.com/";
  l.title = "Thurrott\n.com";
  l.http_status = 200;
  l.tls_verified = true;
  l.load_ms = 1234.4;
  std::string s = FormatLoad(l);
  assert(s ==
         "load #1 requested=https://thurrott.com final=https://www.thurrott.com/ http=200 "
         "title=\"Thurrott .com\" tls=verified cert_status=0x0 cert_errors=0 net_error=0 load_ms=1234");
  l.tls_verified = false;
  l.cert_status = 0x10;
  assert(FormatLoad(l).find("tls=NOT_VERIFIED cert_status=0x10") != std::string::npos);
  printf("load diagnostics: ok\n");
  return 0;
}
