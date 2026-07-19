// src/adapters/net_services.hpp
// Optional homelab services (PLAN.md §T6): a /health JSON endpoint and
// ArduinoOTA. Both run inside the net task; call handle() from its loop.
// Compiled out for the battery profile.
#pragma once
#include <Arduino.h>

namespace adapters {

void netServicesBegin(const char* hostname);  // idempotent; safe once connected
void netServicesHandle();                     // pump OTA + web server

}  // namespace adapters
