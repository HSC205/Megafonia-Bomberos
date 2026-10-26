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
#include "enlace_privado.h"

static_assert(sizeof(MEGAFONIA_SSID) > 1 && sizeof(MEGAFONIA_SSID) <= 33, "SSID: entre 1 y 32 caracteres");
static_assert(sizeof(MEGAFONIA_PASSWORD) >= 9 && sizeof(MEGAFONIA_PASSWORD) <= 64, "Clave WiFi: entre 8 y 63 caracteres");

// Red propia de la central: esta direccion no depende del router.
constexpr char BASE_CENTRAL[] = "http://192.168.4.1:8080";
constexpr uint8_t PINES[] = {27, 26, 25, 33, 32, 18, 19, 23};
constexpr uint32_t ANTIRREBOTE_MS = 30;
constexpr uint32_t PULSACION_LARGA_MS = 5000;
constexpr uint32_t ERROR_MS = 4000;
constexpr char VERSION_FW[] = "0.4.1";
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
enum Modo { NORMAL, MENU, EDITAR_REP, EDITAR_VOL, ESTADO, VERSION, PRUEBA, CANCELANDO };
const char* OPCIONES[] = {
  "Reproducir todos", "Repeticiones", "Volumen", "Estado del sistema", "Version", "Salir"
};
constexpr uint8_t NUM_OPCIONES = sizeof(OPCIONES) / sizeof(OPCIONES[0]);
struct PeticionVolumen { uint32_t id, creada; bool guardar; uint8_t valor; };
struct RespuestaVolumen { uint32_t id; bool ok, guardar; uint8_t valor; };
QueueHandle_t peticionesVol = nullptr, respuestasVol = nullptr;
uint32_t idVolumen = 0;
uint8_t editandoVol = 0;
bool volumenCargado = false, volumenPendiente = false;
const char* notaVolumen = nullptr;
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
bool enlaceConectadoAntes = false;
const char* notaEditor = nullptr;
uint32_t inicioNota = 0;

void idEvento(char* destino, size_t n, uint32_t numero) {
  snprintf(destino, n, "%s-%lu", sesion, static_cast<unsigned long>(numero));
}

