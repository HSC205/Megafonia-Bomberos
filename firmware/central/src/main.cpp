// Central Megafonia 0.3.0. Enlace directo sin router. Audio real por DFPlayer Mini y fin por BUSY.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "enlace_privado.h"

static_assert(sizeof(MEGAFONIA_SSID) > 1 && sizeof(MEGAFONIA_SSID) <= 33, "SSID: entre 1 y 32 caracteres");
static_assert(sizeof(MEGAFONIA_PASSWORD) >= 9 && sizeof(MEGAFONIA_PASSWORD) <= 64, "Clave WiFi: entre 8 y 63 caracteres");
#include <DFRobotDFPlayerMini.h>
#include "SecuenciaAudio.h"

constexpr uint16_t PUERTO = 8080;
const char* AVISOS[] = {"", "I. Urbano", "I. Industrial", "I. Vehiculo",
  "I. Contenedor", "Accidente Trafico", "Rescate", "Servicio Tecnico", "Escape de Gas"};
WebServer servidor(PUERTO);

// Historial acotado en RAM: no se conserva al apagar la central.
struct Evento {
  String id, accion, objetivo, estado;
  int botonPedido = 0, repeticionesPedidas = 0;
  uint8_t boton = 0, repeticiones = 0, repeticion = 0;
};
struct Sesion { String nombre; uint32_t secuencia = 0; };
Evento eventos[64];
Sesion sesiones[32];
size_t siguienteEvento = 0, siguienteSesion = 0;
int activo = -1;
// AP exclusivo para la botonera; LAN se incorporara en otra etapa.
const IPAddress IP_CENTRAL(192, 168, 4, 1);
const IPAddress MASCARA_RED(255, 255, 255, 0);
constexpr uint8_t CANAL_ENLACE = 6;
bool enlaceListo = false;
uint32_t ultimoIntentoEnlace = 0;
int clientesAnteriores = -1;

