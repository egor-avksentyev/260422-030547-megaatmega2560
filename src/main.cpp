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
#include "esp32_link.h"

String menuItems[] = {"Bass", "High", "Volume", "VU Meter", "Bypass", "Dimmer", "Color", "Source", "EQ", "Info"};
int currentMenuItem = 0;
int settings[] = {0, 0, 0, 1, 0, VOLUME_RING_DEFAULT_DIMMER, RING_COLOR_DEFAULT, 0, 0, 0}; // VU Meter "включено", Bypass "выключено", Dimmer/Color колец, Source/EQ по умолчанию (EQ = Flat, индекс 0), Info — курсор списка строк (см. drawInfoScreen())
bool inSettingsMode = false;
bool isMuted = false; // Флаг для состояния Mute
bool streamerRelayOn = false; // Реле Streamer (строка "Streamer" в Info) — см. main.h
bool infoRowLocked = false; // Выбор строки <-> переключение Streamer энкодером внутри Info — см. main.h
unsigned long lastMotorInputTime = 0; // Момент последней команды на мотор Bass/High/Volume (для авто-стопа)
int displayBrightness = DISPLAY_BRIGHTNESS_DEFAULT_PERCENT; // Яркость дисплея, пункт "Dimmer"
bool dimmerEditingDisplay = false; // Какая строка внутри Dimmer сейчас активна (false = кольца)
bool dimmerRowLocked = false; // Подтверждена ли подсвеченная строка Dimmer кликом энкодера (см. main.h)

bool volumeOverlayActive = false;
static int volumeOverlaySavedMenuItem = 0;
static bool volumeOverlaySavedInSettingsMode = false;

// Полноэкранный показ положения ручки (drawArrowIndicator(), тот же экран, что и настоящий
// вход в настройки), когда пользователь крутит Bass/High/Volume РУКОЙ, сидя на карусели
// меню — см. блок обнаружения в loop(). currentMenuItem временно подставляется под нужный
// пункт (Bass=0/High=1/Volume=2 — те же индексы, что в menuItems[]), как и у
// beginVolumeOverlay(), но с собственным сохранённым состоянием и своим (не моторным)
// таймаутом простоя (KNOB_OVERLAY_IDLE_TIMEOUT_MS) — не переиспользует volumeOverlayActive,
// у той свой смысл (глобальный шорткат Up/Down) и своё условие выхода. -1 = не активен,
// 0/1/2 = Bass/High/Volume
static int knobIndicatorActiveItem = -1;
static unsigned long knobIndicatorLastMovementTime = 0;
static int knobOverlaySavedMenuItem = 0; // currentMenuItem на карусели ДО того, как рука взялась за ручку
// Сентинел вместо INT32_MIN — int на AVR 16-битный (-32768..32767), реальные значения
// (дБ -10..10 или % 0..100) никогда не окажутся рядом с этим числом
static int knobOverlayLastDrawnValue = -32000; // не перерисовывать экран, пока значение не изменилось

// Now Playing (esp32_link.h) — см. main.h за смыслом обоих флагов
bool nowPlayingActive = false;
bool nowPlayingMenuVisitActive = false;
static unsigned long nowPlayingMenuVisitLastActivity = 0;
// Было ли реле Streamer уже включено ДО того, как Arylic начал играть — восстанавливается,
// когда воспроизведение останавливается (см. updateNowPlaying() ниже). Streamer — независимое
// реле (main.h/relay.cpp), не часть Source, поэтому здесь запоминается именно его состояние,
// а не какой-то пункт Source
static bool streamerWasOnBeforePlayback = false;

void exitNowPlayingToMenu() {
  nowPlayingMenuVisitActive = true;
  nowPlayingMenuVisitLastActivity = millis();
  inSettingsMode = false;
  drawMenu();
}

void refreshNowPlayingMenuActivity() {
  if (nowPlayingMenuVisitActive) {
    nowPlayingMenuVisitLastActivity = millis();
  }
}

