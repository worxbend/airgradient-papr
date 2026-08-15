// src/adapters/net_services.hpp
// Optional homelab services: a /health JSON endpoint and ArduinoOTA. Both run
// inside the net task; call handle() from its loop. Compiled out for the
// battery profile.
#pragma once
#include <Arduino.h>

namespace adapters {

// True once if POST/GET /repair arrived since the last call. The handler runs
// on the NET task, but the de-ghost routine drives the panel and so must run on
// the UI task — this flag hands the request across instead of racing a refresh.
bool netServicesTakeRepairRequest();

void netServicesBegin(const char* hostname);  // idempotent; safe once connected
void netServicesHandle();                     // pump OTA + web server

}  // namespace adapters
