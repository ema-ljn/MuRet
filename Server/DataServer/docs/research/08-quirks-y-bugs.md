# 08. Quirks y bugs

Catálogo numerado de rarezas y defectos del original. Cada uno dice qué lo dispara, qué efecto
tiene, dónde está la evidencia, qué hace MuRet y qué test lo fija. Los defectos de concurrencia
tienen su desarrollo en [03](03-hilos-y-concurrencia.md) (C-1…C-7); acá se listan para tenerlos
todos juntos.

Decisión general de MuRet (de la memoria del proyecto): **se arreglan los defectos, pero cada uno
se documenta y se cubre con un test que demuestra el comportamiento viejo y el nuevo.** La
compatibilidad se define contra el comportamiento observable "sano", no contra el bug, salvo que un
GS real dependa del bug (no se encontró ninguno).

## Q-01. Los gremios nunca funcionan

**La más importante.** Las dos `CQuery` de gremios (`0x63f3848` tabla `Guild`, `0x6411e78`
miembros) **nunca llaman a `Connect`**: `OnStart` abre 7 conexiones y ninguna es la de gremios
([02](02-arranque.md), [06](06-sql-y-base.md)).

- **Disparador:** cualquier head de gremios (`0x30`–`0x33`, `0x37`, `0x38`, `0x40`).
- **Efecto:** toda SQL de gremios corre sobre una conexión muerta y **falla**. La rama de éxito de
  `0x30` (que agregaría el gremio a la caché RAM) no se alcanza nunca, así que la caché está
  **siempre vacía**. El `0x30` devuelve result 3 (el quirk), el `0x37` no responde, el `0x38`
  manda un broadcast inalcanzable, y el `0x40` solo manda las dos cabeceras con count 0. Para el
  jugador: **los gremios no existen** en este DataServer.
- **Evidencia:** ausencia de `Connect` sobre esas dos instancias en `OnStart`; caché vacía
  confirmada en el `.data` vivo.
- **Consecuencia de segundo orden:** desactiva C-1 (la inversión de locks de gremios). Si alguien
  "arregla" Q-01 sin arreglar C-1, aparece un deadlock.
- **MuRet:** los gremios se implementan de verdad (ola 7). Se conecta la base, se llena la caché, y
  el sync corre sin locks cruzados.
- **Test:** crear gremio → aparece en la caché y en la base; `0x40` lo devuelve. Un test de
  regresión documenta que el original respondía result 3.

## Q-02. Paquetes de 0x801 a 0x2000 desbordan el buffer del consumidor

