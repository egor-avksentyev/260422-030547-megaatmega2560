#pragma once

// ============================================================================
// hardware_settings.h — все настройки "в цифрах" в одном месте: пины, тайминги,
// калибровочные таблицы, позиции/шрифты элементов на экране. Логика (.cpp файлы)
// эти значения только читает, не переопределяет. Если нужно подвинуть надпись,
// поменять скорость мотора или перекалибровать потенциометр — правь только здесь.
// ============================================================================

#include <Arduino.h>

// --- Дисплей (SSD1306, аппаратный SPI). SCK/MOSI — фиксированные пины SPI на
// Mega (52/51), их сменить нельзя; CS/DC/RESET — любые свободные цифровые пины ---
#define DISPLAY_CS_PIN 10
#define DISPLAY_DC_PIN 9
#define DISPLAY_RESET_PIN 12

// --- Энкодер и его кнопка ---
#define ENCODER_A_PIN 18 // INT5 — обязательно пин с аппаратным прерыванием (алгоритм Мазурова)
#define ENCODER_B_PIN 20 // INT3 — обязательно пин с аппаратным прерыванием (алгоритм Мазурова)
#define BUTTON_PIN 8
#define DOUBLE_CLICK_THRESHOLD_MS 300 // Порог для обнаружения двойного нажатия кнопки энкодера

// Сколько валидных переходов таблицы Мазурова соответствуют одному физическому щелчку
// энкодера. Раньше код считал только 1 фронт на щелчок, поэтому "1 щелчок = 1 encoderValue".
// Таблица Мазурова видит ВСЕ переходы — если после прошивки пункты меню перескакивают
// через один (щёлкнул один раз — сдвинулось на два), увеличь это число до 4 и наоборот
#define ENCODER_STEPS_PER_DETENT 2

// --- Моторы Bass/High (motorControl, один провод направления + один PWM) и
// Volume (motorControl2, два провода направления + два PWM) ---
#define MOTOR1_IN 40
#define MOTOR1_PWM 3
#define MOTOR2_IN 42
#define MOTOR2_PWM 5
#define MOTOR3_IN1 32
#define MOTOR3_IN2 34
#define MOTOR3_PWM1 2
#define MOTOR3_PWM2 4
#define SLIDER_MOTOR_SPEED 40 // Фиксированная скорость моторов Bass/High/Volume при вращении энкодера/пульта (диапазон -50..50)
#define SLIDER_MOTOR_IDLE_TIMEOUT 120 // мс без новых команд от энкодера/пульта — мотор мгновенно останавливается

// --- Потенциометры обратной связи положения ручек (+5V/GND по краям, средний вывод сюда) ---
#define BASS_POT_PIN A9
#define HIGH_POT_PIN A10
#define VOLUME_POT_PIN A8

// --- Реле (все активны по HIGH) ---
#define RELAY_PIN_STANDBY 22
#define RELAY_PIN_VU_METER 24
#define RELAY_PIN_LED 26
#define RELAY_PIN_MUTE 28
// Реле переключения источника (пункт меню "Source") — взаимоисключающе, работает
// только одно из трёх одновременно
#define SOURCE_RELAY_1_PIN 30
#define SOURCE_RELAY_2_PIN 44
#define SOURCE_RELAY_3_PIN 46
#define SOURCE_COUNT 3

// --- ИК-приёмник и коды команд с пульта ---
#define IR_PIN 19
#define IR_RIGHT 0x79
#define IR_LEFT 0xF9
#define IR_ENTER 0x7B
#define IR_MUTE 0x38
#define IR_POWER 0xB9

// --- Светодиоды пунктов меню Bass/High/Volume ---
#define LED_BASS_PIN 39
#define LED_HIGH_PIN 41
#define LED_VOLUME_PIN 43

