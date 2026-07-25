// src/adapters/currency_http.hpp
// Exchange rates over HTTPS (TLS works now that LVGL lives in PSRAM):
//   - bank.gov.ua (NBU): USD/EUR/CNY in UAH + 30-day USD/UAH history
//   - api.coingecko.com: BTC/ETH in USD
#pragma once
#include <Arduino.h>

#include "adapters/psram_http.hpp"
#include "domain/currency.hpp"

namespace adapters {

class CurrencyHttp {
 public:
  bool fetch(domain::Currency& out);  // NBU rates + history + crypto
  const char* lastError() const { return err_; }

 private:
  // Bodies land in PSRAM (see psram_http.hpp): the NBU history response is ~30
  // daily records and bank.gov.ua answers chunked, so an Arduino String would
  // fragment internal DRAM on every 30-minute poll.
  bool tlsGet(const char* url, PsramBody& body, uint32_t timeoutMs = 10000);
  char err_[64] = {0};
};

}  // namespace adapters
