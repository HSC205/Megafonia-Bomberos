#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Botonera iniciada");
}

void loop() {
  Serial.println("ESP32 funcionando");
  delay(2000);
}