// --- Физическая кнопка Bypass (кнопка на GND, INPUT_PULLUP): каждое нажатие
// переключает Bypass в противоположное состояние (не привязана к физическому
// положению, а именно к моменту нажатия) — работает независимо от пункта меню
// "Bypass", синхронизируя то же самое settings[4] ---
#define BYPASS_BUTTON_PIN 31
#define BYPASS_LED_PIN 45 // Горит, когда Bypass ВЫКЛЮЧЕН

// ============================================================================
// NeoPixel-кольца вокруг ручек Bass/High/Volume
// ============================================================================

// Volume: 12 светодиодов, 2 нижних не используются, шкала 0-100% заливается по кругу
#define VOLUME_RING_PIN 6 // DATA-провод ленты
#define VOLUME_RING_COUNT 12
#define VOLUME_RING_DEFAULT_DIMMER 20 // % от максимума (255) — регулируется в меню "Dimmer"
// Индексы (0-11) двух "нижних" светодиодов, которые всегда должны быть выключены.
// Подбери по факту после прошивки — зависит от того, с какого физического светодиода
// у тебя начинается адресация ленты (индекс 0)
#define VOLUME_RING_SKIP_A 11
#define VOLUME_RING_SKIP_B 0
// Порядок заливки оставшихся 10 светодиодов от "пустого" конца к "полному" —
// идёт по кругу сразу после SKIP_B, через верх, до SKIP_A. Поправь, если после
// прошивки светодиоды зажигаются не в том месте/порядке, что физически ожидаешь
const uint8_t volumeRingOrder[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// Bass/High: та же схема (12 светодиодов, 2 нижних не используются), но шкала
// двусторонняя (-10..+10dB с центром на 0) — заливка идёт от центра в обе стороны.
// Центральная пара (0dB) — зелёная, горит всегда как метка нуля; остальные 4+4
// светодиода — тёплый жёлтый/выбранный цвет, зажигаются в сторону "-" или "+"
#define BASS_RING_PIN 7 // DATA-провод ленты Bass (пин свободен после переноса энкодера)
#define HIGH_RING_PIN 11 // DATA-провод ленты High
#define RING_COUNT 12
// Индексы центральной пары (0dB, зелёные) и по 4 индекса на "-"/"+" сторону — те же
// допущения, что у Volume (skip внизу на 11/0, тогда центр — противоположная пара 5/6).
// Поправь по факту после прошивки, если физически не совпадает
const uint8_t ringCenterPair[2] = {5, 6};
const uint8_t ringNegativeOrder[4] = {4, 3, 2, 1};
const uint8_t ringPositiveOrder[4] = {7, 8, 9, 10};

// Палитра цветов для активной (не зелёной) части всех трёх колец — выбирается в
// пункте меню "Color". Зелёная пара 0dB у Bass/High цвет не меняет — только жёлтая/
// выбранная часть заливки
struct RingColor {
  const char* name;
  uint8_t r, g, b;
};
#define RING_COLOR_COUNT 21
const RingColor ringColorPalette[RING_COLOR_COUNT] = {
  {"White", 255, 255, 255},
  {"Orange", 255, 69, 0},
  {"Yellow", 255, 255, 0},
  {"Blue", 0, 0, 255},
  {"Pink", 255, 20, 147},
  {"Turquoise", 0, 220, 180},
  {"Red", 255, 0, 0},
  {"Green", 0, 255, 0},
  {"Purple", 160, 32, 240},
  {"Cyan", 0, 255, 255},
  {"Lime", 170, 255, 0},
  {"Magenta", 255, 0, 255},
  {"Gold", 255, 200, 0},
  {"Indigo", 75, 0, 200},
  {"Coral", 255, 90, 60},
  {"Violet", 180, 0, 255},
  {"Amber", 255, 160, 0},
  {"Mint", 0, 255, 140},
  {"Salmon", 255, 110, 90},
  {"SkyBlue", 0, 170, 255},
  {"Lavender", 190, 150, 255},
};
#define RING_COLOR_DEFAULT 2 // Yellow — ближе всего к прежнему тёплому жёлтому

// Анимация возврата зелёной пары 0dB в ноль: 3 плавных "вздоха" тусклый->яркий->тусклый,
// затем держим ровно ярко
#define ZERO_BLINK_PULSE_MS 500 // Длительность одного "вздоха"
#define ZERO_BLINK_COUNT 3
#define ZERO_BLINK_TOTAL_MS (ZERO_BLINK_PULSE_MS * ZERO_BLINK_COUNT)
#define ZERO_BLINK_MIN_BRIGHTNESS 15

// ============================================================================
// Разовая анимация на кольцах Bass/High при переключении Bypass (кнопкой, энкодером
// или пультом). Bypass выключается: кольцо заливается целиком, шаг за шагом в обе
// стороны от зелёного центра, держим залитым, потом гасим. Bypass включается: только
// центральная зелёная пара несколько раз "вальяжно" мигает красным
// ============================================================================
#define BYPASS_FILL_STEP_MS 200 // Задержка между шагами заливки в каждую сторону
#define BYPASS_FILL_STEPS 4 // По 4 светодиода на каждую сторону (ringNegativeOrder/ringPositiveOrder)
#define BYPASS_FILL_HOLD_MS 400 // Сколько держим полностью залитое кольцо перед возвратом
#define BYPASS_FILL_TOTAL_MS (BYPASS_FILL_STEP_MS * BYPASS_FILL_STEPS + BYPASS_FILL_HOLD_MS)
#define BYPASS_BLINK_PULSE_MS 500 // Длительность одного "вздоха"
#define BYPASS_BLINK_COUNT 3
#define BYPASS_BLINK_TOTAL_MS (BYPASS_BLINK_PULSE_MS * BYPASS_BLINK_COUNT)
#define BYPASS_BLINK_MIN_BRIGHTNESS 15

// ============================================================================
// Калибровка потенциометров Bass/High/Volume: правь эти точки вручную по данным
// из Serial Monitor ("... pot raw: X -> Y..."). raw — сырое значение analogRead (0-1023),
// value — соответствующее физическое значение (дБ для Bass/High, % для Volume).
// В каждой паре массивы должны быть одинаковой длины, raw — строго по возрастанию.
// *_POT_ZERO_SNAP_RAW — запас в raw-отсчётах вокруг калибровочной точки 0dB: если сырое
// значение попадает в эту зону, показывается ровно 0dB, а не близкое промежуточное значение —
// так реальный 0 можно физически "поймать", не подбирая точное положение ручки
// ============================================================================
#define BASS_POT_ZERO_SNAP_RAW 5
#define HIGH_POT_ZERO_SNAP_RAW 5
// *_POT_MIN_SNAP_RAW/*_POT_MAX_SNAP_RAW — то же самое, но у краёв шкалы (-10dB/+10dB): если
// ручка физически не докручивается точно до калибровочного raw на краю, но близко к нему —
// всё равно показываем ровно -10/+10, а не -9/8 и т.п. У минимума запас маленький, т.к. рядом
// калибровочная точка -8dB — большой запас "съел" бы её (сделал бы -8dB недостижимым)
#define BASS_POT_MIN_SNAP_RAW 3
#define BASS_POT_MAX_SNAP_RAW 10
#define HIGH_POT_MIN_SNAP_RAW 2
#define HIGH_POT_MAX_SNAP_RAW 10
#define VOLUME_POT_MIN_SNAP_RAW 1 // 0-25% сжаты в 5 raw-отсчётов — большой запас "съедал" бы точку 25%
#define VOLUME_POT_MAX_SNAP_RAW 10

const int bassPotCalRaw[] = {7, 12, 21, 109, 159, 197, 233, 588, 1010};
const int bassPotCalValue[] = {-10, -8, -5, -2, -1, 0, 1, 5, 10};
const int bassPotCalPoints = sizeof(bassPotCalRaw) / sizeof(bassPotCalRaw[0]);

const int highPotCalRaw[] = {8, 11, 21, 202, 254, 660, 1010};
const int highPotCalValue[] = {-10, -8, -5, 0, 1, 5, 10};
const int highPotCalPoints = sizeof(highPotCalRaw) / sizeof(highPotCalRaw[0]);

const int volumePotCalRaw[] = {0, 5, 178, 569, 1020};
const int volumePotCalPercent[] = {0, 25, 50, 75, 100};
const int volumePotCalPoints = sizeof(volumePotCalRaw) / sizeof(volumePotCalRaw[0]);

// ============================================================================
// Экраны настроек — позиция/шрифт надписи названия пункта меню и счётчика значения.
// Правь только эти строки, чтобы подвинуть/увеличить/уменьшить текст на экране.
// Другие варианты размера шрифта (по возрастанию): u8g2_font_ncenB08_tr, _ncenB10_tr,
// _ncenB12_tr, _ncenB14_tr, _ncenB18_tr
// ============================================================================

// --- Экран настроек Bass/High ---
#define LABEL_FONT u8g2_font_ncenB08_tr
#define LABEL_X 45
#define LABEL_Y 25
#define VALUE_FONT u8g2_font_ncenB10_tr
#define VALUE_X 85
#define VALUE_Y 25

// --- Экран настроек Volume ---
#define VOLUME_LABEL_FONT u8g2_font_ncenB08_tr
#define VOLUME_LABEL_X 45
#define VOLUME_LABEL_Y 25
#define VOLUME_VALUE_FONT u8g2_font_ncenB10_tr
#define VOLUME_VALUE_X 95
#define VOLUME_VALUE_Y 25

// --- Экран настроек Dimmer ---
#define DIMMER_LABEL_FONT u8g2_font_ncenB14_tr
#define DIMMER_LABEL_X 25
#define DIMMER_LABEL_Y 20
#define DIMMER_VALUE_FONT u8g2_font_ncenB10_tr
#define DIMMER_VALUE_X 50
#define DIMMER_VALUE_Y 45

// --- Экран настроек Color ---
#define COLOR_LABEL_FONT u8g2_font_ncenB14_tr
#define COLOR_LABEL_X 15
#define COLOR_LABEL_Y 20
#define COLOR_VALUE_FONT u8g2_font_ncenB10_tr
#define COLOR_VALUE_X 15
#define COLOR_VALUE_Y 45

// --- Экран настроек Source ---
#define SOURCE_LABEL_FONT u8g2_font_ncenB14_tr
#define SOURCE_LABEL_X 15
#define SOURCE_LABEL_Y 20
#define SOURCE_VALUE_FONT u8g2_font_ncenB10_tr
#define SOURCE_VALUE_X 15
#define SOURCE_VALUE_Y 45

// --- Точки-индикаторы текущего пункта меню на экране drawMenu() ---
#define MENU_DOTS_PER_ROW 4 // 8 пунктов в один ряд не влезает на 128px экран, поэтому 2 ряда по 4
#define MENU_DOT_RADIUS 3
#define MENU_DOT_SPACING_X 20 // Расстояние между точками по горизонтали
#define MENU_DOT_ROW_SPACING_Y 8 // Расстояние между рядами точек
#define MENU_DOT_ROW1_Y 50 // Y верхнего ряда точек (нижний ряд — MENU_DOT_ROW1_Y + MENU_DOT_ROW_SPACING_Y)
#define MENU_DOT_CENTER_WIDTH 148 // Условная ширина для центрирования рядов точек
#define MENU_DOT_ROW1_X_OFFSET 0 // Доп. сдвиг по X верхнего ряда точек относительно центра
#define MENU_DOT_ROW2_X_OFFSET 0 // Доп. сдвиг по X нижнего ряда точек относительно центра

// --- Мелкие текстовые индикаторы состояния (mute/bypass) на экране drawMenu() ---
#define STATUS_INDICATOR_FONT u8g2_font_ncenB08_tr
#define MUTE_INDICATOR_X 100 // Правый верхний угол
#define MUTE_INDICATOR_Y 10
#define BYPASS_INDICATOR_X 2 // Левый верхний угол
#define BYPASS_INDICATOR_Y 10
