#include "motor_position.h"
#include "hardware_settings.h"
#include "main.h"
#include "motor_driver_logic.h"

int potRawToPercent(int raw, const int calRaw[], const int calPercent[], int calPoints, int zeroSnapRaw, int minSnapRaw, int maxSnapRaw) {
  if (zeroSnapRaw > 0) {
    for (int i = 0; i < calPoints; i++) {
      if (calPercent[i] == 0 && abs(raw - calRaw[i]) <= zeroSnapRaw) {
        return 0;
      }
    }
  }
  if (raw <= calRaw[0] + minSnapRaw) {
    return calPercent[0];
  }
  if (raw >= calRaw[calPoints - 1] - maxSnapRaw) {
    return calPercent[calPoints - 1];
  }
  for (int i = 0; i < calPoints - 1; i++) {
    if (raw >= calRaw[i] && raw <= calRaw[i + 1]) {
      long rawRange = calRaw[i + 1] - calRaw[i];
      long percentRange = calPercent[i + 1] - calPercent[i];
      return calPercent[i] + (raw - calRaw[i]) * percentRange / rawRange;
    }
  }
  return 0;
}

int readPotPercent(int pin, const int calRaw[], const int calPercent[], int calPoints, int* rawOut, int zeroSnapRaw, int minSnapRaw, int maxSnapRaw) {
  const int samples = 64;
  long rawSum = 0;
  for (int i = 0; i < samples; i++) {
    rawSum += analogRead(pin);
  }
  int potRaw = rawSum / samples; // усреднение по 64 сэмплам — гасит дребезг без задержки отклика
  if (rawOut != nullptr) {
    *rawOut = potRaw;
  }
  return potRawToPercent(potRaw, calRaw, calPercent, calPoints, zeroSnapRaw, minSnapRaw, maxSnapRaw);
}

// Пресеты EQ (см. eqPresets[]/EQ_COUNT, hardware_settings.h) с малым, но ненулевым дБ
// (Hip-Hop High=+1, Lounge High=-1) физически укладываются в *_POT_ZERO_SNAP_RAW вокруг
// истинного нуля — обычный снап-к-нулю ниже (нужен, чтобы погасить дребезг при РУЧНОМ
// вращении рядом с нулём) в этом случае маскирует осознанно ненулевой пресет зелёным
// 0dB. isEqSelectionActive() сам по себе тут недостаточен: он остаётся true и после
// того, как пользователь САМ докрутил ручку до настоящего нуля руками (без энкодера/
// пульта — cancelBassRecenter()/cancelHighRecenter() тогда не вызываются вообще, флаг
// повиснет true до следующего программного вмешательства) — поэтому дополнительно
// сравниваем raw с raw-целью пресета И с raw истинного нуля: показываем значение
// пресета, только если текущее положение физически ближе к цели пресета, чем к нулю —
// иначе показываем настоящий 0, даже если пресет формально ещё "активен"
static int eqPresetOverride(int fallbackPercent, int rawValue, int zeroRaw, const int calRaw[], const int calValue[], int calPoints, int8_t EqPreset::*field) {
  if (fallbackPercent != 0 || !isEqSelectionActive()) {
    return fallbackPercent;
  }
  int presetDb = eqPresets[settings[eqMenuIndex()]].*field;
  if (presetDb == 0) {
    return fallbackPercent;
  }
  int presetTargetRaw = valueToRaw(presetDb, calRaw, calValue, calPoints);
  if (abs(rawValue - presetTargetRaw) < abs(rawValue - zeroRaw)) {
    return presetDb;
  }
  return fallbackPercent;
}

