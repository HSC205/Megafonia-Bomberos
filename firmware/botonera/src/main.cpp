#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

constexpr uint8_t PIN_BOTON = 27;

U8G2_SH1106_128X64_NONAME_F_HW_I2C pantalla(
    U8G2_R0, U8X8_PIN_NONE, 22, 21
);

bool lecturaAnterior = HIGH;
bool estadoEstable = HIGH;
unsigned long ultimoCambio = 0;
unsigned int pulsaciones = 0;

void mostrarPantalla() {
  pantalla.clearBuffer();
  pantalla.setFont(u8g2_font_6x12_tr);
  pantalla.drawStr(10, 14, "PRUEBA DE PULSADOR");

  pantalla.setCursor(10, 34);
  pantalla.print("Pulsaciones: ");
  pantalla.print(pulsaciones);

  pantalla.drawStr(
      10, 54,
      estadoEstable == LOW ? "PULSADO" : "SUELTO"
  );

  pantalla.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOTON, INPUT_PULLUP);

  pantalla.setI2CAddress(0x3C * 2);
  pantalla.begin();
  mostrarPantalla();
}

void loop() {
  const unsigned long ahora = millis();
  const bool lectura = digitalRead(PIN_BOTON);

  if (lectura != lecturaAnterior) {
    ultimoCambio = ahora;
    lecturaAnterior = lectura;
  }

  if (ahora - ultimoCambio >= 30 && lectura != estadoEstable) {
    estadoEstable = lectura;

    if (estadoEstable == LOW) {
      pulsaciones++;
      Serial.printf("Pulsacion: %u\n", pulsaciones);
    }

    mostrarPantalla();
  }
}