#pragma once

// ============================================================================
// neopixel.h — три NeoPixel-кольца (Bass/High/Volume): объекты лент, их текущий
// цвет/яркость и функции отрисовки уровня (dB для Bass/High, % для Volume).
// ============================================================================

#include <Adafruit_NeoPixel.h>

extern Adafruit_NeoPixel volumeRing;
extern Adafruit_NeoPixel bassRing;
extern Adafruit_NeoPixel highRing;

extern uint8_t currentRingColorR;
extern uint8_t currentRingColorG;
extern uint8_t currentRingColorB;

// Состояние анимации возврата в 0dB для одного кольца (Bass или High)
struct DbRingState {
  int lastValue = 999; // Сентинел, точно не 0 — чтобы первый же вызов не считался "уже в нуле"
  unsigned long zeroEnterTime = 0;
};
extern DbRingState bassRingState;
extern DbRingState highRingState;

void updateVolumeRing(int percent);
uint8_t volumeMidBreathBrightness(unsigned long elapsed);
bool volumeRingBreathing();
void renderVolumeRingBreath();
uint8_t zeroBlinkBrightness(unsigned long elapsed);
bool dbRingBlinking(const DbRingState &state);
void renderDbRing(Adafruit_NeoPixel &ring, int dbValue, DbRingState &state);
void renderDbRingOuterLeds(Adafruit_NeoPixel &ring, int dbValue);
void applyRingColorScheme();
void applyRingDimmer();
