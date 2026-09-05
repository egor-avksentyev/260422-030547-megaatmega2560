#pragma once

// ============================================================================
// voltage_sensor.h — напряжение сети (модуль трансформатор+LM358, см. VOLTAGE_SENSOR_PIN
// в hardware_settings.h), обслуживает пункт меню "Info".
// ============================================================================

// Усреднённый analogRead() + интерполяция по mainsVoltageCalRaw[]/mainsVoltageCalValue[]
// (см. potRawToPercent() в motor_position.cpp — переиспользуется как есть). rawOut — сырое
// значение ADC, если нужно для калибровки/отладки через Serial
int readMainsVoltage(int* rawOut = nullptr);
