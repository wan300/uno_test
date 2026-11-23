#include <DHT.h>

#define DHTPIN 2
#define DHTTYPE DHT11

// 修改这一行即可更换串口输出中的组号。
const char GROUP_NUMBER[] = "88";
const unsigned long SAMPLE_INTERVAL_MS = 3000;
const unsigned long SLOW_LED_INTERVAL_MS = 1000;
const unsigned long FAST_LED_INTERVAL_MS = 200;
const float LOW_TEMPERATURE_C = 15.0;
const float HIGH_TEMPERATURE_C = 40.0;

DHT dht(DHTPIN, DHTTYPE);

unsigned long lastSampleAt = 0;
unsigned long lastLedChangeAt = 0;
unsigned long ledIntervalMs = SLOW_LED_INTERVAL_MS;
bool ledIsOn = false;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.begin(9600);
  dht.begin();
}

void loop() {
  const unsigned long now = millis();

  if (now - lastLedChangeAt >= ledIntervalMs) {
    lastLedChangeAt = now;
    ledIsOn = !ledIsOn;
    digitalWrite(LED_BUILTIN, ledIsOn ? HIGH : LOW);
  }

  if (now - lastSampleAt < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleAt = now;

  const float humidity = dht.readHumidity();
  const float temperatureC = dht.readTemperature();

  Serial.print(F("Group "));
  Serial.print(GROUP_NUMBER);
  Serial.print(F(" | "));

  if (isnan(humidity) || isnan(temperatureC)) {
    Serial.println(F("Failed to read from DHT11"));
    return;
  }

  // 15–40 °C 内线性调整每次亮灭切换的间隔，超出范围时保持端点值。
  const float boundedTemperature =
      constrain(temperatureC, LOW_TEMPERATURE_C, HIGH_TEMPERATURE_C);
  const float temperatureFraction =
      (boundedTemperature - LOW_TEMPERATURE_C) / (HIGH_TEMPERATURE_C - LOW_TEMPERATURE_C);
  ledIntervalMs = SLOW_LED_INTERVAL_MS
      - static_cast<unsigned long>(temperatureFraction * (SLOW_LED_INTERVAL_MS - FAST_LED_INTERVAL_MS));

  Serial.print(F("Temperature: "));
  Serial.print(temperatureC, 1);
  Serial.print(F(" C | Humidity: "));
  Serial.print(humidity, 1);
  Serial.print(F(" % | LED toggle: "));
  Serial.print(ledIntervalMs);
  Serial.println(F(" ms"));
}
