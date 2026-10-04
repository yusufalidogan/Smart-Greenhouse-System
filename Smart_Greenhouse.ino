#include <SoftwareSerial.h>
#include "DFRobotDFPlayerMini.h"

// =====================================================
// SMART GREENHOUSE SYSTEM - IMPROVED VERSION
//
// A0  -> Soil Moisture Sensor
// D7  -> Relay
// D2  -> Plant Info Button
// D6  -> Buzzer
// D10 -> Arduino RX <- DFPlayer TX
// D11 -> Arduino TX -> DFPlayer RX
//
// RELAY:
// LOW  = Pump ON
// HIGH = Pump OFF
//
// SD CARD:
// 001.mp3 = Watering started
// 002.mp3 = Watering finished
// 003.mp3 = Plant information
// 004.mp3 = Error warning
// =====================================================


// =====================================================
// DFPLAYER
// =====================================================

SoftwareSerial mySerial(10, 11);
DFRobotDFPlayerMini dfplayer;

bool dfplayerReady = false;


// =====================================================
// PIN DEFINITIONS
// =====================================================

const byte moisturePin = A0;
const byte relayPin    = 7;
const byte buttonPin   = 2;
const byte buzzerPin   = 6;


// =====================================================
// RELAY
// =====================================================

const byte PUMP_ON  = LOW;
const byte PUMP_OFF = HIGH;


// =====================================================
// AUDIO
// =====================================================

const byte AUDIO_START    = 1;
const byte AUDIO_FINISHED = 2;
const byte AUDIO_INFO     = 3;
const byte AUDIO_ERROR    = 4;


// =====================================================
// MOISTURE CALIBRATION
// =====================================================
//
// These values should be measured for your sensor.
//
// DRY_VALUE  = sensor value in dry soil
// WET_VALUE  = sensor value in very wet soil
//
// The program converts the raw analog value to
// approximately 0-100% moisture.
//
// NOTE:
// Depending on the sensor, the direction can be reversed.
// This version assumes:
// higher raw value = drier
// lower raw value  = wetter
// =====================================================

const int DRY_VALUE = 850;
const int WET_VALUE = 300;


// =====================================================
// MOISTURE TARGETS
// =====================================================

const int START_WATERING_PERCENT = 35;
const int STOP_WATERING_PERCENT  = 60;


// =====================================================
// SENSOR FILTER
// =====================================================

const byte SENSOR_SAMPLES = 10;

const int SENSOR_MIN = 5;
const int SENSOR_MAX = 1019;

const byte MAX_INVALID_READINGS = 3;


// =====================================================
// TIMING
// =====================================================

const unsigned long SENSOR_READ_INTERVAL = 1000UL;

const unsigned long BUTTON_DEBOUNCE = 50UL;

const unsigned long WATERING_COOLDOWN = 10000UL;

const unsigned long MAX_WATERING_TIME = 15000UL;

const unsigned long MIN_WATERING_TIME = 2000UL;

const unsigned long SERIAL_PRINT_INTERVAL = 3000UL;


// =====================================================
// WATERING CONFIRMATION
// =====================================================

const byte REQUIRED_WET_READINGS = 3;

byte wetReadings = 0;


// =====================================================
// SYSTEM STATE
// =====================================================

enum SystemState {

  STATE_IDLE,
  STATE_WATERING,
  STATE_ERROR
};

SystemState systemState = STATE_IDLE;


// =====================================================
// GLOBAL VARIABLES
// =====================================================

unsigned long lastSensorRead = 0;

unsigned long lastWateringTime = 0;

unsigned long wateringStartTime = 0;

unsigned long lastSerialPrint = 0;

byte invalidSensorReadings = 0;


// =====================================================
// BUTTON VARIABLES
// =====================================================

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

unsigned long lastButtonChange = 0;


// =====================================================
// SENSOR DATA
// =====================================================

int rawMoisture = 0;

int moisturePercent = 0;


// =====================================================
// PUMP CONTROL
// =====================================================

void pumpOn() {

  digitalWrite(relayPin, PUMP_ON);

  Serial.println("PUMP -> ON");
}


void pumpOff() {

  digitalWrite(relayPin, PUMP_OFF);

  Serial.println("PUMP -> OFF");
}


// =====================================================
// AUDIO
// =====================================================

void playAudio(byte fileNumber) {

  if (dfplayerReady) {

    dfplayer.play(fileNumber);
  }
}


// =====================================================
// SENSOR VALIDATION
// =====================================================

bool validSensorValue(int value) {

  if (value <= SENSOR_MIN) {
    return false;
  }

  if (value >= SENSOR_MAX) {
    return false;
  }

  return true;
}


// =====================================================
// READ RAW SENSOR WITH AVERAGING
// =====================================================

