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
// Минимальный зазор между двумя ПОЛНЫМИ перерисовками drawMenu() (display_logic.cpp),
// независимо от того, что их вызвало и в каком порядке — см. комментарий у MENU_ICON_*
// ниже: две передачи слишком близко по времени друг к другу визуально "теряли" одну из них
#define DISPLAY_REDRAW_MIN_GAP_MS 50
// Отдельный, гораздо более короткий зазор именно после ЧАСТИЧНОГО обновления иконки
// (updateDisplayArea() в animate*IconPartial()) — самой передаче нужно лишь несколько мс
// (она в разы меньше полного буфера), поэтому полный DISPLAY_REDRAW_MIN_GAP_MS (50мс) для
// этого случая избыточен и заметно тормозит навигацию (задержка почти на каждое нажатие,
// т.к. иконка тикает каждые MENU_ICON_FRAME_DELAY_MS). Этого зазора достаточно, чтобы
// drawMenu() не стартовал полную передачу прямо во время хвоста частичной
#define DISPLAY_PARTIAL_REDRAW_MIN_GAP_MS 20

// --- Анимация включения/выключения (boot_animation.cpp) — играет при физическом
// включении питания (setup()) и при программном Power On/Off (on_off_logic.cpp) ---
#define BOOT_ANIMATION_FRAME_DELAY_MS 42 // Задержка между кадрами
#define BOOT_ANIMATION_DURATION_MS 4000 // Общая длительность — кадры зацикливаются, пока не наберётся это время
#define BOOT_ANIMATION_X 40 // Левый верхний угол кадра (кадр 48x48, по центру экрана 128x64)
#define BOOT_ANIMATION_Y 8

// --- Полноэкранные анимации при переключении Mute (mute_animation.cpp/unmute_animation.cpp) —
// кадры сгенерированы https://wokwi.com/animator (графика icons8.com), тот же формат и
// приём конвертации, что у boot_animation.cpp. Играют один раз через все кадры подряд
// (не зацикливаясь, в отличие от boot_animation) при каждом переключении Mute ---
#define MUTE_ANIM_FRAME_DELAY_MS 42 // Задержка между кадрами
#define MUTE_ANIM_X 40 // Кадр 48x48 (уменьшено с 64x64), по центру экрана 128x64
#define MUTE_ANIM_Y 8
#define UNMUTE_ANIM_FRAME_DELAY_MS 42
#define UNMUTE_ANIM_X 40 // Кадр 48x48 (уменьшено с 64x64), по центру экрана 128x64
#define UNMUTE_ANIM_Y 8

// --- Анимации-индикаторы на экране главного меню (drawMenu()) — крутящаяся иконка в крайней
// левой части экрана, своя для каждого пункта меню (bass_volume_high_animation.cpp — общая для
// Bass/High/Volume, vu_meter_animation.cpp, bypass_animation.cpp, dimmer_animation.cpp,
// color_animation.cpp, source_animation.cpp). В отличие от Mute-анимаций — не одноразовые, а
// крутятся непрерывно, пока пользователь сидит на соответствующем пункте карусели меню.
// Все используют одну и ту же позицию на экране (показывается только одна иконка за раз,
// под текущий currentMenuItem) — отсюда общие, не по-отдельности на каждую анимацию, константы:
#define MENU_ICON_X 0 // Кадр 32x32, крайняя левая часть экрана 128x64
#define MENU_ICON_Y 10 // По центру вертикали ((64-32)/2)
#define MENU_ICON_FRAME_DELAY_MS 42 // Задержка между кадрами внутри анимации
//
// Позиция иконки EQ — отдельная от общих MENU_ICON_X/Y выше (см. drawEqAnim()/
// animateEqIconPartial() в eq_animation.cpp и их вызовы в display_logic.cpp/main.cpp).
// По умолчанию совпадает с общей позицией — подвинь эти два числа, если кадры анимации EQ
// нарисованы не по центру своего кадра 32x32 и её нужно сдвинуть относительно остальных
#define EQ_ICON_X 5 // Совпадает с MENU_ICON_X по умолчанию — поменяй это число, чтобы сдвинуть иконку EQ по горизонтали
#define EQ_ICON_Y 10 // Совпадает с MENU_ICON_Y по умолчанию — поменяй это число, чтобы сдвинуть иконку EQ по вертикали
// main.cpp перерисовывает только тайлы под самой иконкой через u8g2.updateDisplayArea()
// (не весь drawMenu()) — полная SPI-передача экрана (1024 байта) слишком часто конфликтовала
// с перерисовкой от навигации (см. подробный разбор и историю фикса в CLAUDE.md, раздел
// "Анимации-индикаторы пунктов меню"). DISPLAY_REDRAW_MIN_GAP_MS (выше) — дополнительная
// защита на случай, если что-то всё же вызовет полный drawMenu() слишком близко по времени ---

