#include "esp32_link.h"
#include "hardware_settings.h"
#include "main.h"
#include "encoder.h"
#include "display_logic.h"
#include "relay.h"
#include "animation_logic.h"
#include "motor_driver_logic.h"
#include "neopixel.h"
#include "on_off_logic.h"
#include "animations/mute_animation.h"
#include "animations/unmute_animation.h"
#include <string.h>

static char lineBuf[64];
static uint8_t lineLen = 0;

static char nowPlayingText[ESP32_LINK_META_MAX_LEN + 1] = "";
static char streamingSource[ESP32_LINK_SOURCE_MAX_LEN + 1] = "";
static char controlIp[16] = ""; // "255.255.255.255\0" — максимум для IPv4-строки
static bool playing = false;
static bool arylicKnown = false;
static bool arylicOk = false;
static long trackPosMs = 0;
static long trackLenMs = 0;
static unsigned long trackPosCaptureMillis = 0;

// Перерисовывает экран, который сейчас должен быть виден после действия, не завязанного на
// конкретный пункт меню (Mute/Power) — тот же принцип выбора экрана, что в
// remote_control.cpp у IR_MUTE/IR_ENTER, продублирован здесь для третьего источника ввода
// (см. CLAUDE.md, "два независимых источника ввода" — то же дублирование, но уже на троих)
static void redrawAfterMuteOrPower() {
  if (nowPlayingActive && !nowPlayingMenuVisitActive) {
    drawNowPlayingScreen();
  } else if (!inSettingsMode) {
    drawMenu();
  } else if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
    drawToggleSwitch(settings[currentMenuItem] == 1);
  } else if (menuItems[currentMenuItem] == "Dimmer") {
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
    drawArrowIndicator(settings[currentMenuItem], false, false);
  }
}

