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
#include "animations/boot_animation.h"
#include "animations/bass_volume_high_animation.h"
#include "animations/mute_animation.h"
#include "animations/vu_meter_animation.h"
#include "animations/bypass_animation.h"
#include "animations/dimmer_animation.h"
#include "animations/color_animation.h"
#include "animations/source_animation.h"
#include "animations/eq_animation.h"
#include "animations/info_animation.h"
#include "temperature_sensor.h"

String menuItems[] = {"Bass", "High", "Volume", "VU Meter", "Bypass", "Dimmer", "Color", "Source", "EQ", "Info"};
int currentMenuItem = 0;
int settings[] = {0, 0, 0, 1, 0, VOLUME_RING_DEFAULT_DIMMER, RING_COLOR_DEFAULT, 0, 0, 0}; // VU Meter "включено", Bypass "выключено", Dimmer/Color колец, Source/EQ по умолчанию (EQ = Flat, индекс 0), Info не используется (нет редактируемого значения)
bool inSettingsMode = false;
bool isMuted = false; // Флаг для состояния Mute
unsigned long lastMotorInputTime = 0; // Момент последней команды на мотор Bass/High/Volume (для авто-стопа)
int displayBrightness = DISPLAY_BRIGHTNESS_DEFAULT_PERCENT; // Яркость дисплея, пункт "Dimmer"
bool dimmerEditingDisplay = false; // Какая строка внутри Dimmer сейчас активна (false = кольца)
bool dimmerRowLocked = false; // Подтверждена ли подсвеченная строка Dimmer кликом энкодера (см. main.h)

bool volumeOverlayActive = false;
static int volumeOverlaySavedMenuItem = 0;
static bool volumeOverlaySavedInSettingsMode = false;

static int volumeMenuIndex() {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    if (menuItems[i] == "Volume") {
      return i;
    }
  }
  return 0; // Не должно случаться — "Volume" всегда есть в menuItems[]
}

int dimmerMenuIndex() {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    if (menuItems[i] == "Dimmer") {
      return i;
    }
  }
  return 0; // Не должно случаться — "Dimmer" всегда есть в menuItems[]
}

int colorMenuIndex() {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    if (menuItems[i] == "Color") {
      return i;
    }
  }
  return 0; // Не должно случаться — "Color" всегда есть в menuItems[]
}

int sourceMenuIndex() {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    if (menuItems[i] == "Source") {
      return i;
    }
  }
  return 0; // Не должно случаться — "Source" всегда есть в menuItems[]
}

int vuMeterMenuIndex() {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    if (menuItems[i] == "VU Meter") {
      return i;
    }
  }
  return 0; // Не должно случаться — "VU Meter" всегда есть в menuItems[]
}

int eqMenuIndex() {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    if (menuItems[i] == "EQ") {
      return i;
    }
  }
  return 0; // Не должно случаться — "EQ" всегда есть в menuItems[]
}

