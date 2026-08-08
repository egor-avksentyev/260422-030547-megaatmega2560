// ============================================================================
// main.cpp — точка входа: setup()/loop() и общее состояние меню (menuItems[],
// currentMenuItem, settings[], inSettingsMode, isMuted). Вся конкретная логика
// (моторы, реле, дисплей, кольца, энкодер, пульт, питание, анимация Bypass)
// реализована в соответствующих модулях — main.cpp только их вызывает.
// ============================================================================

#include "main.h"
#include "hardware_settings.h"
#include "display_logic.h"
#include "neopixel.h"
#include "motor_position.h"
#include "motor_driver_logic.h"
#include "relay.h"
#include "animation_logic.h"
#include "encoder.h"
#include "remote_control.h"
#include "on_off_logic.h"

String menuItems[] = {"Bass", "High", "Volume", "VU Meter", "Bypass", "Dimmer", "Color", "Source"};
int currentMenuItem = 0;
int settings[] = {0, 0, 0, 1, 0, VOLUME_RING_DEFAULT_DIMMER, RING_COLOR_DEFAULT, 0}; // VU Meter "включено", Bypass "выключено", Dimmer/Color колец, Source по умолчанию
bool inSettingsMode = false;
bool isMuted = false; // Флаг для состояния Mute
unsigned long lastMotorInputTime = 0; // Момент последней команды на мотор Bass/High/Volume (для авто-стопа)

void resetCursor() {
  if (menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High" || menuItems[currentMenuItem] == "Volume") {
    settings[currentMenuItem] = 0; // Сбрасываем положение курсора в ноль
  }
}

void saveSettings() {
  // Здесь можно добавить код для сохранения состояния settings в EEPROM
}

void loadSettings() {
  // Здесь можно добавить код для загрузки состояния settings из EEPROM
}

void blinkLED(int pin) {
  static unsigned long lastBlinkTime = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastBlinkTime >= 100) { // 500 мс интервал для мигания
    digitalWrite(pin, !digitalRead(pin)); // Переключаем состояние светодиода
    lastBlinkTime = currentTime;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // Wait for serial port to connect. Needed for native USB
  }
  Serial.println("Starting setup...");

  u8g2.begin();
  volumeRing.begin();
  bassRing.begin();
  highRing.begin();
  applyRingColorScheme(); // Устанавливаем цвет активной части колец из settings[6]
  int initialRingBrightness = map(settings[5], 0, 100, 0, 255);
  volumeRing.setBrightness(initialRingBrightness);
  bassRing.setBrightness(initialRingBrightness);
  highRing.setBrightness(initialRingBrightness);
  volumeRing.clear();
  volumeRing.show();
  bassRing.clear();
  bassRing.show();
  highRing.clear();
  highRing.show();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(MOTOR1_IN, OUTPUT);
  pinMode(MOTOR1_PWM, OUTPUT);
  pinMode(MOTOR2_IN, OUTPUT);
  pinMode(MOTOR2_PWM, OUTPUT);
  pinMode(MOTOR3_IN1, OUTPUT);
  pinMode(MOTOR3_IN2, OUTPUT);
  pinMode(MOTOR3_PWM1, OUTPUT);
  pinMode(MOTOR3_PWM2, OUTPUT);
  pinMode(RELAY_PIN_STANDBY, OUTPUT);
  pinMode(RELAY_PIN_VU_METER, OUTPUT);
  pinMode(RELAY_PIN_LED, OUTPUT);
  pinMode(RELAY_PIN_MUTE, OUTPUT);
  pinMode(SOURCE_RELAY_1_PIN, OUTPUT);
  pinMode(SOURCE_RELAY_2_PIN, OUTPUT);
  pinMode(SOURCE_RELAY_3_PIN, OUTPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(LED_BASS_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_VOLUME_PIN, OUTPUT);
  pinMode(BYPASS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BYPASS_LED_PIN, OUTPUT);

  loadSettings(); // Загрузка сохраненных настроек

  digitalWrite(RELAY_PIN_STANDBY, HIGH); // Включаем реле Standby
  digitalWrite(RELAY_PIN_VU_METER, HIGH); // Включаем реле VU Meter
  digitalWrite(RELAY_PIN_MUTE, LOW); // Устанавливаем реле Mute в неактивное состояние (низкий уровень для реле высокого уровня)
  applyBypassState(); // Реле Bypass + индикаторный светодиод из settings[4], загруженного из EEPROM
  applySourceSelection(); // Включаем источник из settings[7] по умолчанию

  attachInterrupt(digitalPinToInterrupt(IR_PIN), IR_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_B_PIN), encoderISR, CHANGE);

  Serial.println("Setup complete"); // Отладочный вывод

  // Кольца светятся сразу после включения, не только внутри своих пунктов меню
  updateVolumeRing(readVolumePotPercent());
  renderDbRing(bassRing, readBassPotPercent(), bassRingState);
  renderDbRing(highRing, readHighPotPercent(), highRingState);

  drawMenu();
}

