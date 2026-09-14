#pragma once

// ============================================================================
// main.h — общее состояние меню, которым пользуются почти все остальные модули
// (текущий пункт меню, режим настроек, массив значений settings[]), плюс
// небольшие функции, которые напрямую управляют этим состоянием. main.cpp — это
// единственное место, где выполняется setup()/loop() и вызываются рабочие
// методы всех остальных модулей.
// ============================================================================

#include <Arduino.h>

extern String menuItems[];
extern int currentMenuItem;
extern int settings[];
extern bool inSettingsMode;
extern bool isMuted;
extern unsigned long lastMotorInputTime;

// "Volume" остаётся обычным пунктом карусели (Enter/навигация работают как всегда), но
// ДОПОЛНИТЕЛЬНО Up/Down с пульта работают как глобальный шорткат громкости из любого
// другого места (кроме случаев, когда ты именно внутри Bass/High/Dimmer — внутри Dimmer
// Up/Down переключают строку яркости, см. dimmerEditingDisplay ниже) — временно подставляют
// currentMenuItem под "Volume" и откатывают обратно, когда автостоп мотора решит, что
// кнопку отпустили (см. beginVolumeOverlay()/endVolumeOverlay())
extern bool volumeOverlayActive;
void beginVolumeOverlay();
void endVolumeOverlay();
void redrawCurrentScreen();

// Аналогично volumeOverlayActive, но для Set (быстрое переключение Source) — откат не по
// автостопу мотора, а просто по таймеру SOURCE_OVERLAY_DURATION_MS (см. hardware_settings.h)
extern bool sourceOverlayActive;
void beginSourceOverlay();
void updateSourceOverlay();

// Экран Dimmer теперь регулирует два значения — яркость колец (settings[currentMenuItem],
// как раньше) и яркость дисплея (displayBrightness, новое). dimmerEditingDisplay — какая из
// двух строк сейчас активна (false = кольца, true = дисплей); переключается Up/Down на
// пульте, только пока ты внутри Dimmer (см. IR_UP/IR_DOWN в remote_control.cpp). Сбрасывается
// в false при каждом новом входе в Dimmer, чтобы не запутаться, где остановился прошлый раз
extern int displayBrightness;
extern bool dimmerEditingDisplay;

// Пока false, вращение энкодера (только само колесо, не пульт — см. checkEncoderButton() в
// encoder.cpp) внутри Dimmer просто двигает подсветку между строками LED/Display
// (dimmerEditingDisplay), не меняя значений — переключиться в редактирование подсвеченной
// строки нужно коротким кликом энкодера. true — вращение меняет значение подсвеченной
// строки; повторный клик возвращает к выбору строки. Сбрасывается в false при каждом новом
// входе в Dimmer. Пульт (Left/Right/Up/Down) этот флаг не проверяет — там всё как раньше
extern bool dimmerRowLocked;

// Индексы "Dimmer"/"Color"/"Source"/"VU Meter" в menuItems[]/settings[] — нужны там, где
// currentMenuItem не гарантированно указывает на них (например при загрузке/сохранении в
// EEPROM, см. on_off_logic.cpp)
int dimmerMenuIndex();
int colorMenuIndex();
int sourceMenuIndex();
int vuMeterMenuIndex();
int eqMenuIndex();

#define MENU_ITEM_COUNT 10

void resetCursor();
void saveSettings();
void loadSettings();
void blinkLED(int pin);

// Полноэкранный "Now Playing" (см. esp32_link.h, hardware_settings.h) — показывается, пока
// Arylic реально играет (PLAY:1 от ESP32). true, только пока показывается САМ полноэкранный
// экран — как только пользователь нажал Enter/клик энкодера, экран сменился на обычное меню
// (см. nowPlayingMenuVisitActive ниже), но nowPlayingActive остаётся true — источник до сих
// пор играет, просто пользователь временно смотрит меню. Переключается на false только
// когда воспроизведение реально остановилось (main.cpp, updateNowPlaying())
extern bool nowPlayingActive;

// true, если пользователь зашёл в обычное меню С экрана Now Playing (Enter/клик энкодера,
// см. exitNowPlayingToMenu()) — самим этим фактом обычная навигация временно разрешена
// (см. gate в remote_control.cpp/encoder.cpp), и запущен таймер простоя: если за
// NOW_PLAYING_MENU_IDLE_TIMEOUT_MS никакой активности — main.cpp сам вернёт обратно на
// Now Playing (см. refreshNowPlayingMenuActivity())
extern bool nowPlayingMenuVisitActive;

// Enter/клик энкодера с экрана Now Playing — переходит в обычную карусель меню и заводит
// таймер простоя (см. nowPlayingMenuVisitActive выше). Не трогает currentMenuItem — куда
// попадёт пользователь, туда он и попадёт, как обычно, с той же самой точки, что и был бы
// на карусели без Now Playing
void exitNowPlayingToMenu();

// Сбрасывает таймер простоя, пока идёт "визит" в меню с экрана Now Playing — вызывать на
// любое пользовательское действие (ИК-команда, вращение/клик энкодера), пока
// nowPlayingMenuVisitActive. Ничего не делает, если сейчас не тот случай
void refreshNowPlayingMenuActivity();

// Реле Streamer (relay.cpp, applyStreamerRelay()) — независимая строка "Streamer" внутри
// пункта меню Info (не взаимоисключающая с Source, см. hardware_settings.h у
// STREAMER_RELAY_PIN). Курсор внутри списка Info хранится как обычно в
// settings[currentMenuItem] (тот же приём, что у Source/EQ) — своей переменной под него
// не нужно, но само состояние реле не привязано ни к какому пункту menuItems[], поэтому
// живёт отдельной глобальной переменной здесь, как isMuted
extern bool streamerRelayOn;

// Настоящая пользовательская настройка реле Streamer — то, что нужно сохранять в EEPROM при
// выключении (см. saveStreamerStateOnShutdown() в on_off_logic.cpp), а НЕ streamerRelayOn
// напрямую: пока играет Arylic (nowPlayingActive), streamerRelayOn может быть временно
// поднят автоматикой (см. updateNowPlaying() в main.cpp) поверх реальной пользовательской
// настройки — если выключить питание именно в этот момент, сохранять нужно то, что было ДО
// автовключения, иначе при следующем включении реле поднималось бы всегда, независимо от
// того, играет ли что-то на самом деле
bool streamerPersistentPreference();

// Двухуровневая навигация энкодером внутри Info — тот же приём, что dimmerRowLocked у
// Dimmer (см. checkEncoderButton() в encoder.cpp): пока false, вращение энкодера (само
// колесо, не пульт) просто двигает курсор по строкам списка, не применяя ничего. Короткий
// клик энкодера переключает в true — тогда вращение переключает реле Streamer (если курсор
// стоит именно на его строке; на остальных строках вращение в этом режиме ничего не делает,
// т.к. там нечего переключать), повторный клик возвращает к выбору строки. Пульт
// (Left/Right/Up/Down) на этот флаг не смотрит — там Left/Right сразу переключают Streamer,
// Up/Down сразу двигают курсор, независимо от того, в каком состоянии оставил Info
// энкодер. Сбрасывается в false при каждом новом входе в Info (тем же способом, что у
// dimmerRowLocked при входе в Dimmer)
extern bool infoRowLocked;
