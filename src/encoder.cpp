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
  static bool wasPressed = false;
  static unsigned long lastButtonPressTime = 0;
  static unsigned long pressStartTime = 0;
  static bool longPressHandled = false;
  // Оценивается один раз, в момент физического нажатия — отличает клик, который САМ входит
  // в настройки Dimmer (должен работать как обычно, даже если его удержать), от клика,
  // сделанного уже ВНУТРИ Dimmer (там короткий клик/удержание значат другое, см. ниже)
  static bool pressStartedInDimmer = false;

  bool isPressed = (digitalRead(BUTTON_PIN) == LOW);

  if (isPressed && !wasPressed) {
    // Свежее нажатие
    unsigned long currentTime = millis();
    pressStartTime = currentTime;
    longPressHandled = false;
    pressStartedInDimmer = inSettingsMode && menuItems[currentMenuItem] == "Dimmer";

    if (!pressStartedInDimmer) {
      if (currentTime - lastButtonPressTime < DOUBLE_CLICK_THRESHOLD_MS) {
        // Обнаружено двойное нажатие
        inSettingsMode = false;
        resetCursor(); // Сбрасываем положение курсора при выходе из режима настроек
        encoderValue = 0; // Сбрасываем значение энкодера при выходе из режима настроек
        stopAllMotors(); // Остановка всех моторов при выходе из режима настроек
        drawMenu();
      } else {
        // Одиночное нажатие, переключить режим настроек
        inSettingsMode = !inSettingsMode;
        if (inSettingsMode) {
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
            drawToggleSwitch(settings[currentMenuItem] == 1);
          } else if (menuItems[currentMenuItem] == "Dimmer") {
            dimmerEditingDisplay = false; // Каждый новый вход в Dimmer начинается со строки LED
            dimmerRowLocked = false; // ...и с выбора строки, а не редактирования значения
            drawDimmerScreen();
          } else if (menuItems[currentMenuItem] == "Color") {
            drawColorScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Source") {
            drawSourceScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "EQ") {
            drawEqScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Info") {
            drawInfoScreen();
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
    // pressStartedInDimmer: короткий клик/долгое удержание обрабатываются ниже, по мере
    // удержания и на отпускании — см. комментарии у DIMMER_EXIT_HOLD_MS в hardware_settings.h
  } else if (isPressed && pressStartedInDimmer && !longPressHandled &&
             millis() - pressStartTime >= DIMMER_EXIT_HOLD_MS) {
    // Зажали кнопку внутри Dimmer на DIMMER_EXIT_HOLD_MS — выходим в карусель меню,
    // независимо от того, были мы в выборе строки или в редактировании значения
    longPressHandled = true;
    inSettingsMode = false;
    resetCursor();
    encoderValue = 0;
    stopAllMotors();
    drawMenu();
  } else if (!isPressed && wasPressed && pressStartedInDimmer && !longPressHandled) {
    // Короткий клик внутри Dimmer (отпустили раньше, чем сработало удержание) —
    // переключает "выбор строки" <-> "редактирование её значения" (см. dimmerRowLocked в main.h)
    dimmerRowLocked = !dimmerRowLocked;
    drawDimmerScreen();
  }

  wasPressed = isPressed;
}