int readRawMoisture() {

  long total = 0;

  byte validSamples = 0;


  for (byte i = 0; i < SENSOR_SAMPLES; i++) {

    int value = analogRead(moisturePin);


    if (validSensorValue(value)) {

      total += value;

      validSamples++;
    }

    // Very short pause only for ADC stability.
    // The main program remains non-blocking.
    delay(2);
  }


  if (validSamples == 0) {

    return -1;
  }


  return total / validSamples;
}


// =====================================================
// CONVERT RAW VALUE TO PERCENT
// =====================================================

int calculateMoisturePercent(int rawValue) {

  int percent;


  // Higher raw value = drier
  percent = map(
    rawValue,
    DRY_VALUE,
    WET_VALUE,
    0,
    100
  );


  // Keep result between 0 and 100.
  percent = constrain(percent, 0, 100);


  return percent;
}


// =====================================================
// READ AND UPDATE SENSOR
// =====================================================

bool updateMoistureSensor() {

  int value = readRawMoisture();


  if (value < 0) {

    invalidSensorReadings++;


    Serial.print("Sensor invalid. Count: ");
    Serial.println(invalidSensorReadings);


    if (invalidSensorReadings >= MAX_INVALID_READINGS) {

      return false;
    }


    return true;
  }


  invalidSensorReadings = 0;


  rawMoisture = value;

  moisturePercent = calculateMoisturePercent(rawMoisture);


  if (millis() - lastSerialPrint >= SERIAL_PRINT_INTERVAL) {

    lastSerialPrint = millis();


    Serial.print("Raw moisture: ");
    Serial.print(rawMoisture);

    Serial.print(" | Moisture: ");
    Serial.print(moisturePercent);

    Serial.println("%");
  }


  return true;
}


// =====================================================
// BUTTON HANDLING
// =====================================================
//
// Uses debounce without blocking the system.
// One action per press.
// =====================================================

void checkButton() {

  bool reading = digitalRead(buttonPin);


  if (reading != lastButtonReading) {

    lastButtonChange = millis();

    lastButtonReading = reading;
  }


  if (millis() - lastButtonChange < BUTTON_DEBOUNCE) {

    return;
  }


  if (reading != stableButtonState) {

    stableButtonState = reading;


    // Button pressed
    if (stableButtonState == LOW) {

      Serial.println("Plant information requested.");

      playAudio(AUDIO_INFO);
    }
  }
}


// =====================================================
// START ERROR STATE
// =====================================================

void enterErrorState() {

  pumpOff();

  systemState = STATE_ERROR;

  Serial.println();
  Serial.println("================================");
  Serial.println(" SENSOR ERROR");
  Serial.println(" SYSTEM STOPPED FOR SAFETY");
  Serial.println("================================");


  tone(buzzerPin, 1000, 700);

  playAudio(AUDIO_ERROR);
}


// =====================================================
// COOLDOWN CHECK
// =====================================================

bool cooldownFinished() {

  if (lastWateringTime == 0) {

    return true;
  }


  return (
    millis() - lastWateringTime
    >= WATERING_COOLDOWN
  );
}


// =====================================================
// START WATERING
// =====================================================

void startWatering() {

  if (systemState != STATE_IDLE) {

    return;
  }


  if (!cooldownFinished()) {

    Serial.println("Watering cooldown active.");

    return;
  }


  systemState = STATE_WATERING;

  wateringStartTime = millis();

  wetReadings = 0;


  Serial.println();
  Serial.println("================================");
  Serial.println(" STARTING SMART WATERING");
  Serial.println("================================");


  // Short warning beep.
  tone(buzzerPin, 2000, 200);


  pumpOn();

  playAudio(AUDIO_START);
}


// =====================================================
// WATERING PROCESS
// =====================================================

