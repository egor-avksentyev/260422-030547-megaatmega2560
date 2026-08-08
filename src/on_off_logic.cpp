#include "on_off_logic.h"
#include "hardware_settings.h"
#include "main.h"
#include "display_logic.h"
#include "neopixel.h"
#include "relay.h"
#include "motor_driver_logic.h"
#include "animation_logic.h"

bool powerOff = false; // Флаг для состояния питания
unsigned long powerButtonPressStartTime = 0; // Время начала нажатия кнопки питания
bool powerButtonPressing = false; // Флаг для состояния удержания кнопки питания

void powerOffScreen() {
  displayMessage("POWER OFF");
}

void powerOnScreen() {
  displayMessage("POWER ON");
}

void powerOffDevices() {
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
  settings[4] = 0; // Bypass выключен по умолчанию при каждом включении питания
  applyBypassState(); // Синхронизирует и реле, и индикаторный светодиод
  applySourceSelection(); // Восстанавливаем выбранный источник из settings[7]

  drawMenu(); // Отображаем меню после "POWER ON"
}
