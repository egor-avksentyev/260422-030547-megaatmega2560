#pragma once

// ============================================================================
// display_logic.h — объект OLED-дисплея (U8g2, аппаратный SPI) и все экраны:
// карусель меню, toggle-переключатель (VU Meter/Bypass), круг+стрелка (Bass/
// High/Volume), Dimmer/Color/Source, и текстовые сообщения (POWER ON/OFF).
// ============================================================================

#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2;

void drawMenu();
void drawToggleSwitch(bool state);
void drawArrowIndicator(int settingValue, bool showArrowRight, bool showArrowLeft);
void drawDimmerScreen(int percent);
void drawColorScreen(int colorIndex);
void drawSourceScreen(int sourceIndex);
void displayMessage(const char* message);
