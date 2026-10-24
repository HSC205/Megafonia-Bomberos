#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C pantalla(
    U8G2_R0, U8X8_PIN_NONE, 22, 21
);

void setup() {
  Serial.begin(115200);

  pantalla.setI2CAddress(0x3C * 2);
  pantalla.begin();

  pantalla.clearBuffer();
  pantalla.setFont(u8g2_font_6x12_tr);
  pantalla.drawStr(10, 16, "MEGAFONIA BOMBEROS");
  pantalla.drawStr(10, 36, "Prueba de pantalla");
  pantalla.drawStr(10, 56, "Botonera iniciada");
  pantalla.sendBuffer();
}

void loop() {
}