#pragma once

// ============================================================================
// animation_logic.h — анимация колец Bass/High, привязанная к состоянию Bypass
// (кнопкой, энкодером или пультом — источник переключения не важен,
// triggerBypassAnim() запускает одно и то же).
// Bypass выключается (mode 1): разовая заливка кольца целиком за BYPASS_FILL_TOTAL_MS,
// не привязана к реальному значению — чисто визуальное подтверждение выключения.
// Bypass включён (mode 2, длится, пока Bypass не выключат): центр дышит, потом держится
// красным; "жёлтые" светодиоды при этом продолжают отражать реальное текущее положение
// ручки (dbValue передаётся снаружи, из main.cpp) — гаснут по мере возврата к нулю,
// пока идёт автовозврат после Bypass.
// ============================================================================

#include <Adafruit_NeoPixel.h>

// 0 = нет анимации, 1 = заливка (Bypass выключен), 2 = мигание красным (Bypass включён)
extern int bypassAnimMode;
extern unsigned long bypassAnimStart;

void triggerBypassAnim();
void renderBypassFillAnim(Adafruit_NeoPixel &ring, unsigned long elapsed);
uint8_t bypassBlinkBrightness(unsigned long elapsed);
void renderBypassBlinkAnim(Adafruit_NeoPixel &ring, unsigned long elapsed, int dbValue);
