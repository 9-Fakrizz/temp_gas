#include <Arduino.h>
#include <max6675.h>

#define SCK_PIN 18
#define CS_PIN  19
#define SO_PIN  5

MAX6675 thermocouple(
  SCK_PIN,
  CS_PIN,
  SO_PIN
);

void setup() {
  Serial.begin(115200);

  delay(1000);

  Serial.println("MAX6675 TEST");
}

void loop() {

  float temp = thermocouple.readCelsius();

  Serial.print("Temperature = ");

  if (isnan(temp)) {
    Serial.println("NaN / SENSOR ERROR");
  }
  else {
    Serial.print(temp);
    Serial.println(" C");
  }

  delay(1000);
}
