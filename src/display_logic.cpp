#include "display_logic.h"
#include "hardware_settings.h"
#include "main.h"
#include "animation_logic.h"
#include "neopixel.h"
#include "motor_position.h"
#include "animations/bass_volume_high_animation.h"
#include "animations/vu_meter_animation.h"
#include "animations/bypass_animation.h"
#include "animations/dimmer_animation.h"
#include "animations/color_animation.h"
#include "animations/source_animation.h"
#include "animations/eq_animation.h"
#include "animations/info_animation.h"
#include "temperature_sensor.h"
#include "voltage_sensor.h"

U8G2_SSD1309_128X64_NONAME2_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ DISPLAY_CS_PIN, /* dc=*/ DISPLAY_DC_PIN, /* reset=*/ DISPLAY_RESET_PIN);

// Надпись "bypass" в углу экрана — нужна на ВСЕХ экранах (не только карусели меню),
// иначе при переключении Bypass из настроек конкретного пункта не видно подтверждения,
// что оно сработало (drawMenu() туда специально не вызывается — это вызывало скачок
// экрана в карусель и обратно). "mute" здесь не рисуется — пока включён Mute, экран и
// так целиком занят непрерывной MUTE-анимацией (см. animateMuteFrame() в main.cpp), эта
// функция в тот момент вообще не вызывается ни с одного из обычных экранов
static void drawStatusIndicators() {
  if (settings[4] == 1) { // Bypass активен
    u8g2.setFont(STATUS_INDICATOR_FONT);
    u8g2.setCursor(BYPASS_INDICATOR_X, BYPASS_INDICATOR_Y);
    u8g2.print("bypass");
  }
}

// Подчёркивает уже напечатанный по (x, y) текущим шрифтом текст — короткая линия по
// реальной ширине текста (u8g2.getStrWidth()), не во весь экран. Используется для
// названия пункта меню на экранах VU Meter/Color/Source/Dimmer
static void drawTitleUnderline(int x, int y, const char* text, int yOffset) {
  u8g2.drawHLine(x, y + yOffset, u8g2.getStrWidth(text));
}

static unsigned long lastDisplayTransferTime = 0;
static unsigned long lastPartialDisplayTransferTime = 0;

// Гарантирует минимальный зазор с ЛЮБОЙ предыдущей полной передачей на дисплей — неважно,
// какой из draw*()/sendBuffer() её вызвал. Этот дисплей физически "теряет" одну из двух
// передач, если они идут слишком близко по времени друг к другу (см. DISPLAY_REDRAW_MIN_GAP_MS
// и историю фикса в CLAUDE.md, раздел "Анимации-индикаторы пунктов меню") — сначала это
// вылезло только на drawMenu() (навигация не отображалась с первого нажатия). Отдельно —
// более короткий зазор именно с последним ЧАСТИЧНЫМ обновлением иконки (updateDisplayArea()),
// чтобы drawMenu() не стартовал полную передачу прямо во время её хвоста: полный
// DISPLAY_REDRAW_MIN_GAP_MS тут избыточен (частичная передача в разы короче полной) и заметно
// тормозит навигацию, если применять его же
static void waitForDisplayRedrawGap() {
  unsigned long now = millis();
  unsigned long sinceLastFull = now - lastDisplayTransferTime;
  if (lastDisplayTransferTime != 0 && sinceLastFull < DISPLAY_REDRAW_MIN_GAP_MS) {
    delay(DISPLAY_REDRAW_MIN_GAP_MS - sinceLastFull);
    now = millis();
  }
  unsigned long sinceLastPartial = now - lastPartialDisplayTransferTime;
  if (lastPartialDisplayTransferTime != 0 && sinceLastPartial < DISPLAY_PARTIAL_REDRAW_MIN_GAP_MS) {
    delay(DISPLAY_PARTIAL_REDRAW_MIN_GAP_MS - sinceLastPartial);
  }
  lastDisplayTransferTime = millis();
}

void markPartialDisplayTransfer() {
  lastPartialDisplayTransferTime = millis();
}