void updateWatering() {

  if (systemState != STATE_WATERING) {

    return;
  }


  unsigned long now = millis();


  // ---------------------------------------------------
  // HARD SAFETY TIMEOUT
  // ---------------------------------------------------

  if (now - wateringStartTime >= MAX_WATERING_TIME) {

    Serial.println();
    Serial.println("!!! MAXIMUM WATERING TIME !!!");


    pumpOff();

    tone(buzzerPin, 1000, 700);

    playAudio(AUDIO_ERROR);


    lastWateringTime = millis();

    systemState = STATE_IDLE;

    wetReadings = 0;


    Serial.println("Watering stopped for safety.");

    return;
  }


  // ---------------------------------------------------
  // MINIMUM WATERING TIME
  // ---------------------------------------------------
  //
  // Prevents an immediate stop caused by one noisy
  // reading immediately after pump activation.
  // ---------------------------------------------------

  if (now - wateringStartTime < MIN_WATERING_TIME) {

    return;
  }


  // ---------------------------------------------------
  // SENSOR CHECK
  // ---------------------------------------------------

  if (now - lastSensorRead < SENSOR_READ_INTERVAL) {

    return;
  }


  lastSensorRead = now;


  if (!updateMoistureSensor()) {

    Serial.println("!!! SENSOR FAILURE DURING WATERING !!!");


    enterErrorState();

    lastWateringTime = millis();

    return;
  }


  // ---------------------------------------------------
  // CHECK MOISTURE
  // ---------------------------------------------------

  Serial.print("Watering moisture: ");
  Serial.print(moisturePercent);
  Serial.println("%");


  // ---------------------------------------------------
  // SOIL WET ENOUGH
  // ---------------------------------------------------

  if (moisturePercent >= STOP_WATERING_PERCENT) {

    wetReadings++;


    Serial.print("Wet confirmation: ");
    Serial.println(wetReadings);


    if (wetReadings >= REQUIRED_WET_READINGS) {

      pumpOff();


      Serial.println();
      Serial.println("================================");
      Serial.println(" WATERING COMPLETED");
      Serial.println("================================");


      playAudio(AUDIO_FINISHED);


      lastWateringTime = millis();

      systemState = STATE_IDLE;

      wetReadings = 0;

      return;
    }
  }


  // ---------------------------------------------------
  // SOIL STILL NOT WET ENOUGH
  // ---------------------------------------------------

  else {

    wetReadings = 0;

    Serial.println("Soil is not wet enough yet.");
  }
}


// =====================================================
// AUTOMATIC WATERING DECISION
// =====================================================

void checkAutomaticWatering() {

  if (systemState != STATE_IDLE) {

    return;
  }


  if (moisturePercent <= START_WATERING_PERCENT) {

    Serial.println("Soil condition: DRY");


    if (cooldownFinished()) {

      startWatering();
    }

    else {

      Serial.println("Cooldown active.");
    }
  }


  else {

    Serial.println("Soil condition: OK");
  }
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(9600);

  mySerial.begin(9600);


  // ---------------------------------------------------
  // PIN MODES
  // ---------------------------------------------------

  pinMode(relayPin, OUTPUT);

  pinMode(buttonPin, INPUT_PULLUP);

  pinMode(buzzerPin, OUTPUT);


  // ---------------------------------------------------
  // CRITICAL SAFETY
  // ---------------------------------------------------

  pumpOff();


  delay(500);


  Serial.println();
  Serial.println("================================");
  Serial.println(" SMART GREENHOUSE SYSTEM");
  Serial.println(" IMPROVED VERSION");
  Serial.println("================================");


  // ---------------------------------------------------
  // DFPLAYER
  // ---------------------------------------------------

  if (dfplayer.begin(mySerial)) {

    dfplayerReady = true;

    dfplayer.volume(25);

    Serial.println("DFPlayer: READY");
  }

  else {

    dfplayerReady = false;

    Serial.println("DFPlayer: NOT FOUND");
    Serial.println("Audio disabled.");
  }


  // ---------------------------------------------------
  // INITIAL SENSOR TEST
  // ---------------------------------------------------

  int initialValue = readRawMoisture();


  if (initialValue < 0) {

    Serial.println();
    Serial.println("!!! INITIAL SENSOR ERROR !!!");


    tone(buzzerPin, 1000, 500);

    playAudio(AUDIO_ERROR);

    systemState = STATE_ERROR;
  }

  else {

    rawMoisture = initialValue;

    moisturePercent =
      calculateMoisturePercent(rawMoisture);


    Serial.print("Initial raw value: ");
    Serial.println(rawMoisture);


    Serial.print("Initial moisture: ");
    Serial.print(moisturePercent);
    Serial.println("%");


    Serial.println("Moisture sensor: READY");
  }


  Serial.println();
  Serial.println("================================");
  Serial.println(" SYSTEM READY");
  Serial.println("================================");
}


// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  // ---------------------------------------------------
  // BUTTON
  // ---------------------------------------------------

  checkButton();


  // ---------------------------------------------------
  // ERROR STATE
  // ---------------------------------------------------

  if (systemState == STATE_ERROR) {

    pumpOff();

    // Try sensor again periodically.
    if (millis() - lastSensorRead >= 3000UL) {

      lastSensorRead = millis();


      if (updateMoistureSensor()) {

        if (invalidSensorReadings == 0) {

          Serial.println("Sensor recovered.");

          systemState = STATE_IDLE;
        }
      }
    }


    return;
  }


  // ---------------------------------------------------
  // WATERING STATE
  // ---------------------------------------------------

  if (systemState == STATE_WATERING) {

    updateWatering();

    return;
  }


  // ---------------------------------------------------
  // NORMAL SENSOR READING
  // ---------------------------------------------------

  if (millis() - lastSensorRead >= SENSOR_READ_INTERVAL) {

    lastSensorRead = millis();


    if (!updateMoistureSensor()) {

      enterErrorState();

      return;
    }


    if (invalidSensorReadings > 0) {

      return;
    }


    checkAutomaticWatering();
  }
}
