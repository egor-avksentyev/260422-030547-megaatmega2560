#pragma once

// ============================================================================
// motor_driver_logic.h — низкоуровневое управление драйверами моторов Bass/High
// (один провод направления + один PWM) и Volume (два провода направления + два PWM).
// ============================================================================

#include <Arduino.h>

void motorControl(int val, byte pinIN, byte pinPWM);
void motorControl2(int val, byte pinIN1, byte pinIN2, byte pinPWM1, byte pinPWM2);
void stopAllMotors();

// Автовозврат Bass/High к произвольной целевой точке (raw ADC) — независимо крутит
// соответствующий мотор, пока физическое положение (по обратной связи потенциометра)
// не окажется в пределах MOTOR_RECENTER_RAW_EPSILON от цели. Если пользователь сам
// берётся за энкодер/пульт для Bass/High, автовозврат для этой ручки нужно отменить
// (cancelBassRecenter()/cancelHighRecenter()), чтобы не бороться с ручным управлением
void requestBassSeek(int targetRaw);
void requestHighSeek(int targetRaw);
void updateBassHighRecenter();
void cancelBassRecenter();
void cancelHighRecenter();
// Идёт ли сейчас фоновый автовозврат к цели — см. подробный комментарий у реализации
// (motor_driver_logic.cpp). Нужны снаружи, чтобы не путать программное движение мотора
// с физическим вращением ручки рукой (main.cpp)
bool isBassSeeking();
bool isHighSeeking();

// Тот же принцип для Volume, но по % (не raw) и с произвольной целью — используется, чтобы
// при включении питания громкость сама доехала до VOLUME_POWERON_TARGET_PERCENT, и при
// восстановлении Bass/High из EEPROM (см. on_off_logic.cpp). cancelVolumeSeek() — по тому же
// принципу, что и Bass/High: отменяется ручным управлением энкодером/пультом
void requestVolumeSeek(int targetPercent);
void updateVolumeSeek();
void cancelVolumeSeek();
bool isVolumeSeeking();

// Применяет EQ-пресет (см. eqPresets[]/EQ_COUNT в hardware_settings.h) — переводит его
// значения дБ в raw-цели (valueToRaw(), motor_position.cpp) и запускает обычный seek для
// Bass/High. eqSelectionActive() отражает, соответствует ли текущее физическое положение
// Bass/High последнему применённому пресету — cancelBassRecenter()/cancelHighRecenter()
// сбрасывают этот флаг, как только пользователь вручную поправит Bass или High после
// выбора пресета (см. приоритет сохранения в on_off_logic.cpp, saveEqStateOnShutdown())
void applyEqPreset(int index);
bool isEqSelectionActive();

// Блокирующий вариант для всех трёх — используется только при выключении питания: моторы
// должны физически доехать до нуля ДО того, как реле обесточат систему, а не когда-нибудь
// потом в фоне через loop() (см. remote_control.cpp, case IR_POWER)
void seekBassHighVolumeToZeroBlocking();
