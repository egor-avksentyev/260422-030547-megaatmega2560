#include <U8g2lib.h>
#include <Encoder.h>
#include <NecDecoder.h>
#include <SPI.h>

#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Dns.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// Объявляем дисплей с использованием SPI интерфейса
U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 12);

#define ENCODER_A_PIN 6
#define ENCODER_B_PIN 7
#define BUTTON_PIN 8
#define MOTOR1_IN 40
#define MOTOR1_PWM 3
#define MOTOR2_IN 42
#define MOTOR2_PWM 5
#define MOTOR3_IN1 32
#define MOTOR3_IN2 34
#define MOTOR3_PWM1 2
#define MOTOR3_PWM2 4
#define RELAY_PIN_STANDBY 13 // Пин для подключения реле Standby
#define RELAY_PIN_VU_METER 30 // Пин для подключения реле VU Meter
#define RELAY_PIN_LED 44 // Пин для подключения реле Led
#define RELAY_PIN_MUTE 46 // Пин для подключения реле Mute
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

String menuItems[] = {"Bass", "High", "Volume", "VU Meter", "Led", "Weather"};
int currentMenuItem = 0;
int settings[] = {0, 0, 0, 1, 1}; // Устанавливаем VU Meter и Led в "включено" по умолчанию
bool inSettingsMode = false;
bool encoderButtonPressed = false;
unsigned long lastButtonPressTime = 0;
unsigned long doubleClickThreshold = 300; // Порог для обнаружения двойного нажатия в миллисекундах
bool isMuted = false; // Флаг для состояния Mute
bool powerOff = false; // Флаг для состояния питания
unsigned long powerButtonPressStartTime = 0; // Время начала нажатия кнопки питания
bool powerButtonPressing = false; // Флаг для состояния удержания кнопки питания
bool inWeatherMode = false; // Флаг для состояния режима погоды

int lastEncoderA = HIGH;
int lastEncoderB = HIGH;
int encoderValue = 0;
unsigned long lastEncoderTime = 0;
unsigned long encoderDebounceDelay = 10; // Задержка для дебаунса энкодера

Encoder encoder(ENCODER_A_PIN, ENCODER_B_PIN); // Создаем объект энкодера

NecDecoder necDecoder; // Создаем объект для декодирования сигналов пульта

volatile bool irReceived = false;
volatile uint8_t irCommand = 0;

// Ethernet модуль подключения
#define W5500_CS 22
#define W5500_RST 23

byte mac[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0, 0 };
IPAddress serverIP;

const char* api_key = "bdf7329d43e34aec9a27413b27ea0d6f"; // Ваш API ключ от Weatherbit
const char* city = "Mykolaiv"; // Ваш город

EthernetClient client;
EthernetUDP udp;
DNSClient dns;

// Прототипы функций
void drawMenu();
void drawToggleSwitch(bool state);
void drawArrowIndicator(int settingValue, bool showArrowRight, bool showArrowLeft);
void displayMessage(const char* message);
void powerOffScreen();
void powerOnScreen();
void powerOffDevices();
void powerOnDevices();
void checkEncoderButton();
void readEncoder();
void motorControl(int val, byte pinIN, byte pinPWM);
void motorControl2(int val, byte pinIN1, byte pinIN2, byte pinPWM1, byte pinPWM2);
void stopAllMotors();
void resetCursor();
void saveSettings();
void loadSettings();
void handleRemoteInput();
void blinkLED(int pin);
void displayWeather();

void IR_ISR() {
  necDecoder.tick();
  if (necDecoder.available()) {
    irCommand = necDecoder.readCommand();
    irReceived = true;
  }
}

