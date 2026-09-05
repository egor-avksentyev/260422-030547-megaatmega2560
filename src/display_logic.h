#pragma once

// ============================================================================
// display_logic.h — объект OLED-дисплея (U8g2, аппаратный SPI) и все экраны:
// карусель меню, toggle-переключатель (VU Meter/Bypass), круг+стрелка (Bass/
// High/Volume), Dimmer/Color/Source, и текстовые сообщения (POWER ON/OFF).
// ============================================================================

#include <U8g2lib.h>

// SSD1309, не SSD1306 — контраст-команда (0x81) у них байт-в-байт одинаковая в u8g2, но
// init-последовательность (precharge/VCOMH) разная; если физически стоит SSD1309, а
// использовать SSD1306-драйвер, эти настройки могли "забить" видимый эффект контраста
extern U8G2_SSD1309_128X64_NONAME2_F_4W_HW_SPI u8g2;

void drawMenu();
void drawToggleSwitch(bool state);
void drawArrowIndicator(int settingValue, bool showArrowRight, bool showArrowLeft);
// Без параметров — читает settings[currentMenuItem] (яркость колец) и displayBrightness
// (яркость дисплея) напрямую, плюс dimmerEditingDisplay (main.h), чтобы отметить активную
// строку. Раньше принимала percent, но теперь показывает две независимые строки, а не одну
void drawDimmerScreen();
void drawColorScreen(int colorIndex);
void drawSourceScreen(int sourceIndex);
void drawEqScreen(int eqIndex);
void drawInfoScreen();
void displayMessage(const char* message);
// Применяет displayBrightness (main.h, пункт "Dimmer", вторая строка) как реальный
// контраст OLED-дисплея (0-100% -> u8g2.setContrast(0-255))
void applyDisplayBrightness();

// Момент последней ПОЛНОЙ перерисовки drawMenu() (millis()) — используется
// bass_volume_high_animation.cpp (и аналогичными файлами), чтобы фоновое частичное
// обновление иконки не лезло в SPI слишком близко по времени к полной перерисовке от навигации
unsigned long lastMenuDrawTime();

// Отмечает момент ЧАСТИЧНОЙ передачи (updateDisplayArea() из animate*IconPartial()) —
// чтобы drawMenu() знал о ней и не стартовал полную перерисовку прямо во время её хвоста
// (короткий зазор DISPLAY_PARTIAL_REDRAW_MIN_GAP_MS, не полный DISPLAY_REDRAW_MIN_GAP_MS)
void markPartialDisplayTransfer();

// Компактный оверлей положения ручки (label — "Bass"/"High"/"Vol", value+unit — "-3"+"dB"
// и т.п.) — рисуется ЧАСТИЧНО (updateDisplayArea(), не весь буфер, и НЕ трогает NeoPixel
// вообще — только OLED), поверх ТОЙ ЖЕ области, что обычно занимает крутящаяся иконка
// карусели (MENU_ICON_X/Y). Используется main.cpp, пока пользователь крутит Bass/High/Volume
// РУКОЙ, сидя на карусели меню. Область самовосстанавливается: как только main.cpp
// перестаёт вызывать эту функцию (истёк idle-таймаут), обычная иконка карусели
// (animate*IconPartial()) сама перерисует эту же область на следующем плановом тике
void drawKnobPositionOverlay(const char* label, int value, const char* unit);