// Диспетчер команд с веб-страницы (CMD:<letter>, см. esp32_link.h) — letter один из
// R/L/E/U/D/M/P/S (right/left/enter/up/down/mute/power/set), тот же словарь, что
// WEB_ACTIONS в web_control.cpp на ESP32. Намеренно НЕ переиспользует switch в
// handleRemoteInput() (remote_control.cpp) — тот полон RC5-специфичных вещей (repeat-кадры,
// антидребезг под конкретные тайминги пульта), не имеющих отношения к командам с веб-
// страницы. Вместо этого — третья по счёту дублированная реализация тех же действий, по
// тому же принципу, что уже описан в CLAUDE.md для пульта/энкодера ("два независимых
// источника ввода... при изменении поведения пункта меню правь оба пути параллельно" —
// теперь путей три, не два). Никакого антидребезга по времени не заведено — в отличие от
// RC5, у этого канала нет проблемы "то же физическое нажатие пришло дважды кадрами", каждая
// строка CMD: — уже одно осознанное действие с браузерной стороны (см. web_control.cpp)
static void executeWebCommand(char letter) {
  if (nowPlayingActive && !nowPlayingMenuVisitActive) {
    // Тот же гейт, что в remote_control.cpp/encoder.cpp — Enter выпускает в меню, Mute/Power
    // работают всегда, Up/Down тоже пропущены (регулировка громкости с веб-страницы во время
    // Now Playing — см. тот же пропуск для IR_UP/IR_DOWN в remote_control.cpp, раньше здесь не
    // был продублирован, из-за чего громкость с пульта работала, а с веб-страницы — нет),
    // остальное игнорируется, пока показан полноэкранный Now Playing
    if (letter == 'E') {
      exitNowPlayingToMenu();
      return;
    }
    if (letter != 'M' && letter != 'P' && letter != 'U' && letter != 'D') {
      return;
    }
  } else {
    refreshNowPlayingMenuActivity();
  }

  if (powerOff && letter != 'P') {
    return;
  }
  if (isMuted && letter != 'M' && letter != 'P') {
    return;
  }

  switch (letter) {
    case 'R':
      if (!inSettingsMode) {
        currentMenuItem = (currentMenuItem + 1) % MENU_ITEM_COUNT;
        drawMenu();
      } else if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
        settings[currentMenuItem] = 1;
        drawToggleSwitch(true);
        if (menuItems[currentMenuItem] == "VU Meter") {
          digitalWrite(RELAY_PIN_VU_METER, HIGH);
        } else {
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
      }
      break;

    case 'L':
      if (!inSettingsMode) {
        currentMenuItem = (currentMenuItem - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        drawMenu();
      } else if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
        settings[currentMenuItem] = 0;
        drawToggleSwitch(false);
        if (menuItems[currentMenuItem] == "VU Meter") {
          digitalWrite(RELAY_PIN_VU_METER, LOW);
        } else {
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
      }
      break;

    case 'E':
      if (!inSettingsMode) {
        inSettingsMode = true;
        if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
          drawToggleSwitch(settings[currentMenuItem] == 1);
        } else if (menuItems[currentMenuItem] == "Dimmer") {
          dimmerEditingDisplay = false;
          dimmerRowLocked = false;
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

    case 'U':
      if (inSettingsMode) {
        if (menuItems[currentMenuItem] == "Bass") {
          cancelBassRecenter();
          motorControl(SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
          lastMotorInputTime = millis();
          drawArrowIndicator(0, true, false);
        } else if (menuItems[currentMenuItem] == "High") {
          cancelHighRecenter();
          motorControl(SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
          lastMotorInputTime = millis();
          drawArrowIndicator(0, true, false);
        } else if (menuItems[currentMenuItem] == "Volume") {
          cancelVolumeSeek();
          motorControl2(SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
          lastMotorInputTime = millis();
          drawArrowIndicator(0, true, false);
        } else if (menuItems[currentMenuItem] == "Dimmer" && dimmerEditingDisplay) {
          dimmerEditingDisplay = false;
          drawDimmerScreen();
        } else if (menuItems[currentMenuItem] == "Source") {
          settings[currentMenuItem] = (settings[currentMenuItem] - 1 + SOURCE_COUNT) % SOURCE_COUNT;
          applySourceSelection();
          drawSourceScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "EQ") {
          settings[currentMenuItem] = (settings[currentMenuItem] - 1 + EQ_COUNT) % EQ_COUNT;
          applyEqPreset(settings[currentMenuItem]);
          drawEqScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "Info") {
          settings[currentMenuItem] = constrain(settings[currentMenuItem] - 1, 0, INFO_ROW_COUNT - INFO_LIST_VISIBLE_ROWS);
          drawInfoScreen();
        }
      } else {
        beginVolumeOverlay();
        cancelVolumeSeek();
        motorControl2(SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
        lastMotorInputTime = millis();
        drawArrowIndicator(0, true, false);
      }
      break;

    case 'D':
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
          dimmerEditingDisplay = true;
          drawDimmerScreen();
        } else if (menuItems[currentMenuItem] == "Source") {
          settings[currentMenuItem] = (settings[currentMenuItem] + 1) % SOURCE_COUNT;
          applySourceSelection();
          drawSourceScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "EQ") {
          settings[currentMenuItem] = (settings[currentMenuItem] + 1) % EQ_COUNT;
          applyEqPreset(settings[currentMenuItem]);
          drawEqScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "Info") {
          settings[currentMenuItem] = constrain(settings[currentMenuItem] + 1, 0, INFO_ROW_COUNT - INFO_LIST_VISIBLE_ROWS);
          drawInfoScreen();
        }
      } else {
        beginVolumeOverlay();
        cancelVolumeSeek();
        motorControl2(-SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
        lastMotorInputTime = millis();
        drawArrowIndicator(0, false, true);
      }
      break;

    case 'S':
      beginSourceOverlay();
      for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (menuItems[i] == "Source") {
          currentMenuItem = i;
          break;
        }
      }
      settings[currentMenuItem] = (settings[currentMenuItem] + 1) % SOURCE_COUNT;
      applySourceSelection();
      drawSourceScreen(settings[currentMenuItem]);
      break;

    case 'M':
      isMuted = !isMuted;
      digitalWrite(RELAY_PIN_MUTE, isMuted ? HIGH : LOW);
      if (isMuted) {
        resetMuteAnimation();
      } else {
        playUnmuteAnimation(); // Блокирует ~800мс — как и в remote_control.cpp/IR_MUTE
        redrawAfterMuteOrPower();
      }
      break;

    case 'P':
      if (powerOff) {
        powerOnDevices();
        powerOff = false;
      } else {
        // Сразу, до многосекундной анимации POWER OFF ниже — иначе ESP32 (и веб-страница)
        // узнают о выключении на несколько секунд позже реального нажатия (см. esp32_link.h)
        esp32LinkSendPower(false);
        digitalWrite(LED_BASS_PIN, LOW);
        digitalWrite(LED_HIGH_PIN, LOW);
        digitalWrite(LED_VOLUME_PIN, LOW);
        saveBypassStateOnShutdown();
        saveBassHighPositionOnShutdown();
        saveSourceStateOnShutdown();
        saveVuMeterStateOnShutdown();
        saveEqStateOnShutdown();
        saveStreamerStateOnShutdown(); // Была пропущена в этой копии — есть в remote_control.cpp/IR_POWER, здесь нет; добавлено заодно
        saveDimmerColorSettings();
        seekBassHighVolumeToZeroBlocking();
        delay(100);
        powerOffScreen();
        delay(3000);
        powerOffDevices();
        powerOff = true;
      }
      break;

    default:
      Serial.print("[esp32_link] неизвестная команда CMD: ");
      Serial.println(letter);
      break;
  }
}

static void handleLine(char* line) {
  if (strncmp(line, "META:", 5) == 0) {
    strncpy(nowPlayingText, line + 5, ESP32_LINK_META_MAX_LEN);
    nowPlayingText[ESP32_LINK_META_MAX_LEN] = '\0';
  } else if (strncmp(line, "IP:", 3) == 0) {
    strncpy(controlIp, line + 3, sizeof(controlIp) - 1);
    controlIp[sizeof(controlIp) - 1] = '\0';
  } else if (strncmp(line, "PLAY:", 5) == 0) {
    // Строго "PLAY:0"/"PLAY:1" целиком (6 символов), не просто один байт после префикса —
    // подтверждено живьём 2026-09-18: strip.show() у NeoPixel на AVR отключает прерывания на
    // время передачи (обязательное требование тайминга WS2812), а приём UART живёт на
    // прерывании — на 115200 бод 64-байтный буфер Serial2 наполняется всего за ~5мс, этого
    // достаточно, чтобы редкое совпадение с обновлением колец (каждые ~200мс, см. main.cpp)
    // потеряло несколько байт и склеило "PLAY:1" со следующим "POS:..." в "PLAY:POS4874186848"
    // — line[5] тогда становится 'P' от "POS", что раньше читалось как "не 1" = false. Битую
    // строку теперь просто игнорируем, а не гадаем по одному байту
    if ((line[5] == '0' || line[5] == '1') && line[6] == '\0') {
      playing = (line[5] == '1');
    }
  } else if (strncmp(line, "ARYLIC:", 7) == 0) {
    arylicKnown = true;
    arylicOk = (strcmp(line + 7, "OK") == 0);
  } else if (strncmp(line, "SRC:", 4) == 0) {
    strncpy(streamingSource, line + 4, ESP32_LINK_SOURCE_MAX_LEN);
    streamingSource[ESP32_LINK_SOURCE_MAX_LEN] = '\0';
  } else if (strncmp(line, "POS:", 4) == 0) {
    char* separator = strchr(line + 4, ':');
    if (separator) {
      *separator = '\0';
      trackPosMs = atol(line + 4);
      trackLenMs = atol(separator + 1);
      trackPosCaptureMillis = millis();
    }
  } else if (strncmp(line, "CMD:", 4) == 0 && line[4] != '\0') {
    executeWebCommand(line[4]);
  }
}

void esp32LinkInit() {
  Serial2.begin(ESP32_LINK_BAUD);
}

void esp32LinkPoll() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        handleLine(lineBuf);
      }
      lineLen = 0;
      continue;
    }
    if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
    // Переполнение строки (не должно случаться при нормальной работе) — просто отбрасывает
    // лишние символы, ждём '\n', чтобы разобраться со следующей строкой с чистого листа
  }
}

