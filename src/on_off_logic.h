#pragma once

// ============================================================================
// on_off_logic.h — глобальная последовательность включения/выключения питания:
// сообщения на экране, снятие/восстановление всех реле и светодиодов, остановка
// моторов и гашение NeoPixel-колец. Standby/VU-Meter/Led идут через эту
// последовательность; Mute — нет, он переключается отдельной кнопкой пульта.
// ============================================================================

extern bool powerOff;
extern unsigned long powerButtonPressStartTime;
extern bool powerButtonPressing;

void powerOffScreen();
void powerOnScreen();
void powerOffDevices();
void powerOnDevices();