void drawMenu() {
  u8g2.setFont(u8g2_font_ncenB18_tf);
  u8g2.clearBuffer();
  
  // Центрирование точек в меню
  int totalWidth = 6 * 20; // 6 точек по 20 пикселей каждая
  int startX = (148 - totalWidth) / 2; // Вычисление стартовой позиции

  for (int i = 0; i < 6; i++) {
    int x = startX + i * 20;
    int y = 60;
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
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.clearBuffer();

  // Отображаем название пункта меню в правом верхнем углу
  u8g2.setCursor(50, 20);
  u8g2.print(menuItems[currentMenuItem]);

  // Рисуем кружок и стрелочку
  int x = 20; // Круг в левой верхней части экрана
  int y = 20;
  u8g2.drawCircle(x, y, 14); // Кружок уменьшен на 30%
  int arrowX = x + 10 * cos(radians(settingValue));
  int arrowY = y + 10 * sin(radians(settingValue));
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
  int progressBarPos = map(settings[currentMenuItem], -50, 50, 20, 108);
  u8g2.drawBox(progressBarPos, 47, 4, 12);

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
  // Переключаем все пины реле и светодиодов в режим INPUT
  pinMode(LED_BASS_PIN, INPUT);
  pinMode(LED_HIGH_PIN, INPUT);
  pinMode(LED_VOLUME_PIN, INPUT);
  pinMode(RELAY_PIN_STANDBY, INPUT);
  pinMode(RELAY_PIN_VU_METER, INPUT);
  pinMode(RELAY_PIN_LED, INPUT);
  pinMode(RELAY_PIN_MUTE, INPUT);
  stopAllMotors();
  delay(100); // Небольшая задержка для гарантированного отключения
  u8g2.setPowerSave(1); // Выключаем дисплей
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
  digitalWrite(RELAY_PIN_STANDBY, LOW); // Включаем Standby
  digitalWrite(RELAY_PIN_VU_METER, LOW); // Включаем VU Meter
  digitalWrite(RELAY_PIN_LED, LOW); // Включаем Led
  digitalWrite(RELAY_PIN_MUTE, HIGH); // Оставляем Mute выключенным

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
        inWeatherMode = false;
        resetCursor(); // Сбрасываем положение курсора при выходе из режима настроек
        encoderValue = 0; // Сбрасываем значение энкодера при выходе из режима настроек
        stopAllMotors(); // Остановка всех моторов при выходе из режима настроек
        drawMenu();
      } else {
        // Одиночное нажатие, переключить режим настроек или погоды
        encoderButtonPressed = true;
        if (currentMenuItem == 5) {
          inWeatherMode = !inWeatherMode;
          if (inWeatherMode) {
            displayWeather();
          } else {
            drawMenu();
          }
        } else {
          inSettingsMode = !inSettingsMode;
          if (inSettingsMode) {
            if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Led") {
              drawToggleSwitch(settings[currentMenuItem] == 1);
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
      }
      lastButtonPressTime = currentTime;
    }
  } else {
    encoderButtonPressed = false;
  }
}

