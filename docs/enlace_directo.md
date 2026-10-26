# Enlace directo entre botonera y central

## Objetivo

La central ESP32-S3 crea una red Wi-Fi propia de 2,4 GHz y la botonera se conecta directamente a ella. Los avisos no dependen del router del parque ni de que su banda de 2,4 GHz este habilitada. Se conserva HTTP y el protocolo de eventos existente; no se utiliza ESP-NOW.

- Central 0.3.0: modo AP, SSID MEGAFONIA-CENTRAL, IP fija 192.168.4.1/24, puerto 8080, canal 6, hasta dos clientes.
- Botonera 0.4.0: modo STA, conectada solo a esa red, destino http://192.168.4.1:8080.
- Red protegida por clave compartida en include/enlace_privado.h, excluida de Git en ambos proyectos. Copiar la plantilla enlace.example.h y usar los mismos valores en las dos placas, o utilizar los dos archivos privados preparados en el paquete local.
- No se usan las credenciales WIFI_SSID/WIFI_PASSWORD del router. Los secrets.h antiguos dejan de ser necesarios para estos firmwares.

La central atiende HTTP sin depender de WL_CONNECTED, que corresponde al modo estacion. El numero de clientes se imprime en el monitor y en el JSON de estado. El dato clientes no prueba por si solo que el cliente sea la botonera ni que una orden se haya reproducido.

La futura conexion Ethernet al router se incorporara despues; ahora no hay Ethernet, puente, encaminamiento ni interfaz movil. Al anadir LAN habra que evitar que su subred coincida con 192.168.4.0/24. La Wi-Fi propia de la botonera debe mantenerse aunque se desconecte LAN.

## Funcionamiento conservado

Ocho avisos, antirrebote, pulsacion larga para menu, botones 7 y 8 para navegar, 1 para aceptar y 2 para volver, repeticiones de 2 a 10 guardadas con Preferences. SecuenciaAudio.h y el control de audio/BUSY se conservan. Un nuevo aviso sustituye al anterior y TODOS recorre los ocho.

El cambio visual solicitado es SIN CONFIRMACION con parpadeo de cuatro segundos y firma by HSC205 al pie de todas las vistas. Las ayudas del menu se han movido para no ocupar la firma. Los textos antiguos de PC pasan a Central. WiFi OK acredita asociacion a la central; el ultimo acuse se mantiene como dato historico y se limpia al perder el enlace.

## Instalacion

Copiar los archivos a firmware/central y firmware/botonera, conservando las carpetas include y src en su nivel correcto. Los dos include/enlace_privado.h deben coincidir. Compilar y cargar primero central por COM y despues botonera. Conservar la alimentacion y el cableado previamente validados. La red aparece despues de inicializar DFPlayer.

No hacer stash del archivo privado con una opcion que incluya los ignorados; no publicarlo. Si se usa una version local con otro volumen, conservar ese ajuste explicitamente: Git llevaba 10/30.

## Pruebas que debe hacer el usuario

1. Conectar ambas placas, esperar RED PROPIA OK y CENTRAL WIFI OK.
2. Probar los ocho avisos, sustitucion, menu, repeticiones y TODOS.
3. Comprobar SIN CONFIRMACION y by HSC205 en OLED.
4. Repetir la activacion sin router. El router ya no debe intervenir en el camino.
5. Reiniciar la central, esperar reconexion y lanzar una pulsacion nueva. No reproducir automaticamente ordenes antiguas.
6. Probar el alcance en la ubicacion real. La cobertura directa entre control y rack aun no esta medida.

La clave Wi-Fi no reemplaza la autenticacion de aplicacion y los roles que se anadiran para el acceso movil. No se ha probado fisicamente esta nueva topologia al preparar el codigo.

## Comprobaciones al preparar el codigo

Las pruebas de host comprueban menu, repeticiones, cancelacion, progreso, parpadeo, firma en todas las vistas y limites de dibujo. Tambien se prueban el inicio AP con IP fija, servidor activo sin clientes, reintento tras fallo y continuidad del bucle de audio. Se compara con la version anterior que la logica del DFPlayer, BUSY, secuencia de pistas y recepcion de eventos no se haya modificado. Estas comprobaciones no sustituyen la prueba de Wi-Fi y audio en las placas.

La compilacion de central 0.3.0 se completo con SUCCESS usando Espressif32 6.12.0 y Arduino-ESP32 2.0.17.

La compilacion de botonera 0.4.0 tambien se completo con SUCCESS, con Espressif32 6.12.0, Arduino-ESP32 2.0.17 y U8g2 2.36.18. No se han cargado placas desde este entorno ni se ha validado el alcance fisico.
