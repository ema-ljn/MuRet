# 10. Lo que falta saber

Huecos de conocimiento que quedan y cómo cerrarlos. Cada uno dice qué evidencia faltaría para
pasarlo de *seam* o *inferido* a Verificado. Son los puntos donde MuRet, al llegar a esa ola, tiene
que conseguir una captura o un volcado antes de fijar bytes.

| # | Hueco | Nivel hoy | Cómo se cierra | Ola que lo necesita |
| --- | --- | --- | --- | --- |
| H-1 | bytes exactos de la respuesta de `0x06` (cargar personaje), `0x3BC` bytes | *seam* | captura del wire GS↔DS en la VM durante un login real | 5 |
| H-2 | layout exacto de `0x08` (abrir bodega), `0x4C8` bytes, campos Money/DbVersion/"pw" | *seam* | captura del wire al abrir la bodega | 5 |
| H-3 | que Life/Mana se multiplican por 10 en `0x06` | *inferido* | comparar el valor de la base con el byte del wire | 5 |
| H-4 | que el u16 `+0xE` de `0x01` es el índice de usuario del GS (`0x12C0`=4800 en la captura) | *inferido* | captura con varios GS para ver si cambia con el índice | 5 |
| H-5 | que un C2 `> 0x2000` cierra por recv de longitud cero (Q-03) | *inferido* | mandar el paquete y ver el cierre | 1, 3 |
| H-6 | cuerpo de `MX_*` y `SP_CHARACTER_TRANSFER` (CustomDB, no está en el binario) | *seam* | leer el CustomDB del repack o el esquema de la VM | 6 |
| H-7 | qué resolución de reloj corre en el laboratorio (cota de throughput de 64 vs 1000 msg/s) | *inferido* | medir con `NtQueryTimerResolution` o cronometrar una ráfaga ([03](03-hilos-y-concurrencia.md)) | 4 |
| H-8 | cuerpo de `0x4035d0` (recorrido de la lista de contextos en el cleanup) | *seam* | re-decompilar con más límites o leer el asm a mano | 3 |
| H-9 | builder de la lista de personajes de `0x01` (`0x40b64b`), reconstruido a mano | *inferido* parcial | captura de la lista con 0, 1 y varios personajes | 5 |

## Cómo se usa esta lista

Antes de implementar un head o un flujo que dependa de un *seam*, MuRet consigue la evidencia que
dice la fila y la agrega al research (con la fecha y el archivo de `vm_evidence/`). Un test de
paridad sobre un flujo con *seam* abierto **no es confiable** hasta cerrarlo: puede pasar contra la
reconstrucción y fallar contra el GS real.

La forma de conseguir casi toda la evidencia que falta es la misma: levantar el DataServer original
y un GameServer 1.02 en la VM2003, capturar el tráfico del puerto 55960 y volcar la base antes y
después de cada operación. Eso es parte de la ola 8 (distribuido con GS real y replay de paquetes),
pero las capturas puntuales (H-1…H-4, H-9) se pueden hacer antes, cuando se necesiten para la ola 5.

## Estado del research

Con los documentos 01–09 el comportamiento del DataServer está descrito a nivel suficiente para
empezar a portar las olas 0 a 4 sin más evidencia. Las olas 5 en adelante (handlers con bytes
exactos, SQL real, gremios) necesitan cerrar los *seam* de arriba a medida que se llega a cada una.