Resultado consultarCentral(const Solicitud& s, bool nuevo) {
  Resultado r{s.secuencia, FALLIDA, -100, s.boton, 1};
  if (WiFi.status() != WL_CONNECTED) return r;
  if (nuevo && millis() - s.creada > 2500) { r.codigo = -101; return r; }
  char id[56], objetivo[56];
  idEvento(id, sizeof(id), s.secuencia);
  idEvento(objetivo, sizeof(objetivo), s.objetivo);
  String url = String(BASE_CENTRAL) + (nuevo ? "/evento" : "/estado?id=");
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

RespuestaVolumen consultarVolumen(const PeticionVolumen& p) {
  RespuestaVolumen r{p.id, false, p.guardar, 0};
  if (WiFi.status() != WL_CONNECTED || millis() - p.creada > 2500) return r;
  WiFiClient cliente;
  HTTPClient http;
  http.setConnectTimeout(800); http.setTimeout(1000);
  if (!http.begin(cliente, String(BASE_CENTRAL) + "/volumen")) return r;
  const char* campos[] = {"X-Volumen"};
  http.collectHeaders(campos, 1);
  int codigo;
  if (p.guardar) {
    http.addHeader("Content-Type", "application/json");
    codigo = http.POST(String("{\"volumen\":") + String(p.valor) + "}");
  } else codigo = http.GET();
  String valor = http.header("X-Volumen");
  bool numero = valor.length() > 0 && valor.length() <= 2;
  for (unsigned i = 0; i < valor.length(); ++i)
    if (valor[i] < '0' || valor[i] > '9') numero = false;
  int v = valor.toInt();
  if (codigo == 200 && numero && v <= 30 && (!p.guardar || v == p.valor)) {
    r.ok = true; r.valor = uint8_t(v);
  }
  http.end(); return r;
}

void pedirVolumen(bool guardar, uint32_t ahora) {
  notaVolumen = nullptr;
  if (!peticionesVol || WiFi.status() != WL_CONNECTED) {
    notaVolumen = "SIN CONFIRMACION"; return;
  }
  PeticionVolumen p{++idVolumen, ahora, guardar, editandoVol};
  volumenPendiente = true;
  xQueueOverwrite(peticionesVol, &p);
}

void actualizarVolumen() {
  if (!respuestasVol) return;
  RespuestaVolumen r;
  while (xQueueReceive(respuestasVol, &r, 0) == pdTRUE) {
    if (r.id != idVolumen || modo != EDITAR_VOL) continue;
    volumenPendiente = false;
    if (!r.ok) {
      // Un timeout al guardar puede haber ocurrido despues de aplicarse.
      // Obliga a consultar otra vez antes de ofrecer un valor como confirmado.
      volumenCargado = false;
      notaVolumen = "SIN CONFIRMACION";
    } else {
      editandoVol = r.valor; volumenCargado = true;
      notaVolumen = r.guardar ? "GUARDADO" : nullptr;
    }
  }
}

void trabajadorRed(void*) {
  Solicitud actual{};
  bool siguiendo = false;
  uint32_t consulta = 0;
  for (;;) {
    Solicitud nueva;
    if (xQueueReceive(envios, &nueva, 0) == pdTRUE) {
      actual = nueva;
      Resultado r = consultarCentral(actual, true);
      siguiendo = r.estado == ACTIVA;
      consulta = millis();
      xQueueSend(resultados, &r, portMAX_DELAY);
      Serial.printf("Evento %lu: HTTP %d, estado %d\n",
                    static_cast<unsigned long>(actual.secuencia), r.codigo,
                    static_cast<int>(r.estado));
    } else if (siguiendo && millis() - consulta >= 150) {
      Resultado r = consultarCentral(actual, false);
      consulta = millis();
      siguiendo = r.estado == ACTIVA;
      xQueueSend(resultados, &r, portMAX_DELAY);
    }
    PeticionVolumen pv;
    if (xQueueReceive(peticionesVol, &pv, 0) == pdTRUE) {
      RespuestaVolumen rv = consultarVolumen(pv);
      xQueueOverwrite(respuestasVol, &rv);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void iniciarRed() {
  envios = xQueueCreate(1, sizeof(Solicitud));
  resultados = xQueueCreate(8, sizeof(Resultado));
  peticionesVol = xQueueCreate(1, sizeof(PeticionVolumen));
  respuestasVol = xQueueCreate(1, sizeof(RespuestaVolumen));
  if (envios && resultados && peticionesVol && respuestasVol &&
      xTaskCreate(trabajadorRed, "RedAvisos", 6144, nullptr, 1, nullptr) == pdPASS) {
    emisorListo = true;
    return;
  }
  if (envios) vQueueDelete(envios);
  if (resultados) vQueueDelete(resultados);
  if (peticionesVol) vQueueDelete(peticionesVol);
  if (respuestasVol) vQueueDelete(respuestasVol);
  peticionesVol = respuestasVol = nullptr;
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
    else if (e[6] == CORTA) seleccion = (seleccion + NUM_OPCIONES - 1) % NUM_OPCIONES;
    else if (e[7] == CORTA) seleccion = (seleccion + 1) % NUM_OPCIONES;
    else if (e[0] == CORTA) {
      if (seleccion == 0) { modo = PRUEBA; mandar(TODOS, 1, ahora); }
      else if (seleccion == 1) {
        modo = EDITAR_REP; editandoRep = repeticiones; notaEditor = nullptr;
      } else if (seleccion == 2) {
        modo = EDITAR_VOL; volumenCargado = false; volumenPendiente = false;
        pedirVolumen(false, ahora);
      } else if (seleccion == 3) modo = ESTADO;
      else if (seleccion == 4) modo = VERSION;
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
  } else if (modo == EDITAR_VOL) {
    // Mientras se guarda no se permite salir: evita ocultar una confirmacion pendiente.
    if (volumenPendiente) return;
    if (e[1] == CORTA) { modo = MENU; ++idVolumen; }
    else if (e[0] == CORTA) pedirVolumen(volumenCargado, ahora);
    else if (volumenCargado && e[6] == CORTA && editandoVol > 0) {
      --editandoVol; notaVolumen = nullptr;
    } else if (volumenCargado && e[7] == CORTA && editandoVol < 30) {
      ++editandoVol; notaVolumen = nullptr;
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
    for (uint8_t fila = 0; fila < 3 && primero + fila < NUM_OPCIONES; ++fila) {
      uint8_t indice = primero + fila;
      if (indice == seleccion) {
        pantalla.drawBox(0, 12 + fila * 11, 128, 11);
        pantalla.setDrawColor(0);
      }
      pantalla.drawStr(0, 22 + fila * 11, indice == seleccion ? ">" : " ");
      pantalla.drawStr(10, 22 + fila * 11, OPCIONES[indice]);
      pantalla.setDrawColor(1);
    }
    pantalla.setFont(u8g2_font_5x7_tr);
    pantalla.drawStr(0, 54, "7/8  1:OK  2:volver");
  } else if (modo == EDITAR_REP) {
    textoCentrado("REPETICIONES", 12);
    char valor[12]; snprintf(valor, sizeof(valor), "%u", static_cast<unsigned>(editandoRep));
    textoCentrado(valor, 32);
    if (notaEditor && ahora - inicioNota < 2000) textoCentrado(notaEditor, 46);
    else textoCentrado("Min 2  Max 10", 46);
    pantalla.setFont(u8g2_font_5x7_tr);
    pantalla.drawStr(0, 54, "7:- 8:+ 1:OK 2:atras");
  } else if (modo == EDITAR_VOL) {
    textoCentrado("VOLUMEN CENTRAL", 12);
    char valor[16];
    if (volumenCargado) snprintf(valor, sizeof(valor), "%u / 30", unsigned(editandoVol));
    else snprintf(valor, sizeof(valor), "-- / 30");
    textoCentrado(valor, 29);
    pantalla.drawFrame(14, 34, 100, 7);
    if (volumenCargado && editandoVol) pantalla.drawBox(15, 35, editandoVol * 98 / 30, 5);
    if (volumenPendiente) textoCentrado("ESPERE...", 52);
    else if (notaVolumen) textoCentrado(notaVolumen, 52);
    else textoCentrado(editandoVol == 0 ? "SILENCIO" : "1:guardar  2:atras", 52);
    pantalla.setFont(u8g2_font_5x7_tr);
    textoCentrado(volumenCargado ? "7:- 8:+ 1:OK 2:atras" : "1:consultar 2:atras", 63);
  } else if (modo == ESTADO) {
    textoCentrado("ESTADO DEL SISTEMA", 10);
    pantalla.setCursor(0, 22);
    pantalla.print(conectado ? "WiFi OK " : "WiFi SIN CONEXION");
    if (conectado) { pantalla.print(WiFi.RSSI()); pantalla.print(" dBm"); }
    pantalla.setCursor(0, 34); pantalla.print("IP:");
    if (conectado) pantalla.print(WiFi.localIP()); else pantalla.print("--");
    pantalla.drawStr(0, 46, huboAcuse ? "Ult.acuse central: OK" : "Central sin comprobar");
    pantalla.setFont(u8g2_font_5x7_tr);
    pantalla.drawStr(0, 54, "2:volver");
  } else if (modo == VERSION) {
    textoCentrado("VERSION BOTONERA", 14);
    textoCentrado(VERSION_FW, 32);
    textoCentrado("Central via WiFi", 46);
    pantalla.setFont(u8g2_font_5x7_tr);
    pantalla.drawStr(0, 54, "2:volver");
  } else if (modo == PRUEBA || modo == CANCELANDO) {
    if (vista == ERROR_AVISO) {
      textoCentrado("PRUEBA DE AVISOS", 16);
      if (((ahora - inicioError) / 500) % 2 == 0)
        textoCentrado("SIN CONFIRMACION", 40);
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
      pantalla.setFont(u8g2_font_5x7_tr);
      textoCentrado("2:cancelar", 54);
    }
  } else if (vista == ESPERA) {
    textoCentrado("MEGAFONIA", 27);
    pantalla.drawStr(20, 47, conectado ? "WiFi OK" : "Sin WiFi");
    iconoWiFi(88, 47, ahora);
  } else {
    pantalla.drawBox(0, 17, 128, 16);
    pantalla.setDrawColor(0);
    textoCentrado(AVISOS[ultimoBoton - 1], 29);
    pantalla.setDrawColor(1);
    if (vista == ERROR_AVISO && ((ahora - inicioError) / 500) % 2 == 0)
      textoCentrado("SIN CONFIRMACION", 49);
  }
  if (modo == VERSION || (modo == NORMAL && vista == ESPERA)) {
    pantalla.setFont(u8g2_font_5x7_tr);
    textoCentrado("by HSC205", 63);
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
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("megafonia-botonera");
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(MEGAFONIA_SSID, MEGAFONIA_PASSWORD);
  Serial.printf("BOTONERA 0.4.1 | enlace directo | buscando %s\n", MEGAFONIA_SSID);
  intentoWiFi = millis();
  dibujar(millis());
}

void loop() {
  uint32_t ahora = millis();
  actualizarResultados(ahora);
  actualizarVolumen();
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
  bool conectado = WiFi.status() == WL_CONNECTED;
  if (conectado && !enlaceConectadoAntes) {
    Serial.printf("CENTRAL WIFI OK | IP botonera %s | destino %s\n",
                  WiFi.localIP().toString().c_str(), BASE_CENTRAL);
  } else if (!conectado && enlaceConectadoAntes) {
    huboAcuse = false;
    // No dejar una pulsacion pendiente para enviarla al recuperar el enlace.
    if (envios) xQueueReset(envios);
    Serial.println("Enlace con central perdido. Reconectando a su red propia...");
  }
  enlaceConectadoAntes = conectado;
  if (!conectado && ahora - intentoWiFi >= 15000) {
    intentoWiFi = ahora; WiFi.reconnect();
  }
  if (ahora - ultimaPantalla >= 100) {
    ultimaPantalla = ahora; dibujar(ahora);
  }
  delay(1);
}


