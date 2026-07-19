// src/adapters/airgradient_http.cpp
#include "adapters/airgradient_http.hpp"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace adapters {

namespace {
// ArduinoJson v7: `doc[key] | NAN` yields NAN when the key is missing or not
// numeric, which is exactly our "field absent" sentinel.
float getf(JsonDocument& d, const char* k) { return d[k] | NAN; }
}  // namespace

bool AirGradientHttp::poll(domain::Measurement& out) {
  if (WiFi.status() != WL_CONNECTED) {
    strlcpy(err_, "wifi down", sizeof(err_));
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(timeoutMs_);
  http.setTimeout(timeoutMs_);
  http.setReuse(false);

  if (!http.begin(client, url_)) {
    strlcpy(err_, "begin failed", sizeof(err_));
    return false;
  }
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(err_, sizeof(err_), "http %d", code);
    http.end();
    return false;
  }

  // Response is small (~1 KB). Filter keeps only fields we model.
  JsonDocument filter;
  for (const char* k :
       {"rco2", "pm01", "pm02", "pm10", "pm003Count", "pm003_count", "atmp",
        "atmpCompensated", "rhum", "rhumCompensated", "tvocIndex", "tvocRaw",
        "noxIndex", "noxRaw", "wifi", "boot", "bootCount", "firmware", "model",
        "serialno", "ledMode"}) {
    filter[k] = true;
  }

  JsonDocument doc;
  DeserializationError jerr = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (jerr) {
    snprintf(err_, sizeof(err_), "json: %s", jerr.c_str());
    return false;
  }

  domain::Measurement m;
  m.rco2 = getf(doc, "rco2");
  m.pm01 = getf(doc, "pm01");
  m.pm02 = getf(doc, "pm02");
  m.pm10 = getf(doc, "pm10");
  // Tolerant of both key spellings across firmware versions (§7 risk table).
  m.pm003Count = doc["pm003Count"].is<float>() ? doc["pm003Count"].as<float>()
                                               : (doc["pm003_count"] | NAN);
  m.atmp = getf(doc, "atmp");
  m.atmpCompensated = getf(doc, "atmpCompensated");
  m.rhum = getf(doc, "rhum");
  m.rhumCompensated = getf(doc, "rhumCompensated");
  m.tvocIndex = getf(doc, "tvocIndex");
  m.tvocRaw = getf(doc, "tvocRaw");
  m.noxIndex = getf(doc, "noxIndex");
  m.noxRaw = getf(doc, "noxRaw");
  m.wifi = doc["wifi"] | 0;
  m.boot = doc["boot"] | 0u;
  m.bootCount = doc["bootCount"] | 0u;
  strlcpy(m.firmware, doc["firmware"] | "", sizeof(m.firmware));
  strlcpy(m.model, doc["model"] | "", sizeof(m.model));
  strlcpy(m.serialno, doc["serialno"] | "", sizeof(m.serialno));
  strlcpy(m.ledMode, doc["ledMode"] | "", sizeof(m.ledMode));

  // A valid reading must carry at least one core metric.
  if (!domain::has(m.rco2) && !domain::has(m.pm02) && !domain::has(m.atmp)) {
    strlcpy(err_, "empty payload", sizeof(err_));
    return false;
  }

  m.valid = true;
  m.ageMs = millis();
  out = m;
  err_[0] = '\0';
  return true;
}

}  // namespace adapters
