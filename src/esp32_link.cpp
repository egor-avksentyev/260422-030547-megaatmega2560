#include "esp32_link.h"
#include "hardware_settings.h"
#include "main.h"
#include "encoder.h"
#include "display_logic.h"
#include "relay.h"
#include "animation_logic.h"
#include "motor_driver_logic.h"
#include "motor_position.h"
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

// Общий вход в настройку текущего пункта (currentMenuItem уже должен быть выставлен) — то,
// что раньше было только внутри case 'E' (ветка !inSettingsMode), вынесено сюда для
// переиспользования в executeWebJump() (CMD:J, Фаза 4 KnobControl) ниже
static void enterSettingsModeForCurrentItem() {
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
}

// Общий выход из настройки обратно в карусель — раньше только внутри case 'E' (ветка
// inSettingsMode), вынесено сюда для переиспользования в case 'X' (Фаза 4 KnobControl) ниже
static void exitSettingsMode() {
  inSettingsMode = false;
  resetCursor();
  encoderValue = 0;
  stopAllMotors();
  drawMenu();
}

// CMD:J<index> (Фаза 4 KnobControl, см. esp32_link.h) — в отличие от остальных букв, несёт
// числовой параметр, поэтому не влезает в switch(letter) у executeWebCommand() ниже и
// разбирается отдельно, в handleLine(). Гейты (powerOff/isMuted/nowPlayingActive) — те же,
// что в начале executeWebCommand(), продублированы здесь по той же причине (CLAUDE.md, "два
// независимых источника ввода... при изменении поведения пункта меню правь оба пути
// параллельно" — то же дублирование, что и у остальных команд в этом файле). Разница с 'E' —
// 'J' всегда реально прыгает и входит в указанный пункт, даже если полноэкранный Now Playing
// был открыт (тот выходит в меню и возвращает, не входя никуда)
static void executeWebJump(int index) {
  if (index < 0 || index >= MENU_ITEM_COUNT) {
    return;
  }
  if (nowPlayingActive && !nowPlayingMenuVisitActive) {
    exitNowPlayingToMenu();
  } else {
    refreshNowPlayingMenuActivity();
  }
  if (powerOff || isMuted) {
    return;
  }
  currentMenuItem = index;
  enterSettingsModeForCurrentItem();
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
      } else if (menuItems[currentMenuItem] == "Info" && settings[currentMenuItem] == INFO_STREAMER_ROW_INDEX) {
        // Фаза 4 KnobControl — тот же путь, что уже есть у IR_RIGHT в remote_control.cpp,
        // продублирован сюда (третий независимый источник ввода, см. CLAUDE.md), раньше здесь
        // не было вообще — у CMD: не было способа переключить Streamer
        streamerRelayOn = true;
        applyStreamerRelay();
        drawInfoScreen();
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
      } else if (menuItems[currentMenuItem] == "Info" && settings[currentMenuItem] == INFO_STREAMER_ROW_INDEX) {
        // Фаза 4 KnobControl — симметрично 'R' выше
        streamerRelayOn = false;
        applyStreamerRelay();
        drawInfoScreen();
      }
      break;

    case 'E':
      if (!inSettingsMode) {
        enterSettingsModeForCurrentItem();
      } else {
        exitSettingsMode();
      }
      break;

    case 'X':
      // Фаза 4 KnobControl — явный выход, без входа обратно, если уже снаружи (в отличие от
      // 'E' тут нет второго смысла)
      if (inSettingsMode) {
        exitSettingsMode();
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
      esp32LinkSendMute(isMuted); // Сразу, тем же принципом, что esp32LinkSendPower() у Power
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
    // CMD:J<index> (Фаза 4 KnobControl) несёт числовой параметр после буквы — разобрать его
    // здесь, до executeWebCommand() (та видит только один char, ей тут делать нечего)
    if (line[4] == 'J' && line[5] != '\0') {
      executeWebJump(atoi(line + 5));
    } else {
      executeWebCommand(line[4]);
    }
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

// --- KnobControl Фаза 4 — см. esp32_link.h за полным форматом каждой телеграммы ---

// Нет готового bypassMenuIndex() (в отличие от dimmerMenuIndex()/colorMenuIndex()/
// sourceMenuIndex()/vuMeterMenuIndex()/eqMenuIndex() в main.h) — остальные пять пунктов этот
// индекс никогда не искали по имени за пределами текущего пункта, Bypass здесь первый. Тот же
// линейный поиск по имени, что уже в этом файле есть у case 'S' (строка с Source выше)
static int bypassMenuIndex() {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    if (menuItems[i] == "Bypass") {
      return i;
    }
  }
  return 4; // Не должно случиться — "Bypass" всегда есть в menuItems[], см. main.cpp
}

