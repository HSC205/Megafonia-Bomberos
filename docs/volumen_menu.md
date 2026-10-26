# Volumen desde el menú

ACTUALIZACION: VOLUMEN EN MENU
Central 0.3.1 / Botonera 0.4.1

Este ZIP solo contiene los dos main.cpp y estas instrucciones.
Mantener los archivos include/enlace_privado.h que ya funcionan en ambas placas.

1. Copiar firmware/central/src/main.cpp a la carpeta src de TU proyecto central,
   sustituyendo el main.cpp anterior. Cargar en el ESP32-S3 de central.
2. Copiar firmware/botonera/src/main.cpp a src de TU proyecto botonera,
   sustituyendo el main.cpp anterior. Cargar en el ESP32 de botonera.
No copiar ambos archivos en la misma carpeta ni aceptar nombres con (1).
Abrir cada proyecto por separado en Visual evita cargar el programa equivocado.

MENU: mantener boton 1 unos 5 s. Ir a Volumen con 7/8 y aceptar con 1.
Al entrar se consulta el volumen REAL CONFIGURADO en la central.
7 baja, 8 sube, 1 guarda y aplica; 2 vuelve sin guardar cambios pendientes.
Rango 0..30; 0 significa silencio. No cambia mientras se edita: solo al confirmar.
El volumen se conserva en la central tras apagarla.
La primera vez, sin ajuste guardado, arranca a 10/30. Si antes cambiaste el
volumen a mano, selecciona ahora el que prefieras desde el menu.
GUARDADO confirma que central guardo el ajuste y envio la orden al DFPlayer;
no es una medicion del sonido ni una confirmacion UART del modulo.
Si no hay respuesta, muestra SIN CONFIRMACION; pulsa 1 para volver a consultar.
Un timeout puede ocurrir despues de aplicar el cambio: consultar verifica el valor.

by HSC205 solo aparece en espera (pantalla principal) y Version.
OLED monocroma: opcion seleccionada y aviso con texto negro sobre fondo blanco.
No se necesitan colores, librerias nuevas, cambios de cableado o de MP3.

VERIFICACION: compilacion real de ambos firmwares correcta; pruebas de menu,
limites, persistencia y fallos realizadas en ordenador. Prueba en placas pendiente.
Probar volumen bajo, subir, apagar central y comprobar que se conserva.
