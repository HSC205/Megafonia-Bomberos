"""Megafonia 0.3.0: receptor y simulacion de audio, sin audio real."""
import json
import re
import threading
import time
from collections import OrderedDict
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

VERSION = "0.3.0"
AVISOS = {
    1: "I. Urbano", 2: "I. Industrial", 3: "I. Vehiculo",
    4: "I. Contenedor", 5: "Accidente Trafico", 6: "Rescate",
    7: "Servicio Tecnico", 8: "Escape de Gas",
}


class Simulador:
    def __init__(self, duracion=2.0, reloj=None):
        self.duracion = duracion
        self.reloj = reloj or time.monotonic
        self.cerrojo = threading.Lock()
        self.eventos = OrderedDict()
        self.secuencias = OrderedDict()
        self.actual = None
        self.fin = 0.0

    def _anunciar(self, e):
        print(f'[{datetime.now():%H:%M:%S}] REPRODUCIENDO | '
              f'{e["boton"]} {e["aviso"]} | '
              f'{e["repeticion"]}/{e["repeticiones"]}', flush=True)

    def _actualizar(self):
        # Un unico reloj de reproduccion, sin temporizadores antiguos.
        while self.actual and self.reloj() >= self.fin:
            e = self.eventos[self.actual]
            if e["repeticion"] < e["repeticiones"]:
                e["repeticion"] += 1
            elif e["accion"] == "TODOS" and e["boton"] < 8:
                e["boton"] += 1
                e["aviso"] = AVISOS[e["boton"]]
                e["repeticion"] = 1
            else:
                e["estado"] = "FINALIZADA"
                print(f'FIN | {e["id"]}', flush=True)
                self.actual = None
                break
            self.fin += self.duracion
            self._anunciar(e)

    def tick(self):
        with self.cerrojo:
            self._actualizar()

    def recibir(self, datos):
        evento_id = datos.get("id")
        if not isinstance(evento_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", evento_id):
            return 400, {"error": "ID incorrecto"}
        sesion, _, numero = evento_id.rpartition("-")
        if not sesion or not numero.isdigit() or not 1 <= int(numero) <= 0xffffffff:
            return 400, {"error": "Secuencia incorrecta"}
        numero = int(numero)
        accion = datos.get("accion")
        boton = datos.get("boton")
        rep = datos.get("repeticiones")
        objetivo = datos.get("objetivo", "")
        if accion not in ("AVISO", "TODOS", "PARAR"):
            return 400, {"error": "Accion incorrecta"}
        if accion != "PARAR":
            if type(rep) is not int or not 2 <= rep <= 10:
                return 400, {"error": "Repeticiones fuera de 2 a 10"}
            if type(boton) is not int or boton not in AVISOS:
                return 400, {"error": "Boton incorrecto"}
        elif not isinstance(objetivo, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", objetivo):
            return 400, {"error": "Objetivo incorrecto"}
        firma = (accion, boton, rep, objetivo)
        with self.cerrojo:
            self._actualizar()
            anterior = self.eventos.get(evento_id)
            if anterior:
                if anterior["firma"] != firma:
                    return 409, {"error": "ID reutilizado con otros datos"}
                return 200, self._publico(anterior, duplicado=True)
            if numero <= self.secuencias.get(sesion, 0):
                return 409, {"error": "Orden antigua"}
            if accion == "PARAR" and self.actual and objetivo != self.actual:
                return 409, {"error": "La reproduccion activa es otra"}
            self.secuencias[sesion] = numero
            self.secuencias.move_to_end(sesion)
            if len(self.secuencias) > 64:
                self.secuencias.popitem(last=False)
            if self.actual:
                self.eventos[self.actual]["estado"] = "PARADA" if accion == "PARAR" else "SUSTITUIDA"
                print(f'{"PARADA" if accion == "PARAR" else "INTERRUMPIDA"} | {self.actual}', flush=True)
            e = {
                "id": evento_id, "accion": accion, "firma": firma,
                "boton": (1 if accion == "TODOS" else boton) if accion != "PARAR" else 0,
                "repeticiones": rep if accion != "PARAR" else 0,
                "repeticion": 1 if accion != "PARAR" else 0,
                "estado": "PARADA" if accion == "PARAR" else "ACTIVA",
            }
            e["aviso"] = AVISOS.get(e["boton"], "")
            self.eventos[evento_id] = e
            self.actual = None if accion == "PARAR" else evento_id
            self.fin = self.reloj() + self.duracion
            if self.actual:
                self._anunciar(e)
            if len(self.eventos) > 256:
                self.eventos.popitem(last=False)
            return 200, self._publico(e, duplicado=False)

    @staticmethod
    def _publico(e, **extra):
        return {**{k: v for k, v in e.items() if k != "firma"}, **extra}

    def consultar(self, evento_id):
        with self.cerrojo:
            self._actualizar()
            e = self.eventos.get(evento_id)
            return self._publico(e) if e else None

    def resumen(self):
        with self.cerrojo:
            self._actualizar()
            return {
                "estado": "Receptor preparado", "version": VERSION,
                "segundos_por_reproduccion": self.duracion,
                "reproduciendo": self._publico(self.eventos[self.actual]) if self.actual else None,
                "ultimos_eventos": [self._publico(e) for e in list(self.eventos.values())[-20:]],
            }


SIM = Simulador()


class Receptor(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(3)

    def responder(self, codigo, datos):
        cuerpo = json.dumps(datos, ensure_ascii=False).encode("utf-8")
        self.send_response(codigo)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(cuerpo)))
        self.send_header("Cache-Control", "no-store")
        if codigo == 200 and "id" in datos:
            self.send_header("X-Evento-ID", datos["id"])
            self.send_header("X-Reproduccion", datos["estado"])
            self.send_header("X-Boton", str(datos["boton"]))
            self.send_header("X-Repeticion", str(datos["repeticion"]))
        self.end_headers()
        try:
            self.wfile.write(cuerpo)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        ruta = urlsplit(self.path)
        if ruta.path == "/":
            self.responder(200, SIM.resumen())
        elif ruta.path == "/estado":
            e = SIM.consultar(parse_qs(ruta.query).get("id", [""])[0])
            self.responder(200 if e else 404, e or {"error": "Evento desconocido"})
        else:
            self.responder(404, {"error": "Ruta desconocida"})

    def do_POST(self):
        if self.path != "/evento":
            self.responder(404, {"error": "Ruta desconocida"})
            return
        try:
            n = int(self.headers.get("Content-Length", "0"))
            if not 0 < n <= 512:
                raise ValueError()
            cuerpo = self.rfile.read(n)
            if len(cuerpo) != n:
                raise ValueError()
            datos = json.loads(cuerpo)
            if not isinstance(datos, dict):
                raise ValueError()
        except (ValueError, UnicodeDecodeError, TimeoutError):
            self.responder(400, {"error": "Peticion incorrecta"})
            return
        codigo, respuesta = SIM.recibir(datos)
        self.responder(codigo, respuesta)

    def log_message(self, formato, *args):
        pass


def actualizar_reproduccion(parar):
    while not parar.wait(0.05):
        SIM.tick()


if __name__ == "__main__":
    servidor = ThreadingHTTPServer(("0.0.0.0", 8080), Receptor)
    parar = threading.Event()
    trabajador = threading.Thread(target=actualizar_reproduccion, args=(parar,), daemon=True)
    trabajador.start()
    print(f'Receptor {VERSION} preparado: menu, repeticiones y secuencia de 8 avisos', flush=True)
    print("http://192.168.68.111:8080/ | Detener: Ctrl+C", flush=True)
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        print("\nReceptor detenido")
    finally:
        parar.set()
        trabajador.join(timeout=1)
        servidor.server_close()
