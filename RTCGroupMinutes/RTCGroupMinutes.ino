// Experiment 2: DS1307 SDA -> A4, SCL -> A5; TM1637 CLK -> D10, DIO -> D9.
// Both modules use 5V and GND on the Arduino Uno. Requires Adafruit RTClib.
#include <RTClib.h>
#include <Wire.h>

constexpr char GROUP_NUMBER[] = "88";
constexpr uint8_t DISPLAY_CLK = 10;
constexpr uint8_t DISPLAY_DIO = 9;
constexpr uint8_t RTC_ADDRESS = 0x68;
constexpr uint8_t DISPLAY_BRIGHTNESS = 2;
constexpr uint8_t BUS_DELAY_US = 12;
constexpr unsigned long RTC_STALL_MS = 5000;
constexpr uint8_t DIGIT_SEGMENTS[] = {
  0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};
static_assert(sizeof(GROUP_NUMBER) == 3 &&
              GROUP_NUMBER[0] >= '0' && GROUP_NUMBER[0] <= '9' &&
              GROUP_NUMBER[1] >= '0' && GROUP_NUMBER[1] <= '9',
              "Enter a two-digit group number");

RTC_DS1307 rtc;
int8_t lastRtcSecond = -1;
unsigned long lastRtcAdvanceAt = 0;

void driveLow(uint8_t pin) {
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT);
}

void releaseLine(uint8_t pin) {
  pinMode(pin, INPUT_PULLUP);
}

bool sendDisplayBytes(const uint8_t bytes[], uint8_t length) {
  if (digitalRead(DISPLAY_CLK) == LOW || digitalRead(DISPLAY_DIO) == LOW) {
    return false;
  }
  driveLow(DISPLAY_DIO);  // Start: DIO falls while CLK is high.
  delayMicroseconds(BUS_DELAY_US);
  driveLow(DISPLAY_CLK);

  bool acknowledged = true;
  for (uint8_t index = 0; index < length && acknowledged; ++index) {
    uint8_t value = bytes[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (value & 1) {
        releaseLine(DISPLAY_DIO);
      } else {
        driveLow(DISPLAY_DIO);
      }
      value >>= 1;
      delayMicroseconds(BUS_DELAY_US);
      releaseLine(DISPLAY_CLK);
      delayMicroseconds(BUS_DELAY_US);
      driveLow(DISPLAY_CLK);
    }

    releaseLine(DISPLAY_DIO);
    delayMicroseconds(BUS_DELAY_US);
    releaseLine(DISPLAY_CLK);
    delayMicroseconds(BUS_DELAY_US);
    acknowledged = digitalRead(DISPLAY_DIO) == LOW;  // Ninth clock pulse.
    driveLow(DISPLAY_CLK);
    delayMicroseconds(BUS_DELAY_US);
  }

  driveLow(DISPLAY_DIO);  // Stop: DIO rises while CLK is high.
  delayMicroseconds(BUS_DELAY_US);
  releaseLine(DISPLAY_CLK);
  delayMicroseconds(BUS_DELAY_US);
  releaseLine(DISPLAY_DIO);
  delayMicroseconds(BUS_DELAY_US);
  return acknowledged;
}

bool showDisplay(const char frame[]) {
  const uint8_t dataMode = 0x40;  // Auto-increment addresses.
  const uint8_t displayControl = 0x88 | DISPLAY_BRIGHTNESS;
  uint8_t displayData[5] = {0xc0};  // Start at the leftmost digit.

  for (uint8_t index = 0; index < 4; ++index) {
    const char symbol = frame[index];
    if (symbol >= '0' && symbol <= '9') {
      displayData[index + 1] = DIGIT_SEGMENTS[symbol - '0'];
    } else if (symbol == '-') {
      displayData[index + 1] = 0x40;
    }
  }
  displayData[2] |= 0x80;  // Center colon on the clock-style module.
  return sendDisplayBytes(&dataMode, 1)
      && sendDisplayBytes(displayData, sizeof(displayData))
      && sendDisplayBytes(&displayControl, 1);
}

bool readClock(DateTime &time) {
  Wire.beginTransmission(RTC_ADDRESS);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (!rtc.isrunning()) {
    const DateTime buildTime(F(__DATE__), F(__TIME__));
    rtc.adjust(buildTime);
    // The course version of RTClib writes 0; the DS1307 expects 1–7.
    Wire.beginTransmission(RTC_ADDRESS);
    Wire.write(3);
    Wire.write(buildTime.dayOfTheWeek() + 1);
    if (Wire.endTransmission() != 0 || !rtc.isrunning()) {
      return false;
    }
  }
  time = rtc.now();
  return time.hour() < 24 && time.minute() < 60 && time.second() < 60;
}

void printTwoDigits(uint8_t value) {
  if (value < 10) {
    Serial.print('0');
  }
  Serial.print(value);
}

void setup() {
  Serial.begin(9600);
  Wire.setWireTimeout(25000, true);
  rtc.begin();
  releaseLine(DISPLAY_CLK);
  releaseLine(DISPLAY_DIO);
}

void loop() {
  DateTime time;
  const bool clockReady = readClock(time);
  char frame[] = {GROUP_NUMBER[0], GROUP_NUMBER[1], '-', '-', '\0'};
  if (clockReady) {
    frame[2] = '0' + time.minute() / 10;
    frame[3] = '0' + time.minute() % 10;
  }

  const bool displayReady = showDisplay(frame);
  Serial.print(F("Group "));
  Serial.print(GROUP_NUMBER);
  Serial.print(F(" | RTC "));
  if (clockReady) {
    const unsigned long now = millis();
    if (time.second() != lastRtcSecond) {
      lastRtcSecond = time.second();
      lastRtcAdvanceAt = now;
    }
    printTwoDigits(time.hour());
    Serial.print(':');
    printTwoDigits(time.minute());
    Serial.print(':');
    printTwoDigits(time.second());
    if (now - lastRtcAdvanceAt >= RTC_STALL_MS) {
      Serial.print(F(" (seconds not advancing)"));
    }
  } else {
    lastRtcSecond = -1;
    Serial.print(F("unavailable"));
  }
  Serial.print(F(" | Display "));
  Serial.write(frame[0]);
  Serial.write(frame[1]);
  Serial.print(':');
  Serial.print(frame + 2);
  Serial.print(F(" | TM1637 "));
  Serial.println(displayReady ? F("ACK") : F("NO ACK"));

  delay(1000);
}
