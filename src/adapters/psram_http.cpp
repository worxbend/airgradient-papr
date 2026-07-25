// src/adapters/psram_http.cpp
#include "adapters/psram_http.hpp"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <cstring>
#include <memory>

namespace adapters {

PsramBody::~PsramBody() {
  if (buf_) heap_caps_free(buf_);
}

bool PsramBody::reserve(size_t want) {
  if (want <= cap_) return true;
  if (want > kMaxBytes) {
    oom_ = true;
    return false;
  }
  size_t ncap = cap_ ? cap_ : kInitialBytes;
  while (ncap < want) ncap *= 2;
  if (ncap > kMaxBytes) ncap = kMaxBytes;

  // +1 so c_str() can always NUL-terminate without another grow.
  char* p = (char*)heap_caps_realloc(buf_, ncap + 1, MALLOC_CAP_SPIRAM);
  if (!p) {
    oom_ = true;
    return false;
  }
  buf_ = p;
  cap_ = ncap;
  return true;
}

size_t PsramBody::write(uint8_t b) {
  return write(&b, 1);
}

size_t PsramBody::write(const uint8_t* data, size_t n) {
  if (!n) return 0;
  if (!reserve(len_ + n)) return 0;  // 0 aborts writeToStream with an error
  memcpy(buf_ + len_, data, n);
  len_ += n;
  return n;
}

const char* PsramBody::c_str() {
  if (!buf_) return "";
  buf_[len_] = '\0';
  return buf_;
}

// ---------------------------------------------------------------------------

void* PsramJsonAllocator::allocate(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

void PsramJsonAllocator::deallocate(void* ptr) {
  heap_caps_free(ptr);
}

void* PsramJsonAllocator::reallocate(void* ptr, size_t newSize) {
  return heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM);
}

PsramJsonAllocator& psramJson() {
  static PsramJsonAllocator a;
  return a;
}

// ---------------------------------------------------------------------------

bool httpGetToPsram(const char* url, PsramBody& out, bool tls,
                    uint32_t connectTimeoutMs, uint32_t timeoutMs, int attempts,
                    const char* userAgent, char* err, size_t errLen) {
  for (int attempt = 0; attempt < attempts; ++attempt) {
    out.clear();

    // The client must outlive `http` — HTTPClient keeps a pointer to it. The
    // secure one is built only when actually needed: its constructor allocates
    // an mbedTLS context, which is exactly the internal-heap pressure this file
    // exists to avoid.
    WiFiClient plain;
    std::unique_ptr<WiFiClientSecure> secure;
    if (tls) {
      secure.reset(new WiFiClientSecure());
      if (!secure) {
        strlcpy(err, "tls alloc", errLen);
        return false;
      }
      secure->setInsecure();
      secure->setHandshakeTimeout(12);
    }
    WiFiClient& client = tls ? static_cast<WiFiClient&>(*secure) : plain;

    HTTPClient http;
    http.setConnectTimeout(connectTimeoutMs);
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    if (userAgent) http.setUserAgent(userAgent);

    if (!http.begin(client, url)) {
      strlcpy(err, "begin failed", errLen);
      delay(400);
      continue;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      snprintf(err, errLen, "http %d", code);
      http.end();
      delay(500);
      continue;
    }

    // De-chunks the body into PSRAM. A negative return means a truncated or
    // failed transfer — never parse a partial body, the filter would silently
    // yield an empty document.
    int written = http.writeToStream(&out);
    http.end();

    if (written < 0) {
      snprintf(err, errLen, "body err %d", written);
    } else if (out.overflowed()) {
      strlcpy(err, "body too large", errLen);
    } else if (out.empty()) {
      strlcpy(err, "empty body", errLen);
    } else {
      err[0] = '\0';
      return true;
    }
    delay(500);
  }
  return false;
}

}  // namespace adapters
