```cpp
/************************************************************
 * ESP32 TEMPERATURE CONTROLLER
 *
 * MAX6675
 * 20x4 I2C LCD
 * 3 Buttons
 * Relay
 * Blynk IoT
 *
 * ==========================================================
 * PIN CONFIGURATION
 * ==========================================================
 *
 * MAX6675
 * SCK -> GPIO 18
 * CS  -> GPIO 19
 * SO  -> GPIO 5
 *
 * LCD I2C
 * SDA -> GPIO 22
 * SCL -> GPIO 21
 *
 * Buttons
 * B1 UP     -> GPIO 32
 * B2 DOWN   -> GPIO 33
 * B3 SELECT -> GPIO 25
 *
 * Relay
 * IN -> GPIO 13
 *
 ************************************************************/

#define BLYNK_PRINT Serial

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <max6675.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>


// ==========================================================
// BLYNK CONFIGURATION
// ==========================================================

#define BLYNK_TEMPLATE_ID   "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "YOUR_TEMPLATE_NAME"
#define BLYNK_AUTH_TOKEN    "YOUR_AUTH_TOKEN"

char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";


// ==========================================================
// PIN CONFIGURATION
// ==========================================================

// MAX6675
#define MAX6675_SCK 18
#define MAX6675_CS  19
#define MAX6675_SO  5

// LCD I2C
#define LCD_SDA 22
#define LCD_SCL 21

// Buttons
#define BUTTON_UP     32
#define BUTTON_DOWN   33
#define BUTTON_SELECT 25

// Relay
#define RELAY_PIN 13


// ==========================================================
// BLYNK VIRTUAL PINS
// ==========================================================

#define VPIN_ONLINE_MODE   V0
#define VPIN_TARGET_TEMP   V1
#define VPIN_CURRENT_TEMP  V2
#define VPIN_MANUAL_RELAY  V3
#define VPIN_RELAY_STATUS  V4
#define VPIN_MODE_STATUS   V5


// ==========================================================
// SETTINGS
// ==========================================================

float setTemperature = 100.0;

const float HYSTERESIS = 2.0;

const float MIN_TEMP = 0.0;
const float MAX_TEMP = 500.0;

const float TEMP_STEP = 1.0;


// ==========================================================
// RELAY
// ==========================================================

// true:
// LOW  = ON
// HIGH = OFF
//
// If your relay behaves opposite, change to false.

#define RELAY_ACTIVE_LOW true


// ==========================================================
// OBJECTS
// ==========================================================

MAX6675 thermocouple(
  MAX6675_SCK,
  MAX6675_CS,
  MAX6675_SO
);

LiquidCrystal_I2C lcd(
  0x27,
  20,
  4
);

BlynkTimer timer;


// ==========================================================
// VARIABLES
// ==========================================================

float currentTemperature = NAN;

bool relayState = false;


// ==========================================================
// OPERATING MODE
// ==========================================================

bool onlineMode = false;


// ==========================================================
// MANUAL RELAY
// ==========================================================

// true = manual relay control active
// false = automatic temperature control

bool manualRelayMode = false;

bool manualRelayState = false;


// ==========================================================
// MENU
// ==========================================================

enum MenuState {
  HOME,
  SETTING
};

MenuState menuState = HOME;


// ==========================================================
// TIMING
// ==========================================================

unsigned long lastTemperatureRead = 0;
unsigned long lastLCDUpdate = 0;
unsigned long lastSerialPrint = 0;

const unsigned long TEMP_INTERVAL = 500;
const unsigned long LCD_INTERVAL = 250;
const unsigned long SERIAL_INTERVAL = 1000;


// ==========================================================
// BUTTON STRUCTURE
// ==========================================================

struct Button {

  uint8_t pin;

  bool stableState;
  bool lastReading;

  unsigned long lastDebounceTime;

  unsigned long pressStartTime;

  bool longPressHandled;
};


Button buttonUp = {
  BUTTON_UP,
  HIGH,
  HIGH,
  0,
  0,
  false
};


Button buttonDown = {
  BUTTON_DOWN,
  HIGH,
  HIGH,
  0,
  0,
  false
};


Button buttonSelect = {
  BUTTON_SELECT,
  HIGH,
  HIGH,
  0,
  0,
  false
};


// ==========================================================
// BUTTON SETTINGS
// ==========================================================

const unsigned long DEBOUNCE_TIME = 40;

const unsigned long LONG_PRESS_TIME = 1000;


// ==========================================================
// RELAY CONTROL
// ==========================================================

void setRelay(bool state) {

  relayState = state;

  if (RELAY_ACTIVE_LOW) {

    digitalWrite(
      RELAY_PIN,
      relayState ? LOW : HIGH
    );

  }
  else {

    digitalWrite(
      RELAY_PIN,
      relayState ? HIGH : LOW
    );
  }

  Serial.print("[RELAY] ");

  if (relayState) {
    Serial.println("ON");
  }
  else {
    Serial.println("OFF");
  }

  // Update Blynk relay status
  if (Blynk.connected()) {

    Blynk.virtualWrite(
      VPIN_RELAY_STATUS,
      relayState ? 1 : 0
    );
  }
}


void relayOn() {

  if (!relayState) {
    setRelay(true);
  }
}


void relayOff() {

  if (relayState) {
    setRelay(false);
  }
}


// ==========================================================
// TEMPERATURE READING
// ==========================================================

void readTemperature() {

  if (
    millis() - lastTemperatureRead >= TEMP_INTERVAL
  ) {

    lastTemperatureRead = millis();

    float newTemperature =
      thermocouple.readCelsius();


    if (!isnan(newTemperature)) {

      currentTemperature =
        newTemperature;
    }

    else {

      Serial.println(
        "[ERROR] MAX6675 sensor error"
      );

      // Safety
      relayOff();
    }
  }
}


// ==========================================================
// AUTOMATIC TEMPERATURE CONTROL
// ==========================================================

void controlTemperature() {

  // --------------------------------------------------------
  // SENSOR ERROR PROTECTION
  // --------------------------------------------------------

  if (isnan(currentTemperature)) {

    relayOff();

    return;
  }


  // --------------------------------------------------------
  // MANUAL RELAY MODE
  // --------------------------------------------------------

  if (manualRelayMode) {

    if (manualRelayState) {
      relayOn();
    }
    else {
      relayOff();
    }

    return;
  }


  // --------------------------------------------------------
  // AUTOMATIC TEMPERATURE CONTROL
  // --------------------------------------------------------

  // Turn heater ON

  if (
    currentTemperature >= setTemperature
    &&
    !relayState
  ) {

    relayOn();

    Serial.println(
      "[CONTROL] Temperature reached threshold"
    );
  }


  // Turn heater OFF

  if (
    currentTemperature <=
    (setTemperature - HYSTERESIS)
    &&
    relayState
  ) {

    relayOff();

    Serial.println(
      "[CONTROL] Temperature below hysteresis"
    );
  }
}


// ==========================================================
// BUTTON READING
// ==========================================================

bool buttonPressed(Button &button) {

  bool reading =
    digitalRead(button.pin);


  if (
    reading != button.lastReading
  ) {

    button.lastDebounceTime =
      millis();
  }


  if (
    millis() - button.lastDebounceTime
    > DEBOUNCE_TIME
  ) {

    if (
      reading != button.stableState
    ) {

      button.stableState =
        reading;


      if (
        button.stableState == LOW
      ) {

        return true;
      }
    }
  }


  button.lastReading =
    reading;

  return false;
}


// ==========================================================
// SELECT BUTTON
// ==========================================================

void handleSelectButton() {

  bool reading =
    digitalRead(BUTTON_SELECT);


  // Button just pressed

  if (
    reading == LOW
    &&
    buttonSelect.stableState == HIGH
  ) {

    if (
      millis() -
      buttonSelect.lastDebounceTime
      > DEBOUNCE_TIME
    ) {

      buttonSelect.pressStartTime =
        millis();

      buttonSelect.longPressHandled =
        false;
    }
  }


  // Button is being held

  if (
    reading == LOW
    &&
    buttonSelect.pressStartTime > 0
  ) {

    unsigned long pressTime =
      millis() -
      buttonSelect.pressStartTime;


    // Long press

    if (
      pressTime >= LONG_PRESS_TIME
      &&
      !buttonSelect.longPressHandled
    ) {

      buttonSelect.longPressHandled =
        true;

      Serial.println(
        "[BUTTON 3] LONG PRESS"
      );

      menuState = HOME;
    }
  }


  // Button released

  if (
    reading == HIGH
    &&
    buttonSelect.stableState == LOW
  ) {

    if (
      millis() -
      buttonSelect.lastDebounceTime
      > DEBOUNCE_TIME
    ) {

      unsigned long pressTime =
        millis() -
        buttonSelect.pressStartTime;


      // Short press

      if (
        pressTime < LONG_PRESS_TIME
        &&
        !buttonSelect.longPressHandled
      ) {

        Serial.println(
          "[BUTTON 3] SHORT PRESS"
        );


        if (menuState == HOME) {

          menuState = SETTING;

          Serial.println(
            "[MENU] Enter SETTING"
          );
        }

        else {

          menuState = HOME;

          Serial.println(
            "[MENU] SETPOINT CONFIRMED"
          );
        }
      }


      buttonSelect.pressStartTime =
        0;
    }
  }


  buttonSelect.lastReading =
    reading;

  buttonSelect.stableState =
    reading;
}


// ==========================================================
// BUTTON MENU
// ==========================================================

void handleButtons() {

  handleSelectButton();


  // --------------------------------------------------------
  // UP
  // --------------------------------------------------------

  if (
    menuState == SETTING
    &&
    buttonPressed(buttonUp)
  ) {

    setTemperature +=
      TEMP_STEP;


    if (
      setTemperature > MAX_TEMP
    ) {

      setTemperature =
        MAX_TEMP;
    }


    Serial.print(
      "[SETTING] Set temperature: "
    );

    Serial.print(
      setTemperature,
      1
    );

    Serial.println(" C");


    // Update Blynk

    if (
      onlineMode
      &&
      Blynk.connected()
    ) {

      Blynk.virtualWrite(
        VPIN_TARGET_TEMP,
        setTemperature
      );
    }
  }


  // --------------------------------------------------------
  // DOWN
  // --------------------------------------------------------

  if (
    menuState == SETTING
    &&
    buttonPressed(buttonDown)
  ) {

    setTemperature -=
      TEMP_STEP;


    if (
      setTemperature < MIN_TEMP
    ) {

      setTemperature =
        MIN_TEMP;
    }


    Serial.print(
      "[SETTING] Set temperature: "
    );

    Serial.print(
      setTemperature,
      1
    );

    Serial.println(" C");


    // Update Blynk

    if (
      onlineMode
      &&
      Blynk.connected()
    ) {

      Blynk.virtualWrite(
        VPIN_TARGET_TEMP,
        setTemperature
      );
    }
  }
}


// ==========================================================
// LCD HOME SCREEN
// ==========================================================

void displayHome() {

  lcd.setCursor(0, 0);

  if (onlineMode) {
    lcd.print("ONLINE MODE        ");
  }
  else {
    lcd.print("OFFLINE MODE       ");
  }


  // --------------------------------------------------------
  // Current temperature
  // --------------------------------------------------------

  lcd.setCursor(0, 1);

  lcd.print("Current: ");


  if (isnan(currentTemperature)) {

    lcd.print("ERROR       ");
  }

  else {

    lcd.print(
      currentTemperature,
      1
    );

    lcd.print((char)223);
    lcd.print("C       ");
  }


  // --------------------------------------------------------
  // Set temperature
  // --------------------------------------------------------

  lcd.setCursor(0, 2);

  lcd.print("Set:     ");

  lcd.print(
    setTemperature,
    1
  );

  lcd.print((char)223);
  lcd.print("C       ");


  // --------------------------------------------------------
  // Relay
  // --------------------------------------------------------

  lcd.setCursor(0, 3);

  lcd.print("Relay: ");

  if (relayState) {
    lcd.print("ON ");
  }
  else {
    lcd.print("OFF");
  }


  if (manualRelayMode) {

    lcd.print(" MANUAL");

  }

  else {

    lcd.print(" AUTO  ");
  }
}


// ==========================================================
// LCD SETTING SCREEN
// ==========================================================

void displaySetting() {

  lcd.setCursor(0, 0);

  lcd.print("SET TEMPERATURE    ");


  lcd.setCursor(0, 1);

  lcd.print("Set: ");

  lcd.print(
    setTemperature,
    1
  );

  lcd.print((char)223);
  lcd.print("C            ");


  lcd.setCursor(0, 2);

  lcd.print("B1: +1 C          ");


  lcd.setCursor(0, 3);

  lcd.print("B2:-1 B3:OK/HOLD  ");
}


// ==========================================================
// UPDATE LCD
// ==========================================================

void updateLCD() {

  if (
    millis() - lastLCDUpdate
    < LCD_INTERVAL
  ) {

    return;
  }


  lastLCDUpdate =
    millis();


  if (menuState == HOME) {

    displayHome();
  }

  else {

    displaySetting();
  }
}


// ==========================================================
// BLYNK - ONLINE/OFFLINE
// ==========================================================

BLYNK_WRITE(VPIN_ONLINE_MODE) {

  int value = param.asInt();

  onlineMode = (value == 1);


  Serial.println();

  if (onlineMode) {

    Serial.println(
      "[BLYNK] ONLINE MODE"
    );

    // Send current values to Blynk

    Blynk.virtualWrite(
      VPIN_TARGET_TEMP,
      setTemperature
    );

    if (!isnan(currentTemperature)) {

      Blynk.virtualWrite(
        VPIN_CURRENT_TEMP,
        currentTemperature
      );
    }

    Blynk.virtualWrite(
      VPIN_RELAY_STATUS,
      relayState ? 1 : 0
    );

    Blynk.virtualWrite(
      VPIN_MODE_STATUS,
      "ONLINE"
    );
  }

  else {

    Serial.println(
      "[BLYNK] OFFLINE MODE"
    );

    // When leaving online mode,
    // cancel manual relay control.

    manualRelayMode = false;
    manualRelayState = false;

    Blynk.virtualWrite(
      VPIN_MANUAL_RELAY,
      0
    );

    Blynk.virtualWrite(
      VPIN_MODE_STATUS,
      "OFFLINE"
    );
  }
}


// ==========================================================
// BLYNK - TARGET TEMPERATURE
// ==========================================================

BLYNK_WRITE(VPIN_TARGET_TEMP) {

  if (!onlineMode) {

    Serial.println(
      "[BLYNK] Target ignored - OFFLINE"
    );

    return;
  }


  float newSetTemperature =
    param.asFloat();


  if (
    newSetTemperature < MIN_TEMP
  ) {

    newSetTemperature =
      MIN_TEMP;
  }


  if (
    newSetTemperature > MAX_TEMP
  ) {

    newSetTemperature =
      MAX_TEMP;
  }


  setTemperature =
    newSetTemperature;


  Serial.print(
    "[BLYNK] Target temperature: "
  );

  Serial.print(
    setTemperature,
    1
  );

  Serial.println(" C");
}


// ==========================================================
// BLYNK - MANUAL RELAY
// ==========================================================

BLYNK_WRITE(VPIN_MANUAL_RELAY) {

  if (!onlineMode) {

    Serial.println(
      "[BLYNK] Manual relay ignored - OFFLINE"
    );

    return;
  }


  int value =
    param.asInt();


  if (value == 1) {

    manualRelayMode = true;
    manualRelayState = true;

    Serial.println(
      "[BLYNK] MANUAL RELAY ON"
    );

  }

  else {

    manualRelayMode = false;
    manualRelayState = false;

    Serial.println(
      "[BLYNK] AUTO TEMPERATURE CONTROL"
    );
  }
}


// ==========================================================
// SEND DATA TO BLYNK
// ==========================================================

void sendDataToBlynk() {

  if (!Blynk.connected()) {
    return;
  }


  // Current temperature

  if (!isnan(currentTemperature)) {

    Blynk.virtualWrite(
      VPIN_CURRENT_TEMP,
      currentTemperature
    );
  }


  // Target temperature

  Blynk.virtualWrite(
    VPIN_TARGET_TEMP,
    setTemperature
  );


  // Relay status

  Blynk.virtualWrite(
    VPIN_RELAY_STATUS,
    relayState ? 1 : 0
  );


  // Mode

  if (onlineMode) {

    Blynk.virtualWrite(
      VPIN_MODE_STATUS,
      "ONLINE"
    );
  }

  else {

    Blynk.virtualWrite(
      VPIN_MODE_STATUS,
      "OFFLINE"
    );
  }
}


// ==========================================================
// SERIAL STATUS
// ==========================================================

void printStatus() {

  Serial.println();
  Serial.println(
    "=============================="
  );


  Serial.print(
    "Current temperature : "
  );

  Serial.print(
    currentTemperature,
    2
  );

  Serial.println(" C");


  Serial.print(
    "Set temperature     : "
  );

  Serial.print(
    setTemperature,
    1
  );

  Serial.println(" C");


  Serial.print(
    "Relay               : "
  );

  Serial.println(
    relayState
      ? "ON"
      : "OFF"
  );


  Serial.print(
    "Mode                : "
  );

  Serial.println(
    onlineMode
      ? "ONLINE"
      : "OFFLINE"
  );


  Serial.print(
    "Manual relay mode   : "
  );

  Serial.println(
    manualRelayMode
      ? "YES"
      : "NO"
  );


  Serial.print(
    "Blynk connected     : "
  );

  Serial.println(
    Blynk.connected()
      ? "YES"
      : "NO"
  );


  Serial.println(
    "=============================="
  );
}


// ==========================================================
// SERIAL COMMANDS
// ==========================================================

void handleSerial() {

  if (!Serial.available()) {
    return;
  }


  String command =
    Serial.readStringUntil('\n');

  command.trim();

  command.toUpperCase();


  if (command == "STATUS") {

    printStatus();

    return;
  }


  if (command == "ON") {

    manualRelayMode = true;
    manualRelayState = true;

    relayOn();

    Serial.println(
      "[MANUAL] Relay ON"
    );

    return;
  }


  if (command == "OFF") {

    manualRelayMode = true;
    manualRelayState = false;

    relayOff();

    Serial.println(
      "[MANUAL] Relay OFF"
    );

    return;
  }


  if (command == "AUTO") {

    manualRelayMode = false;

    Serial.println(
      "[CONTROL] AUTO MODE"
    );

    return;
  }


  if (command == "HOME") {

    menuState = HOME;

    Serial.println(
      "[MENU] HOME"
    );

    return;
  }


  Serial.println("Commands:");
  Serial.println("STATUS");
  Serial.println("ON");
  Serial.println("OFF");
  Serial.println("AUTO");
  Serial.println("HOME");
}


// ==========================================================
// PERIODIC SERIAL OUTPUT
// ==========================================================

void periodicSerialOutput() {

  if (
    millis() - lastSerialPrint
    >= SERIAL_INTERVAL
  ) {

    lastSerialPrint =
      millis();


    Serial.print("[TEMP] ");

    Serial.print(
      currentTemperature,
      2
    );

    Serial.print(" C | SET: ");

    Serial.print(
      setTemperature,
      1
    );

    Serial.print(" C | RELAY: ");

    Serial.print(
      relayState
        ? "ON"
        : "OFF"
    );

    Serial.print(" | MODE: ");

    Serial.println(
      onlineMode
        ? "ONLINE"
        : "OFFLINE"
    );
  }
}


// ==========================================================
// WIFI / BLYNK CONNECTION
// ==========================================================

void connectBlynk() {

  Serial.println();
  Serial.println(
    "[WIFI] Connecting..."
  );

  Blynk.begin(
    BLYNK_AUTH_TOKEN,
    ssid,
    pass
  );

  Serial.println(
    "[BLYNK] Connected"
  );
}


// ==========================================================
// SETUP
// ==========================================================

void setup() {

  Serial.begin(115200);

  delay(500);


  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    " ESP32 Temperature Controller"
  );

  Serial.println(
    " Blynk IoT Version"
  );

  Serial.println(
    "================================"
  );


  // --------------------------------------------------------
  // Buttons
  // --------------------------------------------------------

  pinMode(
    BUTTON_UP,
    INPUT_PULLUP
  );

  pinMode(
    BUTTON_DOWN,
    INPUT_PULLUP
  );

  pinMode(
    BUTTON_SELECT,
    INPUT_PULLUP
  );


  // --------------------------------------------------------
  // Relay
  // --------------------------------------------------------

  pinMode(
    RELAY_PIN,
    OUTPUT
  );


  // IMPORTANT:
  // Start with relay OFF

  setRelay(false);


  // --------------------------------------------------------
  // I2C
  // --------------------------------------------------------

  Wire.begin(
    LCD_SDA,
    LCD_SCL
  );


  // --------------------------------------------------------
  // LCD
  // --------------------------------------------------------

  lcd.init();

  lcd.backlight();

  lcd.clear();


  lcd.setCursor(0, 0);

  lcd.print(
    "TEMP CONTROLLER"
  );


  lcd.setCursor(0, 1);

  lcd.print(
    "Connecting WiFi..."
  );


  delay(1000);


  // --------------------------------------------------------
  // BLYNK
  // --------------------------------------------------------

  connectBlynk();


  // --------------------------------------------------------
  // BLYNK TIMER
  // --------------------------------------------------------

  timer.setInterval(
    1000L,
    sendDataToBlynk
  );


  lcd.clear();


  Serial.println();
  Serial.println(
    "Default set temperature: 100 C"
  );

  Serial.println(
    "Button 1: Increase"
  );

  Serial.println(
    "Button 2: Decrease"
  );

  Serial.println(
    "Button 3: Enter / Confirm"
  );

  Serial.println(
    "Hold Button 3: HOME"
  );

  Serial.println();

  Serial.println(
    "System Ready"
  );
}


// ==========================================================
// LOOP
// ==========================================================

void loop() {

  // --------------------------------------------------------
  // BLYNK
  // --------------------------------------------------------

  Blynk.run();

  timer.run();


  // --------------------------------------------------------
  // Temperature
  // --------------------------------------------------------

  readTemperature();


  // --------------------------------------------------------
  // Buttons
  // --------------------------------------------------------

  handleButtons();


  // --------------------------------------------------------
  // Temperature control
  // --------------------------------------------------------

  controlTemperature();


  // --------------------------------------------------------
  // LCD
  // --------------------------------------------------------

  updateLCD();


  // --------------------------------------------------------
  // Serial
  // --------------------------------------------------------

  handleSerial();

  periodicSerialOutput();
}
```
