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
| H1 Formatos | tablas, terreno, OZJ/OZT, contenedor BMD | verificado byte a byte; el contenedor BMD recorrido en los 1.962 modelos | CL1, CL6 |
| H2 Login | CS, lista, GS, C3/C4, FoxAnti | verificado en el cable: flujo del CS y del GS, login, keepalive, C3/C4 en los dos sentidos y capas de FoxAnti | CL2, CL4 |
| H3 Selección | modelos con animación, preview 3D | contenedor BMD recorrido en los 1.962 modelos; **significado de la animación sin leer** | CL6 |
| H4 Mundo | terreno, objetos, cámara, caminar | **renderer de terreno sin identificar** | CL7 |
| H5 UI/HUD | texto, HUD, ventanas, chat | arquitectura estática | CL5, CL8 |
| H6 Gameplay | los 140 handlers y los emisores | tabla anotada, semántica estática o inferida | CL9 |
| H7 Audio | música y efectos | `wzAudio` decompilada (CoolPlayer 215, Ogg, MP3); el uso desde `main.exe`, poco | CL10 |

## Huecos

| # | Hueco | Bloquea | Cómo se cierra |
| --- | --- | --- | --- |
| P-C01 | **Modelos `.bmd`: significado de la animación y del skinning.** El contenedor está recorrido entero (tamaños verificados); qué son los 24 B por key, la interpolación y cómo se pega cada vértice a su hueso, sin leer | CL6 | CL6.R: leer el cargador de modelos y el render de `0x53c6a0`/`0x5f9780` en el decompilado; validar con un volcado de memoria del cliente vivo |
| P-C02 | **El renderer del terreno** no está identificado (consumidor del frustum `0x5fbb00`) | CL7 | CL7.R: referencias cruzadas a `DAT_07d2d364/368` y a las capas del `.map` en memoria |
| P-C03 | **El ritmo del frame.** Ya se sabe (estático, [02](02-arranque-y-ciclo-de-vida.md)): pasos de lógica de 40 ms y espera activa hasta 40 ms, 25 FPS (no 30). Falta: medirlo en la VM y saber qué depende del paso (animación, movimiento, cooldowns, cámara) para interpolar sin cambiar el juego (D-C4 del roadmap) | CL3 | CL3.R: leer el orquestador `0x63945d` y los usos de `DAT_006d5ee4`; medir con un hook de `SwapBuffers` en la VM XP |
| P-C04 | **La lista de servidores, del lado de `main.exe`.** Bytes y capas verificados ([04](04-protocolo.md)): el CS manda `F4 02`, FoxAnti lo traduce a `F4 06` (código + delta) y `0x4c1860` lo procesa agrupando por `código / 20`; el `0xCC` es relleno que FoxAnti descarta. Falta: la tabla de 25 índices de `0x4c1860` (cómo ubica cada grupo en la pantalla) y qué hace con más de un servidor | CL4 | CL4.R: leer `0x4c1860` y su llamador en el decompilado; probar con un CS falso que mande varias entradas |
| P-C05 | **Diferencial dinámico del login `F1 01` y del keepalive `0x0E`** con el build `c32bbd5f…`. Tamaños, cifrado, checksum y serial ya verificados en la sesión real de [04](04-protocolo.md); falta comparar campo por campo el contenido descifrado | CL4 | un test en CL2 que arme el login con cuenta, tick y serial conocidos y lo compare con el descifrado de esa sesión (con una cuenta de prueba) |
| P-C06 | **Selección de personaje** (`F3 00`/`F3 01`/`F3 03`…): los sub-handlers están anotados; la sesión real de [04](04-protocolo.md) tiene el pedido, la lista de personajes y la entrada al mundo, falta reconstruir el flujo campo por campo | CL6 | CL6.R: emisores y handlers de `0x4e2f93` contra esa sesión |
| P-C07 | **Audio**: `wzAudio.dll` ya está decompilada (CoolPlayer 215, 17 exports) y los formatos son conocidos (MP3, Ogg, WAV). Falta: quién dispara cada sonido en `main.exe`, volúmenes y el mezclado de DirectSound | CL10 | CL10.R: los llamadores de `0x4acfd0`, `0x41ed10` y la tabla de música por mapa |
| P-C08 | **Input completo** (mouse sobre UI, atajos, IME) | CL3, CL8 | CL3.R: el resto del WndProc y las llamadas a `GetAsyncKeyState` |
| P-C09 | **Semántica de cada handler** de juego (combate, items, party, gremio, comercio). La sesión real de [04](04-protocolo.md) da los opcodes reales de caminar, pelear e items, pero sin la hora de cada acción | CL9 | una CL9.R por dominio con esa sesión; las próximas capturas, con la hora de cada acción |
| P-C10 | **¿Con qué servidor se prueba?** El GS del laboratorio exige la capa FoxAnti; un servidor de MuRet todavía no existe | CL4 | decisión del roadmap: primero el GS del laboratorio (con la capa), después los servidores de MuRet |
| P-C11 | **El `A9` del cliente** (Q-C16): sale primero en cada conexión al CS y diez veces en el GS. Se sabe que lo arma `main.exe` con el motor XOR32; falta cuál de los tres emisores corre al conectar y qué significan sus 3 bytes | CL4 | CL4.R: leer `0x415be0`, `0x422990` y `0x424230` y sus llamadores |

## Qué no hace falta saber

- Las protecciones de FoxAnti (VMProtect, ASPack, scans, watchdog): fuera de alcance.
- La estructura interna del código original: MuRet copia el comportamiento observable, no las
  clases.
