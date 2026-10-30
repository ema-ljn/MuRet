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
| Q-C09 | capa FoxAnti | `k0`, `count` y `len` no se validan (comportamiento indefinido con entradas raras) | bug | se validan; para paquetes bien formados el wire es idéntico | CL2 |
| Q-C10 | rearmado C3 → C1 | el paquete rearmado declara `n` y ocupa `n + 1` (C4: `n + 2`) | interno | el paquete descifrado lleva su tamaño real; se documenta en el test | CL2 |
| Q-C11 | drenaje de `FD_WRITE`, `0x4e87b0` | aritmética anómala con varios envíos parciales seguidos | bug | cola de envío con un cursor, probada con parciales `{2, 2, -1}` | CL4 |
| Q-C12 | cargadores de tablas | si falta un archivo: `MessageBox`, `WM_DESTROY`, y sigue cargando las demás | bug | error explícito, el cliente no arranca sin sus datos | CL1 |
| Q-C13 | dispatcher | sin FoxAnti nadie llama al dispatcher: el cliente no procesa paquetes | diseño | el dispatcher es del cliente; la ofuscación es una capa aparte | CL2 |
| Q-C14 | `Data\Object34/35/55` | 7 archivos `.OZJ` contienen un TGA, no un JPEG (`assets_manifest.csv`, "TGA con extension OZJ") | contrato | el cargador de OZJ mira el contenido, no solo la extensión; cómo los trata el original falta leerlo | CL1 |
| Q-C15 | `mumsg.dll`, `CMsg` | lee cada entrada en un buffer de 256 bytes y escribe el `\0` en `buffer[len]` (`decompilacion-maxima/INDEX.md` §3) | bug | largo validado; el texto no se trunca ni desborda | CL1 |

## Fuera de alcance

El anticheat (watchdog de ventanas, timer 1001, mensaje `0x2B11`, hash anti-tamper en idle, scans
y checksums de FoxAnti) **no se porta** ni se estudia más allá de lo que hace falta para
reconocerlo (`AGENTS.md`, "Fuera de alcance").

## Cómo se agrega un quirk

Igual que en el DataServer: número nuevo, dirección del original, evidencia (archivo del repo de
research), clase, qué hace MuRet y en qué ola tiene su test.
