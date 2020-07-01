#include <Arduino.h>
#include <DS3231.h>
#include <Wire.h>
#include <stdio.h>
#include <stdlib.h>

// >>>>>> Macros for debugging and sending out info over serial <<<<<<
#define MSG_ERROR(msg)                     \
  Serial.print("[ERROR] ");                \
  Serial.print(msg);                       \
  Serial.print(" ... form " __FILE__ ":"); \
  Serial.println(__LINE__);
#define MSG_DEBUG(msg)                     \
  Serial.print("[DEBUG] ");                \
  Serial.print(msg);                       \
  Serial.print(" ... form " __FILE__ ":"); \
  Serial.println(__LINE__);
#define MSG_INFO(msg)                      \
  Serial.print("[INFO]  ");                \
  Serial.print(msg);                       \
  Serial.print(" ... form " __FILE__ ":"); \
  Serial.println(__LINE__);

// >>>>>> Analog Pins <<<<<<
#define PIEZO A0          // Analog Pin 0
#define PHOTORESISTOR A1  // Analog Pin 1
#define POTENTIOMETER A2  // Analog Pin 2
// Analog Pin 3 is free for use - Open3
// Analog Pin 4 is free for use - Open1 & Open3 | SDA
// Analog Pin 5 is free for use - Open1 & Open3 | SCL

// >>>>>> Digital IO Pins <<<<<<
#define BLUETOOTH_RX 0  // Digital Pin 0
#define BLUETOOTH_TX 1  // Digital Pin 1
#define SYNC2 2         // Digital Pin 2
#define SYNC1 3         // Digital Pin 3
// Digital Pin 4 is free for use - Open4
// Digital Pin 5 is free for use - Open4
#define LED_RED 6         // Digital Pin 6
#define LED_GREEN 7       // Digital Pin 7
#define EXTERNAL_FLASH 8  // Digital Pin 8
#define CAMERA_TRIGGER 9  // Digital Pin 9
#define CAMERA_FOCUS 10   // Digital Pin 10
#define BUTTON 11         // Digital Pin 11
#define DIP_SWITCH 12     // Digital Pin 12
// Digital Pin 13 is free for use - Open2

// >>>>>> Function Prototypes <<<<<<
void automaticMode();
void manualMode();
void captureImage();
void recordTimelapse(uint32_t f_timeToRecord, uint32_t f_clipLength,
                     uint32_t f_fps);

enum class RecordingMode : uint8_t { automatic = 0, manual = 1 };

RTClib rtc;  // real time clock connected over I2C

// comment out/in below line to change the behaviour of automatic mode
// #define START_TIMELAPSE_RECORDING_LDR_AND_RTC_CONTROLLED
// meteorological sunset of 2nd July 2020 20:58:00 in unix time this is
// static const uint32_t recordingStartTime = 1593716280;

