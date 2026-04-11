#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  Serial.println("--- BOOT START ---");
  pinMode(33, OUTPUT);
}

void loop() {
  static bool state = false;
  digitalWrite(33, state);
  state = !state;
  Serial.println("Tick");
  delay(500);
}
