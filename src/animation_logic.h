#pragma once

// ============================================================================
// animation_logic.h — разовая анимация на кольцах Bass/High при переключении
// Bypass (кнопкой, энкодером или пультом — источник переключения не важен,
// triggerBypassAnim() запускает одно и то же). Пока анимация активна, обычная
// отрисовка уровня dB на этих двух кольцах приостанавливается (см. loop() в main.cpp).
// ============================================================================

#include <Adafruit_NeoPixel.h>

// 0 = нет анимации, 1 = заливка (Bypass выключен), 2 = мигание красным (Bypass включён)
extern int bypassAnimMode;
extern unsigned long bypassAnimStart;

void triggerBypassAnim();
void renderBypassFillAnim(Adafruit_NeoPixel &ring, unsigned long elapsed);
uint8_t bypassBlinkBrightness(unsigned long elapsed);
void renderBypassBlinkAnim(Adafruit_NeoPixel &ring, unsigned long elapsed);
