#pragma once

// ============================================================================
// relay.h — переключение источника входа (Source) и состояния Bypass: реле,
// индикаторный светодиод Bypass и опрос его физической кнопки. VU-Meter/Led/
// Standby/Mute переключаются напрямую по месту (encoder.cpp/remote_control.cpp/
// on_off_logic.cpp) — здесь только Source и Bypass, у которых есть отдельная
// применяющая функция, используемая из нескольких мест.
// ============================================================================

void applySourceSelection();
void applyBypassState();
void checkBypassButton();

// Индекс "STREAMER" в sourceNames[]/settings[sourceMenuIndex()] (hardware_settings.h) — по
// имени, не захардкожен на позицию, тем же приёмом, что dimmerMenuIndex()/colorMenuIndex()
// и т.п. в main.cpp. Используется автопереключением источника при начале воспроизведения на
// Arylic (см. main.cpp, updateNowPlaying())
int streamerSourceIndex();
