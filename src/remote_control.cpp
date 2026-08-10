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
#include <IRremote.hpp>

void initRemoteControl() {
  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK);
}

void handleRemoteInput() {
  if (IrReceiver.decode()) {
    if (IrReceiver.decodedIRData.protocol != IR_PROTOCOL || IrReceiver.decodedIRData.address != IR_ADDRESS) {
      // Не наш пульт: либо чистый шум (Protocol=UNKNOWN), либо наводка (например от моторов),
      // случайно похожая на валидный кадр другого протокола/адреса — реальная кнопка всегда
      // приходит как IR_PROTOCOL с Address=IR_ADDRESS
      IrReceiver.resume();
      return;
    }

    bool isRepeat = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;
    uint16_t irCommand = IrReceiver.decodedIRData.command;
    IrReceiver.resume(); // Можно принимать следующий кадр — irCommand уже сохранён

    // Долгое нажатие шлёт кучу repeat-кадров подряд, пока держишь кнопку — для
    // одноразовых действий (навигация и т.п.) это не новая команда, гасим. Но для
    // Power они нужны, чтобы отслеживать длительность удержания (см. case IR_POWER),
    // а для Up/Down — чтобы мотор двигался непрерывно, пока держишь кнопку (см. ниже)
    if (isRepeat && irCommand != IR_POWER && irCommand != IR_UP && irCommand != IR_DOWN) {
      return;
    }

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
        // Надпись "mute" теперь рисуется на всех экранах (см. drawStatusIndicators() в
        // display_logic.cpp) — перерисовываем ТЕКУЩИЙ экран (не всегда drawMenu(), иначе
        // это скачок в карусель меню и обратно), чтобы подтверждение было видно сразу же,
        // а не после следующего случайного перерисовывания
        if (!inSettingsMode) {
          drawMenu();
        } else if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
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
        break;
      case IR_UP: // Двигает мотор, пока держишь — repeat-кадры разрешены выше, а
                  // останавливает существующий таймаут простоя в main.cpp (SLIDER_MOTOR_IDLE_TIMEOUT),
                  // когда новые кадры перестают приходить (кнопку отпустили)
        if (inSettingsMode) {
          if (menuItems[currentMenuItem] == "Bass") {
            cancelBassRecenter(); // Ручное управление пультом отменяет автовозврат после Bypass
            motorControl(SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, true, false);
          } else if (menuItems[currentMenuItem] == "High") {
            cancelHighRecenter(); // Ручное управление пультом отменяет автовозврат после Bypass
            motorControl(SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, true, false);
          } else if (menuItems[currentMenuItem] == "Volume") {
            cancelVolumeSeek(); // Ручное управление пультом отменяет автовозврат к целевой громкости после включения питания
            motorControl2(SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, true, false);
          }
        }
        break;
      case IR_DOWN: // Симметрично IR_UP, в другую сторону
        if (inSettingsMode) {
          if (menuItems[currentMenuItem] == "Bass") {
            cancelBassRecenter();
            motorControl(-SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, false, true);
          } else if (menuItems[currentMenuItem] == "High") {
            cancelHighRecenter();
            motorControl(-SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, false, true);
          } else if (menuItems[currentMenuItem] == "Volume") {
            cancelVolumeSeek();
            motorControl2(-SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, false, true);
          }
        }
        break;
      case IR_POWER: {
        // Одно нажатие — сразу, без удержания. Игнорируем repeat-кадры, иначе удержание
        // кнопки будет непрерывно включать/выключать обратно. Отдельно от repeat: изредка
        // пульт/наводка шлёт по одному нажатию два самостоятельных (не repeat-) кадра —
        // без доп. защиты это включает и тут же снова выключает, выглядит как "не сработало".
        static unsigned long lastPowerActionTime = 0;
        if (!isRepeat && millis() - lastPowerActionTime > 800) {
          lastPowerActionTime = millis();
          Serial.println("Power button pressed"); // Отладочный вывод
          if (powerOff) {
            powerOnDevices();
            powerOff = false;
          } else {
            // Отключение устройств, затем отображение "POWER OFF"
            digitalWrite(LED_BASS_PIN, LOW);
            digitalWrite(LED_HIGH_PIN, LOW);
            digitalWrite(LED_VOLUME_PIN, LOW);
            saveBypassStateOnShutdown(); // Восстанавливается при следующем включении, независимо от значения
            saveBassHighPositionOnShutdown(); // Пока моторы ещё не сдвинуты — иначе тут же перезапишет 0dB/0%
            seekBassHighVolumeToZeroBlocking(); // Сначала все моторы едут в ноль...
            delay(100); // Небольшая задержка для гарантированного отключения
            powerOffScreen();
            delay(3000); // 3 секунды для отображения "POWER OFF"
            powerOffDevices(); // ...и только потом обесточивается система
            powerOff = true;
          }
        }
        break;
      }
      default:
        Serial.println("Unknown button pressed"); // Отладочный вывод
        break;
    }
  }
}
