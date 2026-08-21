// src/adapters/weather_http.cpp
// Geolocation: ip-api.com (HTTP). Weather + UV: wttr.in ?format=j1 (HTTP).
// Both plain HTTP — no TLS, which is far more reliable on the ESP32 radio.
#include "adapters/weather_http.hpp"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <time.h>

#include "adapters/psram_http.hpp"
#include "app/clock.hpp"
#include "domain/weather_fmt.hpp"

namespace adapters {

namespace {
float atofOr(const char* s) {
  return (s && s[0]) ? (float)atof(s) : NAN;
}
}  // namespace

bool WeatherHttp::geolocate(double& lat, double& lon, char* city,
                            size_t cityLen, long& utcOffsetSec) {
  // Fail fast when the radio is down. The caller retries this on every weather
  // cycle until a fix sticks, and without the check each of those attempts sits
  // in the net task for the full 4 s connect timeout before returning the same
  // answer, delaying the AirGradient poll queued behind it. Same guard as
  // airgradient_http::poll().
  if (WiFi.status() != WL_CONNECTED) {
    strlcpy(err_, "wifi down", sizeof(err_));
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  // No keep-alive: this endpoint is hit at most once per boot, so a pooled
  // socket would only sit around holding internal DRAM afterwards.
  http.setReuse(false);
  if (!http.begin(client,
                  "http://ip-api.com/json/"
                  "?fields=status,city,lat,lon,offset")) {
    strlcpy(err_, "geo begin", sizeof(err_));
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(err_, sizeof(err_), "geo http %d", code);
    http.end();
    return false;
  }
  // Parse into PSRAM, like every other JSON parse in this firmware: a default
  // JsonDocument allocates in internal DRAM, and DRAM churn on the network path
  // is what fragments the heap the panel driver needs (see psram_http.hpp).
  // The body is ~1 KB and not chunked, so streaming straight into the parser is
  // still correct here — PsramBody is for the big chunked responses.
  JsonDocument doc(&adapters::psramJson());
  DeserializationError e = deserializeJson(doc, http.getStream());
  http.end();
  if (e || strcmp(doc["status"] | "", "success") != 0) {
    strlcpy(err_, "geo parse", sizeof(err_));
    return false;
  }
  lat = doc["lat"] | 0.0;
  lon = doc["lon"] | 0.0;
  utcOffsetSec = doc["offset"] | 0L;
  strlcpy(city, doc["city"] | "", cityLen);
  return true;
}

bool WeatherHttp::fetch(double lat, double lon, const char* city,
                        domain::Weather& out) {
  char url[64];
  snprintf(url, sizeof(url), "http://wttr.in/%.4f,%.4f?format=j1", lat, lon);

  // wttr.in renders on first hit and can be slow / flaky — retry a few times.
  // The response is *chunked* and tens of KB, so it goes to PSRAM via
  // writeToStream() rather than an Arduino String: getString() would grow a
  // buffer by repeated realloc in internal DRAM every 10 minutes, which is what
  // fragments the heap the panel driver needs (see adapters/psram_http.hpp).
  // A streamed parse is still not an option — the chunk-size markers corrupt
  // it. 2 x 10 s, not 3 x 15 s: the old worst case pinned the net task for ~63
  // s, starving the AirGradient poll (the actual product) and leaning on the
  // net-stall watchdog. A slow first-render wttr.in hit now just waits for the
  // next 10-minute cycle.
  adapters::PsramBody body;
  if (!httpGetToPsram(
          url, body, /*tls=*/false, /*connectTimeoutMs=*/6000,
          /*timeoutMs=*/10000, /*attempts=*/2,
          /*userAgent=*/"curl/8.5.0",  // wttr.in serves JSON to curl
          err_, sizeof(err_))) {
    return false;
  }

  // Filter keeps only the handful of fields we need — the full j1 payload is
  // tens of KB of hourly forecasts we must not load into RAM. The filter doc
  // goes to PSRAM too: internal-DRAM churn on this (repeating) path interleaves
  // with TLS windows and fragments the heap the panel driver needs.
  JsonDocument filter(&adapters::psramJson());
  JsonObject fc = filter["current_condition"][0].to<JsonObject>();
  fc["temp_C"] = true;
  fc["FeelsLikeC"] = true;
  fc["humidity"] = true;
  fc["windspeedKmph"] = true;
  fc["uvIndex"] = true;
  fc["precipMM"] = true;
  fc["weatherDesc"][0]["value"] = true;
  JsonObject fw = filter["weather"][0].to<JsonObject>();
  fw["date"] = true;
  fw["maxtempC"] = true;
  fw["mintempC"] = true;
  fw["uvIndex"] = true;
  fw["astronomy"][0]["sunrise"] = true;
  fw["astronomy"][0]["sunset"] = true;
  JsonObject fh = fw["hourly"][0].to<JsonObject>();
  fh["time"] = true;
  fh["tempC"] = true;
  fh["uvIndex"] = true;
  fh["weatherDesc"][0]["value"] = true;

  // Parse into PSRAM too — even filtered, the 3-day/hourly arrays are a few KB
  // that would otherwise come out of internal DRAM while the body is still
  // held.
  JsonDocument doc(&adapters::psramJson());
  DeserializationError e = deserializeJson(
      doc, body.c_str(), body.length(), DeserializationOption::Filter(filter));
  if (e) {
    snprintf(err_, sizeof(err_), "wx json: %s", e.c_str());
    return false;
  }

  JsonObject cc = doc["current_condition"][0];
  JsonObject wd = doc["weather"][0];
  JsonObject as = wd["astronomy"][0];

  domain::Weather w;
  w.tempC = atofOr(cc["temp_C"] | "");
  w.apparentC = atofOr(cc["FeelsLikeC"] | "");
  w.humidity = atofOr(cc["humidity"] | "");
  w.windKmh = atofOr(cc["windspeedKmph"] | "");
  w.precip = atofOr(cc["precipMM"] | "");
  w.uvIndex = atofOr(cc["uvIndex"] | "");
  w.uvMax = atofOr(wd["uvIndex"] | "");
  w.tMax = atofOr(wd["maxtempC"] | "");
  w.tMin = atofOr(wd["mintempC"] | "");
  strlcpy(w.desc, cc["weatherDesc"][0]["value"] | "", sizeof(w.desc));
  strlcpy(w.sunrise, as["sunrise"] | "", sizeof(w.sunrise));
  strlcpy(w.sunset, as["sunset"] | "", sizeof(w.sunset));
  strlcpy(w.city, city, sizeof(w.city));

  // ---- forecast: 3 days + flattened 3-hourly slots ------------------------
  struct FlatH {
    int hour;
    float t;
    int uv;
  };
  FlatH flat[24];
  int nflat = 0;
  int di = 0;
  for (JsonObject day : doc["weather"].as<JsonArray>()) {
    if (di < 3) {
      // Every field has to be present and in range before the date is used.
      // sscanf reports how many fields it filled, and a body that matches only
      // the year leaves the month at 0 — which used to reach the day-of-week
      // table with an index of -1. An unparseable date now simply leaves
      // w.days[di].name at its default empty value.
      int y = 0, mo = 0, dd = 0;
      if (sscanf(day["date"] | "", "%d-%d-%d", &y, &mo, &dd) == 3 && y > 0 &&
          mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31)
        strlcpy(w.days[di].name, domain::dowName(domain::dowIndex(y, mo, dd)),
                sizeof(w.days[di].name));
      w.days[di].tMax = atofOr(day["maxtempC"] | "");
      w.days[di].tMin = atofOr(day["mintempC"] | "");
    }
    for (JsonObject h : day["hourly"].as<JsonArray>()) {
      int hour = atoi(h["time"] | "0") / 100;
      if (di < 3 && hour == 12)
        domain::shortCond(h["weatherDesc"][0]["value"] | "", w.days[di].cond,
                          sizeof(w.days[di].cond));
      if (nflat < 24) {
        flat[nflat++] = {hour, atofOr(h["tempC"] | ""),
                         (int)(h["uvIndex"] | 0)};
      }
    }
    di++;
  }
  w.nDays = di < 3 ? di : 3;

  // Hourly row starts at the current 3-hour block.
  int curBlock = -1;
  time_t now = time(nullptr);
  if (now > app::kClockSyncedAfter) {
    struct tm lt;
    localtime_r(&now, &lt);
    curBlock = (lt.tm_hour / 3) * 3;
  }
  int start = 0;
  if (curBlock >= 0)
    for (int i = 0; i < 8 && i < nflat; ++i)
      if (flat[i].hour >= curBlock) {
        start = i;
        break;
      }
  w.nHours = 0;
  for (int i = 0; i < 8 && start + i < nflat; ++i) {
    FlatH& f = flat[start + i];
    if (i == 0)
      strlcpy(w.hours[0].label, "Now", sizeof(w.hours[0].label));
    else
      domain::hourLabel(f.hour, w.hours[i].label, sizeof(w.hours[i].label));
    w.hours[i].tempC = f.t;
    w.hours[i].uv = f.uv;
    w.nHours++;
  }

  // High-UV window (>=5) over today's slots.
  int uvFirst = -1, uvLast = -1, uvMax = 0;
  for (int i = 0; i < 8 && i < nflat; ++i) {
    if (flat[i].uv >= 5) {
      if (uvFirst < 0) uvFirst = flat[i].hour;
      uvLast = flat[i].hour;
      if (flat[i].uv > uvMax) uvMax = flat[i].uv;
    }
  }
  if (uvFirst >= 0) {
    char a[6], b[6];
    domain::hourLabel(uvFirst, a, sizeof(a));
    domain::hourLabel(uvLast, b, sizeof(b));
    snprintf(w.uvWindow, sizeof(w.uvWindow), "%s-%s", a, b);
    w.uvWindowMax = uvMax;
  } else {
    strlcpy(w.uvWindow, "None", sizeof(w.uvWindow));
  }

  if (isnan(w.tempC)) {
    strlcpy(err_, "wx empty", sizeof(err_));
    return false;
  }
  w.valid = true;
  w.ageMs = millis();
  out = w;
  err_[0] = 0;
  return true;
}

}  // namespace adapters