// --- Энкодер и его кнопка ---
#define ENCODER_A_PIN 18 // INT5 — обязательно пин с аппаратным прерыванием (алгоритм Мазурова)
#define ENCODER_B_PIN 20 // INT3 — обязательно пин с аппаратным прерыванием (алгоритм Мазурова)
#define BUTTON_PIN 8
#define DOUBLE_CLICK_THRESHOLD_MS 300 // Порог для обнаружения двойного нажатия кнопки энкодера
// Внутри пункта меню Dimmer у кнопки энкодера особое поведение (см. checkEncoderButton() в
// encoder.cpp): короткий клик переключает между "выбором строки LED/Display" и "редактированием
// её значения" (dimmerRowLocked в main.h), а не выходит из настроек, как везде — поэтому выход
// в карусель меню там требует зажать кнопку на этот срок
#define DIMMER_EXIT_HOLD_MS 2000

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
// Раз в сколько мс перерисовываются кольца Bass/High/Volume в loop() (main.cpp) — 200мс
// (5 обновлений/сек) хватало под скорость самого мотора, но при резком ручном вращении
// (без мотора, руками) физическое положение убегает быстрее — заметна задержка зажигания
// светодиодов. Понижать сильно ниже рискованно: за один тик здесь усредняются все три
// потенциометра (до ~15-20мс) и идёт SPI/one-wire на три кольца — это отъедает время у
// тайминга ИК-приёма и мотора
#define RING_UPDATE_INTERVAL_MS 100
#define SLIDER_MOTOR_IDLE_TIMEOUT 260 // мс без новых команд от энкодера/пульта — мотор мгновенно останавливается.
// Пульт (RC5) при зажатой кнопке шлёт повторные кадры раз в ~114мс. Таймаут должен
// переживать не только обычный джиттер, но и изредка целиком потерянный кадр (наводка от
// самих моторов, задержка ИК-приёма во время NeoPixel.show() и т.п.) — иначе один пропущенный
// кадр (гэп ~228мс) всё равно даёт заметный завтык. Было 120мс, потом 200мс — оба случая
// укладывались в 1 период без запаса на пропуск.
// Допуск в raw-отсчётах для автовозврата Bass/High к 0dB после Bypass (см. motor_driver_logic.cpp).
// Специально уже, чем BASS_POT_ZERO_SNAP_RAW/HIGH_POT_ZERO_SNAP_RAW — снап нужен только для
// чистого отображения "0dB" на экране, а моторное автовозвращение должно доводить ручку до
// самой калибровочной точки, а не до края зоны снапа, иначе ручка не докручивает пару градусов
#define MOTOR_RECENTER_RAW_EPSILON 2
#define VOLUME_SEEK_EPSILON_PERCENT 2 // Аналог MOTOR_RECENTER_RAW_EPSILON для Volume, но в % (не raw) — см. requestVolumeSeek()

