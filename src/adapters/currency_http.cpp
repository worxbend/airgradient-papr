// src/adapters/currency_http.cpp
#include "adapters/currency_http.hpp"

#include <ArduinoJson.h>
#include <time.h>

#include "app/clock.hpp"

namespace adapters {

bool CurrencyHttp::tlsGet(const char* url, PsramBody& body,
                          uint32_t timeoutMs) {
  // 2 attempts, not 3: each TLS handshake takes ~45 KB of INTERNAL DRAM
  // (mbedTLS is pinned internal on this build) — every extra attempt is
  // another window in which a long-lived allocation can pin a fragment and
  // close the panel's heap gate. A failed cycle just retries in 30 min.
  return httpGetToPsram(url, body, /*tls=*/true, /*connectTimeoutMs=*/6000,
                        timeoutMs, /*attempts=*/2, /*userAgent=*/nullptr, err_,
                        sizeof(err_));
}

bool CurrencyHttp::fetch(domain::Currency& out) {
  domain::Currency c;
  PsramBody body;  // reused across all three requests; clear()ed per attempt

  // ---- NBU current rates (USD/EUR/CNY in UAH) ----
  if (!tlsGet("https://bank.gov.ua/NBUStatService/v1/statdirectory/"
              "exchange?json",
              body)) {
    // tlsGet already left the underlying reason in err_; prefix "nbu: " via a
    // scratch copy — snprintf'ing err_ into itself would be aliasing UB.
    char why[sizeof(err_)];
    strlcpy(why, err_, sizeof(why));
    snprintf(err_, sizeof(err_), "nbu: %s", why);
    return false;
  }
  {
    JsonDocument filter(&psramJson());  // keep even the filter out of DRAM
    JsonObject f = filter[0].to<JsonObject>();
    f["cc"] = true;
    f["rate"] = true;
    f["exchangedate"] = true;
    JsonDocument doc(&psramJson());
    if (deserializeJson(doc, body.c_str(), body.length(),
                        DeserializationOption::Filter(filter))) {
      strlcpy(err_, "nbu parse", sizeof(err_));
      return false;
    }
    float cnyUah = NAN;
    for (JsonObject o : doc.as<JsonArray>()) {
      const char* cc = o["cc"] | "";
      float r = o["rate"] | NAN;
      if (!strcmp(cc, "USD")) {
        c.usdUah = r;
        strlcpy(c.date, o["exchangedate"] | "", sizeof(c.date));
      } else if (!strcmp(cc, "EUR")) {
        c.eurUah = r;
      } else if (!strcmp(cc, "CNY")) {
        cnyUah = r;
      }
    }
    if (!isnan(cnyUah) && !isnan(c.usdUah) && c.usdUah > 0)
      c.cnyUsd = cnyUah / c.usdUah;
  }

  // ---- CoinGecko (BTC/ETH in USD) ----
  if (tlsGet("https://api.coingecko.com/api/v3/simple/"
             "price?ids=bitcoin,ethereum&vs_currencies=usd",
             body)) {
    JsonDocument doc(&psramJson());
    if (!deserializeJson(doc, body.c_str(), body.length())) {
      c.btcUsd = doc["bitcoin"]["usd"] | NAN;
      c.ethUsd = doc["ethereum"]["usd"] | NAN;
    }
  }

  // ---- NBU 30-day USD/UAH history ----
  time_t now = time(nullptr);
  if (now > app::kClockSyncedAfter) {  // history needs a real date range
    char start[9], end[9];
    struct tm t;
    localtime_r(&now, &t);
    strftime(end, sizeof(end), "%Y%m%d", &t);
    time_t past = now - 31L * 86400L;
    localtime_r(&past, &t);
    strftime(start, sizeof(start), "%Y%m%d", &t);

    char url[256];
    snprintf(url, sizeof(url),
             "https://bank.gov.ua/NBU_Exchange/"
             "exchange_site?start=%s&end=%s&valcode=usd&sort=exchangedate&"
             "order=asc&json",
             start, end);
    if (tlsGet(url, body)) {
      JsonDocument filter(&psramJson());
      filter[0]["rate"] = true;
      JsonDocument doc(&psramJson());
      if (!deserializeJson(doc, body.c_str(), body.length(),
                           DeserializationOption::Filter(filter))) {
        for (JsonObject o : doc.as<JsonArray>()) {
          if (c.histCount >= domain::kHistMax) break;
          float r = o["rate"] | NAN;
          if (!isnan(r)) c.hist[c.histCount++] = r;
        }
      }
    }
  }

  if (isnan(c.usdUah)) {
    strlcpy(err_, "no rates", sizeof(err_));
    return false;
  }
  c.valid = true;
  c.ageMs = millis();
  out = c;
  err_[0] = 0;
  return true;
}

}  // namespace adapters
