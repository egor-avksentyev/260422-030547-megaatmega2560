#include "temperature_sensor.h"
#include "hardware_settings.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// Все три датчика — на одной 1-Wire шине (один пин, один резистор-подтяжка). Индекс 0/1/2
// в getTempCByIndex() — это порядок, в котором DallasTemperature обнаружил датчики при
// сканировании шины (begin()), а не физическое место — если он отвечает "не тому" датчику
// на экране, поменяй местами подписи в tempSensorLabels[] (hardware_settings.h), не провода
static OneWire oneWireBus(TEMP_SENSOR_PIN);
static DallasTemperature sensors(&oneWireBus);

// true, если хотя бы один requestTemperatures() уже был отправлен — на первый вызов
// readAllTemperatures() результата ещё нет (конверсия только запускается), см. ниже
static bool conversionInFlight = false;

void initTemperatureSensors() {
  sensors.begin();
  // Не ждать конверсию внутри requestTemperatures() (по умолчанию блокирует ~750мс на
  // 12-битном разрешении) — читаем результат ПРЕДЫДУЩЕГО запроса на следующем вызове
  // readAllTemperatures(), а не тут же после текущего. INFO_UPDATE_INTERVAL_MS (main.cpp)
  // с запасом больше времени конверсии, так что к следующему тику результат уже готов
  sensors.setWaitForConversion(false);
}

void readAllTemperatures(float* out) {
  if (conversionInFlight) {
    // Если датчика с таким индексом на шине не нашлось (сломан/отвалился/их физически
    // меньше трёх) — getTempCByIndex() сама вернёт DEVICE_DISCONNECTED_C, что совпадает
    // с TEMP_SENSOR_INVALID, отдельно проверять getDeviceCount() не нужно
    out[0] = sensors.getTempCByIndex(0);
    out[1] = sensors.getTempCByIndex(1);
    out[2] = sensors.getTempCByIndex(2);
  } else {
    out[0] = out[1] = out[2] = TEMP_SENSOR_INVALID;
  }
  sensors.requestTemperatures();
  conversionInFlight = true;
}
