#include <U8g2lib.h>
#include <NecDecoder.h>
#include <SPI.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// Объявляем дисплей с использованием SPI интерфейса
U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 12);

// --- NeoPixel-кольцо вокруг ручки Volume (12 светодиодов, 2 нижних не используются) ---
#define VOLUME_RING_PIN 6 // DATA-провод ленты
#define VOLUME_RING_COUNT 12
#define VOLUME_RING_DEFAULT_DIMMER 20 // % от максимума (255) — было в 5 раз ярче, теперь регулируется в меню "Dimmer"

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
uint8_t currentRingColorR = ringColorPalette[RING_COLOR_DEFAULT].r;
uint8_t currentRingColorG = ringColorPalette[RING_COLOR_DEFAULT].g;
uint8_t currentRingColorB = ringColorPalette[RING_COLOR_DEFAULT].b;
// Индексы (0-11) двух "нижних" светодиодов, которые всегда должны быть выключены.
// Подбери по факту после прошивки — зависит от того, с какого физического светодиода
// у тебя начинается адресация ленты (индекс 0)
#define VOLUME_RING_SKIP_A 11
#define VOLUME_RING_SKIP_B 0
// Порядок заливки оставшихся 10 светодиодов от "пустого" конца к "полному" —
// идёт по кругу сразу после SKIP_B, через верх, до SKIP_A. Поправь, если после
// прошивки светодиоды зажигаются не в том месте/порядке, что физически ожидаешь
const uint8_t volumeRingOrder[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

Adafruit_NeoPixel volumeRing(VOLUME_RING_COUNT, VOLUME_RING_PIN, NEO_GRB + NEO_KHZ800);

// --- NeoPixel-кольца вокруг ручек Bass и High: та же схема, что у Volume (12 светодиодов,
// 2 нижних не используются), но шкала двусторонняя (-10..+10dB с центром на 0), поэтому
// заливка идёт от центра в обе стороны, а не от одного края. Центральная пара (0dB) —
// зелёная, горит всегда как метка нуля; остальные 4+4 светодиода — тёплый жёлтый,
// зажигаются в сторону "-" или "+" по модулю значения
#define BASS_RING_PIN 7 // DATA-провод ленты Bass (пин свободен после переноса энкодера)
#define HIGH_RING_PIN 11 // DATA-провод ленты High
#define RING_COUNT 12
// Индексы центральной пары (0dB, зелёные) и по 4 индекса на "-"/"+" сторону — те же
// допущения, что у Volume (skip внизу на 11/0, тогда центр — противоположная пара 5/6).
// Поправь по факту после прошивки, если физически не совпадает
const uint8_t ringCenterPair[2] = {5, 6};
const uint8_t ringNegativeOrder[4] = {4, 3, 2, 1};
const uint8_t ringPositiveOrder[4] = {7, 8, 9, 10};

// При возврате в нулевую зону зелёная пара не просто загорается, а плавно "дышит"
// (тусклый->яркий->тусклый) 3 раза подряд, потом держится ровно ярко. Всё по millis(),
// без delay() — ничего не блокирует; во время анимации потенциометр не перечитывается,
// пересчитывается только яркость уже известного значения — дёшево по процессору
#define ZERO_BLINK_PULSE_MS 500 // Длительность одного "вздоха"
#define ZERO_BLINK_COUNT 3
#define ZERO_BLINK_TOTAL_MS (ZERO_BLINK_PULSE_MS * ZERO_BLINK_COUNT)
#define ZERO_BLINK_MIN_BRIGHTNESS 15

struct DbRingState {
  int lastValue = 999; // Сентинел, точно не 0 — чтобы первый же вызов не считался "уже в нуле"
  unsigned long zeroEnterTime = 0;
};
DbRingState bassRingState;
DbRingState highRingState;

Adafruit_NeoPixel bassRing(RING_COUNT, BASS_RING_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel highRing(RING_COUNT, HIGH_RING_PIN, NEO_GRB + NEO_KHZ800);

// --- Экран настроек Bass/High: правь только эти строки, чтобы подвинуть/увеличить/
// уменьшить надпись названия ("Bass"/"High") и счётчик dB ---
#define LABEL_FONT u8g2_font_ncenB08_tr // Шрифт надписи названия пункта меню
#define LABEL_X 45
#define LABEL_Y 25
#define VALUE_FONT u8g2_font_ncenB10_tr // Шрифт счётчика dB
#define VALUE_X 85
#define VALUE_Y 25

// --- Экран настроек Volume: отдельная настройка надписи "Volume" и счётчика % ---
#define VOLUME_LABEL_FONT u8g2_font_ncenB08_tr
#define VOLUME_LABEL_X 45
#define VOLUME_LABEL_Y 25
#define VOLUME_VALUE_FONT u8g2_font_ncenB10_tr
#define VOLUME_VALUE_X 95
#define VOLUME_VALUE_Y 25

// --- Экран настроек Dimmer: отдельная настройка позиции надписи "Dimmer" и счётчика % ---
#define DIMMER_LABEL_FONT u8g2_font_ncenB14_tr
#define DIMMER_LABEL_X 25
#define DIMMER_LABEL_Y 20
#define DIMMER_VALUE_FONT u8g2_font_ncenB10_tr
#define DIMMER_VALUE_X 50
#define DIMMER_VALUE_Y 45

// --- Экран настроек Color: отдельная настройка позиции надписи "Color" и названия цвета ---
#define COLOR_LABEL_FONT u8g2_font_ncenB14_tr
#define COLOR_LABEL_X 15
#define COLOR_LABEL_Y 20
#define COLOR_VALUE_FONT u8g2_font_ncenB10_tr
#define COLOR_VALUE_X 15
#define COLOR_VALUE_Y 45

// --- Экран настроек Source: отдельная настройка позиции надписи "Source" и номера ---
#define SOURCE_LABEL_FONT u8g2_font_ncenB14_tr
#define SOURCE_LABEL_X 15
#define SOURCE_LABEL_Y 20
#define SOURCE_VALUE_FONT u8g2_font_ncenB10_tr
#define SOURCE_VALUE_X 15
#define SOURCE_VALUE_Y 45
// Другие варианты размера шрифта (по возрастанию): u8g2_font_ncenB08_tr, _ncenB10_tr,
// _ncenB12_tr, _ncenB14_tr, _ncenB18_tr

// --- Калибровка потенциометров Bass/High/Volume: правь эти точки вручную по данным
// из Serial Monitor ("... pot raw: X -> Y..."). raw — сырое значение analogRead (0-1023),
// value — соответствующее физическое значение (дБ для Bass/High, % для Volume).
// В каждой паре массивы должны быть одинаковой длины, raw — строго по возрастанию.
// *_POT_ZERO_SNAP_RAW — запас в raw-отсчётах вокруг калибровочной точки 0dB: если сырое
// значение попадает в эту зону, показывается ровно 0dB, а не близкое промежуточное значение —
// так реальный 0 можно физически "поймать", не подбирая точное положение ручки
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

#define ENCODER_A_PIN 18 // INT5 — обязательно пин с аппаратным прерыванием (алгоритм Мазурова)
#define ENCODER_B_PIN 20 // INT3 — обязательно пин с аппаратным прерыванием (алгоритм Мазурова)
#define BUTTON_PIN 8
#define MOTOR1_IN 40
#define MOTOR1_PWM 3
#define MOTOR2_IN 42
#define MOTOR2_PWM 5
#define MOTOR3_IN1 32
#define MOTOR3_IN2 34
#define MOTOR3_PWM1 2
#define MOTOR3_PWM2 4
#define BASS_POT_PIN A9 // Потенциометр обратной связи положения ручки Bass (+5V/GND по краям, средний вывод сюда)
#define HIGH_POT_PIN A10 // Потенциометр обратной связи положения ручки High (+5V/GND по краям, средний вывод сюда)
#define VOLUME_POT_PIN A8 // Потенциометр обратной связи положения ручки Volume (+5V/GND по краям, средний вывод сюда)
#define SLIDER_MOTOR_SPEED 40 // Фиксированная скорость моторов Bass/High/Volume при вращении энкодера/пульта (диапазон -50..50)
#define SLIDER_MOTOR_IDLE_TIMEOUT 120 // мс без новых команд от энкодера/пульта — мотор мгновенно останавливается
#define RELAY_PIN_STANDBY 22 // Пин для подключения реле Standby
#define RELAY_PIN_VU_METER 24 // Пин для подключения реле VU Meter
#define RELAY_PIN_LED 26 // Пин для подключения реле Led
#define RELAY_PIN_MUTE 28 // Пин для подключения реле Mute
// Реле переключения источника (пункт меню "Source") — взаимоисключающе, работает
// только одно из трёх одновременно
#define SOURCE_RELAY_1_PIN 30
#define SOURCE_RELAY_2_PIN 44
#define SOURCE_RELAY_3_PIN 46
#define SOURCE_COUNT 3
#define IR_PIN 19 // Пин для подключения инфракрасного приемника
#define LED_BASS_PIN 39 // Светодиод Bass
#define LED_HIGH_PIN 41 // Светодиод High
#define LED_VOLUME_PIN 43 // Светодиод Volume

// Коды команд с пульта
#define IR_RIGHT 0x79
#define IR_LEFT 0xF9
#define IR_ENTER 0x7B
#define IR_MUTE 0x38
#define IR_POWER 0xB9 // Новый код для включения и отключения питания

String menuItems[] = {"Bass", "High", "Volume", "VU Meter", "Bypass", "Dimmer", "Color", "Source"};
int currentMenuItem = 0;
int settings[] = {0, 0, 0, 1, 0, VOLUME_RING_DEFAULT_DIMMER, RING_COLOR_DEFAULT, 0}; // VU Meter "включено", Bypass "выключено", Dimmer/Color колец, Source по умолчанию
bool inSettingsMode = false;
bool encoderButtonPressed = false;
unsigned long lastButtonPressTime = 0;
unsigned long doubleClickThreshold = 300; // Порог для обнаружения двойного нажатия в миллисекундах
bool isMuted = false; // Флаг для состояния Mute
bool powerOff = false; // Флаг для состояния питания
unsigned long powerButtonPressStartTime = 0; // Время начала нажатия кнопки питания
bool powerButtonPressing = false; // Флаг для состояния удержания кнопки питания

volatile int encoderValue = 0; // Пишется из encoderISR() по прерыванию — обязательно volatile
unsigned long lastMotorInputTime = 0; // Момент последней команды на мотор Bass/High/Volume (для авто-стопа)

NecDecoder necDecoder; // Создаем объект для декодирования сигналов пульта

volatile bool irReceived = false;
volatile uint8_t irCommand = 0;

// Прототипы функций
void drawMenu();
void drawToggleSwitch(bool state);
void drawArrowIndicator(int settingValue, bool showArrowRight, bool showArrowLeft);
void drawDimmerScreen(int percent);
void drawColorScreen(int colorIndex);
void applyRingColorScheme();
void drawSourceScreen(int sourceIndex);
void applySourceSelection();
void displayMessage(const char* message);
void powerOffScreen();
void powerOnScreen();
void powerOffDevices();
void powerOnDevices();
void checkEncoderButton();
void motorControl(int val, byte pinIN, byte pinPWM);
void motorControl2(int val, byte pinIN1, byte pinIN2, byte pinPWM1, byte pinPWM2);
void stopAllMotors();
void resetCursor();
void saveSettings();
void loadSettings();
void handleRemoteInput();
void blinkLED(int pin);
int potRawToPercent(int raw, const int calRaw[], const int calPercent[], int calPoints, int zeroSnapRaw = 0, int minSnapRaw = 0, int maxSnapRaw = 0);
int readPotPercent(int pin, const int calRaw[], const int calPercent[], int calPoints, int* rawOut = nullptr, int zeroSnapRaw = 0, int minSnapRaw = 0, int maxSnapRaw = 0);
int readBassPotPercent(int* rawOut = nullptr);
int readHighPotPercent(int* rawOut = nullptr);
int readVolumePotPercent(int* rawOut = nullptr);
int readCurrentPotPercent(int* rawOut = nullptr);
int currentPotValueMin();
int currentPotValueMax();
bool currentPotIsDb();
void updateVolumeRing(int percent);
uint8_t zeroBlinkBrightness(unsigned long elapsed);
void renderDbRing(Adafruit_NeoPixel &ring, int dbValue, DbRingState &state);
bool dbRingBlinking(const DbRingState &state);
void applyRingDimmer();

void IR_ISR() {
  necDecoder.tick();
  if (necDecoder.available()) {
    irCommand = necDecoder.readCommand();
    irReceived = true;
  }
}

// Алгоритм Олега Мазурова: чтение квадратурного энкодера через таблицу состояний.
// encoderISR() вызывается по прерыванию на ЛЮБОМ фронте (CHANGE) обоих пинов A и B —
// не зависит от того, чем занят loop(), поэтому не пропускает шаги при быстром вращении.
// Индекс в таблице — 4 бита: 2 старших — предыдущее состояние A,B, 2 младших — текущее.
// Невалидные/дребезговые переходы (в т.ч. "осталось как было") дают 0 и просто
// игнорируются на уровне самой таблицы — отдельный таймер-дебаунс не нужен
static const int8_t ENCODER_STATE_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
volatile uint8_t encoderOldAB = 0;
volatile int8_t encoderRawAccum = 0; // Накопитель валидных переходов между физическими щелчками

// Сколько валидных переходов таблицы соответствуют одному физическому щелчку энкодера.
// Раньше код считал только 1 фронт на щелчок, поэтому "1 щелчок = 1 encoderValue".
// Таблица Мазурова видит ВСЕ переходы — если после прошивки пункты меню перескакивают
// через один (щёлкнул один раз — сдвинулось на два), увеличь это число до 4 и наоборот
#define ENCODER_STEPS_PER_DETENT 2

void encoderISR() {
  encoderOldAB <<= 2;
  encoderOldAB |= (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
  encoderRawAccum += ENCODER_STATE_TABLE[encoderOldAB & 0x0F];
  if (encoderRawAccum >= ENCODER_STEPS_PER_DETENT) {
    encoderValue++;
    encoderRawAccum -= ENCODER_STEPS_PER_DETENT;
  } else if (encoderRawAccum <= -ENCODER_STEPS_PER_DETENT) {
    encoderValue--;
    encoderRawAccum += ENCODER_STEPS_PER_DETENT;
  }
}

int potRawToPercent(int raw, const int calRaw[], const int calPercent[], int calPoints, int zeroSnapRaw, int minSnapRaw, int maxSnapRaw) {
  if (zeroSnapRaw > 0) {
    for (int i = 0; i < calPoints; i++) {
      if (calPercent[i] == 0 && abs(raw - calRaw[i]) <= zeroSnapRaw) {
        return 0;
      }
    }
  }
  if (raw <= calRaw[0] + minSnapRaw) {
    return calPercent[0];
  }
  if (raw >= calRaw[calPoints - 1] - maxSnapRaw) {
    return calPercent[calPoints - 1];
  }
  for (int i = 0; i < calPoints - 1; i++) {
    if (raw >= calRaw[i] && raw <= calRaw[i + 1]) {
      long rawRange = calRaw[i + 1] - calRaw[i];
      long percentRange = calPercent[i + 1] - calPercent[i];
      return calPercent[i] + (raw - calRaw[i]) * percentRange / rawRange;
    }
  }
  return 0;
}

int readPotPercent(int pin, const int calRaw[], const int calPercent[], int calPoints, int* rawOut, int zeroSnapRaw, int minSnapRaw, int maxSnapRaw) {
  const int samples = 64;
  long rawSum = 0;
  for (int i = 0; i < samples; i++) {
    rawSum += analogRead(pin);
  }
  int potRaw = rawSum / samples; // усреднение по 64 сэмплам — гасит дребезг без задержки отклика
  if (rawOut != nullptr) {
    *rawOut = potRaw;
  }
  return potRawToPercent(potRaw, calRaw, calPercent, calPoints, zeroSnapRaw, minSnapRaw, maxSnapRaw);
}

int readBassPotPercent(int* rawOut) {
  return readPotPercent(BASS_POT_PIN, bassPotCalRaw, bassPotCalValue, bassPotCalPoints, rawOut, BASS_POT_ZERO_SNAP_RAW, BASS_POT_MIN_SNAP_RAW, BASS_POT_MAX_SNAP_RAW);
}

int readHighPotPercent(int* rawOut) {
  return readPotPercent(HIGH_POT_PIN, highPotCalRaw, highPotCalValue, highPotCalPoints, rawOut, HIGH_POT_ZERO_SNAP_RAW, HIGH_POT_MIN_SNAP_RAW, HIGH_POT_MAX_SNAP_RAW);
}

int readVolumePotPercent(int* rawOut) {
  return readPotPercent(VOLUME_POT_PIN, volumePotCalRaw, volumePotCalPercent, volumePotCalPoints, rawOut, 0, VOLUME_POT_MIN_SNAP_RAW, VOLUME_POT_MAX_SNAP_RAW);
}

int readCurrentPotPercent(int* rawOut) {
  if (menuItems[currentMenuItem] == "Bass") {
    return readBassPotPercent(rawOut);
  }
  if (menuItems[currentMenuItem] == "High") {
    return readHighPotPercent(rawOut);
  }
  return readVolumePotPercent(rawOut);
}

int currentPotValueMin() {
  if (menuItems[currentMenuItem] == "Bass") {
    return bassPotCalValue[0];
  }
  if (menuItems[currentMenuItem] == "High") {
    return highPotCalValue[0];
  }
  return volumePotCalPercent[0];
}

int currentPotValueMax() {
  if (menuItems[currentMenuItem] == "Bass") {
    return bassPotCalValue[bassPotCalPoints - 1];
  }
  if (menuItems[currentMenuItem] == "High") {
    return highPotCalValue[highPotCalPoints - 1];
  }
  return volumePotCalPercent[volumePotCalPoints - 1];
}

bool currentPotIsDb() {
  return menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High";
}

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

// Применяет settings[] для пункта "Color" (индекс 0-5 в ringColorPalette) как реальный
// цвет активной (не зелёной) части всех трёх колец
void applyRingColorScheme() {
  int idx = constrain(settings[6], 0, RING_COLOR_COUNT - 1);
  currentRingColorR = ringColorPalette[idx].r;
  currentRingColorG = ringColorPalette[idx].g;
  currentRingColorB = ringColorPalette[idx].b;
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

// Применяет settings[] для пункта "Source" (0-2): включает ровно одно из трёх реле,
// остальные два принудительно гасит — переключение источников взаимоисключающее
void applySourceSelection() {
  int idx = constrain(settings[7], 0, SOURCE_COUNT - 1);
  digitalWrite(SOURCE_RELAY_1_PIN, idx == 0 ? HIGH : LOW);
  digitalWrite(SOURCE_RELAY_2_PIN, idx == 1 ? HIGH : LOW);
  digitalWrite(SOURCE_RELAY_3_PIN, idx == 2 ? HIGH : LOW);
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

void drawMenu() {
  u8g2.setFont(u8g2_font_ncenB18_tf);
  u8g2.clearBuffer();

  // Точки меню в 2 ряда по 4 — 8 в один ряд не влезает на 128px экран
  const int dotsPerRow = 4;
  int totalWidth = dotsPerRow * 20;
  int startX = (148 - totalWidth) / 2; // Вычисление стартовой позиции

  for (int i = 0; i < 8; i++) {
    int row = i / dotsPerRow;
    int col = i % dotsPerRow;
    int x = startX + col * 20;
    int y = 50 + row * 8; // Верхний ряд — 50, нижний — 58
    if (i == currentMenuItem) {
      u8g2.drawDisc(x, y, 3, U8G2_DRAW_ALL);
    } else {
      u8g2.drawCircle(x, y, 3);
    }
  }

  u8g2.setCursor((128 - u8g2.getStrWidth(menuItems[currentMenuItem].c_str())) / 2, 32);
  u8g2.print(menuItems[currentMenuItem]);

  if (isMuted) {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.setCursor(100, 10);
    u8g2.print("mute");
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
  } else if (menuItems[currentMenuItem] == "Bass") {
    renderDbRing(bassRing, potValue, bassRingState);
  } else if (menuItems[currentMenuItem] == "High") {
    renderDbRing(highRing, potValue, highRingState);
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

void displayMessage(const char* message) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr); // Установка шрифта меньшего размера
  int strWidth = u8g2.getStrWidth(message);
  u8g2.setCursor((128 - strWidth) / 2, 32); // Центрирование сообщения
  u8g2.print(message);
  u8g2.sendBuffer();
  delay(3000); // Задержка 3 секунды
}

void powerOffScreen() {
  displayMessage("POWER OFF");
}

void powerOnScreen() {
  displayMessage("POWER ON");
}

void powerOffDevices() {
  // Явно гасим светодиоды (LOW = выключено)
  pinMode(LED_BASS_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_VOLUME_PIN, OUTPUT);
  digitalWrite(LED_BASS_PIN, LOW);
  digitalWrite(LED_HIGH_PIN, LOW);
  digitalWrite(LED_VOLUME_PIN, LOW);

  // Явно размыкаем все реле (LOW = выключено при активной по HIGH логике)
  pinMode(RELAY_PIN_STANDBY, OUTPUT);
  pinMode(RELAY_PIN_VU_METER, OUTPUT);
  pinMode(RELAY_PIN_LED, OUTPUT);
  pinMode(RELAY_PIN_MUTE, OUTPUT);
  digitalWrite(RELAY_PIN_STANDBY, LOW);
  digitalWrite(RELAY_PIN_VU_METER, LOW);
  digitalWrite(RELAY_PIN_LED, LOW);
  digitalWrite(RELAY_PIN_MUTE, LOW);
  digitalWrite(SOURCE_RELAY_1_PIN, LOW); // Гасим все реле источников — взаимоисключающий выбор на паузе
  digitalWrite(SOURCE_RELAY_2_PIN, LOW);
  digitalWrite(SOURCE_RELAY_3_PIN, LOW);

  stopAllMotors();
  delay(100); // Небольшая задержка для гарантированного отключения
  u8g2.setPowerSave(1); // Выключаем дисплей
  volumeRing.clear(); // Гасим кольца Volume/Bass/High
  volumeRing.show();
  bassRing.clear();
  bassRing.show();
  highRing.clear();
  highRing.show();
}

void powerOnDevices() {
  // Включение всех подключенных устройств
  u8g2.setPowerSave(0); // Включаем дисплей
  powerOnScreen(); // Отображаем "POWER ON"
  pinMode(LED_BASS_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_VOLUME_PIN, OUTPUT);
  digitalWrite(LED_BASS_PIN, HIGH);
  digitalWrite(LED_HIGH_PIN, HIGH);
  digitalWrite(LED_VOLUME_PIN, HIGH);

  pinMode(RELAY_PIN_STANDBY, OUTPUT);
  pinMode(RELAY_PIN_VU_METER, OUTPUT);
  pinMode(RELAY_PIN_LED, OUTPUT);
  pinMode(RELAY_PIN_MUTE, OUTPUT);
  digitalWrite(RELAY_PIN_STANDBY, HIGH); // Включаем Standby
  digitalWrite(RELAY_PIN_VU_METER, HIGH); // Включаем VU Meter
  digitalWrite(RELAY_PIN_MUTE, LOW); // Оставляем Mute выключенным
  digitalWrite(RELAY_PIN_LED, LOW); // Led выключен по умолчанию
  applySourceSelection(); // Восстанавливаем выбранный источник из settings[7]

  drawMenu(); // Отображаем меню после "POWER ON"
}

void checkEncoderButton() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!encoderButtonPressed) {
      unsigned long currentTime = millis();
      if (currentTime - lastButtonPressTime < doubleClickThreshold) {
        // Обнаружено двойное нажатие
        encoderButtonPressed = true;
        inSettingsMode = false;
        resetCursor(); // Сбрасываем положение курсора при выходе из режима настроек
        encoderValue = 0; // Сбрасываем значение энкодера при выходе из режима настроек
        stopAllMotors(); // Остановка всех моторов при выходе из режима настроек
        drawMenu();
      } else {
        // Одиночное нажатие, переключить режим настроек
        encoderButtonPressed = true;
        inSettingsMode = !inSettingsMode;
        if (inSettingsMode) {
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
            drawToggleSwitch(settings[currentMenuItem] == 1);
          } else if (menuItems[currentMenuItem] == "Dimmer") {
            drawDimmerScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Color") {
            drawColorScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Source") {
            drawSourceScreen(settings[currentMenuItem]);
          } else {
            drawArrowIndicator(settings[currentMenuItem], false, false); // Переход на экран с кругом и стрелочкой для Bass, High, Volume
          }
        } else {
          resetCursor(); // Сбрасываем положение курсора при выходе из режима настроек
          encoderValue = 0; // Сбрасываем значение энкодера при выходе из режима настроек
          stopAllMotors(); // Остановка всех моторов при выходе из режима настроек
          drawMenu();
        }
      }
      lastButtonPressTime = currentTime;
    }
  } else {
    encoderButtonPressed = false;
  }
}

void motorControl(int val, byte pinIN, byte pinPWM) {
  val = map(val, -50, 50, -255, 255);

  if (val > 0) {  // Вперёд
    analogWrite(pinPWM, val);
    digitalWrite(pinIN, LOW);
  } else if (val < 0) {  // Назад
    analogWrite(pinPWM, 255 + val);
    digitalWrite(pinIN, HIGH);
  } else {  // Стоп
    digitalWrite(pinIN, LOW);
    digitalWrite(pinPWM, LOW);
  }
}

void motorControl2(int val, byte pinIN1, byte pinIN2, byte pinPWM1, byte pinPWM2) {
  val = map(val, -50, 50, -255, 255);

  if (val > 0) {  // Вперёд
    analogWrite(pinPWM1, val);
    analogWrite(pinPWM2, 0);
    digitalWrite(pinIN1, HIGH);
    digitalWrite(pinIN2, LOW);
  } else if (val < 0) {  // Назад
    analogWrite(pinPWM1, 0);
    analogWrite(pinPWM2, -val);
    digitalWrite(pinIN1, LOW);
    digitalWrite(pinIN2, HIGH);
  } else {  // Стоп
    analogWrite(pinPWM1, 0);
    analogWrite(pinPWM2, 0);
    digitalWrite(pinIN1, LOW);
    digitalWrite(pinIN2, LOW);
  }
}

void stopAllMotors() {
  motorControl(0, MOTOR1_IN, MOTOR1_PWM);
  motorControl(0, MOTOR2_IN, MOTOR2_PWM);
  motorControl2(0, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
}

void resetCursor() {
  if (menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High" || menuItems[currentMenuItem] == "Volume") {
    settings[currentMenuItem] = 0; // Сбрасываем положение курсора в ноль
  }
}

void saveSettings() {
  // Здесь можно добавить код для сохранения состояния settings в EEPROM
}

void loadSettings() {
  // Здесь можно добавить код для загрузки состояния settings из EEPROM
}

void handleRemoteInput() {
  if (irReceived) {
    irReceived = false; // Сброс флага

    Serial.print("Received IR command: 0x");
    Serial.println(irCommand, HEX); // Отладочный вывод

    switch (irCommand) {
      case IR_RIGHT:
        Serial.println("Right button pressed"); // Отладочный вывод
        if (!inSettingsMode) {
          currentMenuItem = (currentMenuItem + 1) % 8;
          Serial.print("Current Menu Item: ");
          Serial.println(currentMenuItem); // Отладочный вывод
          drawMenu();
        } else {
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
            settings[currentMenuItem] = 1;
            drawToggleSwitch(true);
            if (menuItems[currentMenuItem] == "VU Meter") {
              digitalWrite(RELAY_PIN_VU_METER, HIGH);
            } else if (menuItems[currentMenuItem] == "Bypass") {
              digitalWrite(RELAY_PIN_LED, HIGH);
            }
          } else if (menuItems[currentMenuItem] == "Dimmer") {
            settings[currentMenuItem] = constrain(settings[currentMenuItem] + 5, 0, 100);
            applyRingDimmer();
            drawDimmerScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Color") {
            settings[currentMenuItem] = (settings[currentMenuItem] + 1) % RING_COLOR_COUNT;
            applyRingColorScheme();
            drawColorScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Source") {
            settings[currentMenuItem] = (settings[currentMenuItem] + 1) % SOURCE_COUNT;
            applySourceSelection();
            drawSourceScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Bass") {
            motorControl(SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, true, false);
          } else if (menuItems[currentMenuItem] == "High") {
            motorControl(SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, true, false);
          } else if (menuItems[currentMenuItem] == "Volume") {
            motorControl2(SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, true, false);
          }
        }
        break;
      case IR_LEFT:
        Serial.println("Left button pressed"); // Отладочный вывод
        if (!inSettingsMode) {
          currentMenuItem = (currentMenuItem - 1 + 8) % 8;
          Serial.print("Current Menu Item: ");
          Serial.println(currentMenuItem); // Отладочный вывод
          drawMenu();
        } else {
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
            settings[currentMenuItem] = 0;
            drawToggleSwitch(false);
            if (menuItems[currentMenuItem] == "VU Meter") {
              digitalWrite(RELAY_PIN_VU_METER, LOW);
            } else if (menuItems[currentMenuItem] == "Bypass") {
              digitalWrite(RELAY_PIN_LED, LOW);
            }
          } else if (menuItems[currentMenuItem] == "Dimmer") {
            settings[currentMenuItem] = constrain(settings[currentMenuItem] - 5, 0, 100);
            applyRingDimmer();
            drawDimmerScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Color") {
            settings[currentMenuItem] = (settings[currentMenuItem] - 1 + RING_COLOR_COUNT) % RING_COLOR_COUNT;
            applyRingColorScheme();
            drawColorScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Source") {
            settings[currentMenuItem] = (settings[currentMenuItem] - 1 + SOURCE_COUNT) % SOURCE_COUNT;
            applySourceSelection();
            drawSourceScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Bass") {
            motorControl(-SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, false, true);
          } else if (menuItems[currentMenuItem] == "High") {
            motorControl(-SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, false, true);
          } else if (menuItems[currentMenuItem] == "Volume") {
            motorControl2(-SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
            lastMotorInputTime = millis();
            drawArrowIndicator(0, false, true);
          }
        }
        break;
      case IR_ENTER:
        Serial.println("Enter button pressed"); // Отладочный вывод
        if (!inSettingsMode) {
          inSettingsMode = true;
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
            drawToggleSwitch(settings[currentMenuItem] == 1);
          } else if (menuItems[currentMenuItem] == "Dimmer") {
            drawDimmerScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Color") {
            drawColorScreen(settings[currentMenuItem]);
          } else if (menuItems[currentMenuItem] == "Source") {
            drawSourceScreen(settings[currentMenuItem]);
          } else {
            drawArrowIndicator(settings[currentMenuItem], false, false);
          }
        } else {
          inSettingsMode = false;
          resetCursor();
          encoderValue = 0;
          stopAllMotors();
          drawMenu();
        }
        break;
      case IR_MUTE:
        Serial.println("Mute button pressed"); // Отладочный вывод
        isMuted = !isMuted; // Переключаем состояние Mute
        digitalWrite(RELAY_PIN_MUTE, isMuted ? HIGH : LOW); // Управляем реле Mute
        drawMenu(); // Перерисовываем меню для отображения/удаления надписи Mute
        break;
      case IR_POWER:
        if (!powerButtonPressing) {
          powerButtonPressing = true;
          powerButtonPressStartTime = millis();
        } else if (millis() - powerButtonPressStartTime >= 3000) {
          // Длительное нажатие кнопки питания (3 секунды)
          Serial.println("Power button long press"); // Отладочный вывод
          if (powerOff) {
            powerOnDevices();
            powerOff = false;
          } else {
            // Отключение устройств, затем отображение "POWER OFF"
            digitalWrite(LED_BASS_PIN, LOW);
            digitalWrite(LED_HIGH_PIN, LOW);
            digitalWrite(LED_VOLUME_PIN, LOW);
            stopAllMotors();
            delay(100); // Небольшая задержка для гарантированного отключения
            powerOffScreen();
            delay(3000); // 3 секунды для отображения "POWER OFF"
            powerOffDevices();
            powerOff = true;
          }
          powerButtonPressing = false;
        }
        break;
      default:
        Serial.println("Unknown button pressed"); // Отладочный вывод
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // Wait for serial port to connect. Needed for native USB
  }
  Serial.println("Starting setup...");

  u8g2.begin();
  volumeRing.begin();
  bassRing.begin();
  highRing.begin();
  applyRingColorScheme(); // Устанавливаем цвет активной части колец из settings[6]
  int initialRingBrightness = map(settings[5], 0, 100, 0, 255);
  volumeRing.setBrightness(initialRingBrightness);
  bassRing.setBrightness(initialRingBrightness);
  highRing.setBrightness(initialRingBrightness);
  volumeRing.clear();
  volumeRing.show();
  bassRing.clear();
  bassRing.show();
  highRing.clear();
  highRing.show();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(MOTOR1_IN, OUTPUT);
  pinMode(MOTOR1_PWM, OUTPUT);
  pinMode(MOTOR2_IN, OUTPUT);
  pinMode(MOTOR2_PWM, OUTPUT);
  pinMode(MOTOR3_IN1, OUTPUT);
  pinMode(MOTOR3_IN2, OUTPUT);
  pinMode(MOTOR3_PWM1, OUTPUT);
  pinMode(MOTOR3_PWM2, OUTPUT);
  pinMode(RELAY_PIN_STANDBY, OUTPUT);
  pinMode(RELAY_PIN_VU_METER, OUTPUT);
  pinMode(RELAY_PIN_LED, OUTPUT);
  pinMode(RELAY_PIN_MUTE, OUTPUT);
  pinMode(SOURCE_RELAY_1_PIN, OUTPUT);
  pinMode(SOURCE_RELAY_2_PIN, OUTPUT);
  pinMode(SOURCE_RELAY_3_PIN, OUTPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(LED_BASS_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_VOLUME_PIN, OUTPUT);

  loadSettings(); // Загрузка сохраненных настроек

  digitalWrite(RELAY_PIN_STANDBY, HIGH); // Включаем реле Standby
  digitalWrite(RELAY_PIN_VU_METER, HIGH); // Включаем реле VU Meter
  digitalWrite(RELAY_PIN_MUTE, LOW); // Устанавливаем реле Mute в неактивное состояние (низкий уровень для реле высокого уровня)
  digitalWrite(RELAY_PIN_LED, LOW); // Led выключен по умолчанию
  applySourceSelection(); // Включаем источник из settings[7] по умолчанию

  attachInterrupt(digitalPinToInterrupt(IR_PIN), IR_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_B_PIN), encoderISR, CHANGE);

  Serial.println("Setup complete"); // Отладочный вывод

  // Кольца светятся сразу после включения, не только внутри своих пунктов меню
  updateVolumeRing(readVolumePotPercent());
  renderDbRing(bassRing, readBassPotPercent(), bassRingState);
  renderDbRing(highRing, readHighPotPercent(), highRingState);

  drawMenu();
}

void loop() {
  if (irReceived) { // Проверка сигнала с ИК-пульта
    handleRemoteInput(); // Обработка входных данных с пульта
  }

  checkEncoderButton();

  if (encoderValue != 0) {
    if (!inSettingsMode) {
      if (encoderValue > 0) {
        currentMenuItem = (currentMenuItem + 1) % 8;
        Serial.print("Menu item changed to: ");
        Serial.println(menuItems[currentMenuItem]);
      } else if (encoderValue < 0) {
        currentMenuItem = (currentMenuItem - 1 + 8) % 8;
        Serial.print("Menu item changed to: ");
        Serial.println(menuItems[currentMenuItem]);
      }
      encoderValue = 0;
      drawMenu();
    } else {
      if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Bypass") {
        if (encoderValue > 2) { // Добавляем холостой ход в 2 шага
          settings[currentMenuItem] = 1; // Включаем режим ON
          encoderValue = 0;
          drawToggleSwitch(true);
          if (menuItems[currentMenuItem] == "VU Meter") {
            digitalWrite(RELAY_PIN_VU_METER, HIGH); // Включаем реле (высокий уровень для реле высокого уровня)
          } else if (menuItems[currentMenuItem] == "Bypass") {
            digitalWrite(RELAY_PIN_LED, HIGH);
          }
        } else if (encoderValue < -2) { // Добавляем холостой ход в 2 шага
          settings[currentMenuItem] = 0; // Выключаем режим OFF
          encoderValue = 0;
          drawToggleSwitch(false);
          if (menuItems[currentMenuItem] == "VU Meter") {
            digitalWrite(RELAY_PIN_VU_METER, LOW); // Выключаем реле (низкий уровень для реле высокого уровня)
          } else if (menuItems[currentMenuItem] == "Bypass") {
            digitalWrite(RELAY_PIN_LED, LOW);
          }
        }
      } else {
        // Определяем, куда двигается энкодер, и устанавливаем флаги для отображения стрелок
        bool showArrowRight = (encoderValue > 0);
        bool showArrowLeft = (encoderValue < 0);
        int direction = (encoderValue > 0) ? 1 : -1;

        if (menuItems[currentMenuItem] == "Bass") {
          motorControl(direction * SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
          lastMotorInputTime = millis();
          encoderValue = 0;
          drawArrowIndicator(0, showArrowRight, showArrowLeft);
        } else if (menuItems[currentMenuItem] == "High") {
          motorControl(direction * SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
          lastMotorInputTime = millis();
          encoderValue = 0;
          drawArrowIndicator(0, showArrowRight, showArrowLeft);
        } else if (menuItems[currentMenuItem] == "Volume") {
          motorControl2(direction * SLIDER_MOTOR_SPEED, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
          lastMotorInputTime = millis();
          encoderValue = 0;
          drawArrowIndicator(0, showArrowRight, showArrowLeft);
        } else if (menuItems[currentMenuItem] == "Dimmer") {
          settings[currentMenuItem] = constrain(settings[currentMenuItem] + encoderValue * 5, 0, 100);
          encoderValue = 0;
          applyRingDimmer();
          drawDimmerScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "Color") {
          settings[currentMenuItem] = ((settings[currentMenuItem] + direction) % RING_COLOR_COUNT + RING_COLOR_COUNT) % RING_COLOR_COUNT;
          encoderValue = 0;
          applyRingColorScheme();
          drawColorScreen(settings[currentMenuItem]);
        } else if (menuItems[currentMenuItem] == "Source") {
          settings[currentMenuItem] = ((settings[currentMenuItem] + direction) % SOURCE_COUNT + SOURCE_COUNT) % SOURCE_COUNT;
          encoderValue = 0;
          applySourceSelection();
          drawSourceScreen(settings[currentMenuItem]);
        }
      }
    }
  }

  // Обновление светодиодов в режиме настройки
  if (inSettingsMode) {
    if (menuItems[currentMenuItem] == "Bass") {
      blinkLED(LED_BASS_PIN);
    } else if (menuItems[currentMenuItem] == "High") {
      blinkLED(LED_HIGH_PIN);
    } else if (menuItems[currentMenuItem] == "Volume") {
      blinkLED(LED_VOLUME_PIN);
    }
  } else {
    digitalWrite(LED_BASS_PIN, HIGH);
    digitalWrite(LED_HIGH_PIN, HIGH);
    digitalWrite(LED_VOLUME_PIN, HIGH);
  }

  // Кольца Bass/High/Volume светятся всегда (не только внутри своих пунктов меню), пока система не в Standby
  static unsigned long lastRingUpdate = 0;
  if (!powerOff && millis() - lastRingUpdate >= 200) {
    lastRingUpdate = millis();
    updateVolumeRing(readVolumePotPercent());
    renderDbRing(bassRing, readBassPotPercent(), bassRingState);
    renderDbRing(highRing, readHighPotPercent(), highRingState);
  }

  // Во время анимации мигания (возврат в 0dB) кольцу нужно обновляться чаще для
  // плавности — но ТОЛЬКО пока анимация активна, и без перечтения потенциометра
  // (дёшево: просто пересчёт яркости уже известного значения + show())
  static unsigned long lastBlinkRender = 0;
  if (!powerOff && millis() - lastBlinkRender >= 30) {
    if (dbRingBlinking(bassRingState)) {
      renderDbRing(bassRing, bassRingState.lastValue, bassRingState);
    }
    if (dbRingBlinking(highRingState)) {
      renderDbRing(highRing, highRingState.lastValue, highRingState);
    }
    lastBlinkRender = millis();
  }

  // Моторы Bass/High/Volume мгновенно останавливаются, если давно не было новых команд от энкодера/пульта
  if (inSettingsMode && millis() - lastMotorInputTime > SLIDER_MOTOR_IDLE_TIMEOUT) {
    if (menuItems[currentMenuItem] == "Bass") {
      motorControl(0, MOTOR1_IN, MOTOR1_PWM);
    } else if (menuItems[currentMenuItem] == "High") {
      motorControl(0, MOTOR2_IN, MOTOR2_PWM);
    } else if (menuItems[currentMenuItem] == "Volume") {
      motorControl2(0, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
    }
  }

  // Живое обновление экрана с положением ручки Bass/High/Volume (реже, экрану такая частота не нужна)
  static unsigned long lastPotUpdate = 0;
  if (inSettingsMode &&
      (menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High" || menuItems[currentMenuItem] == "Volume") &&
      millis() - lastPotUpdate >= 200) {
    lastPotUpdate = millis();
    drawArrowIndicator(0, false, false);
  }
}

void blinkLED(int pin) {
  static unsigned long lastBlinkTime = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastBlinkTime >= 100) { // 500 мс интервал для мигания
    digitalWrite(pin, !digitalRead(pin)); // Переключаем состояние светодиода
    lastBlinkTime = currentTime;
  }
}