bool idValido(const String& id) {
  if (id.length() < 1 || id.length() > 64) return false;
  for (size_t i = 0; i < id.length(); ++i) {
    char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

bool separarId(const String& id, String& sesion, uint32_t& numero) {
  if (!idValido(id)) return false;
  int guion = id.lastIndexOf('-');
  if (guion <= 0 || guion == int(id.length()) - 1) return false;
  uint64_t n = 0;
  for (size_t i = guion + 1; i < id.length(); ++i) {
    if (id[i] < '0' || id[i] > '9') return false;
    n = n * 10 + (id[i] - '0');
    if (n > UINT32_MAX) return false;
  }
  if (!n) return false;
  sesion = id.substring(0, guion);
  numero = uint32_t(n);
  return true;
}

int buscarEvento(const String& id) {
  for (size_t i = 0; i < 64; ++i) if (eventos[i].id == id) return int(i);
  return -1;
}

constexpr uint8_t PIN_RX_DF = 16, PIN_TX_DF = 17, PIN_BUSY = 4;
constexpr uint8_t VOLUMEN = 10;  // Rango 0..30, bajo para la prueba.
constexpr bool USAR_TONOS = true;
constexpr uint32_t PAUSA_MS = 250, INICIO_MAX_MS = 4000, PISTA_MAX_MS = 180000;
DFRobotDFPlayerMini reproductor;
SecuenciaAudio secuenciaAudio;
enum FaseAudio { SILENCIO, ESPERAR_REPOSO, ESPERAR_INICIO, SONANDO };
FaseAudio faseAudio = SILENCIO;
bool dfListo = false, altoDetectado = false;
bool respuestaDfRecibida = false, busyValidado = false;
uint32_t marcaAudio = 0, desdeAlto = 0;
uint16_t pistaActual = 0;

void fallarAudio(const char* motivo) {
  Serial.printf("ERROR AUDIO | %s\n", motivo);
  if (activo >= 0) eventos[activo].estado = "FALLIDA";
  activo = -1;
  faseAudio = SILENCIO;
  reproductor.stop();
}

void anunciar() {
  const Evento& e = eventos[activo];
  Serial.printf("AUDIO | %u %s | repeticion %u/%u | pista %04u | %s\n",
                e.boton, AVISOS[e.boton], e.repeticion, e.repeticiones, pistaActual,
                secuenciaAudio.etapa() == SecuenciaAudio::VOZ ? "VOZ" : "TONO");
}

void iniciarAudio(const Evento& e) {
  reproductor.stop();
  secuenciaAudio.iniciar(e.boton, e.repeticiones, e.accion == "TODOS", USAR_TONOS);
  faseAudio = ESPERAR_REPOSO;
  marcaAudio = millis();
  altoDetectado = false;
}

void actualizarReproduccion(uint32_t ahora) {
  // El fin se determina por BUSY, nunca por un mensaje UART atrasado.
  if (reproductor.available()) {
    uint8_t tipo = reproductor.readType();
    int valor = reproductor.read();
    if (tipo == DFPlayerCardRemoved) {
      dfListo = false;
      if (activo >= 0) fallarAudio("MicroSD retirada; insertar y reiniciar central");
    } else if (tipo == DFPlayerError) {
      Serial.printf("DFPlayer informa error %d\n", valor);
      if (activo >= 0 && faseAudio != ESPERAR_REPOSO) {
        fallarAudio("Error del DFPlayer durante la reproduccion"); return;
      }
    }
  }
  if (activo < 0 || faseAudio == SILENCIO) return;
  bool ocupado = digitalRead(PIN_BUSY) == LOW;
  if (faseAudio == ESPERAR_REPOSO) {
    if (ocupado) altoDetectado = false;
    else if (!altoDetectado) { altoDetectado = true; desdeAlto = ahora; }
    if (altoDetectado && uint32_t(ahora - desdeAlto) >= PAUSA_MS) {
      pistaActual = secuenciaAudio.pista();
      reproductor.playMp3Folder(pistaActual);
      marcaAudio = millis();
      faseAudio = ESPERAR_INICIO;
      altoDetectado = false;
      anunciar();
    } else if (uint32_t(ahora - marcaAudio) >= INICIO_MAX_MS) {
      fallarAudio("BUSY no vuelve a reposo; comprobar GPIO 4 y cableado");
    }
  } else if (faseAudio == ESPERAR_INICIO) {
    if (ocupado) {
      busyValidado = true;
      Serial.println("BUSY CONFIRMA: reproduccion iniciada");
      faseAudio = SONANDO;
      marcaAudio = ahora;
      altoDetectado = false;
    } else if (uint32_t(ahora - marcaAudio) >= INICIO_MAX_MS) {
      fallarAudio("La pista no arranca; comprobar archivo MP3 y BUSY");
    }
  } else if (faseAudio == SONANDO) {
    if (ocupado) altoDetectado = false;
    else if (!altoDetectado) { altoDetectado = true; desdeAlto = ahora; }
    if (altoDetectado && uint32_t(ahora - desdeAlto) >= 60) {
      if (secuenciaAudio.terminarPista()) {
        eventos[activo].boton = secuenciaAudio.boton();
        eventos[activo].repeticion = secuenciaAudio.repeticion();
        faseAudio = ESPERAR_REPOSO;
        marcaAudio = ahora;
        desdeAlto = ahora;
        altoDetectado = true;
      } else {
        eventos[activo].estado = "FINALIZADA";
        Serial.printf("FIN AUDIO | %s\n", eventos[activo].id.c_str());
        activo = -1;
        faseAudio = SILENCIO;
      }
    } else if (uint32_t(ahora - marcaAudio) >= PISTA_MAX_MS) {
      fallarAudio("Pista supera 180 segundos o BUSY queda activo");
    }
  }
}

void enviarJson(int codigo, JsonDocument& doc) {
  String cuerpo;
  serializeJson(doc, cuerpo);
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(codigo, "application/json; charset=utf-8", cuerpo);
}

void errorHttp(int codigo, const char* mensaje) {
  StaticJsonDocument<192> doc;
  doc["error"] = mensaje;
  enviarJson(codigo, doc);
}

void responderEvento(const Evento& e, bool duplicado = false) {
  StaticJsonDocument<768> doc;
  doc["id"] = e.id;
  doc["accion"] = e.accion;
  doc["estado"] = e.estado;
  doc["boton"] = e.boton;
  doc["aviso"] = AVISOS[e.boton];
  doc["repeticion"] = e.repeticion;
  doc["repeticiones"] = e.repeticiones;
  doc["duplicado"] = duplicado;
  doc["simulado"] = false;
  servidor.sendHeader("X-Evento-ID", e.id);
  servidor.sendHeader("X-Reproduccion", e.estado);
  servidor.sendHeader("X-Boton", String(e.boton));
  servidor.sendHeader("X-Repeticion", String(e.repeticion));
  enviarJson(200, doc);
}

void recibirEvento() {
  actualizarReproduccion(millis());
  String cuerpo = servidor.arg("plain");
  if (cuerpo.isEmpty() || cuerpo.length() > 512) {
    errorHttp(400, "Peticion incorrecta"); return;
  }
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, cuerpo) || !doc.is<JsonObject>() ||
      !doc["id"].is<const char*>() || !doc["accion"].is<const char*>()) {
    errorHttp(400, "JSON incorrecto"); return;
  }
  String id = doc["id"].as<String>();
  String accion = doc["accion"].as<String>();
  String sesion;
  uint32_t numero;
  if (!separarId(id, sesion, numero)) { errorHttp(400, "ID incorrecto"); return; }
  if (accion != "AVISO" && accion != "TODOS" && accion != "PARAR") {
    errorHttp(400, "Accion incorrecta"); return;
  }
  if (!doc["boton"].is<int>() || !doc["repeticiones"].is<int>()) {
    errorHttp(400, "Boton o repeticiones incorrectos"); return;
  }
  int boton = doc["boton"].as<int>();
  int rep = doc["repeticiones"].as<int>();
  if (accion != "PARAR" && (boton < 1 || boton > 8 || rep < 2 || rep > 10)) {
    errorHttp(400, "Boton 1-8; repeticiones 2-10"); return;
  }
  if (doc.containsKey("objetivo") && !doc["objetivo"].is<const char*>()) {
    errorHttp(400, "Objetivo incorrecto"); return;
  }
  String objetivo = doc["objetivo"] | "";
  if (accion == "PARAR" && !idValido(objetivo)) {
    errorHttp(400, "Objetivo incorrecto"); return;
  }
  int anterior = buscarEvento(id);
  if (anterior >= 0) {
    const Evento& e = eventos[anterior];
    if (e.accion != accion || e.botonPedido != boton ||
        e.repeticionesPedidas != rep || e.objetivo != objetivo) {
      errorHttp(409, "ID reutilizado con otros datos"); return;
    }
    responderEvento(e, true); return;
  }
  int indiceSesion = -1;
  for (size_t i = 0; i < 32; ++i) {
    if (sesiones[i].nombre == sesion) { indiceSesion = int(i); break; }
  }
  if (indiceSesion >= 0 && numero <= sesiones[indiceSesion].secuencia) {
    errorHttp(409, "Orden antigua"); return;
  }
  if (accion == "PARAR" && activo >= 0 && eventos[activo].id != objetivo) {
    errorHttp(409, "La reproduccion activa es otra"); return;
  }
  if (accion != "PARAR" && !dfListo) {
    errorHttp(503, "DFPlayer no preparado; revisar SD y reiniciar central"); return;
  }
  if (indiceSesion < 0) {
    indiceSesion = int(siguienteSesion);
    siguienteSesion = (siguienteSesion + 1) % 32;
  }
  sesiones[indiceSesion].nombre = sesion;
  sesiones[indiceSesion].secuencia = numero;
  if (activo >= 0) {
    eventos[activo].estado = accion == "PARAR" ? "PARADA" : "SUSTITUIDA";
    Serial.println(accion == "PARAR" ? "AUDIO PARADO" : "AVISO SUSTITUIDO");
    activo = -1;
  }
  if (accion == "PARAR") {
    reproductor.stop();
    faseAudio = SILENCIO;
  }
  int indice = int(siguienteEvento);
  siguienteEvento = (siguienteEvento + 1) % 64;
  Evento& e = eventos[indice];
  e.id = id; e.accion = accion; e.objetivo = objetivo;
  e.botonPedido = boton; e.repeticionesPedidas = rep;
  e.boton = accion == "PARAR" ? 0 : accion == "TODOS" ? 1 : boton;
  e.repeticiones = accion == "PARAR" ? 0 : rep;
  e.repeticion = accion == "PARAR" ? 0 : 1;
  e.estado = accion == "PARAR" ? "PARADA" : "ACTIVA";
  if (accion != "PARAR") {
    activo = indice;
    iniciarAudio(e);
  }
  responderEvento(e);
}

