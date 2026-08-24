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

// Яркость 6-го светодиода, пока Volume превышает VOLUME_MID_PERCENT: непрерывное
// "дыхание" тусклый->яркий->тусклый, без остановки (в отличие от zeroBlinkBrightness,
// который через несколько вздохов держится ровно ярко)
uint8_t volumeMidBreathBrightness(unsigned long elapsed) {
  unsigned long pos = elapsed % VOLUME_MID_BREATH_PULSE_MS;
  unsigned long half = VOLUME_MID_BREATH_PULSE_MS / 2;
  if (pos < half) {
    return map(pos, 0, half, VOLUME_MID_BREATH_MIN_BRIGHTNESS, 255);
  }
  return map(pos, half, VOLUME_MID_BREATH_PULSE_MS, 255, VOLUME_MID_BREATH_MIN_BRIGHTNESS);
}

static int lastVolumePercent = 0; // Кэш последнего процента — чтобы дыхание можно было перерисовывать чаще без лишних analogRead()

// true, пока громкость выше VOLUME_MID_PERCENT (используется, чтобы включить более
// частое обновление кольца ТОЛЬКО на время дыхания, не постоянно — как dbRingBlinking())
bool volumeRingBreathing() {
  return lastVolumePercent > VOLUME_MID_PERCENT;
}

// Перерисовывает кольцо Volume по уже известному (закэшированному) проценту — для
// частого обновления одной только яркости дышащего светодиода, без нового считывания потенциометра
void renderVolumeRingBreath() {
  updateVolumeRing(lastVolumePercent);
}

// Зажигает светодиоды кольца Volume по кругу пропорционально уровню (0-100%),
// нижний светодиод (VOLUME_RING_SKIP_B) в заливку не участвует
void updateVolumeRing(int percent) {
  lastVolumePercent = percent;
  // Линейно: 0%->0, 50%->6 (ровно половина из 11), 100%->11. Округление до ближайшего
  // (целочисленный трюк +половина знаменателя), а не усечение вниз — иначе верхний
  // светодиод зажигался бы только при значении ровно 100%, а не при приближении к нему
  int litCount = (percent * 11 + 50) / 100;
  litCount = constrain(litCount, 0, 11);
  bool exceededMidPoint = percent > VOLUME_MID_PERCENT; // Строго выше — не на 50% и не ниже
  volumeRing.clear();
  for (int i = 0; i < litCount; i++) {
    // Градиент яркости по кругу: первый горящий светодиод самый тусклый (i=0 -> 1/11),
    // последний — самый яркий (i=10 -> 11/11). Дальше ещё умножается на общий Dimmer
    float fraction = (i + 1) / 11.0;
    if (exceededMidPoint && i == VOLUME_MID_BREATH_LED_INDEX) {
      // 6-й светодиод (горел ещё до 50%) — дышит, пока громкость выше середины шкалы
      fraction = volumeMidBreathBrightness(millis()) / 255.0;
    }
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

// Зажигает "жёлтые" (не центральные) светодиоды кольца Bass/High в сторону "-" или "+"
// от центра по модулю dbValue — на нуле ничего не зажигает (см. renderDbRing). Вынесена
// отдельно, чтобы этой же реальной живой отрисовкой (гаснет по мере приближения к нулю)
// могли пользоваться и анимации Bypass (renderBypassFillAnim/renderBypassBlinkAnim),
// не только обычная отрисовка уровня дБ
void renderDbRingOuterLeds(Adafruit_NeoPixel &ring, int dbValue) {
  if (dbValue > 0) {
    // map(dbValue, 1, 10, 1, 4), не map(dbValue, 0, 10, 0, 4) — целочисленное деление у
    // последнего округляет ЛЮБОЕ dbValue из {1,2} в litCount=0 (1*4/10=0, 2*4/10=0), а раз
    // dbValue!=0 (мы уже не в ветке нуля), это давало ПОЛНОСТЬЮ тёмное кольцо — ни зелёного
    // центра (не на нуле), ни жёлтого края (0 светодиодов). Теперь любое не-нулевое значение
    // зажигает хотя бы 1 светодиод
    int litCount = constrain(map(dbValue, 1, 10, 1, 4), 1, 4);
    for (int i = 0; i < litCount; i++) {
      // Градиент от нуля к краю: ближний к центру светодиод (i=0) самый тусклый,
      // крайний (i=3) самый яркий. Зелёной пары это не касается
      float fraction = (i + 1) / 4.0;
      ring.setPixelColor(ringPositiveOrder[i],
        (uint8_t)(currentRingColorR * fraction),
        (uint8_t)(currentRingColorG * fraction),
        (uint8_t)(currentRingColorB * fraction));
    }
  } else if (dbValue < 0) {
    int litCount = constrain(map(-dbValue, 1, 10, 1, 4), 1, 4);
    for (int i = 0; i < litCount; i++) {
      float fraction = (i + 1) / 4.0;
      ring.setPixelColor(ringNegativeOrder[i],
        (uint8_t)(currentRingColorR * fraction),
        (uint8_t)(currentRingColorG * fraction),
        (uint8_t)(currentRingColorB * fraction));
    }
  }
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
  } else {
    renderDbRingOuterLeds(ring, dbValue);
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
