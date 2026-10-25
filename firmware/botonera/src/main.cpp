#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_system.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "secrets.h"

constexpr char BASE_PC[] = "http://192.168.68.114:8080";
constexpr uint8_t PINES[] = {27, 26, 25, 33, 32, 18, 19, 23};
constexpr uint32_t ANTIRREBOTE_MS = 30;
constexpr uint32_t PULSACION_LARGA_MS = 5000;
constexpr uint32_t ERROR_MS = 4000;
constexpr char VERSION_FW[] = "0.3.0";
constexpr uint8_t MIN_REP = 2, MAX_REP = 10;
const char* AVISOS[] = {
  "I. Urbano", "I. Industrial", "I. Vehiculo", "I. Contenedor",
  "Accidente Trafico", "Rescate", "Servicio Tecnico", "Escape de Gas"
};
enum Evento { NINGUNO, CORTA, LARGA };
class Boton {
 public:
  void iniciar(uint8_t pin, bool larga) {
    pin_ = pin;
    permiteLarga_ = larga;
    pinMode(pin_, INPUT_PULLUP);
    anterior_ = estable_ = digitalRead(pin_);
    cambio_ = millis();
    armado_ = false;  // Exige estar suelto y estable al arrancar.
  }
  Evento actualizar(uint32_t ahora) {
    bool lectura = digitalRead(pin_);
    if (lectura != anterior_) {
      anterior_ = lectura;
      cambio_ = ahora;
    }
    if (!armado_) {
      if (lectura == HIGH && ahora - cambio_ >= ANTIRREBOTE_MS) {
        estable_ = HIGH;
        armado_ = true;
      }
      return NINGUNO;
    }
    if (ahora - cambio_ >= ANTIRREBOTE_MS && lectura != estable_) {
      estable_ = lectura;
      if (estable_ == LOW) {
        inicio_ = cambio_;
        largaEmitida_ = false;
      } else if (!largaEmitida_) {
        if (permiteLarga_ && cambio_ - inicio_ >= PULSACION_LARGA_MS) {
          largaEmitida_ = true;
          return LARGA;
        }
        return CORTA;
      }
    }
    if (permiteLarga_ && estable_ == LOW && lectura == LOW &&
        !largaEmitida_ && ahora - inicio_ >= PULSACION_LARGA_MS) {
      largaEmitida_ = true;
      return LARGA;
    }
    return NINGUNO;
  }
 private:
  uint8_t pin_ = 0;
  bool permiteLarga_ = false, anterior_ = HIGH, estable_ = HIGH;
  bool armado_ = false, largaEmitida_ = false;
  uint32_t cambio_ = 0, inicio_ = 0;
};



enum Accion { AVISO_INDIVIDUAL, TODOS, PARAR };
enum EstadoRemoto { ACTIVA, FINALIZADA, PARADA, FALLIDA };
struct Solicitud {
  uint32_t secuencia, creada, objetivo;
  Accion accion;
  uint8_t boton, repeticiones;
};
struct Resultado {
  uint32_t secuencia;
  EstadoRemoto estado;
  int codigo;
  uint8_t boton, repeticion;
};
enum Vista { ESPERA, AVISO, ERROR_AVISO };
enum Modo { NORMAL, MENU, EDITAR_REP, ESTADO, VERSION, PRUEBA, CANCELANDO };
const char* OPCIONES[] = {
  "Reproducir todos", "Repeticiones", "Estado del sistema", "Version", "Salir"
};
QueueHandle_t envios = nullptr, resultados = nullptr;
bool emisorListo = false;
char sesion[32] = {};
Boton botones[8];
U8G2_SH1106_128X64_NONAME_F_HW_I2C pantalla(
    U8G2_R0, U8X8_PIN_NONE, 22, 21);
Vista vista = ESPERA;
Modo modo = NORMAL;
uint8_t seleccion = 0, repeticiones = 2, editandoRep = 2;
uint8_t ultimoBoton = 0, repeticionActual = 1;
uint32_t secuencia = 0, objetivoActivo = 0, inicioError = 0;
uint32_t ultimaPantalla = 0, intentoWiFi = 0, ultimaTeclaBaja = 0;
bool bloqueoSalida = false, huboAcuse = false;
const char* notaEditor = nullptr;
uint32_t inicioNota = 0;

void idEvento(char* destino, size_t n, uint32_t numero) {
  snprintf(destino, n, "%s-%lu", sesion, static_cast<unsigned long>(numero));
}

