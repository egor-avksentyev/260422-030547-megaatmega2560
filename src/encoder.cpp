#include "encoder.h"
#include "hardware_settings.h"
#include "main.h"
#include "display_logic.h"
#include "motor_driver_logic.h"

volatile int encoderValue = 0;

// Алгоритм Олега Мазурова: чтение квадратурного энкодера через таблицу состояний.
// encoderISR() вызывается по прерыванию на ЛЮБОМ фронте (CHANGE) обоих пинов A и B —
// не зависит от того, чем занят loop(), поэтому не пропускает шаги при быстром вращении.
// Индекс в таблице — 4 бита: 2 старших — предыдущее состояние A,B, 2 младших — текущее.
// Невалидные/дребезговые переходы (в т.ч. "осталось как было") дают 0 и просто
// игнорируются на уровне самой таблицы — отдельный таймер-дебаунс не нужен
static const int8_t ENCODER_STATE_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
static volatile uint8_t encoderOldAB = 0;
static volatile int8_t encoderRawAccum = 0; // Накопитель валидных переходов между физическими щелчками

void encoderISR() {
  encoderOldAB <<= 2;
  encoderOldAB |= (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
  encoderRawAccum += ENCODER_STATE_TABLE[encoderOldAB & 0x0F];
  if (encoderRawAccum >= ENCODER_STEPS_PER_DETENT) {
    encoderValue++;
    encoderRawAccum -= ENCODER_STEPS_PER_DETENT;
  } else if (encoderRawAccum <= -ENCODER_STEPS_PER_DETENT) {
    encoderValue--;
    encoderRawAccum += ENCODER_STEPS_PER_DETENT;
  }
}

void checkEncoderButton() {
  static bool encoderButtonPressed = false;
  static unsigned long lastButtonPressTime = 0;

  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!encoderButtonPressed) {
      unsigned long currentTime = millis();
      if (currentTime - lastButtonPressTime < DOUBLE_CLICK_THRESHOLD_MS) {
        // Обнаружено двойное нажатие
        encoderButtonPressed = true;
        inSettingsMode = false;
        resetCursor(); // Сбрасываем положение курсора при выходе из режима настроек
        encoderValue = 0; // Сбрасываем значение энкодера при выходе из режима настроек
        stopAllMotors(); // Остановка всех моторов при выходе из режима настроек
        drawMenu();
      } else {
        // Одиночное нажатие, переключить режим настроек
        encoderButtonPressed = true;
        inSettingsMode = !inSettingsMode;
        if (inSettingsMode) {
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
            drawToggleSwitch(settings[currentMenuItem] == 1);
          } else if (menuItems[currentMenuItem] == "Dimmer") {
            drawDimmerScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Color") {
            drawColorScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Source") {
            drawSourceScreen(settings[currentMenuItem]);
          } else {
            drawArrowIndicator(settings[currentMenuItem], false, false); // Переход на экран с кругом и стрелочкой для Bass, High, Volume
          }
        } else {
          resetCursor(); // Сбрасываем положение курсора при выходе из режима настроек
          encoderValue = 0; // Сбрасываем значение энкодера при выходе из режима настроек
          stopAllMotors(); // Остановка всех моторов при выходе из режима настроек
          drawMenu();
        }
      }
      lastButtonPressTime = currentTime;
    }
  } else {
    encoderButtonPressed = false;
  }
}
