// src/app/health.hpp
// Cross-task status snapshot for the /health endpoint (PLAN.md §T6). Scalar
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
  volatile uint32_t partialRefreshes = 0;
  char sensorFirmware[20] = {0};
};

extern HealthStats g_health;

}  // namespace app
