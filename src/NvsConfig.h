#pragma once
#include <Arduino.h>

// Thin wrapper over the "netcfg" NVS namespace for display orientation and
// per-rotation XPT2046 touch calibration overrides. Separate from DataFetcher
// so both main.cpp and Renderer can use it without cross-dependencies.
namespace NvsConfig {
  uint8_t loadRotation(uint8_t def);            // key "rot"; returns def if unset
  void    saveRotation(uint8_t rot);

  // Per-rotation calibration override. Key "cal0".."cal3", each a 10-byte blob
  // (5 x uint16_t). loadCal returns false if nothing is stored for that rotation.
  bool    loadCal(uint8_t rot, uint16_t out[5]);
  void    saveCal(uint8_t rot, const uint16_t data[5]);
}