void setup() {
  // Initialize the used pins
  pinMode(BLUETOOTH_RX, INPUT);
  pinMode(BLUETOOTH_TX, OUTPUT);
  pinMode(SYNC1, OUTPUT);
  pinMode(SYNC2, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(EXTERNAL_FLASH, OUTPUT);
  pinMode(CAMERA_TRIGGER, OUTPUT);
  pinMode(CAMERA_FOCUS, OUTPUT);
  pinMode(BUTTON,
          INPUT);  // use internal pull up to get falling edge interrupts
  pinMode(DIP_SWITCH, INPUT);

  Wire.begin();
  Serial.begin(115200);

  delay(4000);
}

void loop() {
  RecordingMode recordingMode =
      static_cast<RecordingMode>(digitalRead(DIP_SWITCH));
  switch (recordingMode) {
    case RecordingMode::automatic:
      automaticMode();
      break;
    case RecordingMode::manual:
      manualMode();
      break;
    default:
      MSG_ERROR("Dip switch is in undefined state!");
  }
  delay(200);
}

/**
 * @brief handle manual/test mode
 *
 * when `DIP_SWITCH` is `HIGH` camera can be triggered by pressing `BUTTON`
 */
int prevBtnState = LOW;
void manualMode() {
  digitalWrite(LED_RED, HIGH);

  int btnState = digitalRead(BUTTON);
  ;
  if (btnState != prevBtnState) {
    if (btnState == HIGH) {
      MSG_INFO("Capture image via BUTTON press");
      captureImage();
      MSG_INFO("Image Capture done");
      delay(1500);
    }
  }
  prevBtnState = btnState;
  delay(10);
}

/**
 * @brief handle automatic mode
 *
 */
void automaticMode() {
  digitalWrite(LED_RED, LOW);
#ifdef START_TIMELAPSE_RECORDING_LDR_AND_RTC_CONTROLLED
  DateTime now = rtc.now();
  uint16_t brightness = analogRead(PHOTORESISTOR);

  // TODO: overthink this variable
  static bool timelapseRecorded = false;

  // start recording a timelapse if sunset starts
  // either it is dark enough or the time of the meteorological sunset has
  // passed
  if (brightness < 140 ||
      now.unixtime() >= recordingStartTime && !timelapseRecorded) {
    recordTimelapse(7200, 120, 60);  // record 2h as 2min timeslapse with 60fps
    timelapseRecorded = true;
  }
  delay(1000);
#else
  int btnState = digitalRead(BUTTON);
  if (btnState) {
    MSG_INFO("Capture timelapse via BUTTON press");
    recordTimelapse(7200, 60, 30);  // record 2h as 2min timeslapse with 60fps
    delay(500);
  }
  delay(10);
#endif
}

/**
 * @brief record a given time interval as a fixed length time lapse with
 fixed
 * fps
 *
 * @param f_timeToRecord  time interval to record in seconds
 * @param f_clipLength  length of the final timelapse
 * @param f_fps  fps of the final timelapse
 */
void recordTimelapse(uint32_t f_timeToRecord, uint32_t f_clipLength,
                     uint32_t f_fps) {
  bool recordTimeLapse = true;
  for (uint32_t i = 0ul; i < (f_clipLength * f_fps) && recordTimeLapse; ++i) {
    char msg[21];
    snprintf(msg, 21, "Image number %05i", i);
    MSG_INFO(msg);
    digitalWrite(LED_RED, HIGH);  // indicate start of image capture
    captureImage();
    for (uint32_t s = 0; s < (f_timeToRecord / (f_clipLength * f_fps)) * 100.0f;
         ++s) {
      // if the switch get toggled to off the recording of the timelapse should
      // be stopped
      if (static_cast<RecordingMode>(digitalRead(DIP_SWITCH)) ==
          RecordingMode::manual) {
        recordTimeLapse = false;
        MSG_INFO("stopped recoding of timelapse");
        break;
      }
      delay(10);
    }
    digitalWrite(LED_RED, LOW);  // indicate end of image capture
  }
}

/**
 * @brief trigger camera to record an image
 *
 */
void captureImage() {
  DateTime now = rtc.now();
  char imagename[45];
  snprintf(imagename, 45, "capturing IMG_%04d%02d%02d_%02d%02d%02d", now.year(),
           now.month(), now.day(), now.hour() + 1, now.minute(), now.second());
  MSG_INFO(imagename);

  auto toggleLed = []() {
    digitalWrite(LED_GREEN, HIGH);
    delayMicroseconds(7);
    digitalWrite(LED_GREEN, LOW);
    delayMicroseconds(7);
  };
  // send out sequence via IR to trigger image capturing of camera
  uint8_t i;
  for (i = 0; i < 76; ++i) {
    toggleLed();
  }
  delay(27);
  delayMicroseconds(810);
  for (i = 0; i < 16; ++i) {
    toggleLed();
  }
  delayMicroseconds(1540);
  for (i = 0; i < 16; ++i) {
    toggleLed();
  }
  delayMicroseconds(3545);
  for (i = 0; i < 16; ++i) {
    toggleLed();
  }
}
