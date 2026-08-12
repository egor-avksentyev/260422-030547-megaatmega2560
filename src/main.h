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
// другого места (кроме случаев, когда ты именно внутри Bass/High) — временно подставляют
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

#define MENU_ITEM_COUNT 8

void resetCursor();
void saveSettings();
void loadSettings();
void blinkLED(int pin);
