// src/adapters/psram_http.hpp
// Keep large HTTP responses — and the JSON documents parsed from them — out of
// internal DRAM.
//
// Why this exists: `http.getString()` is the obvious way to read a body, but
// wttr.in and bank.gov.ua answer with `Transfer-Encoding: chunked`, so there is
// no `Content-Length` for HTTPClient to pre-reserve against. The Arduino
// `String` therefore grows by repeated `realloc` in *internal* DRAM — tens of KB
// at a time, every weather poll and every currency poll. That is the single
// biggest source of internal-heap fragmentation in this firmware, and
// fragmentation is what eventually starves the panel driver's 8 KB per-frame
// `xTaskCreate` and deadlocks a refresh (handbook ch. 03, DECISIONS "Refresh
// rate limit + long-run freeze fixes").
//
// `HTTPClient::writeToStream()` de-chunks exactly the same way `getString()`
// does, but lets us choose the destination — so the bytes land in the 8 MB
// PSRAM instead, and internal heap stays flat across days of polling.
#pragma once
#include <ArduinoJson.h>
#include <Stream.h>

#include <cstddef>
#include <cstdint>

class HTTPClient;

namespace adapters {

// Growable PSRAM byte buffer usable as an HTTPClient write target.
// Write-only: the Stream read half is stubbed out, which is all
// `writeToStream()` needs.
class PsramBody : public Stream {
 public:
  static constexpr size_t kInitialBytes = 16 * 1024;
  // Hard ceiling so a misbehaving server can't grow this without bound. The
  // largest body we consume (wttr.in ?format=j1) is well under 100 KB.
  static constexpr size_t kMaxBytes = 192 * 1024;

  PsramBody() = default;
  ~PsramBody();
  PsramBody(const PsramBody&) = delete;
  PsramBody& operator=(const PsramBody&) = delete;

  void clear() {
    len_ = 0;
    oom_ = false;
  }
  size_t length() const { return len_; }
  bool empty() const { return len_ == 0; }
  // True if a grow failed (PSRAM exhausted or kMaxBytes hit); the contents are
  // then truncated and must not be parsed.
  bool overflowed() const { return oom_; }
  // NUL-terminated view of the body. Never null; "" when empty.
  const char* c_str();

  // ---- Print / Stream ----------------------------------------------------
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* data, size_t n) override;
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  bool reserve(size_t want);

  char* buf_ = nullptr;
  size_t cap_ = 0;
  size_t len_ = 0;
  bool oom_ = false;
};

// ArduinoJson allocator backed by PSRAM, so the parsed document doesn't land in
// internal DRAM either. Pass to a document: `JsonDocument doc(&psramJson());`.
class PsramJsonAllocator : public ArduinoJson::Allocator {
 public:
  void* allocate(size_t size) override;
  void deallocate(void* ptr) override;
  void* reallocate(void* ptr, size_t newSize) override;
};

// Shared instance — stateless, so one is enough.
PsramJsonAllocator& psramJson();

// GET `url` and write the (de-chunked) body into `out`.
// Returns true only on HTTP 200 with a complete, non-empty body. `err`/`errLen`
// receive a short reason on failure. Retries `attempts` times with a short
// backoff, since wttr.in renders on first hit and can be slow.
//
// `tls` selects WiFiClientSecure (setInsecure) instead of a plain WiFiClient.
bool httpGetToPsram(const char* url, PsramBody& out, bool tls, uint32_t connectTimeoutMs,
                    uint32_t timeoutMs, int attempts, const char* userAgent,
                    char* err, size_t errLen);

}  // namespace adapters