void drawMenu() {
  waitForDisplayRedrawGap();

  u8g2.setFont(MENU_TITLE_FONT);
  u8g2.clearBuffer();

  // Индикатор текущего пункта — одна строка вертикальных палочек, по одной на пункт;
  // активная смещена вниз относительно остальных (см. MENU_BAR_* в hardware_settings.h)
  int totalWidth = (MENU_ITEM_COUNT - 1) * MENU_BAR_SPACING_X;
  int startX = (128 - totalWidth) / 2 + MENU_BAR_X_OFFSET;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    int x = startX + i * MENU_BAR_SPACING_X;
    int y = MENU_BAR_Y + (i == currentMenuItem ? MENU_BAR_SELECTED_Y_OFFSET : 0);
    u8g2.drawBox(x, y, MENU_BAR_WIDTH, MENU_BAR_HEIGHT);
  }

  u8g2.setCursor((128 - u8g2.getStrWidth(menuItems[currentMenuItem].c_str())) / 2 + MENU_TITLE_X_OFFSET, MENU_TITLE_Y);
  u8g2.print(menuItems[currentMenuItem]);

  // Крутящаяся иконка пункта меню — своя для каждого пункта (или общая для Bass/High/Volume),
  // см. подробности в hardware_settings.h у MENU_ICON_*
  if (menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High" || menuItems[currentMenuItem] == "Volume") {
    drawBassVolumeHighAnim(MENU_ICON_X, MENU_ICON_Y);
  } else if (menuItems[currentMenuItem] == "VU Meter") {
    drawVuMeterAnim(MENU_ICON_X, MENU_ICON_Y);
  } else if (menuItems[currentMenuItem] == "Bypass") {
    drawBypassAnim(MENU_ICON_X, MENU_ICON_Y);
  } else if (menuItems[currentMenuItem] == "Dimmer") {
    drawDimmerAnim(MENU_ICON_X, MENU_ICON_Y);
  } else if (menuItems[currentMenuItem] == "Color") {
    drawColorAnim(MENU_ICON_X, MENU_ICON_Y);
  } else if (menuItems[currentMenuItem] == "Source") {
    drawSourceAnim(MENU_ICON_X, MENU_ICON_Y);
  } else if (menuItems[currentMenuItem] == "EQ") {
    drawEqAnim(EQ_ICON_X, EQ_ICON_Y);
  } else if (menuItems[currentMenuItem] == "Info") {
    drawInfoAnim(MENU_ICON_X, MENU_ICON_Y);
  }

  drawStatusIndicators();

  u8g2.sendBuffer();
}

unsigned long lastMenuDrawTime() {
  return lastDisplayTransferTime;
}

void drawToggleSwitch(bool state) {
  waitForDisplayRedrawGap();

  u8g2.setFont(TOGGLE_FONT);
  u8g2.clearBuffer();

  // Отображаем название пункта меню — подчёркнуто у обоих (VU Meter и Bypass, экран общий),
  // см. TOGGLE_LABEL_UNDERLINE_Y_OFFSET. menuItems[currentMenuItem].c_str(), а не
  // захардкоженная строка — экран общий на два разных названия
  u8g2.setCursor(TOGGLE_LABEL_X, TOGGLE_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);
  drawTitleUnderline(TOGGLE_LABEL_X, TOGGLE_LABEL_Y, menuItems[currentMenuItem].c_str(), TOGGLE_LABEL_UNDERLINE_Y_OFFSET);

  // "Pill"-переключатель — позиция/размер через TOGGLE_* в hardware_settings.h. OFF:
  // дорожка пустая (только контур), бегунок закрашен слева. ON: дорожка залита целиком,
  // бегунок — "вырезанный" (setDrawColor(0)) кружок с контуром справа, чтобы не сливаться
  // с залитой дорожкой на монохромном экране
  int trackX = TOGGLE_SWITCH_X;
  int trackY = TOGGLE_SWITCH_Y;
  int trackW = TOGGLE_SWITCH_WIDTH;
  int trackH = TOGGLE_SWITCH_HEIGHT;
  int radius = trackH / 2;
  int knobRadius = radius - TOGGLE_KNOB_MARGIN;
  int knobY = trackY + radius;
  int knobXOff = trackX + radius; // OFF — бегунок у левого края
  int knobXOn = trackX + trackW - radius; // ON — бегунок у правого края

  u8g2.drawRFrame(trackX, trackY, trackW, trackH, radius);
  if (state) {
    u8g2.drawRBox(trackX, trackY, trackW, trackH, radius);
    u8g2.setDrawColor(0);
    u8g2.drawDisc(knobXOn, knobY, knobRadius);
    u8g2.setDrawColor(1);
    u8g2.drawCircle(knobXOn, knobY, knobRadius);
  } else {
    u8g2.drawDisc(knobXOff, knobY, knobRadius);
  }

  // Подпись состояния — фиксированная позиция справа от дорожки (не скачет между
  // сторонами, как было раньше)
  u8g2.setFont(TOGGLE_STATE_FONT);
  u8g2.setCursor(trackX + TOGGLE_STATE_TEXT_X_OFFSET, trackY + TOGGLE_STATE_TEXT_Y_OFFSET);
  u8g2.print(state ? "On" : "Off");

  drawStatusIndicators();

  u8g2.sendBuffer();
}

