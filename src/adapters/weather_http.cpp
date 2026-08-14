// src/adapters/weather_http.cpp
// Geolocation: ip-api.com (HTTP). Weather + UV: wttr.in ?format=j1 (HTTP).
// Both plain HTTP — no TLS, which is far more reliable on the ESP32 radio.
#include "adapters/weather_http.hpp"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <time.h>

#include "adapters/psram_http.hpp"
#include "app/clock.hpp"

namespace adapters {

namespace {
float atofOr(const char* s) {
  return (s && s[0]) ? (float)atof(s) : NAN;
}

// Day-of-week (0=Sun) via Sakamoto's algorithm.
int dow(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
const char* dowName(int i) {
  static const char* n[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return n[i % 7];
}

// 24h hour -> "12AM".."9PM" (slots are multiples of 3).
void hourLabel(int h, char* out, size_t n) {
  int h12 = h % 12;
  if (h12 == 0) h12 = 12;
  snprintf(out, n, "%d%s", h12, h < 12 ? "AM" : "PM");
}

// Case-insensitive substring test (strcasestr isn't portable to newlib).
bool containsCI(const char* hay, const char* needle) {
  if (!hay) return false;
  size_t nl = strlen(needle);
  for (const char* p = hay; *p; ++p)
    if (strncasecmp(p, needle, nl) == 0) return true;
  return false;
}

// wttr description -> short uppercase condition word.
void shortCond(const char* d, char* out, size_t n) {
  auto has = [&](const char* k) { return containsCI(d, k); };
  const char* w = "CLOUDY";
  if (!d || !d[0])
    w = "--";
  else if (has("thunder") || has("storm"))
    w = "STORM";
  else if (has("snow") || has("sleet") || has("blizzard"))
    w = "SNOW";
  else if (has("rain") || has("drizzle") || has("shower"))
    w = "RAIN";
  else if (has("fog") || has("mist"))
    w = "FOG";
  else if (has("sunny") || has("clear"))
    w = "CLEAR";
  else if (has("overcast"))
    w = "OVERCAST";
  else if (has("partly") || has("cloud"))
    w = "CLOUDY";
  strlcpy(out, w, n);
}
}  // namespace

bool WeatherHttp::geolocate(double& lat, double& lon, char* city,
                            size_t cityLen, long& utcOffsetSec) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(4000);
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
  JsonDocument doc;
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
  // A streamed parse is still not an option — the chunk-size markers corrupt it.
  // 2 x 10 s, not 3 x 15 s: the old worst case pinned the net task for ~63 s,
  // starving the AirGradient poll (the actual product) and leaning on the
  // net-stall watchdog. A slow first-render wttr.in hit now just waits for the
  // next 10-minute cycle.
  adapters::PsramBody body;
  if (!httpGetToPsram(url, body, /*tls=*/false, /*connectTimeoutMs=*/6000,
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
  // that would otherwise come out of internal DRAM while the body is still held.
  JsonDocument doc(&adapters::psramJson());
  DeserializationError e = deserializeJson(doc, body.c_str(), body.length(),
                                           DeserializationOption::Filter(filter));
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
      int y = 0, mo = 0, dd = 0;
      sscanf(day["date"] | "", "%d-%d-%d", &y, &mo, &dd);
      if (y)
        strlcpy(w.days[di].name, dowName(dow(y, mo, dd)),
                sizeof(w.days[di].name));
      w.days[di].tMax = atofOr(day["maxtempC"] | "");
      w.days[di].tMin = atofOr(day["mintempC"] | "");
    }
    for (JsonObject h : day["hourly"].as<JsonArray>()) {
      int hour = atoi(h["time"] | "0") / 100;
      if (di < 3 && hour == 12)
        shortCond(h["weatherDesc"][0]["value"] | "", w.days[di].cond,
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
      hourLabel(f.hour, w.hours[i].label, sizeof(w.hours[i].label));
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
    hourLabel(uvFirst, a, sizeof(a));
    hourLabel(uvLast, b, sizeof(b));
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