Resultado consultarPC(const Solicitud& s, bool nuevo) {
  Resultado r{s.secuencia, FALLIDA, -100, s.boton, 1};
  if (WiFi.status() != WL_CONNECTED) return r;
  if (nuevo && millis() - s.creada > 2500) { r.codigo = -101; return r; }
  char id[56], objetivo[56];
  idEvento(id, sizeof(id), s.secuencia);
  idEvento(objetivo, sizeof(objetivo), s.objetivo);
  String url = String(BASE_PC) + (nuevo ? "/evento" : "/estado?id=");
  if (!nuevo) url += id;
  WiFiClient cliente;
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(1000);
  if (!http.begin(cliente, url)) { r.codigo = -102; return r; }
  const char* campos[] = {"X-Evento-ID", "X-Reproduccion", "X-Boton", "X-Repeticion"};
  http.collectHeaders(campos, 4);
  if (nuevo) {
    char cuerpo[256];
    const char* accion = s.accion == TODOS ? "TODOS" :
                         s.accion == PARAR ? "PARAR" : "AVISO";
    snprintf(cuerpo, sizeof(cuerpo),
             "{\"id\":\"%s\",\"accion\":\"%s\",\"boton\":%u,"
             "\"repeticiones\":%u,\"objetivo\":\"%s\"}",
             id, accion, static_cast<unsigned>(s.boton),
             static_cast<unsigned>(s.repeticiones), objetivo);
    http.addHeader("Content-Type", "application/json");
    r.codigo = http.POST(String(cuerpo));
  } else r.codigo = http.GET();
  if (r.codigo == 200 && http.header("X-Evento-ID") == String(id)) {
    String estado = http.header("X-Reproduccion");
    if (estado == "ACTIVA") r.estado = ACTIVA;
    else if (estado == "FINALIZADA") r.estado = FINALIZADA;
    else if (estado == "PARADA") r.estado = PARADA;
    int boton = http.header("X-Boton").toInt();
    int rep = http.header("X-Repeticion").toInt();
    if (boton >= 1 && boton <= 8) r.boton = boton;
    if (rep >= MIN_REP && rep <= MAX_REP) r.repeticion = rep;
    else if (rep == 1) r.repeticion = 1;
  }
  http.end();
  return r;
}

