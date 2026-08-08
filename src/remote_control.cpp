#include "remote_control.h"
#include "hardware_settings.h"
#include "main.h"
#include "display_logic.h"
#include "neopixel.h"
#include "relay.h"
#include "animation_logic.h"
#include "motor_driver_logic.h"
#include "on_off_logic.h"
#include "encoder.h"

NecDecoder necDecoder; // Создаем объект для декодирования сигналов пульта

volatile bool irReceived = false;
volatile uint8_t irCommand = 0;

void IR_ISR() {
  necDecoder.tick();
  if (necDecoder.available()) {
    irCommand = necDecoder.readCommand();
    irReceived = true;
  }
}

void handleRemoteInput() {
  if (irReceived) {
    irReceived = false; // Сброс флага

    Serial.print("Received IR command: 0x");
    Serial.println(irCommand, HEX); // Отладочный вывод

    switch (irCommand) {
      case IR_RIGHT:
        Serial.println("Right button pressed"); // Отладочный вывод
        if (!inSettingsMode) {
          currentMenuItem = (currentMenuItem + 1) % MENU_ITEM_COUNT;
          Serial.print("Current Menu Item: ");
          Serial.println(currentMenuItem); // Отладочный вывод
          drawMenu();
        } else {
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
            settings[currentMenuItem] = 1;
            drawToggleSwitch(true);
            if (menuItems[currentMenuItem] == "VU Meter") {
              digitalWrite(RELAY_PIN_VU_METER, HIGH);
            } else if (menuItems[currentMenuItem] == "Bypass") {
              applyBypassState();
              triggerBypassAnim();
            }
          } else if (menuItems[currentMenuItem] == "Dimmer") {
            settings[currentMenuItem] = constrain(settings[currentMenuItem] + 5, 0, 100);
            applyRingDimmer();
            drawDimmerScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Color") {
            settings[currentMenuItem] = (settings[currentMenuItem] + 1) % RING_COLOR_COUNT;
            applyRingColorScheme();
            drawColorScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Source") {
            settings[currentMenuItem] = (settings[currentMenuItem] + 1) % SOURCE_COUNT;
            applySourceSelection();
            drawSourceScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Bass") {
            motorControl(SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, true, false);
          } else if (menuItems[currentMenuItem] == "High") {
            motorControl(SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, true, false);
          } else if (menuItems[currentMenuItem] == "Volume") {
            motorControl2(SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, true, false);
          }
        }
        break;
      case IR_LEFT:
        Serial.println("Left button pressed"); // Отладочный вывод
        if (!inSettingsMode) {
          currentMenuItem = (currentMenuItem - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
          Serial.print("Current Menu Item: ");
          Serial.println(currentMenuItem); // Отладочный вывод
          drawMenu();
        } else {
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
            settings[currentMenuItem] = 0;
            drawToggleSwitch(false);
            if (menuItems[currentMenuItem] == "VU Meter") {
              digitalWrite(RELAY_PIN_VU_METER, LOW);
            } else if (menuItems[currentMenuItem] == "Bypass") {
              applyBypassState();
              triggerBypassAnim();
            }
          } else if (menuItems[currentMenuItem] == "Dimmer") {
            settings[currentMenuItem] = constrain(settings[currentMenuItem] - 5, 0, 100);
            applyRingDimmer();
            drawDimmerScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Color") {
            settings[currentMenuItem] = (settings[currentMenuItem] - 1 + RING_COLOR_COUNT) % RING_COLOR_COUNT;
            applyRingColorScheme();
            drawColorScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Source") {
            settings[currentMenuItem] = (settings[currentMenuItem] - 1 + SOURCE_COUNT) % SOURCE_COUNT;
            applySourceSelection();
            drawSourceScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Bass") {
            motorControl(-SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, false, true);
          } else if (menuItems[currentMenuItem] == "High") {
            motorControl(-SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, false, true);
          } else if (menuItems[currentMenuItem] == "Volume") {
            motorControl2(-SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, false, true);
          }
        }
        break;
      case IR_ENTER:
        Serial.println("Enter button pressed"); // Отладочный вывод
        if (!inSettingsMode) {
          inSettingsMode = true;
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
            drawToggleSwitch(settings[currentMenuItem] == 1);
          } else if (menuItems[currentMenuItem] == "Dimmer") {
            drawDimmerScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Color") {
            drawColorScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Source") {
            drawSourceScreen(settings[currentMenuItem]);
          } else {
            drawArrowIndicator(settings[currentMenuItem], false, false);
          }
        } else {
          inSettingsMode = false;
          resetCursor();
          encoderValue = 0;
          stopAllMotors();
          drawMenu();
        }
        break;
      case IR_MUTE:
        Serial.println("Mute button pressed"); // Отладочный вывод
        isMuted = !isMuted; // Переключаем состояние Mute
        digitalWrite(RELAY_PIN_MUTE, isMuted ? HIGH : LOW); // Управляем реле Mute
        drawMenu(); // Перерисовываем меню для отображения/удаления надписи Mute
        break;
      case IR_POWER:
        if (!powerButtonPressing) {
          powerButtonPressing = true;
          powerButtonPressStartTime = millis();
        } else if (millis() - powerButtonPressStartTime >= 3000) {
          // Длительное нажатие кнопки питания (3 секунды)
          Serial.println("Power button long press"); // Отладочный вывод
          if (powerOff) {
            powerOnDevices();
            powerOff = false;
          } else {
            // Отключение устройств, затем отображение "POWER OFF"
            digitalWrite(LED_BASS_PIN, LOW);
            digitalWrite(LED_HIGH_PIN, LOW);
            digitalWrite(LED_VOLUME_PIN, LOW);
            stopAllMotors();
            delay(100); // Небольшая задержка для гарантированного отключения
            powerOffScreen();
            delay(3000); // 3 секунды для отображения "POWER OFF"
            powerOffDevices();
            powerOff = true;
          }
          powerButtonPressing = false;
        }
        break;
      default:
        Serial.println("Unknown button pressed"); // Отладочный вывод
        break;
    }
  }
}
