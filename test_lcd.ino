#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <max6675.h>

// =====================================================
// PIN CONFIGURATION
// =====================================================

// MAX6675
#define MAX6675_SCK 18
#define MAX6675_CS   5
#define MAX6675_SO  19

// I2C LCD
#define I2C_SDA 22
#define I2C_SCL 21

// Buttons (button -> GPIO and GND)
#define BUTTON_1 32
#define BUTTON_2 33
#define BUTTON_3 25

// Relay
#define RELAY_PIN 13

// =====================================================
// RELAY CONFIGURATION
// =====================================================

// Most relay modules are ACTIVE LOW.
//
// ACTIVE LOW:
// GPIO LOW  = Relay ON
// GPIO HIGH = Relay OFF
//
// If your relay works opposite, change this to false.
#define RELAY_ACTIVE_LOW true

// =====================================================
// LCD
// =====================================================

// Common I2C address:
// 0x27 or 0x3F
LiquidCrystal_I2C lcd(0x27, 20, 4);

// =====================================================
// MAX6675
// =====================================================

MAX6675 thermocouple(
  MAX6675_SCK,
  MAX6675_CS,
  MAX6675_SO
);

// =====================================================
// VARIABLES
// =====================================================

bool relayState = false;

// Temperature
float temperatureC = 0.0;

// Update timers
unsigned long lastTemperatureRead = 0;
unsigned long lastSerialPrint = 0;
unsigned long lastLCDUpdate = 0;

const unsigned long TEMPERATURE_INTERVAL = 500;
const unsigned long SERIAL_INTERVAL      = 1000;
const unsigned long LCD_INTERVAL         = 500;

// =====================================================
// BUTTON DEBOUNCE
// =====================================================

struct Button {
  uint8_t pin;

  bool currentState;
  bool lastReading;

  unsigned long lastDebounceTime;
};

Button button1 = {
  BUTTON_1,
  HIGH,
  HIGH,
  0
};

Button button2 = {
  BUTTON_2,
  HIGH,
  HIGH,
  0
};

Button button3 = {
  BUTTON_3,
  HIGH,
  HIGH,
  0
};

const unsigned long DEBOUNCE_DELAY = 50;

// =====================================================
// RELAY FUNCTIONS
// =====================================================

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
}

// -----------------------------------------------------

void relayOn() {
  setRelay(true);
}

// -----------------------------------------------------

void relayOff() {
  setRelay(false);
}

// -----------------------------------------------------

void toggleRelay() {
  setRelay(!relayState);
}

// =====================================================
// BUTTON FUNCTION
// =====================================================

bool buttonPressed(Button &button) {

  bool reading = digitalRead(button.pin);

  // Input changed
  if (reading != button.lastReading) {

    button.lastDebounceTime = millis();

  }

  // Stable longer than debounce delay
  if (
    (millis() - button.lastDebounceTime)
    > DEBOUNCE_DELAY
  ) {

    if (reading != button.currentState) {

      button.currentState = reading;

      // INPUT_PULLUP:
      // Pressed = LOW
      if (button.currentState == LOW) {

        button.lastReading = reading;

        return true;

      }
    }
  }

  button.lastReading = reading;

  return false;
}

// =====================================================
// READ BUTTONS
// =====================================================

void handleButtons() {

  // ---------------------------------------------------
  // BUTTON 1
  // Relay ON
  // ---------------------------------------------------

  if (buttonPressed(button1)) {

    Serial.println();
    Serial.println("[BUTTON 1] PRESSED");
    Serial.println("Action: Relay ON");

    relayOn();
  }

  // ---------------------------------------------------
  // BUTTON 2
  // Relay OFF
  // ---------------------------------------------------

  if (buttonPressed(button2)) {

    Serial.println();
    Serial.println("[BUTTON 2] PRESSED");
    Serial.println("Action: Relay OFF");

    relayOff();
  }

  // ---------------------------------------------------
  // BUTTON 3
  // Toggle Relay
  // ---------------------------------------------------

  if (buttonPressed(button3)) {

    Serial.println();
    Serial.println("[BUTTON 3] PRESSED");
    Serial.println("Action: Toggle Relay");

    toggleRelay();
  }
}

// =====================================================
// READ TEMPERATURE
// =====================================================

void readTemperature() {

  if (
    millis() - lastTemperatureRead
    >= TEMPERATURE_INTERVAL
  ) {

    lastTemperatureRead = millis();

    temperatureC =
      thermocouple.readCelsius();
  }
}

// =====================================================
// SERIAL TEMPERATURE OUTPUT
// =====================================================

void printStatus() {

  Serial.println("--------------------------------");

  Serial.print("Temperature : ");

  if (isnan(temperatureC)) {
    Serial.println("ERROR");
  }
  else {
    Serial.print(temperatureC, 2);
    Serial.println(" C");
  }

  Serial.print("Relay       : ");

  if (relayState) {
    Serial.println("ON");
  }
  else {
    Serial.println("OFF");
  }

  Serial.print("Button 1    : ");
  Serial.println(
    digitalRead(BUTTON_1) == LOW
      ? "PRESSED"
      : "RELEASED"
  );

  Serial.print("Button 2    : ");
  Serial.println(
    digitalRead(BUTTON_2) == LOW
      ? "PRESSED"
      : "RELEASED"
  );

  Serial.print("Button 3    : ");
  Serial.println(
    digitalRead(BUTTON_3) == LOW
      ? "PRESSED"
      : "RELEASED"
  );

  Serial.println("--------------------------------");
}

