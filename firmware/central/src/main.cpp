// Central Megafonia 0.1.0. Audio SIMULADO: 2 segundos por repeticion.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "secrets.h"

constexpr uint32_t DURACION_MS = 2000;
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
uint32_t inicioPaso = 0;
bool conectadoAntes = false;
uint32_t ultimoIntento = 0;

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

void anunciar() {
  const Evento& e = eventos[activo];
  Serial.printf("SIMULACION | %u %s | repeticion %u/%u\n", e.boton,
                AVISOS[e.boton], e.repeticion, e.repeticiones);
}

void actualizarReproduccion(uint32_t ahora) {
  while (activo >= 0 && uint32_t(ahora - inicioPaso) >= DURACION_MS) {
    Evento& e = eventos[activo];
    inicioPaso += DURACION_MS;
    if (e.repeticion < e.repeticiones) ++e.repeticion;
    else if (e.accion == "TODOS" && e.boton < 8) {
      ++e.boton;
      e.repeticion = 1;
    } else {
      e.estado = "FINALIZADA";
      Serial.printf("FIN SIMULACION | %s\n", e.id.c_str());
      activo = -1;
      return;
    }
    anunciar();
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
  doc["simulado"] = true;
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
  if (indiceSesion < 0) {
    indiceSesion = int(siguienteSesion);
    siguienteSesion = (siguienteSesion + 1) % 32;
  }
  sesiones[indiceSesion].nombre = sesion;
  sesiones[indiceSesion].secuencia = numero;
  if (activo >= 0) {
    eventos[activo].estado = accion == "PARAR" ? "PARADA" : "SUSTITUIDA";
    Serial.println(accion == "PARAR" ? "SIMULACION PARADA" : "AVISO SUSTITUIDO");
    activo = -1;
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
    inicioPaso = millis();
    anunciar();
  }
  responderEvento(e);
}

void resumen() {
  actualizarReproduccion(millis());
  StaticJsonDocument<1024> doc;
  doc["estado"] = "Central preparada";
  doc["version"] = "0.1.0";
  doc["simulado"] = true;
  doc["segundos_por_reproduccion"] = 2;
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
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

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nCENTRAL MEGAFONIA 0.1.0 - AUDIO SIMULADO");
  Serial.printf("Flash: %u MB | PSRAM: %u MB\n",
                unsigned(ESP.getFlashChipSize() / 1048576),
                unsigned(ESP.getPsramSize() / 1048576));
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
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("megafonia-central");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  ultimoIntento = millis();
  Serial.println("Conectando al WiFi...");
}

void loop() {
  uint32_t ahora = millis();
  actualizarReproduccion(ahora);
  bool conectado = WiFi.status() == WL_CONNECTED;
  if (conectado && !conectadoAntes) {
    servidor.begin();
    Serial.printf("WIFI OK | http://%s:%u/\n", WiFi.localIP().toString().c_str(), PUERTO);
    Serial.println("Central lista. Duracion simulada: 2 segundos por repeticion.");
  } else if (!conectado && conectadoAntes) {
    servidor.stop();
    Serial.println("WiFi perdido. Reconectando...");
  }
  conectadoAntes = conectado;
  if (conectado) servidor.handleClient();
  else if (uint32_t(ahora - ultimoIntento) >= 15000) {
    ultimoIntento = ahora;
    WiFi.reconnect();
    Serial.println("Esperando conexion WiFi...");
  }
  delay(1);
}