// --- Выключение/включение питания: моторы и долговременная память положения Bass/High ---
#define MOTOR_ZERO_TIMEOUT_MS 6000 // Максимум ждём при выключении, пока Bass/High/Volume доедут до нуля —
// предохранитель на случай упора/заклинившего потенциометра, чтобы не блокировать выключение навечно.
// Было 3000 — хватало только на "типичный" случай (Volume около VOLUME_POWERON_TARGET_PERCENT=30%
// или ниже). При включённой на полную громкости (100%) моторы Bass/High/Volume едут к нулю все
// вместе на одном общем таймауте (seekBassHighVolumeToZeroBlocking()), а полный ход Volume
// 100%->0% на фиксированной скорости SLIDER_MOTOR_SPEED физически занимает заметно больше
// времени, чем короткий ход от 30% — таймаут истекал раньше, чем ручка Volume реально доезжала
// до нуля, и stopAllMotors() глушил мотор на середине пути (~50%). Если после прошивки ручка
// всё ещё не успевает доехать от 100% при выключении — увеличивай дальше по тому же принципу
#define VOLUME_POWERON_TARGET_PERCENT 30 // Volume всегда стартует с этого % при включении питания —
// вне зависимости от Bypass и от того, что было в EEPROM (там хранится только Bass/High, см. on_off_logic.cpp)
// Два независимых слота EEPROM — разные адреса, чтобы не пересекались (см. on_off_logic.cpp):
#define EEPROM_BYPASS_STATE_ADDR 0 // Состояние Bypass — сохраняется при КАЖДОМ выключении, восстанавливается при включении
#define EEPROM_BASS_HIGH_POSITION_ADDR 8 // Положение Bass/High — только если Bypass был выключен
#define EEPROM_DIMMER_COLOR_ADDR 16 // Яркость колец (LED)/дисплея (Display) и цвет колец (Color) —
// в отличие от двух слотов выше, пишется сразу при КАЖДОМ изменении значения, не только при
// выключении питания (см. saveSettings()/saveDimmerColorSettings() в on_off_logic.cpp)
#define EEPROM_SOURCE_STATE_ADDR 24 // Выбранный источник (Source) — как Bypass/Bass-High выше,
// пишется только при выключении питания (см. saveSourceStateOnShutdown() в on_off_logic.cpp)
#define EEPROM_VU_METER_STATE_ADDR 32 // Состояние VU Meter — тот же паттерн, что у Source/Bypass выше:
// пишется только при выключении питания (см. saveVuMeterStateOnShutdown() в on_off_logic.cpp)

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
// только одно из четырёх одновременно
#define SOURCE_RELAY_1_PIN 30
#define SOURCE_RELAY_2_PIN 44
#define SOURCE_RELAY_3_PIN 46
#define SOURCE_RELAY_4_PIN 47
#define SOURCE_COUNT 4
// Названия источников для экрана (см. drawSourceScreen() в display_logic.cpp) — порядок
// соответствует порядку переключения settings[7]/applySourceSelection() (relay.cpp)
const char* const sourceNames[SOURCE_COUNT] = {"AUX", "CD", "DAT", "STREAMER"};
// Сколько держится полноэкранный показ источника после нажатия Set на пульте (см.
// beginSourceOverlay()/updateSourceOverlay() в main.cpp), прежде чем само вернуться туда,
// где был экран до нажатия
#define SOURCE_OVERLAY_DURATION_MS 1200

// --- ИК-приёмник и коды команд пульта. Читаются через IRremote. После
// переобучения пульт шлёт все кнопки одним протоколом RC5, Address=0x18 —
// сюда записан только Command, коллизий между значениями нет
// (полная таблица — IR_REMOTE_CODES.md) ---
#define IR_PIN 19
#define IR_PROTOCOL RC5 // Проверяется в remote_control.cpp, чтобы наводка (например от моторов),
// случайно задекодированная в другой протокол, не спутывалась с реальной кнопкой
#define IR_ADDRESS 0x18
#define IR_RIGHT 0x4 // RC5, Address=0x18
#define IR_LEFT 0x6 // RC5, Address=0x18
#define IR_ENTER 0x0 // RC5, Address=0x18
#define IR_MUTE 0x2 // RC5, Address=0x18
#define IR_POWER 0x1 // RC5, Address=0x18
#define IR_UP 0x1D // RC5, Address=0x18 — двигает мотор Bass/High/Volume, пока держишь
#define IR_DOWN 0x8 // RC5, Address=0x18 — двигает мотор Bass/High/Volume, пока держишь
#define IR_SET 0x21 // RC5, Address=0x18 — прямой шорткат переключения Source (см. IR_REMOTE_CODES.md)

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

// Volume: 12 светодиодов, 1 нижний не используется, шкала 0-100% заливается по кругу.
// Раньше не использовались 2 (индексы 0 и 11) — максимумом был 10-й светодиод по счёту;
// теперь верхний (11) тоже участвует, максимум — 11-й
#define VOLUME_RING_PIN 6 // DATA-провод ленты
#define VOLUME_RING_COUNT 12
#define VOLUME_RING_DEFAULT_DIMMER 20 // % от максимума (255) — регулируется в меню "Dimmer"
// Индекс (0-11) "нижнего" светодиода, который всегда должен быть выключен.
// Подбери по факту после прошивки — зависит от того, с какого физического светодиода
// у тебя начинается адресация ленты (индекс 0)
#define VOLUME_RING_SKIP_B 0
// Порядок заливки оставшихся 11 светодиодов от "пустого" конца к "полному" —
// идёт по кругу сразу после SKIP_B, через верх, до конца. Поправь, если после
// прошивки светодиоды зажигаются не в том месте/порядке, что физически ожидаешь
const uint8_t volumeRingOrder[11] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

// Гистерезис ТОЛЬКО для перехода "0 <-> 1-й светодиод" в updateVolumeRing() (neopixel.cpp) —
// самая шумная граница шкалы, т.к. 0-25% упакованы всего в 5 raw-отсчётов потенциометра
// (наводка от моторов Bass/High, NeoPixel и т.п. перекидывает через неё на 1-2 raw). Раньше
// пробовали снап прямо в калибровке (readVolumePotPercent) — но там окно шириной в пару raw
// съедало почти весь промежуток 0-25% целиком и получался скачок сразу к 25% (3 светодиода
// разом) вместо плавного нарастания. Здесь же сам процент/raw не трогается (EEPROM/seek видят
// настоящее значение) — фиксируется только решение "показывать 1-й светодиод или нет"
#define VOLUME_RING_FIRST_LED_ENTER_PERCENT 9 // Нужно набрать хотя бы столько %, чтобы 1-й светодиод зажёгся
#define VOLUME_RING_FIRST_LED_EXIT_PERCENT 5 // И упасть ниже этого, чтобы он погас — шире входного порога

