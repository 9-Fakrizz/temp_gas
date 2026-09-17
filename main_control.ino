#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <max6675.h>

// =====================================================
// PIN CONFIGURATION
// =====================================================

// MAX6675
#define MAX6675_SCK 18
#define MAX6675_CS  19
#define MAX6675_SO  5

// LCD I2C
#define LCD_SDA 21
#define LCD_SCL 22

// Buttons
#define BUTTON_UP     32
#define BUTTON_DOWN   33
#define BUTTON_SELECT 25

// Relay
#define RELAY_PIN 13


// =====================================================
// SETTINGS
// =====================================================

float setTemperature = 100.0;

// Temperature hysteresis
// Relay ON  : temperature >= setTemperature
// Relay OFF : temperature <= setTemperature - HYSTERESIS
const float HYSTERESIS = 2.0;

// Temperature setting limits
const float MIN_TEMP = 0.0;
const float MAX_TEMP = 500.0;

// Temperature adjustment step
const float TEMP_STEP = 1.0;


// =====================================================
// RELAY
// =====================================================

// Most relay modules are ACTIVE LOW.
//
// true:
// LOW  = ON
// HIGH = OFF
//
// If your relay behaves opposite, change to false.
#define RELAY_ACTIVE_LOW true


// =====================================================
// OBJECTS
// =====================================================

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


// =====================================================
// VARIABLES
// =====================================================

float currentTemperature = 0.0;

bool relayState = false;

// Menu state
enum MenuState {
  HOME,
  SETTING
};

MenuState menuState = HOME;


// =====================================================
// TIMING
// =====================================================

unsigned long lastTemperatureRead = 0;
unsigned long lastLCDUpdate = 0;
unsigned long lastSerialPrint = 0;

const unsigned long TEMP_INTERVAL = 500;
const unsigned long LCD_INTERVAL = 250;
const unsigned long SERIAL_INTERVAL = 1000;


// =====================================================
// BUTTON STRUCTURE
// =====================================================

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


// =====================================================
// BUTTON SETTINGS
// =====================================================

const unsigned long DEBOUNCE_TIME = 40;

// Hold SELECT for this long -> Home
const unsigned long LONG_PRESS_TIME = 1000;


// =====================================================
// RELAY CONTROL
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


// =====================================================

void relayOn() {

  if (!relayState) {

    setRelay(true);
  }
}


// =====================================================

void relayOff() {

  if (relayState) {

    setRelay(false);
  }
}


// =====================================================
// TEMPERATURE READING
// =====================================================

void readTemperature() {

  if (
    millis() - lastTemperatureRead
    >= TEMP_INTERVAL
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
    }
  }
}


// =====================================================
// AUTOMATIC TEMPERATURE CONTROL
// =====================================================

void controlTemperature() {

  // Sensor error protection
  if (isnan(currentTemperature)) {

    relayOff();

    return;
  }


  // ---------------------------------------------------
  // Turn heater ON
  // ---------------------------------------------------

  if (
    currentTemperature >= setTemperature
    && !relayState
  ) {

    relayOn();

    Serial.println(
      "[CONTROL] Temperature reached threshold"
    );
  }


  // ---------------------------------------------------
  // Turn heater OFF
  // ---------------------------------------------------

  if (
    currentTemperature <=
    (setTemperature - HYSTERESIS)
    && relayState
  ) {

    relayOff();

    Serial.println(
      "[CONTROL] Temperature below hysteresis"
    );
  }
}


// =====================================================
// BUTTON READING
// =====================================================

