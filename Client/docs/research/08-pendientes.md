# 08. Lo que falta saber

El research del cliente tiene mapa de todo, pero detalle validado solo de la red, el cifrado y los
formatos. Esta lista dice qué falta, qué ola lo necesita y cómo se cierra. Cada hueco se cierra en
una sub-ola **CL*n*.R** (research del binario) antes de la ola que lo usa, como el carril R del
[camino de aprendizaje](../../../docs/superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md#carril-r-ingeniería-inversa).

## Ruta crítica del original

El laboratorio midió la distancia a "cliente jugable" en hitos (`mu-client/docs/coverage-main.md`
§7). MuRet la usa como esqueleto de sus olas:

| Hito | Qué es | Estado del research | Ola de MuRet |
| --- | --- | --- | --- |
| H0 Esqueleto | ventana, contexto GL, bucle, teclado, config | estático, 70 % | CL3 |
| H1 Formatos | tablas, terreno, OZJ/OZT | verificado byte a byte; **faltan los modelos BMD** | CL1, CL6 |
| H2 Login | CS, lista, GS, C3/C4, FoxAnti | recepción y cifrado verificados; login y keepalive estáticos | CL2, CL4 |
| H3 Selección | modelos con animación, preview 3D | parseo de modelos N4; **animación sin analizar** | CL6 |
| H4 Mundo | terreno, objetos, cámara, caminar | **renderer de terreno sin identificar** | CL7 |
| H5 UI/HUD | texto, HUD, ventanas, chat | arquitectura estática | CL5, CL8 |
| H6 Gameplay | los 140 handlers y los emisores | tabla anotada, semántica estática o inferida | CL9 |
| H7 Audio | música y efectos | casi nada (8 %) | CL10 |

## Huecos

| # | Hueco | Bloquea | Cómo se cierra |
| --- | --- | --- | --- |
| P-C01 | **Modelos `.bmd`: animación y skinning.** Parseo conocido; huesos, keyframes e interpolación sin analizar | CL6 | CL6.R: leer el cargador de modelos y el render de `0x53c6a0`/`0x5f9780` en el decompilado; validar con un volcado de memoria del cliente vivo |
| P-C02 | **El renderer del terreno** no está identificado (consumidor del frustum `0x5fbb00`) | CL7 | CL7.R: referencias cruzadas a `DAT_07d2d364/368` y a las capas del `.map` en memoria |
| P-C03 | **El ritmo del frame** (`0x63945d`, 7 KB, sin decompilar en detalle): ¿lógica fija a 25 ticks como en MuMain, o tope de 30 FPS como recuerda Emanuel? ¿Qué cosas dependen de ese ritmo (animación, movimiento, cooldowns)? Hace falta para subir los FPS sin cambiar la velocidad del juego (D-C4 del roadmap) | CL3 | CL3.R: leer el orquestador y buscar el `Sleep`/`timeGetTime` que limita; medir con un hook de `SwapBuffers` en la VM XP |
| P-C04 | **Layout exacto de los paquetes de la lista de servidores** (`F4 06`, `F4 03`) | CL4 | sacarlos del cable (`muproxy.log`, puerto 44405) y de `0x4c1860` |
| P-C05 | **Diferencial dinámico del login `F1 01` y del keepalive `0x0E`** con el build `c32bbd5f…` | CL4 | captura en la VM XP con credenciales, tick, serial y `rand` controlados (follow-up F2 de `mu-client/CHECKPOINT.md`) |
| P-C06 | **Selección de personaje** (`F3 00`/`F3 01`/`F3 03`…): los sub-handlers están anotados, el flujo no está reconstruido | CL6 | CL6.R: emisores y handlers de `0x4e2f93`, validar contra el cable |
| P-C07 | **Audio**: `wzAudio.dll` (5 exports: `Create/Play/Stop/Option/GetStreamOffsetRange`) y DirectSound | CL10 | CL10.R: formatos de los archivos de música y sonido y quién los dispara |
| P-C08 | **Input completo** (mouse sobre UI, atajos, IME) | CL3, CL8 | CL3.R: el resto del WndProc y las llamadas a `GetAsyncKeyState` |
| P-C09 | **Semántica de cada handler** de juego (combate, items, party, gremio, comercio) | CL9 | una CL9.R por dominio, con el cable como oráculo |
| P-C10 | **¿Con qué servidor se prueba?** El GS del laboratorio exige la capa FoxAnti; un servidor de MuRet todavía no existe | CL4 | decisión del roadmap: primero el GS del laboratorio (con la capa), después los servidores de MuRet |

## Qué no hace falta saber

- Las protecciones de FoxAnti (VMProtect, ASPack, scans, watchdog): fuera de alcance.
- La estructura interna del código original: MuRet copia el comportamiento observable, no las
  clases.