void resumen() {
  actualizarReproduccion(millis());
  StaticJsonDocument<1024> doc;
  doc["estado"] = !dfListo ? "Revisar DFPlayer y microSD" :
    busyValidado ? "Audio iniciado y confirmado por BUSY" : "Pendiente de verificar audio con BUSY";
  doc["version"] = "0.3.0";
  doc["simulado"] = false;
  doc["dfplayer_envio_habilitado"] = dfListo;
  doc["respuesta_uart_recibida"] = respuestaDfRecibida;
  doc["reproduccion_busy_validada"] = busyValidado;
  doc["busy_activo"] = digitalRead(PIN_BUSY) == LOW;
  doc["tonos"] = USAR_TONOS;
  doc["volumen"] = VOLUMEN;
  doc["ip"] = WiFi.softAPIP().toString();
  doc["modo_red"] = "AP_DIRECTO";
  doc["ssid"] = MEGAFONIA_SSID;
  doc["clientes_wifi"] = WiFi.softAPgetStationNum();
  doc["rssi"] = nullptr;  // En modo AP no hay RSSI de un router.
  doc["ethernet_implementado"] = false;
  if (activo < 0) doc["reproduciendo"] = nullptr;
  else {
    const Evento& e = eventos[activo];
    JsonObject r = doc.createNestedObject("reproduciendo");
    r["id"] = e.id; r["aviso"] = AVISOS[e.boton];
    r["boton"] = e.boton; r["repeticion"] = e.repeticion;
    r["repeticiones"] = e.repeticiones;
  }
  enviarJson(200, doc);
}