// Как только Volume превышает VOLUME_MID_PERCENT (строго больше — не при значении
// ровно 50 и не ниже), 6-й светодиод (индекс 5 в volumeRingOrder — тот, что горел ещё
// ДО 50%) перестаёт гореть ровно и начинает непрерывно "дышать" тусклый->яркий->тусклый,
// как визуальная отметка "громкость выше середины шкалы"
#define VOLUME_MID_PERCENT 50
#define VOLUME_MID_BREATH_LED_INDEX 5 // Индекс в volumeRingOrder (0-based) — 6-й физический светодиод
#define VOLUME_MID_BREATH_PULSE_MS 1500 // Длительность одного "вздоха" — медленнее, чем у зелёной пары (ZERO_BLINK_PULSE_MS)
#define VOLUME_MID_BREATH_MIN_BRIGHTNESS 15

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
#define RING_COLOR_COUNT 41
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
  // +20 — RING_COLOR_DEFAULT (Yellow, индекс 2) не трогали, так что стартовый цвет не съехал
  {"Crimson", 220, 20, 60},
  {"Chartreuse", 127, 255, 0},
  {"Teal", 0, 128, 128},
  {"Maroon", 176, 0, 32},
  {"Navy", 0, 60, 160},
  {"Olive", 150, 150, 0},
  {"Orchid", 200, 90, 190},
  {"Peach", 255, 170, 130},
  {"Rose", 255, 0, 90},
  {"Emerald", 0, 200, 90},
  {"Sapphire", 15, 90, 200},
  {"Ruby", 224, 17, 95},
  {"Bronze", 200, 130, 60},
  {"Steel", 100, 140, 170},
  {"Plum", 160, 70, 140},
  {"Mustard", 210, 180, 20},
  {"Periwinkle", 140, 140, 255},
  {"Fuchsia", 255, 0, 200},
  {"Aquamarine", 60, 230, 180},
  {"Tangerine", 255, 120, 20},
};
#define RING_COLOR_DEFAULT 2 // Yellow — ближе всего к прежнему тёплому жёлтому

// Анимация возврата зелёной пары 0dB в ноль: 3 плавных "вздоха" тусклый->яркий->тусклый,
// затем держим ровно ярко
#define ZERO_BLINK_PULSE_MS 500 // Длительность одного "вздоха"
#define ZERO_BLINK_COUNT 3
#define ZERO_BLINK_TOTAL_MS (ZERO_BLINK_PULSE_MS * ZERO_BLINK_COUNT)
#define ZERO_BLINK_MIN_BRIGHTNESS 15

// ============================================================================
// Анимация на кольцах Bass/High, привязанная к состоянию Bypass (кнопка, энкодер
// или пульт — источник переключения не важен). Bypass включён: центральная пара
// несколько раз "вальяжно" дышит красным, затем держится ровно ярко-красной —
// и остаётся такой всё время, пока Bypass не выключат (это не разовая анимация,
// а постоянное состояние, просто с "вступлением"). Bypass выключается: разовая
// анимация заливки кольца целиком, шаг за шагом в обе стороны от зелёного центра,
// держим залитым, потом гасим и возвращаемся к обычной отрисовке уровня дБ
// (зелёный центр на 0dB)
// ============================================================================
#define BYPASS_FILL_STEP_MS 200 // Задержка между шагами заливки в каждую сторону
#define BYPASS_FILL_STEPS 4 // По 4 светодиода на каждую сторону (ringNegativeOrder/ringPositiveOrder)
#define BYPASS_FILL_HOLD_MS 400 // Сколько держим полностью залитое кольцо перед возвратом
#define BYPASS_FILL_TOTAL_MS (BYPASS_FILL_STEP_MS * BYPASS_FILL_STEPS + BYPASS_FILL_HOLD_MS)
#define BYPASS_BLINK_PULSE_MS 500 // Длительность одного "вздоха"
#define BYPASS_BLINK_COUNT 3 // Сколько вздохов делаем перед тем, как держаться ровно ярко
#define BYPASS_BLINK_INTRO_MS (BYPASS_BLINK_PULSE_MS * BYPASS_BLINK_COUNT)
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
// Расширено с 5: этот же допуск используется и для остановки мотора при автовозврате к нулю
// (см. recenterEpsilonFor() в motor_driver_logic.cpp) — за краткий люфт/отскок механики между
// командой "стоп" и следующим реальным замером потенциометра значение могло уйти чуть дальше
// прежнего узкого окна, и экран на секунду показывал "не совсем ноль" (ни зелёный, ни жёлтый
// толком) именно после автовозврата (Bypass, включение/выключение питания)
#define BASS_POT_ZERO_SNAP_RAW 8
#define HIGH_POT_ZERO_SNAP_RAW 8
// *_POT_ZERO_EXIT_SNAP_RAW — гистерезис: шире, чем *_POT_ZERO_SNAP_RAW. Как только
// значение один раз распозналось как 0dB, выйти из зоны 0dB требует отклонения больше
// этого запаса, а не обычного (входного). Без этого дребезг контакта потенциометра
// ровно в точке 0dB (у некоторых экземпляров именно там контакт "прыгает") заставляет
// показание/кольцо мигать между 0dB и ±1dB даже когда ручка физически не движется
#define BASS_POT_ZERO_EXIT_SNAP_RAW 13
#define HIGH_POT_ZERO_EXIT_SNAP_RAW 13
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

