# 08. Lo que falta saber

El research del cliente tiene mapa de todo, pero detalle validado solo de la red, el cifrado, los
formatos y, desde las trazas de OpenGL, el terreno, la cámara, el texto y el skinning del personaje. Esta lista dice qué falta, qué ola lo necesita y cómo se cierra. Cada hueco se cierra en
una sub-ola **CL*n*.R** (research del binario) antes de la ola que lo usa, como el carril R del
[camino de aprendizaje](../../../docs/superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md#carril-r-ingeniería-inversa).

## Ruta crítica del original

El laboratorio midió la distancia a "cliente jugable" en hitos (`mu-client/docs/coverage-main.md`
§7). MuRet la usa como esqueleto de sus olas:

| Hito | Qué es | Estado del research | Ola de MuRet |
| --- | --- | --- | --- |
| H0 Esqueleto | ventana, contexto GL, bucle, teclado, config | estático, 70 %; el ritmo del frame medido en la selección y en Lorencia (21,33 FPS, lógica a 25) | CL3 |
| H1 Formatos | tablas, terreno, OZJ/OZT, contenedor BMD | verificado byte a byte; el contenedor BMD recorrido en los 1.962 modelos | CL1, CL6 |
| H2 Login | CS, lista, GS, C3/C4, FoxAnti | verificado en el cable: flujo del CS y del GS, login, keepalive, C3/C4 en los dos sentidos y capas de FoxAnti | CL2, CL4 |
| H3 Selección | modelos con animación, preview 3D | contenedor BMD recorrido en los 1.962 modelos; skinning, pose y frame del personaje reproducidos contra las trazas de GL (acción 3 de `Player.bmd`, error < 0,016); el código del render de modelos, sin leer | CL6 |
| H4 Mundo | terreno, objetos, cámara, caminar | renderer de terreno identificado y verificado celda por celda contra una traza de GL; cámara y objetos del `.obj` verificados en la misma traza ([06](06-render-escenas-y-ui.md)) | CL7 |
| H5 UI/HUD | texto, HUD, ventanas, chat | arquitectura estática; el camino del texto, el quad 2D y los viewports verificados en las trazas de GL | CL5, CL8 |
| H6 Gameplay | los 140 handlers y los emisores | tabla anotada, semántica estática o inferida | CL9 |
| H7 Audio | música y efectos | `wzAudio` decompilada (CoolPlayer 215, Ogg, MP3); el uso desde `main.exe`, poco | CL10 |

## Huecos

| # | Hueco | Bloquea | Cómo se cierra |
| --- | --- | --- | --- |
| P-C01 | **Modelos `.bmd`: lo que falta del skinning y la animación.** Ya reproducido contra las trazas y leído en el binario ([05](05-formatos-de-datos.md#modelos-bmd), [06](06-render-escenas-y-ui.md)): un hueso por vértice, pose por cuaterniones con las acciones de `Player.bmd`, frame fraccionario que avanza por paso con la velocidad de cada acción (tabla de `0x60dbe0`: 0,26 en la 3 y 0,28 en la 1, verificadas con el reloj de la traza), mascota y alas por frame dibujado (Q-C20), la rotación extra de la cabeza (`0x4eb860`, *estático*) y las UV de tres modos de brillo (`0x4ed890`, verificadas). Falta: (a) quién escribe el ángulo de la cabeza (`objeto + 0x28`) en los personajes; (b) de qué depende cuántos pases de brillo lleva cada item; (c) qué hace el llamador con el aviso adelantado de la selección (Q-C21); (d) para qué traen las partes sus 56 huesos, `lock_positions` y los huesos dummy | CL6 | CL6.R: (a) y (b) en la lógica del personaje (`0x4fd5f0`) y el render de `0x53c6a0`/`0x5f9780`; los números de [06](06-render-escenas-y-ui.md) sirven de oráculo. Si hace falta otra captura, una con un item de otro nivel |
| ~~P-C02~~ | **Cerrado el 2026-10-31.** El renderer del terreno es `0x5fc260` → `0x5fc190` → `0x5fc0e0` → `0x5fac90` → `0x5fa830`, verificado celda por celda contra la traza de Lorencia ([06](06-render-escenas-y-ui.md)). Quedan dos cosas chicas: qué textura es el slot `0x122` (Q-C19) y quién suma las luces dinámicas al terreno | CL7 | CL7.R pasa a ser una lectura guiada de esa cadena |
| P-C03 | **Qué depende del paso de 40 ms.** El ritmo ya está medido y explicado ([02](02-arranque-y-ciclo-de-vida.md)): el dibujo va a 21,33 FPS (medido) y la lógica a 25 pasos por segundo (*inferido* de la aritmética del bucle), porque la espera de 40 ms mide con `GetTickCount` (ticks de 15,625 ms) y termina a los 46,875 ms; 3.511 frames de la selección y 3.597 de Lorencia enganchados a esa grilla. La lista de lo que corre por paso y por frame en `0x637700` ya está ([02](02-arranque-y-ciclo-de-vida.md#qué-corre-en-cada-paso-y-qué-en-cada-frame)): la lógica de la escena, el avance del cuerpo (verificado), un sistema de 5 subpasos, dos temporizadores, dos cuentas regresivas y un ciclo de 32 pasos. El orquestador `0x63945d` solo elige la escena y, después del frame, arma paquetes `F1`. Falta: qué hacen `0x623e70` (40 KB, solo en el juego) y la lógica de cada escena (movimiento, cooldowns, cámara) para interpolar sin cambiar el juego (D-C4 del roadmap), y para qué son los dos contadores | CL3 | CL3.R: leer `0x636290` y `0x623e70` y los usos de `DAT_05767940`, `DAT_077f3b74` y `DAT_07d2d360` |
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
