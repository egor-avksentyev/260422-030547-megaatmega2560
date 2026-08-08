#pragma once

// ============================================================================
// motor_driver_logic.h — низкоуровневое управление драйверами моторов Bass/High
// (один провод направления + один PWM) и Volume (два провода направления + два PWM).
// ============================================================================

#include <Arduino.h>

void motorControl(int val, byte pinIN, byte pinPWM);
void motorControl2(int val, byte pinIN1, byte pinIN2, byte pinPWM1, byte pinPWM2);
void stopAllMotors();
