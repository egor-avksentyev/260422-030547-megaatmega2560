#pragma once

// ============================================================================
// motor_position.h — обратная связь физического положения ручек Bass/High/Volume
// по потенциометрам: усреднение сырых отсчётов ADC и перевод в дБ/% по
// калибровочным таблицам из hardware_settings.h.
// ============================================================================

int potRawToPercent(int raw, const int calRaw[], const int calPercent[], int calPoints, int zeroSnapRaw = 0, int minSnapRaw = 0, int maxSnapRaw = 0);
int readPotPercent(int pin, const int calRaw[], const int calPercent[], int calPoints, int* rawOut = nullptr, int zeroSnapRaw = 0, int minSnapRaw = 0, int maxSnapRaw = 0);
int readBassPotPercent(int* rawOut = nullptr);
int readHighPotPercent(int* rawOut = nullptr);
int readVolumePotPercent(int* rawOut = nullptr);
int readCurrentPotPercent(int* rawOut = nullptr);
int currentPotValueMin();
int currentPotValueMax();
bool currentPotIsDb();

// Точный raw-отсчёт калибровочной точки 0dB — не спутать с "снапнутым" процентом/дБ
// из readBassPotPercent()/readHighPotPercent(): тот округляет к 0 в пределах
// *_POT_ZERO_SNAP_RAW (нужно для чистого отображения "0dB" на экране), а моторное
// автовозвращение (см. motor_driver_logic.cpp) должно доводить ручку до самой точки,
// а не просто до края зоны снапа — иначе не докручивает несколько градусов до истинного нуля
int bassZeroRaw();
int highZeroRaw();