void trabajadorRed(void*) {
  Solicitud actual{};
  bool siguiendo = false;
  uint32_t consulta = 0;
  for (;;) {
    Solicitud nueva;
    if (xQueueReceive(envios, &nueva, 0) == pdTRUE) {
      actual = nueva;
      Resultado r = consultarPC(actual, true);
      siguiendo = r.estado == ACTIVA;
      consulta = millis();
      xQueueSend(resultados, &r, portMAX_DELAY);
      Serial.printf("Evento %lu: HTTP %d, estado %d\n",
                    static_cast<unsigned long>(actual.secuencia), r.codigo,
                    static_cast<int>(r.estado));
    } else if (siguiendo && millis() - consulta >= 150) {
      Resultado r = consultarPC(actual, false);
      consulta = millis();
      siguiendo = r.estado == ACTIVA;
      xQueueSend(resultados, &r, portMAX_DELAY);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void iniciarRed() {
  envios = xQueueCreate(1, sizeof(Solicitud));
  resultados = xQueueCreate(8, sizeof(Resultado));
  if (envios && resultados &&
      xTaskCreate(trabajadorRed, "RedAvisos", 6144, nullptr, 1, nullptr) == pdPASS) {
    emisorListo = true;
    return;
  }
  if (envios) vQueueDelete(envios);
  if (resultados) vQueueDelete(resultados);
  envios = resultados = nullptr;
}

void cargarRepeticiones() {
  Preferences p;
  if (p.begin("megafonia", true)) {
    uint8_t n = p.getUChar("repeticiones", MIN_REP);
    repeticiones = n >= MIN_REP && n <= MAX_REP ? n : MIN_REP;
    p.end();
  }
}

bool guardarRepeticiones(uint8_t n) {
  if (n < MIN_REP || n > MAX_REP) return false;
  if (n == repeticiones) return true;
  Preferences p;
  if (!p.begin("megafonia", false)) return false;
  bool ok = p.putUChar("repeticiones", n) == 1;
  p.end();
  if (ok) repeticiones = n;
  return ok;
}

void bloquearSalida(uint32_t ahora) {
  bloqueoSalida = true;
  ultimaTeclaBaja = ahora;
}

void mostrarError(uint32_t ahora) {
  vista = ERROR_AVISO;
  inicioError = ahora;
}

void mandar(Accion accion, uint8_t boton, uint32_t ahora) {
  uint32_t objetivo = objetivoActivo;
  ++secuencia;
  if (accion != PARAR) {
    objetivoActivo = secuencia;
    ultimoBoton = accion == TODOS ? 1 : boton;
    repeticionActual = 1;
  }
  if (!emisorListo || WiFi.status() != WL_CONNECTED) {
    mostrarError(ahora);
    if (envios) xQueueReset(envios);
    return;
  }
  Solicitud s{secuencia, ahora, objetivo, accion, boton, repeticiones};
  vista = AVISO;
  xQueueOverwrite(envios, &s);
}

void actualizarResultados(uint32_t ahora) {
  if (!resultados) return;
  Resultado r;
  while (xQueueReceive(resultados, &r, 0) == pdTRUE) {
    if (r.secuencia != secuencia) continue;
    if (r.estado == FALLIDA) mostrarError(ahora);
    else {
      huboAcuse = true;
      if (r.estado == FINALIZADA || r.estado == PARADA) {
        vista = ESPERA;
        if (modo == PRUEBA || modo == CANCELANDO) {
          modo = MENU;
          bloquearSalida(ahora);
        }
      } else {
        vista = AVISO;
        if (r.boton >= 1 && r.boton <= 8) ultimoBoton = r.boton;
        repeticionActual = r.repeticion;
      }
    }
  }
}

void manejarTeclas(const Evento* e, uint32_t ahora) {
  if (modo == NORMAL) {
    if (e[0] == LARGA) {
      modo = MENU;
      seleccion = 0;
      bloquearSalida(ahora);
    } else for (uint8_t i = 0; i < 8; ++i)
      if (e[i] == CORTA) mandar(AVISO_INDIVIDUAL, i + 1, ahora);
    return;
  }
  // En cualquier pantalla del menu no se envian los avisos normales.
  if (modo == MENU) {
    if (e[1] == CORTA) { modo = NORMAL; bloquearSalida(ahora); }
    else if (e[6] == CORTA) seleccion = (seleccion + 4) % 5;
    else if (e[7] == CORTA) seleccion = (seleccion + 1) % 5;
    else if (e[0] == CORTA) {
      if (seleccion == 0) { modo = PRUEBA; mandar(TODOS, 1, ahora); }
      else if (seleccion == 1) {
        modo = EDITAR_REP; editandoRep = repeticiones; notaEditor = nullptr;
      } else if (seleccion == 2) modo = ESTADO;
      else if (seleccion == 3) modo = VERSION;
      else { modo = NORMAL; bloquearSalida(ahora); }
    }
  } else if (modo == EDITAR_REP) {
    if (e[1] == CORTA) modo = MENU;
    else if (e[6] == CORTA && editandoRep > MIN_REP) --editandoRep;
    else if (e[7] == CORTA && editandoRep < MAX_REP) ++editandoRep;
    else if (e[0] == CORTA) {
      if (guardarRepeticiones(editandoRep)) modo = MENU;
      else { notaEditor = "ERROR AL GUARDAR"; inicioNota = ahora; }
    }
  } else if (modo == ESTADO || modo == VERSION) {
    if (e[1] == CORTA) modo = MENU;
  } else if (modo == PRUEBA && e[1] == CORTA) {
    modo = CANCELANDO;
    mandar(PARAR, 0, ahora);
  }
}
void textoCentrado(const char* texto, uint8_t y) {
  int ancho = pantalla.getStrWidth(texto);
  pantalla.drawStr(ancho < 128 ? (128 - ancho) / 2 : 0, y, texto);
}

void iconoWiFi(int x, int y, uint32_t ahora) {
  int barras;
  if (WiFi.status() != WL_CONNECTED) barras = 1 + (ahora / 250) % 4;
  else {
    int rssi = WiFi.RSSI();
    barras = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : 1;
  }
  for (int i = 0; i < 4; ++i) {
    int altura = 2 + i * 2;
    if (i < barras) pantalla.drawBox(x + i * 4, y - altura, 3, altura);
    else pantalla.drawFrame(x + i * 4, y - altura, 3, altura);
  }
  // Pequeno punto intermitente: actividad visual sin alterar la cobertura.
  if (WiFi.status() == WL_CONNECTED && (ahora / 700) % 2 == 0)
    pantalla.drawPixel(x + 18, y - 1);
}


void dibujar(uint32_t ahora) {
  bool conectado = WiFi.status() == WL_CONNECTED;
  pantalla.clearBuffer();
  pantalla.setFont(u8g2_font_6x12_tr);
  if (modo == MENU) {
    textoCentrado("MENU DE SERVICIO", 10);
    uint8_t primero = seleccion < 2 ? 0 : seleccion - 2;
    for (uint8_t fila = 0; fila < 3 && primero + fila < 5; ++fila) {
      uint8_t indice = primero + fila;
      pantalla.drawStr(0, 24 + fila * 12, indice == seleccion ? ">" : " ");
      pantalla.drawStr(10, 24 + fila * 12, OPCIONES[indice]);
    }
    pantalla.drawStr(0, 63, "7/8  1:OK  2:volver");
  } else if (modo == EDITAR_REP) {
    textoCentrado("REPETICIONES", 12);
    char valor[12]; snprintf(valor, sizeof(valor), "%u", static_cast<unsigned>(editandoRep));
    textoCentrado(valor, 32);
    if (notaEditor && ahora - inicioNota < 2000) textoCentrado(notaEditor, 46);
    else textoCentrado("Min 2  Max 10", 46);
    pantalla.drawStr(0, 62, "7:- 8:+ 1:OK 2:atras");
  } else if (modo == ESTADO) {
    textoCentrado("ESTADO DEL SISTEMA", 10);
    pantalla.setCursor(0, 22);
    pantalla.print(conectado ? "WiFi OK " : "WiFi SIN CONEXION");
    if (conectado) { pantalla.print(WiFi.RSSI()); pantalla.print(" dBm"); }
    pantalla.setCursor(0, 34); pantalla.print("IP:");
    if (conectado) pantalla.print(WiFi.localIP()); else pantalla.print("--");
    pantalla.drawStr(0, 46, huboAcuse ? "Ult. acuse PC: OK" : "PC: sin comprobar");
    pantalla.drawStr(0, 60, "2:volver");
  } else if (modo == VERSION) {
    textoCentrado("VERSION BOTONERA", 14);
    textoCentrado(VERSION_FW, 32);
    textoCentrado("API PC 0.3.0", 46);
    pantalla.drawStr(0, 60, "2:volver");
  } else if (modo == PRUEBA || modo == CANCELANDO) {
    if (vista == ERROR_AVISO) {
      textoCentrado("PRUEBA DE AVISOS", 16);
      if (((ahora - inicioError) / 500) % 2 == 0)
        textoCentrado("SIN CONFIRMAR", 40);
    } else if (modo == CANCELANDO) {
      textoCentrado("DETENIENDO PRUEBA", 32);
    } else {
      char progreso[24];
      snprintf(progreso, sizeof(progreso), "Prueba %u/8", static_cast<unsigned>(ultimoBoton));
      textoCentrado(progreso, 12);
      textoCentrado(AVISOS[ultimoBoton - 1], 28);
      snprintf(progreso, sizeof(progreso), "Repeticion %u/%u",
               static_cast<unsigned>(repeticionActual), static_cast<unsigned>(repeticiones));
      textoCentrado(progreso, 44);
      textoCentrado("2:cancelar", 60);
    }
  } else if (vista == ESPERA) {
    textoCentrado("MEGAFONIA", 27);
    pantalla.drawStr(20, 47, conectado ? "WiFi OK" : "Sin WiFi");
    iconoWiFi(88, 47, ahora);
  } else {
    textoCentrado(AVISOS[ultimoBoton - 1], 29);
    if (vista == ERROR_AVISO && ((ahora - inicioError) / 500) % 2 == 0)
      textoCentrado("SIN CONFIRMAR", 49);
  }
  pantalla.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < 8; ++i) botones[i].iniciar(PINES[i], i == 0);
  pantalla.setI2CAddress(0x3C * 2);
  pantalla.begin();
  cargarRepeticiones();
  uint64_t mac = ESP.getEfuseMac();
  snprintf(sesion, sizeof(sesion), "%08lx%04lx-%08lx",
           static_cast<unsigned long>(mac >> 16),
           static_cast<unsigned long>(mac & 0xffff),
           static_cast<unsigned long>(esp_random()));
  iniciarRed();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  intentoWiFi = millis();
  dibujar(millis());
}

void loop() {
  uint32_t ahora = millis();
  actualizarResultados(ahora);
  if (vista == ERROR_AVISO && ahora - inicioError >= ERROR_MS) {
    vista = ESPERA;
    if (modo == PRUEBA || modo == CANCELANDO) {
      modo = MENU; bloquearSalida(ahora);
    }
  }
  Evento eventos[8];
  bool todosSueltos = true;
  for (uint8_t i = 0; i < 8; ++i) {
    eventos[i] = botones[i].actualizar(ahora);
    if (digitalRead(PINES[i]) == LOW) todosSueltos = false;
  }
  if (bloqueoSalida) {
    if (!todosSueltos) ultimaTeclaBaja = ahora;
    if (todosSueltos && ahora - ultimaTeclaBaja >= ANTIRREBOTE_MS)
      bloqueoSalida = false;
  } else manejarTeclas(eventos, ahora);
  if (WiFi.status() != WL_CONNECTED && ahora - intentoWiFi >= 15000) {
    intentoWiFi = ahora; WiFi.reconnect();
  }
  if (ahora - ultimaPantalla >= 100) {
    ultimaPantalla = ahora; dibujar(ahora);
  }
  delay(1);
}

