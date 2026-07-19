// src/adapters/currency_http.cpp
#include "adapters/currency_http.hpp"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

namespace adapters {

bool CurrencyHttp::tlsGet(const char* url, String& body, uint32_t timeoutMs) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(12);
    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(timeoutMs);
    if (!http.begin(client, url)) {
      strlcpy(err_, "begin failed", sizeof(err_));
      delay(400);
      continue;
    }
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
      body = http.getString();
      http.end();
      return !body.isEmpty();
    }
    snprintf(err_, sizeof(err_), "http %d", code);
    http.end();
    delay(500);
  }
  return false;
}

bool CurrencyHttp::fetch(domain::Currency& out) {
  domain::Currency c;
  String body;

  // ---- NBU current rates (USD/EUR/CNY in UAH) ----
  if (!tlsGet("https://bank.gov.ua/NBUStatService/v1/statdirectory/"
              "exchange?json",
              body)) {
    snprintf(err_, sizeof(err_), "nbu: %s", err_);
    return false;
  }
  {
    JsonDocument filter;
    JsonObject f = filter[0].to<JsonObject>();
    f["cc"] = true;
    f["rate"] = true;
    f["exchangedate"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
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
    JsonDocument doc;
    if (!deserializeJson(doc, body)) {
      c.btcUsd = doc["bitcoin"]["usd"] | NAN;
      c.ethUsd = doc["ethereum"]["usd"] | NAN;
    }
  }

  // ---- NBU 30-day USD/UAH history ----
  time_t now = time(nullptr);
  if (now > 1700000000) {
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
      JsonDocument filter;
      filter[0]["rate"] = true;
      JsonDocument doc;
      if (!deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
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
