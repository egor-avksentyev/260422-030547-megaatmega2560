#include "rc5_icu.h"

// ICP4 = PL0 = Arduino-пин 49 на Mega
#define RC5_ICU_DDR DDRL
#define RC5_ICU_PIN_REG PINL
#define RC5_ICU_PIN_BIT PL0

// Тики таймера при делителе /8 на 16МГц = 0.5мкс/тик. RC5: полубит 889мкс
// (~1778 тиков), целый бит 1778мкс (~3556 тиков). Границы с запасом — те же
// значения, что в проверенной сторонней реализации pinkeen/avr-rc5
#define RC5_SHORT_MIN 888
#define RC5_SHORT_MAX 2666
#define RC5_LONG_MIN 2668
#define RC5_LONG_MAX 4444

enum Rc5State : uint8_t {
  STATE_START1 = 0,
  STATE_MID1 = 1,
  STATE_MID0 = 2,
  STATE_START0 = 3,
  STATE_BEGIN = 4,
  STATE_END = 5,
};

// Таблица переходов состояний — алгоритм Guy Carpenter (Clearwater Software,
// "An Efficient Algorithm for Decoding RC5 Remote Control Signals", 2001),
// та же таблица, что в pinkeen/avr-rc5. Сама логика состояний не менялась —
// поменялся только источник тайминга фронта (см. rc5_icu.h)
static const uint8_t rc5Trans[4] = {0x01, 0x91, 0x9b, 0xfb};

static volatile uint16_t rc5Command = 0;
static volatile uint8_t rc5Ccounter = 14;
static volatile Rc5State rc5State = STATE_BEGIN;
static volatile bool rc5HasNew = false;
static volatile uint16_t rc5LastCapture = 0;

// Не трогает rc5LastCapture специально — следующий захват всё равно попадёт в
// ветку STATE_BEGIN ниже, которая не проверяет длительность интервала вообще
// (первый фронт кадра всегда трактуется как старт, независимо от того, что
// намеряно между ним и предыдущим, уже неактуальным, кадром)
static void rc5Reset() {
  rc5HasNew = false;
  rc5Ccounter = 14;
  rc5Command = 0;
  rc5State = STATE_BEGIN;
  TIMSK4 |= _BV(ICIE4);
}

void rc5IcuInit() {
  RC5_ICU_DDR &= ~_BV(RC5_ICU_PIN_BIT); // вход
  TCCR4A = 0;
  TCCR4B = _BV(CS41); // делитель /8, нормальный режим счёта (WGM43:40 = 0)
  TCCR4B |= _BV(ICES4); // первый захват — по фронту нарастания, дальше тоггл на каждый захват
  rc5Reset();
}

ISR(TIMER4_CAPT_vect) {
  uint16_t capture = ICR4; // защёлкнуто аппаратно в момент фронта, а не сейчас
  uint16_t delay = capture - rc5LastCapture; // корректно оборачивается по модулю 65536
  rc5LastCapture = capture;

  TCCR4B ^= _BV(ICES4); // следующий фронт — противоположный (ICU ловит только один за раз)

  uint8_t event = (RC5_ICU_PIN_REG & _BV(RC5_ICU_PIN_BIT)) ? 2 : 0;
  if (delay > RC5_LONG_MIN && delay < RC5_LONG_MAX) {
    event += 4;
  } else if (delay < RC5_SHORT_MIN || delay > RC5_SHORT_MAX) {
    // Не короткий и не длинный интервал — шум или начало после долгой паузы.
    // Не return — этот же фронт всё равно должен быть учтён как старт кадра ниже
    rc5Reset();
  }

  if (rc5State == STATE_BEGIN) {
    rc5Ccounter--;
    rc5Command |= (uint16_t)1 << rc5Ccounter;
    rc5State = STATE_MID1;
    return;
  }

  uint8_t newState = (rc5Trans[rc5State] >> event) & 0x03;
  if (newState == rc5State || rc5State > STATE_START0) {
    rc5Reset();
    return;
  }
  rc5State = (Rc5State)newState;

  if (rc5State == STATE_MID0) {
    rc5Ccounter--;
  } else if (rc5State == STATE_MID1) {
    rc5Ccounter--;
    rc5Command |= (uint16_t)1 << rc5Ccounter;
  }

  if (rc5Ccounter == 0 && (rc5State == STATE_START1 || rc5State == STATE_MID0)) {
    rc5State = STATE_END;
    rc5HasNew = true;
    TIMSK4 &= ~_BV(ICIE4); // ждём, пока результат заберут (rc5IcuGetFrame) — не перезатираем
  }
}

bool rc5IcuGetFrame(uint8_t &address, uint8_t &command, bool &toggle) {
  if (!rc5HasNew) {
    return false;
  }
  noInterrupts();
  uint16_t frame = rc5Command;
  interrupts();
  toggle = (frame >> 11) & 0x01;
  address = (frame >> 6) & 0x1F;
  command = frame & 0x3F;
  rc5Reset(); // снимает has_new и заново включает захват для следующего кадра
  return true;
}
