#include "motor_driver_logic.h"
#include "hardware_settings.h"

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