bool esp32LinkIsPlaying() {
  return playing;
}

const char* esp32LinkNowPlayingText() {
  return nowPlayingText;
}

const char* esp32LinkStreamingSource() {
  return streamingSource;
}

long esp32LinkTrackPosMs() {
  return trackPosMs;
}

long esp32LinkTrackLenMs() {
  return trackLenMs;
}

unsigned long esp32LinkTrackPosAgeMs() {
  return millis() - trackPosCaptureMillis;
}

const char* esp32LinkControlIp() {
  return controlIp;
}

bool esp32LinkArylicKnown() {
  return arylicKnown;
}

bool esp32LinkArylicOk() {
  return arylicOk;
}

void esp32LinkSendPower(bool poweredOn) {
  Serial2.print("POWER:");
  Serial2.println(poweredOn ? '1' : '0');
}

void esp32LinkSendSensors(const float temps[3], int voltage) {
  // dtostrf(), не snprintf("%f"...) — тот же приём, что в display_logic.cpp: avr-libc по
  // умолчанию собран без поддержки float в *printf
  char t1[8], t2[8], t3[8];
  dtostrf(temps[0], 1, 1, t1);
  dtostrf(temps[1], 1, 1, t2);
  dtostrf(temps[2], 1, 1, t3);
  Serial2.print("TEMP:");
  Serial2.print(t1);
  Serial2.print(':');
  Serial2.print(t2);
  Serial2.print(':');
  Serial2.println(t3);
  Serial2.print("VOLT:");
  Serial2.println(voltage);
}
