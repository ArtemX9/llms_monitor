#include "Battery.h"
#include "Config.h"
#include <esp_adc_cal.h>

static esp_adc_cal_characteristics_t s_adcChars;

void Battery::init() {
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                            1100, &s_adcChars);
}

int Battery::readMilliVoltsAveraged() {
  uint32_t sum = 0;
  for (int i = 0; i < BAT_SAMPLE_COUNT; i++) {
    sum += analogRead(BAT_ADC_PIN);
    delayMicroseconds(100);
  }
  uint32_t raw = sum / BAT_SAMPLE_COUNT;
  return esp_adc_cal_raw_to_voltage(raw, &s_adcChars) * 2; // divider is 2:1
}

void Battery::classify(int mv) {
  if (_lastMv >= 0) {
    int delta = mv - _lastMv;
    if (delta > BAT_CHG_RISE_MV) {
      _risingStreak++;
      _fallingStreak = 0;
      if (_risingStreak >= BAT_CHG_RISE_STREAK) _charging = true;
    } else if (delta < -BAT_CHG_RISE_MV) {
      _fallingStreak++;
      _risingStreak = 0;
      if (_fallingStreak >= BAT_CHG_FALL_STREAK) _charging = false;
    } else {
      // Flat: sticky through CV taper — don't touch _charging, but a flat
      // sample isn't part of either streak, so it resets both.
      _risingStreak = 0;
      _fallingStreak = 0;
    }
  }
  _lastMv = mv;
}

int Battery::readPercent() {
  int mv = readMilliVoltsAveraged();
  classify(mv);

  if (mv <= BAT_MIN_MV) return 0;
  if (mv >= BAT_MAX_MV) return 100;
  return (mv - BAT_MIN_MV) * 100 / (BAT_MAX_MV - BAT_MIN_MV);
}

bool Battery::isCharging() const { return _charging; }
