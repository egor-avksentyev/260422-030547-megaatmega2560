#pragma once

// ============================================================================
// motor_driver_logic.h — низкоуровневое управление драйверами моторов Bass/High
// (один провод направления + один PWM) и Volume (два провода направления + два PWM).
// ============================================================================

#include <Arduino.h>

void motorControl(int val, byte pinIN, byte pinPWM);
void motorControl2(int val, byte pinIN1, byte pinIN2, byte pinPWM1, byte pinPWM2);
void stopAllMotors();

// Автовозврат Bass/High в 0dB — запускается при каждом переключении Bypass (в любую
// сторону, см. triggerBypassAnim()), независимо крутит соответствующий мотор к нулю,
// пока физическое положение (по обратной связи потенциометра) не станет ровно 0.
// Если пользователь сам берётся за энкодер/пульт для Bass/High, автовозврат для этой
// ручки нужно отменить (cancelBassRecenter()/cancelHighRecenter()), чтобы не бороться
// с ручным управлением
void requestBassHighRecenter();
void updateBassHighRecenter();
void cancelBassRecenter();
void cancelHighRecenter();
