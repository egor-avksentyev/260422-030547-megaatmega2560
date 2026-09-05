#include "voltage_sensor.h"
#include "hardware_settings.h"
#include "motor_position.h"

// Не через общий readPotPercent() (64 быстрых analogRead() подряд, ~6.4мс) — этого хватает
// для потенциометров (постоянное напряжение), но модуль ZMPT101B+LM358 НЕ выпрямляет сеть,
// а отдаёт саму AC-синусоиду, смещённую примерно на середину шкалы ADC (bias ~VCC/2) —
// амплитуда этой синусоиды пропорциональна напряжению сети, а среднее по полному периоду
// (или нескольким) всегда сходится к bias практически независимо от амплитуды (проверено
// на реальном железе: raw~509 при 0В и raw~517 при 220В сети — 8 отсчётов на весь диапазон,
// то есть простое усреднение измеряет только bias, а не сеть). Поэтому вместо среднего ловим
// min/max за окно из VOLTAGE_SENSOR_SAMPLES отсчётов и берём размах (max-min) — он растёт
// вместе с амплитудой синусоиды. Окно должно перекрывать хотя бы один полный период сети
// (100Гц ряби/полупериодов при 50Гц сети — 10мс), поэтому сэмплов берём заметно больше, чем
// у потенциометров (см. VOLTAGE_SENSOR_SAMPLES, hardware_settings.h)
int readMainsVoltage(int* rawOut) {
  int rawMin = 1023;
  int rawMax = 0;
  for (int i = 0; i < VOLTAGE_SENSOR_SAMPLES; i++) {
    int sample = analogRead(VOLTAGE_SENSOR_PIN);
    if (sample < rawMin) rawMin = sample;
    if (sample > rawMax) rawMax = sample;
  }
  int raw = rawMax - rawMin;
  if (rawOut) {
    *rawOut = raw;
  }
  return potRawToPercent(raw, mainsVoltageCalRaw, mainsVoltageCalValue, mainsVoltageCalPoints);
}
