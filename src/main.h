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

#define MENU_ITEM_COUNT 8

void resetCursor();
void saveSettings();
void loadSettings();
void blinkLED(int pin);