// Мёртвая зона у нижнего края хода Volume (0-10%) — физически последний участок дорожки
// почти не меняет сопротивление, поэтому обычный seek по проценту (updateVolumeSeek()) может
// посчитать, что уже доехал, хотя ручка физически ещё не у истинного нуля — раз ADC внутри
// этой зоны не меняется, узнать по фидбэку, сколько хода осталось, невозможно в принципе.
// Применяется ТОЛЬКО при выключении (seekBassHighVolumeToZeroBlocking() в
// motor_driver_logic.cpp) — не во время обычного ручного управления, чтобы не гонять мотор
// в механический упор каждый раз, когда пользователь просто оставил громкость тихой
#define VOLUME_ZERO_DEADZONE_PERCENT 10
// Слепой (без сверки с потенциометром) доворот мотора Volume после обычного seek — на случай,
// если тот отчитался "доехал" ещё внутри мёртвой зоны. Обычной точности seek по raw-цели тут
// мало (см. выше) — нужен доворот на большее время, с запасом заметно больше, чем ушло бы на
// 10° поворота вала, чтобы гарантированно дойти до физического упора, а не до края мёртвой зоны
#define VOLUME_ZERO_BLIND_PUSH_MS 1000

// -8dB нет отдельной точкой ни у Bass, ни у High — дорожка потенциометра логарифмическая,
// и в самом низу шкалы (примерно от -8dB и до полного физического упора к -10dB) её
// сопротивление меняется настолько мало, что ADC (даже усреднённый по 64 сэмплам) отдаёт
// одно и то же raw (~1) на всём этом участке — измерено на месте (см. пользовательский
// замер "-8dB = 1 raw"), это не шум и не баг: в этой зоне физически нет данных, чтобы
// различить -8dB и -10dB. Поэтому нижняя калибровочная точка (-10dB) просто сдвинута на
// этот измеренный "пол" — вся мёртвая зона честно схлопывается в -10dB, а не в
// придуманную непроверяемую -8dB
const int bassPotCalRaw[] = {1, 12, 81, 159, 191, 233, 588, 1010};
const int bassPotCalValue[] = {-10, -5, -2, -1, 0, 1, 5, 10};
const int bassPotCalPoints = sizeof(bassPotCalRaw) / sizeof(bassPotCalRaw[0]);

const int highPotCalRaw[] = {1, 10, 88, 202, 254, 660, 1010};
const int highPotCalValue[] = {-10, -5, -2, 0, 1, 5, 10};
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
#define LABEL_Y 30
#define VALUE_FONT u8g2_font_ncenB10_tr
#define VALUE_X 85
#define VALUE_Y 30

// --- Экран настроек Volume ---
#define VOLUME_LABEL_FONT u8g2_font_ncenB08_tr
#define VOLUME_LABEL_X 43
#define VOLUME_LABEL_Y 30
#define VOLUME_VALUE_FONT u8g2_font_ncenB10_tr
#define VOLUME_VALUE_X 90
#define VOLUME_VALUE_Y 30

// --- Общие элементы экрана Bass/High/Volume (drawArrowIndicator() в display_logic.cpp) —
// кружок с стрелкой-указателем реального положения ручки, треугольники-индикаторы
// направления вращения (показываются на пару кадров при вводе с энкодера/пульта) и
// горизонтальный прогресс-бар снизу. Общие для всех трёх пунктов — своей позиции у
// каждого нет, в отличие от LABEL_*/VALUE_* выше ---
#define ARROW_CIRCLE_X 20 // Центр кружка — левая верхняя часть экрана
#define ARROW_CIRCLE_Y 20
#define ARROW_CIRCLE_RADIUS 14
#define ARROW_NEEDLE_LENGTH 18 // Длиннее радиуса — стрелка выходит за пределы кружка