bool iniciarEnlaceDirecto() {
  ultimoIntentoEnlace = millis();
  servidor.stop();
  bool ok = WiFi.mode(WIFI_AP);
  if (ok) ok = WiFi.softAPConfig(IP_CENTRAL, IP_CENTRAL, MASCARA_RED);
  if (ok) ok = WiFi.softAP(MEGAFONIA_SSID, MEGAFONIA_PASSWORD, CANAL_ENLACE, 0, 2);
  if (!ok) {
    WiFi.softAPdisconnect(true);
    Serial.println("ERROR RED | no se pudo crear la WiFi propia; reintento en 15 s");
    return false;
  }
  servidor.begin();
  clientesAnteriores = -1;
  Serial.printf("RED PROPIA OK | SSID %s | canal %u\n", MEGAFONIA_SSID, CANAL_ENLACE);
  Serial.printf("CENTRAL | http://%s:%u/ | no necesita router ni Internet\n",
                WiFi.softAPIP().toString().c_str(), PUERTO);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nCENTRAL MEGAFONIA 0.3.0 - WIFI PROPIA Y AUDIO REAL");
  Serial.printf("Flash: %u MB | PSRAM: %u MB\n",
                unsigned(ESP.getFlashChipSize() / 1048576),
                unsigned(ESP.getPsramSize() / 1048576));
  pinMode(PIN_BUSY, INPUT_PULLUP);
  Serial1.begin(9600, SERIAL_8N1, PIN_RX_DF, PIN_TX_DF);
  Serial.println("Esperando 3 segundos al arranque del DFPlayer...");
  delay(3000);
  // begin sin ACK y sin reset siempre devuelve true: NO prueba el enlace.
  // Habilitamos el envio y verificamos por una consulta y por BUSY al reproducir.
  reproductor.begin(Serial1, false, false);
  reproductor.setTimeOut(600);
  reproductor.stop();
  delay(150);
  reproductor.volume(VOLUMEN);
  delay(150);
  reproductor.enableDAC();
  delay(150);
  int archivos = reproductor.readFileCounts();
  respuestaDfRecibida = archivos > 0;
  if (respuestaDfRecibida) {
    Serial.printf("DFPLAYER RESPONDE | archivos reportados: %d\n", archivos);
  } else {
    Serial.println("Sin respuesta valida a la consulta. Aun no confirma un fallo de audio.");
  }
  dfListo = true;  // Envios permitidos; no significa reproduccion confirmada.
  Serial.println("MODO COMPATIBLE | volumen 10/30 | pulsar un aviso para verificar BUSY");
  servidor.on("/", HTTP_GET, resumen);
  servidor.on("/evento", HTTP_POST, recibirEvento);
  servidor.on("/estado", HTTP_GET, []() {
    actualizarReproduccion(millis());
    String id = servidor.arg("id");
    if (!idValido(id)) { errorHttp(400, "ID incorrecto"); return; }
    int i = buscarEvento(id);
    if (i < 0) { errorHttp(404, "Evento desconocido"); return; }
    responderEvento(eventos[i]);
  });
  servidor.onNotFound([]() { errorHttp(404, "Ruta desconocida"); });
  WiFi.persistent(false);
  enlaceListo = iniciarEnlaceDirecto();
}

void loop() {
  uint32_t ahora = millis();
  actualizarReproduccion(ahora);
  if (enlaceListo && !(WiFi.getMode() & WIFI_AP)) {
    servidor.stop();
    enlaceListo = false;
    Serial.println("ERROR RED | punto de acceso detenido; recreando enlace");
  }
  if (!enlaceListo) {
    if (uint32_t(ahora - ultimoIntentoEnlace) >= 15000)
      enlaceListo = iniciarEnlaceDirecto();
  } else {
    // Se atiende aunque no haya clientes: WL_CONNECTED solo sirve para modo STA.
    servidor.handleClient();
    int clientes = WiFi.softAPgetStationNum();
    if (clientes != clientesAnteriores) {
      Serial.printf("ENLACE DIRECTO | clientes WiFi conectados: %d\n", clientes);
      clientesAnteriores = clientes;
    }
  }
  delay(1);
}

