#include "motor_position.h"
#include "hardware_settings.h"
#include "main.h"

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

int readBassPotPercent(int* rawOut) {
  // Гистерезис вокруг 0dB (см. BASS_POT_ZERO_EXIT_SNAP_RAW) — только для возвращаемого
  // (отображаемого) значения; rawOut отдаёт настоящее сырое значение без изменений,
  // на него завязан точный автовозврат мотора (motor_driver_logic.cpp), которому
  // гистерезис дисплея не нужен и мог бы помешать
  static bool wasAtZero = false;
  int zeroSnap = wasAtZero ? BASS_POT_ZERO_EXIT_SNAP_RAW : BASS_POT_ZERO_SNAP_RAW;
  int percent = readPotPercent(BASS_POT_PIN, bassPotCalRaw, bassPotCalValue, bassPotCalPoints, rawOut, zeroSnap, BASS_POT_MIN_SNAP_RAW, BASS_POT_MAX_SNAP_RAW);
  wasAtZero = (percent == 0);
  return percent;
}

int readHighPotPercent(int* rawOut) {
  static bool wasAtZero = false;
  int zeroSnap = wasAtZero ? HIGH_POT_ZERO_EXIT_SNAP_RAW : HIGH_POT_ZERO_SNAP_RAW;
  int percent = readPotPercent(HIGH_POT_PIN, highPotCalRaw, highPotCalValue, highPotCalPoints, rawOut, zeroSnap, HIGH_POT_MIN_SNAP_RAW, HIGH_POT_MAX_SNAP_RAW);
  wasAtZero = (percent == 0);
  return percent;
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
