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

// Долговременная (EEPROM) память положения Bass/High — пишется при КАЖДОМ выключении,
// независимо от Bypass (см. комментарий у saveBassHighPositionOnShutdown() в
// on_off_logic.cpp). powerOnDevices() сам восстанавливает сохранённое положение —
// снаружи нужно только вызвать saveBassHighPositionOnShutdown() перед выключением
void saveBassHighPositionOnShutdown();

// Состояние Bypass — в отличие от положения Bass/High, сохраняется ПРИ КАЖДОМ выключении
// (не только когда Bypass выключен) и восстанавливается самим powerOnDevices() при
// следующем включении. Снаружи нужно только вызвать перед выключением
void saveBypassStateOnShutdown();

// Долговременная (EEPROM) память настроек Dimmer/Color — яркость колец (LED), яркость
// дисплея (Display) и цвет колец (Color). В отличие от двух блоков выше, пишется сразу при
// КАЖДОМ изменении значения (см. saveSettings() в main.cpp), а не только при выключении
// питания; loadSettings() (main.cpp) вызывает загрузку в самом начале setup()
void saveDimmerColorSettings();
void loadDimmerColorSettings();

// Долговременная (EEPROM) память выбранного источника (Source) — как Bypass/Bass-High
// выше, пишется ТОЛЬКО при выключении питания (не на каждое изменение, в отличие от
// Dimmer/Color) и восстанавливается сама powerOnDevices() при следующем включении.
// Снаружи нужно только вызвать перед выключением
void saveSourceStateOnShutdown();

// Долговременная (EEPROM) память состояния VU Meter — тот же паттерн, что у Source/Bypass
// выше (пишется только при выключении, восстанавливается сама powerOnDevices())
void saveVuMeterStateOnShutdown();

// Долговременная (EEPROM) память выбранного EQ-пресета — пишется только при выключении,
// как Source/Bypass выше, но с одной оговоркой: если после выбора пресета пользователь
// вручную поправил Bass или High (isEqSelectionActive() в motor_driver_logic.cpp вернёт
// false), пресет сохраняется как НЕ активный — при следующем включении приоритет у ручного
// положения (см. saveBassHighPositionOnShutdown()), а не у пресета, который его больше не
// описывает. Если наоборот — пресет применили ПОСЛЕ ручной правки, он активен и имеет
// приоритет при восстановлении
void saveEqStateOnShutdown();
