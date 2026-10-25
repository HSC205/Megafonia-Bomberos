# Central 0.2.1 con audio real

El 25/10/2026 Héctor confirmó el funcionamiento del montaje con DFPlayer y altavoz tras corregir su alimentación y reconectar BUSY. También probó el menú de la botonera y «Reproducir todos los mensajes». La central ya reproduce audio real; los apartados anteriores de simulación se conservan como historial.

## Conexiones

| DFPlayer | ESP32-S3 o destino |
|---|---|
| VCC | 5 V verificados en el montaje |
| GND | GND común |
| TX | GPIO16, RX de Serial1 |
| RX | GPIO17, TX de Serial1, con 1 kΩ en serie |
| BUSY | GPIO4, activo en LOW |
| SPK1 y SPK2 | Los dos terminales del altavoz, sin unir SPK a GND |

Se empleó un altavoz de 6 Ω y 10 W. Su potencia nominal no es la potencia que entrega el módulo. El volumen del código conservado es 10/30 y se ajusta mediante VOLUMEN; se sugirió 20 durante la prueba, sin confirmación del valor finalmente usado. Para el amplificador de megafonía queda pendiente la salida DAC y su integración; SPK no se conecta directamente a una entrada de línea.

## Alimentación y diagnóstico

El terminal VIN elegido en este montaje no proporcionaba los 5 V necesarios al DFPlayer. Al corregir la alimentación, el módulo respondió y reprodujo. No atribuir este fallo a incompatibilidad del módulo: no quedó demostrado que cambiar el inicio UART fuera necesario. Verificar la alimentación en VCC y GND antes de diagnosticar protocolo o archivos.

La versión conservada espera 3 s y usa begin sin ACK ni reset. Ese retorno no prueba comunicación. La consulta UART, el inicio por BUSY y el sonido real aportan comprobaciones diferentes. El recuento de archivos observado no constituye un inventario validado de MP3.

## Archivos y reproducción

En /MP3: 0009_inicio.mp3 para tono inicial, 0001 a 0008 para los ocho avisos y 0020 para tono final. Se propuso duplicar el antiguo 0000 como 0009, conservando copia de seguridad; no depender del índice cero. Los sufijos descriptivos pueden mantenerse después de los cuatro dígitos.

Cada aviso reproduce tono inicial una vez, voz según las repeticiones configuradas y tono final una vez. TODOS recorre los ocho avisos con esa secuencia completa. El fin depende de BUSY, ya no de dos segundos simulados. Un nuevo aviso sustituye al anterior y PARAR detiene la reproducción.

La secuencia pura se comprobó con una prueba de host. El usuario confirmó audio real y TODOS; las pruebas específicas de pérdida de red, persistencia tras apagar y cada condición de error siguen pendientes.

## Pantalla futura de la central

Registrar para la próxima mejora una pantalla propia que muestre la información útil del monitor serie: conexión e IP, aviso en curso, fase de tono o voz, repetición actual y total, estado de reproducción y último error. Mantener una presentación legible, sin volcar todo el registro. Modelo, medidas, pines y código aún por definir.

## Pendientes

- Cambiar SIN CONFIRMAR por SIN RESPUESTA en la botonera.
- Reservar la IP del router y probar desconexión y recuperación.
- Comprobar persistencia de repeticiones tras apagar.
- Integrar salida de línea y amplificador de megafonía.
- Añadir, si se mantiene la idea, anuncio de arranque activable desde el menú.
- Diseñar la pantalla de diagnóstico de la central.

No se publican secrets.h ni credenciales. La copia de Windows debe sincronizarse conservando antes sus cambios locales; publicar en GitHub no la actualiza automáticamente.
