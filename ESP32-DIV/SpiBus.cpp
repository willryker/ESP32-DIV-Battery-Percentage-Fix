#include "SpiBus.h"

// Recursive so a guarded helper can be called from an already-guarded caller
// on the same task without self-deadlock.
static SemaphoreHandle_t s_spiMutex = nullptr;

void spiBusInit() {
  if (s_spiMutex == nullptr) {
    s_spiMutex = xSemaphoreCreateRecursiveMutex();
  }
}

bool spiBusTake(uint32_t timeoutMs) {
  // Lazy safety net: if a caller runs before setup() created the mutex, make it
  // now rather than silently skipping serialization.
  if (s_spiMutex == nullptr) {
    spiBusInit();
  }
  // If allocation failed we must not hard-block the whole device; degrade to
  // "unserialized but running" rather than deadlocking on a null handle.
  if (s_spiMutex == nullptr) {
    return true;
  }
  const TickType_t ticks =
      (timeoutMs == 0xFFFFFFFFUL) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
  return xSemaphoreTakeRecursive(s_spiMutex, ticks) == pdTRUE;
}

void spiBusGive() {
  if (s_spiMutex != nullptr) {
    xSemaphoreGiveRecursive(s_spiMutex);
  }
}
