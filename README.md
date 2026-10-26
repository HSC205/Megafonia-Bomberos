# Megafonia-Bomberos

Sistema de avisos por megafonía con dos ESP32, OLED y DFPlayer Mini.

## Enlace directo sin router

La central ESP32-S3 crea su propia Wi-Fi de 2,4 GHz; la botonera ESP32-WROOM-32U se conecta directamente a ella. El router no interviene en el envio de avisos. El acceso Ethernet desde la red del parque queda para una etapa posterior.

- Central 0.3.0: red MEGAFONIA-CENTRAL y direccion 192.168.4.1:8080.
- Botonera 0.4.0: ocho avisos, menu, repeticiones, SIN CONFIRMACION y firma by HSC205.
- Archivo privado en cada proyecto: include/enlace_privado.h. Copiar enlace.example.h y poner los mismos valores en ambas placas. No publicarlo.
- No hay que cambiar el cableado del montaje probado ni los MP3.

Instalacion, arquitectura y pruebas pendientes en [docs/enlace_directo.md](docs/enlace_directo.md).

La etapa anterior validada fisicamente por el usuario esta conservada en el commit 812e063. La nueva topologia requiere comprobarse con las dos placas, incluido funcionamiento sin router y reconexion.
