// Minimal boot diagnostic. Built only by [env:diag] to isolate board/PSRAM
// bring-up from the full firmware. Not part of the app.
#ifdef DIAG_BUILD
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== DIAG BOOT ===");
  Serial.printf("chip: %s rev%d, %d cores\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("flash size: %u\n", (unsigned)ESP.getFlashChipSize());
  Serial.printf("psramFound: %d\n", (int)psramFound());
  Serial.printf("psram size: %u\n", (unsigned)ESP.getPsramSize());
  Serial.printf("free heap: %u\n", (unsigned)ESP.getFreeHeap());
  Serial.println("=== DIAG OK ===");
}

void loop() {
  Serial.println("tick");
  delay(1000);
}
#endif