// Опрашивает esp32_link (esp32LinkPoll() дёргает вызывающая сторона отдельно — здесь только
// реакция на изменение состояния), включает/выключает реле Streamer по фронтам PLAY:0/1,
// перерисовывает полноэкранный Now Playing, когда меняется метадата, и возвращает из
// "визита" в меню по таймауту простоя. Вызывается из loop() пока !powerOff — см. там же
static void updateNowPlaying() {
  static bool wasPlaying = false;
  static char lastRenderedText[ESP32_LINK_META_MAX_LEN + 1] = "";
  static char lastRenderedSource[ESP32_LINK_SOURCE_MAX_LEN + 1] = "";

  bool playingNow = esp32LinkIsPlaying();

  if (playingNow && !wasPlaying) {
    // Восходящий фронт: Arylic начал играть — запоминаем, было ли реле Streamer уже
    // включено (тогда восстановление ниже будет безобидным no-op), и включаем его
    streamerWasOnBeforePlayback = streamerRelayOn;
    streamerRelayOn = true;
    applyStreamerRelay();
    nowPlayingActive = true;
    nowPlayingMenuVisitActive = false;
    // Если стрим начался, пока пользователь сидел внутри пункта меню (Bass/High/Volume,
    // Dimmer и т.д.) — без явного сброса ниже периодический блок "живого обновления"
    // (loop(), 200мс, гейт на inSettingsMode/volumeOverlayActive, не на nowPlayingActive)
    // продолжал бы перерисовывать ТОТ экран поверх/вместо только что нарисованного Now
    // Playing на каждом же тике — экран "не отрисовывался нормально" именно поэтому.
    // endVolumeOverlay() не вызываем — та сама планирует redrawCurrentScreen() (лишний кадр
    // старого экрана прямо перед Now Playing), просто снимаем флаг напрямую
    inSettingsMode = false;
    volumeOverlayActive = false;
    lastRenderedText[0] = '\0'; // форсируем перерисовку блоком ниже на этом же тике
    lastRenderedSource[0] = '\0';
  } else if (!playingNow && wasPlaying) {
    // Нисходящий фронт: пауза/стоп — возвращаем прежнее состояние реле Streamer и уходим в
    // обычную карусель, независимо от того, был показан полноэкранный Now Playing или
    // пользователь уже "гостил" в меню (nowPlayingMenuVisitActive)
    streamerRelayOn = streamerWasOnBeforePlayback;
    applyStreamerRelay();
    nowPlayingActive = false;
    nowPlayingMenuVisitActive = false;
    inSettingsMode = false;
    if (!isMuted) {
      drawMenu();
    }
  }
  wasPlaying = playingNow;

  if (nowPlayingActive && !nowPlayingMenuVisitActive && !isMuted) {
    // Источник (Spotify/AirPlay/...) сравнивается отдельно от текста трека — на AirPlay текст
    // всегда пуст (см. project_arylic_airplay_no_metadata в памяти), так что смена источника
    // без смены текста иначе осталась бы незамеченной и экран не перерисовался бы
    bool textChanged = strcmp(esp32LinkNowPlayingText(), lastRenderedText) != 0;
    bool sourceChanged = strcmp(esp32LinkStreamingSource(), lastRenderedSource) != 0;
    if (textChanged || sourceChanged) {
      strncpy(lastRenderedText, esp32LinkNowPlayingText(), sizeof(lastRenderedText) - 1);
      lastRenderedText[sizeof(lastRenderedText) - 1] = '\0';
      strncpy(lastRenderedSource, esp32LinkStreamingSource(), sizeof(lastRenderedSource) - 1);
      lastRenderedSource[sizeof(lastRenderedSource) - 1] = '\0';
      drawNowPlayingScreen();
    }
    // Тиканье прогресс-бара/счётчика и бегущей строки — отдельно от блока выше: тот
    // перерисовывает экран целиком только когда меняются текст/источник (редко), а эти два —
    // каждую секунду/каждый шаг прокрутки. Обе функции сами решают, рисовать ли что-то и как
    // часто (см. updateNowPlayingProgress()/updateNowPlayingTitleScroll())
    updateNowPlayingProgress();
    updateNowPlayingTitleScroll();
  }

  if (nowPlayingMenuVisitActive && millis() - nowPlayingMenuVisitLastActivity > NOW_PLAYING_MENU_IDLE_TIMEOUT_MS) {
    nowPlayingMenuVisitActive = false;
    if (!isMuted) {
      drawNowPlayingScreen();
    }
  }
}

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
#if defined(DISPLAY_DRIVER_SSD1306) || defined(DISPLAY_DRIVER_SSD1309)
  // VCOMH (0xDB) по умолчанию у SSD1306/1309-драйверов в u8g2 — 0x20; сама библиотека в
  // комментарии к init-последовательности пишет, что 0x00 даёт максимальный диапазон для
  // setContrast() (issue #98) — без этого регулировка яркости дисплея почти незаметна
  // визуально. Специфично для SSD1306/1309-семейства — на SH1106 команда 0xDB не определена
  // так же, поэтому хак только под этим #if, не глобально
  u8g2.sendF("ca", 0x0DB, 0x000);