void drawArrowIndicator(int settingValue, bool showArrowRight, bool showArrowLeft) {
  waitForDisplayRedrawGap();

  u8g2.clearBuffer();

  bool isVolume = (menuItems[currentMenuItem] == "Volume");

  // Название пункта меню — позиция и шрифт настраиваются через LABEL_*/VOLUME_LABEL_*
  u8g2.setFont(isVolume ? VOLUME_LABEL_FONT : LABEL_FONT);
  u8g2.setCursor(isVolume ? VOLUME_LABEL_X : LABEL_X, isVolume ? VOLUME_LABEL_Y : LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);

  // Стрелка и прогресс-бар привязаны к реальному физическому положению ручки со
  // своего потенциометра для Bass/High/Volume, а не к цели settingValue — чтобы
  // кружок показывал, где ручка находится сейчас, а не куда её тянет мотор.
  // Volume — шкала 0-100%, Bass/High — шкала в дБ (диапазон берём из калибровки)
  int valueMin = currentPotValueMin();
  int valueMax = currentPotValueMax();
  const char* unit = currentPotIsDb() ? "dB" : "%";
  int potRaw;
  int potValueRaw = readCurrentPotPercent(&potRaw);

  int potValue = potValueRaw;

  if (isVolume) {
    updateVolumeRing(potValue);
  } else if (bypassAnimMode == 0) { // Не перерисовываем кольцо поверх анимации переключения Bypass
    if (menuItems[currentMenuItem] == "Bass") {
      renderDbRing(bassRing, potValue, bassRingState);
    } else if (menuItems[currentMenuItem] == "High") {
      renderDbRing(highRing, potValue, highRingState);
    }
  }
  // Кусочно-линейно, не одним map(): от центра (0dB/50%) до половины пути к краю угол
  // растёт до ARROW_NEEDLE_HORIZONTAL_ANGLE_DEG (строго горизонтально на ±5дБ у Bass/High
  // и на 25%/75% у Volume), а от половины пути до самого края — до ARROW_NEEDLE_MAX_ANGLE_DEG.
  // На центральном значении угол ровно 0°, и стрелка (см. arrowX/arrowY ниже) смотрит точно
  // вверх — перпендикулярно горизонту
  float halfRange = (valueMax - valueMin) / 2.0;
  float center = (valueMin + valueMax) / 2.0;
  float fraction = (halfRange != 0) ? (potValue - center) / halfRange : 0; // -1..1
  float absFraction = fabs(fraction);
  float sign = (fraction < 0) ? -1.0 : 1.0;
  float angleDeg;
  if (absFraction <= 0.5) {
    angleDeg = fraction * (ARROW_NEEDLE_HORIZONTAL_ANGLE_DEG * 2.0);
  } else {
    angleDeg = sign * (ARROW_NEEDLE_HORIZONTAL_ANGLE_DEG
        + (ARROW_NEEDLE_MAX_ANGLE_DEG - ARROW_NEEDLE_HORIZONTAL_ANGLE_DEG) * (absFraction - 0.5) * 2.0);
  }
  int angleValue = (int)(angleDeg + (angleDeg >= 0 ? 0.5 : -0.5)); // Округление к ближайшему

  Serial.print(menuItems[currentMenuItem]);
  Serial.print(" pot raw: ");
  Serial.print(potRaw);
  Serial.print(" -> ");
  Serial.print(potValueRaw);
  Serial.print(unit);
  Serial.print("  (shown: ");
  Serial.print(potValue);
  Serial.print(unit);
  Serial.println(")");

  // Рисуем кружок и стрелочку — позиция/размер настраиваются через ARROW_CIRCLE_*/
  // ARROW_NEEDLE_LENGTH в hardware_settings.h
  int x = ARROW_CIRCLE_X;
  int y = ARROW_CIRCLE_Y;
  u8g2.drawCircle(x, y, ARROW_CIRCLE_RADIUS);
  int arrowX = x + ARROW_NEEDLE_LENGTH * sin(radians(angleValue));
  int arrowY = y - ARROW_NEEDLE_LENGTH * cos(radians(angleValue));
  u8g2.drawLine(x, y, arrowX, arrowY); // Стрелочка

  // Отрисовка стрелочек направления — позиция/размер через ARROW_INDICATOR_Y_*/ARROW_*_X_*
  if (showArrowRight) {
    u8g2.drawTriangle(ARROW_RIGHT_X_NEAR, ARROW_INDICATOR_Y_TOP, ARROW_RIGHT_X_FAR, ARROW_INDICATOR_Y_MID, ARROW_RIGHT_X_NEAR, ARROW_INDICATOR_Y_BOTTOM); // Стрелочка вправо
  }
  if (showArrowLeft) {
    u8g2.drawTriangle(ARROW_LEFT_X_NEAR, ARROW_INDICATOR_Y_TOP, ARROW_LEFT_X_FAR, ARROW_INDICATOR_Y_MID, ARROW_LEFT_X_NEAR, ARROW_INDICATOR_Y_BOTTOM); // Стрелочка влево
  }

  // Отрисовка остальных элементов — позиция/размер через PROGRESS_LINE_*/PROGRESS_BAR_*.
  // Засечки на минимуме/центре/максимуме шкалы — читаемость без цифровой разметки,
  // особенно центра (0dB у Bass/High)
  int progressCenterX = (PROGRESS_BAR_X_MIN + PROGRESS_BAR_X_MAX) / 2;
  u8g2.drawVLine(PROGRESS_BAR_X_MIN, PROGRESS_LINE_Y - PROGRESS_TICK_HEIGHT, PROGRESS_TICK_HEIGHT);
  u8g2.drawVLine(progressCenterX, PROGRESS_LINE_Y - PROGRESS_TICK_HEIGHT, PROGRESS_TICK_HEIGHT);
  u8g2.drawVLine(PROGRESS_BAR_X_MAX, PROGRESS_LINE_Y - PROGRESS_TICK_HEIGHT, PROGRESS_TICK_HEIGHT);
  u8g2.drawHLine(PROGRESS_LINE_X, PROGRESS_LINE_Y, PROGRESS_LINE_WIDTH);
  int progressBarPos = map(potValue, valueMin, valueMax, PROGRESS_BAR_X_MIN, PROGRESS_BAR_X_MAX);
  u8g2.drawBox(progressBarPos, PROGRESS_BAR_Y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT);

  // Счётчик %/dB — позиция и шрифт настраиваются через VALUE_*/VOLUME_VALUE_*
  u8g2.setFont(isVolume ? VOLUME_VALUE_FONT : VALUE_FONT);
  u8g2.setCursor(isVolume ? VOLUME_VALUE_X : VALUE_X, isVolume ? VOLUME_VALUE_Y : VALUE_Y);
  u8g2.print(potValue);
  u8g2.print(unit);

  drawStatusIndicators();

  u8g2.sendBuffer();
}

