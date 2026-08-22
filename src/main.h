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

#define MENU_ITEM_COUNT 9

void resetCursor();
void saveSettings();
void loadSettings();
void blinkLED(int pin);