// =====================================================
// UPDATE LCD
// =====================================================

void updateLCD() {

  if (
    millis() - lastLCDUpdate
    < LCD_INTERVAL
  ) {
    return;
  }

  lastLCDUpdate = millis();

  // ---------------------------------------------------
  // Line 1
  // ---------------------------------------------------

  lcd.setCursor(0, 0);

  lcd.print("Temperature:       ");

  // ---------------------------------------------------
  // Line 2
  // ---------------------------------------------------

  lcd.setCursor(0, 1);

  if (isnan(temperatureC)) {

    lcd.print("Sensor ERROR       ");

  }
  else {

    lcd.print(temperatureC, 2);

    lcd.print((char)223);

    lcd.print("C             ");
  }

  // ---------------------------------------------------
  // Line 3
  // ---------------------------------------------------

  lcd.setCursor(0, 2);

  lcd.print("Relay: ");

  if (relayState) {
    lcd.print("ON ");
  }
  else {
    lcd.print("OFF");
  }

  lcd.print("          ");

  // ---------------------------------------------------
  // Line 4
  // ---------------------------------------------------

  lcd.setCursor(0, 3);

  lcd.print("B1:ON B2:OFF B3:TGL");
}

// =====================================================
// SERIAL COMMAND
// =====================================================

void handleSerial() {

  if (!Serial.available()) {
    return;
  }

  String command =
    Serial.readStringUntil('\n');

  command.trim();
  command.toUpperCase();

  Serial.print("[SERIAL COMMAND] ");
  Serial.println(command);

  // ---------------------------------------------------

  if (command == "ON") {

    relayOn();

  }

  // ---------------------------------------------------

  else if (command == "OFF") {

    relayOff();

  }

  // ---------------------------------------------------

  else if (
    command == "TOGGLE"
    || command == "T"
  ) {

    toggleRelay();

  }

  // ---------------------------------------------------

  else if (
    command == "STATUS"
    || command == "S"
  ) {

    printStatus();

  }

  // ---------------------------------------------------

  else {

    Serial.println("Unknown command");

    Serial.println(
      "Commands: ON, OFF, TOGGLE, STATUS"
    );
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  // ---------------------------------------------------
  // Serial
  // ---------------------------------------------------

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" ESP32 Temperature Controller");
  Serial.println("==============================");

  // ---------------------------------------------------
  // Buttons
  // ---------------------------------------------------

  pinMode(
    BUTTON_1,
    INPUT_PULLUP
  );

  pinMode(
    BUTTON_2,
    INPUT_PULLUP
  );

  pinMode(
    BUTTON_3,
    INPUT_PULLUP
  );

  // ---------------------------------------------------
  // Relay
  // ---------------------------------------------------

  pinMode(
    RELAY_PIN,
    OUTPUT
  );

  // Start relay OFF
  relayOff();

  // ---------------------------------------------------
  // I2C
  // ---------------------------------------------------

  Wire.begin(
    I2C_SDA,
    I2C_SCL
  );

  // ---------------------------------------------------
  // LCD
  // ---------------------------------------------------

  lcd.init();

  lcd.backlight();

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print("ESP32 Controller");

  lcd.setCursor(0, 1);

  lcd.print("Initializing...");

  // MAX6675 needs some stabilization time
  delay(1000);

  lcd.clear();

  // ---------------------------------------------------
  // Instructions
  // ---------------------------------------------------

  Serial.println();
  Serial.println("Serial Commands:");
  Serial.println("ON     -> Relay ON");
  Serial.println("OFF    -> Relay OFF");
  Serial.println("TOGGLE -> Toggle Relay");
  Serial.println("STATUS -> Show status");
  Serial.println();

  Serial.println("Buttons:");
  Serial.println("B1 -> Relay ON");
  Serial.println("B2 -> Relay OFF");
  Serial.println("B3 -> Toggle Relay");
  Serial.println();

  Serial.println("System Ready");
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // Read thermocouple
  readTemperature();

  // Read buttons
  handleButtons();

  // Serial commands
  handleSerial();

  // Update LCD
  updateLCD();

  // Periodic Serial Monitor output
  if (
    millis() - lastSerialPrint
    >= SERIAL_INTERVAL
  ) {

    lastSerialPrint = millis();

    Serial.print("[TEMP] ");

    if (isnan(temperatureC)) {

      Serial.print("ERROR");

    }
    else {

      Serial.print(temperatureC, 2);
      Serial.print(" C");
    }

    Serial.print(" | Relay: ");

    Serial.println(
      relayState ? "ON" : "OFF"
    );
  }
}