#endif
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
  pinMode(STREAMER_RELAY_PIN, OUTPUT);
  pinMode(LED_BASS_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_VOLUME_PIN, OUTPUT);
  pinMode(BYPASS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BYPASS_LED_PIN, OUTPUT);

  initRemoteControl(); // Теперь на Input Capture Timer4 (пин 49) — см. rc5_icu.h/remote_control.cpp
  initTemperatureSensors(); // Датчики DS18B20, пункт меню "Info"
  esp32LinkInit(); // Serial2 (RX2, пин 17, фиксированный) — приём от ESP32-компаньона, см. esp32_link.h
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
  handleRemoteInput(); // Проверяет сигнал с ИК-пульта сама (rc5IcuGetFrame(), см. rc5_icu.h)

  // Приём от ESP32-компаньона (esp32_link.h) — в отличие от остального фона, нужен ДАЖЕ в
  // Standby: иначе Mega не узнает, что Arylic начал играть, и не сможет сама включиться
  // (см. ниже). Сам updateNowPlaying() (переключение Source/показ Now Playing) по-прежнему
  // имеет смысл только пока система включена — реле/дисплей обесточены в Standby
  esp32LinkPoll();

  // Автовключение из Standby — ТОЛЬКО на фронт "не играло -> играет", а не на сам факт "сейчас
  // играет" (раньше был чистый level-check — если выключить питание пультом ПОКА стрим уже
  // идёт, esp32LinkIsPlaying() оставался true все следующие итерации, и система сама включалась
  // обратно почти сразу же после выключения, будто кнопка Power не сработала; пользователь
  // так и описал баг и сам нашёл обходной путь — пауза и повторный play ДЕЙСТВИТЕЛЬНО создают
  // фронт и включают систему, что и подтвердило причину). wasPlayingAtStandbyEntry
  // синхронизируется с ТЕКУЩИМ состоянием ровно в момент входа в Standby (см. ниже) — именно
  // поэтому "стрим продолжается без остановки" после выключения больше не считается новым стартом
  static bool wasPowerOff = false;
  static bool wasPlayingAtStandbyEntry = false;
  if (powerOff && !wasPowerOff) {
    wasPlayingAtStandbyEntry = esp32LinkIsPlaying();
  }
  wasPowerOff = powerOff;

  if (powerOff) {
    bool playingNow = esp32LinkIsPlaying();
    if (playingNow && !wasPlayingAtStandbyEntry) {
      // Настоящий новый старт воспроизведения уже ПОСЛЕ выключения — включаемся тем же путём,
      // что и ручной Power с пульта (см. IR_POWER в remote_control.cpp). updateNowPlaying() сама
      // подхватит уже true playingNow на следующей же итерации (теперь !powerOff) и включит
      // реле Streamer + покажет Now Playing — здесь только само включение
      powerOnDevices();
      powerOff = false;
    }
    wasPlayingAtStandbyEntry = playingNow;
  } else {
    updateNowPlaying();
  }

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

  // Пока показан полноэкранный Now Playing (до того, как нажали Enter/клик энкодера) —
  // вращение энкодера тоже недоступно, тот же принцип, что у Mute/powerOff выше (см.
  // handleRemoteInput() в remote_control.cpp за симметричным блоком для пульта)
  bool nowPlayingFullscreen = nowPlayingActive && !nowPlayingMenuVisitActive;
  if (isMuted || powerOff || nowPlayingFullscreen) {
    encoderValue = 0; // Отбрасываем накопленное вращение — навигация недоступна
  } else if (encoderValue != 0) {
    refreshNowPlayingMenuActivity(); // Вращение энкодера тоже считается активностью в "визите" из Now Playing
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
          // Список строк (температуры/Streamer/Setup IP/Control IP/Arylic, см.
          // drawInfoScreen()) — тот же приём двухуровневой навигации, что у Dimmer
          // (dimmerRowLocked): пока !infoRowLocked, вращение просто двигает курсор по кругу,
          // ничего не применяя. После короткого клика (infoRowLocked, см.
          // checkEncoderButton()) вращение переключает Streamer — но только если курсор
          // стоит именно на его строке, на остальных строках это просто нечего переключать.
          // Пульт (Left/Right/Up/Down) на этот флаг не смотрит — там Left/Right сразу
          // переключают Streamer, независимо от состояния энкодера
          if (!infoRowLocked) {
            settings[currentMenuItem] = ((settings[currentMenuItem] + direction) % INFO_ROW_COUNT + INFO_ROW_COUNT) % INFO_ROW_COUNT;
          } else if (settings[currentMenuItem] == INFO_STREAMER_ROW_INDEX) {
            streamerRelayOn = (direction > 0);
            applyStreamerRelay();
          }
          encoderValue = 0;
          drawInfoScreen();
        }
      }
    }
  }

  // Обновление светодиодов в режиме настройки (или во время временного показа Volume
  // с карусели через Up/Down — см. volumeOverlayActive, или во время ручного вращения
  // Bass/High/Volume рукой — см. knobIndicatorActiveItem). Раньше этот блок не проверял
  // powerOff — powerOffDevices() гасил светодиоды (LOW), но уже на следующей итерации
  // loop() этот же блок (ветка "иначе") тут же зажигал их обратно (HIGH), т.к.
  // inSettingsMode/оверлеи к этому моменту уже false. Теперь блок целиком пропускается
  // в Standby, погашенное состояние остаётся как есть
  if (!powerOff) {
    if (inSettingsMode || volumeOverlayActive || knobIndicatorActiveItem != -1) {
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
  }

  // Крутящаяся иконка пункта меню в углу drawMenu(), пока пользователь сидит на карусели
  // (не зашёл в настройки). Частичное обновление тайлов (не весь экран) — см. подробности в
  // hardware_settings.h у MENU_ICON_* и в bass_volume_high_animation.h
  if (!isMuted && !inSettingsMode && !volumeOverlayActive && !sourceOverlayActive && !powerOff && knobIndicatorActiveItem == -1 && !nowPlayingFullscreen) {
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
  // Раньше здесь была пауза (MENU_NAVIGATION_RING_PAUSE_MS) на короткое время после команд
  // навигации — обходила потерю RC5-кадра, если приём совпадал с NeoPixel.show(). Убрана:
  // причина устранена на уровне приёма (см. rc5_icu.h — декодер на Input Capture Timer4
  // не теряет кадры даже во время бит-банга колец), костыль больше не нужен
  if (!powerOff && millis() - lastRingUpdate >= RING_UPDATE_INTERVAL_MS) {
    lastRingUpdate = millis();
    int bassRaw, highRaw, volumeRaw;
    int bassPercent = readBassPotPercent(&bassRaw);
    int highPercent = readHighPotPercent(&highRaw);
    int volumePercent = readVolumePotPercent(&volumeRaw);
    updateVolumeRing(volumePercent);
    if (bypassAnimMode == 0) {
      renderDbRing(bassRing, bassPercent, bassRingState);
      renderDbRing(highRing, highPercent, highRingState);
    }
    // IRremote игнорирует новый сигнал, пока явно не вызван resume() (внутри
    // handleRemoteInput()) — этот блок с усреднением потенциометров занимает заметное
    // время (до ~15-20мс), проверяем пульт сразу после, а не только в начале loop()
    handleRemoteInput();

    // Обнаружение ручного вращения любого кноба РУКОЙ, пока сидим на карусели —
    // isBassSeeking()/isHighSeeking()/isVolumeSeeking() исключают движение от фонового
    // автовозврата (после Bypass/питания/EQ-пресета) — это не рука.
    //
    // Сравнение идёт по УЖЕ ПЕРЕВЕДЁННОМУ значению (dB/%), а не по raw ADC — см. подробный
    // комментарий у KNOB_OVERLAY_VALUE_THRESHOLD_DB/_PERCENT (hardware_settings.h): потенциометры
    // откалиброваны неравномерно, и raw-порог был почти недостижим у самого начала шкалы
    // (-10dB/0%), где raw меняется очень мало на градус поворота — именно там, откуда обычно
    // начинают крутить, если ручка стояла на нуле
    // Имена сохранены как есть (lastXValue/xKnobStreak), хотя по факту это теперь "точка
    // отсчёта окна"/"счётчик тиков внутри окна" — см. sustainedKnobMovement() ниже
    static int lastBassValue = bassPercent, lastHighValue = highPercent, lastVolumeValue = volumePercent;
    static byte bassKnobStreak = 0, highKnobStreak = 0, volumeKnobStreak = 0;

    // Окно целиком: сравниваем НАЧАЛО окна (значение KNOB_OVERLAY_CONSECUTIVE_TICKS тиков
    // назад) с ТЕКУЩИМ тиком — а не требуем, чтобы КАЖДЫЙ из N соседних тиков по отдельности
    // дал скачок ≥ порога. У Bass/High всего ~21 грубая ступенька дБ на весь диапазон
    // (в отличие от Volume с её 101 %), и усреднение ADC (64 сэмпла) иногда даёт тик без
    // изменения даже во время реального непрерывного вращения — требование "N подряд"
    // почти никогда не набиралось (кольцо при этом реагировало нормально — у него такого
    // фильтра нет). Как только накопленное смещение от начала окна превышает порог — окно
    // сдвигается вперёд (текущее значение становится новой точкой отсчёта), не считая
    // тиков; если окно истекло (набралось N тиков), а порог так и не превышен — тоже
    // сдвигаем окно вперёд, а не копим дрейф бесконечно.
    //
    // Порог свой у каждой ручки (threshold-параметр, не общий #define) — у Volume дребезг
    // контакта (особенно в нижней части хода, см. VOLUME_RING_FIRST_LED_*_PERCENT в
    // hardware_settings.h — та же наводка) может дать скачок на 1-3% сам по себе, без
    // касания рукой; у Bass/High свой порог в дБ, шире не нужен (там наоборот было мало
    // разрешения, а не наводка)
    auto sustainedKnobMovement = [](int value, int* windowStartValue, byte* ticksInWindow, int threshold) -> bool {
      bool triggered = abs(value - *windowStartValue) >= threshold;
      (*ticksInWindow)++;
      if (triggered || *ticksInWindow >= KNOB_OVERLAY_CONSECUTIVE_TICKS) {
        *windowStartValue = value;
        *ticksInWindow = 0;
      }
      return triggered;
    };

    // KNOB_OVERLAY_MOTOR_SETTLE_MS после любой команды на мотор (Up/Down, автовозврат) —
    // остаточное механическое "доседание" потенциометра иначе могло быть принято за
    // настоящее ручное вращение сразу после отпускания кнопки (см. hardware_settings.h)
    bool onCarouselIdle = !inSettingsMode && !volumeOverlayActive && !sourceOverlayActive && !isMuted && !powerOff
        && millis() - lastMotorInputTime >= KNOB_OVERLAY_MOTOR_SETTLE_MS;
    if (onCarouselIdle) {
      bool bassSustained = sustainedKnobMovement(bassPercent, &lastBassValue, &bassKnobStreak, KNOB_OVERLAY_VALUE_THRESHOLD_DB);
      bool highSustained = sustainedKnobMovement(highPercent, &lastHighValue, &highKnobStreak, KNOB_OVERLAY_VALUE_THRESHOLD_DB);
      bool volumeSustained = sustainedKnobMovement(volumePercent, &lastVolumeValue, &volumeKnobStreak, KNOB_OVERLAY_VALUE_THRESHOLD_PERCENT);
      int previousActiveItem = knobIndicatorActiveItem;
      if (!isBassSeeking() && bassSustained) {
        knobIndicatorActiveItem = 0;
        knobIndicatorLastMovementTime = millis();
      } else if (!isHighSeeking() && highSustained) {
        knobIndicatorActiveItem = 1;
        knobIndicatorLastMovementTime = millis();
      } else if (!isVolumeSeeking() && volumeSustained) {
        knobIndicatorActiveItem = 2;
        knobIndicatorLastMovementTime = millis();
      }
      if (knobIndicatorActiveItem != -1) {
        if (previousActiveItem == -1) {
          // Карусель -> оверлей: запомнить, куда вернуться после (см. идентичный приём у
          // beginVolumeOverlay())
          knobOverlaySavedMenuItem = currentMenuItem;
        }
        if (previousActiveItem != knobIndicatorActiveItem) {
          // Только что вошли или сменился показываемый пункт — сбросить кэш последнего
          // нарисованного значения, иначе случайное совпадение чисел между ручками
          // пропустило бы самую первую отрисовку нового пункта
          knobOverlayLastDrawnValue = -32000;
        }
        // Bass/High/Volume — индексы 0/1/2 в menuItems[] по построению (main.cpp), поэтому
        // knobIndicatorActiveItem можно подставлять напрямую, без отдельного поиска по имени
        currentMenuItem = knobIndicatorActiveItem;
        int shownValue = (knobIndicatorActiveItem == 0) ? bassPercent
                        : (knobIndicatorActiveItem == 1) ? highPercent
                        : volumePercent;
        // Полноэкранный вид — тот же drawArrowIndicator(), что и настоящий вход в настройки —
        // но только когда значение реально изменилось, а не на каждый 100мс тик подряд одно и то же
        if (shownValue != knobOverlayLastDrawnValue) {
          knobOverlayLastDrawnValue = shownValue;
          drawArrowIndicator(0, false, false);
        }
      }
    } else {
      // Не на карусели — сбрасываем накопленное состояние, а не копим его вхолостую.
      // Иначе движение, которое сделал сам пульт (например реальный ход мотора Volume при
      // удержании Up/Down через beginVolumeOverlay()), успевало "накопить" стрик, и сразу
      // после возврата на карусель индикатор включался бы повторно, как будто рукой
      lastBassValue = bassPercent;
      lastHighValue = highPercent;
      lastVolumeValue = volumePercent;
      bassKnobStreak = 0;
      highKnobStreak = 0;
      volumeKnobStreak = 0;
      if (knobIndicatorActiveItem != -1) {
        // Что-то другое перехватило экран, пока оверлей был активен (Mute/Power/Source-шорткат/
        // настоящий вход в настройки через Enter). Если это был настоящий Enter — inSettingsMode
        // уже true, пользователь легитимно зашёл в тот пункт, на который сейчас указывает
        // currentMenuItem, тянуть его обратно нельзя. Во всех остальных случаях восстанавливаем
        // то, что было на карусели ДО того, как рука взялась за ручку
        if (!inSettingsMode) {
          currentMenuItem = knobOverlaySavedMenuItem;
        }
        knobIndicatorActiveItem = -1;
      }
    }
  }

  // Гасим оверлей положения, если рукой перестали крутить — вне тика выше, чтобы
  // 1-секундный таймаут срабатывал точно. В отличие от компактного варианта, экран сейчас
  // занят ПОЛНОЙ настройкой (drawArrowIndicator()), а не иконкой карусели — просто перестать
  // его трогать недостаточно, нужно явно вернуть currentMenuItem и перерисовать то, что было
  // видно ДО того, как рука взялась за ручку — обычно карусель, но если рукой крутили прямо
  // поверх полноэкранного Now Playing (см. main.h/esp32_link.h), вернуться нужно туда же, а
  // не в карусель — иначе после того, как ручку отпустили, экран навсегда "застревал" бы на
  // карусели, хотя источник всё ещё Streamer и играет
  if (knobIndicatorActiveItem != -1 && millis() - knobIndicatorLastMovementTime >= KNOB_OVERLAY_IDLE_TIMEOUT_MS) {
    knobIndicatorActiveItem = -1;
    currentMenuItem = knobOverlaySavedMenuItem;
    if (nowPlayingActive && !nowPlayingMenuVisitActive) {
      drawNowPlayingScreen();
    } else {
      drawMenu();
    }
  }

  // Во время анимации мигания (возврат в 0dB, дыхание Volume выше середины шкалы)
  // кольцу нужно обновляться чаще для плавности — но ТОЛЬКО пока анимация активна,
  // и без перечтения потенциометра (дёшево: просто пересчёт яркости уже известного
  // значения + show())
  static unsigned long lastBlinkRender = 0;
  if (!powerOff && millis() - lastBlinkRender >= 30) {
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
  if (!powerOff && bypassAnimMode != 0) {
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