void readEncoder() {
  unsigned long currentTime = millis();
  if (currentTime - lastEncoderTime > encoderDebounceDelay) {
    int currentEncoderA = digitalRead(ENCODER_A_PIN);
    int currentEncoderB = digitalRead(ENCODER_B_PIN);
    if (currentEncoderA != lastEncoderA || currentEncoderB != lastEncoderB) {
      if (currentEncoderA == LOW && lastEncoderA == HIGH) {
        if (currentEncoderB == LOW) {
          encoderValue++;
        } else {
          encoderValue--;
        }
      }
      lastEncoderA = currentEncoderA;
      lastEncoderB = currentEncoderB;
      lastEncoderTime = currentTime;
    }
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
        if (!inSettingsMode && !inWeatherMode) {
          currentMenuItem = (currentMenuItem + 1) % 6;
          Serial.print("Current Menu Item: ");
          Serial.println(currentMenuItem); // Отладочный вывод
          drawMenu();
        } else if (inWeatherMode) {
          inWeatherMode = false;
          drawMenu();
        } else {
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Led") {
            settings[currentMenuItem] = 1;
            drawToggleSwitch(true);
            if (menuItems[currentMenuItem] == "VU Meter") {
              digitalWrite(RELAY_PIN_VU_METER, LOW);
            } else if (menuItems[currentMenuItem] == "Led") {
              digitalWrite(RELAY_PIN_LED, LOW);
            }
          } else {
            settings[currentMenuItem] = constrain(settings[currentMenuItem] + 1, -50, 50);
            drawArrowIndicator(settings[currentMenuItem], true, false);
            if (menuItems[currentMenuItem] == "Bass") {
              motorControl(settings[currentMenuItem], MOTOR1_IN, MOTOR1_PWM);
            } else if (menuItems[currentMenuItem] == "High") {
              motorControl(settings[currentMenuItem], MOTOR2_IN, MOTOR2_PWM);
            } else if (menuItems[currentMenuItem] == "Volume") {
              motorControl2(settings[currentMenuItem], MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
            }
          }
        }
        break;
      case IR_LEFT:
        Serial.println("Left button pressed"); // Отладочный вывод
        if (!inSettingsMode && !inWeatherMode) {
          currentMenuItem = (currentMenuItem - 1 + 6) % 6;
          Serial.print("Current Menu Item: ");
          Serial.println(currentMenuItem); // Отладочный вывод
          drawMenu();
        } else if (inWeatherMode) {
          inWeatherMode = false;
          drawMenu();
        } else {
          if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Led") {
            settings[currentMenuItem] = 0;
            drawToggleSwitch(false);
            if (menuItems[currentMenuItem] == "VU Meter") {
              digitalWrite(RELAY_PIN_VU_METER, HIGH);
            } else if (menuItems[currentMenuItem] == "Led") {
              digitalWrite(RELAY_PIN_LED, HIGH);
            }
          } else {
            settings[currentMenuItem] = constrain(settings[currentMenuItem] - 1, -50, 50);
            drawArrowIndicator(settings[currentMenuItem], false, true);
            if (menuItems[currentMenuItem] == "Bass") {
              motorControl(settings[currentMenuItem], MOTOR1_IN, MOTOR1_PWM);
            } else if (menuItems[currentMenuItem] == "High") {
              motorControl(settings[currentMenuItem], MOTOR2_IN, MOTOR2_PWM);
            } else if (menuItems[currentMenuItem] == "Volume") {
              motorControl2(settings[currentMenuItem], MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
            }
          }
        }
        break;
      case IR_ENTER:
        Serial.println("Enter button pressed"); // Отладочный вывод
        if (!inSettingsMode && !inWeatherMode) {
          if (currentMenuItem == 5) {
            inWeatherMode = true;
            displayWeather();
          } else {
            inSettingsMode = true;
            if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Led") {
              drawToggleSwitch(settings[currentMenuItem] == 1);
            } else {
              drawArrowIndicator(settings[currentMenuItem], false, false);
            }
          }
        } else if (inWeatherMode) {
          inWeatherMode = false;
          drawMenu();
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
        digitalWrite(RELAY_PIN_MUTE, isMuted ? LOW : HIGH); // Управляем реле Mute
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
  pinMode(IR_PIN, INPUT);
  pinMode(LED_BASS_PIN, OUTPUT);
  pinMode(LED_HIGH_PIN, OUTPUT);
  pinMode(LED_VOLUME_PIN, OUTPUT);

  loadSettings(); // Загрузка сохраненных настроек

  digitalWrite(RELAY_PIN_STANDBY, LOW); // Включаем реле Standby
  digitalWrite(RELAY_PIN_VU_METER, LOW); // Включаем реле VU Meter
  digitalWrite(RELAY_PIN_LED, LOW); // Включаем реле Led
  digitalWrite(RELAY_PIN_MUTE, HIGH); // Устанавливаем реле Mute в неактивное состояние (высокий уровень для реле низкого уровня)

  attachInterrupt(digitalPinToInterrupt(IR_PIN), IR_ISR, FALLING);

  // Инициализация Ethernet модуля с таймаутом
  pinMode(W5500_RST, OUTPUT);
  digitalWrite(W5500_RST, LOW);
  delay(200);
  digitalWrite(W5500_RST, HIGH);

  // Генерация случайных байтов для MAC
  mac[4] = random(0, 256);
  mac[5] = random(0, 256);

  Ethernet.init(W5500_CS);
  bool ethernetInitialized = false;

  Serial.println("Checking Ethernet hardware status...");
  Ethernet.begin(mac);

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Ethernet shield was not found.");
   displayMessage("Ethernet shield not found");
  } else if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("LAN not connected");
   displayMessage("LAN not connected");
  } else {
    Serial.println("LAN cable connected");
    displayMessage("LAN cable connected");

    unsigned long connectionStartTime = millis();
    while ((millis() - connectionStartTime) < 10000) { // Таймаут 10 секунд
      if (Ethernet.begin(mac) != 0) {
        ethernetInitialized = true;
        break;
      }
      delay(100);
    }

    if (ethernetInitialized) {
      Serial.println("Ethernet initialized successfully");

      dns.begin(Ethernet.dnsServerIP());
      int dnsStatus = dns.getHostByName("api.weatherbit.io", serverIP);
      if (dnsStatus != 1) {
        Serial.print("DNS lookup failed with status: ");
        Serial.println(dnsStatus);
      }
    } else {
      Serial.println("Failed to initialize Ethernet. Skipping network features.");
    }
  }

  Serial.println("Setup complete"); // Отладочный вывод

  drawMenu();
}

