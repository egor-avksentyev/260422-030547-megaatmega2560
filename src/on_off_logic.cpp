#include "on_off_logic.h"
#include "hardware_settings.h"
#include "main.h"
#include "display_logic.h"
#include "neopixel.h"
#include "relay.h"
#include "motor_driver_logic.h"
#include "motor_position.h"
#include "animation_logic.h"
#include "animations/boot_animation.h"
#include <EEPROM.h>

bool powerOff = false; // Флаг для состояния питания

// --- Долговременная память положения Bass/High (EEPROM) ---
// Сохраняется только если Bypass выключен при выключении питания (если включён, Bass/High
// всё равно принудительно прижаты к 0dB — запоминать нечего), восстанавливается при
// следующем включении (см. powerOnDevices()). Включение Bypass во время работы стирает
// эти данные — раз ручки "зажаты" в 0dB, старое положение больше не актуально.
struct SavedBassHighPosition {
  uint16_t magic; // Отличает "данные есть" от чистой/незаписанной EEPROM — иначе неинициализированные 0xFF читались бы как случайное валидное положение
  int bassRaw;
  int highRaw;
};
#define SAVED_POSITION_MAGIC 0xB055

void saveBassHighPositionOnShutdown() {
  if (settings[4] != 0) { // Bypass включён — нет смысла запоминать то, что и так съедет к 0dB
    return;
  }
  int bassRaw, highRaw;
  readBassPotPercent(&bassRaw);
  readHighPotPercent(&highRaw);
  SavedBassHighPosition data = {SAVED_POSITION_MAGIC, bassRaw, highRaw};
  EEPROM.put(EEPROM_BASS_HIGH_POSITION_ADDR, data); // Побайтно сравнивает с уже записанным и не трогает EEPROM, если ничего не изменилось
}

void clearSavedBassHighPosition() {
  SavedBassHighPosition data = {0, 0, 0}; // magic=0 != SAVED_POSITION_MAGIC — читается как "данных нет"
  EEPROM.put(EEPROM_BASS_HIGH_POSITION_ADDR, data);
}

static bool loadSavedBassHighPosition(int* bassRawOut, int* highRawOut) {
  SavedBassHighPosition data;
  EEPROM.get(EEPROM_BASS_HIGH_POSITION_ADDR, data);
  if (data.magic != SAVED_POSITION_MAGIC) {
    return false;
  }
  *bassRawOut = data.bassRaw;
  *highRawOut = data.highRaw;
  return true;
}

// --- Долговременная память состояния Bypass (EEPROM) ---
// В отличие от положения Bass/High, сохраняется ПРИ КАЖДОМ выключении (не только когда
// Bypass выключен) — восстанавливается при следующем включении вместо того, чтобы всегда
// сбрасываться в "выключено"
struct SavedBypassState {
  uint16_t magic;
  uint8_t bypassOn;
};
#define SAVED_BYPASS_MAGIC 0xB1A5

void saveBypassStateOnShutdown() {
  SavedBypassState data = {SAVED_BYPASS_MAGIC, (uint8_t)(settings[4] != 0 ? 1 : 0)};
  EEPROM.put(EEPROM_BYPASS_STATE_ADDR, data);
}

// false (то есть Bypass выключен) для самого первого включения, пока в EEPROM ничего не записано
static bool loadSavedBypassState() {
  SavedBypassState data;
  EEPROM.get(EEPROM_BYPASS_STATE_ADDR, data);
  return (data.magic == SAVED_BYPASS_MAGIC) && (data.bypassOn != 0);
}

void powerOffScreen() {
  playBootAnimation();
}

void powerOnScreen() {
  playBootAnimation();
}

