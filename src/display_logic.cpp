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

  // Точки меню в 2 ряда по MENU_DOTS_PER_ROW
  int totalWidth = MENU_DOTS_PER_ROW * MENU_DOT_SPACING_X;
  int startX = (MENU_DOT_CENTER_WIDTH - totalWidth) / 2; // Вычисление стартовой позиции

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    int row = i / MENU_DOTS_PER_ROW;
    int col = i % MENU_DOTS_PER_ROW;
    int rowXOffset = (row == 0) ? MENU_DOT_ROW1_X_OFFSET : MENU_DOT_ROW2_X_OFFSET;
    int x = startX + col * MENU_DOT_SPACING_X + rowXOffset;
    int y = MENU_DOT_ROW1_Y + row * MENU_DOT_ROW_SPACING_Y;
    if (i == currentMenuItem) {
      u8g2.drawDisc(x, y, MENU_DOT_RADIUS, U8G2_DRAW_ALL);
    } else {
      u8g2.drawCircle(x, y, MENU_DOT_RADIUS);
    }
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

  // Отображаем название пункта меню
  u8g2.setCursor(TOGGLE_LABEL_X, TOGGLE_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);

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
  // Без смещения: на центральном значении (0dB для Bass/High, 50% для Volume) map() даёт
  // ровно 0°, и стрелка (см. arrowX/arrowY ниже) смотрит точно вверх — перпендикулярно
  // горизонту. Раньше был "+10" сдвиг, из-за которого стрелка была не строго вертикальна
  // именно в центральной точке
  int angleValue = map(potValue, valueMin, valueMax, -120, 120);

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

  // Отрисовка остальных элементов — позиция/размер через PROGRESS_LINE_*/PROGRESS_BAR_*
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

void drawDimmerScreen() {
  waitForDisplayRedrawGap();

  u8g2.setFont(DIMMER_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(DIMMER_LABEL_X, DIMMER_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);

  // Две строки — яркость колец и яркость дисплея; активная (та, что сейчас крутит Left/Right)
  // отмечена "> " перед текстом, см. dimmerEditingDisplay (main.h), переключается Up/Down
  u8g2.setFont(DIMMER_ROW_FONT);
  u8g2.setCursor(DIMMER_ROW_X, DIMMER_ROW1_Y);
  u8g2.print(dimmerEditingDisplay ? "  LED " : "> LED ");
  u8g2.print(settings[currentMenuItem]);
  u8g2.print("%");

  u8g2.setCursor(DIMMER_ROW_X, DIMMER_ROW2_Y);
  u8g2.print(dimmerEditingDisplay ? "> Display " : "  Display ");
  u8g2.print(displayBrightness);
  u8g2.print("%");

  drawStatusIndicators();

  u8g2.sendBuffer();
}

void applyDisplayBrightness() {
  u8g2.setContrast(map(displayBrightness, 0, 100, 0, 255));
}

void drawColorScreen(int colorIndex) {
  waitForDisplayRedrawGap();

  u8g2.setFont(COLOR_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(COLOR_LABEL_X, COLOR_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);

  u8g2.setFont(COLOR_VALUE_FONT);
  u8g2.setCursor(COLOR_VALUE_X, COLOR_VALUE_Y);
  u8g2.print(ringColorPalette[colorIndex].name);

  drawStatusIndicators();

  u8g2.sendBuffer();
}

void drawSourceScreen(int sourceIndex) {
  waitForDisplayRedrawGap();

  u8g2.setFont(SOURCE_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(SOURCE_LABEL_X, SOURCE_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);

  u8g2.setFont(SOURCE_VALUE_FONT);
  u8g2.setCursor(SOURCE_VALUE_X, SOURCE_VALUE_Y);
  u8g2.print(sourceNames[sourceIndex]);

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