- **Disparador:** un C1/C2 con `size` entre `0x801` y `0x2000`.
- **Efecto:** el parser lo encola (cabe en el buffer de recepción de `0x2000`), pero el consumidor
  lo copia a un buffer de pila de `0x800` sin comparar tamaños → **desbordamiento de pila**
  ([04](04-red.md#dominio-efectivo-del-tamaño)). Ningún head del GS 1.02 llega a ese tamaño, pero
  el servidor no se protege.
- **Evidencia:** buffer `unsigned char buf[0x800]` en `0x402da0`; parser acepta hasta `0x2000`.
- **MuRet:** tamaño máximo explícito y validado; nada desborda; se rechaza con log.
- **Test:** mandar un paquete de `0x1000` bytes y comprobar que MuRet lo rechaza sin corromper
  memoria (ASan).

## Q-03. Un C2 mayor a 0x2000 cierra la conexión

- **Disparador:** C2 con `size > 0x2000`.
- **Efecto:** no se puede completar (el buffer es `0x2000`), el recv se re-postea con `len = 0` y
  la recepción de longitud cero cierra la conexión. *Inferido*; se confirma con una captura.
- **MuRet:** rechazo explícito con log, sin depender de un recv de longitud cero.

## Q-04. El `head`/`size` se lee de bytes viejos (P-1)

- **Disparador:** un paquete completo que deja 1 o 2 bytes de cola en el buffer.
- **Efecto:** el parser intenta otra vuelta con esa cola y lee `head`/`size` de bytes que todavía
  no llegaron (del paquete anterior). No hay lectura fuera de rango, pero el servidor puede actuar
  sobre un `head` inventado ([04](04-red.md#la-máquina-de-estados)).
- **MuRet:** el parser solo mira bytes efectivamente recibidos. **Se arregla.**

## Q-05. El error de la query de arranque viaja al GameServer como dato

- **Disparador:** `SELECT ItemCount … GameServerInfo` falla al arrancar o en el login `0x00`.
- **Efecto:** el `ItemCount` que se manda al GS es `-103` (exec falló) o `-101` (sin fila), porque
  los códigos de error de `CQuery` se tratan como enteros ([06](06-sql-y-base.md)). El servidor
  **sigue** sirviendo.
- **MuRet:** se preserva el valor en la respuesta por compatibilidad, pero se loguea como error y
  se distingue ausencia de valor.

## Q-06. Credenciales y conexiones muertas

- `Admin`/`BobStudio` están hardcodeadas pero el DSN usa `Trusted_Connection=Yes`, así que el
  driver las ignora ([02](02-arranque.md)). El login SQL `Admin` ni existe.
- Dos `CQuery` conectan y nadie las usa (`CItemExt` `0x63ebec8`, `EventDBSet` `0x6402b40`).
- **MuRet:** cadena de conexión en config; sin conexiones sin usar.

## Q-07. SQL por concatenación de `%s`

- Casi todas las SQL arman la cadena con `%s` sin escapar. Inyectable con una comilla simple. En el
  modelo del original no importa (el único cliente es el GS, que valida), pero es deuda
  ([06](06-sql-y-base.md)).
- **MuRet:** todo parametrizado.

## Q-08. `ds_FindFreeGameIdSlot` devuelve el slot 0 cuando la query falla

- **Disparador:** la consulta que busca el primer `GameID` libre de la cuenta falla.
- **Efecto:** devuelve 0, o sea el primer slot, que puede **pisar** un personaje existente.
  *Inferido* en el impacto; la rama de retorno 0 en fallo es Verificado.
- **MuRet:** distinguir "no hay slot" de "falló la consulta"; nunca devolver 0 por defecto.

## Q-09. El cierre desde la UI no cierra los sockets ni drena la cola

- Al cerrar la ventana se liberan los slots pero no se cierran los sockets ni se procesan los
  mensajes encolados; el proceso termina enseguida ([02](02-arranque.md), C-6 en
  [03](03-hilos-y-concurrencia.md)). Los mensajes en vuelo se pierden.
- **MuRet:** apagado ordenado (cerrar el listener, drenar la cola, esperar los envíos, cerrar).

## Q-10. El port check filtra un socket por sonda

- El segundo listener acepta y **no cierra** el socket ([04](04-red.md#dos-sockets-de-escucha)).
  Fuga deliberada.
- **MuRet:** sonda de salud sin fugas.

## Defectos de concurrencia (ver [03](03-hilos-y-concurrencia.md))

| # | Qué | Alcanzable hoy | MuRet |
| --- | --- | --- | --- |
| C-1 | inversión de locks gremios↔global → deadlock | no (lo tapa Q-01) | sync sin locks cruzados |
| C-2 | `pendingOps` no cuenta los envíos pendientes → fuga o use-after-free | raro (GS que no lee) | contador por cada op posteada |
| C-3 | un OVERLAPPED de envío reusado, datos en la pila | raro (buffer lleno) | cola de envío, buffers propios |
| C-4 | cierre con error filtra el contexto; completion vieja cierra el slot reusado | sí, en desconexión abrupta | id con generación |
| C-5 | el log no tiene lock | sí | logger seguro |
| C-6 | el cierre de la UI no toma el lock global | sí, sin efecto | apagado ordenado |
| C-7 | un hilo de sync de gremios por `0x40`, sin tope | sí (cabeceras intercaladas) | sync en el hilo de lógica |

## Cómo se usan estos números

Cada quirk es un caso de prueba. En las olas donde MuRet toca el área, el test correspondiente
demuestra primero el comportamiento del original (sirve de documentación ejecutable) y después el
de MuRet. Los `Q-0x` y `C-x` se citan desde los demás documentos y desde el
[roadmap](../../../../docs/superpowers/specs/2026-10-25-dataserver-port-design.md).