void loop() {
  if (irReceived) { // Проверка сигнала с ИК-пульта
    handleRemoteInput(); // Обработка входных данных с пульта
  }

  checkEncoderButton();
  checkBypassButton();

  if (encoderValue != 0) {
    if (!inSettingsMode) {
      if (encoderValue > 0) {
        currentMenuItem = (currentMenuItem + 1) % MENU_ITEM_COUNT;
        Serial.print("Menu item changed to: ");
        Serial.println(menuItems[currentMenuItem]);
      } else if (encoderValue < 0) {
        currentMenuItem = (currentMenuItem - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        Serial.print("Menu item changed to: ");
        Serial.println(menuItems[currentMenuItem]);
      }
      encoderValue = 0;
      drawMenu();
    } else {
      if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
        if (encoderValue > 2) { // Добавляем холостой ход в 2 шага
          settings[currentMenuItem] = 1; // Включаем режим ON
          encoderValue = 0;
          drawToggleSwitch(true);
          if (menuItems[currentMenuItem] == "VU Meter") {
            digitalWrite(RELAY_PIN_VU_METER, HIGH); // Включаем реле (высокий уровень для реле высокого уровня)
          } else if (menuItems[currentMenuItem] == "Bypass") {
            applyBypassState();
            triggerBypassAnim();
          }
        } else if (encoderValue < -2) { // Добавляем холостой ход в 2 шага
          settings[currentMenuItem] = 0; // Выключаем режим OFF
          encoderValue = 0;
          drawToggleSwitch(false);
          if (menuItems[currentMenuItem] == "VU Meter") {
            digitalWrite(RELAY_PIN_VU_METER, LOW); // Выключаем реле (низкий уровень для реле высокого уровня)
          } else if (menuItems[currentMenuItem] == "Bypass") {
            applyBypassState();
            triggerBypassAnim();
          }
        }
      } else {
        // Определяем, куда двигается энкодер, и устанавливаем флаги для отображения стрелок
        bool showArrowRight = (encoderValue > 0);
        bool showArrowLeft = (encoderValue < 0);
        int direction = (encoderValue > 0) ? 1 : -1;

        if (menuItems[currentMenuItem] == "Bass") {
          motorControl(direction * SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
          lastMotorInputTime = millis();
          encoderValue = 0;
          drawArrowIndicator(0, showArrowRight, showArrowLeft);
        } else if (menuItems[currentMenuItem] == "High") {
          motorControl(direction * SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
          lastMotorInputTime = millis();
          encoderValue = 0;
          drawArrowIndicator(0, showArrowRight, showArrowLeft);
        } else if (menuItems[currentMenuItem] == "Volume") {
          motorControl2(direction * SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
          lastMotorInputTime = millis();
          encoderValue = 0;
          drawArrowIndicator(0, showArrowRight, showArrowLeft);
        } else if (menuItems[currentMenuItem] == "Dimmer") {
          settings[currentMenuItem] = constrain(settings[currentMenuItem] + encoderValue * 5, 0, 100);
          encoderValue = 0;
          applyRingDimmer();
          drawDimmerScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "Color") {
          settings[currentMenuItem] = ((settings[currentMenuItem] + direction) % RING_COLOR_COUNT + RING_COLOR_COUNT) % RING_COLOR_COUNT;
          encoderValue = 0;
          applyRingColorScheme();
          drawColorScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "Source") {
          settings[currentMenuItem] = ((settings[currentMenuItem] + direction) % SOURCE_COUNT + SOURCE_COUNT) % SOURCE_COUNT;
          encoderValue = 0;
          applySourceSelection();
          drawSourceScreen(settings[currentMenuItem]);
        }
      }
    }
  }

  // Обновление светодиодов в режиме настройки
  if (inSettingsMode) {
    if (menuItems[currentMenuItem] == "Bass") {
      blinkLED(LED_BASS_PIN);
    } else if (menuItems[currentMenuItem] == "High") {
      blinkLED(LED_HIGH_PIN);
    } else if (menuItems[currentMenuItem] == "Volume") {
      blinkLED(LED_VOLUME_PIN);
    }
  } else {
    digitalWrite(LED_BASS_PIN, HIGH);
    digitalWrite(LED_HIGH_PIN, HIGH);
    digitalWrite(LED_VOLUME_PIN, HIGH);
  }

  // Кольца Bass/High/Volume светятся всегда (не только внутри своих пунктов меню), пока система не в Standby.
  // Bass/High пропускаются, пока играет разовая анимация переключения Bypass (см. блок ниже) —
  // она сама берёт на себя отображение этих двух колец, пока активна
  static unsigned long lastRingUpdate = 0;
  if (!powerOff && millis() - lastRingUpdate >= 200) {
    lastRingUpdate = millis();
    updateVolumeRing(readVolumePotPercent());
    if (bypassAnimMode == 0) {
      renderDbRing(bassRing, readBassPotPercent(), bassRingState);
      renderDbRing(highRing, readHighPotPercent(), highRingState);
    }
  }

  // Во время анимации мигания (возврат в 0dB) кольцу нужно обновляться чаще для
  // плавности — но ТОЛЬКО пока анимация активна, и без перечтения потенциометра
  // (дёшево: просто пересчёт яркости уже известного значения + show())
  static unsigned long lastBlinkRender = 0;
  if (!powerOff && bypassAnimMode == 0 && millis() - lastBlinkRender >= 30) {
    if (dbRingBlinking(bassRingState)) {
      renderDbRing(bassRing, bassRingState.lastValue, bassRingState);
    }
    if (dbRingBlinking(highRingState)) {
      renderDbRing(highRing, highRingState.lastValue, highRingState);
    }
    lastBlinkRender = millis();
  }

  // Разовая анимация переключения Bypass кнопкой — заливка кольца (Bypass выключен)
  // или мигание зелёной пары красным (Bypass включён), см. checkBypassButton().
  // Обновляется чаще (30мс) для плавности, пока активна; когда завершится — просто
  // гасим флаг, и предыдущий блок сам вернёт обычное отображение уровня дБ
  if (!powerOff && bypassAnimMode != 0) {
    unsigned long elapsed = millis() - bypassAnimStart;
    unsigned long totalMs = (bypassAnimMode == 1) ? BYPASS_FILL_TOTAL_MS : BYPASS_BLINK_TOTAL_MS;
    if (elapsed >= totalMs) {
      bypassAnimMode = 0;
    } else {
      static unsigned long lastBypassAnimRender = 0;
      if (millis() - lastBypassAnimRender >= 30) {
        lastBypassAnimRender = millis();
        if (bypassAnimMode == 1) {
          renderBypassFillAnim(bassRing, elapsed);
          renderBypassFillAnim(highRing, elapsed);
        } else {
          renderBypassBlinkAnim(bassRing, elapsed);
          renderBypassBlinkAnim(highRing, elapsed);
        }
      }
    }
  }

  // Моторы Bass/High/Volume мгновенно останавливаются, если давно не было новых команд от энкодера/пульта
  if (inSettingsMode && millis() - lastMotorInputTime > SLIDER_MOTOR_IDLE_TIMEOUT) {
    if (menuItems[currentMenuItem] == "Bass") {
      motorControl(0, MOTOR1_IN, MOTOR1_PWM);
    } else if (menuItems[currentMenuItem] == "High") {
      motorControl(0, MOTOR2_IN, MOTOR2_PWM);
    } else if (menuItems[currentMenuItem] == "Volume") {
      motorControl2(0, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
    }
  }

  // Живое обновление экрана с положением ручки Bass/High/Volume (реже, экрану такая частота не нужна)
  static unsigned long lastPotUpdate = 0;
  if (inSettingsMode &&
      (menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High" || menuItems[currentMenuItem] == "Volume") &&
      millis() - lastPotUpdate >= 200) {
    lastPotUpdate = millis();
    drawArrowIndicator(0, false, false);
  }
}