// Рисует одну строку списка настроек ("LED 20%", "AUX", ...) с подсветкой, если это
// активный/выбранный вариант — скруглённый залитый прямоугольник позади текста с
// инвертированным текстом поверх (setDrawColor(0)), вместо старого "> " перед текстом,
// плюс отдельная точка слева от строки (доп. индикатор выбора). Размер подсветки — по
// реальной ширине строки (u8g2.getStrWidth()) и ascent/descent ТЕКУЩЕГО шрифта (вызывающий
// код должен успеть setFont() до вызова), а не подогнан на глаз. Общий приём для списков
// в настройках — используется и в Dimmer, и в Source
static void drawHighlightedRow(int x, int y, const char* text, bool isActive, int padX, int padY, int radius, int dotRadius, int dotXOffset) {
  if (isActive) {
    int textWidth = u8g2.getStrWidth(text);
    int boxX = x - padX;
    int boxY = y - u8g2.getAscent() - padY;
    int boxW = textWidth + padX * 2;
    int boxH = (u8g2.getAscent() - u8g2.getDescent()) + padY * 2;
    u8g2.drawRBox(boxX, boxY, boxW, boxH, radius);
    u8g2.setDrawColor(0);
    u8g2.setCursor(x, y);
    u8g2.print(text);
    u8g2.setDrawColor(1);
    int dotY = y - (u8g2.getAscent() + u8g2.getDescent()) / 2;
    u8g2.drawDisc(x - dotXOffset, dotY, dotRadius);
  } else {
    u8g2.setCursor(x, y);
    u8g2.print(text);
  }
}

