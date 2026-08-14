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
#include "animations/mute_animation.h"
#include "animations/unmute_animation.h"
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

    // Пока система выключена (powerOff), должна работать ТОЛЬКО кнопка Power — раньше остальные
    // кнопки пульта продолжали молча менять currentMenuItem/settings[]/inSettingsMode (экран
    // просто спит, не показывая этого), а IR_UP/IR_DOWN могли даже physически провернуть мотор,
    // хотя реле и дисплей уже обесточены
    if (powerOff && irCommand != IR_POWER) {
      return;
    }

    // Пока Mute включён, экран целиком занят непрерывной MUTE-анимацией (см. animateMuteFrame()
    // в main.cpp) — навигация по меню/настройкам недоступна, чтобы не бороться за экран с этой
    // анимацией. Сам Mute (чтобы выключить) и Power — работают всегда
    if (isMuted && irCommand != IR_MUTE && irCommand != IR_POWER) {
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
            if (dimmerEditingDisplay) {
              displayBrightness = constrain(displayBrightness + 5, 0, 100);
              applyDisplayBrightness();
            } else {
              settings[currentMenuItem] = constrain(settings[currentMenuItem] + 5, 0, 100);
              applyRingDimmer();
            }
            drawDimmerScreen();
            saveSettings();
          } else if (menuItems[currentMenuItem] == "Color") {
            settings[currentMenuItem] = (settings[currentMenuItem] + 1) % RING_COLOR_COUNT;
            applyRingColorScheme();
            drawColorScreen(settings[currentMenuItem]);
            saveSettings();
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
            if (dimmerEditingDisplay) {
              displayBrightness = constrain(displayBrightness - 5, 0, 100);
              applyDisplayBrightness();
            } else {
              settings[currentMenuItem] = constrain(settings[currentMenuItem] - 5, 0, 100);
              applyRingDimmer();
            }
            drawDimmerScreen();
            saveSettings();
          } else if (menuItems[currentMenuItem] == "Color") {
            settings[currentMenuItem] = (settings[currentMenuItem] - 1 + RING_COLOR_COUNT) % RING_COLOR_COUNT;
            applyRingColorScheme();
            drawColorScreen(settings[currentMenuItem]);
            saveSettings();
          } else if (menuItems[currentMenuItem] == "Source") {
            settings[currentMenuItem] = (settings[currentMenuItem] - 1 + SOURCE_COUNT) % SOURCE_COUNT;
            applySourceSelection();
            drawSourceScreen(settings[currentMenuItem]);
          }
        }
        break;
      case IR_ENTER: {
        // Та же защита от "бита" пульта, что у IR_POWER/IR_MUTE — изредка одно физическое
        // нажатие шлёт два самостоятельных не-repeat кадра подряд. Без защиты второй кадр
        // сразу отменяет действие первого (вошли в настройки — тут же вышли, и наоборот) —
        // выглядит как "не сработало с первого раза". Окно как у Mute (200мс) — Enter такое
        // же нечастое осознанное действие, длинное окно Power (800мс) тут не нужно
        static unsigned long lastEnterActionTime = 0;
        if (millis() - lastEnterActionTime > 200) {
          lastEnterActionTime = millis();
          Serial.println("Enter button pressed"); // Отладочный вывод
          if (!inSettingsMode) {
            inSettingsMode = true;
            if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
              drawToggleSwitch(settings[currentMenuItem] == 1);
            } else if (menuItems[currentMenuItem] == "Dimmer") {
              dimmerEditingDisplay = false; // Каждый новый вход в Dimmer начинается со строки LED
              dimmerRowLocked = false; // ...и с выбора строки, а не редактирования значения (энкодер)
              drawDimmerScreen();
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
        }
        break;
      }
      case IR_MUTE: {
        // Та же защита от "бита" пульта, что у IR_POWER (см. case IR_POWER ниже) — изредка
        // одно физическое нажатие шлёт два самостоятельных не-repeat кадра подряд. Без этой
        // защиты второй кадр сразу переключает Mute обратно — выглядит как "сработало не с
        // первого раза" (на самом деле сработало, но тут же откатилось). Окно короче, чем у
        // Power (800мс) — Mute, в отличие от Power, часто хочется переключать быстро туда-сюда,
        // и длинное окно само начинает глушить осознанные повторные нажатия
        static unsigned long lastMuteActionTime = 0;
        if (millis() - lastMuteActionTime > 200) {
          lastMuteActionTime = millis();
          Serial.println("Mute button pressed"); // Отладочный вывод
          isMuted = !isMuted; // Переключаем состояние Mute
          digitalWrite(RELAY_PIN_MUTE, isMuted ? HIGH : LOW); // Управляем реле Mute
          if (isMuted) {
            // Экран сразу займёт непрерывная анимация — см. animateMuteFrame() в main.cpp
            // (крутится, пока Mute включён), перерисовывать текущий экран смысла нет —
            // его тут же перекроет следующая же итерация loop(). resetMuteAnimation()
            // форсирует полную перерисовку на первом кадре (стирает то, что было на экране
            // до Mute) — далее animateMuteFrame() сама переключится на частичные тайлы
            resetMuteAnimation();
          } else {
            playUnmuteAnimation(); // Разовый переход обратно к обычному экрану — блокирует ~800мс
            // playUnmuteAnimation() не отдаёт управление в loop() всё это время, но приём ИК
            // работает по таймеру независимо от loop() — если за эти ~800мс пользователь успел
            // нажать что-то ещё (например Mute второй раз, не дождавшись), IRremote всё равно
            // это поймал и держит декодированным в своём буфере; без явного сброса здесь
            // handleRemoteInput() увидел бы этот "подвисший" кадр как новое нажатие сразу же на
            // следующей итерации loop() — то есть Mute мгновенно переключился бы обратно.
            // Отсюда и ощущение "нужно нажать несколько раз, чтобы попасть в нужный момент"
            if (IrReceiver.decode()) {
              IrReceiver.resume();
            }
            // Надпись "mute" рисуется на всех экранах (см. drawStatusIndicators() в
            // display_logic.cpp) — перерисовываем ТЕКУЩИЙ экран (не всегда drawMenu(), иначе
            // это скачок в карусель меню и обратно)
            if (!inSettingsMode) {
              drawMenu();
            } else if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
              drawToggleSwitch(settings[currentMenuItem] == 1);
            } else if (menuItems[currentMenuItem] == "Dimmer") {
              // Просто перерисовка текущего экрана после Unmute — dimmerEditingDisplay НЕ
              // сбрасываем (в отличие от свежего входа через Enter), строка остаётся той же
              drawDimmerScreen();
            } else if (menuItems[currentMenuItem] == "Color") {
              drawColorScreen(settings[currentMenuItem]);
            } else if (menuItems[currentMenuItem] == "Source") {
              drawSourceScreen(settings[currentMenuItem]);
            } else {
              drawArrowIndicator(settings[currentMenuItem], false, false);
            }
          }
        }
        break;
      }
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
          } else if (menuItems[currentMenuItem] == "Dimmer" && dimmerEditingDisplay) {
            // Up — выбрать верхнюю строку (яркость колец); внутри Dimmer Up/Down не
            // трогают Volume вообще (см. main.h у dimmerEditingDisplay)
            dimmerEditingDisplay = false;
            drawDimmerScreen();
          }
        } else {
          // На карусели (не в настройках) Up/Down крутят Volume прямо отсюда, без захода
          // в сам пункт — временно показываем экран Volume (см. beginVolumeOverlay()),
          // возвращаемся в карусель по тому же таймауту простоя мотора, что и обычно
          beginVolumeOverlay();
          cancelVolumeSeek();
          motorControl2(SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
          lastMotorInputTime = millis();
          drawArrowIndicator(0, true, false);
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
          } else if (menuItems[currentMenuItem] == "Dimmer" && !dimmerEditingDisplay) {
            // Down — выбрать нижнюю строку (яркость дисплея)
            dimmerEditingDisplay = true;
            drawDimmerScreen();
          }
        } else {
          beginVolumeOverlay();
          cancelVolumeSeek();
          motorControl2(-SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
          lastMotorInputTime = millis();
          drawArrowIndicator(0, false, true);
        }
        break;
      case IR_SET: {
        // Прямой шорткат переключения источника — не важно, где сейчас курсор карусели
        // или в каком экране настройки ты находишься, Set переключает Source на следующий
        // и показывает его название на весь экран (AUX/CD/DAT, см. sourceNames[] в
        // hardware_settings.h) на SOURCE_OVERLAY_DURATION_MS, потом сам возвращается туда,
        // где был экран до нажатия (см. beginSourceOverlay()/updateSourceOverlay() в
        // main.cpp). Та же защита от дубль-кадра, что у Enter/Mute — иначе одно нажатие
        // могло бы перескочить сразу на 2 источника
        static unsigned long lastSetActionTime = 0;
        if (millis() - lastSetActionTime > 200) {
          lastSetActionTime = millis();
          Serial.println("Set button pressed"); // Отладочный вывод
          beginSourceOverlay(); // Запоминает, куда вернуться, ДО того как currentMenuItem поменяется
          for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            if (menuItems[i] == "Source") {
              currentMenuItem = i;
              break;
            }
          }
          settings[currentMenuItem] = (settings[currentMenuItem] + 1) % SOURCE_COUNT;
          applySourceSelection();
          drawSourceScreen(settings[currentMenuItem]);
        }
        break;
      }
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
            powerOnDevices(); // Блокирует несколько секунд (анимация POWER ON)
            powerOff = false;
            // Та же ловушка "подвисшего" ИК-кадра, что у Unmute (см. case IR_MUTE выше) —
            // приём ИК работает по таймеру независимо от loop(), пока мы тут блокированы
            if (IrReceiver.decode()) {
              IrReceiver.resume();
            }
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
            // Та же ловушка — несколько секунд блокирующих вызовов выше могли накопить
            // "подвисший" кадр (например ещё одно Power, пока ждали выключения)
            if (IrReceiver.decode()) {
              IrReceiver.resume();
            }
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
