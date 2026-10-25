# Central ESP32-S3 y enlace con la botonera

Etapa validada el 25 de octubre de 2026. Central 0.1.0 sobre ESP32-S3 N16R8 y botonera 0.3.0 con OLED y ocho pulsadores. El audio sigue simulado: no hay control de DFPlayer en esta versión.

## Preparación

Abrir `firmware/central`, no `firmware/central/central`. Copiar el `secrets.h` de la botonera a `firmware/central/include/secrets.h`. Debe definir `WIFI_SSID` y `WIFI_PASSWORD`; las credenciales nunca se suben a Git.

Compilar y cargar por el USB-C marcado COM. Monitor serie a 115200. La configuración fija Espressif32 6.12.0, ArduinoJson 6.21.5, flash 16 MB y PSRAM octal. El arranque observado muestra Flash 16 MB y PSRAM 7 MB con la división entera usada por el programa; no se ha realizado una prueba de memoria específica.

## Enlace probado

La central obtuvo por DHCP `192.168.68.114` y escucha HTTP en el puerto 8080. Su raíz devuelve JSON con `Central preparada` y `simulado: true`. Reservar la dirección en el router posteriormente.

La declaración `BASE_PC` de la botonera ahora contiene `http://192.168.68.114:8080`. Se conserva el nombre para minimizar el cambio. Cargar la botonera tras editar; ambos ESP32 deben estar alimentados. El receptor Python del PC se conserva como herramienta anterior, pero no hace falta ejecutarlo para este enlace.

Héctor confirmó antes los ocho botones y la navegación completa. En la prueba de la central se observan I. Urbano e I. Industrial con repeticiones 1/2, 2/2 y FIN SIMULACION; Escape de Gas aparece con sus dos repeticiones. La OLED muestra el aviso hasta finalizar la simulación.

## Protocolo

- `POST /evento`: AVISO, TODOS y PARAR, con ID, botón, repeticiones y objetivo.
- `GET /estado?id=...`: estado de un evento.
- Cabeceras: X-Evento-ID, X-Reproduccion, X-Boton, X-Repeticion.
- Dos segundos por repetición; de 2 a 10 repeticiones.
- Un aviso nuevo sustituye al anterior. TODOS recorre 1 a 8. PARAR detiene el objetivo activo.
- Historial volátil limitado a 64 eventos y 32 sesiones. Reiniciar no reanuda avisos.

Sustitución, TODOS y cancelación están implementados; no consta una validación específica de todos esos casos contra la central en esta sesión.

## Pendientes

DFPlayer, microSD, audio real, tono de entrada y salida, diagnóstico real, fallo de Wi-Fi y recuperación, reserva DHCP y cambiar SIN CONFIRMAR por SIN RESPUESTA. El menú aún contiene referencias a PC de la versión anterior.

## Guardar en Git desde VS Code

Guardar archivos, revisar Control de código fuente, preparar los archivos previstos con +, escribir un mensaje, Commit y Push si falta. Nunca incluir secrets.h ni .pio. Las marcas M y U indican cambios respecto a Git, no archivos sin guardar. El punto en la pestaña sí indica cambios sin guardar.

Si esta etapa se publica directamente desde ChatGPT, revisar git status y el historial antes de sincronizar Windows; el commit remoto no actualiza automáticamente esa copia local.