void powerOffDevices() {
  // Принудительно выходим из режима настроек — иначе если выключили питание, пока был
  // открыт экран Bass/High/Volume, main.cpp продолжает каждые 200мс перерисовывать
  // кольцо этого пункта (тот блок не проверяет powerOff) и оно зажигается заново
  inSettingsMode = false;
  resetCursor();

  // Явно гасим светодиоды (LOW = выключено)
  pinMode(LED_BASS_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_VOLUME_PIN, OUTPUT);
  digitalWrite(LED_BASS_PIN, LOW);
  digitalWrite(LED_HIGH_PIN, LOW);
  digitalWrite(LED_VOLUME_PIN, LOW);

  // Явно размыкаем все реле (LOW = выключено при активной по HIGH логике)
  pinMode(RELAY_PIN_STANDBY, OUTPUT);
  pinMode(RELAY_PIN_VU_METER, OUTPUT);
  pinMode(RELAY_PIN_LED, OUTPUT);
  pinMode(RELAY_PIN_MUTE, OUTPUT);
  digitalWrite(RELAY_PIN_STANDBY, LOW);
  digitalWrite(RELAY_PIN_VU_METER, LOW);
  digitalWrite(RELAY_PIN_LED, LOW);
  digitalWrite(RELAY_PIN_MUTE, LOW);
  digitalWrite(SOURCE_RELAY_1_PIN, LOW); // Гасим все реле источников — взаимоисключающий выбор на паузе
  digitalWrite(SOURCE_RELAY_2_PIN, LOW);
  digitalWrite(SOURCE_RELAY_3_PIN, LOW);
  digitalWrite(BYPASS_LED_PIN, LOW); // Гасим индикатор Bypass вместе со всем остальным
  bypassAnimMode = 0; // Прерываем анимацию колец, если она была активна на момент выключения

  stopAllMotors();
  cancelBassRecenter(); // Не продолжать автовозврат (к 0dB/сохранённому положению/целевой громкости) после включения питания заново
  cancelHighRecenter();
  cancelVolumeSeek();
  delay(100); // Небольшая задержка для гарантированного отключения
  u8g2.setPowerSave(1); // Выключаем дисплей
  volumeRing.clear(); // Гасим кольца Volume/Bass/High
  volumeRing.show();
  bassRing.clear();
  bassRing.show();
  highRing.clear();
  highRing.show();
}

void powerOnDevices() {
  // Включение всех подключенных устройств
  u8g2.setPowerSave(0); // Включаем дисплей
  powerOnScreen(); // Отображаем "POWER ON"
  pinMode(LED_BASS_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_VOLUME_PIN, OUTPUT);
  digitalWrite(LED_BASS_PIN, HIGH);
  digitalWrite(LED_HIGH_PIN, HIGH);
  digitalWrite(LED_VOLUME_PIN, HIGH);

  pinMode(RELAY_PIN_STANDBY, OUTPUT);
  pinMode(RELAY_PIN_VU_METER, OUTPUT);
  pinMode(RELAY_PIN_LED, OUTPUT);
  pinMode(RELAY_PIN_MUTE, OUTPUT);
  digitalWrite(RELAY_PIN_STANDBY, HIGH); // Включаем Standby
  digitalWrite(RELAY_PIN_VU_METER, HIGH); // Включаем VU Meter
  digitalWrite(RELAY_PIN_MUTE, LOW); // Оставляем Mute выключенным
  settings[4] = loadSavedBypassState() ? 1 : 0; // Восстанавливаем Bypass, как было перед выключением
  applyBypassState(); // Синхронизирует и реле, и индикаторный светодиод
  applySourceSelection(); // Восстанавливаем выбранный источник из settings[7]

  if (settings[4] == 1) {
    // Bypass восстановлен включённым — центр колец Bass/High должен быть красным (как при
    // живом включении Bypass через triggerBypassAnim()), а не обычным зелёным нулевым цветом.
    // Ставим режим 2 напрямую (не через triggerBypassAnim()) — тот ещё и запросил бы
    // автовозврат к 0dB, что перезаписало бы восстановление Bass/High из EEPROM ниже
    bypassAnimMode = 2;
    bypassAnimStart = millis();
  }

  // Volume всегда стартует с фиксированного значения (независимо от Bypass/EEPROM);
  // Bass/High восстанавливаются из EEPROM, только если там есть данные (т.е. Bypass был
  // выключен на момент выключения) — оба идут в фоне через loop() (см. updateVolumeSeek()/
  // updateBassHighRecenter()), а не блокируют старт
  requestVolumeSeek(VOLUME_POWERON_TARGET_PERCENT);
  int savedBassRaw, savedHighRaw;
  if (loadSavedBassHighPosition(&savedBassRaw, &savedHighRaw)) {
    requestBassSeek(savedBassRaw);
    requestHighSeek(savedHighRaw);
  }

  drawMenu(); // Отображаем меню после "POWER ON"
}
