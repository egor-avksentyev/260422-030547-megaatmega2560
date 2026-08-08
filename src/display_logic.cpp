#include "display_logic.h"
#include "hardware_settings.h"
#include "main.h"
#include "animation_logic.h"
#include "neopixel.h"
#include "motor_position.h"

U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ DISPLAY_CS_PIN, /* dc=*/ DISPLAY_DC_PIN, /* reset=*/ DISPLAY_RESET_PIN);

void drawMenu() {
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

  if (isMuted) {
    u8g2.setFont(STATUS_INDICATOR_FONT);
    u8g2.setCursor(MUTE_INDICATOR_X, MUTE_INDICATOR_Y);
    u8g2.print("mute");
  }

  if (settings[4] == 1) { // Bypass активен
    u8g2.setFont(STATUS_INDICATOR_FONT);
    u8g2.setCursor(BYPASS_INDICATOR_X, BYPASS_INDICATOR_Y);
    u8g2.print("bypass");
  }

  u8g2.sendBuffer();
}

void drawToggleSwitch(bool state) {
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.clearBuffer();

  // Отображаем название пункта меню в правом верхнем углу
  u8g2.setCursor(20, 20);
  u8g2.print(menuItems[currentMenuItem]);

  // Отрисовка toggle switch
  int x = 40;
  int y = 40;
  u8g2.drawFrame(x, y, 50, 18); // Рамка переключателя
  if (state) {
    u8g2.drawBox(x + 25, y, 25, 18); // Положение ON
    u8g2.setCursor(x + 55, y + 15);
    u8g2.print("On");
  } else {
    u8g2.drawBox(x, y, 25, 18); // Положение OFF
    u8g2.setCursor(x - 35, y + 15);
    u8g2.print("Off");
  }

  u8g2.sendBuffer();
}

void drawArrowIndicator(int settingValue, bool showArrowRight, bool showArrowLeft) {
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
  int angleValue = map(potValue, valueMin, valueMax, -120, 120) + 10; // +10° смещение угла стрелки (применяется везде)

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

  // Рисуем кружок и стрелочку
  int x = 20; // Круг в левой верхней части экрана
  int y = 20;
  int circleRadius = 14; // Кружок уменьшен на 30%
  u8g2.drawCircle(x, y, circleRadius);
  int needleLength = 18; // Длиннее радиуса — стрелка выходит за пределы кружка
  int arrowX = x + needleLength * sin(radians(angleValue));
  int arrowY = y - needleLength * cos(radians(angleValue));
  u8g2.drawLine(x, y, arrowX, arrowY); // Стрелочка

  // Отрисовка стрелочек
  if (showArrowRight) {
    u8g2.drawTriangle(110, 30, 120, 35, 110, 40); // Стрелочка вправо
  }
  if (showArrowLeft) {
    u8g2.drawTriangle(40, 30, 30, 35, 40, 40); // Стрелочка влево
  }

  // Отрисовка остальных элементов
  u8g2.drawHLine(20, 45, 88);
  int progressBarPos = map(potValue, valueMin, valueMax, 20, 108);
  u8g2.drawBox(progressBarPos, 47, 4, 12);

  // Счётчик %/dB — позиция и шрифт настраиваются через VALUE_*/VOLUME_VALUE_*
  u8g2.setFont(isVolume ? VOLUME_VALUE_FONT : VALUE_FONT);
  u8g2.setCursor(isVolume ? VOLUME_VALUE_X : VALUE_X, isVolume ? VOLUME_VALUE_Y : VALUE_Y);
  u8g2.print(potValue);
  u8g2.print(unit);

  u8g2.sendBuffer();
}

void drawDimmerScreen(int percent) {
  u8g2.setFont(DIMMER_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(DIMMER_LABEL_X, DIMMER_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);

  u8g2.setFont(DIMMER_VALUE_FONT);
  u8g2.setCursor(DIMMER_VALUE_X, DIMMER_VALUE_Y);
  u8g2.print(percent);
  u8g2.print("%");

  u8g2.sendBuffer();
}

void drawColorScreen(int colorIndex) {
  u8g2.setFont(COLOR_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(COLOR_LABEL_X, COLOR_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);

  u8g2.setFont(COLOR_VALUE_FONT);
  u8g2.setCursor(COLOR_VALUE_X, COLOR_VALUE_Y);
  u8g2.print(ringColorPalette[colorIndex].name);

  u8g2.sendBuffer();
}

void drawSourceScreen(int sourceIndex) {
  u8g2.setFont(SOURCE_LABEL_FONT);
  u8g2.clearBuffer();

  u8g2.setCursor(SOURCE_LABEL_X, SOURCE_LABEL_Y);
  u8g2.print(menuItems[currentMenuItem]);

  u8g2.setFont(SOURCE_VALUE_FONT);
  u8g2.setCursor(SOURCE_VALUE_X, SOURCE_VALUE_Y);
  u8g2.print(sourceIndex + 1);

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