void loop() {
  if (irReceived) { // Проверка сигнала с ИК-пульта
    handleRemoteInput(); // Обработка входных данных с пульта
  }

  readEncoder();
  checkEncoderButton();

  if (encoderValue != 0) {
    if (!inSettingsMode && !inWeatherMode) {
      if (encoderValue > 0) {
        currentMenuItem = (currentMenuItem + 1) % 6;
        Serial.print("Menu item changed to: ");
        Serial.println(menuItems[currentMenuItem]);
      } else if (encoderValue < 0) {
        currentMenuItem = (currentMenuItem - 1 + 6) % 6;
        Serial.print("Menu item changed to: ");
        Serial.println(menuItems[currentMenuItem]);
      }
      encoderValue = 0;
      drawMenu();
    } else if (inSettingsMode) {
      if (menuItems[currentMenuItem] == "VU Meter" || menuItems[currentMenuItem] == "Led") {
        if (encoderValue > 2) { // Добавляем холостой ход в 2 шага
          settings[currentMenuItem] = 1; // Включаем режим ON
          encoderValue = 0;
          drawToggleSwitch(true);
          if (menuItems[currentMenuItem] == "VU Meter") {
            digitalWrite(RELAY_PIN_VU_METER, LOW); // Включаем реле (низкий уровень для реле низкого уровня)
          } else if (menuItems[currentMenuItem] == "Led") {
            digitalWrite(RELAY_PIN_LED, LOW); // Включаем реле (низкий уровень для реле низкого уровня)
          }
        } else if (encoderValue < -2) { // Добавляем холостой ход в 2 шага
          settings[currentMenuItem] = 0; // Выключаем режим OFF
          encoderValue = 0;
          drawToggleSwitch(false);
          if (menuItems[currentMenuItem] == "VU Meter") {
            digitalWrite(RELAY_PIN_VU_METER, HIGH); // Выключаем реле (высокий уровень для реле низкого уровня)
          } else if (menuItems[currentMenuItem] == "Led") {
            digitalWrite(RELAY_PIN_LED, HIGH); // Выключаем реле (высокий уровень для реле низкого уровня)
          }
        }
      } else {
        // Определяем, куда двигается энкодер, и устанавливаем флаги для отображения стрелок
        bool showArrowRight = (encoderValue > 0);
        bool showArrowLeft = (encoderValue < 0);
        settings[currentMenuItem] = constrain(settings[currentMenuItem] + encoderValue, -50, 50);
        encoderValue = 0;
        if (menuItems[currentMenuItem] == "Bass" || menuItems[currentMenuItem] == "High" || menuItems[currentMenuItem] == "Volume") {
          drawArrowIndicator(settings[currentMenuItem], showArrowRight, showArrowLeft);
        } else {
          drawToggleSwitch(settings[currentMenuItem] == 1); // Переход на экран с toggle switch для VU Meter и Led
        }

        // Управление моторами в режиме настройки Bass или High или Volume
        if (menuItems[currentMenuItem] == "Bass") {
          motorControl(settings[currentMenuItem], MOTOR1_IN, MOTOR1_PWM);
        } else if (menuItems[currentMenuItem] == "High") {
          motorControl(settings[currentMenuItem], MOTOR2_IN, MOTOR2_PWM);
        } else if (menuItems[currentMenuItem] == "Volume") {
          motorControl2(settings[currentMenuItem], MOTOR3_IN1, MOTOR3_IN2, MOTOR3_PWM1, MOTOR3_PWM2);
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
}

void blinkLED(int pin) {
  static unsigned long lastBlinkTime = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastBlinkTime >= 100) { // 500 мс интервал для мигания
    digitalWrite(pin, !digitalRead(pin)); // Переключаем состояние светодиода
    lastBlinkTime = currentTime;
  }
}

void displayWeather() {
    Serial.println("Initializing Ethernet...");
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.clearBuffer();
    u8g2.setCursor(0, 0);
    u8g2.print("Initializing Ethernet...");
    u8g2.sendBuffer();

    if (Ethernet.begin(mac) == 0) {
        Serial.println("Failed to configure Ethernet using DHCP");
        u8g2.setCursor(0, 10);
        u8g2.print("DHCP failed!");
        u8g2.sendBuffer();
        while (true) {
            delay(1);
        }
    }
    Serial.println("DHCP configuration:");
    u8g2.setCursor(0, 10);
    u8g2.print("DHCP success!");
    u8g2.sendBuffer();
    delay(1000);

    Serial.print("IP Address: ");
    Serial.println(Ethernet.localIP());
    Serial.print("Subnet Mask: ");
    Serial.println(Ethernet.subnetMask());
    Serial.print("Gateway: ");
    Serial.println(Ethernet.gatewayIP());
    Serial.print("DNS: ");
    Serial.println(Ethernet.dnsServerIP());

    // Инициализация DNS
    dns.begin(Ethernet.dnsServerIP());

    u8g2.setCursor(0, 20);
    u8g2.print("Resolving DNS...");
    u8g2.sendBuffer();

    // Разрешение имени хоста через DNS
    int dnsStatus = dns.getHostByName("api.weatherbit.io", serverIP);
    if (dnsStatus == 1) {
        Serial.print("Server IP: ");
        Serial.println(serverIP);
        u8g2.setCursor(0, 30);
        u8g2.print("DNS success!");
        u8g2.sendBuffer();
    } else {
        Serial.print("DNS lookup failed with status: ");
        Serial.println(dnsStatus);
        u8g2.setCursor(0, 30);
        u8g2.print("DNS failed!");
        u8g2.sendBuffer();
    }

    dnsStatus = dns.getHostByName("api.weatherbit.io", serverIP);
    if (dnsStatus != 1) {
        Serial.print("DNS lookup failed with status: ");
        Serial.println(dnsStatus);
        u8g2.setCursor(0, 30);
        u8g2.print("DNS failed!");
        u8g2.sendBuffer();
        delay(10000);
        return;
    }

    int maxAttempts = 5; // Максимальное количество попыток подключения
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        Serial.print("Attempt ");
        Serial.print(attempt);
        Serial.println(" to connect to server...");
        u8g2.setCursor(0, 40);
        u8g2.print("Connecting to server...");
        u8g2.sendBuffer();

        if (client.connect(serverIP, 80)) {
            Serial.println("Connected to server");
            u8g2.setCursor(0, 50);
            u8g2.print("Connected!");
            u8g2.sendBuffer();

            String url = "/v2.0/current?city=";
            url += city;
            url += "&key=";
            url += api_key;

            Serial.print("Requesting URL: ");
            Serial.println(url);
            u8g2.setCursor(0, 60);
            u8g2.print("Requesting data...");
            u8g2.sendBuffer();

            client.println("GET " + url + " HTTP/1.1");
            client.println("Host: api.weatherbit.io");
            client.println("User-Agent: Arduino/1.0");
            client.println("Accept: */*");
            client.println("Connection: close");
            client.println();

            unsigned long timeout = millis() + 10000; // Увеличиваем тайм-аут до 10 секунд
            while (client.connected() && !client.available() && millis() < timeout) {
                delay(10);
            }

            if (client.available()) {
                Serial.println("Receiving response");
                u8g2.setCursor(0, 70);
                u8g2.print("Receiving response...");
                u8g2.sendBuffer();

                String line;
                while (client.connected() || client.available()) {
                    line = client.readStringUntil('\n');
                    Serial.println(line);
                    if (line == "\r") {
                        break;
                    }
                }

                String payload = client.readString();
                Serial.println("Response: " + payload);

                if (payload.length() == 0) {
                    Serial.println("Empty response from server");
                    u8g2.setCursor(0, 80);
                    u8g2.print("Empty response!");
                    u8g2.sendBuffer();
                    return;
                }

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload);

                if (error) {
                    Serial.print(F("deserializeJson() failed: "));
                    Serial.println(error.c_str());
                    u8g2.setCursor(0, 80);
                    u8g2.print("JSON error!");
                    u8g2.sendBuffer();
                    return;
                }

                float temperature = doc["data"][0]["temp"];
                const char* weather = doc["data"][0]["weather"]["description"];

                u8g2.clearBuffer();
                u8g2.setFont(u8g2_font_ncenB08_tr);
                u8g2.setCursor(0, 10);
                u8g2.print("Weather in ");
                u8g2.print(city);
                u8g2.setCursor(0, 30);
                u8g2.print("Temp: ");
                u8g2.setFont(u8g2_font_ncenB10_tr);
                u8g2.print(temperature);
                u8g2.print(" C");
                u8g2.setFont(u8g2_font_ncenB08_tr);
                u8g2.setCursor(0, 50);
                u8g2.print("Condition: ");
                u8g2.setCursor(0, 60);
                u8g2.print(weather);
                u8g2.sendBuffer();
                break; // Прерываем цикл, если подключение успешно
            } else {
                Serial.println("No response or timeout");
                u8g2.setCursor(0, 80);
                u8g2.print("No response!");
                u8g2.sendBuffer();
            }
            client.stop();
        } else {
            Serial.println("Connection failed");
            u8g2.setCursor(0, 50);
            u8g2.print("Connection failed!");
            u8g2.sendBuffer();
        }
        delay(2000); // Задержка перед повторной попыткой
    }
}
