#include "NvsConfig.h"
#include <Preferences.h>

namespace {
  void calKey(uint8_t rot, char* buf, size_t n) {
    snprintf(buf, n, "cal%u", (unsigned)(rot & 0x03));
  }
}

uint8_t NvsConfig::loadRotation(uint8_t def) {
  Preferences prefs;
  prefs.begin("netcfg", true);
  uint8_t r = prefs.getUChar("rot", def);
  prefs.end();
  if (r > 3) r = def;
  return r;
}

void NvsConfig::saveRotation(uint8_t rot) {
  Preferences prefs;
  prefs.begin("netcfg", false);
  prefs.putUChar("rot", rot & 0x03);
  prefs.end();
}

bool NvsConfig::loadCal(uint8_t rot, uint16_t out[5]) {
  char key[8];
  calKey(rot, key, sizeof(key));
  Preferences prefs;
  prefs.begin("netcfg", true);
  size_t got = prefs.getBytes(key, out, 5 * sizeof(uint16_t));
  prefs.end();
  return got == 5 * sizeof(uint16_t);
}

void NvsConfig::saveCal(uint8_t rot, const uint16_t data[5]) {
  char key[8];
  calKey(rot, key, sizeof(key));
  Preferences prefs;
  prefs.begin("netcfg", false);
  prefs.putBytes(key, data, 5 * sizeof(uint16_t));
  prefs.end();
}