bool buttonPressed(Button &button) {

  bool reading =
    digitalRead(button.pin);


  // Detect change
  if (
    reading != button.lastReading
  ) {

    button.lastDebounceTime =
      millis();
  }


  // Stable state
  if (
    millis() - button.lastDebounceTime
    > DEBOUNCE_TIME
  ) {

    if (
      reading != button.stableState
    ) {

      button.stableState =
        reading;

      // Button pressed
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


// =====================================================
// SELECT BUTTON
// =====================================================

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

      Serial.println(
        "[MENU] Returning to HOME"
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


// =====================================================
// BUTTON MENU
// =====================================================

void handleButtons() {

  // ---------------------------------------------------
  // SELECT
  // ---------------------------------------------------

  handleSelectButton();


  // ---------------------------------------------------
  // UP
  // ---------------------------------------------------

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
  }


  // ---------------------------------------------------
  // DOWN
  // ---------------------------------------------------

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
  }
}


// =====================================================
// LCD HOME SCREEN
// =====================================================

void displayHome() {

  lcd.setCursor(0, 0);
  lcd.print("TEMP CONTROLLER    ");


  // Current temperature
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


  // Set temperature
  lcd.setCursor(0, 2);

  lcd.print("Set:     ");

  lcd.print(
    setTemperature,
    1
  );

  lcd.print((char)223);
  lcd.print("C       ");


  // Relay
  lcd.setCursor(0, 3);

  lcd.print("Relay: ");

  if (relayState) {

    lcd.print("ON ");
  }

  else {

    lcd.print("OFF");
  }

  lcd.print("   B3=SET       ");
}


// =====================================================
// LCD SETTING SCREEN
// =====================================================

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


  lastLCDUpdate =
    millis();


  if (menuState == HOME) {

    displayHome();
  }

  else {

    displaySetting();
  }
}


// =====================================================
// SERIAL STATUS
// =====================================================

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
    "Menu                : "
  );

  if (menuState == HOME) {

    Serial.println("HOME");
  }

  else {

    Serial.println("SETTING");
  }


  Serial.println(
    "=============================="
  );
}


// =====================================================
// SERIAL COMMANDS
// =====================================================

void handleSerial() {

  if (!Serial.available()) {

    return;
  }


  String command =
    Serial.readStringUntil('\n');

  command.trim();

  command.toUpperCase();


  // ---------------------------------------------------

  if (command == "STATUS") {

    printStatus();

    return;
  }


  // ---------------------------------------------------

  if (command == "ON") {

    // Manual override
    relayOn();

    Serial.println(
      "[MANUAL] Relay ON"
    );

    return;
  }


  // ---------------------------------------------------

  if (command == "OFF") {

    // Manual override
    relayOff();

    Serial.println(
      "[MANUAL] Relay OFF"
    );

    return;
  }


  // ---------------------------------------------------

  if (command == "HOME") {

    menuState = HOME;

    Serial.println(
      "[MENU] HOME"
    );

    return;
  }


  // ---------------------------------------------------

  Serial.println(
    "Commands:"
  );

  Serial.println(
    "STATUS"
  );

  Serial.println(
    "ON"
  );

  Serial.println(
    "OFF"
  );

  Serial.println(
    "HOME"
  );
}


// =====================================================
// PERIODIC SERIAL OUTPUT
// =====================================================

void periodicSerialOutput() {

  if (
    millis() - lastSerialPrint
    >= SERIAL_INTERVAL
  ) {

    lastSerialPrint =
      millis();


    Serial.print(
      "[TEMP] "
    );

    Serial.print(
      currentTemperature,
      2
    );

    Serial.print(
      " C | SET: "
    );

    Serial.print(
      setTemperature,
      1
    );

    Serial.print(
      " C | RELAY: "
    );

    Serial.println(
      relayState
        ? "ON"
        : "OFF"
    );
  }
}


// =====================================================
// SETUP
// =====================================================

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
    "================================"
  );


  // ---------------------------------------------------
  // Buttons
  // ---------------------------------------------------

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


  // ---------------------------------------------------
  // Relay
  // ---------------------------------------------------

  pinMode(
    RELAY_PIN,
    OUTPUT
  );


  // IMPORTANT:
  // Start with relay OFF
  setRelay(false);


  // ---------------------------------------------------
  // I2C
  // ---------------------------------------------------

  Wire.begin(
    LCD_SDA,
    LCD_SCL
  );


  // ---------------------------------------------------
  // LCD
  // ---------------------------------------------------

  lcd.init();

  lcd.backlight();

  lcd.clear();


  lcd.setCursor(0, 0);

  lcd.print(
    "TEMP CONTROLLER"
  );


  lcd.setCursor(0, 1);

  lcd.print(
    "Starting..."
  );


  delay(1000);


  lcd.clear();


  // ---------------------------------------------------
  // Startup information
  // ---------------------------------------------------

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


// =====================================================
// LOOP
// =====================================================

void loop() {

  // 1. Read temperature
  readTemperature();


  // 2. Handle buttons
  handleButtons();


  // 3. Temperature control
  controlTemperature();


  // 4. Update LCD
  updateLCD();


  // 5. Serial commands
  handleSerial();


  // 6. Serial monitoring
  periodicSerialOutput();
}
