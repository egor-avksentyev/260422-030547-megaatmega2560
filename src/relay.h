#pragma once

// ============================================================================
// relay.h — переключение источника входа (Source), состояния Bypass и реле Streamer:
// реле, индикаторный светодиод Bypass и опрос его физической кнопки. VU-Meter/Led/
// Standby/Mute переключаются напрямую по месту (encoder.cpp/remote_control.cpp/
// on_off_logic.cpp) — здесь только Source/Bypass/Streamer, у которых есть отдельная
// применяющая функция, используемая из нескольких мест.
// ============================================================================

void applySourceSelection();
void applyBypassState();
void checkBypassButton();

// Реле Streamer (строка "Streamer" в пункте меню Info) — независимо от Source, не
// взаимоисключающее с ним. Читает глобальный streamerRelayOn (main.h). Раньше (до
// вынесения Streamer из Source, см. память проекта project_streamer_independent_relay)
// автопереключение при начале воспроизведения на Arylic искало STREAMER по имени в
// sourceNames[] (streamerSourceIndex()) — теперь просто выставляет streamerRelayOn
// напрямую и зовёт эту функцию, см. updateNowPlaying() в main.cpp
void applyStreamerRelay();
