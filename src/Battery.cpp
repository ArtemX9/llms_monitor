#include "Battery.h"
#include "Config.h"
#include <esp_adc_cal.h>

static esp_adc_cal_characteristics_t s_adcChars;

void Battery::init() {
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                            1100, &s_adcChars);
}

int Battery::readPercent() {
  uint32_t sum = 0;
  for (int i = 0; i < BAT_SAMPLE_COUNT; i++) {
    sum += analogRead(BAT_ADC_PIN);
    delayMicroseconds(100);
  }
  uint32_t raw = sum / BAT_SAMPLE_COUNT;
  int mv = esp_adc_cal_raw_to_voltage(raw, &s_adcChars) * 2; // divider is 2:1

  if (mv <= BAT_MIN_MV) return 0;
  if (mv >= BAT_MAX_MV) return 100;
  return (mv - BAT_MIN_MV) * 100 / (BAT_MAX_MV - BAT_MIN_MV);
}