// MENU:<item>:<inSettingsMode 0|1>:<value...>\n — вызывается БЕЗУСЛОВНО, периодически,
// независимо от inSettingsMode (см. main.cpp/loop()): поле inSettingsMode само несёт этот
// факт, и его переход 1→0 — единственный сигнал, по которому кноп узнаёт "я вышла из
// настройки" и откатывается в свою карусель (см. esp32_link.h, "Живое зеркало только внутри
// конкретного экрана"). Если бы телеграмма вообще переставала приходить при выходе, кноп
// просто навсегда застрял бы на последнем известном значении, не имея сигнала к откату —
// ESP32-компаньон по архитектуре "чистый проброс без своей логики", ему неоткуда взять такой
// сигнал самому
void esp32LinkSendMenu() {
  // beginVolumeOverlay() (main.h/main.cpp) — глобальный шорткат громкости с пульта/CMD: —
  // временно подставляет currentMenuItem под Volume, но НЕ трогает inSettingsMode (если
  // пользователь висел на обычной карусели, оно так и останется false на время оверлея).
  // Без этой поправки MENU: отправляла бы "Volume:0:" (без значения) в самом частом случае
  // применения оверлея — кноп остался бы без живого дБ ровно тогда, когда они нужнее всего.
  // volumeOverlayActive — такой же законный повод показать значение, как и настоящий
  // inSettingsMode, тут это один и тот же случай "Mega прямо сейчас показывает Volume
  // пользователю", просто два разных способа туда попасть
  bool showingValue = inSettingsMode || volumeOverlayActive;
  Serial2.print("MENU:");
  Serial2.print(menuItems[currentMenuItem]);
  Serial2.print(':');
  Serial2.print(showingValue ? '1' : '0');
  Serial2.print(':');
  if (!showingValue) {
    Serial2.println();
    return;
  }
  if (menuItems[currentMenuItem] == "Bass") {
    // Живое показание потенциометра, НЕ settings[] — см. esp32_link.h за тем, почему это
    // важное отличие от SETTINGS: ниже
    Serial2.println(readBassPotPercent());
  } else if (menuItems[currentMenuItem] == "High") {
    Serial2.println(readHighPotPercent());
  } else if (menuItems[currentMenuItem] == "Volume") {
    Serial2.println(readVolumePotPercent());
  } else if (menuItems[currentMenuItem] == "Dimmer") {
    Serial2.print(displayBrightness);
    Serial2.print(':');
    Serial2.print(settings[currentMenuItem]);
    Serial2.print(':');
    Serial2.print(dimmerEditingDisplay ? '1' : '0');
    Serial2.print(':');
    Serial2.println(dimmerRowLocked ? '1' : '0');
  } else if (menuItems[currentMenuItem] == "Info") {
    Serial2.println(); // Без значения вообще, см. esp32_link.h
  } else {
    // VU Meter/Bypass (0/1), Source/Color/EQ (индекс пресета) — одно число, прямо из settings[]
    Serial2.println(settings[currentMenuItem]);
  }
}

// SETTINGS:<bassDb>:<highDb>:<volumePct>:<sourceIdx>:<eqIdx>:<colorIdx>:<bypass 0|1>:
// <vu 0|1>\n — КОМАНДНАЯ цель каждого пункта (settings[] напрямую), не живые показания
// потенциометров, см. esp32_link.h за тем, почему это оправданное отличие от MENU: выше
//
// Найден живьём (2026-10-21): это было верно для Source/EQ/Color/Bypass/VU (им правда есть
// куда писать — settings[currentMenuItem] реально обновляется при вводе, см. esp32_link.cpp/
// remote_control.cpp/main.cpp), но НЕ для Bass/High/Volume — их слоты settings[0]/[1]/[2] не
// присваиваются вообще НИГДЕ в прошивке (grep по всему репозиторию это подтвердил), кроме как
// читаются здесь. Останутся нулями всю жизнь прошивки — кноп (KnobControl) показывал на
// HomeIdle "Bass --"/дальше навсегда 0, хотя реальная ручка физически крутится и MENU: это
// показывает корректно (readBassPotPercent() и т.п., та же пара функций, что и ниже у
// esp32LinkSendMenu()). Фикс — те же живые показания потенциометра сюда тоже, раз единственная
// альтернатива (settings[]) для этих трёх пунктов — мёртвый код, который никогда не был живым.
void esp32LinkSendSettingsSnapshot() {
  Serial2.print("SETTINGS:");
  Serial2.print(readBassPotPercent()); // Bass — живое показание, settings[0] никогда не пишется
  Serial2.print(':');
  Serial2.print(readHighPotPercent()); // High — живое показание, settings[1] никогда не пишется
  Serial2.print(':');
  Serial2.print(readVolumePotPercent()); // Volume — живое показание, settings[2] никогда не пишется
  Serial2.print(':');
  Serial2.print(settings[sourceMenuIndex()]);
  Serial2.print(':');
  Serial2.print(settings[eqMenuIndex()]);
  Serial2.print(':');
  Serial2.print(settings[colorMenuIndex()]);
  Serial2.print(':');
  Serial2.print(settings[bypassMenuIndex()]);
  Serial2.print(':');
  Serial2.println(settings[vuMeterMenuIndex()]);
}

void esp32LinkSendMuteAndStreamer() {
  esp32LinkSendMute(isMuted);
  Serial2.print("STREAMER:");
  Serial2.println(streamerRelayOn ? '1' : '0');
}

void esp32LinkSendMute(bool muted) {
  Serial2.print("MUTE:");
  Serial2.println(muted ? '1' : '0');
}
