#include "neopixel.h"
#include "hardware_settings.h"
#include "main.h"

Adafruit_NeoPixel volumeRing(VOLUME_RING_COUNT, VOLUME_RING_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel bassRing(RING_COUNT, BASS_RING_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel highRing(RING_COUNT, HIGH_RING_PIN, NEO_GRB + NEO_KHZ800);

uint8_t currentRingColorR = ringColorPalette[RING_COLOR_DEFAULT].r;
uint8_t currentRingColorG = ringColorPalette[RING_COLOR_DEFAULT].g;
uint8_t currentRingColorB = ringColorPalette[RING_COLOR_DEFAULT].b;

DbRingState bassRingState;
DbRingState highRingState;

// Зажигает светодиоды кольца Volume по кругу пропорционально уровню (0-100%),
// 2 нижних светодиода (VOLUME_RING_SKIP_A/B) в заливку не участвуют
void updateVolumeRing(int percent) {
  // Двумя отрезками: 0%->0, 50%->7, 100%->10 — загорается быстрее в первой половине
  // хода (раньше жаловались, что "отстаёт"), но 0% гарантированно гасит все светодиоды
  int litCount;
  if (percent <= 50) {
    litCount = map(percent, 0, 50, 0, 7);
  } else {
    litCount = map(percent, 50, 100, 7, 10);
  }
  litCount = constrain(litCount, 0, 10);
  volumeRing.clear();
  for (int i = 0; i < litCount; i++) {
    // Градиент яркости по кругу: первый горящий светодиод самый тусклый (i=0 -> 1/10),
    // последний — самый яркий (i=9 -> 10/10). Дальше ещё умножается на общий Dimmer
    float fraction = (i + 1) / 10.0;
    volumeRing.setPixelColor(volumeRingOrder[i],
      (uint8_t)(currentRingColorR * fraction),
      (uint8_t)(currentRingColorG * fraction),
      (uint8_t)(currentRingColorB * fraction));
  }
  volumeRing.show();
}

// Яркость зелёной пары в момент анимации возврата в ноль: 3 плавных "вздоха"
// тусклый->яркий->тусклый (map — целочисленно, дёшево), затем держим ровно ярко
uint8_t zeroBlinkBrightness(unsigned long elapsed) {
  if (elapsed >= ZERO_BLINK_TOTAL_MS) {
    return 255;
  }
  unsigned long posInPulse = elapsed % ZERO_BLINK_PULSE_MS;
  unsigned long half = ZERO_BLINK_PULSE_MS / 2;
  if (posInPulse < half) {
    return map(posInPulse, 0, half, ZERO_BLINK_MIN_BRIGHTNESS, 255);
  }
  return map(posInPulse, half, ZERO_BLINK_PULSE_MS, 255, ZERO_BLINK_MIN_BRIGHTNESS);
}

// true, пока идёт анимация мигания у этого кольца (используется, чтобы включить
// более частое обновление ленты ТОЛЬКО на время анимации, не постоянно)
bool dbRingBlinking(const DbRingState &state) {
  return state.lastValue == 0 && (millis() - state.zeroEnterTime) < ZERO_BLINK_TOTAL_MS;
}

// Зажигает кольцо Bass/High: центральная пара (0dB) — зелёная, горит ТОЛЬКО ровно
// на нуле (с анимацией мигания при входе в зону); остальные светодиоды — тёплый
// жёлтый, зажигаются в сторону "-" или "+" от центра по модулю значения
void renderDbRing(Adafruit_NeoPixel &ring, int dbValue, DbRingState &state) {
  bool isZero = (dbValue == 0);
  if (isZero && state.lastValue != 0) {
    state.zeroEnterTime = millis(); // Только что вошли в зону нуля — запускаем анимацию с начала
  }
  state.lastValue = dbValue;

  ring.clear();
  if (isZero) {
    uint8_t b = zeroBlinkBrightness(millis() - state.zeroEnterTime);
    ring.setPixelColor(ringCenterPair[0], 0, b, 0);
    ring.setPixelColor(ringCenterPair[1], 0, b, 0);
  } else if (dbValue > 0) {
    int litCount = constrain(map(dbValue, 0, 10, 0, 4), 0, 4);
    for (int i = 0; i < litCount; i++) {
      // Градиент от нуля к краю: ближний к центру светодиод (i=0) самый тусклый,
      // крайний (i=3) самый яркий. Зелёной пары это не касается
      float fraction = (i + 1) / 4.0;
      ring.setPixelColor(ringPositiveOrder[i],
        (uint8_t)(currentRingColorR * fraction),
        (uint8_t)(currentRingColorG * fraction),
        (uint8_t)(currentRingColorB * fraction));
    }
  } else {
    int litCount = constrain(map(-dbValue, 0, 10, 0, 4), 0, 4);
    for (int i = 0; i < litCount; i++) {
      float fraction = (i + 1) / 4.0;
      ring.setPixelColor(ringNegativeOrder[i],
        (uint8_t)(currentRingColorR * fraction),
        (uint8_t)(currentRingColorG * fraction),
        (uint8_t)(currentRingColorB * fraction));
    }
  }
  ring.show();
}

// Применяет settings[] для пункта "Color" (индекс в ringColorPalette) как реальный
// цвет активной (не зелёной) части всех трёх колец
void applyRingColorScheme() {
  int idx = constrain(settings[6], 0, RING_COLOR_COUNT - 1);
  currentRingColorR = ringColorPalette[idx].r;
  currentRingColorG = ringColorPalette[idx].g;
  currentRingColorB = ringColorPalette[idx].b;
}

// Применяет settings[] для пункта "Dimmer" (0-100%) как реальную яркость всех трёх колец
void applyRingDimmer() {
  int brightness = map(settings[5], 0, 100, 0, 255);
  volumeRing.setBrightness(brightness);
  volumeRing.show();
  bassRing.setBrightness(brightness);
  bassRing.show();
  highRing.setBrightness(brightness);
  highRing.show();
}
