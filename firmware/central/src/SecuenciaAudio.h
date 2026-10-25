#pragma once
#include <stdint.h>

// Orden de pistas; independiente del hardware y del reloj.
class SecuenciaAudio {
 public:
  enum Etapa { INICIO, VOZ, FINAL, TERMINADA };
  void iniciar(uint8_t boton, uint8_t repeticiones, bool todos, bool tonos) {
    boton_ = todos ? 1 : boton;
    total_ = repeticiones;
    repeticion_ = 1;
    todos_ = todos;
    tonos_ = tonos;
    etapa_ = tonos ? INICIO : VOZ;
  }
  bool terminarPista() {
    if (etapa_ == INICIO) etapa_ = VOZ;
    else if (etapa_ == VOZ) {
      if (repeticion_ < total_) ++repeticion_;
      else etapa_ = tonos_ ? FINAL : TERMINADA;
    } else if (etapa_ == FINAL) etapa_ = TERMINADA;
    if (etapa_ == TERMINADA && todos_ && boton_ < 8) {
      ++boton_; repeticion_ = 1; etapa_ = tonos_ ? INICIO : VOZ;
    }
    return etapa_ != TERMINADA;
  }
  uint16_t pista() const { return etapa_ == INICIO ? 9 : etapa_ == FINAL ? 20 : boton_; }
  uint8_t boton() const { return boton_; }
  uint8_t repeticion() const { return repeticion_; }
  Etapa etapa() const { return etapa_; }
 private:
  uint8_t boton_ = 1, total_ = 2, repeticion_ = 1;
  bool todos_ = false, tonos_ = true;
  Etapa etapa_ = TERMINADA;
};