int readBassPotPercent(int* rawOut) {
  // Гистерезис вокруг 0dB (см. BASS_POT_ZERO_EXIT_SNAP_RAW) — только для возвращаемого
  // (отображаемого) значения; rawOut отдаёт настоящее сырое значение без изменений,
  // на него завязан точный автовозврат мотора (motor_driver_logic.cpp), которому
  // гистерезис дисплея не нужен и мог бы помешать
  static bool wasAtZero = false;
  int zeroSnap = wasAtZero ? BASS_POT_ZERO_EXIT_SNAP_RAW : BASS_POT_ZERO_SNAP_RAW;
  int raw;
  int percent = readPotPercent(BASS_POT_PIN, bassPotCalRaw, bassPotCalValue, bassPotCalPoints, &raw, zeroSnap, BASS_POT_MIN_SNAP_RAW, BASS_POT_MAX_SNAP_RAW);
  wasAtZero = (percent == 0);
  if (rawOut != nullptr) {
    *rawOut = raw;
  }
  return eqPresetOverride(percent, raw, bassZeroRaw(), bassPotCalRaw, bassPotCalValue, bassPotCalPoints, &EqPreset::bassDb);
}

int readHighPotPercent(int* rawOut) {
  static bool wasAtZero = false;
  int zeroSnap = wasAtZero ? HIGH_POT_ZERO_EXIT_SNAP_RAW : HIGH_POT_ZERO_SNAP_RAW;
  int raw;
  int percent = readPotPercent(HIGH_POT_PIN, highPotCalRaw, highPotCalValue, highPotCalPoints, &raw, zeroSnap, HIGH_POT_MIN_SNAP_RAW, HIGH_POT_MAX_SNAP_RAW);
  wasAtZero = (percent == 0);
  if (rawOut != nullptr) {
    *rawOut = raw;
  }
  return eqPresetOverride(percent, raw, highZeroRaw(), highPotCalRaw, highPotCalValue, highPotCalPoints, &EqPreset::highDb);
}

int readVolumePotPercent(int* rawOut) {
  return readPotPercent(VOLUME_POT_PIN, volumePotCalRaw, volumePotCalPercent, volumePotCalPoints, rawOut, 0, VOLUME_POT_MIN_SNAP_RAW, VOLUME_POT_MAX_SNAP_RAW);
}

int readCurrentPotPercent(int* rawOut) {
  if (menuItems[currentMenuItem] == "Bass") {
    return readBassPotPercent(rawOut);
  }
  if (menuItems[currentMenuItem] == "High") {
    return readHighPotPercent(rawOut);
  }
  return readVolumePotPercent(rawOut);
}

int currentPotValueMin() {
  if (menuItems[currentMenuItem] == "Bass") {
    return bassPotCalValue[0];
  }
  if (menuItems[currentMenuItem] == "High") {
    return highPotCalValue[0];
  }
  return volumePotCalPercent[0];
}

int currentPotValueMax() {
  if (menuItems[currentMenuItem] == "Bass") {
    return bassPotCalValue[bassPotCalPoints - 1];
  }
  if (menuItems[currentMenuItem] == "High") {
    return highPotCalValue[highPotCalPoints - 1];
  }
  return volumePotCalPercent[volumePotCalPoints - 1];
}

bool currentPotIsDb() {
  return menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High";
}

static int calRawForValue(int targetValue, const int calRaw[], const int calValue[], int calPoints) {
  for (int i = 0; i < calPoints; i++) {
    if (calValue[i] == targetValue) {
      return calRaw[i];
    }
  }
  return calRaw[0]; // Не должно случаться — в таблице обязана быть точная точка 0dB
}

int bassZeroRaw() {
  return calRawForValue(0, bassPotCalRaw, bassPotCalValue, bassPotCalPoints);
}

int highZeroRaw() {
  return calRawForValue(0, highPotCalRaw, highPotCalValue, highPotCalPoints);
}

int valueToRaw(int targetValue, const int calRaw[], const int calValue[], int calPoints) {
  if (targetValue <= calValue[0]) {
    return calRaw[0];
  }
  if (targetValue >= calValue[calPoints - 1]) {
    return calRaw[calPoints - 1];
  }
  for (int i = 0; i < calPoints - 1; i++) {
    if (targetValue >= calValue[i] && targetValue <= calValue[i + 1]) {
      long valueRange = calValue[i + 1] - calValue[i];
      long rawRange = calRaw[i + 1] - calRaw[i];
      return calRaw[i] + (targetValue - calValue[i]) * rawRange / valueRange;
    }
  }
  return calRaw[0];
}