// Треугольники-стрелочки вправо/влево (showArrowRight/showArrowLeft) — по 3 точки на
// каждый, но Y одни и те же для обоих (только своя высота треугольника), а X считается от
// "ближнего" к центру экрана края (_NEAR, на уровне TOP/BOTTOM) к "дальнему" острию (_FAR,
// на уровне MID)
#define ARROW_INDICATOR_Y_TOP 30
#define ARROW_INDICATOR_Y_MID 35
#define ARROW_INDICATOR_Y_BOTTOM 40
#define ARROW_RIGHT_X_NEAR 110
#define ARROW_RIGHT_X_FAR 120
#define ARROW_LEFT_X_NEAR 40
#define ARROW_LEFT_X_FAR 30

// Горизонтальная линия-шкала и бегущий по ней прогресс-бар (положение потенциометра)
#define PROGRESS_LINE_X 20
#define PROGRESS_LINE_Y 45
#define PROGRESS_LINE_WIDTH 88
#define PROGRESS_BAR_X_MIN 20 // Левый край хода бегунка (соответствует минимуму шкалы)
#define PROGRESS_BAR_X_MAX 108 // Правый край хода бегунка (соответствует максимуму шкалы)
#define PROGRESS_BAR_Y 47
#define PROGRESS_BAR_WIDTH 4
#define PROGRESS_BAR_HEIGHT 12
// Короткие засечки над шкалой на минимуме/центре/максимуме — читаемость (особенно
// центра, 0dB для Bass/High) без разметки цифрами
#define PROGRESS_TICK_HEIGHT 4

// --- Экран настроек VU Meter/Bypass ("pill"-переключатель, drawToggleSwitch() в
// display_logic.cpp) — скруглённая дорожка (drawRFrame/drawRBox, радиус = половина высоты)
// с круглым бегунком (drawDisc), как на телефоне: OFF — дорожка пустая, бегунок слева;
// ON — дорожка залита целиком, бегунок — "вырезанный" кружок с контуром справа (иначе на
// монохромном экране бегунок слился бы с залитой дорожкой) ---
#define TOGGLE_FONT u8g2_font_ncenB12_tr // Название пункта меню сверху
#define TOGGLE_LABEL_X 20
#define TOGGLE_LABEL_Y 30
#define TOGGLE_SWITCH_X 30 // Левый верхний угол дорожки переключателя
#define TOGGLE_SWITCH_Y 40
#define TOGGLE_SWITCH_WIDTH 50
#define TOGGLE_SWITCH_HEIGHT 20 // Радиус скругления дорожки/бегунка считается как половина этой высоты
#define TOGGLE_KNOB_MARGIN 3 // Отступ бегунка от края дорожки (радиус бегунка = высота/2 - этот отступ)
#define TOGGLE_STATE_FONT u8g2_font_ncenB10_tr // Подпись "On"/"Off" справа от дорожки
#define TOGGLE_STATE_TEXT_X_OFFSET 62 // Смещение от TOGGLE_SWITCH_X — позиция фиксирована, не скачет между сторонами
#define TOGGLE_STATE_TEXT_Y_OFFSET 16 // Смещение от TOGGLE_SWITCH_Y
// Подчёркивание названия пункта меню — см. DIMMER_LABEL_UNDERLINE_Y_OFFSET выше. Общее и
// для VU Meter, и для Bypass (тот же экран, drawToggleSwitch())
#define TOGGLE_LABEL_UNDERLINE_Y_OFFSET 3