void drawDimmerScreen() {
  waitForDisplayRedrawGap();

  u8g2.setFont(DIMMER_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(DIMMER_LABEL_X, DIMMER_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);
  drawTitleUnderline(DIMMER_LABEL_X, DIMMER_LABEL_Y, "Dimmer", DIMMER_LABEL_UNDERLINE_Y_OFFSET);

  // Две строки — яркость колец и яркость дисплея; какая активна — dimmerEditingDisplay,
  // переключается Up/Down на пульте (или вращением энкодера, пока не подтверждена кликом)
  u8g2.setFont(DIMMER_ROW_FONT);
  char ledRow[16];
  snprintf(ledRow, sizeof(ledRow), "LED %d%%", settings[currentMenuItem]);
  drawHighlightedRow(DIMMER_ROW_X, DIMMER_ROW1_Y, ledRow, !dimmerEditingDisplay,
    DIMMER_ROW_HIGHLIGHT_PAD_X, DIMMER_ROW_HIGHLIGHT_PAD_Y, DIMMER_ROW_HIGHLIGHT_RADIUS,
    DIMMER_ROW_DOT_RADIUS, DIMMER_ROW_DOT_X_OFFSET);

  char displayRow[16];
  snprintf(displayRow, sizeof(displayRow), "Display %d%%", displayBrightness);
  drawHighlightedRow(DIMMER_ROW_X, DIMMER_ROW2_Y, displayRow, dimmerEditingDisplay,
    DIMMER_ROW_HIGHLIGHT_PAD_X, DIMMER_ROW_HIGHLIGHT_PAD_Y, DIMMER_ROW_HIGHLIGHT_RADIUS,
    DIMMER_ROW_DOT_RADIUS, DIMMER_ROW_DOT_X_OFFSET);

  drawStatusIndicators();

  u8g2.sendBuffer();
}

void applyDisplayBrightness() {
  u8g2.setContrast(map(displayBrightness, 0, 100, 0, 255));
}

// Квадратик-образец яркости выбранного цвета — монохромный экран не может показать сам
// цвет (r/g/b), поэтому это дизеринг: каждый пиксель зажигается с вероятностью
// luminance/255 (random(256) < luminance), где luminance = (r*299+g*587+b*114)/1000 —
// чем выше яркость, тем плотнее точки, вплоть до почти сплошной заливки у самых ярких
// цветов (White и т.п.). Узор точек — свой, но ФИКСИРОВАННЫЙ у каждого цвета (не
// перегенерируется на каждый redraw): randomSeed() от самих r/g/b (не от индекса в
// ringColorPalette[] — так узор переживает переупорядочивание палитры) даёт один и тот же
// узор каждый раз при выборе этого цвета, и он отличается от узора других цветов —
// значит, цвета можно узнавать по узору точек, а не только по плотности заливки.
// Multiplicative hash (2654435761 — множитель Кнута) разносит соседние по таблице r/g/b,
// чтобы у похожих по яркости соседних цветов не совпадали и узоры
static void drawBrightnessSwatch(int x, int y, int size, uint8_t r, uint8_t g, uint8_t b) {
  uint32_t seed = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  randomSeed(seed * 2654435761UL);
  uint16_t luminance = ((uint16_t)r * 299 + (uint16_t)g * 587 + (uint16_t)b * 114) / 1000;
  for (int yy = 0; yy < size; yy++) {
    for (int xx = 0; xx < size; xx++) {
      if ((uint16_t)random(256) < luminance) {
        u8g2.drawPixel(x + xx, y + yy);
      }
    }
  }
  u8g2.drawFrame(x, y, size, size);
}

void drawColorScreen(int colorIndex) {
  waitForDisplayRedrawGap();

  u8g2.setFont(COLOR_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(COLOR_LABEL_X, COLOR_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);
  drawTitleUnderline(COLOR_LABEL_X, COLOR_LABEL_Y, "Color", COLOR_LABEL_UNDERLINE_Y_OFFSET);

  u8g2.setFont(COLOR_VALUE_FONT);
  u8g2.setCursor(COLOR_VALUE_X, COLOR_VALUE_Y);
  u8g2.print(ringColorPalette[colorIndex].name);

  drawBrightnessSwatch(COLOR_SWATCH_X, COLOR_SWATCH_Y, COLOR_SWATCH_SIZE,
    ringColorPalette[colorIndex].r, ringColorPalette[colorIndex].g, ringColorPalette[colorIndex].b);

  drawStatusIndicators();

  u8g2.sendBuffer();
}

void drawSourceScreen(int sourceIndex) {
  waitForDisplayRedrawGap();

  u8g2.setFont(SOURCE_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(SOURCE_LABEL_X, SOURCE_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);
  drawTitleUnderline(SOURCE_LABEL_X, SOURCE_LABEL_Y, "Source", SOURCE_LABEL_UNDERLINE_Y_OFFSET);

  // Список ВСЕХ источников (не только текущего) — активный подсвечен (см.
  // drawHighlightedRow() выше), чтобы сразу видеть остальные варианты и куда крутить,
  // а не только текущее значение. Растёт вниз сам по SOURCE_COUNT — не привязан к
  // конкретному числу источников
  u8g2.setFont(SOURCE_LIST_FONT);
  for (int i = 0; i < SOURCE_COUNT; i++) {
    int y = SOURCE_LIST_Y_START + i * SOURCE_LIST_LINE_HEIGHT;
    drawHighlightedRow(SOURCE_LIST_X, y, sourceNames[i], i == sourceIndex,
      SOURCE_ROW_HIGHLIGHT_PAD_X, SOURCE_ROW_HIGHLIGHT_PAD_Y, SOURCE_ROW_HIGHLIGHT_RADIUS,
      SOURCE_ROW_DOT_RADIUS, SOURCE_ROW_DOT_X_OFFSET);
  }

  drawStatusIndicators();

  u8g2.sendBuffer();
}

void drawEqScreen(int eqIndex) {
  waitForDisplayRedrawGap();

  u8g2.setFont(EQ_LABEL_FONT);
  u8g2.clearBuffer();

  // Справа, по вертикали по центру экрана, крупным шрифтом — в отличие от Source/Dimmer,
  // где заголовок мелкий и сверху. Список ниже показывает только названия пресетов (без
  // значений дБ), он узкий и не пересекается с заголовком у правого края
  int labelX = 128 - u8g2.getStrWidth("EQ") - EQ_LABEL_RIGHT_MARGIN + EQ_LABEL_X_OFFSET;
  u8g2.setCursor(labelX, EQ_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);
  drawTitleUnderline(labelX, EQ_LABEL_Y, "EQ", EQ_LABEL_UNDERLINE_Y_OFFSET);

  // EQ_COUNT пресетов не помещаются на экране все разом (EQ_LIST_VISIBLE_ROWS зараз,
  // в отличие от Source, где SOURCE_COUNT подобран так, чтобы влезть целиком) — окно
  // центрируется на выбранном пункте и не выходит за границы списка
  int windowStart = eqIndex - (EQ_LIST_VISIBLE_ROWS - 1) / 2;
  windowStart = constrain(windowStart, 0, max(0, EQ_COUNT - EQ_LIST_VISIBLE_ROWS));

  u8g2.setFont(EQ_LIST_FONT);
  for (int row = 0; row < EQ_LIST_VISIBLE_ROWS; row++) {
    int i = windowStart + row;
    if (i >= EQ_COUNT) {
      break;
    }
    int y = EQ_LIST_Y_START + row * EQ_LIST_LINE_HEIGHT;
    drawHighlightedRow(EQ_LIST_X, y, eqPresets[i].name, i == eqIndex,
      EQ_ROW_HIGHLIGHT_PAD_X, EQ_ROW_HIGHLIGHT_PAD_Y, EQ_ROW_HIGHLIGHT_RADIUS,
      EQ_ROW_DOT_RADIUS, EQ_ROW_DOT_X_OFFSET);
  }

  drawStatusIndicators();

  u8g2.sendBuffer();
}

void drawInfoScreen() {
  waitForDisplayRedrawGap();

  u8g2.setFont(INFO_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(INFO_LABEL_X, INFO_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);
  drawTitleUnderline(INFO_LABEL_X, INFO_LABEL_Y, "Info", INFO_LABEL_UNDERLINE_Y_OFFSET);

  float temps[3];
  readAllTemperatures(temps);

  u8g2.setFont(INFO_ROW_FONT);
  for (int i = 0; i < 3; i++) {
    int y = INFO_LIST_Y_START + i * INFO_LIST_LINE_HEIGHT;
    u8g2.setCursor(INFO_ROW_X, y);
    u8g2.print(tempSensorLabels[i]);
    u8g2.print(": ");
    if (temps[i] == TEMP_SENSOR_INVALID) {
      u8g2.print("--");
    } else {
      u8g2.print(temps[i], 1);
      u8g2.print("C");
    }
  }

  int voltageRaw;
  int voltage = readMainsVoltage(&voltageRaw);
  // Для калибровки/подстройки подстроечника на модуле — крути его и подай известное
  // напряжение (мультиметром на розетке), смотри raw в Serial Monitor, потом подставь
  // пару (raw, вольты) в mainsVoltageCalRaw[]/mainsVoltageCalValue[] (hardware_settings.h)
  Serial.print("Voltage sensor raw: ");
  Serial.print(voltageRaw);
  Serial.print(" -> ");
  Serial.print(voltage);
  Serial.println("V");

  u8g2.setFont(INFO_VOLTAGE_FONT);
  u8g2.setCursor(INFO_VOLTAGE_X, INFO_VOLTAGE_Y);
  u8g2.print(INFO_VOLTAGE_LABEL);
  u8g2.print(":");
  u8g2.print(voltage);
  u8g2.print("V");

  drawStatusIndicators();

  u8g2.sendBuffer();
}

void displayMessage(const char* message) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr); // Установка шрифта меньшего размера
  int strWidth = u8g2.getStrWidth(message);
  u8g2.setCursor((128 - strWidth) / 2, 32); // Центрирование сообщения
  u8g2.print(message);
  u8g2.sendBuffer();
  delay(3000); // Задержка 3 секунды
}
