// src/app/health.hpp
// Cross-task status snapshot for the /health endpoint. Scalar
// fields only; 32-bit aligned reads/writes on ESP32 are atomic, which is all a
// status endpoint needs — no locking.
#pragma once
#include <cstdint>

namespace app {

struct HealthStats {
  volatile uint32_t pollCount = 0;
  volatile uint32_t consecutiveFail = 0;
  volatile uint32_t lastOkMs = 0;
  volatile int rssi = 0;
  volatile uint32_t fullRefreshes = 0;
  // Panel-guard diagnostics: why pushes were held back, and what one costs.
  volatile uint32_t refreshDeferredRate = 0;  // blocked by the min-interval
  volatile uint32_t refreshDeferredHeap = 0;  // blocked by the heap gates
  volatile uint32_t refreshDeferredTemp = 0;  // blocked by the 0..50 C clamp
  volatile uint32_t lastRefreshMs = 0;        // duration of the last push
  volatile uint32_t maxRefreshMs = 0;
  // Liveness beacon written by the net task; the UI task reboots the board if
  // this stops advancing (a wedged socket/TLS session would otherwise look
  // exactly like a healthy but permanently "Stale" screen).
  volatile uint32_t netTickMs = 0;
  volatile uint32_t resetReason = 0;  // esp_reset_reason() of THIS boot
  char sensorFirmware[20] = {0};
};

extern HealthStats g_health;

}  // namespace app