// --- Экран настроек Dimmer — 2 строки (яркость колец и яркость дисплея), между которыми
// переключаются Up/Down на пульте (см. IR_UP/IR_DOWN в remote_control.cpp); Left/Right
// регулируют то значение, что выбрано. Активная строка отмечена скруглённой подсветкой
// (инвертированный текст поверх неё) — см. drawDimmerScreen() в display_logic.cpp ---
#define DIMMER_LABEL_FONT u8g2_font_ncenB12_tr
#define DIMMER_LABEL_X 25
#define DIMMER_LABEL_Y 25
#define DIMMER_ROW_FONT u8g2_font_ncenB08_tr
#define DIMMER_ROW_X 10
#define DIMMER_ROW1_Y 44 // Яркость колец (LED)
#define DIMMER_ROW2_Y 62 // Яркость дисплея (Display)
#define DISPLAY_BRIGHTNESS_DEFAULT_PERCENT 80 // Стартовая яркость дисплея — сбрасывается при каждом включении, как и Dimmer колец
// Подсветка активной строки — размер считается по реальной ширине текста строки
// (u8g2.getStrWidth()) + эти отступы, высота — по ascent/descent шрифта DIMMER_ROW_FONT
#define DIMMER_ROW_HIGHLIGHT_PAD_X 3
#define DIMMER_ROW_HIGHLIGHT_PAD_Y 2
#define DIMMER_ROW_HIGHLIGHT_RADIUS 3
// Точка слева от активной строки (доп. индикатор, помимо подсветки) — X считается от
// DIMMER_ROW_X назад (в сторону левого края экрана) на эту величину
#define DIMMER_ROW_DOT_RADIUS 1
#define DIMMER_ROW_DOT_X_OFFSET 7
// Подчёркивание названия пункта меню (menuItems[currentMenuItem]) — линия по ширине
// самого текста (u8g2.getStrWidth()), не во весь экран; отступ вниз от baseline текста
#define DIMMER_LABEL_UNDERLINE_Y_OFFSET 3

// --- Экран настроек Color ---
#define COLOR_LABEL_FONT u8g2_font_ncenB12_tr
#define COLOR_LABEL_X 33
#define COLOR_LABEL_Y 35
#define COLOR_VALUE_FONT u8g2_font_ncenB08_tr
#define COLOR_VALUE_X 15
#define COLOR_VALUE_Y 55
// Квадратик-образец справа от названия цвета — монохромный экран не может показать сам
// цвет, поэтому это дизеринг-заливка по яркости (r*299+g*587+b*114)/1000 выбранного
// цвета: тёмные цвета — редкие точки, светлые — почти сплошная заливка. Узор точек —
// свой, но ФИКСИРОВАННЫЙ у каждого цвета (randomSeed() от самих r/g/b) — см.
// drawBrightnessSwatch() в display_logic.cpp. COLOR_SWATCH_Y поднят так, чтобы нижняя
// грань квадрата (Y + COLOR_SWATCH_SIZE) не срезалась низом экрана (высота 64)
#define COLOR_SWATCH_X 90
#define COLOR_SWATCH_Y 36
#define COLOR_SWATCH_SIZE 24
// Подчёркивание названия пункта меню — см. DIMMER_LABEL_UNDERLINE_Y_OFFSET выше
#define COLOR_LABEL_UNDERLINE_Y_OFFSET 3

// --- Экран настроек Source ---
// --- Экран настроек Source — список ВСЕХ источников (не только текущего), активный
// подсвечен скруглённым прямоугольником (см. drawHighlightedRow() в display_logic.cpp,
// общий приём с Dimmer) — растёт вниз по SOURCE_COUNT строк, а не фиксирован на 3/4 ---
#define SOURCE_LABEL_FONT u8g2_font_ncenB10_tr
#define SOURCE_LABEL_X 55

#define SOURCE_LABEL_Y 30
#define SOURCE_LIST_FONT u8g2_font_ncenB08_tr
#define SOURCE_LIST_X 10
#define SOURCE_LIST_Y_START 25 // Y (baseline) первой строки списка
#define SOURCE_LIST_LINE_HEIGHT 11 // Шаг между строками — при SOURCE_COUNT=4 последняя строка на 58, укладывается в экран высотой 64
#define SOURCE_ROW_HIGHLIGHT_PAD_X 3
#define SOURCE_ROW_HIGHLIGHT_PAD_Y 2
#define SOURCE_ROW_HIGHLIGHT_RADIUS 3
// Точка слева от активного источника — см. DIMMER_ROW_DOT_* выше (тот же приём)
#define SOURCE_ROW_DOT_RADIUS 1
#define SOURCE_ROW_DOT_X_OFFSET 7
// Подчёркивание названия пункта меню — см. DIMMER_LABEL_UNDERLINE_Y_OFFSET выше
#define SOURCE_LABEL_UNDERLINE_Y_OFFSET 3

// --- Пункт меню "EQ" — список готовых пресетов темброблока (Bass/High), список
// прокручивается (EQ_COUNT больше, чем помещается строк на экране за раз, в отличие от
// Source) — см. drawEqScreen() в display_logic.cpp, применение — applyEqPreset() в
// motor_driver_logic.cpp (двигает моторы Bass/High через тот же seek-механизм, что и
// автовозврат, а не реле, как у Source) ---
struct EqPreset {
  const char* name;
  int8_t bassDb;
  int8_t highDb;
};
#define EQ_COUNT 10
const EqPreset eqPresets[EQ_COUNT] = {
  {"Flat",       0,  0},
  {"Jazz",       2,  3},
  {"Rock",       5,  4},
  {"Pop",        2,  2},
  {"Classical", -2,  3},
  {"Vocal",     -3,  4},
  {"Electronic", 7,  5},
  {"Hip-Hop",    9,  1},
  {"Acoustic",   3,  3},
  {"Lounge",     1, -1},
};

