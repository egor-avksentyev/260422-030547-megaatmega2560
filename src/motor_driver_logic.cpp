#include "motor_driver_logic.h"
#include "hardware_settings.h"
#include "motor_position.h"
#include "main.h"
#include "neopixel.h"

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

static bool bassSeeking = false;
static int bassSeekTargetRaw = 0;
static bool highSeeking = false;
static int highSeekTargetRaw = 0;
static bool volumeSeeking = false;
static int volumeSeekTargetPercent = 0;

// Сравнивает по raw, а не по "снапнутому" отображаемому значению — тот округляет к 0
// в широкой зоне (BASS_POT_ZERO_SNAP_RAW/HIGH_POT_ZERO_SNAP_RAW, нужной только для чистого
// отображения на экране), из-за чего мотор останавливался на краю этой зоны, недокручивая
// несколько градусов до истинной цели
void requestBassSeek(int targetRaw) {
  int raw;
  readBassPotPercent(&raw);
  bassSeekTargetRaw = targetRaw;
  bassSeeking = abs(raw - targetRaw) > MOTOR_RECENTER_RAW_EPSILON;
}

void requestHighSeek(int targetRaw) {
  int raw;
  readHighPotPercent(&raw);
  highSeekTargetRaw = targetRaw;
  highSeeking = abs(raw - targetRaw) > MOTOR_RECENTER_RAW_EPSILON;
}

void requestBassHighRecenter() {
  requestBassSeek(bassZeroRaw());
  requestHighSeek(highZeroRaw());
}

void cancelBassRecenter() {
  bassSeeking = false;
}

void cancelHighRecenter() {
  highSeeking = false;
}

void updateBassHighRecenter() {
  if (bassSeeking) {
    int raw;
    readBassPotPercent(&raw);
    if (abs(raw - bassSeekTargetRaw) <= MOTOR_RECENTER_RAW_EPSILON) {
      motorControl(0, MOTOR1_IN, MOTOR1_PWM);
      bassSeeking = false;
    } else {
      // Если крутит в обратную сторону — поменяй знак здесь
      motorControl(raw > bassSeekTargetRaw ? -SLIDER_MOTOR_SPEED : SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
      lastMotorInputTime = millis();
    }
  }
  if (highSeeking) {
    int raw;
    readHighPotPercent(&raw);
    if (abs(raw - highSeekTargetRaw) <= MOTOR_RECENTER_RAW_EPSILON) {
      motorControl(0, MOTOR2_IN, MOTOR2_PWM);
      highSeeking = false;
    } else {
      // Если крутит в обратную сторону — поменяй знак здесь
      motorControl(raw > highSeekTargetRaw ? -SLIDER_MOTOR_SPEED : SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
      lastMotorInputTime = millis();
    }
  }
}

void requestVolumeSeek(int targetPercent) {
  int percent = readVolumePotPercent();
  volumeSeekTargetPercent = targetPercent;
  volumeSeeking = abs(percent - targetPercent) > VOLUME_SEEK_EPSILON_PERCENT;
}

void cancelVolumeSeek() {
  volumeSeeking = false;
}

void updateVolumeSeek() {
  if (!volumeSeeking) {
    return;
  }
  int percent = readVolumePotPercent();
  if (abs(percent - volumeSeekTargetPercent) <= VOLUME_SEEK_EPSILON_PERCENT) {
    motorControl2(0, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
    volumeSeeking = false;
  } else {
    // Если крутит в обратную сторону — поменяй знак здесь
    int dir = percent > volumeSeekTargetPercent ? -SLIDER_MOTOR_SPEED : SLIDER_MOTOR_SPEED;
    motorControl2(dir, MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
    lastMotorInputTime = millis();
  }
}

void seekBassHighVolumeToZeroBlocking() {
  requestBassSeek(bassZeroRaw());
  requestHighSeek(highZeroRaw());
  requestVolumeSeek(0);
  unsigned long start = millis();
  while (millis() - start < MOTOR_ZERO_TIMEOUT_MS && (bassSeeking || highSeeking || volumeSeeking)) {
    updateBassHighRecenter();
    updateVolumeSeek();
    // Кольца иначе не обновляются — этот цикл сам не даёт дойти до обычной перерисовки
    // в main.cpp, а ручки при этом реально едут в ноль
    renderDbRing(bassRing, readBassPotPercent(), bassRingState);
    renderDbRing(highRing, readHighPotPercent(), highRingState);
    updateVolumeRing(readVolumePotPercent());
    delay(5);
  }
  stopAllMotors(); // Предохранитель: гарантированно стоп, даже если что-то не успело доехать до таймаута
  // Финальный кадр — на случай если моторы остановились чуть раньше последней отрисовки выше
  renderDbRing(bassRing, readBassPotPercent(), bassRingState);
  renderDbRing(highRing, readHighPotPercent(), highRingState);
  updateVolumeRing(readVolumePotPercent());
}
