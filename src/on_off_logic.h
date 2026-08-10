#pragma once

// ============================================================================
// on_off_logic.h — глобальная последовательность включения/выключения питания:
// сообщения на экране, снятие/восстановление всех реле и светодиодов, остановка
// моторов и гашение NeoPixel-колец. Standby/VU-Meter/Led идут через эту
// последовательность; Mute — нет, он переключается отдельной кнопкой пульта.
// ============================================================================

extern bool powerOff;

void powerOffScreen();
void powerOnScreen();
void powerOffDevices();
void powerOnDevices();

// Долговременная (EEPROM) память положения Bass/High — только пока Bypass выключен, см.
// комментарий у saveBassHighPositionOnShutdown() в on_off_logic.cpp. powerOnDevices()
// сам восстанавливает сохранённое положение, если оно есть — снаружи нужно только
// вызвать saveBassHighPositionOnShutdown() перед выключением и clearSavedBassHighPosition()
// при включении Bypass (см. remote_control.cpp / animation_logic.cpp)
void saveBassHighPositionOnShutdown();
void clearSavedBassHighPosition();

// Состояние Bypass — в отличие от положения Bass/High, сохраняется ПРИ КАЖДОМ выключении
// (не только когда Bypass выключен) и восстанавливается самим powerOnDevices() при
// следующем включении. Снаружи нужно только вызвать перед выключением
void saveBypassStateOnShutdown();