// Перерисовывает экран, который сейчас должен быть виден по inSettingsMode/currentMenuItem —
// нужно, чтобы после отпускания Up/Down (глобальный шорткат громкости, см. beginVolumeOverlay())
// вернуть на экран именно то, что было до него (карусель или конкретный экран настройки)
void redrawCurrentScreen() {
  if (!inSettingsMode) {
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

// Вызывается из IR_UP/IR_DOWN (remote_control.cpp) при нажатии Up/Down где угодно, кроме
// как внутри Bass/High — временно подставляет currentMenuItem под "Volume", запомнив, куда
// вернуться (первый раз за сессию удержания; повторные вызовы во время уже активного оверлея
// не перезаписывают сохранённое состояние)
void beginVolumeOverlay() {
  if (!volumeOverlayActive) {
    volumeOverlaySavedMenuItem = currentMenuItem;
    volumeOverlaySavedInSettingsMode = inSettingsMode;
    volumeOverlayActive = true;
  }
  currentMenuItem = volumeMenuIndex();
}

// Откатывает currentMenuItem/inSettingsMode к тому, что было до beginVolumeOverlay(), и
// перерисовывает тот экран — вызывается автостопом мотора (main.cpp, loop()), когда Up/Down
// давно не приходили (т.е. кнопку отпустили)
void endVolumeOverlay() {
  if (!volumeOverlayActive) {
    return;
  }
  volumeOverlayActive = false;
  currentMenuItem = volumeOverlaySavedMenuItem;
  inSettingsMode = volumeOverlaySavedInSettingsMode;
  redrawCurrentScreen();
}

bool sourceOverlayActive = false;
static unsigned long sourceOverlayStart = 0;
static int sourceOverlaySavedMenuItem = 0;
static bool sourceOverlaySavedInSettingsMode = false;

// Вызывается из IR_SET (remote_control.cpp) — запоминает, куда вернуться (только при первом
// нажатии за сессию показа, повторные нажатия просто продлевают показ, не трогая
// сохранённое состояние), и запускает/перезапускает таймер на SOURCE_OVERLAY_DURATION_MS
void beginSourceOverlay() {
  if (!sourceOverlayActive) {
    sourceOverlaySavedMenuItem = currentMenuItem;
    sourceOverlaySavedInSettingsMode = inSettingsMode;
    sourceOverlayActive = true;
  }
  sourceOverlayStart = millis();
}

// Вызывается из loop() каждую итерацию — как только пройдёт SOURCE_OVERLAY_DURATION_MS с
// последнего нажатия Set, откатывает currentMenuItem/inSettingsMode и перерисовывает экран,
// что был виден до первого нажатия
void updateSourceOverlay() {
  if (sourceOverlayActive && millis() - sourceOverlayStart >= SOURCE_OVERLAY_DURATION_MS) {
    sourceOverlayActive = false;
    currentMenuItem = sourceOverlaySavedMenuItem;
    inSettingsMode = sourceOverlaySavedInSettingsMode;
    redrawCurrentScreen();
  }
}

void resetCursor() {
  if (menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High" || menuItems[currentMenuItem] == "Volume") {
    settings[currentMenuItem] = 0; // Сбрасываем положение курсора в ноль
  }
}

void saveSettings() {
  saveDimmerColorSettings(); // LED/Display/Color — см. on_off_logic.cpp
}

void loadSettings() {
  loadDimmerColorSettings(); // LED/Display/Color — см. on_off_logic.cpp
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

  // Восстанавливаем LED/Display/Color из EEPROM ДО того, как их значения впервые применятся
  // ниже (applyDisplayBrightness()/applyRingColorScheme()/initialRingBrightness) — иначе
  // экран/кольца на старте на мгновение показывали бы значения по умолчанию
  loadSettings();

  u8g2.begin();
  // VCOMH (0xDB) по умолчанию у SSD1309-драйвера в u8g2 — 0x20; сама библиотека в комментарии
  // к init-последовательности пишет, что 0x00 даёт максимальный диапазон для setContrast()
  // (issue #98) — без этого регулировка яркости дисплея была почти незаметна визуально
  u8g2.sendF("ca", 0x0DB, 0x000);
  applyDisplayBrightness(); // Стартовая яркость дисплея, пункт "Dimmer" (displayBrightness)
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
  pinMode(SOURCE_RELAY_4_PIN, OUTPUT);
  pinMode(LED_BASS_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_VOLUME_PIN, OUTPUT);
  pinMode(BYPASS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BYPASS_LED_PIN, OUTPUT);

  initRemoteControl();
  initTemperatureSensors(); // Датчики DS18B20, пункт меню "Info"
  attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_B_PIN), encoderISR, CHANGE);

  Serial.println("Setup complete"); // Отладочный вывод

  // Та же последовательность включения, что и при программном Power On с пульта (см.
  // on_off_logic.cpp) — восстанавливает Bypass и положение Bass/High из EEPROM, играет
  // анимацию "POWER ON", включает реле/светодиоды, рисует меню. Раньше физическое включение
  // (просто воткнули кабель) делало упрощённую версию вручную прямо здесь и НЕ восстанавливало
  // EEPROM вообще (Bypass всегда стартовал выключенным, Bass/High не доезжали до сохранённого
  // положения) — теперь оба пути включения ведут себя одинаково
  powerOnDevices();
}

