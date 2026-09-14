#pragma once

// ============================================================================
// display_logic.h — объект OLED-дисплея (U8g2, аппаратный SPI) и все экраны:
// карусель меню, toggle-переключатель (VU Meter/Bypass), круг+стрелка (Bass/
// High/Volume), Dimmer/Color/Source, и текстовые сообщения (POWER ON/OFF).
// ============================================================================

#include <U8g2lib.h>
#include "hardware_settings.h" // DISPLAY_DRIVER_SH1106/SSD1306/SSD1309 — выбор контроллера

// Тип u8g2 переключается вместе с DISPLAY_DRIVER_* (hardware_settings.h) — сменить экран
// можно раскомментировав нужный #define там, без правки этого файла. SH1106 физически имеет
// 132x64 видеопамять со сдвигом на 2 колонки — его драйвер сам учитывает сдвиг; у SSD1306/1309
// сдвига нет — если перепутать (как уже было один раз — экран, подписанный "0.96 OLED", это
// SSD1306, а не SH1106/1.3", хотя изначально его приняли за 1.3"), картинка съедет на 2px и
// с края появится мусорная полоса (несуществующие для этого чипа колонки видеопамяти)
#ifdef DISPLAY_DRIVER_SH1106
extern U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI u8g2;
#elif defined(DISPLAY_DRIVER_SSD1306)
extern U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2;
#elif defined(DISPLAY_DRIVER_SSD1309)
extern U8G2_SSD1309_128X64_NONAME2_F_4W_HW_SPI u8g2;
#else
#error "Выбери DISPLAY_DRIVER_SH1106 / DISPLAY_DRIVER_SSD1306 / DISPLAY_DRIVER_SSD1309 в hardware_settings.h"
#endif

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
// Полноэкранный "сейчас играет" — артист/трек (esp32_link.h), статус воспроизведения и
// источник (всегда "Streamer" — только это и переключает его на экран, см. main.cpp,
// updateNowPlaying()). Как и другие полноэкранные режимы (Mute) — сама очищает буфер
void drawNowPlayingScreen();
// Частичное обновление прогресс-бара трека внутри уже нарисованного Now Playing (тем же
// приёмом, что animate*IconPartial() в animations/ — чистит только свой регион и шлёт по SPI
// только его тайлы через u8g2.updateDisplayArea(), не весь буфер). Вызывать из loop() каждую
// итерацию, пока nowPlayingActive && !nowPlayingMenuVisitActive && !isMuted — сама пропускает
// свой ход, если недавно была любая другая передача на дисплей, и если полоска не сдвинулась
// ни на пиксель с прошлого раза (см. .cpp)
void updateNowPlayingProgress();
// Бегущая строка для длинного названия трека — тот же частичный приём, что и у
// updateNowPlayingProgress(), вызывать из loop() рядом с ней (то же условие: nowPlayingActive
// && !nowPlayingMenuVisitActive && !isMuted). Сама ничего не делает, пока название помещается
// на экран целиком без прокрутки
void updateNowPlayingTitleScroll();
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

