# 07. Quirks y bugs

Catálogo numerado de rarezas del cliente original. El prefijo `Q-C` las separa de las del
DataServer (`Q-nn` en su [08](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md)).

## Regla

En el DataServer los quirks se **corrigen** y se documentan (D-10 del roadmap). En el cliente hay
dos clases y la regla cambia según cuál:

- **Contrato de datos:** rarezas que definen cómo se leen los archivos reales de `Data\` o qué
  bytes salen al cable. **Se reproducen**: si MuRet las "arreglara", leería mal los archivos del
  cliente original o el servidor lo rechazaría (`divergencias-cliente-servidor.md`, "Regla para la
  reconstrucción").
- **Bugs internos:** desbordes, estado que queda corrido, errores silenciosos. **Se corrigen**,
  con un test que muestra el comportamiento viejo y el nuevo, igual que en el DataServer.

## Catálogo

| # | Dónde | Qué pasa | Clase | MuRet | Ola |
| --- | --- | --- | --- | --- | --- |
| Q-C01 | `Item.bmd`, `0x54b9e0` | un parche `EB 66` en el binario anula la verificación del checksum: se carga aunque no coincida | contrato | se calcula y se informa, pero no se rechaza el archivo | CL1 |
| Q-C02 | `TerrainHeight.OZB`, `0x5f9020` | las alturas se leen desde BMP + 1080, no desde el offset de píxeles que declara la cabecera | contrato | se reproduce | CL1 |
| Q-C03 | corte, `0x4e893d` | con 1 o 2 bytes de un C2/C4 al final del buffer, el tamaño se lee de bytes viejos; si da 0, el resto no se procesa y el flujo queda corrido | bug | el lector solo mira bytes recibidos y espera los que faltan | CL2 |
| Q-C04 | serial de recepción, `0x4e1d50` | si el serial no coincide, guarda el recibido sin `++`: el siguiente tampoco coincide y el desfase queda para siempre (no desconecta ni descarta) | bug | se resincroniza con el recibido + 1 y se loguea una vez | CL2 |
| Q-C05 | `0x4e1d50` | el buffer descifrado mide ~2.046 bytes en la pila y el corte acepta C4 de hasta 0x2000: un C4 de más de ~2,8 KB del servidor desborda la pila. *Estático*, sin probar | bug | tamaño máximo explícito; lo que lo supera se rechaza | CL2 |
| Q-C06 | motor C1/C2 | más de 0x800 bytes en un paquete: el resto se ignora en silencio y sale un paquete truncado | bug | error del escritor; nunca sale un paquete truncado | CL2 |
| Q-C07 | `Text.bmd`, `0x5495e0` | el registro 7 no tiene `\0` dentro de sus 300 bytes | contrato | los textos de largo fijo nunca suponen terminador | CL1 |
| Q-C08 | contenedor guardia de `Skill.bmd`, `0x40f600` | la tabla se guarda ofuscada en memoria con llamadas a `rand()` | anticheat | no se porta: la tabla queda en claro | CL1 |
| Q-C09 | capa FoxAnti (`contracts-inbound.md`) | no se valida ningún largo: el `count` de la lista contra el paquete (lee hasta ~1 KB de más y desborda el buffer temporal), `len < 5` en `reset_queue` (un `C1 04 F3 11` tumba al cliente), la clasificación lee `pkt[2..3]` sin mirar el largo, y `k0 ≥ 32` saldría de la KeyTable | bug | se validan; con paquetes bien formados el cable y lo que llega al dispatcher son idénticos | CL2 |
| Q-C10 | rearmado C3 → C1 | el paquete rearmado declara `n` y ocupa `n + 1` (C4: `n + 2`) | interno | el paquete descifrado lleva su tamaño real; se documenta en el test | CL2 |
| Q-C11 | drenaje de `FD_WRITE`, `0x4e87b0` | aritmética anómala con varios envíos parciales seguidos | bug | cola de envío con un cursor, probada con parciales `{2, 2, -1}` | CL4 |
| Q-C12 | cargadores de tablas | si falta un archivo: `MessageBox`, `WM_DESTROY`, y sigue cargando las demás | bug | error explícito, el cliente no arranca sin sus datos | CL1 |
| Q-C13 | dispatcher | sin FoxAnti nadie llama al dispatcher: el cliente no procesa paquetes | diseño | el dispatcher es del cliente; la ofuscación es una capa aparte | CL2 |
| Q-C14 | `Data\Object34/35/55` | 7 archivos `.OZJ` contienen un TGA, no un JPEG (`assets_manifest.csv`, "TGA con extension OZJ") | contrato | el cargador de OZJ mira el contenido, no solo la extensión; cómo los trata el original falta leerlo | CL1 |
| Q-C15 | `mumsg.dll`, `CMsg` | lee cada entrada en un buffer de 256 bytes y escribe el `\0` en `buffer[len]` (`decompilacion-maxima/INDEX.md` §3) | bug | largo validado; el texto no se trunca ni desborda | CL1 |
| Q-C16 | primer paquete al CS | en las 64 conexiones del `muproxy.log` el cliente manda `A9 00 00 01` (con XOR32) **antes** del saludo del CS; en el GS manda `A9 01 00 08` diez veces. Lo arma `main.exe` (emisores `0x415be0`, `0x422990`, `0x424230`); qué es, sin identificar | a confirmar | se reproduce tal cual hasta saber qué es | CL4 |
| Q-C17 | capa FoxAnti, códigos de servidor | `parse_f4_02` suma al código un delta por grupos de 11 (`0x190`, `0x172`, `0x154`, …) y `encode_outbound` lo resta con una escalera que **no tiene** el `0x154`: la ida y vuelta cierra para los códigos 0 a 20, desde 21 el `F4 03` sale con otro código (21 → 51) y desde 98 la entrada queda en 0. El laboratorio solo usa el 0. *Análisis* sobre el código portado de la FoxAnti mínima | contrato del laboratorio, a confirmar | `Network::Obfuscation` lo reproduce byte a byte y un test lo deja escrito | CL2 |
| Q-C18 | `decode_inbound`, antes del corte | decide el `0xAA` por el primer paquete de cada lectura de `recv` y lo aplica (o no) a la lectura entera; `parse_f4_02` además reescribe la lectura entera y lo que venga detrás de la lista queda en cero. *Análisis*: si el servidor manda en claro los tipos exentos (lo que la clasificación supone), uno que llegue pegado detrás de otro paquete se lee mal. En las capturas no pasa: el CS manda un paquete por vez y en el GS no hubo exentos | bug | se decide por paquete con el primer byte del cable (`6B`/`68`/`69`/`6E` con `0xAA`, `C1`/`C2` exentos) | CL2 |
| Q-C19 | terreno, `0x5fa830` | una capa 2 de valor 255 en una celda con alfa no se trata como vacía: el byte se lee sin signo y se dibuja el slot `255 + 0x23 = 0x122` de la tabla de texturas, que no es un tile (en la sesión, una textura de 32 px de ancho), con el alfa de cada esquina. **Verificado** en 3 celdas de la traza de Lorencia (alfa de 0 a 0,3, casi invisible) | visible, a confirmar | se reproduce en el modo fiel; qué textura es el slot `0x122` se lee en CL7.R | CL7 |
| Q-C20 | dibujo del personaje, `0x509340` | la mascota (0,5 keys) y las alas (0,25) avanzan su animación por frame dibujado, no por paso: con más FPS van más rápido. A 21,33 FPS la mascota da 10,7 keys por segundo. **Verificado** en las trazas de GL ([06](06-render-escenas-y-ui.md#cuánto-avanza-la-animación)) | bug | avanzan por tiempo, a la velocidad que tienen a 21,33 FPS; un test con 21,33 y 240 FPS | CL6 |
| Q-C21 | `0x4ec200`, escena 4 | en la selección, la prueba de "terminó la acción" usa el frame + 2: avisa dos keys antes, pero la vuelta a la key 0 usa el frame real. *Estático*; qué hace el llamador con ese aviso, sin leer | visible, a confirmar | se reproduce en el modo fiel hasta saber qué cambia en pantalla | CL6 |
| Q-C22 | reloj del frame, `0x4eb620` | `timeGetTime` (ms desde que arrancó Windows) se guarda en un `float`: desde las 4,7 horas de Windows prendido (2²⁴ ms) pierde milisegundos (de a 2 ms; a los 3 días, de a 32), y el brillo de los items, que usa ese reloj, avanza a saltos. *Estático* | bug | el reloj del frame cuenta desde que arranca el cliente, con precisión de ms | CL6 |

## Fuera de alcance

El anticheat (watchdog de ventanas, timer 1001, mensaje `0x2B11`, hash anti-tamper en idle, scans
y checksums de FoxAnti) **no se porta** ni se estudia más allá de lo que hace falta para
reconocerlo (`AGENTS.md`, "Fuera de alcance").

## Cómo se agrega un quirk

Igual que en el DataServer: número nuevo, dirección del original, evidencia (archivo del repo de
research), clase, qué hace MuRet y en qué ola tiene su test.
