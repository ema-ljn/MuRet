# CL9 H1/H9: reparación y límites del cierre

Esta entrega continúa la auditoría del 14 de noviembre sobre `main@96e3fc84f009cb0eb8955e92d1f6567d1cb24cc2`.
La rama es `codex/cl9-h1-h9-cierre`: un PR consolidado, commits por concepto y sin merge automático.
El [plan de trabajo](2026-11-14-cl9-h1-h9-cierre.md) distingue reparación propia, integración y validación.
Los porcentajes históricos por bytes citados no miden equivalencia funcional.

**Criterio de Emanuel, reafirmado durante esta tarea:** fidelidad al ASM y al pseudocódigo; ante discrepancia,
prevalece el ASM. La VM es la validación posterior. Una divergencia demostrada permanece abierta aunque
los tests pasen; documentar una política diferente no la convierte en un comportamiento equivalente.

## Fuente y método

Fuente primaria fijada: [mu-server-repack-research-102i@14f0f931](https://github.com/EmaNLJN/mu-server-repack-research-102i/tree/14f0f931f68776dd103927f58c4359dc29ced712/client-research/mainexe-vmxp-c32bbd5f),
exports individuales `decompiled/functions/<VA8>_FUN_<VA8>.asm` y `.c`.
El PE de referencia tiene 3.058.308 bytes, ImageBase `0x00400000` y SHA-256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
El ASM prevalece sobre el pseudocódigo. Se leyeron bytes del archivo para constantes, tablas y vtables;
no se ejecutó, cargó ni emuló el original ni sus DLL. No se trasladan goldens ni certificaciones de otra build.
Sólo se ejecutan los binarios propios de MuRet. VM y servidor compatible quedan para el final.

Se aplicaron `cpp-pro`, las reglas C++ del repo, depuración sistemática, pruebas de regresión antes del
arreglo y revisión independiente con **Astra 6 high**, solicitada por Emanuel. Context7 se consultó para
`SDL_MouseButtonEvent.clicks`, invalidación de `std::map::erase`, `try_emplace` y conversión integral C++20.
Esos contratos de librería no sustituyen la evidencia del original.

## H1: lo reparado y su observador

| Ruta | Contrato ASM y cambio | Prueba efectiva |
| --- | --- | --- |
| `DF`, empujón tipo `0xf1` | `0x4c6c9a` llama `0x51fe10(posición, 0)`. Se crean veinte intentos de `5c5/2`, con dos sorteos antes de cada intento aun sin slot; cinco adicionales por alta efectiva. El helper `World::CreatePushBurst` reutiliza el pool existente. | `ShownEquipmentTest`: pool vacío, parcialmente libre y lleno; conserva celdas/slots anteriores y observa avance CRT exacto. |
| Creación/paso/dibujo de `5c5/2` | `0x5404ad..0x5404ca`: duplica escala y triplica el vector local; se conserva el redondeo FST antes de multiplicar/rotar. Usa los kernels existentes y `Effect\\Spark02.jpg`, slot `0x5c5`, ya cargado globalmente. | `NoriaParticlesTest`: creación con distintas semillas, paso y dibujo del subtipo. El flag0 no crea la explosión opcional `5ed` del caller de combate. |
| Emote del héroe transformado | `0x576abf → 0x4ea030` consulta el modelo del actor. `GameChat` ahora ofrece primero la fauna cargada del mundo y después la biblioteca base, también antes del primer Render. Animación fuera del modelo conserva el no-op y ambos envíos del héroe parado. | `GameChat.ATransformedHeroUsesItsLoadedFaunaModelBeforeSendingTheChat`; `GameSceneEmoteTest` atraviesa Step, sesión y FakeSocket con el BMD real: dos acciones `C1 05 18` y el chat, en orden, sin Failure. |
| Aviso de bendición, P-C163 | `0x4e68d3..0x4e6904`: al héroe se le muestra `0x555190(nombreVacío, Text[1828], 1)` en cada aplicación; el OR va después. Otro actor sólo recibe el bit. Los seis callers prestan los recursos efectivos. | `ViewportPlayersTest`: repetición, otro actor sin servicios, texto ausente con prefijo conservado y texto vacío que envejece el log y agrega su fila. |

En el DF, la cuenta total observada incluye ocho sorteos anteriores del handler: `8 + 40 + 5*altas`,
con `altas=min(slotsLibres,20)`. Los dos sorteos de cada intento no pueden moverse dentro de `CreateChecked`.
La creación con flag1 y sus efectos de combate siguen siendo H3; no se declara portada toda CL11.

P-C163 se resolvió por sus writers y consumidores. `0x774300c = 0x76bd1dc + 1828*300` pertenece a la tabla
Text.bmd, escrita indirectamente por `0x5495e0` (o por el loader Text.txt de desarrollo).
`0x5767b6c` es una de 127 anclas inicialmente zero-fill de `.data`: los 156 operandos hallados en ese intervalo
son `PUSH` a consumidores de mensajes. `0x555190` y su sink virtual `0x45bb20` sólo leen el nombre.
No se identificó un writer de nombre por esas bases; esto no demuestra ausencia universal de alias o corrupción.
El texto se obtiene del índice cargado, sin hardcodear la traducción. Si está vacío, la llamada sigue siendo observable.

### Correcciones de la auditoría anterior

**`0x4129f0` pertenece a la transferencia de GameServer, H13.** El owner estático `0x6eed40` publica su
puntero en `[0x6eed7c]`; el setter escribe su string `this+8`. El consumidor `0x412bb0` lo incorpora a `B1/01`
después de `B1/00` y `F1/00`. No tiene consumidor gráfico recuperado. Se corrige el comentario de selección;
no se agrega un renderer ni un cartel inexistente. El nombre ya persiste en el personaje elegido de MuRet.

Los 95 disparadores de emote y las 256 clasificaciones de acción remota ya coincidían en la auditoría estática;
esta entrega conserva sus tablas. Esa comparación no prueba ataques, efectos ni ejecución del original.
La comparación por bytes occidentales 1252 frente a `_mbclen`/CP949 sigue siendo una política documentada.

## H9: lo reparado y su observador

| Ruta | Contrato ASM y cambio | Prueba efectiva |
| --- | --- | --- |
| Destrucción de botones | `0x45ae34..0x45ae47` borra `pressedButton` sólo si coincide con un botón destruido. La limpieza alcanza pestañas, cuadros, cierres F/F6 y reset desde sus callsites reales. | `FriendWindowsTest` y `OldWindowsTest`: cerrar/reabrir con mouse mantenido, botones hijos y segunda respuesta; borrar una ventana conserva la pulsación de otra viva. |
| IDs, wrap y colisión | `0x459ef0`, `0x48f6f2..0x48f84d`, `0x49090c`: incremento DWORD, comparación unsigned y primera inserción. Se conserva el orden duplicado y el asentado del objeto nuevo sin modificar los botones anteriores. | `OldWindowsTest`: bit31, vuelta a cero, clave repetida y posición de botones; `FriendWindowsTest`: borrar Amigos con ID negativo firmado. Identidad de asignación y objetos rechazados retenidos: [ADR0015](../../adr/0015-colisiones-de-ids-de-ventanas.md), Q-C1364. |
| Colas durante Draw | `0x45aa50 → 0x45aa80(1)` drena toda la cola antes del guard de mouse: también procesa teclado, foco, Name/Answer y envíos. El dibujo usa el mismo `PostAndStep`; los Close/Raise del manager esperan su Step. | `FriendWindowsDrawTest` y `GameSceneOldWindowsTest`: orden, envío único, Enter, alta de EDIT con caret actual y corte H10 en el punto correcto. Los frames intermedios usan copias sin envío externo. |
| Doble clic SDL | Windows reemplaza el segundo DOWN por `0x203`: sólo prende `doubleClick` (`0x4b47b6`), no el clic ordinario. El siguiente mensaje lo apaga (`0x4b3444`); también lo consumen los controles. No hay un borrado por tick. | `InputEventsTest`, `GameUiTest` y `GameSceneOldWindowsTest`: clicks2/4 frente a3, estado físico separado, persistencia sin mensaje, consumo sin republicación y actualización pendiente aun tras `ConsumeEdges`. |
| UP+DOWN en un lote | Un DOWN nuevo rearma `leftHeld` aunque la UI lo haya consumido y ambos eventos lleguen antes del siguiente paso. | `GameUiTest.AnUpDownBatchRearmsAHeldFlagConsumedByTheUi`, con `InputState` real. |

El mapa secundario nativo guarda punteros a las pestañas (`0x4a232d..0x4a2348 → 0x4908d0`),
y `0x490a8a` devuelve el objeto aunque su owner haya sido rechazado por el mapa principal. La implementación
inicial de esta rama liberaba ese temporal: la revisión por fidelidad detectó que eso perdía un lookup válido.
El modelo final separa ID nativo de identidad de asignación y retiene el objeto por valor; ambas cosas sobreviven
a Reset porque `0x48ea50` no vacía el mapa secundario. La copia de proyección tiene almacenamiento independiente.
`manager+0x48` (Amigos) y `base+0x44` (padre) sí son IDs: el layout debe conservar la búsqueda del padre anterior
por ID, no sustituirla por el objeto nuevo. Seis regresiones observan lookup, cola, geometría, reset, primera
inserción y copia. Las referencias a asignaciones ya destruidas se tratan como ausentes; no se certifica el
comportamiento de punteros liberados ni la reutilización de direcciones del allocator nativo.

La salida de cascada agotada (`0x48f700..0x48f70a`) sí destruye el objeto recién creado. Se aplica la limpieza
por ID de sus botones antes de retornar 0; se conservan los prefijos previos. `0x4917e0 → 0x4918a0` deduplica
el ID de la caja: una prueba inicial esperaba dos registros aun con ID repetido, y su oráculo se corrigió
contra ASM antes del arreglo. Las dos regresiones distinguen ID reutilizado de pulsación ajena.
El caller `Scenes::GameFriends::OpenABox` ahora presta los globals reales del runtime, como los otros dos
callers UI. `GameFriendsTest.AReceivedNoticeReleasesTheReusedButtonWhenItsCascadeAborts` atraviesa el handler
de resultado con una cascada congestionada; antes del binding sólo fallaba la pulsación que no se limpiaba.

El guard unsigned `0x48ee98..0x48ee9b` devuelve 0 para tipos fuera de 0..10, sin construir ni consumir IDs.
El port conserva ese retorno, incluidos 11 y −1. Sólo los casos conocidos 1/2/6/7 mantienen el corte explícito H10.
Las regresiones distinguen ambos contratos y preservan mapa, orden, foco, pulsación y cola anteriores.

**Otra errata de la auditoría anterior:** C1/C2/C3 de amigos usan XOR32; la solicitud C0 no.
La prueba de escena compara el paquete cifrado real de `0x49d575`. El protocolo de producción no se cambió.
`0x472d30` se documenta como cola de mensajes, no FriendEntry; los mapas `0x4a81f0/0x4aa0b0` pertenecen a H10.

Astra detectó tres problemas en el snapshot de revisión: un identificador de test mal editado, el borrado
incorrecto del doble clic por tick y el rearme omitido tras UP+DOWN. El primero se corrigió y recompiló;
los otros dos tuvieron regresiones que fallaron antes del arreglo. El dictamen de ese snapshot fue solicitar
cambios. La relectura acotada posterior levantó R2/R3 sin nuevos Critical/Important en ese delta, sujeta a los
checks coordinados. Las regresiones correspondientes ya pasan en la suite completa en curso; la validación
dinámica del original sigue pendiente. Otra relectura acotada de Astra confirmó la salida de cascada y el
binding de los avisos recibidos, sin nuevos Critical/Important; no ejecutó los tests por su cuenta.

## Verificación de esta rama

Código propio H1/H9 en `60c5c696` y `fe421138`; fixture gráfico actualizado en `180a0877`.
Build ALL local: GCC14 Debug `-O0 -g0`, warnings como errores y PCH apagado, exit0.

La primera corrida completa tuvo 6.913 aprobados y 59 skips por variables de evidencia sin definir. Se recuperaron
las referencias desde sus commits y se verificaron 47 archivos del corpus de memoria por SHA. La activación
reveló un fixture de terreno obsoleto ya presente en 96e3: omitía Item/Skill/Text, esperaba un corte portado y
seleccionaba el último lote UI. Se corrigió el fixture, conservando cámara, 329 celdas, 25 quads y su orden contra
la misma traza; ahora exige frame sin fallo. La corrida final con los tres corpus y GL obligatorio terminó:

| Verificación local | Resultado observado |
| --- | --- |
| CTest completo, red habilitada y Xvfb sin reset automático | **6.972 aprobados, cero fallos, cero skips**, 148,89 s |
| Dentro de esa corrida: gráficos con `MURET_CLIENT_REQUIRE_GL=1` | 106 casos ejecutados |
| Formato18 e include-cleaner18 | archivos C++ modificados y nuevos sin hallazgos pendientes |
| Corpus antes salteados | 20 memoria/modelos, 25 wire, 14 trazas; archivos fijados por hash |

Un primer intento gráfico encontró un fallo de arranque SDL/Xvfb, anterior al código del test; se repitió
con `SDL_VIDEODRIVER=x11` y `-noreset`, y la corrida completa final pasó. No se cambió el renderer para ocultarlo.
Los comandos de recuperación, variables y repetición están en el
[runbook para el agente con VM](../../../Client/docs/research/scripts/game/capture/RUNBOOK-cl9-vm.md).
El [manifest](../../../Client/docs/research/scripts/game/capture/cl9-vm-evidence.json) lista los 59 casos.

Las capturas de memoria son históricas del build 2f16; las trazas GL archivadas declaran c32. El cable conserva
su procedencia por bytes. Pasar estos tests no acredita una nueva sesión c32 de H1/H9 ni equivalencia universal.
No se ejecutó el original ni la VM. La ejecución remota de Linux/Windows del head publicado se registra en el PR;
no se acredita Windows por este build local ni por el CI anterior de `main`.

## Paquete para continuar en la VM

Por pedido posterior de Emanuel, la entrega incluye `recover_cl9_evidence.py`, unittests sintéticos,
manifest de fuentes/hashes/dependencias, spec UI de lectura y runbook con comandos, precondiciones,
casos nuevos H1/H9 y formato de devolución. Los raw logs, claves, volcados y trazas quedan fuera de MuRet.
El helper recupera archivos y genera referencias matemáticas, sin ejecutar EXE, VM ni CTest.
Su reporte mantiene `native_verified=false` y `tests_launched=false`: la integridad de archivos y la fidelidad
observada del original se comprueban por vías distintas. Las pruebas del helper corren también en CI Linux/Windows.

Verificación local del paquete: 25 unittests, 24 aprobados y uno omitido porque exige NTFS real.
La recuperación real y su repetición idempotente verificaron 62/62 outputs y 51/51 assets; `--check-only`
confirmó el estado sin escribir. Los 59 tests alimentados desde la copia producida por el helper pasaron 59/59.
Astra revisó el paquete: se corrigieron el escape por junction de Windows y la descarga completa
innecesaria al usar fetch. Hay regresiones para contención canónica y recuperación selectiva desde un clon
parcial; la prueba de junction real queda incluida en el job Windows. No se ejecutaron originales para estas comprobaciones.

## Qué sigue abierto y dónde continuar

| Dueño | Trabajo pendiente y criterio de cierre |
| --- | --- |
| H3 | Integrar `18/78` y `18/79` y el caller de ráfaga con flag1, con fauna y recursos reales. No duplicar el helper del empujón. |
| H4/CL11 | Efectos concretos todavía cortados en `ApplyEffectFlags`, alta `45 isNew` y skills; cada efecto conserva su orden, RNG y errores. P-C163 y `5c5/2` ya tienen solución en esta entrega. |
| H7 | Integrar baja del cartel de tienda al salir del viewport (`0x4ca7f0..0x4ca809`, `451630/451430`); el mapa existe en su rama. |
| H2/H12/H13 | Stop de NPC, fábrica/model patches, holder/paneles y transferencia GS conservan su dueño. El setter `4129f0` se valida con `B1/01`, no con dibujo. |
| H10 | Factory1/2/6/7, chats/cartas y sus registros. Las 195 filas históricamente atribuidas a H9 no se vuelven conformes por cambiar el mapa de ventanas. P-C183 sigue abierto. |
| Plataforma / P-C181 | Editor Win32/IME/selección completo no reconstruido. Se conserva TextField Latin1 de 10; no se afirma equivalencia de todo EDIT. |
| VM / P-C184, P-C186a-c, U1 | Comparar GDI/FreeType, quads, RNG del dibujo, mover/cerrar/reabrir y foco tras destruir EDIT contra el HWND real. `0x467710` restaura el proc y libera GDI, pero no llama DestroyWindow/SetFocus: no se inventa una limpieza de foco sin resolverlo. Se necesita servidor compatible para amigos; GS0.97 del repack no valida ese flujo. |

Al integrar las ramas pendientes:

- H3/H4 deben reutilizar el pool y el subtipo `5c5/2`; el caller con flag1 conserva además su efecto opcional.
  Los nuevos callers de `ApplyEffectFlags` deben prestar los recursos efectivos para Text[1828], no omitir el aviso.
- H10 debe conservar la identidad de asignación, primera inserción y vida del registro secundario al ampliar la fábrica.
  Los mapas de chats/cartas siguen separados. No volver a vaciar `tabs` en Reset ni a resolver su owner sólo por ID.
- Los cierres y reinicios nuevos deben prestar `pressedButton`; las nuevas llamadas de fábrica desde el juego,
  también los globals del runtime para el destructor de un intento cancelado.
- Conservar la publicación de doble clic por mensaje y su consumo único. Un subpaso sin mensajes no lo apaga
  ni lo repone; Draw intermedio usa copias sin efectos de red.

Estos pendientes impiden llamar a H1/H9 «100% equivalentes al original» o «CL9 terminada».
El resultado de esta rama es cerrar los defectos propios demostrados y dejar sus conexiones verificables,
con las dependencias y la validación del original separadas y concretas. El merge queda a cargo de Emanuel.
