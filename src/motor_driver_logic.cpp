#include "motor_driver_logic.h"
#include "hardware_settings.h"
#include "motor_position.h"
#include "main.h"

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

static bool bassRecentering = false;
static bool highRecentering = false;

void requestBassHighRecenter() {
  int bassRaw, highRaw;
  readBassPotPercent(&bassRaw);
  readHighPotPercent(&highRaw);
  bassRecentering = abs(bassRaw - bassZeroRaw()) > MOTOR_RECENTER_RAW_EPSILON;
  highRecentering = abs(highRaw - highZeroRaw()) > MOTOR_RECENTER_RAW_EPSILON;
}

void cancelBassRecenter() {
  bassRecentering = false;
}

void cancelHighRecenter() {
  highRecentering = false;
}

// Сравнивает по raw, а не по "снапнутому" отображаемому значению — тот округляет к 0
// в широкой зоне (BASS_POT_ZERO_SNAP_RAW/HIGH_POT_ZERO_SNAP_RAW, нужной только для чистого
// отображения на экране), из-за чего мотор останавливался на краю этой зоны, недокручивая
// несколько градусов до истинного нуля
void updateBassHighRecenter() {
  if (bassRecentering) {
    int raw;
    readBassPotPercent(&raw);
    int targetRaw = bassZeroRaw();
    if (abs(raw - targetRaw) <= MOTOR_RECENTER_RAW_EPSILON) {
      motorControl(0, MOTOR1_IN, MOTOR1_PWM);
      bassRecentering = false;
    } else {
      // Если крутит в обратную сторону — поменяй знак здесь
      motorControl(raw > targetRaw ? -SLIDER_MOTOR_SPEED : SLIDER_MOTOR_SPEED, MOTOR1_IN, MOTOR1_PWM);
      lastMotorInputTime = millis();
    }
  }
  if (highRecentering) {
    int raw;
    readHighPotPercent(&raw);
    int targetRaw = highZeroRaw();
    if (abs(raw - targetRaw) <= MOTOR_RECENTER_RAW_EPSILON) {
      motorControl(0, MOTOR2_IN, MOTOR2_PWM);
      highRecentering = false;
    } else {
      // Если крутит в обратную сторону — поменяй знак здесь
      motorControl(raw > targetRaw ? -SLIDER_MOTOR_SPEED : SLIDER_MOTOR_SPEED, MOTOR2_IN, MOTOR2_PWM);
      lastMotorInputTime = millis();
    }
  }
}
