#include "relay.h"
#include "hardware_settings.h"
#include "main.h"
#include "animation_logic.h"
#include "display_logic.h"

// Применяет settings[] для пункта "Source" (0-3): включает ровно одно из четырёх реле,
// остальные три принудительно гасит — переключение источников взаимоисключающее
void applySourceSelection() {
  int idx = constrain(settings[7], 0, SOURCE_COUNT - 1);
  digitalWrite(SOURCE_RELAY_1_PIN, idx == 0 ? HIGH : LOW);
  digitalWrite(SOURCE_RELAY_2_PIN, idx == 1 ? HIGH : LOW);
  digitalWrite(SOURCE_RELAY_3_PIN, idx == 2 ? HIGH : LOW);
  digitalWrite(SOURCE_RELAY_4_PIN, idx == 3 ? HIGH : LOW);
}

// Применяет текущее settings[4] (Bypass) и на реле, и на отдельный индикаторный
// светодиод (горит, когда Bypass ВЫКЛЮЧЕН) — вызывается и от кнопки, и от пункта меню
void applyBypassState() {
  digitalWrite(RELAY_PIN_LED, settings[4] == 1 ? HIGH : LOW);
  digitalWrite(BYPASS_LED_PIN, settings[4] == 0 ? HIGH : LOW);
}

// Опрашивает физическую кнопку Bypass. Каждое нажатие (переход в LOW) переключает
// Bypass в противоположное состояние и запускает соответствующую анимацию колец
void checkBypassButton() {
  static int lastState = HIGH;
  static unsigned long lastChangeTime = 0;
  int state = digitalRead(BYPASS_BUTTON_PIN);
  if (state != lastState && millis() - lastChangeTime > 50) { // Простая защита от дребезга контактов
    lastChangeTime = millis();
    lastState = state;
    if (state == LOW) { // Реагируем только на нажатие, не на отпускание
      settings[4] = (settings[4] == 0) ? 1 : 0;
      applyBypassState();
      triggerBypassAnim();
      // Перерисовываем экран, чтобы кнопка отражалась независимо от того, что на
      // экране в момент нажатия: надпись "bypass" в карусели меню или toggle switch
      // на экране настроек самого пункта "Bypass". Пока включён Mute, экран занят его
      // непрерывной анимацией (см. main.cpp) — реле всё равно переключаем, а экран не трогаем
      if (!isMuted) {
        if (inSettingsMode && menuItems[currentMenuItem] == "Bypass") {
          drawToggleSwitch(settings[4] == 1);
        } else if (!inSettingsMode) {
          drawMenu();
        }
      }
    }
  }
}
