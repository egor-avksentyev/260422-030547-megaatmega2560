#include "animation_logic.h"
#include "hardware_settings.h"
#include "neopixel.h"
#include "main.h"

int bypassAnimMode = 0;
unsigned long bypassAnimStart = 0;

// Запускает анимацию колец Bass/High под текущее settings[4] (Bypass) — вызывается
// и от физической кнопки, и от переключения пункта меню "Bypass" энкодером/пультом,
// чтобы анимация работала одинаково независимо от источника переключения. Физическое
// положение Bass/High при этом не трогается — раньше Bypass ещё и принудительно
// возвращал их к 0dB (requestBassHighRecenter()), но это уводило регуляторы в ноль при
// каждом переключении Bypass, что оказалось нежелательным поведением
void triggerBypassAnim() {
  bypassAnimMode = (settings[4] == 0) ? 1 : 2;
  bypassAnimStart = millis();
}

// Разовая анимация заливки (Bypass выключается) — не привязана к реальному значению,
// шаг за шагом заполняет кольцо в обе стороны от зелёного центра за BYPASS_FILL_TOTAL_MS,
// после чего main.cpp сам возвращает обычную (реальную) отрисовку уровня дБ
void renderBypassFillAnim(Adafruit_NeoPixel &ring, unsigned long elapsed) {
  ring.clear();
  ring.setPixelColor(ringCenterPair[0], 0, 255, 0);
  ring.setPixelColor(ringCenterPair[1], 0, 255, 0);
  int stepsLit = constrain((int)(elapsed / BYPASS_FILL_STEP_MS), 0, BYPASS_FILL_STEPS);
  for (int i = 0; i < stepsLit; i++) {
    ring.setPixelColor(ringNegativeOrder[i], currentRingColorR, currentRingColorG, currentRingColorB);
    ring.setPixelColor(ringPositiveOrder[i], currentRingColorR, currentRingColorG, currentRingColorB);
  }
  ring.show();
}

// Несколько "вздохов" тусклый->яркий->тусклый, затем держим ровно ярко-красным —
// и остаётся так, пока Bypass не выключат (elapsed растёт без ограничения)
uint8_t bypassBlinkBrightness(unsigned long elapsed) {
  if (elapsed >= BYPASS_BLINK_INTRO_MS) {
    return 255;
  }
  unsigned long pos = elapsed % BYPASS_BLINK_PULSE_MS;
  unsigned long half = BYPASS_BLINK_PULSE_MS / 2;
  if (pos < half) {
    return map(pos, 0, half, BYPASS_BLINK_MIN_BRIGHTNESS, 255);
  }
  return map(pos, half, BYPASS_BLINK_PULSE_MS, 255, BYPASS_BLINK_MIN_BRIGHTNESS);
}

// dbValue — реальное текущее положение ручки: "жёлтые" светодиоды гаснут по мере
// возврата к нулю, центр — красный (дыхание, см. bypassBlinkBrightness())
void renderBypassBlinkAnim(Adafruit_NeoPixel &ring, unsigned long elapsed, int dbValue) {
  ring.clear();
  renderDbRingOuterLeds(ring, dbValue);
  uint8_t b = bypassBlinkBrightness(elapsed);
  ring.setPixelColor(ringCenterPair[0], b, 0, 0);
  ring.setPixelColor(ringCenterPair[1], b, 0, 0);
  ring.show();
}
