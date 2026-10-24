#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

constexpr unsigned long ANTIRREBOTE_MS = 30;
constexpr unsigned long PULSACION_LARGA_MS = 5000;

enum Evento { NINGUNO, CORTA, LARGA };

class Boton {
 public:
  Boton(uint8_t pin, bool permiteLarga)
      : pin_(pin), permiteLarga_(permiteLarga) {}

  void iniciar() {
    pinMode(pin_, INPUT_PULLUP);
  }

  Evento actualizar(unsigned long ahora) {
    bool lectura = digitalRead(pin_);

    if (lectura != anterior_) {
      anterior_ = lectura;
      cambio_ = ahora;
    }

    if (ahora - cambio_ >= ANTIRREBOTE_MS &&
        lectura != estable_) {
      estable_ = lectura;

      if (estable_ == LOW) {
        inicio_ = cambio_;
        largaEmitida_ = false;
      } else {
        if (!largaEmitida_) {
          if (permiteLarga_ &&
              cambio_ - inicio_ >= PULSACION_LARGA_MS) {
            largaEmitida_ = true;
            return LARGA;
          }
          return CORTA;
        }
      }
    }

    if (permiteLarga_ && estable_ == LOW && lectura == LOW &&
        !largaEmitida_ &&
        ahora - inicio_ >= PULSACION_LARGA_MS) {
      largaEmitida_ = true;
      return LARGA;
    }

    return NINGUNO;
  }

  bool pulsado() const {
    return estable_ == LOW;
  }

 private:
  uint8_t pin_;
  bool permiteLarga_;
  bool anterior_ = HIGH;
  bool estable_ = HIGH;
  bool largaEmitida_ = false;
  unsigned long cambio_ = 0;
  unsigned long inicio_ = 0;
};

Boton botonMas(27, true);
Boton botonMenos(26, false);

U8G2_SH1106_128X64_NONAME_F_HW_I2C pantalla(
    U8G2_R0, U8X8_PIN_NONE, 22, 21
);

unsigned int contador = 0;
unsigned long ultimoDibujo = 0;
const char* ultimoEvento = "Listo";

void mostrarPantalla() {
  pantalla.clearBuffer();
  pantalla.setFont(u8g2_font_6x12_tr);
  pantalla.drawStr(4, 12, "CONTADOR DOS BOTONES");

  pantalla.setCursor(4, 28);
  pantalla.print("Cuenta: ");
  pantalla.print(contador);

  pantalla.setCursor(4, 44);
  pantalla.print("+");
  pantalla.print(botonMas.pulsado() ? ":ON  " : ":OFF ");
  pantalla.print("-");
  pantalla.print(botonMenos.pulsado() ? ":ON" : ":OFF");

  pantalla.drawStr(4, 60, ultimoEvento);
  pantalla.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  botonMas.iniciar();
  botonMenos.iniciar();

  pantalla.setI2CAddress(0x3C * 2);
  pantalla.begin();
  mostrarPantalla();
}

void loop() {
  unsigned long ahora = millis();
  Evento mas = botonMas.actualizar(ahora);
  Evento menos = botonMenos.actualizar(ahora);

  if (mas == LARGA) {
    contador = 0;
    ultimoEvento = "Reinicio por 5 s";
    Serial.println("Pulsacion larga: contador a cero");
  } else {
    if (mas == CORTA) {
      if (contador < 9999) contador++;
      ultimoEvento = "Corta +: sumar";
      Serial.printf("Sumar: %u\n", contador);
    }

    if (menos == CORTA) {
      if (contador > 0) contador--;
      ultimoEvento = "Corta -: restar";
      Serial.printf("Restar: %u\n", contador);
    }
  }

  if (ahora - ultimoDibujo >= 100) {
    ultimoDibujo = ahora;
    mostrarPantalla();
  }
}