void loop() {
  handleRemoteInput(); // Проверяет сигнал с ИК-пульта сама (IrReceiver.decode())

  updateSourceOverlay(); // Откатывает полноэкранный показ Source (Set с пульта) по таймеру

  // Пока Mute включён, экран целиком занят непрерывной MUTE-анимацией (см. ниже) — навигация
  // энкодером (вход/выход из настроек, смена пункта, вращение) недоступна по той же причине,
  // что и с пульта (см. remote_control.cpp) — не бороться за экран с этой анимацией. Пока
  // система выключена (powerOff) — тоже недоступна: должна работать только кнопка Power
  // с пульта, энкодер (вращение и кнопка) и физическая кнопка Bypass — не должны иметь эффекта
  if (!isMuted && !powerOff) {
    checkEncoderButton();
  }
  if (!powerOff) {
    checkBypassButton();
  }

  if (!powerOff) {
    updateBassHighRecenter(); // Автовозврат Bass/High в 0dB после переключения Bypass (или к сохранённому положению после включения питания)
    updateVolumeSeek(); // Автовозврат Volume к VOLUME_POWERON_TARGET_PERCENT после включения питания
  }

  if (isMuted || powerOff) {
    encoderValue = 0; // Отбрасываем накопленное вращение — навигация недоступна, пока включён Mute или система выключена
  } else if (encoderValue != 0) {
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
          cancelBassRecenter(); // Ручное управление энкодером отменяет автовозврат после Bypass
          motorControl(direction * SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
          lastMotorInputTime = millis();
          encoderValue = 0;
          drawArrowIndicator(0, showArrowRight, showArrowLeft);
        } else if (menuItems[currentMenuItem] == "High") {
          cancelHighRecenter(); // Ручное управление энкодером отменяет автовозврат после Bypass
          motorControl(direction * SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
          lastMotorInputTime = millis();
          encoderValue = 0;
          drawArrowIndicator(0, showArrowRight, showArrowLeft);
        } else if (menuItems[currentMenuItem] == "Volume") {
          cancelVolumeSeek(); // Ручное управление энкодером отменяет автовозврат к целевой громкости после включения питания
          motorControl2(direction * SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
          lastMotorInputTime = millis();
          encoderValue = 0;
          drawArrowIndicator(0, showArrowRight, showArrowLeft);
        } else if (menuItems[currentMenuItem] == "Dimmer") {
          if (!dimmerRowLocked) {
            // Строка ещё не подтверждена кликом энкодера (см. checkEncoderButton() в
            // encoder.cpp) — вращение просто двигает подсветку между LED (верх) и Display
            // (низ), значения не меняет. Пульт (Up/Down) на этот флаг не смотрит — там
            // выбор строки работает как раньше, независимо от энкодера
            if (encoderValue > 0) {
              dimmerEditingDisplay = true;
            } else if (encoderValue < 0) {
              dimmerEditingDisplay = false;
            }
          } else if (dimmerEditingDisplay) {
            displayBrightness = constrain(displayBrightness + encoderValue * 5, 0, 100);
            applyDisplayBrightness();
            saveSettings();
          } else {
            settings[currentMenuItem] = constrain(settings[currentMenuItem] + encoderValue * 5, 0, 100);
            applyRingDimmer();
            saveSettings();
          }
          encoderValue = 0;
          drawDimmerScreen();
        } else if (menuItems[currentMenuItem] == "Color") {
          settings[currentMenuItem] = ((settings[currentMenuItem] + direction) % RING_COLOR_COUNT + RING_COLOR_COUNT) % RING_COLOR_COUNT;
          encoderValue = 0;
          applyRingColorScheme();
          drawColorScreen(settings[currentMenuItem]);
          saveSettings();
        } else if (menuItems[currentMenuItem] == "Source") {
          settings[currentMenuItem] = ((settings[currentMenuItem] + direction) % SOURCE_COUNT + SOURCE_COUNT) % SOURCE_COUNT;
          encoderValue = 0;
          applySourceSelection();
          drawSourceScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "EQ") {
          settings[currentMenuItem] = ((settings[currentMenuItem] + direction) % EQ_COUNT + EQ_COUNT) % EQ_COUNT;
          encoderValue = 0;
          applyEqPreset(settings[currentMenuItem]);
          drawEqScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "Info") {
          // Нет редактируемого значения — просто гасим накопленное вращение, иначе
          // encoderValue никогда не обнулится и утащит "хвост" в следующий пункт меню
          encoderValue = 0;
        }
      }
    }
  }

  // Обновление светодиодов в режиме настройки (или во время временного показа Volume
  // с карусели через Up/Down — см. volumeOverlayActive)
  if (inSettingsMode || volumeOverlayActive) {
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

  // Крутящаяся иконка пункта меню в углу drawMenu(), пока пользователь сидит на карусели
  // (не зашёл в настройки). Частичное обновление тайлов (не весь экран) — см. подробности в
  // hardware_settings.h у MENU_ICON_* и в bass_volume_high_animation.h
  if (!isMuted && !inSettingsMode && !volumeOverlayActive && !sourceOverlayActive && !powerOff) {
    if (menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High" || menuItems[currentMenuItem] == "Volume") {
      animateBassVolumeHighIconPartial(MENU_ICON_X, MENU_ICON_Y);
    } else if (menuItems[currentMenuItem] == "VU Meter") {
      animateVuMeterIconPartial(MENU_ICON_X, MENU_ICON_Y);
    } else if (menuItems[currentMenuItem] == "Bypass") {
      animateBypassIconPartial(MENU_ICON_X, MENU_ICON_Y);
    } else if (menuItems[currentMenuItem] == "Dimmer") {
      animateDimmerIconPartial(MENU_ICON_X, MENU_ICON_Y);
    } else if (menuItems[currentMenuItem] == "Color") {
      animateColorIconPartial(MENU_ICON_X, MENU_ICON_Y);
    } else if (menuItems[currentMenuItem] == "Source") {
      animateSourceIconPartial(MENU_ICON_X, MENU_ICON_Y);
    } else if (menuItems[currentMenuItem] == "EQ") {
      animateEqIconPartial(EQ_ICON_X, EQ_ICON_Y);
    } else if (menuItems[currentMenuItem] == "Info") {
      animateInfoIconPartial(MENU_ICON_X, MENU_ICON_Y);
    }
  }

  // Непрерывная MUTE-анимация — занимает весь экран, пока Mute включён (см. mute_animation.h)
  if (isMuted) {
    animateMuteFrame();
  }

  // Кольца Bass/High/Volume светятся всегда (не только внутри своих пунктов меню), пока система не в Standby.
  // Bass/High пропускаются, пока играет разовая анимация переключения Bypass (см. блок ниже) —
  // она сама берёт на себя отображение этих двух колец, пока активна
  static unsigned long lastRingUpdate = 0;
  // Придерживаем обновление колец на MENU_NAVIGATION_RING_PAUSE_MS после команды НАВИГАЦИИ
  // по меню (Right/Left/Enter/Set) — см. подробный комментарий у MENU_NAVIGATION_RING_PAUSE_MS
  // (hardware_settings.h). НЕ применяется к Up/Down (та команда не трогает
  // lastMenuNavigationTime() вообще) — там кольца обязаны жить в реальном времени
  bool recentMenuNavigation = millis() - lastMenuNavigationTime() < MENU_NAVIGATION_RING_PAUSE_MS;
  if (!powerOff && !recentMenuNavigation && millis() - lastRingUpdate >= RING_UPDATE_INTERVAL_MS) {
    lastRingUpdate = millis();
    updateVolumeRing(readVolumePotPercent());
    if (bypassAnimMode == 0) {
      renderDbRing(bassRing, readBassPotPercent(), bassRingState);
      renderDbRing(highRing, readHighPotPercent(), highRingState);
    }
    // IRremote игнорирует новый сигнал, пока явно не вызван resume() (внутри
    // handleRemoteInput()) — этот блок с усреднением потенциометров занимает заметное
    // время (до ~15-20мс), проверяем пульт сразу после, а не только в начале loop()
    handleRemoteInput();
  }

  // Во время анимации мигания (возврат в 0dB, дыхание Volume выше середины шкалы)
  // кольцу нужно обновляться чаще для плавности — но ТОЛЬКО пока анимация активна,
  // и без перечтения потенциометра (дёшево: просто пересчёт яркости уже известного
  // значения + show())
  static unsigned long lastBlinkRender = 0;
  // Тот же MENU_NAVIGATION_RING_PAUSE_MS, что и у основного блока колец выше — эта анимация
  // тоже дёргает NeoPixel.show() (renderDbRing()/renderVolumeRingBreath()), просто чаще (раз
  // в 30мс, а не 100мс), и раньше её этим не защищали
  if (!powerOff && !recentMenuNavigation && millis() - lastBlinkRender >= 30) {
    if (bypassAnimMode == 0) {
      if (dbRingBlinking(bassRingState)) {
        renderDbRing(bassRing, bassRingState.lastValue, bassRingState);
      }
      if (dbRingBlinking(highRingState)) {
        renderDbRing(highRing, highRingState.lastValue, highRingState);
      }
    }
    if (volumeRingBreathing()) {
      renderVolumeRingBreath();
    }
    lastBlinkRender = millis();
  }

  // Анимация, привязанная к состоянию Bypass, см. checkBypassButton()/triggerBypassAnim().
  // Режим 1 (заливка, Bypass выключается) — разовая, конечная: по истечении гасим флаг
  // и сразу возвращаем обычное отображение уровня дБ (без ожидания следующего 200мс тика).
  // Режим 2 (мигание красным, Bypass включён) — держится ВСЁ время, пока Bypass включён,
  // и сам не заканчивается: заканчивает его только повторный triggerBypassAnim() при
  // выключении Bypass (переводит в режим 1)
  // Тот же MENU_NAVIGATION_RING_PAUSE_MS — эта анимация тоже дёргает NeoPixel.show()
  // (renderDbRing()/renderBypassFillAnim()/renderBypassBlinkAnim())
  if (!powerOff && !recentMenuNavigation && bypassAnimMode != 0) {
    unsigned long elapsed = millis() - bypassAnimStart;
    if (bypassAnimMode == 1 && elapsed >= BYPASS_FILL_TOTAL_MS) {
      bypassAnimMode = 0;
      renderDbRing(bassRing, readBassPotPercent(), bassRingState);
      renderDbRing(highRing, readHighPotPercent(), highRingState);
    } else {
      static unsigned long lastBypassAnimRender = 0;
      if (millis() - lastBypassAnimRender >= 30) {
        lastBypassAnimRender = millis();
        if (bypassAnimMode == 1) {
          renderBypassFillAnim(bassRing, elapsed);
          renderBypassFillAnim(highRing, elapsed);
        } else {
          renderBypassBlinkAnim(bassRing, elapsed, readBassPotPercent());
          renderBypassBlinkAnim(highRing, elapsed, readHighPotPercent());
        }
      }
    }
  }

  // Моторы Bass/High/Volume мгновенно останавливаются, если давно не было новых команд от
  // энкодера/пульта — тем же таймаутом заканчивается и временный показ Volume с карусели
  // через Up/Down (см. volumeOverlayActive), раз новых команд на мотор Volume нет
  if ((inSettingsMode || volumeOverlayActive) && millis() - lastMotorInputTime > SLIDER_MOTOR_IDLE_TIMEOUT) {
    if (menuItems[currentMenuItem] == "Bass") {
      motorControl(0, MOTOR1_IN, MOTOR1_PWM);
    } else if (menuItems[currentMenuItem] == "High") {
      motorControl(0, MOTOR2_IN, MOTOR2_PWM);
    } else if (menuItems[currentMenuItem] == "Volume") {
      motorControl2(0, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
      if (volumeOverlayActive) {
        endVolumeOverlay();
      }
    }
  }

  // Живое обновление экрана с положением ручки Bass/High/Volume (реже, экрану такая частота не нужна)
  static unsigned long lastPotUpdate = 0;
  if (!isMuted && (inSettingsMode || volumeOverlayActive) &&
      (menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High" || menuItems[currentMenuItem] == "Volume") &&
      millis() - lastPotUpdate >= 200) {
    lastPotUpdate = millis();
    drawArrowIndicator(0, false, false);
    handleRemoteInput(); // См. комментарий у блока обновления колец выше
  }

  // Живое обновление температур на экране Info, пока он открыт — readAllTemperatures()
  // не блокирует (см. temperature_sensor.cpp), поэтому handleRemoteInput() здесь не
  // обязателен так же, как у блоков выше, но не помешает
  static unsigned long lastInfoUpdate = 0;
  if (!isMuted && inSettingsMode && menuItems[currentMenuItem] == "Info" &&
      millis() - lastInfoUpdate >= INFO_UPDATE_INTERVAL_MS) {
    lastInfoUpdate = millis();
    drawInfoScreen();
  }
}