// Заголовок "EQ" — справа, по вертикали по центру экрана, крупным шрифтом (в отличие от
// Source/Dimmer, где заголовок мелкий и сверху). X считается динамически от ширины текста
// (u8g2.getStrWidth()) в drawEqScreen() — EQ_LABEL_RIGHT_MARGIN только отступ от правого края
#define EQ_LABEL_FONT u8g2_font_ncenB12_tr
#define EQ_LABEL_Y 38 // Baseline по вертикали ближе к центру экрана (высота 64) для этого шрифта
#define EQ_LABEL_RIGHT_MARGIN 4
#define EQ_LABEL_X_OFFSET -10 // Доп. сдвиг по X (+ вправо, - влево) относительно правого края — подвинь это число
#define EQ_LIST_FONT u8g2_font_ncenB08_tr
#define EQ_LIST_X 4
#define EQ_LIST_Y_START 14 // Y (baseline) первой видимой строки списка — выше, чем раньше: заголовок
// больше не сверху, вся высота экрана теперь под список
#define EQ_LIST_LINE_HEIGHT 12
#define EQ_LIST_VISIBLE_ROWS 5 // Сколько строк видно за раз — EQ_COUNT=10 больше, список прокручивается вокруг выбранного пункта
#define EQ_ROW_HIGHLIGHT_PAD_X 2
#define EQ_ROW_HIGHLIGHT_PAD_Y 2
#define EQ_ROW_HIGHLIGHT_RADIUS 3
#define EQ_ROW_DOT_RADIUS 1
#define EQ_ROW_DOT_X_OFFSET 6
// Подчёркивание названия пункта меню — см. DIMMER_LABEL_UNDERLINE_Y_OFFSET выше
#define EQ_LABEL_UNDERLINE_Y_OFFSET 3

#define EEPROM_EQ_STATE_ADDR 40 // Следующий свободный слот после EEPROM_VU_METER_STATE_ADDR (32)

// --- Название текущего пункта меню (крупный текст по центру) на экране drawMenu() ---
// _tr, а не _tf — пункты меню это обычный ASCII (Bass/High/Volume/...), полный юникод-набор
// _tf не нужен, а весит заметно больше (экономия ~2.6КБ в зоне PROGMEM-шрифтов, см. память
// про лимит 64КБ)
#define MENU_TITLE_FONT u8g2_font_ncenB12_tr
#define MENU_TITLE_Y 32
#define MENU_TITLE_X_OFFSET 10 // Доп. сдвиг по X относительно центра (ширина текста разная у каждого пункта, поэтому X всегда считается заново)

// --- Индикатор текущего пункта меню на экране drawMenu() — одна строка вертикальных
// палочек, по одной на каждый из MENU_ITEM_COUNT пунктов (не точки в 2 ряда, как раньше).
// Активный пункт отмечается не заливкой, а тем, что его палочка смещена вниз относительно
// остальных — при переключении пункта старая палочка возвращается на обычный уровень,
// новая (под currentMenuItem) съезжает вниз ---
#define MENU_BAR_WIDTH 3 // Толщина палочки
#define MENU_BAR_HEIGHT 8 // Высота палочки
#define MENU_BAR_SPACING_X 8 // Расстояние между палочками по горизонтали (MENU_ITEM_COUNT палочек * 8 умещается в 128px)
#define MENU_BAR_X_OFFSET 22 // Доп. сдвиг всей строки палочек по X (+ вправо, - влево) относительно центра экрана.
// Было 30 — с 8 пунктами это ещё укладывалось в 128px, но с добавлением 9-го (EQ) строка
// стала на 8px шире и с прежним сдвигом вылезала за правый край экрана (последняя палочка
// рисовалась частично за пределами видимой области). Если добавишь ещё пункты меню — проверь
// этот отступ заново: правая граница строки = (128-totalWidth)/2 + это значение + totalWidth +
// MENU_BAR_WIDTH, должна остаться меньше 128
#define MENU_BAR_Y 50 // Y верхнего края НЕвыбранных палочек
#define MENU_BAR_SELECTED_Y_OFFSET 4 // На сколько вниз смещена палочка выбранного пункта

// --- Мелкий текстовый индикатор состояния Bypass (на всех экранах, не только drawMenu()) ---
#define STATUS_INDICATOR_FONT u8g2_font_ncenB08_tr
#define BYPASS_INDICATOR_X 80 // Правый верхний угол — раньше здесь была надпись "mute"
#define BYPASS_INDICATOR_Y 10
