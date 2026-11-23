// Experiment 2: DS1307 SDA -> A4, SCL -> A5; TM1637 CLK -> D10, DIO -> D9.
// Both modules use 5V and GND on the Arduino Uno. Requires Adafruit RTClib.
// To set local time, send SET YYYY-MM-DD HH:MM:SS at 9600 baud with a newline.
#include <RTClib.h>
#include <Wire.h>
#include <stdio.h>

constexpr char GROUP_NUMBER[] = "88";
constexpr uint8_t DISPLAY_CLK = 10;
constexpr uint8_t DISPLAY_DIO = 9;
constexpr uint8_t RTC_ADDRESS = 0x68;
constexpr uint8_t DISPLAY_BRIGHTNESS = 2;
constexpr uint8_t BUS_DELAY_US = 12;
constexpr unsigned long UPDATE_INTERVAL_MS = 1000;
constexpr unsigned long RTC_STALL_MS = 3000;
constexpr uint8_t DIGIT_SEGMENTS[] = {
  0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};
static_assert(sizeof(GROUP_NUMBER) == 3 &&
              GROUP_NUMBER[0] >= '0' && GROUP_NUMBER[0] <= '9' &&
              GROUP_NUMBER[1] >= '0' && GROUP_NUMBER[1] <= '9',
              "Enter a two-digit group number");

RTC_DS1307 rtc;
bool timeKnown = false;
bool haveRtcSample = false;
bool rtcTracking = false;
uint32_t clockSeconds = 0;
uint32_t previousRtcSeconds = 0;
unsigned long clockTickAt = 0;
unsigned long lastRtcAdvanceAt = 0;
unsigned long lastDisplayAt = 0;

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
  if (Wire.endTransmission() != 0 || !rtc.isrunning()) {
    return false;
  }
  time = rtc.now();
  return time.year() >= 2000 && time.year() <= 2099
      && time.month() >= 1 && time.month() <= 12
      && time.day() >= 1 && time.day() <= 31
      && time.hour() < 24 && time.minute() < 60 && time.second() < 60;
}

bool setRtcTime(const DateTime &time) {
  // Write all seven clock registers together, including a weekday from 1 to 7.
  const uint8_t fields[] = {
    time.second(), time.minute(), time.hour(),
    uint8_t(time.dayOfTheWeek() + 1), time.day(), time.month(),
    uint8_t(time.year() - 2000)
  };
  Wire.beginTransmission(RTC_ADDRESS);
  Wire.write(0);
  for (const uint8_t field : fields) {
    Wire.write(((field / 10) << 4) | (field % 10));
  }
  return Wire.endTransmission() == 0;
}

void anchorClock(uint32_t seconds, unsigned long now) {
  clockSeconds = seconds;
  clockTickAt = now;
  timeKnown = true;
}

bool syncClock(const char *command) {
  unsigned int year, month, day, hour, minute, second;
  if (strlen(command) != 23
      || sscanf(command, "SET %4u-%2u-%2u %2u:%2u:%2u",
                &year, &month, &day, &hour, &minute, &second) != 6
      || year < 2000 || year > 2099 || month < 1 || month > 12
      || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
    return false;
  }

  const DateTime requested(year, month, day, hour, minute, second);
  const uint32_t timestamp = requested.unixtime();
  const DateTime normalized(timestamp);
  if (normalized.month() != month || normalized.day() != day) {
    return false;
  }

  anchorClock(timestamp, millis());
  haveRtcSample = false;
  rtcTracking = false;
  const bool rtcWriteAck = setRtcTime(requested);
  Serial.print(F("SYNC OK | "));
  Serial.print(command + 4);
  Serial.println(rtcWriteAck ? F(" | DS1307 write ACK") : F(" | DS1307 write failed"));
  return true;
}

bool handleSerial() {
  static char command[24];
  static uint8_t length = 0;
  static bool overflow = false;
  bool synchronized = false;

  while (Serial.available() > 0) {
    const char next = Serial.read();
    if (next == '\n') {
      if (length != 0) {
        command[length] = '\0';
        if (!overflow && syncClock(command)) {
          synchronized = true;
        } else {
          Serial.println(F("SYNC ERROR | Use SET YYYY-MM-DD HH:MM:SS"));
        }
      }
      length = 0;
      overflow = false;
    } else if (next != '\r') {
      if (length < sizeof(command) - 1) {
        command[length++] = next;
      } else {
        overflow = true;
      }
    }
  }
  return synchronized;
}

bool updateClock(unsigned long now) {
  if (timeKnown) {
    const unsigned long elapsedSeconds = (now - clockTickAt) / 1000;
    clockSeconds += elapsedSeconds;
    clockTickAt += elapsedSeconds * 1000;
  }

  DateTime rtcTime;
  if (!readClock(rtcTime)) {
    haveRtcSample = false;
    rtcTracking = false;
    return false;
  }
  const uint32_t rtcSeconds = rtcTime.unixtime();
  const uint32_t step = rtcSeconds - previousRtcSeconds;
  const uint32_t difference = rtcSeconds > clockSeconds
      ? rtcSeconds - clockSeconds : clockSeconds - rtcSeconds;

  // Only follow the RTC once it ticks. Avoid jumping back to a stale RTC.
  if (haveRtcSample && step >= 1 && step <= 3 && (!timeKnown || difference <= 2)) {
    anchorClock(rtcSeconds, now);
    rtcTracking = true;
    lastRtcAdvanceAt = now;
  }
  haveRtcSample = true;
  previousRtcSeconds = rtcSeconds;
  return rtcTracking && now - lastRtcAdvanceAt < RTC_STALL_MS;
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
  const bool synchronized = handleSerial();
  const unsigned long now = millis();
  if (!synchronized && now - lastDisplayAt < UPDATE_INTERVAL_MS) {
    return;
  }
  lastDisplayAt = now;

  const bool usingRtc = updateClock(now);
  const DateTime time = timeKnown ? DateTime(clockSeconds) : DateTime();
  char frame[] = {GROUP_NUMBER[0], GROUP_NUMBER[1], '-', '-', '\0'};
  if (timeKnown) {
    frame[2] = '0' + time.minute() / 10;
    frame[3] = '0' + time.minute() % 10;
  }

  const bool displayReady = showDisplay(frame);
  Serial.print(F("Group "));
  Serial.print(GROUP_NUMBER);
  Serial.print(F(" | Time "));
  if (timeKnown) {
    printTwoDigits(time.hour());
    Serial.print(':');
    printTwoDigits(time.minute());
    Serial.print(':');
    printTwoDigits(time.second());
    Serial.print(usingRtc ? F(" | Source DS1307") : F(" | Source Uno timer"));
  } else {
    Serial.print(F("awaiting sync"));
  }
  Serial.print(F(" | Display "));
  Serial.write(frame[0]);
  Serial.write(frame[1]);
  Serial.print(':');
  Serial.print(frame + 2);
  Serial.print(F(" | TM1637 "));
  Serial.println(displayReady ? F("ACK") : F("NO ACK"));
}
