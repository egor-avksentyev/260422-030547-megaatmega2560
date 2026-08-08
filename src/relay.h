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
