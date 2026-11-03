# Plan de la ola CL5: Render 2D y la escena de login

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md).
> Escrito el 2026-11-02, con lo aprendido en la CL3 y la CL4. Cada sub-ola trae su contrato, sus
> nombres y sus tests; lo que se aprenda en una se escribe acá antes de arrancar la siguiente.
>
> **Quién escribe los pasos `TODO(human)`:** por pedido de Emanuel (2026-11-02, pregunta 1 de la
> CL2), Claude los resuelve también, hasta que él diga que participa. En
> [PROGRESS](../../learning/PROGRESS.md) quedan como resueltos por Claude.

**Objetivo:** que el cliente muestre lo que muestra el `main.exe` desde que arranca hasta que el
GameServer acepta el login, y que se pueda usar: la escena de carga, la de login con sus fondos,
la arena que corre, los textos de abajo, la lista de servidores, la elección, la ventana de la
cuenta y la clave, y los avisos (no se pudo conectar, se cortó, el login no entró). Todo sobre
OpenGL 3.3 core con shaders (D-C3), con las primitivas 2D del original reproducidas al vértice
(el quad de `0x621b60`, las dos coordenadas: 640 × 480 escaladas y píxeles reales, la Y dada
vuelta), texturas desde los OZT y OZJ de la CL1, y texto rasterizado sin suavizado como el GDI
del original. La red es la de la CL4: la `ServerSession` pasa a esperar a la UI para elegir el
servidor y para la cuenta y la clave.

**Lo que esta ola no dibuja todavía:** el original dibuja detrás del login un **mundo 3D**, el mapa
55 (`World56` y `Object56` del `Data\`), con el logo como modelo (`Logo03.bmd`) y un personaje con
el cuervo oscuro. Eso necesita los modelos (CL6) y el terreno (CL7). La CL5 deja el lugar del pase
3D en el orden del original, con el mundo como dato (el número de mapa de la escena, nunca un
mundo fijo en el código), y la CL7 lo llena con el mismo código que carga cualquier `World<N>`
(CL7.5). Pregunta 3.

**Arquitectura:** dos capas nuevas y una que crece, como dice el roadmap.
- `Client::Render` (`Client/src/Render/`) se parte en dos, como la plataforma en la CL3: lo que
  arma qué dibujar va en `ClientLib` y se prueba sin GL (el `SpriteBatch`, la matriz del 2D, el
  diseño del texto, los colores `0x02`, la tabla de texturas como datos); lo que habla con OpenGL
  va en un target nuevo, `ClientRender` (`Client/src/Render/Gl/`), que enlaza `ClientPlatform`
  por la tabla de funciones de GL. Pregunta 4.
- `Client::Scenes` suma el `SceneManager`, la `LoadingScene` y la `LoginScene`, que reemplazan a
  la `TestScene` cuando hay carpeta de datos. Una escena no ve GL: llena un `SpriteBatch` y pide
  textos en el lienzo que le da un `IFrameTarget` (en `main.cpp`, el de GL, que dibuja y presenta),
  y sube y suelta sus texturas por un `Render::ITextureStore` (la `TextureTable` de GL). El
  `SceneManager` es el `ILoopClient` del bucle, con el `Poll` de la red (CL4.1).
- `Client::UI` (`Client/src/UI/`) nace con lo mínimo para el login: botón, campo de texto, la
  ventana de la lista de servidores, la de la cuenta y el aviso. En `ClientLib`, sin GL ni SDL.
- `Client::Data` suma `WorldFolder` (de un número de mapa a su carpeta, como `0x61a560`) y los
  juegos de texturas como datos; `Client::Input` suma el texto tecleado; `Client::App` cambia la
  `ServerSession` para que espere a la UI (CL5.5).

**Stack:** C++20 (`std::span`, `std::array`, `std::variant`, `std::string_view`), OpenGL 3.3 core
con GLSL 330 (VAO, VBO que se llena por frame, texturas 2D, mezcla, `discard` en vez del alpha
test), SDL3 (ventana, `SDL_StartTextInput` y `SDL_EVENT_TEXT_INPUT`), GoogleTest. Dos
bibliotecas nuevas, vistas con Context7 el 2026-11-02 (`CODING_RULES.md` §10): **glm** 1.0.3 para
vectores y matrices (pregunta 6) y **FreeType** 2.14.3 para rasterizar el texto (pregunta 7). El
loader de GL sigue propio y crece (pregunta 5).

**Guía de estudio:** `docs/learning/ola-cl5.0-el-pipeline-grafico.md`, que se escribe al arrancar
la CL5.0 (L-10). De la CL5.1 en adelante, la sección de cada sub-ola de este plan.

**Research que fija esta ola:**
- [06-render-escenas-y-ui](../../../Client/docs/research/06-render-escenas-y-ui.md): la máquina
  de escenas, lo que el original le pide a OpenGL (*verificado* en las trazas de la selección y de
  Lorencia), las primitivas 2D (`0x6219b0`, `0x621b60`, `0x621aa0`), el texto (`0x554180`,
  `0x553e50`, `0x554000`, los colores de `0x4667a0`) y "Qué implica para MuRet".
- [05-formatos-de-datos](../../../Client/docs/research/05-formatos-de-datos.md#texturas): el OZT
  y el OZJ, el buffer de lados potencia de 2 y lo que queda fuera de la imagen (Q-C30).
- [09-datos-disponibles](../../../Client/docs/research/09-datos-disponibles.md): qué textura va
  en qué slot (`0x63b870`, `0x63bd00`), la tabla de 1.845 slots y las texturas de las trazas con
  su archivo.
- [02-arranque-y-ciclo-de-vida](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md): el
  orquestador de escenas (`0x63945d`), las fuentes de `WinMain`, las teclas de cada escena y de
  las ventanas de la UI nueva (Enter y Esc por frame), y la versión que se dibuja.
- [03](../../../Client/docs/research/03-red-y-cifrado.md) y
  [04](../../../Client/docs/research/04-protocolo.md), con lo que les pasa la CL4.R: lo que pasa al
  conectar (el aviso `0x70` con el texto 402 de `Text.bmd`, el `FD_CLOSE` con el texto 3) y el
  flujo del CS y del GS.
- [07](../../../Client/docs/research/07-quirks-y-bugs.md): Q-C14 (OZJ que son TGA), Q-C23 (una
  textura que falta corta el juego), Q-C29 a Q-C31 (los cargadores), Q-C43 (la cuenta y la clave),
  Q-C47 (las dos posiciones del mouse), y el keepalive que sale antes del login, que la CL4.R
  pasa a 07 como quirk nuevo.
- [08](../../../Client/docs/research/08-pendientes.md): P-C04 (la lista de servidores del lado de
  `main.exe`, que la CL4.R pasó a esta ola: la CL5.R, paso 6) y P-C12 (los mapas: la carpeta de cada
  uno).
- En `client-research/` del repo de research, el decompilado de `mainexe-vmxp-c32bbd5f`:
  `00622e40` (la escena de carga), `0065eeb0` (su UI), `00634c60` (la lógica del login),
  `006349a0` (su preparación), `0061c7c0` (sus texturas), `006350b0` (su dibujo), `0065f300` (las
  ventanas de la UI nueva), `00621b60`, `00622000`, `00620a00`, `00620a90`, `006219b0`,
  `00620780`, `00554180`, `004667a0`, `004665d0`, `0061a560` (la carpeta del mundo) y las
  `CreateFontA` de `004b9b80`. Las trazas de
  [mu-online-reverse-lab#45](https://github.com/EmaNLJN/mu-online-reverse-lab/pull/45)
  (`graphics-01/`), que son de la selección y de Lorencia: **no hay traza de la escena de login**.
  `mu-client` como oráculo de `0x466560` (`tests/ui/test_texture_slots.c`).

**Lo que dice el binario de las escenas de carga y de login** (salió del decompilado para este
plan; la CL5.R lo confirma en el desensamblado con `objdump` y lo pasa al research). Como en el
research, *estático* es lo leído en el binario; *verificado*, lo que mostraron las trazas de GL;
*inferido*, lo que se deduce. Todo lo de la escena de login es *estático*, porque no hay traza.
**La CL5.R lo confirmó el 2026-11-03** y lo pasó a 06 (§ La escena de carga y la de login), 02 y
05; abajo quedó corregido, con "(CL5.R)" en lo que cambió o se completó:

| Qué | Dónde | Qué hace |
| --- | --- | --- |
| las escenas | `DAT_006d5dc8`, `0x63945d` | 1 la carga inicial (`0x622e40`), 2 el login, 3 la carga del mundo (`0x6356f0`, los fondos `Interface\LSBg01` a `LSBg04`), 4 la selección, 5 el juego. Pasan 1 → 2 (`0x622e40`), 2 → 4 (`0x634c60`), 4 → 3 (`0x6342c0`), 3 → 5 (`0x6356f0`), y `0x4c2710`, con la respuesta del `F1 02` cifrada, pone 4 si `pkt[4]` es 1 y 2 si es 2 (CL5.R; la tabla entera, en 02). *Estático* |
| la carga | `0x622e40` | sube `Interface\lo_back_01.jpg`, `lo_back_02.jpg`, `lo_mu_logo.tga`, `lo_121518.tga`, `lo_webzen_logo.tga` y `lo_lo.jpg` a los slots `0x71e` a `0x723`, arma su UI (`0x65eeb0`: cinco imágenes y una barra con el `0x723`), carga los datos del juego (`0x61ccb0`), libera los seis slots (`0x63c150`), loguea `> Loading ok.` y pasa a la 2. *Estático* |
| entrar al login | `0x634c60` llama a `0x6349a0` cada vez que se entra a la escena: la bandera `DAT_07d3a13d` la vuelve a 0 `0x4c2f24`, al pasar a la 2 (`0x4c2f1a`), así que al volver al login carga otra vez el mundo y reconecta al CS | el mapa pasa a `0x37`, el 55 (`DAT_006c9dd0`), y `0x61a560` carga ese mundo; sube las texturas de la escena (`0x61c7c0`); arma las ventanas de la UI (`0x65f300`); conecta al CS (`0x4c1340`); pone el objeto `0xa2` en una posición fija y arma el personaje de la escena con el cuervo (`0x517270`, el `A9` de la CL4.R); suena `data\music\main_theme.mp3` (`0x4acfd0`) y loguea `> Login Scene init success.` *Estático* |
| la carpeta del mundo | `0x61a560`, con `0x616ec0` | `World%d` con el mapa + 1, salvo del 11 al 17 → `World12`, del 18 al 23 → `World19`, del 24 al 29 y el 36 → `World25`; el mismo número va en `EncTerrain%d`. Dos casos más: con el argumento y `DAT_006c9dd0` en 32, el mapa pasa a 9 (`0x61a56c`..`0x61a57d`) y la carpeta es `World10` (los cinco llamadores pasan los dos iguales), y antes de la carpeta `0x616ec0` fuerza el mapa 7 si `DAT_07d366bc` está prendido (`0x616ee1`; en el binario nadie lo escribe: código muerto, y `WorldFolder` no lo lleva, CL5.R). Los mapas que llegan: los de los tres handlers de `0x4c41c9`, `0x4c4e9d` y `0x4c780c` (un byte sin signo del paquete, de 0 a 255, y ninguno mira el rango: Q-C58, CL5.R), el 54 de `0x633f20` (desde `0x6342c0`, `0x633f44`: `World55`) y el 55 del login. El 55 da `World56`, que está en el `Data\` del laboratorio con `Object56`, y es el único mundo que multiplica las alturas por 3 (`GoldenTerrainHeights.World56ReachesThreeTimes255`, CL1). *Estático*; las alturas, *verificado* en la CL1 |
| las texturas del login | `0x61c7c0` | `Logo\Login_Back01.jpg` y `Login_Back02.jpg` (slots `0x15` y `0x16`), `Object9\sand01.jpg` y `sand02.jpg` (`0x5c2` y `0x5c3`), `Effect\FireSnuff.jpg` (`0x635`), el modelo `0xa2` (`0x60da30` con `Data\Logo\`: `Logo03.bmd`, *inferido* por cómo arma los nombres) y 13 de `Interface\` en `0xc` a `0x1a` (sin `0x15` y `0x16`): `cha_bt`, `server_b2_all`, `server_b2_loding`, `server_deco_all`, `server_menu_b_all`, `server_credit_b_all`, `deco`, `login_back`, `login_me`, `server_ex03`, `server_ex01`, `server_ex02` y `cr_mu_lo`. El tercer argumento vale `0x2600` o `0x2601` y el cuarto `0x2900` o `0x2901`: `GL_NEAREST` o `GL_LINEAR`, y `GL_CLAMP` o `GL_REPEAT` (*inferido* por los valores) |
| el dibujo | `0x6350b0`, en este orden | (1) la cámara (`0x635ae0`) y la mezcla normal (`0x620a00(1)`); (2) 2D (`0x6219b0`) y los dos fondos, `0x621b60(0x15, 0, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0)` y el `0x16` en x = 320, escalados desde 640 × 480; (2b) un pase entre los dos: `0x621a90` cierra el 2D, `0x620db0(0, 0, w, 0x30)` con el ancho de `0x5bb8d0`, `0x5fbb00(w · k, …)` (con una tangente: el frustum, *inferido*) y `0x620fc0` (dos llamadas a una importación de GL, con `0x1701` y `0x1700`, las constantes de `GL_PROJECTION` y `GL_MODELVIEW`): no dibuja nada, y `0x620fc0` saca las dos matrices (CL5.R); (3) el pase 3D en `0x620db0(0, 25, 640, 430)`: si la ventana de `+0x4578` de la UI está cerrada (`+0x4584` es su bandera de abierta, `+0xc`: CL5.R), el terreno (`0x5fc260`), los personajes (`0x510d00`), los objetos (`0x53c6a0`) y los efectos; (4) 2D otra vez, mezcla aditiva (`0x620a90`), el color (0,25; 0,1; 0,1) y las dos arenas (`0x622000(0x5c2, 0, 25, 640, 430, …)` y el `0x5c3`), corridas por el reloj; (5) tres textos con la fuente `DAT_05763554` y el color (0,8; 0,7; 0,6), apoyados en y = 479: el 454 de `Text.bmd` termina en x = 335, el 455 empieza ahí y el 456, con la versión (`DAT_057630ac`), va en x = 0; (6) un fundido de 16 pasos (`DAT_07d3a0cc`, `DAT_006d5e30`, de a 1/16), que pone el color y no dibuja nada (Q-C66, CL5.R); (7) lo común a las escenas (`0x5f7bc0`, `0x554810`, `0x5554e0`, `0x5b0550`, `0x40eca0`, `0x65fe00` la UI nueva, `0x5ac2d0`, `0x633110`). *Estático*. La selección (`0x634680`) tiene la misma forma, y en su traza está: una perspectiva que no dibuja y un ortho con 2 quads antes del mundo (*verificado*, 06) |
| el quad | `0x621b60(slot, x, y, w, h, u, v, du, dv, escalar tamaño, escalar posición, alfa)` | (06, *verificado* en las trazas) `GL_TRIANGLE_FAN` con (x, H − y), (x, H − y − h), (x + w, H − y − h) y (x + w, H − y), y las UV (u, v), (u, v + dv), (u + du, v + dv), (u + du, v). Con el alfa en más de 0 (`DAT_006b86a0`), cada vértice va con (1, 1, 1, alfa) y después vuelve a (1, 1, 1, 1): **el color que había antes se pierde**, y el quad siguiente sin alfa sale blanco. Con alfa 0, usa el color que haya. *Estático* |
| la arena | `0x622000` | el mismo quad, siempre escalado, con la v corrida 0,25 · dv de un lado: la textura se inclina. Los nueve argumentos (CL5.R): `0x622000(0x5c2, 0, 25, 640, 430, u, 0, 0,3, 0,3)` con u = `((int) T % 100000) · 0,0001`, y `0x622000(0x5c3, 0, 25, 640, 430, u, 0, 3, 2)` con `· 0,0005`; T es `DAT_05767b88`, el `timeGetTime` absoluto en float (Q-C65). Las UV: (u, v + 0,25 dv), (u, v + 0,75 dv), (u + du, v + dv) y (u + du, v). *Estático* |
| la mezcla | `0x620a00`, `0x620a90`, `0x620780` | el modo va en `DAT_07d366dc`: el 2, `GL_BLEND` con (`SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`) y el alpha test prendido; el 3, (`ONE`, `ONE`) y el alpha test apagado. Los dos prenden la textura. La textura atada se recuerda en `DAT_006d5d68` (un slot negativo es un nombre de GL directo). El alpha test es el `GL_GREATER 0,25` que dejó la cámara (06). *Estático*; los estados, *verificado* en las trazas |
| el texto | `0x554180` → `0x553e50` y `0x554000` | (06, *verificado*) un `glTexImage2D` de 256 × 32 por string y por frame en el slot 0, rasterizado con GDI. Antes, la escena pone el color del texto (`DAT_006c8e5c`), el del fondo (`DAT_006c8e64`, `0x80000000` en el login), un ancho (`DAT_006c8e70`, `0x100`) y la fuente (`SelectObject`). La posición sale de `GetTextExtentPointA`, pasada a 640 con `cx · 640 / W` (división entera sin signo). *Estático* |
| las fuentes | `WinMain`, `0x4b9b80` | cuatro `CreateFontA` **sin suavizado** (`NONANTIALIASED_QUALITY`, el 3) y con el charset de `0x6c76a0` (`0x86` en la `.data`): normal (400) y negrita (700) de alto n − 1, negrita de 2 (n − 1), y una normal de 12 con otra cara. n sale del ancho de la pantalla: 12 con 640, 13 con 800, 14 con 1024 y 15 con 1280; con 1600, la quinta resolución, n queda en 0 (de la `.bss`) y las alturas en -1, -1 y -2 (Q-C59, CL5.R). Las caras son los textos 0 y 18 de `Text.bmd` (la tabla empieza en `0x76bd1dc`), o la del sistema si están vacíos. *Estático* |
| los colores `0x02` | `0x4667a0` y `0x4665d0`, solo desde `0x466000` | un `0x02` y el byte que sigue: el byte elige el color del texto y el del fondo (`0x4665d0`: 1 y `0xf4` rojo, `0xf0` y `0xf1` toman los dos de la escena, uno al revés del otro, `0xf2`, `0xf3` y `0xf5` a `0xf8` el suyo, y cualquier otro, blanco). Guarda hasta 9 cambios, cada uno con dónde empieza en el texto limpio y su ancho (`GetTextExtentPointA` menos 1), y saca del texto todos los pares. Lo usa solo el camino con caché (`0x466000`), que en las trazas no corrió (06), y que corre solo con `TextOut` en 1 en el registro; con otro valor, `0x467060` le pasa el texto entero a `TextOutA`, con los `0x02` adentro (CL5.R, 02 y 06). *Estático* |
| las ventanas | `0x65f300`, desde `0x6349a0` | arma siete ventanas de la UI nueva (en `+0x8`, `+0x3d0`, `+0x7e8`, `+0xe40`, `+0x10e8`, `+0x4228` y `+0x4578`) y las ubica en **píxeles de la ventana** (el tamaño está en `0x640850`, `+0x28` y `+0x2c`): la de `+0x8`, de 352 × 113, al centro; la de `+0x4228`, centrada en x y a dos tercios en y. La de `+0x8` es el aviso, la de `+0xe40` la barra de abajo, la de `+0x10e8` la lista y la de `+0x4228` la cuenta; las de `+0x3d0` (la de Esc), `+0x7e8` y `+0x4578`, sin leer (CL5.R, 06; P-C13). Tres ventanas leen Enter y Esc por frame (`0x63d880`, `0x642e30`, `0x6418b0`; 02). *Estático* |
| las teclas del login | `0x634c60`, por paso | Esc, con ciertas ventanas cerradas, suena el 0x19 y abre la de `+0x3d0`. Con el login aceptado (`DAT_057678c0 == 0x14`, que pone el `F1 01`), loguea `> Request Character list`, pasa a la escena 4 y manda el `F3 00` (CL6). Además, cada tanto el personaje de la escena toma la acción `0x54` o `0x55` (`rand() % 6`, CL6). *Estático* |

**Lo que falta para dibujarlo como el original** (la CL5.R abrió P-C13 en 08 con lo que no cerró):
- **Una traza de GL de la carga y del login**, con GLIntercept como `graphics-01` y con `ImageLog`
  prendido: el orden de los quads, sus posiciones, sus texturas, la mezcla y el quad del texto, en
  640 × 480 y en 1024 × 768. Hoy todo es estático, y los tests de la CL5.2 y la CL5.4 dicen
  "*estático*" hasta que llegue; con ella pasan a comparar contra la traza, como la CL7 con el
  terreno.
- **Capturas de pantalla** del login en esas dos resoluciones, fuera de Git: el texto de GDI y el
  de FreeType no van a ser iguales píxel a píxel, y se comparan a ojo.
- **Qué ventana es cada una** de las siete de `0x65f300`, su dibujo y sus botones (CL5.R: hecho
  salvo tres, que quedan en P-C13).
- **La lista con varios servidores** (P-C04): la tabla de 25 índices de `0x4c1860` y cómo dibuja
  los grupos. Lo cierra la lectura del binario y, si hace falta, un CS falso con varias entradas en
  la VM. (CL5.R: cerrado en el binario, 06 § La lista de servidores; verlo, con las capturas.)
- **La cara de la fuente** del `Text.bmd` del laboratorio y si Windows la tiene: si no, GDI elige
  otra, y lo que se ve depende del sistema.
- **Qué texto muestra cada resultado del `F1 01`** (cuenta inválida, en uso, bloqueada…). (CL5.R:
  hecho, 06 § Los avisos.)

**Terminada cuando** (roadmap: la pantalla de login se ve y se usa). En concreto:
- con `--data`, `Main` muestra la escena de carga y pasa a la de login: los dos fondos, la arena
  que corre, los tres textos de abajo con la versión, y la lista que llega del CS; elegir un
  servidor conecta al GS y muestra la ventana de la cuenta y la clave; Enter manda el login, y
  aceptado pasa a la escena de selección (vacía hasta la CL6); rechazado, un "no se pudo conectar"
  o un corte muestran el aviso con el texto del original;
- los quads del original salen al vértice en 640 × 480 y en 1024 × 768 (`SpriteBatchTest`), el
  texto se ubica con la fórmula de `0x6350b0`, los colores `0x02` se separan como en `0x4667a0`, y
  la máquina de escenas rechaza una transición que el original no tiene;
- `WorldFolder` da la carpeta de cada mapa que llega a `0x61a560` (del 0 al 36, el 54 y el 55), y
  los 20 mundos del laboratorio son la carpeta de alguno
  (`GoldenWorldFolder.EveryLabWorldIsTheFolderOfAMap`); la escena de login pide su
  mundo por número, y el pase 3D lo recibe como dato;
- en Linux con Xvfb (CI), los tests de GL leen los píxeles de un framebuffer y dan lo que dice el
  `SpriteBatch`; sin `--data`, `Main --frames 60` sigue con la `TestScene` como en CI;
- a mano, en Windows y contra el GS del laboratorio, la lista, la elección, el login aceptado y un
  login con una clave mala.

## Preguntas para decidir al empezar

1. **Quién escribe la CL5.** *Decidido (Emanuel, 2026-11-02, en la CL2): Claude escribe todo*, la
   CL5.0 en `labs/` y los pasos `TODO(human)` incluidos, hasta que digas que participás. Las
   preguntas 2 a 12 siguen con su recomendación mientras no digas otra cosa.
2. **Una CL5.R, y en qué orden.** El roadmap no tiene carril R en la CL5, pero el research no
   tiene la escena de login: lo de arriba salió para este plan y es todo estático. *Recomendación:*
   sumar la CL5.R después de la CL5.0, como en la CL3 y la CL4: confirma la tabla de arriba, lee
   las siete ventanas y la lista (P-C04), y lo pasa al 06 y al 02. El orden queda CL5.0, CL5.R,
   CL5.1 a CL5.5, y la fila de la CL5 del roadmap se corrige con este plan.
3. **El mundo 3D detrás del login.** El original lo dibuja entre los fondos y la arena (paso 3 de
   la tabla), y es el mapa 55: modelos de la CL6, terreno de la CL7. *Recomendación:* la CL5
   dibuja todo lo 2D y deja el pase 3D como un punto del orden, `IWorldView::Draw(WorldView)`, que
   recibe el mapa de la escena (`SceneWorld{55}`, de una tabla por escena, no del código) y su
   carpeta por `WorldFolder`; en la CL5 no dibuja nada y el fondo queda negro debajo de la arena.
   La CL7 lo llena con el terreno y los objetos de cualquier `World<N>` (CL7.1, CL7.2 y CL7.5), y
   la CL6 suma el logo y el personaje con el cuervo. Nada en la CL5 supone un mundo: `WorldFolder`
   se prueba con todos los mapas que da el original (del 0 al 36, el 54 de la selección y el 55)
   y con los 20 mundos del laboratorio. La razón
   de no esperar a la CL7 para la UI: el login se usa (la CL4 necesita la cuenta) y lo 2D no
   depende del mundo. Lorencia sigue siendo la evidencia de las trazas (es lo que se capturó), no
   un límite.
4. **Dónde vive `Render`.** *Recomendación:* en dos lugares. Lo que no ve GL (`SpriteBatch`, la
   matriz del 2D, `TextLayout`, `InlineColors`, los juegos de texturas) va en `ClientLib` y se
   prueba en los dos sistemas sin ventana. Lo que ve GL (`GlRenderer`, `GlTexture`, `GlShader`,
   `GlTextTexture`) va en `ClientRender`, que enlaza `ClientPlatform` por `GlFunctions`, y sus tests
   en `Client/tests/render_gl/` (pregunta 9). En el CMake, `ClientLib` suma un `list(FILTER ...
   EXCLUDE REGEX "/src/Render/Gl/")`, como el de `Platform`.
5. **El loader de GL (D-C2).** El roadmap dice que si la tabla de la CL0.3 crece demasiado se
   revisa: con texturas, shaders, buffers, VAO, mezcla y los framebuffers de los tests pasa de 5 a
   unas 45 funciones. Se miraron tres caminos: el puerto `glad` de vcpkg (ya descartado en la
   CL0.3: trae el perfil de compatibilidad y pide Python), un `glad.c` generado por su web y
   copiado al repo (código generado de miles de líneas, con su propia forma de actualizarse) y la
   tabla propia. *Recomendación:* **la tabla propia, con una X-macro**: una línea por función
   (`MURET_GL_FUNCTION(PFNGLGENTEXTURESPROC, genTextures, "glGenTextures")`) en
   `Platform/GlFunctionList.h`, que arma el struct y el lazo de `LoadGlFunctions`; los chequeos de
   `IsValidProcAddress` y el reintento en `opengl32.dll` no cambian. Son pocas líneas y la razón va
   en el header, como pide §10. D-C2 se corrige en el roadmap: el loader con la X-macro y glm en la
   lista de dependencias en la CL5.1, y FreeType sin SDL3_ttf en la CL5.3 (preguntas 6 y 7).
6. **La matemática (§10).** Para el 2D alcanza una matriz de `gluOrtho2D`; la CL6 y la CL7 piden
   perspectiva, rotaciones y cuaterniones para comparar con las trazas. Se miró con Context7
   (`/g-truc/glm`): **glm** 1.0.3 (puerto `glm` de vcpkg, solo headers, MIT) trae `mat4` en
   column-major como GL, `glm::ortho(left, right, bottom, top)` (el de `gluOrtho2D`, con near −1 y
   far 1), `glm::perspective(fovy, aspect, near, far)` con fovy en radianes (el reemplazo de
   `gluPerspective` que da su manual), el clip de −1 a 1 por defecto (sin
   `GLM_FORCE_DEPTH_ZERO_TO_ONE`) y `glm::quat`. *Recomendación:* glm, sin macros `GLM_FORCE_*`, y
   `glm::value_ptr` para `glUniformMatrix4fv`. La CL5.0 prueba que `glm::ortho` da la matriz de
   `gluOrtho2D`; la CL6 mira si `glm::slerp` da lo mismo que `0x5fcd20` antes de usarlo.
7. **El rasterizado del texto (§10, D-C12).** El original usa GDI sin suavizado: glifos de un bit
   con el hinting de TrueType de Windows. Se miraron con Context7:
   - **FreeType** 2.14.3 (`/freetype/freetype`, puerto `freetype` de vcpkg, licencia FTL o
     GPL-2.0+): `FT_Set_Pixel_Sizes`, `FT_LOAD_TARGET_MONO` y `FT_RENDER_MODE_MONO` dan glifos de un
     bit (`FT_PIXEL_MODE_MONO`, filas con su `pitch`) con el intérprete de TrueType: es lo más cerca
     de `NONANTIALIASED_QUALITY` que hay fuera de Windows. Sin las features por defecto (`brotli`,
     `bzip2`, `png`, `zlib`): `zlib` y `bzip2` son para fuentes comprimidas (`zlib` también para
     WOFF), `brotli` para WOFF2 y `png` para glifos de color; una TTF del sistema no las usa.
   - **stb_truetype** (puerto `stb`): un header, pero sin hinting; a 11 a 14 píxeles y en un bit
     queda más lejos de GDI.
   - **SDL3_ttf** 3.2.2 (`/libsdl-org/sdl_ttf`): es FreeType por debajo, y `TTF_RenderText_Solid`
     da un bit, pero devuelve un `SDL_Surface` y trae SDL a `ClientLib`, donde viven el diseño, la
     caché y sus tests; el diseño (los colores `0x02`, el ancho como `GetTextExtentPointA`) se
     escribe igual.

   *Recomendación:* **FreeType**, en `ClientLib` detrás de un `IGlyphRasterizer` (los tests del
   diseño usan uno falso y corren sin fuente). Píxel a píxel no va a ser GDI: el tamaño de celda,
   el peso y que no haya suavizado sí; la diferencia va a 07 como quirk nuevo. El paso 1 de la
   CL5.3 confirma con Context7 qué intérprete usa FreeType en mono (`interpreter-version`) y si
   conviene fijarlo. La FTL pide el crédito en la documentación: va en el README del cliente.
8. **De dónde sale la fuente.** El original pide por nombre una fuente del sistema (los textos 0 y
   18 de `Text.bmd`); MuRet no puede traerla, y FreeType necesita un archivo. *Recomendación:*
   `[Video] FontFile` (y `BoldFontFile`, opcional: si falta, la negrita se arma con
   `FT_Outline_Embolden`) en la config, con una ruta. Sin la clave, en Windows se busca la cara del
   texto 0 en la lista de fuentes del sistema (`HKLM\SOFTWARE\Microsoft\Windows
   NT\CurrentVersion\Fonts`, en `Platform`); en Linux, sin la clave, es un error que la nombra. Los
   tests de FreeType leen `MURET_CLIENT_FONT_FILE` (en CI, una fuente del runner: DejaVu en Ubuntu,
   una de `C:\Windows\Fonts` en Windows), como D-C9: sin la variable se saltean, y si apunta a un
   archivo que no existe o que FreeType no abre, fallan. El nombre de la cara no va a ningún log
   de test ni doc.
9. **Los tests de GL.** El runner de Linux tiene Xvfb con Mesa (el paso de `Main --frames 60`), y
   el de Windows no tiene OpenGL 3.3. *Recomendación:* los tests de `render_gl/` abren una ventana
   escondida con un contexto 3.3 core, dibujan en un framebuffer de 64 × 64 y leen los píxeles con
   `glReadPixels`. Sin pantalla o sin 3.3 se saltean (`GTEST_SKIP`), salvo con
   `MURET_CLIENT_REQUIRE_GL=1`, que los hace fallar: el paso de Xvfb del CI corre
   `xvfb-run -a ctest -L gl` con esa variable, para que un Xvfb mal armado no los deje salteados
   en silencio. En Windows, a mano.
10. **La escala de la UI y el tamaño de la letra.** El original escala x por W/640 e y por H/480,
    cada una por su lado (`0x620d50`, `0x620d80`), y sus resoluciones son 640, 800, 1024, 1280 y
    1600 de ancho (CL5.R); la UI nueva va en píxeles, centrada, y la letra crece de a un punto con
    el ancho. *Recomendación:* las mismas fórmulas para cualquier resolución que valga, también
    ancha (lo escalado se estira, como lo haría el original); la letra con la tabla de `WinMain` y,
    para un ancho que no está, el escalón del mayor de los cuatro anchos que no lo pasa, y el de 640
    por debajo (menos de 800, 12; menos de 1024, 13; menos de 1280, 14; si no, 15), que es una
    diferencia con el original (que con 1600 deja la letra en -1 y -2: Q-C59). *Decidido (Emanuel,
    2026-11-03, #72):* valen las cinco resoluciones del original o un modo del monitor, y la ventana
    no cambia de tamaño (`App::IsValidResolution`); si el área de dibujo cambia igual (otra densidad
    de pantalla), las fuentes y la caché se rehacen. Un modo que mantenga el 4:3 de lo escalado
    cambia lo que se ve: va detrás de una opción, con un ADR, cuando haga falta. **Esto se aparta de
    D-C11**, que pide toda la UI en 640 × 480 escalada: las ventanas de la UI nueva del original
    (`0x65f300`) van en píxeles, y MuRet las sigue, por fidelidad. Si se acepta, la CL5.5 corrige
    D-C11 en el roadmap (lo escalado en 640 × 480, la UI nueva en píxeles, centrada como el
    original); si no, las ventanas se escalan y la diferencia con el original va a 07.
11. **La tabla de texturas.** El original nombra cada textura por un slot fijo de su tabla de 1.845
    (`0x15` es `Login_Back01`), y la CL5.R y la CL7 comparan con trazas que nombran slots
    (09). *Recomendación:* `TextureTable` indexada por `TextureSlot`, el número del original, con
    lo que cada escena sube desde su juego de texturas (un dato: ruta, slot, filtro y wrap) y
    suelta al irse, como `0x63c150` en la carga. El asignador dinámico de `0x466560` (los slots
    `10000 + índice` del texto con caché) **no se reproduce**: es interno, en las escenas medidas
    no corre, y la caché de MuRet (CL5.3) es otra; la línea del 06 que pide los casos de
    `test_texture_slots` se corrige en la CL5.3 con esta razón.
12. **El mouse de la UI y la sesión de la CL4.** La CL3.3 dejó para la CL5 qué posición del mouse
    usa la UI (Q-C47). *Recomendación:* la UI del login es la nueva: píxeles del framebuffer (fuera
    de D-C11, pregunta 10), acotados a `[0, W − 1] × [0, H − 1]`, como `0x640940`; la de 640 × 480
    sin acotar del WndProc queda para la CL8, que la necesita. Y la `ServerSession` deja de elegir
    sola: espera en `ChoosingServer` y en `WaitingCredentials` (CL5.5; preguntas 7 y 9 de la CL4).
    Lo que la CL4 hacía sin UI (el primero de la lista y las variables `MURET_CLIENT_ACCOUNT` y
    `MURET_CLIENT_PASSWORD`) pasa a un `AutoLogin` de `App` que solo arma `main.cpp` con
    `--auto-login`, para la prueba a mano y para depurar la red sin la UI.

## Lo que más puede morder

Cada uno tiene su test en la sub-ola que lo dice. En la review se miran primero:

- **La Y dada vuelta y las filas de la textura.** El original pone (x, H − y) arriba y le da la v
  de la fila 0 del buffer; el OZJ sube las filas como vienen y el OZT las da vuelta al leer (CL1).
  Si MuRet da vuelta otra vez (en el shader, en las UV o con un `glTexImage2D` "corregido"), el
  login sale cabeza abajo. Una textura de 2 × 2 con cuatro colores, dibujada como el fondo `0x15`,
  deja el téxel (0, 0) arriba a la izquierda (CL5.2, `GlSpriteRendererTest.TheFirstRowIsAtTheTop`).
- **`GL_UNPACK_ALIGNMENT`.** Vale 4 por defecto, y una fila RGB de un OZJ de 1 o 2 píxeles de ancho
  (el buffer puede ser de 1 × 1, `TextureSide`) mide 3 o 6 bytes: sin `glPixelStorei(..., 1)` GL
  lee de más (CL5.1, `GlTextureTest.UploadsAnRgbTextureOfOddWidth`).
- **`GL_CLAMP` no existe en el perfil core.** El original pide `0x2900`; en GL 1.1 con `GL_LINEAR`,
  el borde se mezcla con el color del borde (negro), y muchos drivers de Windows lo trataban como
  `GL_CLAMP_TO_EDGE`. *Recomendación:* `GL_CLAMP_TO_EDGE`, con la diferencia anotada en 07 hasta
  que la traza con `ImageLog` o una captura lo diga (CL5.1).
- **El alpha test es un `discard`.** `GL_GREATER 0,25` pasa a `if (color.a <= 0.25) discard;`
  solo con la mezcla normal, no con la aditiva (`0x620a90` lo apaga); y el color por vértice se
  recorta a [0, 1] antes de usarlo, como la función fija (06). Un 0,25 justo no se dibuja y un
  0,26 sí (CL5.2, `GlSpriteRendererTest.TheAlphaTestDropsAQuarter`).
- **El color que `0x621b60` pierde.** Con alfa, el quad deja el color en blanco: el siguiente sin
  alfa sale blanco aunque antes se haya pedido otro. Si MuRet guarda el color de antes, un quad
  puede salir de otro color que en el original; si lo pierde, hay que saber en qué escena se nota.
  *Recomendación:* reproducirlo en el `SpriteBatch` (el color actual pasa a blanco después de un
  quad con alfa) y anotarlo en 07 como contrato visible hasta que la traza diga si se ve
  (CL5.2, `SpriteBatchTest.AQuadWithAlphaLeavesTheColorWhite`).
- **El orden con mezcla.** La arena es aditiva sobre lo que haya; el texto va con su fondo
  semitransparente. Juntar quads de la misma textura en un solo `glDrawArrays` está bien solo si
  no se salta a otro que va en el medio: el `SpriteBatch` corta un comando cada vez que cambia la
  textura o la mezcla, y nunca reordena (CL5.2, `SpriteBatchTest.TheOrderIsKept`).
- **El texto no es GDI.** FreeType en mono no da los mismos píxeles ni, a veces, el mismo ancho que
  `GetTextExtentPointA`, y el texto 454 se ubica por su ancho: un píxel de diferencia corre todo.
  Los tests fijan la fórmula con un rasterizador falso (`TextLayoutTest`) y el ancho de FreeType
  con una fuente conocida, no contra GDI; la diferencia queda en 07 (CL5.3).
- **El texto de la escena no va al log ni a los tests.** Los textos de `Text.bmd` son del juego:
  los tests con datos reales comparan largos o hashes (`Fnv1a.h`), no el texto, y el log nombra el
  índice, no el contenido. La cara de la fuente, lo mismo (CL5.3 y CL5.4).
- **La cuenta y la clave en la UI.** El campo de la clave muestra asteriscos, no tiene
  `operator<<`, se vacía al mandar el login, y su `PrintTo` de los tests escribe solo el largo,
  como el de `Credentials` de la CL4. Ningún aviso muestra lo tecleado (CL5.5).
- **El texto tecleado y el foco.** En SDL3 el texto llega con `SDL_EVENT_TEXT_INPUT` solo después
  de `SDL_StartTextInput(ventana)`, y un IME manda edición en curso que no es texto. Los campos de
  la cuenta y la clave aceptan ASCII imprimible (el paquete no lleva otra cosa, Q-C43) y hasta 10
  bytes; lo demás se descarta sin aviso al servidor (CL5.5).
- **HiDPI.** Con `SDL_WINDOW_HIGH_PIXEL_DENSITY` la ventana y el framebuffer tienen tamaños
  distintos: la UI va en píxeles del framebuffer, y `Platform` pasa el mouse a esos píxeles con la
  densidad de la ventana. Hoy no se pide la bandera, y un test lo deja fijo (CL5.5).
- **Una textura que falta.** El original muestra "File not exist" y corta (Q-C23); MuRet no puede
  seguir sin el fondo del login, así que la escena falla con un error que nombra el archivo, y
  `Main` termina con él, como la CL3.4 con una carpeta de datos que no existe (CL5.4,
  `LoginSceneTest.AMissingTextureFailsWithItsName`).

---

## Sub-ola CL5.0: el pipeline gráfico (N.0, en `labs/`)

**Concepto:**
- de la función fija al pipeline programable: qué hacía `glBegin`/`glVertex`/`glColor`, la pila
  de matrices, `glAlphaFunc` y `glTexEnv`, y qué los reemplaza en el perfil core: buffers de
  vértices (VBO) y su formato (VAO, `glVertexAttribPointer`), un vertex shader que lleva cada
  vértice al clip con una matriz, un fragment shader que mezcla textura y color, y `discard`;
- las coordenadas: del espacio del objeto al clip, la división por w, el NDC de −1 a 1 y el
  viewport; por qué `gluOrtho2D(0, W, 0, H)` deja la y hacia arriba y el original da vuelta la suya
  (`H − y`);
- matrices en column-major y qué espera `glUniformMatrix4fv`;
- texturas: `glTexImage2D`, el formato y el tipo, `GL_UNPACK_ALIGNMENT`, el filtro (`NEAREST`,
  `LINEAR`), el wrap (`REPEAT`, `CLAMP_TO_EDGE`) y por qué el original usaba lados potencia de 2;
- mezcla: normal (`SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`) y aditiva (`ONE`, `ONE`), y por qué el orden
  de dibujo importa con una y no con la otra;
- primitivas: un `GL_TRIANGLE_FAN` de 4 vértices son los triángulos (0, 1, 2) y (0, 2, 3).

Del C++: RAII sobre nombres de GL (un `GLuint` que se borra en el destructor, se mueve y no se
copia, y que necesita el contexto vivo), `std::span` para subir un buffer, `glm::mat4` y por qué
`glm::value_ptr` da los 16 floats en el orden que espera GL.

**Archivos:**
- Crear: `docs/learning/ola-cl5.0-el-pipeline-grafico.md` (la guía; Claude)
- Crear: `labs/cl5.0-pipeline/OriginalQuad.h/.cpp` (los cuatro vértices de `0x621b60`),
  `Ortho.h/.cpp` (la matriz de `gluOrtho2D`, escrita a mano y comparada con `glm::ortho`),
  `Fan.h/.cpp` (de fan a triángulos), sus tests (`test_original_quad.cpp`, `test_ortho.cpp`,
  `test_fan.cpp`), `README.md`, `jefe/test_original_pipeline.cpp` (Emanuel) y
  `gl/test_lab_framebuffer.cpp` (el dibujo de verdad, con Xvfb)
- Modificar: `labs/CMakeLists.txt` (la biblioteca `MuRetLabsCl50` y el ejecutable `LabCl50Tests`
  con `gtest_discover_tests(... TEST_PREFIX "LabCl50.")`, como `LabCl40Tests`; el de `gl/` aparte,
  `LabCl50GlTests`, con la etiqueta `gl`), `labs/README.md`, `vcpkg.json` (`glm`)

- [x] **Paso 1 (Claude):** la guía, justo antes de empezar.
- [x] **Paso 2 (Emanuel, `TODO(human)`; lo resolvió Claude):** `Labs::OriginalQuad`: los cuatro
  vértices y las cuatro UV de `0x621b60` para (x, y, w, h, u, v, du, dv, escalar tamaño, escalar
  posición) en una pantalla de W × H, en el orden del fan. Primero los tests:
  `OriginalQuadTest.FlipsYLikeTheOriginal` (en 640 × 480, el `0x15` del login: (0, 455), (0, 135),
  (320, 135), (320, 455)), `ScalesFrom640x480` (en 1024 × 768: (0, 728), (0, 216), (512, 216),
  (512, 728)), `ScalesPositionAndSizeApart` (solo la posición, solo el tamaño),
  `TheUvGoesDownTheTexture` ((u, v), (u, v + dv), (u + du, v + dv), (u + du, v)).
- [x] **Paso 3 (Emanuel; lo resolvió Claude):** `Labs::Ortho`, la matriz de `gluOrtho2D(l, r, b, t)`
  a mano (la de la página de manual: 2/(r − l), 2/(t − b), −1 en z, y la traslación). Tests:
  `OrthoTest.MapsTheCornersToNdc` ((0, 0) a (−1, −1) y (W, H) a (1, 1)), `MatchesGlm` (los 16 floats
  de `glm::ortho(0, W, 0, H)` en 640 × 480 y 1024 × 768), `IsColumnMajor` (la traslación en los
  elementos 12 y 13).
- [x] **Paso 4 (Emanuel; lo resolvió Claude):** `Labs::FanToTriangles`: de n vértices de un fan a
  los índices de sus n − 2 triángulos. Tests: `FanTest.FourVerticesAreTwoTriangles` ((0, 1, 2),
  (0, 2, 3)), `ThreeVerticesAreOne`, `LessThanThreeAreNone`.
- [x] **Paso 5 (Emanuel, jefe; lo resolvió Claude):** el pipeline del original contra el de MuRet,
  sin GL: los vértices de `OriginalQuad` multiplicados por `Ortho` caen en el NDC donde caería el
  píxel que dibuja el original. Con el `0x15` en 1024 × 768, la esquina de arriba a la izquierda cae
  en (−1, 0,8958) y la de abajo a la derecha en (0, −0,4375); un punto en el medio de la UV
  (0,5; 0,5) cae en el centro del quad. Y lo mismo con el viewport del pase 3D,
  `0x620db0(0, 25, 640, 430)` en 1024 × 768: `glViewport(0, 40, 1024, 688)`, la fila de la selección
  del 06 (*verificado* en su traza).
- [x] **Paso 6 (Claude, `gl/`):** `LabCl50Gl.DrawsTheQuadIntoAFramebuffer`: con Xvfb, una ventana
  escondida, un FBO de 64 × 64, una textura de 2 × 2 con cuatro colores y el quad que la cubre
  entera: los cuatro cuadrantes del framebuffer leídos con `glReadPixels` son los cuatro colores,
  con el (0, 0) de la textura arriba a la izquierda. Sin pantalla, `GTEST_SKIP`.
- [x] **Paso 7 (Emanuel, lo anota en `labs/README.md`; lo resolvió Claude):** comparar, con la traza
  del 06 a mano, cuánto le cuesta al original mandar 95.000 vértices de a uno contra un VBO por
  frame, y por qué el orden de dibujo no se puede cambiar donde hay mezcla.
- [x] **Paso 8:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "LabCl50"
  xvfb-run -a ctest --preset linux-x64-debug -R "LabCl50Gl"
  ```
Hecho así: todos los números de los pasos 2 a 5 salieron como estaban escritos (con
`EXPECT_FLOAT_EQ`, porque el original no da enteros justos), y el desensamblado cambió dos cuentas
que el decompilado no muestra. `0x620d50` y `0x620d80` **multiplican por la inversa en float**
(`0x3acccccd` en `0x6ba070` y `0x3b088889` en `0x6ba06c`), no dividen: 25 escalado en 640 × 480 es
25,000002, y dividiendo daría otro float en 294 (con H = 768) a 401 (con H = 480) de las 481
coordenadas y enteras (`OriginalQuadTest.ScalesByTheInverseOfTheSize`). Y el borde de abajo de
`0x621b60` sale de la resta sin redondear: `FST` guarda `H − y` como float y lo deja en la pila, y
`FSUB` le resta h a ese (`0x621beb`..`0x621bf3`); en 1280 × 1024 el `0x15` va de 970,66669 a
287,99997, no a 288 (`TheBottomComesFromTheUnroundedTop`). El lab hace la cuenta en `double` (la x87
con los 53 bits de Windows) y guarda en float. Suma tests que el plan no tenía:
`FanTest.EachTriangleSharesTheFirstVertex`, y en el jefe `TheNdcGoesBackToThePixelOfTheOriginal` (en
las cuatro resoluciones del original), `BothTrianglesFaceTheCamera` (con la y para abajo quedan de
espaldas), `TheGameViewportTruncatesLikeTheTrace` (`0x620db0(0, 0, 640, 432)` da el `0,77,1024,691`
de la traza) y `At800x600TheViewportTruncatesAndTheQuadDoesNot` (el viewport en enteros sin signo
queda 0,75 y 0,25 píxeles corrido del quad escalado). En `gl/`, la ventana escondida usa
`SdlContext`, los deleters de `Window.h`, `LoadGlFunctions` y los chequeos de versión y perfil de
`ClientPlatform`, y una X-macro propia con 41 funciones (con el reintento en `opengl32.dll`), que es
el punto de partida de la CL5.1. Los nombres en CTest llevan la suite: el
`LabCl50Gl.DrawsTheQuadIntoAFramebuffer` de este plan es
`LabCl50Gl.LabFramebufferTest.DrawsTheQuadIntoAFramebuffer`, y al lado están la mezcla
(`TheOrderMattersOnlyWithNormalBlending`), `GL_UNPACK_ALIGNMENT` en 4
(`LabTextureTest.TheDefaultAlignmentReadsTheSecondRowFromByteEight`) y los nombres con dueño
(`GlNameTest`). Sin pantalla, los 4 de `gl/` que abren GL se saltean, y con
`MURET_CLIENT_REQUIRE_GL=1` fallan. El paso del CI de la pregunta 9 (Xvfb con `ctest -L '^gl$'`,
`MURET_CLIENT_REQUIRE_GL=1` y `--no-tests=error`) y `glm` en `vcpkg.json` llegaron con esta sub-ola,
no con la CL5.1. Las respuestas, con las salidas, en
[`labs/cl5.0-pipeline/README.md`](../../../labs/cl5.0-pipeline/README.md).

## Sub-ola CL5.R: las escenas de carga y de login, en el binario (P-C04, P-C13)

**Concepto:** carril R antes de dibujar. La tabla "Lo que dice el binario de las escenas de carga y
de login" salió del decompilado para este plan; esta sub-ola la confirma en el desensamblado,
cierra lo que la CL5 necesita y lo pasa al research. Lo que solo se ve con una traza queda escrito
como pendiente, con cómo se cierra.

**Archivos:** modificar `Client/docs/research/06-render-escenas-y-ui.md` (una sección nueva, "La
escena de carga y la de login": el orden de dibujo, las texturas, el texto, las ventanas, la lista),
`02-arranque-y-ciclo-de-vida.md` (las cinco escenas y sus transiciones, las fuentes de `WinMain`),
`05-formatos-de-datos.md` (la carpeta de cada mapa, de `0x61a560`), `07-quirks-y-bugs.md` (los
quirks nuevos), `08-pendientes.md` (P-C04 cerrado o con lo que falte, P-C12 con la carpeta de cada
mapa, y P-C13 con la traza), PROGRESS y este plan.

- [x] **Paso 1 (las escenas):** el `switch` de `0x63945d` y quién escribe `DAT_006d5dc8`
  (`0x622e40`, `0x634c60`, `0x6342c0`, `0x6356f0`, `0x633c10`, `0x4c2710`): de qué escena a cuál,
  y con qué. Sale la tabla de transiciones que prueba el `SceneManager` de la CL5.4.
- [x] **Paso 2 (la carga):** `0x622e40`, `0x65eeb0` y `0x647050`, `0x6402d0`: dónde y de qué
  tamaño va cada una de las seis texturas `lo_*`, y cómo avanza la barra (qué cuenta `0x61ccb0` y
  cuándo dibuja: si hay `SwapBuffers` en el medio de la carga).
- [x] **Paso 3 (el login, al entrar):** `0x6349a0` en orden; el mundo 55 por `0x61a560` (la
  carpeta de cada mapa, del `cmp` del desensamblado, a 05 y a P-C12); `0x61c7c0` con el tercer y
  cuarto argumento leídos en `0x63b870` y `0x63bd00` (el filtro y el wrap); el objeto `0xa2` y el
  nombre de su modelo (`0x60da30`, `DAT_006d2758`). Lo que queda abierto de la carpeta: quién
  escribe `DAT_07d366bc`, la bandera con que `0x616ec0` fuerza el mapa 7 (si nadie, es código
  muerto y `WorldFolder` no la lleva; si alguien, un caso más de `WorldFolderTest`); qué mapas
  mandan los tres handlers que llaman a `0x61a560` (`0x4c41c9`, `0x4c4e9d`, `0x4c780c`) y si
  alguno pasa de 36; y si `0x633f20` es la entrada a la selección con el 54 (*inferido* por la
  forma, igual a la de `0x6349a0`). El 35 da `World36`, que no está en el laboratorio: a P-C12.
- [x] **Paso 4 (el login, el dibujo):** `0x6350b0` con sus llamadas en orden (también el pase (2b)
  de la tabla: `0x621a90`, `0x620db0(0, 0, w, 0x30)`, `0x5fbb00` y `0x620fc0`), los argumentos de
  `0x622000` que Ghidra no muestra (cómo corre la arena con el reloj, y qué reloj: el T de
  `0x4eb620` u otro), qué apagan o prenden `0x6208f0`, `0x620890` y `0x6208b0` (la textura y el
  depth), el fundido (`DAT_07d3a0cc`, `DAT_006d5e30`: quién cuenta y cada cuánto), qué es la ventana
  de `+0x4584` que apaga el mundo, y qué dibuja cada una de las ocho llamadas comunes del final (el
  cursor, el chat, los avisos). El quad del texto: con qué banderas llama `0x554000` a `0x621b60`
  (la posición escalada y el tamaño en píxeles, *inferido* por el 06) y qué hacen el color, el fondo
  y el ancho de `DAT_006c8e5c`, `64` y `70`.
- [x] **Paso 5 (el texto):** las cuatro `CreateFontA` en el desensamblado (los 14 argumentos), el
  valor de `DAT_07758568` para un ancho que no está en la tabla, quién más escribe `0x6c76a0` (el
  charset), y quién llama a `0x466000` (`0x465eb0`): dónde se usan los colores `0x02`, con la
  tabla entera de `0x4665d0` y el formato de sus colores. Ningún texto de `Text.bmd` va al research:
  se nombran por índice.
- [x] **Paso 6 (las ventanas y la lista, P-C04):** las siete ventanas de `0x65f300`: qué clase es
  cada una (su vtable), cuál es la lista de servidores, cuál la cuenta y la clave y cuál la de Esc
  (`+0x3d0`); qué dibuja cada una con qué slots (`0xc` a `0x1a`), en qué píxeles, y qué hace con
  el mouse (`0x640940`) y con Enter y Esc. La lista: `0x4c1860` y su tabla de 25 índices (cómo
  ubica cada grupo de `código / 20`), qué muestra de la carga, y qué pasa al hacer clic (el `F4 03`
  de `0x4c6070`). La cuenta: quién llama al emisor del login (`0x641bf0`), con qué tecla o botón, y
  el campo de texto (`0x468320`): cuántos bytes acepta, qué caracteres y cómo dibuja la clave.
  *Inferido* hoy: la ventana de la cuenta aparece con el GS ya conectado (en el cable, el keepalive
  sale 2 y 4 s antes del login, y en una conexión sin login salen 34: el quirk del keepalive de la
  CL4.R).
- [x] **Paso 7 (los avisos):** el aviso `0x70` (`0x65fe50`), los textos de cada resultado del
  `F1 01` (por índice de `Text.bmd`), el de "no se pudo conectar" (402) y el del `FD_CLOSE` (3):
  dónde se dibuja y qué botón lo cierra.
- [x] **Paso 8:** pasar todo al research con *estático* o *inferido* en cada cosa, abrir P-C13 en
  08 con la traza y las capturas de la lista de arriba ("Lo que falta"), y corregir la tabla de
  este plan con lo que haya cambiado. Los quirks nuevos a 07: el color que pierde `0x621b60` y lo
  que salga de los pasos 4 a 6, y el 32, que deja el mapa del juego en 9 al cargar `World10`
  (`0x61a57d`; la CL7 lo reproduce o lo anota). Al 06, en "Primitivas 2D", lo que vio la CL5.0 en
  el desensamblado: `0x620d50` y `0x620d80` multiplican por la inversa en float (`0x6ba070`,
  `0x6ba06c`) y no dividen, y en `0x621b60` el borde de abajo sale de `H − y` sin redondear
  (`FST` y `FLD ST0`, `0x621beb`..`0x621bf3`), no del float guardado que muestra el decompilado.

Hecho así (2026-11-03): todo en el desensamblado de `c32bbd5f` con `objdump`, y casi todo
*estático*, porque no hay traza de estas escenas (P-C13). Los pasos 1 a 7 están en 06 (§ La escena
de carga y la de login, con la sección nueva y lo de la x87 en "Primitivas 2D"), en 02 (§ Las
cinco escenas y sus transiciones, y § Resolución, fuentes y texto) y en 05 (la carpeta de cada
mapa). Lo que cambió respecto de la tabla de arriba quedó corregido en ella. Lo que no estaba:

- la escena la escriben ocho `mov` con números fijos, y el `F1 02` (`0x4c2710`), si llegó cifrado,
  la lleva a 4 o a 2;
- la carga diseña su UI en 800 × 600, con la imagen de la UI nueva (`0x647650`, la misma de los
  fondos de las ventanas: UV a medio téxel, y atando la textura sin `0x620780`, Q-C64), y dibuja 11
  frames con `SwapBuffers` mientras carga;
- la ventana de `+0x4584` era la bandera de abierta de la de `+0x4578`; el `0x4c6070` del plan es el
  pedido de la lista, no el `F4 03` (ese lo manda el clic, `0x645e10`);
- hay una quinta resolución, 1600 × 1200, donde la letra queda en -1 y -2 (Q-C59);
- los campos de la cuenta y la clave son dos `EDIT` de Windows con `EM_LIMITTEXT` 10 y la clave
  con `ES_PASSWORD`; la ventana la abre el `F1 00`, con el GS conectado (ya no es *inferido*);
- el OK de cuatro avisos cierra el juego (Q-C69), y en el juego no hay avisos (Q-C70);
- la lista se corta en el primer lugar vacío y no muestra los grupos 21 a 23 (Q-C62), y no mira
  rangos ni topes: la ventana tiene 16 botones de servidor y un grupo puede tener 20 (Q-C63).

Los quirks nuevos van de Q-C57 a Q-C70; P-C04 quedó cerrado, P-C12 tiene la carpeta de cada mapa
y P-C13 lo que falta. Sin leer: las ventanas de `+0x3d0`, `+0x7e8` y `+0x4578`, los botones de la
barra de `+0xe40`, y el resto de `0x641bf0`, que llama a la tabla de hash de `0x40b240` (puede ser
el anticheat: fuera de alcance).

**Lo que la CL5.R deja para las sub-olas que siguen:**

- **CL5.1:** el filtro y el wrap de cada textura del juego del login: `GL_NEAREST` los fondos y
  casi todas las de `Interface\`; `GL_LINEAR` la arena, `FireSnuff` y `cr_mu_lo`; `GL_REPEAT` la
  arena, `server_ex03` y `server_ex02`. Las seis de la carga, `GL_LINEAR` y `GL_CLAMP`, salvo
  `lo_lo.jpg`, la barra, que repite (`GoldenTextureSet.TheLoginSetLoads` y el juego de la carga).
- **CL5.2:** Q-C57 sigue en `SpriteBatchTest.AQuadWithAlphaLeavesTheColorWhite`. La arena
  (`SkewedQuad`) con las UV de 06 (u, v + 0,25 dv arriba a la izquierda y u, v + 0,75 dv abajo).
  La UI de la carga no es el quad de `0x621b60`: va en 800 × 600 con la y desde arriba, `600 − y`
  y UV a medio téxel; o la `LoadingScene` la pasa a píxeles, o el `SpriteBatch` suma esa forma
  (`SpriteBatchTest.PlacesTheLoadingImagesLikeTheOriginal`, en 640 × 480 y en 1024 × 768). No es
  solo de la carga: es la imagen de toda la UI nueva, y las ventanas de la CL5.5 la usan en píxeles
  (fx = fy = 1, la y desde H), así que lo que se elija acá sirve para las dos.
- **CL5.3:** el texto de las trazas es el de `TextOut` distinto de 1: un `TextOutA` por texto, sin
  los colores `0x02`, que el original solo separa con `TextOut` en 1. `InlineColors` se escribe
  igual (lo usa el chat de la CL8), pero el login no lo necesita. El quad del texto: el tamaño
  siempre en píxeles, la posición escalada si el que llama lo pide, y du = (w + 0,01) / 256. La
  letra: n de 12 a 15 por ancho y 15 desde 1280 (Q-C59, `FontSetTest.TheStepFollowsTheWidth`); la
  y de los textos del login, con H (Q-C60, `TextLayoutTest.TheLoginTextUsesTheHeightForY`); el
  charset GB2312 (Q-C67).
- **CL5.4:** la tabla de transiciones del paso 1 es la de 02. `WorldFolderNumber` recibe de 0 a 255
  (el byte de los handlers) y da el mapa + 1 de 37 en adelante, con el 255 en 256: devuelve un
  `std::uint16_t` (`WorldFolderTest.MapsBeyondThirtySixAreMapPlusOne`); `OriginalMaps` sigue con los
  39 de los tests; la bandera de `0x616ec0` no va. La carga dibuja un frame por avance. El fundido
  no dibuja (Q-C66): `LoginSceneTest.DrawsInTheOrderOfTheOriginal` no tiene un quad de fundido. La
  arena: aditiva, con el color (0,25; 0,1; 0,1), u de los ms de la escena (Q-C65); los textos, sin
  copia (Q-C61).
- **CL5.5:** el orden de la lista, sus nombres por índice y lo que se aparta (Q-C62, Q-C63). En
  `ServerListModelTest.GroupsByCodeOverTwenty`, el 420 sería del grupo 21, que no tiene botón (MuRet
  lo ignora, Q-C62): el ejemplo pasa a 400 y 401 juntos y 380 aparte (el grupo 19). El 380 sale por
  la regla de MuRet: el original corta en el lugar 5, vacío, y no lo muestra. Los tests nuevos:
  `KeepsTheOrderOfTheOriginal`, `AListWithoutGroupTwentyShowsItsGroups`,
  `IgnoresTheGroupsWithoutAButton` y `AGroupKeepsTwentyServers`. **Decidido:** `ServerListWindow`
  tiene 20 botones de servidor, uno por cada número de un grupo: MuRet muestra todos los servidores
  del grupo, y el original, con 16, pisa sus barras desde el 17 (Q-C63,
  `ServerListWindowTest.ShowsTheTwentyServersOfAGroup`). Las imágenes de las ventanas tienen la
  forma de las de la carga, en píxeles (CL5.2). Los campos: 10 caracteres, la clave enmascarada; el
  original deja pasar todo salvo Esc y `0x19` a `0x1c`, y MuRet, ASCII imprimible (Q-C43). En el
  original, el subclass toma Tab y Enter (salvo los casos de 06): los manda a la ventana del juego,
  o Tab pasa el foco al control de `+0x98` si `+0x44` está prendido (en el login, sin leer); que Tab
  cambie de campo es de MuRet. Cancelar en la cuenta vuelve al CS y a la lista. En el aviso, Enter y
  Esc hacen lo de OK. `APeerCloseShowsText3` también ve el aviso `0x70` con el texto 402, como el
  `FD_CLOSE`; y `LoginSceneSessionTest.OkOnTheConnectNoticeQuits` fija Q-C69.

## Sub-ola CL5.1: `Renderer` y texturas desde OZT/OZJ

**Concepto:** de una textura en memoria (la de la CL1) a una textura de GL, y de una tabla de
texturas global a una que es de la escena y se suelta al irse. Va en dos PRs: **PR A**, la tabla
de GL que crece, `ClientRender`, el shader y `GlTexture`; **PR B**, el juego de texturas como dato
y `TextureTable`.

**Contrato** (05 § Texturas; 09; la tabla de arriba):

| Qué | Original | MuRet |
| --- | --- | --- |
| subir | `0x63b870` (OZJ, `GL_RGB`) y `0x63bd00` (OZT, `GL_RGBA`): el buffer de lados potencia de 2 con `glTexImage2D` | `GlTexture::Upload(const Data::Textures::Texture&, Sampling)`: el mismo buffer, el mismo formato, `GL_UNPACK_ALIGNMENT` en 1 |
| el filtro y el wrap | el tercer y cuarto argumento: `0x2600`/`0x2601`, `0x2900`/`0x2901` | `Sampling{Filter::Nearest/Linear, Wrap::Clamp/Repeat}`; `Clamp` es `GL_CLAMP_TO_EDGE` (no hay `GL_CLAMP` en core; a 07) |
| el nombre | pide `<ruta>.jpg` o `.tga` y abre `.OZJ` u `.OZT` (09) | `TextureSetEntry::path` como lo escribe el original; `ResolveTexturePath` cambia la extensión y busca con `ResolveDataPath` (CL1) |
| el slot | una tabla global de 1.845 slots (`0x7d3a198`, `0x38` bytes, el nombre de GL en `+0x2c`) | `TextureTable`, indexada por `TextureSlot` (el número del original), de la escena (pregunta 11) |
| liberar | `0x63c150(slot)` | `TextureTable::Release(slot)` y el destructor |
| una que falta | "File not exist", `MessageBox` y `WM_DESTROY` (Q-C23) | `LoadTextureSet` devuelve `TextureSetError::Missing` con la ruta; quien carga la escena falla |
| una que no se lee | el OZJ que es TGA (Q-C14), el OZT de 24 bits (Q-C29): no se sube y no avisa | `TextureSetError::Unreadable` con la ruta y el `DataError`: la escena falla (para el login, ninguna de las 18 de `0x61c7c0` lo es: 5 OZJ y 13 de `Interface\`, porque el `0xa2` es un modelo; lo prueba el golden) |

**Archivos:**
- **PR A:** crear `Client/src/Platform/GlFunctionList.h` (la X-macro),
  `Client/src/Render/Gl/GlShader.h/.cpp`, `GlTexture.h/.cpp`, `GlFramebuffer.h/.cpp` (para los
  tests), `GlRenderer.h/.cpp` (el shader del 2D, el VAO y el VBO; dibuja en la CL5.2),
  `Client/src/Render/Gl/Shaders.h` (el GLSL 330 como `constexpr std::string_view`),
  `Client/tests/render_gl/CMakeLists.txt`, `test_gl_shader.cpp`, `test_gl_texture.cpp` y
  `Client/tests/support/GlTestContext.h/.cpp` (la ventana escondida, el contexto y el salteo de la
  pregunta 9). Modificar `Client/src/Platform/GlLoader.h/.cpp`, `Client/CMakeLists.txt`
  (`ClientRender`, el filtro de `ClientLib`, `glm`), `vcpkg.json`, `Client/tests/CMakeLists.txt`
  (`add_subdirectory(render_gl)`), el CI (el paso de Xvfb con `ctest -L gl` y
  `MURET_CLIENT_REQUIRE_GL=1`), el roadmap (D-C2: el loader y glm) y este plan. *Nota de la
  CL5.0:* `glm` en `vcpkg.json` y el paso del CI ya llegaron con ella: la CL5.1 enlaza
  `glm::glm-header-only` en `ClientRender` y le pone la etiqueta `gl` a los tests de `render_gl/`
  (`gtest_discover_tests(... PROPERTIES LABELS gl)`, como `LabCl50GlTests`), y el paso los corre
  sin cambiar el CI. La X-macro de `labs/cl5.0-pipeline/gl/LabGl.h`, con 41 funciones y quién usa
  cada grupo, es el punto de partida de `GlFunctionList.h`, y `HiddenGlWindow` (que no se mueve:
  `Create` la da en un `unique_ptr`, porque los nombres de GL guardan un puntero a sus funciones),
  el del `GlTestContext`. Modificar también `labs/cl5.0-pipeline/gl/LabGl.cpp` y
  `HiddenGlWindow.cpp`: el lab deja su copia del pedido de `Platform/GlLoader.cpp` y pide las
  funciones con el de `Platform`, y lee `MURET_CLIENT_REQUIRE_GL` con
  `Core::ReadEnvironmentVariable` (de la pila de la CL4) en vez de copiar el de `GoldenDir.cpp`
  (CODING_RULES §4).
- **PR B:** crear `Client/src/Data/Textures/Sampling.h`, `TextureSet.h/.cpp` (la entrada, el error y
  `LoadTextureSet`), `Client/src/Render/TextureSlot.h`, `Client/src/Render/ITextureStore.h` (lo que
  ve la escena), `Client/src/Render/Gl/TextureTable.h/.cpp`,
  `Client/tests/data/test_texture_set.cpp`, `test_golden_texture_set.cpp` y
  `Client/tests/render_gl/test_texture_table.cpp`. Modificar PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Data::Textures
{
// El tercer y cuarto argumento de 0x63b870 y 0x63bd00.
enum class Filter : std::uint8_t { Nearest, Linear };  // 0x2600, 0x2601
enum class Wrap : std::uint8_t { Clamp, Repeat };      // 0x2900, 0x2901
struct Sampling
{
    Filter filter = Filter::Nearest;
    Wrap wrap = Wrap::Clamp;
};

// Una textura de un juego: como la pide el original ("Logo\\Login_Back01.jpg"), su slot y cómo se
// muestrea. Un juego es un dato (una tabla por escena o por mundo), nunca una lista en el código
// que la usa: la escena de login tiene el suyo, y el mundo N el de su carpeta (CL7).
struct TextureSetEntry
{
    std::string_view path;
    std::uint16_t slot;
    Sampling sampling;
};

enum class TextureSetErrorKind : std::uint8_t { Missing, Unreadable };
struct TextureSetError
{
    TextureSetErrorKind kind;
    std::string path;            // la que pidió el original
    std::optional<DataError> data; // con Unreadable
};

struct LoadedTexture
{
    std::uint16_t slot;
    Sampling sampling;
    Texture texture;
};

// ".jpg" → ".OZJ" y ".tga" → ".OZT", sin mirar mayúsculas, y ResolveDataPath (CL1).
std::optional<std::filesystem::path> ResolveTexturePath(const std::filesystem::path& dataRoot,
                                                        std::string_view originalPath,
                                                        std::error_code& error);
// Lee todas, en orden; la primera que falta o no se lee corta.
Core::Expected<std::vector<LoadedTexture>, TextureSetError> LoadTextureSet(
    const std::filesystem::path& dataRoot, std::span<const TextureSetEntry> entries);
} // namespace Client::Data::Textures

namespace Client::Render
{
// El número de slot del original (0x15 es Login_Back01): lo que nombran las trazas (09).
struct TextureSlot
{
    std::uint16_t value;
    friend bool operator==(TextureSlot, TextureSlot) = default;
};
inline constexpr std::size_t OriginalTextureSlotCount = 1845; // 0x64e6 dwords en 0x622e40

// Dónde sube y suelta sus texturas una escena (ClientLib), sin ver GL: lo implementa
// Gl::TextureTable y, en los tests de las escenas (CL5.4), el FakeTextureStore de support/.
class ITextureStore
{
public:
    virtual ~ITextureStore() = default;
    virtual Core::Expected<void, std::string> Load(
        std::span<const Data::Textures::LoadedTexture> textures) = 0;
    virtual void Release(TextureSlot slot) = 0;
};
} // namespace Client::Render

namespace Client::Render::Gl
{
class GlTexture // se mueve, no se copia; necesita el contexto vivo
{
public:
    static Core::Expected<GlTexture, std::string> Upload(const Platform::GlFunctions& gl,
                                                         const Data::Textures::Texture& texture,
                                                         Data::Textures::Sampling sampling);
    [[nodiscard]] GLuint Name() const;
    [[nodiscard]] int Width() const;  // el del buffer: textureWidth
    [[nodiscard]] int Height() const;
};

class TextureTable final : public Render::ITextureStore
{
public:
    explicit TextureTable(const Platform::GlFunctions& gl);
    Core::Expected<void, std::string> Load(
        std::span<const Data::Textures::LoadedTexture> textures) override;
    [[nodiscard]] const GlTexture* Find(TextureSlot slot) const; // nullptr si no está
    void Release(TextureSlot slot) override;
};
} // namespace Client::Render::Gl
```

- [x] **Paso 1 (PR A):** los puertos de vcpkg en el baseline del repo (`builtin-baseline` `9a5dfef5`
  de `vcpkg.json`), que Context7 no ve: se miran en el registro de vcpkg en ese commit. La revisión
  del plan ya los miró: `glm` 1.0.3, `freetype` 2.14.3 (features por defecto `brotli`, `bzip2`,
  `png` y `zlib`; FTL o GPL-2.0+; se suma en la CL5.3) y `sdl3-ttf` 3.2.2#1 (descartado). Con
  Context7, las funciones de GL 3.3 core que se piden. La lista de la X-macro, con el uso de cada
  una.
- [x] **Paso 2 (PR A):** los tests sin ventana: `GlLoaderTest.EveryFunctionHasItsName` (la lista
  no repite nombres, y cada uno empieza con `gl`); con ventana (`render_gl/`, pregunta 9):
  `GlShaderTest.CompilesTheSpriteShader`, `GlShaderTest.ABadShaderSaysItsLog` (el log de
  `glGetShaderInfoLog` en el error), `GlTextureTest.UploadsAnRgbaTexture` (el OZT sintético de
  2 × 2: los cuatro texeles leídos de un FBO), `GlTextureTest.UploadsAnRgbTextureOfOddWidth` (un
  OZJ de 1 × 1 y uno de 3 × 2 en su buffer de 4 × 2: las filas no se corren),
  `GlTextureTest.NearestAndLinearSampleAsAsked`, `GlTextureTest.RepeatWrapsAndClampDoesNot`.
- [x] **Paso 3 (PR A):** la X-macro y `ClientRender`; `GlShader`, `GlTexture`, `GlFramebuffer` y
  el `GlTestContext`. El CI corre `render_gl` con Xvfb.
- [x] **Paso 4 (PR B):** los tests de los datos: `TextureSetTest.ChangesTheExtensionLikeTheOriginal`
  (`.jpg` → `.OZJ`, `.TGA` → `.OZT`, otra extensión queda), `TextureSetTest.AMissingFileSaysItsPath`
  (en una carpeta temporal), `TextureSetTest.AnUnreadableFileSaysWhy` (un OZT de 24 bits),
  `TextureSetTest.LoadsInOrder`; golden: `GoldenTextureSet.TheLoginSetLoads` y
  `TheLoadingSetLoads` (las tablas de la CL5.4, contra el `Data\` del laboratorio: todas están y
  todas se leen, con el tamaño de cada una), y con ventana `TextureTableTest.FindsWhatItLoaded`,
  `TextureTableTest.ReleaseForgetsTheSlot`.
- [x] **Paso 5 (PR B):** `Sampling`, `TextureSet`, `TextureSlot` y `TextureTable`.
- [x] **Paso 6:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "GlLoader|TextureSet"
  xvfb-run -a env MURET_CLIENT_REQUIRE_GL=1 ctest --preset linux-x64-debug -L '^gl$'
  ```
Hecho así (PR A, 2026-11-03): la X-macro de `Platform/GlFunctionList.h` tiene 46 funciones en dos
grupos: `MURET_GL_QUERY_FUNCTIONS` (`glGetString` y `glGetIntegerv`) y el resto. `LoadGlFunctions`
pide las dos primeras, mira la versión y el perfil (el chequeo que estaba en `Window.cpp` y copiado
en el lab) y recién ahí pide el resto, así que un driver viejo dice su versión y no la primera
función de la 3.3 que le falta. Lo prueba `GlLoaderDriverTest`, sin ventana: `LoadGlFunctions`
recibe cómo pedir cada función (`GlProcLookup`, por defecto `GlProcAddress`), y un driver falso que
dice 2.1, o 3.3 sin el perfil core, recibe solo los dos pedidos de la consulta. El pedido de una
función quedó público (`Platform::LoadGlFunction`, con el reintento en `opengl32.dll`), y el lab
pide con él lo único que le queda, `glDrawElements`; lee `MURET_CLIENT_REQUIRE_GL` con
`Core::ReadEnvironmentVariable`, que llegó con el merge de `origin/claude/cl45` porque `main`
todavía no tiene `Core/Environment.h`. Cuatro cosas cambiaron de lugar: `Data/Textures/Sampling.h`
vino al PR A, porque `GlTexture::Upload` lo recibe; `Render/SpriteVertex.h`, con `Color`, se
adelantó de la CL5.2, y `GlRenderer` ya dibuja triángulos (`Draw(span<const SpriteVertex>, const
GlTexture&, PixelSize)`), para probar con píxeles leídos que la y va para arriba como en
`gluOrtho2D`, que el color del vértice modula la textura y que el viewport es la pantalla
(`GlRendererTest`); y `GlFramebuffer` y `GlName` (este último, el del lab) están en `ClientRender`,
no en los tests. El test de ancho impar no usa el 1 × 1 ni el 3 × 2 en 4 × 2 que decía el paso 2:
con `GL_RGB` sus filas miden 3 y 12 bytes, y ninguno corre filas con el alineado en 4. Usa un 2 × 2
(filas de 6 bytes), un 1 × 2 (de 3) y el 3 × 2 en 4 × 2 con la columna de relleno en negro.
`GL_UNPACK_ALIGNMENT` vuelve al valor que tenía, no a 4 fijo (`LeavesTheUnpackAlignmentAsItWas`).
`GL_CLAMP` (`0x2900`) no existe en el perfil core: `Wrap::Clamp` es `GL_CLAMP_TO_EDGE`, y la
diferencia con `GL_LINEAR` en el borde es Q-C73. Suma tests que el paso 2 no tenía:
`GlShaderTest.ABadLinkSaysItsLog` (un `vec2` contra un `vec3`, porque Mesa enlaza una entrada que
nadie escribe), `GlTextureRulesTest` (un buffer que no mide lo que dice no llama a GL),
`GlFramebufferTest`, `GlTestContextTest` (abre un 3.3 core y carga las 46) y los cuatro `GlNameTest`
del lab. Los de `render_gl/` llevan la etiqueta `gl` y 60 s cada uno: `muret_add_test` suma `LABEL`
y `TIMEOUT`, y la ventana escondida va en `ClientGlTestSupport`, aparte de `ClientTestSupport`, para
que el resto de los tests no enlace GL. Los mutantes a mano (sin `GL_UNPACK_ALIGNMENT` en 1, la
vuelta a 4 fijo, sin `WRAP_T`, sin el filtro de minificación, `Clamp` como `GL_REPEAT`, `Linear`
como `GL_NEAREST`, RGB como RGBA, sin el chequeo del buffer, sin viewport, la y para abajo, sin
modular, el log del shader vacío y un nombre de GL repetido o sin `gl`) mueren todos.
En la revisión del PR A se sumaron tres cosas: `GlLoaderDriverTest` (un `LoadGlFunctions` que
pedía todas antes de mirar el contexto pasaba todos los tests),
`GlRendererTest.EachThreeVerticesAreATriangle` (dos quads separados en un solo `Draw`: con
`GL_TRIANGLE_FAN` o `GL_TRIANGLE_STRIP` pasaban todos) y, en `NearestAndLinearSampleAsAsked`, el
téxel de lejos con `Nearest` puede ser cualquiera de los cuatro: el centro cae justo en el borde
entre téxeles, y una GPU de verdad puede redondear la UV para cualquier lado.
Hecho así (PR B, 2026-11-03): `LoadTextureSet` es la primera lectura del disco de la capa de
datos (los cargadores de la CL1 reciben los bytes). El nombre sale como en los dos cargadores, que
copian hasta el **primer** punto, con él, y le ponen su extensión (`0x63b8e4`..`0x63b8fd` en el del
OZJ): `Monster\bossmap1_R.jpg.jpg` abre `bossmap1_R.OZJ`. La extensión la pone el cargador y no
el nombre, así que "otra extensión queda" del paso 4 no es como el original: el cargador sale de la
extensión pedida (`.jpg` el OZJ, `.tga` el OZT, sin mirar mayúsculas, `TextureLoaderOf`), y una que
no es ninguna de las dos es un error de la tabla, `TextureSetErrorKind::UnknownFormat`, un tercer
tipo que el contrato no tenía; no se busca en el disco. Sin punto, el original no escribe el byte
que va antes de la extensión (Q-C74); MuRet pone un punto. `ResolveTexturePath` no quedó: lo hacen
`TextureFileName` y `ResolveDataPath`. `TextureSetError` suma el `std::error_code` del sistema
(una carpeta sin permiso, un archivo de más de 16 MB) y `Describe`, que lo pone en palabras con la
ruta pedida. El contrato pedía `Core::Expected<void, std::string>` y no existía: `Expected<void, E>`
llegó con este PR, con sus tests (`ExpectedVoidTest`), y la CL5.4 lo usa en `Enter` y `Start`.
`Scenes/TextureSets.h`, las tablas de la CL5.4, vino acá para el golden: es solo el `.h`, con
`constexpr`, y sus tests (`TextureSetsTest`: cada entrada con cargador, slots sin repetir y dentro
de los 1.845, y cuántas hay de cada muestreo). El golden quedó en
`tests/scenes/test_golden_texture_sets.cpp` y no en `tests/data/`, porque lee las tablas de
`Scenes`: las 24 están y se leen, cada una con su slot, el formato de su cargador, el tamaño de la
imagen medido en el `Data\` del laboratorio y su buffer, sin mirar un píxel. `TextureTable` guarda
1.845 `std::optional<GlTexture>` indexados por el slot, ni se copia ni se mueve (las escenas la
guardan como `ITextureStore&`), reemplaza un slot ocupado y corta con un slot fuera de la tabla o
con una textura que `GlTexture` no sube, con el slot en hexadecimal en el error. Suma tests que el
paso 4 no tenía: `TextureSetTest.FindsTheFileInAnyCase`, `AnUnknownExtensionIsRefused` y
`AnEmptySetLoadsNothing`, y `TextureTableTest.ALoadReplacesTheSlot`,
`ASlotOutsideTheTableIsRefused` y `AnUploadThatFailsSaysTheSlot`. Los mutantes a mano (cortar en
el último punto, siempre `.OZJ`, `.tga` con mayúsculas, siempre el cargador del OZT, una que falta
como ilegible, sin el error del cargador, seguir después de un error, sin el slot o sin el
muestreo, un `Release` que no suelta, una carga que no reemplaza, el tope corrido en uno, un `Find`
sin tope y un `Expected<void>` que siempre es éxito) mueren todos; el del tope corrido muere por un
acceso fuera del vector, no por una aserción.
En la revisión del PR B se sumaron dos tests: `TextureSetTest.ANameWithoutADotGetsOne` (el punto que
pone MuRet donde el original deja un byte de la pila, Q-C74: un nombre armado con
`substr(0, punto + 1)` pasaba todos los tests) y `TextureTableTest.TheLastSlotIsInTheTable` (el
slot 1.844 se carga: un tope corrido en uno para el otro lado, que rechazaba el último, también
pasaba). `TextureLoaderOf` compara con `Data::EqualsIgnoringAsciiCase`, la de `ResolveDataPath`,
que pasó a `DataPath.h` en vez de tener una tercera copia, y su comentario dice que las texturas de
un modelo (`0x60d5f0`, CL6) eligen el cargador con otra regla: la letra después del primer punto.

## Sub-ola CL5.2: `SpriteBatch` en 640 × 480 escalado

**Concepto:** coordenadas lógicas contra físicas (D-C11): la UI escalada desde 640 × 480, la que va
en píxeles, y la Y dada vuelta del original; y por lotes: muchos quads en un buffer y pocos
`glDrawArrays`, sin cambiar el orden.

**Contrato** (06 § Primitivas 2D; la tabla de arriba):

| Qué | Original | MuRet |
| --- | --- | --- |
| entrar al 2D | `0x6219b0`: viewport entero y `gluOrtho2D(0, W, 0, H)`, en píxeles reales | `SpriteBatch::Begin(PixelSize)`; el renderer pone el viewport y `glm::ortho(0, W, 0, H)` |
| el quad | `0x621b60(slot, x, y, w, h, u, v, du, dv, escalar tamaño, escalar posición, alfa)` | `SpriteBatch::Quad(OriginalQuad)`, los mismos campos y en el mismo orden; los vértices de la tabla de arriba. *De la CL5.0:* el borde de abajo es `H − y − h` restado en `double` sin redondear `H − y` antes, como `Labs::FanVertices` (en 1280 × 1024 el `0x15` termina en 287,99997, no en 288) |
| escalar | `0x620d50` (`x · W / 640`) y `0x620d80` (`y · H / 480`), cada eje por su lado | `ScaleX`, `ScaleY`, en `float` como el original; en una ventana ancha, lo escalado se estira (pregunta 10). *De la CL5.0:* multiplican por la inversa en float (`1.0f / 640`, `1.0f / 480`, las de `0x6ba070` y `0x6ba06c`) con la cuenta en `double`, como la x87, y no dividen: 25 da 40,000004 en 1024 × 768 y 56,250004 en 1920 × 1080, un ULP arriba |
| sin textura | `0x621aa0` | `SpriteBatch::ColorQuad` |
| la arena | `0x622000`, la v corrida 0,25 · dv | `SpriteBatch::SkewedQuad`, con lo que diga la CL5.R |
| el color | `glColor` global; con alfa, (1, 1, 1, alfa) por vértice y después blanco | `SpriteBatch::SetColor`; con alfa, el vértice va con (1, 1, 1, alfa) y el color queda blanco (Q-C nuevo, "Lo que más puede morder"); el shader lo recorta a [0, 1] |
| la mezcla | `0x620a00` normal con alpha test; `0x620a90` aditiva sin | `BlendMode::Alpha` (con `discard` en 0,25) y `BlendMode::Additive` |
| el lote | un `glBegin(GL_TRIANGLE_FAN)` por quad | un `DrawCommand` por tramo de la misma textura y mezcla, con los dos triángulos de cada fan; nunca reordena |

**Archivos:** crear `Client/src/Render/ScreenScale.h/.cpp`, `SpriteVertex.h`, `OriginalQuad.h`,
`SpriteBatch.h/.cpp`, `Client/src/Render/Gl/GlSpriteRenderer.h/.cpp`,
`Client/tests/render/CMakeLists.txt` (`muret_add_module_tests()`), `test_screen_scale.cpp`,
`test_sprite_batch.cpp` y `Client/tests/render_gl/test_gl_sprite_renderer.cpp`. Modificar
`Client/tests/CMakeLists.txt` (`add_subdirectory(render)`), `Client/src/Render/Gl/Shaders.h`,
PROGRESS y este plan.
*Nota de la CL5.1:* `SpriteVertex.h`, con `Color`, ya llegó con la CL5.1, y `Gl::GlRenderer` ya
sube vértices y dibuja triángulos con una textura (`Draw(span<const SpriteVertex>, const GlTexture&,
PixelSize)`), con el viewport y `glm::ortho(0, W, 0, H)`. `GlSpriteRenderer` lo usa por comando o
lo absorbe; el alpha test, la mezcla y el color recortado a [0, 1] son de la CL5.2.

**Interfaces:**

```cpp
namespace Client::Render
{
// 0x620d50 y 0x620d80: de 640 × 480 a la pantalla, cada eje por su lado.
float ScaleX(float x, App::PixelSize screen);
float ScaleY(float y, App::PixelSize screen);

enum class BlendMode : std::uint8_t { Alpha, Additive }; // 0x620a00 y 0x620a90

struct Color
{
    float red = 1.0f, green = 1.0f, blue = 1.0f, alpha = 1.0f;
};

// Los argumentos de 0x621b60, en su orden.
struct OriginalQuad
{
    TextureSlot slot;
    float x = 0, y = 0, width = 0, height = 0;
    float u = 0, v = 0, du = 1, dv = 1;
    bool scaleSize = false;
    bool scalePosition = false;
    float alpha = 0; // > 0: cada vértice con (1, 1, 1, alpha), y el color queda blanco
};

struct SpriteVertex
{
    float x, y; // píxeles de la pantalla, con la y hacia arriba (gluOrtho2D)
    float u, v;
    Color color;
};

struct DrawCommand
{
    std::optional<TextureSlot> texture; // sin textura: ColorQuad
    BlendMode blend;
    std::size_t firstVertex;
    std::size_t vertexCount; // de a 6: dos triángulos por quad
};

class SpriteBatch
{
public:
    void Begin(App::PixelSize screen);
    void SetBlend(BlendMode blend);
    void SetColor(Color color);
    void Quad(const OriginalQuad& quad);
    void ColorQuad(float x, float y, float width, float height, bool scale);
    [[nodiscard]] std::span<const SpriteVertex> Vertices() const;
    [[nodiscard]] std::span<const DrawCommand> Commands() const;
};
} // namespace Client::Render

namespace Client::Render::Gl
{
class GlSpriteRenderer
{
public:
    explicit GlSpriteRenderer(const Platform::GlFunctions& gl); // compila el shader
    // Sube los vértices (un VBO que se rehace por frame), pone el viewport y la matriz, y corre
    // los comandos en orden. Un slot que la tabla no tiene es un aviso y el comando se saltea.
    void Draw(const SpriteBatch& batch, const TextureTable& textures, Core::Log& log);
};
} // namespace Client::Render::Gl
```

- [x] **Paso 1 (Emanuel, `TODO(human)`; lo resolvió Claude):** antes de los tests, la tabla de los
  quads del login en 640 × 480 y en 1024 × 768: para el `0x15`, el `0x16` y las dos arenas, los
  cuatro vértices y sus UV, calculados a mano con las fórmulas de `0x621b60` y `0x622000`.

  Los argumentos, de 06 § El login, el dibujo: `0x621b60(0x15, 0, 25, 320, 320, 0, 0, 1, 1, 1, 1,
  0)`, el `0x16` igual en x = 320, y las arenas `0x622000(0x5c2, 0, 25, 640, 430, u, 0, 0,3, 0,3)`
  y `0x622000(0x5c3, 0, 25, 640, 430, u, 0, 3, 2)`, con la u del reloj en 0. Cada esquina es
  (x, y; u, v), en el orden del fan; la y, en píxeles con la y para arriba. *Estático* hasta la
  traza de P-C13.

  | quad | pantalla | arriba a la izquierda | abajo a la izquierda | abajo a la derecha | arriba a la derecha |
  | --- | --- | --- | --- | --- | --- |
  | `0x15` | 640 × 480 | (0, 455; 0, 0) | (0, 134,99997; 0, 1) | (320, 134,99997; 1, 1) | (320, 455; 1, 0) |
  | `0x16` | 640 × 480 | (320, 455; 0, 0) | (320, 134,99997; 0, 1) | (640, 134,99997; 1, 1) | (640, 455; 1, 0) |
  | `0x5c2` | 640 × 480 | (0, 455; 0, 0,075) | (0, 24,999968; 0, 0,225) | (640, 24,999968; 0,3, 0,3) | (640, 455; 0,3, 0) |
  | `0x5c3` | 640 × 480 | (0, 455; 0, 0,5) | (0, 24,999968; 0, 1,5) | (640, 24,999968; 3, 2) | (640, 455; 3, 0) |
  | `0x15` | 1024 × 768 | (0, 728; 0, 0) | (0, 216; 0, 1) | (512, 216; 1, 1) | (512, 728; 1, 0) |
  | `0x16` | 1024 × 768 | (512, 728; 0, 0) | (512, 216; 0, 1) | (1024, 216; 1, 1) | (1024, 728; 1, 0) |
  | `0x5c2` | 1024 × 768 | (0, 728; 0, 0,075) | (0, 39,999935; 0, 0,225) | (1024, 39,999935; 0,3, 0,3) | (1024, 728; 0,3, 0) |
  | `0x5c3` | 1024 × 768 | (0, 728; 0, 0,5) | (0, 39,999935; 0, 1,5) | (1024, 39,999935; 3, 2) | (1024, 728; 3, 0) |

  Las cuentas, con la inversa en float de la CL5.0. En 640 × 480, 25 escalado es 12.000 · 1/480
  = 25,0000013, guardado 25,000002, y 480 menos eso es 454,9999981 en la pila, guardado 455. El
  alto, 320 escalado, es 320,0000168, guardado 320,00003, y el borde de abajo sale de la pila:
  454,9999981 − 320,0000305 = 134,9999676, guardado 134,99997. La arena: 430 escalado es
  430,0000225, guardado 430,00003, y abajo da 24,9999676, guardado 24,999968. En 1024 × 768, 25
  escalado es 40,000004 y arriba queda 727,9999962, guardado 728; 320 escalado es 512,0000269,
  guardado 512 justo, y abajo, 215,9999962, guardado 216. La arena, 688,00006 de alto: abajo,
  39,9999352, guardado 39,999935. Las x son enteras: 320 y 640 escalados en 640 son
  320,0000048 y 640,0000095, que en float dan 320 y 640 (y 512 y 1024 en 1024), y la derecha es
  x + w. La v de la arena, de la x87: el cuarto de dv es 0,3 · 0,25 = 0,075000000745, guardado
  0,075, y abajo a la izquierda va v + dv sin redondear menos ese cuarto, 0,2250000089, guardado
  0,225 (`OriginalSkewedQuadTest.TheLowerLeftVComesFromTheUnroundedSum`). Las UV no se escalan.
- [x] **Paso 2:** los tests:
  - `ScreenScaleTest.ScalesEachAxisApart` (en 1024 × 768, 320 → 512 y 25 → 40; en 1920 × 1080,
    320 → 960 y 25 → 56,25; con `EXPECT_FLOAT_EQ`, porque los 25 dan un ULP arriba, CL5.0);
  - `SpriteBatchTest.PlacesTheLoginBackgroundLikeTheOriginal` (los vértices del paso 1, en los dos
    tamaños; los de 640 × 480, *estático* hasta la traza de P-C13);
  - `SpriteBatchTest.WithoutTheScaleFlagsItIsInPixels` (y con una sola de las dos);
  - `SpriteBatchTest.TheFanIsTwoTrianglesInOrder` ((0, 1, 2) y (0, 2, 3) del fan del original);
  - `SpriteBatchTest.AnAlphaAboveZeroIsTheVertexColor` ((1, 1, 1, alfa) en los cuatro);
  - `SpriteBatchTest.AQuadWithAlphaLeavesTheColorWhite` (rojo, un quad con alfa, otro sin: el
    segundo es blanco);
  - `SpriteBatchTest.AZeroAlphaUsesTheCurrentColor`;
  - `SpriteBatchTest.TheColorIsClampedToOne` (1,34 queda 1, como la función fija; 06);
  - `SpriteBatchTest.QuadsOfTheSameTextureAreOneCommand`, `ADifferentBlendStartsACommand`,
    `TheOrderIsKept` (A, B, A: tres comandos, en ese orden);
  - con ventana: `GlSpriteRendererTest.TheFirstRowIsAtTheTop` (la textura de 2 × 2 como fondo),
    `GlSpriteRendererTest.AdditiveAddsAndAlphaBlends`, `TheAlphaTestDropsAQuarter` (0,25 no se
    dibuja, 0,26 sí, y con la aditiva los dos), `AMissingSlotIsSkippedWithAWarning`.
- [x] **Paso 3:** `ScreenScale`, `SpriteBatch` y `GlSpriteRenderer`.
- [x] **Paso 4:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug
  ctest --preset linux-x64-debug -R "ScreenScale|SpriteBatch"
  xvfb-run -a env MURET_CLIENT_REQUIRE_GL=1 ctest --preset linux-x64-debug -L '^gl$' \
    -R GlSpriteRenderer
  ```
Hecho así (2026-11-03): las cuentas del original quedaron en `ClientLib`, sin GL, cada primitiva en
su archivo: `Render/ScreenScale` (`0x620d50` y `0x620d80`), `OriginalQuad` (`FanCorners`, las cuatro
esquinas de `0x621b60`), `OriginalSkewedQuad` (`SkewedFanCorners`, las de `0x622000`) y `UiImage`
(`UiImageCorners` y `DesignScale`), con las esquinas en `QuadCorner.h`. El `SpriteBatch` las junta
en vértices y tramos. Las del lab de la CL5.0 se movieron acá: `labs/cl5.0-pipeline/OriginalQuad.h`
quedó como alias de las del cliente, su `.cpp` se borró y `MuRetLabsCl50` enlaza `ClientLib`, así
que los `OriginalQuadTest` del lab prueban el código del cliente. `ScaleX` y `ScaleY` escalan cada
eje por su lado en cualquier resolución que valga (`App/Resolution.h`, #72); el caso de 1920 × 1080
sigue, porque es un modo de monitor. Lo que se aparta del contrato: (a) `ColorQuad(x, y, w, h)` no
lleva el `bool scale`: `0x621aa0` recibe cuatro argumentos, escala los cuatro siempre con `0x620d50`
y `0x620d80` y hace las cuentas de `0x621b60`, con el borde de abajo desde la pila
(`0x621b01`..`0x621b0b`), y sin UV. (b) El `SpriteBatch` guarda el estado de GL que tocan las
primitivas, como lo deja el original: el color, la mezcla, la textura prendida y el alpha test.
`SetBlend` (`0x620a00` y `0x620a90`) prende la textura siempre y el alpha test solo con la normal;
`ColorQuad` apaga los dos, como `0x620910(0)`, y quedan apagados hasta el próximo `SetBlend`; la
imagen de la UI nueva deja su color y prende o apaga la textura según su slot. Es Q-C75, nuevo en
07: un quad de `0x621b60` después de uno sin textura sale sin textura. Por eso `DrawCommand` suma
`alphaTest`, y un tramo se corta por textura, mezcla o alpha test. `Begin` vacía el lote y no toca
el estado, que pasa de un frame al siguiente como en el original; el primero empieza como el login
(normal, blanco, textura y alpha test prendidos). (c) El color se recorta a [0, 1] en el
`SpriteBatch` (`SetColor`, el alfa del quad y el color de la imagen) y no en el shader: así se
prueba una vez, sin GL. (d) `UiImage` es la imagen de la UI nueva que pedía la nota de la CL5.R: su
propio espacio con la y desde arriba, `H / fy` como borde de arriba, cada esquina por fx y fy, UV a
medio téxel del tamaño del buffer y el color de `glColor4ub`. `DesignScale(W, 800)` y
`DesignScale(H, 600)` dan los fx y fy de la carga (`0x65eeb0`, con las inversas de `0x6ba884` y
`0x6ba880`). No lleva el ancla de `+0x9c`, el corrimiento de `+0x58` y `+0x5c`, los cuadros de una
animación ni la u de la barra de la carga (`0x647370`, que no va a medio téxel): son de la CL5.4 y
la CL5.5. (e) `GlSpriteRenderer::Create` devuelve un `Core::Expected`, como `GlRenderer::Create`, en
vez de un constructor: el shader puede no compilar. `GlRenderer` se partió en `Upload` (el VBO, una
vez por frame, el viewport, la matriz, el programa y el VAO, que quedan atados) y `DrawRange` (un
tramo, con su textura o sin ninguna y con el alpha test o sin él); `Draw` es los dos. El fragment
shader suma dos banderas, `textured` y `alphaTest`, con `glUniform1i` en la X-macro (47 funciones):
sin textura el fragmento es el color, y con el alpha test, `if (color.a <= 0.25) discard;`.
`GlSpriteRenderer::Draw` prende `GL_BLEND`, pone el `glBlendFunc` de cada tramo y deja la mezcla
prendida, como `0x620a00` y `0x620a90`. Un slot que la tabla no tiene se avisa una sola vez por slot
(el lote se dibuja en cada frame), con el slot en hexadecimal, y su tramo no se dibuja. Los tests de
`PlacesTheLoginBackgroundLikeTheOriginal` son dos, uno por pantalla (`...In1024x768`). Suma tests
que el paso 2 no tenía: `OriginalQuadTest` y `OriginalSkewedQuadTest` (el borde de abajo y la v de
la arena desde la pila), `UiImageTest` y `DesignScaleTest`,
`ScreenScaleTest.MultipliesByTheInverseOfTheSize` y `In640x480ItStillScales`, y en
`SpriteBatchTest`: `AColorQuadIsScaledAndHasNoTexture`,
`AColorQuadLeavesTheTextureAndTheAlphaTestOff`, `ASkewedQuadUsesTheCurrentColor`,
`PlacesTheLoadingImagesLikeTheOriginal` (en 640 × 480 y en 1024 × 768), `AnImageLeavesItsColor`,
`AnImageTurnsTheTextureOnByItsSlot`, `BeginEmptiesTheFrameAndKeepsTheState`, `StartsLikeTheLogin` y
`ADifferentBlendStartsACommandWithTheSameAlphaTest`; con ventana,
`GlSpriteRendererTest.AColorQuadHasNoTextureNorAlphaTest` y `AMissingSlotIsWarnedOnce`. Los mutantes
a mano mueren todos: escalar la x con la inversa de 480 (15 tests, entre ellos
`ScreenScaleTest.ScalesEachAxisApart`), dividir en vez de multiplicar por la inversa
(`MultipliesByTheInverseOfTheSize`), el fan en (0, 1, 2) y (2, 3, 0)
(`TheFanIsTwoTrianglesInOrder`), un quad con alfa que no deja el blanco
(`AQuadWithAlphaLeavesTheColorWhite`) o que usa el color actual
(`AnAlphaAboveZeroIsTheVertexColor`), sin recortar (`TheColorIsClampedToOne`), un tramo que no mira
la mezcla (`ADifferentBlendStartsACommandWithTheSameAlphaTest`) o que nunca junta
(`QuadsOfTheSameTextureAreOneCommand`), el quad sin textura que deja la textura prendida
(`AColorQuadLeavesTheTextureAndTheAlphaTestOff`), la arena corrida medio dv
(`IsAlwaysScaledAndSkewsTheLeftSide`), la imagen sin el medio téxel
(`InPixelsTheYGoesDownFromTheHeight`), la aditiva con `GL_SRC_ALPHA` o la normal con `(GL_ONE,
GL_ZERO)` (`AdditiveAddsAndAlphaBlends`), sin `glEnable(GL_BLEND)`, el alpha test con `<` o apagado
(`TheAlphaTestDropsAQuarter`), siempre con textura (`AColorQuadHasNoTextureNorAlphaTest`), el aviso
en cada frame (`AMissingSlotIsWarnedOnce`) y un slot que falta que se dibuja igual
(`AMissingSlotIsSkippedWithAWarning`). El mutante de la mezcla aditiva vivía con el verde de alfa 1
y el que no miraba la mezcla vivía porque el alpha test cambia con ella: por eso el verde de
`AdditiveAddsAndAlphaBlends` lleva alfa 0,6 y está el test de los dos quads sin textura. En la
revisión vivían cuatro más, y cada uno tiene ahora su test: la imagen que apaga el alpha test
(`SpriteBatchTest.AnImageKeepsTheAlphaTest`), el tramo que no mira el alpha test
(`AnotherAlphaTestStartsACommand`), la imagen sin recortar su color (`TheColorIsClampedToOne`) y el
viewport o la matriz con W y H cambiados, que con el framebuffer cuadrado no se veían
(`GlSpriteRendererTest.TheScreenIsWidthByHeight`, con una pantalla de 64 × 32). En 06 quedó que
la textura la vuelven a prender las siete mezclas del original, no solo la normal y la aditiva, y
en 07, que Q-C64 lo fija `AnImageTurnsTheTextureOnByItsSlot`.

## Sub-ola CL5.3: texto con caché y colores `0x02`

**Concepto:** rasterizar una fuente (contornos, hinting, glifos de un bit, métricas: avance, alto
de celda, ascenso), medir un texto como `GetTextExtentPointA`, y una caché: el original rasteriza y
sube cada texto en cada frame; MuRet lo rasteriza una vez y lo reusa mientras no cambie. Va en dos
PRs: **PR A**, lo que no ve GL (los colores `0x02`, el diseño, el rasterizador con FreeType);
**PR B**, la caché y el dibujo.

**Contrato** (06 § Texto; la tabla de arriba):

| Qué | Original | MuRet |
| --- | --- | --- |
| las fuentes | cuatro `CreateFontA` sin suavizado, alto n − 1 (normal y negrita), 2 (n − 1) (negrita) y 12 (otra cara), con n por el ancho | `FontSet` con las cuatro, `FontSizeFor(width)` con la tabla y el escalón de la pregunta 10; FreeType en mono |
| la cara | los textos 0 y 18 de `Text.bmd` | `[Video] FontFile` y `BoldFontFile`, o la búsqueda de la pregunta 8 |
| medir | `GetTextExtentPointA` | `IGlyphRasterizer::Measure(text)`: el avance sumado y el alto de celda |
| rasterizar | `0x553e50`: `TextOutA` sobre un DIB, que `0x554000` sube a 256 × 32 RGBA en el slot 0, por frame | `GlTextTexture`: el mismo 256 × 32 RGBA, rasterizado una vez por texto, fuente y colores |
| los colores | el del texto (`DAT_006c8e5c`) y el del fondo (`DAT_006c8e64`), globales | `TextStyle{text, background, boxWidth, font}`, un valor por llamada |
| ubicar | la x y la y de `0x554180`, en 640 × 480; el quad, en píxeles | `TextLayout`, con las fórmulas de `0x6350b0` (y la de `0x554000` que confirme la CL5.R) |
| los `0x02` | `0x4667a0`: hasta 9 cambios, el par sale del texto, el color por `0x4665d0` | `ParseInlineColors`: el texto limpio y los tramos, con la misma tabla |
| la caché | ninguna en estas escenas; `0x466000` tiene una (por hash, en un árbol), con slots `10000 + índice` (`0x466560`) | una caché propia, con tope, que se vacía al cambiar el tamaño de la ventana; el asignador de `0x466560` no se reproduce (pregunta 11) |

**Archivos:**
- **PR A:** crear `Client/src/Render/Text/InlineColors.h/.cpp`, `IGlyphRasterizer.h`,
  `FontSet.h/.cpp` (los tamaños y las cuatro fuentes), `TextLayout.h/.cpp`,
  `FreeTypeRasterizer.h/.cpp`, `Client/tests/render/test_inline_colors.cpp`,
  `test_font_set.cpp`, `test_text_layout.cpp`, `test_freetype_rasterizer.cpp` y
  `Client/tests/support/FakeGlyphRasterizer.h`. Modificar `Client/CMakeLists.txt` (FreeType en
  `ClientLib`), `vcpkg.json` (`freetype` sin features por defecto), `App/Config.h/.cpp` y su test
  (`[Video] FontFile`, `BoldFontFile`), el CI (`MURET_CLIENT_FONT_FILE`), `Client/README.md`
  (la config y el crédito de FreeType), el roadmap (D-C2: FreeType, sin SDL3_ttf), PROGRESS y este
  plan.
- **PR B:** crear `Client/src/Render/Text/TextCache.h/.cpp`,
  `Client/src/Render/Gl/GlTextTexture.h/.cpp`, `Client/src/Platform/SystemFonts.h/.cpp` (la búsqueda
  de Windows de la pregunta 8; en Linux da `std::nullopt`),
  `Client/tests/render/test_text_cache.cpp` y `Client/tests/render_gl/test_gl_text_texture.cpp`.
  Modificar `Client/docs/research/06` (la línea de `test_texture_slots`, pregunta 11), `07` (el
  quirk de GDI y FreeType, y el de la letra en un ancho que no está), PROGRESS y este plan.

*Nota de la CL5.2:* el quad del texto es un `SpriteBatch::Quad` con el `OriginalQuad` de 06
(slot 0, du = (w + 0,01) / 256, dv = (h + 0,01) / 32, `scaleSize` en `false` y `scalePosition`
como lo pida el que llama). `GlSpriteRenderer` busca cada slot en la `TextureTable`: la textura de
un texto tiene que estar ahí, en un slot que entre en los 1.845, o el renderer suma de dónde
sacarla. Un slot que no está se avisa una vez y su tramo no se dibuja.

**Interfaces:**

```cpp
namespace Client::Render::Text
{
// 0x4667a0 y 0x4665d0.
inline constexpr char InlineColorMarker = '\x02';
inline constexpr std::size_t MaxInlineColors = 9;
struct ColorRun
{
    std::size_t start;   // en el texto limpio
    std::uint8_t code;   // el byte que sigue al 0x02
    std::uint32_t text;  // el color de 0x4665d0, en el formato del original
    std::uint32_t background;
};
struct InlineText
{
    std::string text;            // sin los pares
    std::vector<ColorRun> runs;  // hasta MaxInlineColors
};
// `sceneText` y `sceneBackground` son los dos colores que cruzan 0xf0 y 0xf1.
InlineText ParseInlineColors(std::string_view text, std::uint32_t sceneText,
                             std::uint32_t sceneBackground);

// Las cuatro de WinMain: la normal, la negrita, la negrita grande, y una normal de 12 con la otra
// cara (el texto 18 de Text.bmd).
enum class FontKind : std::uint8_t { Normal, Bold, BigBold, OtherFace12 };
// n de WinMain: 12, 13, 14 y 15; otro ancho, el escalón de la pregunta 10.
int FontStepFor(int screenWidth);
int FontHeightFor(FontKind kind, int screenWidth); // n − 1, n − 1, 2 (n − 1), 12

struct TextExtent { int width = 0; int height = 0; }; // como el SIZE de GetTextExtentPointA

// Lo que el original deja en globales antes de 0x554180 (DAT_006c8e5c, 64 y 70, y SelectObject).
struct TextStyle
{
    FontKind font = FontKind::Normal;
    std::uint32_t text = 0xffffffff;
    std::uint32_t background = 0;
    int boxWidth = 0x100;
};

class IGlyphRasterizer
{
public:
    virtual ~IGlyphRasterizer() = default;
    virtual TextExtent Measure(FontKind font, std::string_view text) const = 0;
    // En un RGBA de 256 × 32 (el de 0x554000): el texto en `textColor`, el resto en `background`.
    virtual void Rasterize(FontKind font, std::string_view text, std::uint32_t textColor,
                           std::uint32_t background,
                           std::span<std::uint8_t, 256 * 32 * 4> rgba) const = 0;
};

// Las posiciones del login (0x6350b0): un texto que termina en x = 335, uno que empieza ahí, y
// uno en x = 0, los tres apoyados en y = 479. La división es entera y sin signo, como el original.
int RightAlignedAt(int x, TextExtent extent, App::PixelSize screen);    // x − cx · 640 / W
int BottomAlignedAt(int y, TextExtent extent, App::PixelSize screen);   // y − cy · 640 / W

class FreeTypeRasterizer final : public IGlyphRasterizer
{
public:
    // Los altos salen de FontHeightFor con el ancho de `screen`. Error con la ruta si FreeType no
    // abre el archivo.
    static Core::Expected<std::unique_ptr<FreeTypeRasterizer>, std::string> Open(
        const std::filesystem::path& regular, const std::optional<std::filesystem::path>& bold,
        int screenWidth);
};
} // namespace Client::Render::Text
```

Ojo con `BottomAlignedAt`: el original resta `cy · 640 / W` también para la y (no `cy · 480 / H`);
se reproduce y queda en 07 si la CL5.R lo confirma.

- [x] **Paso 1 (PR A):** confirmar con Context7 el uso de FreeType en mono (`FT_Set_Pixel_Sizes`
  contra el alto de celda de `CreateFontA`, que con un alto positivo es la celda y no la letra;
  `FT_LOAD_TARGET_MONO`, `FT_RENDER_MODE_MONO`, el `pitch` de `FT_PIXEL_MODE_MONO`, y qué
  intérprete de TrueType corre en mono) y el puerto de vcpkg sin sus features por defecto.

  Hecho así (2026-11-03): Context7 tiene poco de FreeType, así que manda el header (la 2.13.2 del
  sistema) y las fuentes de la 2.14.3 del puerto (`ttgload.c` y `ttinterp.c`). `FT_Set_Pixel_Sizes`
  pide el em, no la celda: con la celda de `CreateFontA` las letras salen un 15 % más grandes. La
  celda es el ascenso más el descenso; `FreeTypeRasterizer` pide la escala con `FT_Request_Size` y
  `FT_SIZE_REQUEST_TYPE_SCALES`: el alto sobre `usWinAscent + usWinDescent` de la OS/2, los que usa
  GDI (`REAL_DIM` usa los de la hhea, que en DejaVu y Arial son los mismos). `FT_LOAD_TARGET_MONO`
  es el hinting fuerte para un bit e implica `FT_LOAD_MONOCHROME`; `FT_RENDER_MODE_MONO` da
  `FT_PIXEL_MODE_MONO`, un bit por píxel con el de la izquierda en el bit alto; `pitch` son los
  bytes de una fila, positivo de arriba hacia abajo, y negativo con `buffer` en la fila de abajo
  (siempre se suma `pitch` para bajar). El intérprete por defecto es el v40, y en mono
  `tt_loader_init` le apaga el modo compatible: queda todo al programa de la fuente, y `GETINFO`
  dice 40. Se fija el v35, el del rasterizador de Windows 98, con `FT_Property_Set(library,
  "truetype", "interpreter-version", ...)`, que está en todos los builds; con el v35 y el hinting,
  FreeType toma los anchos de la `hdmx`, como GDI. El puerto, en la baseline: `freetype` 2.14.3,
  FTL o GPL-2.0+, con `brotli`, `bzip2`, `png` y `zlib` por defecto, que con `"default-features":
  false` no baja; sin HarfBuzz y sin `error-strings` (los errores van con su número); se usa con
  `find_package(Freetype)` y `Freetype::Freetype`, el mismo target del `FindFreetype` del sistema.
- [x] **Paso 2 (Emanuel, `TODO(human)`; lo resolvió Claude):** antes de los tests, la tabla de
  `ParseInlineColors` para cinco textos: sin `0x02`, uno al principio, dos seguidos, uno al final
  sin byte detrás y diez cambios (qué queda del décimo), con lo que dice `0x4667a0`.

  Hecho así (2026-11-03), del desensamblado de `0x4667a0` y `0x4665d0` (*estático*). Los colores
  van como `0xAABBGGRR`; la escena es la del login, texto `0xffffffff` y fondo `0x80000000`:

  | Texto (bytes) | Texto limpio | Tramos (inicio, código, texto, fondo) | Por qué |
  | --- | --- | --- | --- |
  | `abc` | `abc` | ninguno | sin `0x02` no hay tramos, y con 0 tramos no se reescribe nada (`0x466829`) |
  | `02 f4` `abc` | `abc` | (0, `f4`, `0xff0000ff`, 0) | el inicio es la posición menos dos por par anterior (`0x4667f7`); el código es el byte que sigue (`0x4667fe`), y `0x4665d0` da rojo sin fondo para `0x01` y `0xf4` |
  | `ab` `02 f0` `02 f1` `cd` | `abcd` | (2, `f0`, escena), (2, `f1`, escena cruzada) | el segundo `0x02` está en 4, menos 2: los dos empiezan en 2, y vale el segundo. `0xf0` deja `DAT_006c8e5c` y `DAT_006c8e64`; `0xf1` los cruza (`0x466610`) |
  | `ab` `02` | `ab` | (2, `00`, `0xffffffff`, 0) | el código es el 0 que termina el texto; con 0, `0x4665d0` cae en el `ja` (0 − 1 pasa de `0xf7`) y da blanco sin fondo. El tramo empieza donde termina el texto y no pinta nada |
  | diez pares `02 c` `x`, con c = `01`, `f2` a `f8`, `f0`, `f1` y las letras de la a a la j | `abcdefghij` | nueve, del 0 al 8; el noveno, `f0` | `0x4667e9` corta cuando lleva 18 bytes de pares, y la reescritura (`0x46687b`..`0x466894`) saca todos los pares, también el décimo: la j sigue con el color del noveno |

  Además, `0x4667a0` mide cada tramo con `GetTextExtentPointA` del texto limpio hasta su inicio y
  guarda el ancho menos 1 (0 queda 0, `0x4668c3`..`0x4668cb`): es para la CL8, que pinta el chat por
  columnas. Y reescribe el texto en el buffer del que llama, después de poner en 0 sus 256 bytes
  (`0x46686e`): un bug, Q-C77 en 07 (PR B).
- [x] **Paso 3 (PR A):** los tests:
  - `InlineColorsTest.StripsTheMarkerAndItsCode`, `RecordsWhereEachColorStarts`,
    `TheCodeIsAColorOfTheTable` (1 y `0xf4` rojo, otro blanco), `F0AndF1SwapTheSceneColors`,
    `KeepsAtMostNineColors`, `AMarkerAtTheEndHasNoCode` (lo que diga el paso 2);
  - `FontSetTest.TheStepFollowsTheWidth` (640 → 12, 800 → 13, 1024 → 14, 1280 → 15, y el
    escalón: 1000 → 13, 1920 → 15, 320 → 12), `FontSetTest.EachFontHasItsHeight`;
  - `TextLayoutTest.PlacesTheLoginTextsLikeTheOriginal` (con el rasterizador falso, que mide 6 por
    letra y 13 de alto: los tres textos de `0x6350b0` en 640 × 480 y en 1024 × 768),
    `TextLayoutTest.TheDivisionIsUnsignedAndInteger`;
  - con `MURET_CLIENT_FONT_FILE`: `FreeTypeRasterizerTest.RendersWithoutAntialiasing` (todo píxel
    es del color del texto o del fondo), `MeasuresTheAdvance` (el ancho de "ab" es la suma de los
    dos), `TheCellHeightIsTheFontHeight`, `AMissingFileSaysItsPath`;
  - `ConfigTest.ReadsTheFontFiles`.
- [x] **Paso 4 (PR A):** `InlineColors`, `FontSet`, `TextLayout` y `FreeTypeRasterizer`.

  Hecho así (PR A, 2026-11-03): todo en `Client/src/Render/Text/`, en `ClientLib`, con FreeType solo
  en `FreeTypeRasterizer.cpp` (sus headers no salen de ahí). Lo que se aparta de las interfaces de
  arriba: (a) `IGlyphRasterizer` no colorea: `DrawInk(font, text, firstColumn, InkMask)` deja 0xff
  donde hay letra, como el primer byte del DIB de `TextOutA`, y `PaintTextBox` (`TextBox.h`) hace lo
  de `0x465a00`: 0xff es el color del texto y cualquier otro byte, el del fondo. La regla es del
  original y no de la fuente, así se prueba con el falso, y la caja k de un texto ancho necesita la
  columna 256 k. (b) `TextStyle` no lleva `boxWidth`: `0x467060` no lee `DAT_006c8e70`, que lee solo
  `0x553ae0` (en `0x553bef`); los dos colores van en un `TextColors`. (c) `ParseInlineColors(text,
  InlineColorContext)`: los dos colores de la escena y la bandera de `0xf8` (`DAT_006c7a88`, 1 en la
  `.data`, que escriben ventanas de la UI). (d) `BottomAlignedAt` resta `cy · 480 / H`, como pedía
  Q-C60, y no `cy · 640 / W`. (e) Suma `TextPieces`, lo de `0x467060` con el modo 1 del login y de
  `0x554000`: las filas hasta 32, el corte en el borde derecho de un texto de más de 256 columnas
  (truncado), una caja por cada 256 columnas, la x y la y truncadas (`_ftol`) y corridas por
  `0x554000` (una x negativa a 0, una caja que pasa de W contra el borde), y el quad en píxeles, sin
  escalar (`0x467060` pasa 0 en la bandera), con du = (w + 0,01) / 256 y dv = (filas + 0,01) / 32.
  (f) La tabla del contrato dice `FontSizeFor(width)`; la función es `FontStepFor`, como en las
  interfaces. (g) `FreeTypeRasterizer` suma `InterpreterVersion()` y `PixelsPerEm(font)`, para sus
  tests. La celda sale de la OS/2 (paso 1); la línea de base, del ascenso de Windows sobre la celda,
  redondeado; la negrita sin `BoldFontFile`, de `FT_Outline_EmboldenXY` con un píxel a lo ancho y un
  píxel más de avance por letra (*inferido* de la negrita que simula GDI); los avances de los 256
  bytes se calculan al abrir, y `Measure` los suma sin kerning; sin los bitmaps de la fuente
  (`FT_LOAD_NO_BITMAP`). Cada byte es su carácter de Latin-1 (Q-C67, que quedó decidido con los
  bytes del `Local\Text.bmd` del laboratorio, contados sin imprimirlos). La diferencia con GDI es
  Q-C76, nuevo en 07 (se escribió en el PR A porque es de este código). La config suma `[Video]
  FontFile` y `BoldFontFile`, rutas desde la carpeta del ejecutable; que el archivo exista lo dice
  `Open`, con la ruta. Los tests de FreeType leen `MURET_CLIENT_FONT_FILE` con `Tests::FontFileTest`
  (`support/GoldenDir.h`); los de abrir un archivo que falta o que no es una fuente son
  `FreeTypeRasterizerOpenTest` y corren sin la variable. Suma tests que el paso 3 no tenía:
  `InlineColorsTest.ATextWithoutMarkersStaysAsItIs`, `F8HasABackgroundWhenTheFlagIsOff`,
  `TheByteAfterTheMarkerIsAlwaysTheCode` y `StopsAtTheFirstNul`;
  `FontSetTest.OnlyTheBoldOnesAreBold` y `EveryValidResolutionHasAStep` (las cinco del original de
  `App/Resolution.h`, #72, y anchos de monitor); `TextBoxTest` (el color en RGBA, tinta y fondo, lo
  que no se ve en 0, la columna de la caja y que solo 0xff es letra);
  `TextLayoutTest.TheLoginTextUsesTheHeightForY`, `TheQuadIsInPixelsWithTheUvOfTheBox`,
  `TheRowsAreTheCellUpTo32`, `AWideTextGoesInBoxesOf256`, `AWideTextIsCutAtTheRightEdge`,
  `AShortTextIsMovedInsideTheScreen` y `AnEmptyTextHasNoBoxes`;
  `FreeTypeRasterizerTest.TheEmIsSmallerThanTheCell`, `TheLettersAreInsideTheCell`,
  `DrawsFromTheFirstColumn`, `WithoutABoldFileTheBoldIsOnePixelWiderPerLetter`,
  `WithABoldFileThereIsNoSyntheticBold`, `ReadsEachByteAsLatin1`, `UsesTheInterpreterOfWindows98` y
  `AMissingBoldFileSaysItsPath`; y `ConfigTest.ARelativeFontFileIsFromTheExeFolder`. Los mutantes a
  mano mueren todos: el `0x02` del final sin tramo (`AMarkerAtTheEndHasNoCode`), diez tramos o el
  décimo par que queda en el texto (`KeepsAtMostNineColors`), `0xf0` y `0xf1` cambiados
  (`F0AndF1SwapTheSceneColors` y dos más), el escalón de 1280 con `<=`, el de 800 desde 640 o el 0
  del original fuera de la tabla (`TheStepFollowsTheWidth`), la división en 64 bits con signo o en
  double (`TheDivisionIsUnsignedAndInteger`), la y con W (`TheLoginTextUsesTheHeightForY`), el corte
  redondeado o sin corte (`AWideTextIsCutAtTheRightEdge`), las UV sin el 0,01
  (`TheQuadIsInPixelsWithTheUvOfTheBox` y dos más), FreeType con suavizado
  (`RendersWithoutAntialiasing` y los otros ocho con fuente: `Open` lo rechaza), el intérprete v40
  (`UsesTheInterpreterOfWindows98`), la negrita sin su píxel de avance
  (`WithoutABoldFileTheBoldIsOnePixelWiderPerLetter`), los bytes altos como UTF-8 inválido
  (`ReadsEachByteAsLatin1`) y el tamaño pedido como em con `FT_Set_Pixel_Sizes`, que vivía hasta que
  se sumó `TheEmIsSmallerThanTheCell`.
  Con la revisión (2026-11-03): una letra sin área (el guion bajo de algunas fuentes, que el hinting
  deja en una línea) hacía fallar `Open`, porque `FT_Outline_EmboldenXY` no ensancha un contorno sin
  sentido de giro; ahora esa letra queda como está, con su píxel de avance
  (`FreeTypeRasterizerOpenTest.ALetterWithoutAreaIsBoldToo`, con una fuente mínima armada en
  memoria por `support/TinyTrueTypeFont.h`, que corre sin `MURET_CLIENT_FONT_FILE`). Un mutante que
  leía los bits de cada byte al revés vivía: lo mata `TheLeftPixelOfAByteIsItsHighBit` (una "L"),
  que también mata el paso entre filas sin `pitch`. Y `PaintTextBox` no dibuja un texto que empieza
  con un salto de línea, como `0x467060` (`0x4671e1`;
  `TextBoxTest.ATextThatStartsWithANewLineIsOnlyItsBackground`): ningún texto del `Text.bmd` del
  laboratorio empieza con uno, contado sin imprimirlos.
- [x] **Paso 5 (PR B):** los tests de la caché: `TextCacheTest.TheSameTextIsRasterizedOnce`,
  `ADifferentColorIsAnotherEntry`, `ResizingEmptiesIt`, `KeepsAtMostItsCap` (el más viejo sale);
  con ventana, `GlTextTextureTest.UploadsTheBoxOf256By32` y `TheTextIsAtTheTopLeftOfTheBox`.

  Hecho así (PR B, 2026-11-03): los cuatro de `TextCacheTest`, sin GL, con el rasterizador falso y
  `support/FakeTextureStore.h` (la tabla falsa, que anota cada `Load` y cada `Release`; el plan la
  ponía en la CL5.4). Los de la ventana son `TextCacheGlTest.UploadsTheBoxOf256By32` y
  `TheTextIsAtTheTopLeftOfTheBox`, porque no hay `GlTextTexture` (paso 6): la caché sube a la
  `TextureTable` de verdad y `GlSpriteRenderer` dibuja la caja en el framebuffer de 64 × 64.
  Suma: `TextCacheTest.AWideTextUsesABoxPer256Columns`, `ATextOf256ColumnsIsOneBox`,
  `ABoxPastTheColumn1280HasItsLetters` (Q-C78), `TheBoxIsTheTextOnItsColors` (256 × 32 RGBA,
  `GL_NEAREST` y `GL_CLAMP`), `TheQuadsAreThoseOfTextPieces`, `AnEmptyTextDrawsNothing`,
  `ATextWiderThanTheCacheIsAnError`, `AStoreThatFailsIsAnError` e `ItReleasesItsSlots`;
  `TextureTableTest.TheTextSlotsComeAfterTheOriginal`; `InlineColorsTest.ALongTextKeepsItsTail`
  (Q-C77); `FontFilesTest` (ocho) y `SystemFontsTest` (siete; seis en Windows).
- [x] **Paso 6 (PR B):** `TextCache`, `GlTextTexture` y `SystemFonts`; el 06 y el 07.

  Hecho así (PR B, 2026-11-03): sin `GlTextTexture`. La caja es un `Data::Textures::Texture` de 256
  × 32 RGBA, con `GL_NEAREST` y `GL_CLAMP` como sube `0x63bd00` la `FontInput.tga` del slot 0, que
  `TextCache` sube por `ITextureStore`: la `TextureTable` ya la sube con `GlTexture`, y una clase de
  GL para el texto la duplicaría. Así `TextCache` vive en `ClientLib` y se prueba sin ventana. Los
  slots: `TextureSlot.h` suma 256 del texto después de los 1.845 del original
  (`FirstTextTextureSlot`, el 1.845, y `TextureSlotCount`, 2.101), y la `TextureTable` los tiene
  todos, como pedía la nota de la CL5.2: `GlSpriteRenderer` los encuentra sin cambios. `TextCache`
  guarda por texto, fuente y colores (`TextColors`) el ancho medido y una caja por cada 256
  columnas, en una `std::list` del más reciente al más viejo (un acierto pasa adelante con `splice`)
  y un `std::map` que apunta a sus nodos; el tope son sus slots (256: 8 MiB), y un texto nuevo que
  no entra saca al del fondo y suelta sus slots. Un texto que pide más cajas que todos los slots es
  un error que no saca a nadie; si la tabla no sube, sus slots vuelven. `Draw(batch, x, y, texto,
  estilo)` pone los quads de `TextPieces` con la pantalla del lote y el slot de cada caja; un texto
  vacío no dibuja nada; `ChangeRasterizer` (otro ancho, otras fuentes) la vacía, y al morir suelta
  sus slots. La interfaz se aparta del `ITextDrawer` de la CL5.4 (recibe el lote y devuelve
  `Expected`): el de la CL5.4 la envuelve (su nota). La fuente sin la clave:
  `Platform/SystemFonts.h` con `FindSystemFontFile(cara, negrita)`, que busca en HKLM y después en
  HKCU con las funciones A del registro, como la cara llega a `CreateFontA`, y `FontEntryNamesFace`,
  la regla de los nombres ("<cara> (TrueType)", " Bold" para la negrita, las caras de un .ttc
  separadas por " & ", sin mirar mayúsculas, solo contornos), que se prueba también en Linux. Suma
  `App/FontFiles.h`: `ChooseFontFiles(config, cara, búsqueda)`, la decisión de la pregunta 8 sin
  Windows: la config manda; sin `FontFile`, la búsqueda con la cara, y la negrita del sistema si
  falta `BoldFontFile` (como `CreateFontA` con peso 700 elige la negrita de la cara, *inferido*);
  sin búsqueda o sin la cara, un error que nombra la clave y no la cara. Nadie la llama todavía: la
  cara es el texto 0 de `Text.bmd`, que `main.cpp` carga en la CL5.4. El código de Windows no se
  compiló acá (sin SDK): se revisó con un `windows.h` de prueba y lo compila el CI. En 06: el DIB y
  la caja en § Texto, la fila del texto en "Qué implica para MuRet" y la línea de
  `test_texture_slots`, con la razón de la pregunta 11. En 07: Q-C77 (`0x4667a0` pone 256 ceros en
  el buffer del que llama y reescribe ahí el texto, leyendo de más de su copia de 256 bytes) y Q-C78
  (el DIB de 1.280 columnas, que `0x465a00` lee sin mirar el ancho), los dos *estáticos* y
  corregidos; Q-C76 llegó con el PR A. Los mutantes a mano mueren todos: la clave sin los colores,
  sin el fondo o sin la fuente (`ADifferentColorIsAnotherEntry`), el acierto sin pasar adelante
  (`KeepsAtMostItsCap`), sacar al más nuevo (`KeepsAtMostItsCap` e `ItReleasesItsSlots`), sin sacar
  a nadie (`KeepsAtMostItsCap` y `ADifferentColorIsAnotherEntry`, que abortan), el tope contra los
  slots libres (`KeepsAtMostItsCap` y `ADifferentColorIsAnotherEntry`), las cajas redondeadas para
  abajo o truncadas (`ATextOf256ColumnsIsOneBox`, y once con la truncada), sin soltar los slots si
  la tabla falla (`AStoreThatFailsIsAnError`) o al morir (`ItReleasesItsSlots`), los slots del texto
  fuera de la `TextureTable` (los dos `TextCacheGlTest` y `TheTextSlotsComeAfterTheOriginal`), la
  cara en el error (`AFaceThatIsNotInTheSystemIsAnErrorWithoutTheFace`), sin buscar la negrita
  (`WithoutTheKeyTheSystemListIsSearched`), un prefijo como la cara (`APrefixIsAnotherFace` y dos
  más) y las .fon como fuentes (`OnlyOutlineFontsCount`).

  Con la revisión (2026-11-03): un texto nuevo podía sacar a uno que ya estaba en el lote del
  frame, y como `GlSpriteRenderer` busca cada slot recién al dibujar, el quad del viejo salía con
  la caja del nuevo. Ahora la caché no saca una entrada que tiene un slot en el lote: si la del
  fondo lo tiene, el texto nuevo es un error y no se suelta nada
  (`TextCacheTest.ATextOfThisBatchIsNotEvicted`, que mata al mutante sin la regla), y
  `KeepsAtMostItsCap` y `ADifferentColorIsAnotherEntry` empiezan otro lote antes de sacar.
  `SystemFonts.cpp` compara con `Data::EqualsIgnoringAsciiCase` en vez de una copia propia.
- [x] **Paso 7:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug
  ctest --preset linux-x64-debug -R "InlineColors|FontSet|TextLayout|FreeType|TextCache|Config"
  ```

  Hecho así (2026-11-03): en Linux, con GCC, Debug y sin precompiled headers, como el job del CI.
  El PR A: 1.120 tests, 0 fallan, con `MURET_CLIENT_FONT_FILE` y sin ella (los 12 de
  `FreeTypeRasterizerTest` se saltean). El PR B: 1.153, 0 fallan, también con y sin la variable.
  Los de GL con `MURET_CLIENT_REQUIRE_GL=1 xvfb-run -a ctest -L '^gl$'`: 46 en el A y 49 en el B,
  0 fallan; `Main --frames 60` en Xvfb sale con 0 en los dos.

## Sub-ola CL5.4: `SceneManager`, la escena de carga y la `LoginScene`

**Concepto:** una máquina de escenas con transiciones explícitas en vez de un entero global, una
escena como un objeto con entrar, paso, dibujo y salir, y los recursos de la escena (texturas,
ventanas) que viven lo que ella. Y el mundo como dato: la escena dice qué mapa usa, y una función
da su carpeta para cualquier mapa.

**Contrato** (02 § El frame; la tabla de arriba; lo que diga la CL5.R):

| Qué | Original | MuRet |
| --- | --- | --- |
| las escenas | `DAT_006d5dc8`: 1, 2, 3, 4, 5 | `SceneId` con esos números (para el log y las trazas) y una clase por escena |
| las transiciones | 1 → 2, 2 → 4, 4 → 3, 3 → 5, y las de `0x4c2710`: de 5 o 4 a 4, y a 2 (CL5.R, 02) | `SceneManager::Request(SceneId)`: una que no está en la tabla es un error en el log y no cambia nada |
| la carga | `0x622e40`: sus seis texturas, su UI, los datos, y a la 2 | `LoadingScene`: su juego de texturas (la tabla de `0x622e40`), lo que carga MuRet (`Text.bmd`, el juego del login) con la barra, y a `Login` |
| entrar al login | `0x6349a0`, cada vez que se entra: mapa 55, texturas, ventanas, CS, objeto, personaje, música | `LoginScene::Enter`, también cada vez: su mundo de la tabla de escenas (`SceneWorld{55}`), su juego de texturas en el `ITextureStore`, sus ventanas (CL5.5) y `ServerSession::Start` (CL4); el logo y el personaje, CL6; la música, CL10 |
| las texturas de la escena | `0x61c7c0` sube las 18 del login; la carga suelta las suyas con `0x63c150` | `Render::ITextureStore` (CL5.1): la escena sube su juego en `Enter` y lo suelta en `Leave`; lo implementa `Gl::TextureTable` |
| la red por frame | `0x4e1d50` en cada vuelta del bucle (CL4.1) | `SceneManager::Poll`, que corre `pollNetwork` (la `ServerSession` de `main.cpp`) también minimizada |
| la carpeta del mundo | `0x61a560`: `World` + mapa + 1, con los tres rangos de evento y el 32 que usa la del 9 | `Data::WorldFolderNumber(map)` y `WorldFolder(map)`, para los mapas de `OriginalMaps` (del 0 al 36, el 54 y el 55) |
| el dibujo | `0x6350b0`, en el orden de la tabla | `LoginScene::Render`: los fondos, el pase 3D (`IWorldView`, vacío hasta la CL7), la arena, los textos, el fundido y la UI, en ese orden |
| la versión | el texto 456 con la versión del archivo o de `config.ini` (02) | el texto 456 con la versión de MuRet (`MURET_VERSION` del CMake) |
| a la selección | el login aceptado (`0x14`): escena 4 y el `F3 00` | `SessionState::LoggedIn`: `Request(CharacterSelect)`; la `CharacterSelectScene` de la CL5 no dibuja nada y no manda el `F3 00` (CL6.3, P-C06) |

**Archivos:** crear `Client/src/Data/WorldFolder.h/.cpp`, `Client/src/Scenes/SceneId.h`, `IScene.h`,
`IFrameTarget.h`, `SceneContext.h` (con `GameData`), `SceneManager.h/.cpp`, `SceneWorlds.h` (la
tabla escena → mapa), `TextureSets.h/.cpp` (los juegos de la carga y del login, como datos),
`IWorldView.h`, `LoadingScene.h/.cpp`, `LoginScene.h/.cpp`, `CharacterSelectScene.h/.cpp` (vacía),
`Client/tests/data/test_world_folder.cpp`, `test_golden_world_folder.cpp`,
`Client/tests/scenes/test_scene_manager.cpp`, `test_loading_scene.cpp`, `test_login_scene.cpp`,
`test_golden_login_scene.cpp` y `Client/tests/support/RecordingTextDrawer.h`,
`RecordingFrameTarget.h` y `FakeTextureStore.h`, y `Client/src/Render/Gl/GlFrameTarget.h/.cpp` (el
`IFrameTarget` de `main.cpp`). Modificar `Client/src/main.cpp` (con `--data`, el `SceneManager` en
vez de la `TestScene`), `Client/CMakeLists.txt` (`MURET_VERSION`), `Client/README.md`, PROGRESS y
este plan.
*Nota de la CL5.1:* `Scenes/TextureSets.h` (solo el `.h`: las dos tablas son `constexpr`) ya llegó
con la CL5.1, con `test_texture_sets.cpp` y el golden (`test_golden_texture_sets.cpp`), y también
`Render/ITextureStore.h`, `Render/TextureSlot.h` y `Core::Expected<void, E>`. Falta el
`FakeTextureStore` de `support/`.

*Nota de la CL5.2:* el `SpriteBatch` no tiene `Flush`: el `GlFrameTarget` dibuja lo juntado
con `GlSpriteRenderer::Draw(batch, textures, log)` y vuelve a `Begin`, que vacía el lote y no toca
el estado (el color, la mezcla, la textura y el alpha test pasan de un tramo al otro y de un frame
al siguiente, Q-C75), o suma el `Flush` ahí. La arena es `SetBlend(Additive)`, `SetColor` y
`SkewedQuad`. La carga dibuja con `SpriteBatch::Image(UiImage)`, con `scaleX = DesignScale(W,
800)`, `scaleY = DesignScale(H, 600)` y, para las UV, el tamaño del buffer de cada textura
(`Texture::textureWidth` y `textureHeight`). La u de la barra (`0x647370`) no va a medio téxel y
el `UiImage` no la tiene: se suma con su test. `GlSpriteRenderer` deja `GL_BLEND` prendida. No
apaga `GL_DEPTH_TEST` ni `GL_CULL_FACE`, que el original apaga al entrar al 2D (`0x6219b0` llama a
`0x620870`) y con la mezcla normal y la aditiva (`0x6208f0`): hoy nadie los prende, y cuando el
pase 3D lo haga (la CL7), el 2D que sigue los apaga, con un test.

*Nota de la CL5.3:* el `ITextDrawer` de la escena envuelve un `Render::Text::TextCache`, que
recibe el lote (`Draw(batch, x, y, texto, estilo)`) y devuelve un `Expected`: el error (la tabla no
subió, el texto no entra en los slots, o todos están en textos del lote, que no salen hasta el
próximo `Begin`) va al log una vez y el texto no se dibuja. Un lote por frame. La caché vive
lo que su `ITextureStore`, y sus slots (del 1.845 al 2.100) no chocan con los de las escenas.
`FakeTextureStore.h` ya está en `support/`. Antes de los textos, `0x6350b0` deja
`glColor3f(0,8; 0,7; 0,6)` después de la arena y la mezcla que ella dejó: los quads del texto, con
alfa 0, salen con ese color, y la escena lo tiene que poner como el original (06 § El login, el
dibujo). `main.cpp` arma el `FreeTypeRasterizer` con `App::ChooseFontFiles(config, cara,
búsqueda)`: la cara es el texto 0 de `Text.bmd`, y la búsqueda, `Platform::FindSystemFontFile` en
Windows y ninguna en Linux. En el `Text.bmd` del laboratorio los textos 0 y 18 están vacíos: con
esos datos, también en Windows hace falta `[Video] FontFile`. Una ruta que encontró la búsqueda no
va al log, porque su nombre puede ser el de la cara: si `Open` falla con ella, su error (que nombra
el archivo) se cambia por uno que nombra la clave. Con otra densidad de pantalla (el
área de dibujo cambia aunque la ventana no), otro `FreeTypeRasterizer` y `ChangeRasterizer`.

**Interfaces:**

```cpp
namespace Client::Data
{
// Los mapas que llegan a 0x61a560: del 0 al 36 (los del juego; los handlers mandan un byte, de 0 a
// 255, sin mirar el rango: CL5.R, Q-C58), el 54 de la selección (0x633f20) y el 55 del login
// (0x6349a0). Los tests de todos los mapas van por esta lista.
inline constexpr std::array<std::uint8_t, 39> OriginalMaps = {0,  1,  2, /* … */ 35, 36, 54, 55};
// 0x61a560: el número de la carpeta World<N> y de EncTerrain<N> para el mapa `map`, con los tres
// rangos de evento y el 32, que usa la carpeta del 9 (World10, 0x61a56c..0x61a57d). La bandera
// con que 0x616ec0 fuerza el 7 no va: nadie la escribe (CL5.R). Devuelve más de un byte, porque
// el 255 da 256.
std::uint16_t WorldFolderNumber(std::uint8_t map);
std::string WorldFolder(std::uint8_t map); // "World56" para el 55
} // namespace Client::Data

namespace Client::Scenes
{
enum class SceneId : std::uint8_t
{
    Loading = 1,
    Login = 2,
    WorldLoading = 3,
    CharacterSelect = 4,
    Game = 5,
};

// El mundo de una escena: un dato de la tabla de SceneWorlds.h (el login, el 55 de 0x6349a0), que
// la escena pasa al pase 3D. La CL7 dibuja cualquier mapa con él.
struct SceneWorld
{
    std::uint8_t map;
};

class IWorldView
{
public:
    virtual ~IWorldView() = default;
    // El pase 3D de la escena, en el viewport de 640 × 480 que dice el original (para el login,
    // 0, 25, 640, 430). En la CL5, uno que no dibuja nada.
    virtual void Draw(SceneWorld world, float alpha) = 0;
};

// Un texto en (x, y) de 640 × 480, como 0x554180. Lo implementa el TextCache con el rasterizador
// (CL5.3); los tests usan uno que graba.
class ITextDrawer
{
public:
    virtual ~ITextDrawer() = default;
    virtual Render::Text::TextExtent Measure(Render::Text::FontKind font,
                                             std::string_view text) const = 0;
    virtual void Draw(int x, int y, std::string_view text,
                      const Render::Text::TextStyle& style) = 0;
};

// Lo que una escena necesita para dibujar sin ver GL.
struct SceneCanvas
{
    Render::SpriteBatch& sprites;
    ITextDrawer& text;
    IWorldView& world;
    App::PixelSize screen;
    Core::Duration frameClock;   // el reloj del frame, para la arena
};

// El frame del lado de GL: main.cpp con el Gl::GlFrameTarget (el GlSpriteRenderer, el TextCache
// y el swap de la ventana); los tests, el RecordingFrameTarget de support/, que anota.
class IFrameTarget
{
public:
    virtual ~IFrameTarget() = default;
    // Limpia y da el lienzo del frame, con el SpriteBatch vacío; vale hasta Present.
    virtual SceneCanvas& BeginFrame(Core::Duration frameClock) = 0;
    // Manda lo que juntó el SpriteBatch y presenta: el swap va adentro de Render (ILoopClient.h).
    virtual void Present() = 0;
};

// Lo que comparten las escenas: de dónde leen, adónde suben sus texturas (Render::ITextureStore,
// CL5.1) y el log. Lo arma main.cpp; los tests, con un FakeTextureStore.
struct SceneContext
{
    std::filesystem::path dataRoot;
    Render::ITextureStore& textures;
    Core::Log& log;
};

// Lo que deja la carga para las escenas que siguen; vive en main.cpp, como el SceneManager.
struct GameData
{
    std::optional<Data::Text::TextTable> text; // Text.bmd (CL1)
};

class IScene
{
public:
    virtual ~IScene() = default;
    virtual Core::Expected<void, std::string> Enter() = 0; // un error (una textura que falta) corta
    virtual void Step(const Input::InputState& input) = 0;
    virtual void Render(SceneCanvas& canvas, float alpha) = 0;
    virtual void Leave() = 0;
    // A qué escena quiere ir; vacío si sigue.
    [[nodiscard]] virtual std::optional<SceneId> Next() const = 0;
};

class LoadingScene final : public IScene
{
public:
    // Enter sube LoadingTextureSet (TextureSets.h) con context.textures; Step lee Text.bmd a
    // `data` y pide Login; Leave suelta los seis slots, como 0x63c150.
    LoadingScene(SceneContext context, GameData& data);
};

class LoginScene final : public IScene
{
public:
    // `session` es nullptr sin host (Main sin connect): la escena dibuja y no conecta. `version`
    // es MURET_VERSION. Enter: LoadTextureSet con LoginTextureSet, textures.Load (la primera que
    // falta corta con su ruta) y ServerSession::Start. Leave: textures.Release de cada slot del
    // juego. Next: CharacterSelect con la sesión en LoggedIn.
    LoginScene(SceneContext context, const GameData& data, App::ServerSession* session,
               std::string_view version);
};

// Arma la escena de un SceneId: main.cpp con las de verdad, los tests con dobles.
using SceneFactory = std::function<std::unique_ptr<IScene>(SceneId)>;

class SceneManager final : public App::ILoopClient
{
public:
    // `pollNetwork` corre en cada Poll: main.cpp pasa ServerSession::Poll (y, con --auto-login,
    // AutoLogin::Poll, CL5.5); sin host, nada. La sesión es de main.cpp y no de una escena, porque
    // sigue viva del login a la selección; la LoginScene la recibe por su constructor.
    SceneManager(SceneFactory factory, IFrameTarget& frame, Core::IClock& clock,
                 std::function<void()> pollNetwork, Core::Log& log);
    Core::Expected<void, std::string> Start(SceneId first); // Loading
    void Poll() override;                                  // la red, también minimizada
    void Step(const Input::InputState& input) override;    // y, si la escena pide otra, cambia
    void Render(float alpha) override; // BeginFrame con el reloj, la escena y Present
    [[nodiscard]] SceneId Current() const;
};
} // namespace Client::Scenes
```

- [x] **Paso 1 (Emanuel, `TODO(human)`; lo resolvió Claude):** antes de los tests, la tabla de
  transiciones con lo que dé el paso 1 de la CL5.R, y la de `WorldFolderNumber` para los mapas 0 a
  36, el 54 y el 55.

  Hecho así (PR A, 2026-11-03), otra vez en el desensamblado de `c32bbd5f` y no solo en el plan
  (*estático*). Las transiciones son los ocho `mov` a `DAT_006d5dc8`, que siguen siendo los únicos
  (`0x622f93` 1 → 2, `0x634dce` 2 → 4, `0x633f05`, `0x634470` y `0x634638` 4 → 3, `0x635a91` 3 →
  5, `0x4c363f` a 4 y `0x4c2f1a` a 2), y la escena arranca en 1 (la `.data`, `0x2d5dc8` del
  archivo). Las dos de `0x4c2710` no miran la escena de ahora, salvo la 5 para limpiar
  (`0x4c275c`), ni las mira el dispatcher (`0x4e2b10`, la tabla del `F1` en `0x4e5ea8` y
  `0x4e5e88`, el 2 en `0x4e2d80`): la tabla deja las de 02, desde la 4 y la 5, y lo demás es Q-C79.
  Quedó en `Scenes/SceneTransitions.h` (`OriginalTransitions`, `IsOriginalTransition` y
  `FirstScene`), que el plan no listaba. `WorldFolderNumber`: el 32 y `DAT_006c9dd0` en 32 pasan a 9
  (`0x61a56c`..`0x61a57d`); después, con `esi` en el mapa + 1, los `cmp` con signo contra 11 y 17,
  24 y 29 o el 36, y 18 y 23 (`0x61a5e4`..`0x61a61d`), y el mismo `esi` va a `"World%d"`
  (`0x6c7330`) y a `"Data\%s\EncTerrain%d.map"` (`0x6d12e8`). Los cinco que la llaman: `0x4c41d4`,
  `0x4c4eab` y `0x4c7817` con un byte del paquete (`xor eax, eax` y `mov al`, sin mirar el rango),
  `0x633f4e` con el 54 (`0x633f44`) y `0x6349bc` con el 55 (`0x6349b2`). La tabla de 39 filas está
  en `WorldFolderTest.MapsEachMapToItsFolder`.
- [ ] **Paso 2:** los tests:
  - `WorldFolderTest.MapsEachMapToItsFolder` (0 → 1, 10 → 11, 11 a 17 → 12, 18 a 23 → 19, 24 a 29
    y 36 → 25, 30 → 31, 35 → 36, 55 → 56), `WorldFolderTest.TheMapThirtyTwoUsesTheFolderOfNine`
    (32 → 10, como el 9: `0x61a56c`..`0x61a57d`), `WorldFolderTest.TheSelectionMapIsWorld55`
    (54 → 55, `0x633f44`), `WorldFolderTest.NamesTheFolder` (`"World56"`) y
    `WorldFolderTest.MapsBeyondThirtySixAreMapPlusOne` (37 → 38, 53 → 54, 56 → 57 y 255 → 256:
    ningún handler mira el rango, Q-C58);
  - `GoldenWorldFolder.EveryLabWorldIsTheFolderOfAMap` (para cada uno de los 20 mundos del
    laboratorio, `LabWorlds` de `TerrainFiles.h`, hay un mapa de `OriginalMaps` que da esa
    carpeta: `World10` por el 9 y el 32, `World55` por el 54) y
    `GoldenWorldFolder.EveryMapFindsItsTerrain` (para cada mapa de `OriginalMaps`,
    `ResolveDataPath` encuentra `EncTerrain<N>.map`; el único que el laboratorio no trae, el 35 →
    `World36`, va en el test como faltante conocido, y el test falla si aparece o si falta otro),
    el patrón de los `LoadsEveryWorldOfTheLab` de la CL1;
  - `SceneManagerTest.StartsInLoading`, `LoadingGoesToLogin`,
    `LoginGoesToCharacterSelectWhenLoggedIn`, `AnInvalidTransitionIsRejected` (de `Loading` a
    `Game`: aviso y sigue en `Loading`), `AFailedEnterStopsWithItsError`,
    `LeavingReleasesTheTextures` (con el `FakeTextureStore`: de `Loading` a `Login`, los seis slots
    de la carga se sueltan antes de que suban los 18 del login), `PollsTheNetworkOncePerFrame`
    (`Poll` corre `pollNetwork` una vez, también minimizada y antes de `Start`) y
    `RendersBetweenBeginAndPresent` (con el `RecordingFrameTarget`: `BeginFrame` con el reloj, el
    `Render` de la escena y `Present`, una vez por frame);
  - `LoginSceneTest.DrawsInTheOrderOfTheOriginal` (con un `IWorldView` y un `ITextDrawer` que
    graban: el `0x15`, el `0x16`, el fin del 2D (un `Flush` del `SpriteBatch`, donde el original
    hace el pase (2b) de la tabla), el mundo, la arena `0x5c2` y `0x5c3` aditivas, los tres textos y
    la UI, en ese orden), `LoginSceneTest.TheWorldIsTheOneOfItsTable` (el pase 3D recibe el mapa 55,
    y `WorldFolder` da `World56`), `LoginSceneTest.TheSandRunsWithTheFrameClock` (dos frames con el
    reloj corrido dan otra u, y el mismo reloj, la misma), `LoginSceneTest.StartsTheSessionOnEnter`,
    `LoginSceneTest.WithoutASessionItDrawsAndDoesNotConnect`,
    `LoginSceneTest.AMissingTextureFailsWithItsName`, `LoginSceneTest.ShowsTheVersionOfMuRet`;
  - golden: `GoldenLoginScene.EntersWithTheLabData` (con el `Data\` del laboratorio, `Enter` sube
    las 18 texturas a sus slots en un `FakeTextureStore` y lee `Text.bmd`; ningún texto va a la
    salida del test).

  Hecho así (PR A, 2026-11-03): los de `WorldFolderTest` como dice arriba, más
  `OriginalMapsAreTheGameMapsAndTheTwoScenes` y `TheEventRangesEndWhereTheOriginalCompares` (los
  bordes de cada rango); los dos `GoldenWorldFolder`, que también buscan la carpeta de cada mundo
  del laboratorio. `SceneTransitionsTest` (cinco: los números, la primera escena, la tabla y las 25
  de cinco escenas, de las que valen ocho). En `SceneManagerTest`, los ocho de arriba con escenas de
  prueba (en `LoginGoesToCharacterSelectWhenLoggedIn` la de prueba pide la selección cuando el test
  dice; el de la `LoginScene` con la sesión es del B), y suma `ARequestFollowsTheSameTable`,
  `AFailedFirstEnterFailsTheStart`, `AFactoryWithoutTheSceneFails`,
  `TheSceneLeavesWhenTheManagerCloses` y `ReentersTheSelectionFresh` (4 → 4).
  `PollsTheNetworkOncePerFrame` corre con el `GameLoop` de verdad, minimizado y no. Los mutantes a
  mano, cada uno con su test que falla: el 32 sin su `if` (`MapsEachMapToItsFolder`,
  `TheMapThirtyTwoUsesTheFolderOfNine`), un rango que termina en 16 o sin el 36
  (`MapsEachMapToItsFolder`, `TheEventRangesEndWhereTheOriginalCompares`), el número en un byte,
  que da 0 para el 255 (`MapsBeyondThirtySixAreMapPlusOne`), una transición inválida en la tabla
  o aceptada por el `SceneManager` (`RefusesEveryOther`, `AnInvalidTransitionIsRejected`), el
  aviso en cada paso (`AnInvalidTransitionIsRejected`), el `Enter` de la nueva antes del `Leave`
  de la vieja (`LeavingReleasesTheTextures` y cuatro más), `pollNetwork` dos veces o solo con
  escena (`PollsTheNetworkOncePerFrame`), `Present` antes del `Render` de la escena
  (`RendersBetweenBeginAndPresent`), y sin el `Leave` al fallar o al cerrar
  (`AFailedEnterStopsWithItsError`, `TheSceneLeavesWhenTheManagerCloses`).
- [ ] **Paso 3:** `WorldFolder`, las escenas, el `SceneManager`, el `GlFrameTarget` y `main.cpp`. La
  `TestScene` queda para `Main` sin `--data` (CI).

  Hecho así (PR A, 2026-11-03): `Data/WorldFolder.h` con `OriginalMaps` y los rangos como una tabla
  (`FolderRange`) en el orden en que compara el original; `Scenes/SceneId.h` (con `SceneName`, para
  el log: "Login (2)"), `SceneTransitions.h`, `IScene.h`, `SceneCanvas.h` (aparte, y en el A solo
  con `sprites`, `screen` y `frameClock`: el texto y el mundo los suma el B, que los usa),
  `IFrameTarget.h`, `SceneContext.h` (con `GameData`) y `SceneManager.h/.cpp`. Se aparta de la
  interfaz: `Current()` da un `std::optional<SceneId>`, vacío antes de `Start` o después de un
  `Enter` que falló; `Failure()` da ese error, para que `main.cpp` cierre; `Request` es público
  (lo va a llamar la red con el `F1 02`, CL6 y CL9) y no se llama desde una escena, que pide con
  `Next`; un pedido rechazado se avisa una vez mientras se repita, y no en cada paso. Cambiar es
  `Leave` de la que sale, la fábrica y `Enter`; una fábrica sin la escena (`nullptr`) es un error
  como un `Enter` que falla, y la escena que falló hace su `Leave`, que suelta lo que alcanzó a
  subir; al destruirse, el `SceneManager` hace el `Leave` de la de ahora (así las texturas no quedan
  para el que cierra, y la `TextureTable` tiene que vivir más que él). El 4 → 4 arma una escena
  nueva, como el original con `0x7d3a13f` en 0 (`0x4c38eb`). Los dobles:
  `support/RecordingFrameTarget.h` (un `SpriteBatch` de verdad, y cada frame con su reloj, si se
  presentó y lo que tenía el lote) y `support/ScenePrinters.h`; el `FakeTextureStore` ya estaba
  (CL5.3). La línea entre los PRs se corrió un poco: Q-C79 (07 y la fila del 02) va en el A, con la
  tabla que lo explica.
- [ ] **Paso 4:** a mano, en Linux con el `Data\` del laboratorio, `Main --data <Data>`: la carga,
      el login con sus fondos, la arena y los textos (la UI llega en la CL5.5). Una captura, fuera
      de Git.
- [ ] **Paso 5:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug
  ctest --preset linux-x64-debug -R "WorldFolder|SceneManager|LoadingScene|LoginScene"
  ```

## Sub-ola CL5.5: UI de login conectada a CL4: lista de servidores, cuenta y clave

**Concepto:** UI retenida mínima (una ventana con widgets que se dibujan y reciben el input), el
foco del teclado, el texto tecleado contra las teclas, y una máquina de estados de red que espera
al jugador. Va en dos PRs: **PR A**, el texto tecleado, los widgets y la lista (sin red);
**PR B**, la `ServerSession` que espera a la UI y el login entero.

**Contrato** (04 § El flujo de conexión; P-C04; la CL5.R, pasos 6 y 7):

| Qué | Original | MuRet |
| --- | --- | --- |
| la lista | `F4 06` → `0x4c1860`, agrupada por `código / 20` con una tabla de 25 (P-C04); 16 botones de servidor, aunque un grupo puede tener 20 (Q-C63) | `UI::ServerListModel`: los grupos y su orden, con lo que diga la CL5.R; la carga de cada servidor; `ServerListWindow` con 20 botones de servidor (decidido en la CL5.R) |
| elegir | un clic en el servidor → `F4 03` con su código | `ServerSession::ChooseServer(code)`, en `ChoosingServer` |
| cuándo sale la cuenta | con el GS conectado: la abre el `F1 00` (`0x4c1e20`, CL5.R) | `SessionState::WaitingCredentials`: la ventana de la cuenta se muestra |
| los campos | dos `EDIT` de Windows con `EM_LIMITTEXT` 10, la clave con `ES_PASSWORD` (CL5.R), y Tab y Enter los toma el subclass (`0x467b10`, 06); la cuenta y la clave de hasta 10 bytes en el paquete (Q-C43) | `UI::TextField` con tope de 10 bytes, ASCII imprimible, la clave con asteriscos; Tab cambia de campo (de MuRet) |
| mandar | Enter u OK, en `0x6418b0` → el emisor `0x641bf0`; con un campo vacío, el aviso `0x64` o `0x65` (CL5.R) | `ServerSession::SubmitCredentials(Credentials)`; el campo de la clave se vacía |
| la UI por frame | las ventanas leen Enter y Esc y el mouse por frame (`0x640940`) | la UI se atiende en el paso, con los flancos de la CL3.3 (los ve el primer paso del frame): a 25 pasos por segundo, la demora es parecida a la de 21,33 frames; la diferencia va a 07 |
| el mouse | píxeles de la ventana, acotados (Q-C47) | píxeles del framebuffer, acotados (pregunta 12) |
| los avisos | el `0x70` con el texto 402 (no conectó, y después del texto 3 del `FD_CLOSE`) y el del resultado del `F1 01` | `UI::MessageBox` con el texto de `Text.bmd` por índice; los índices, los de 06 § Los avisos (CL5.R) |
| sin UI | (no hay) | `App::AutoLogin` con `--auto-login`: el primero de la lista y las variables de la pregunta 7 de la CL4 |

**Archivos:**
- **PR A:** crear `Client/src/UI/Widget.h`, `Button.h/.cpp`, `TextField.h/.cpp`,
  `ServerListModel.h/.cpp`, `ServerListWindow.h/.cpp`, `LoginWindow.h/.cpp`, `MessageBox.h/.cpp`,
  `Client/tests/ui/CMakeLists.txt`, `test_text_field.cpp`, `test_button.cpp`,
  `test_server_list_model.cpp`, `test_server_list_window.cpp`, `test_login_window.cpp` y
  `Client/tests/support/UiPrinters.h` (el `PrintTo` del campo de la clave). Modificar
  `Client/src/Input/InputEvent.h`, `InputState.h/.cpp` (el texto tecleado),
  `Client/src/Platform/InputEvents.cpp` (`SDL_EVENT_TEXT_INPUT` y el mouse en píxeles del
  framebuffer), `Platform/Window.h/.cpp` (`StartTextInput`, `StopTextInput`), sus tests,
  `Client/tests/CMakeLists.txt` (`add_subdirectory(ui)`), el roadmap (D-C11, con la pregunta 10),
  PROGRESS y este plan.
- **PR B:** crear `Client/src/App/AutoLogin.h/.cpp`, `Client/tests/app/test_auto_login.cpp`,
  `Client/tests/scenes/test_login_scene_session.cpp`. Modificar
  `Client/src/App/ServerSession.h/.cpp` y su test, `App/CommandLine.h/.cpp` y su test
  (`--auto-login`), `Scenes/LoginScene.h/.cpp`, `main.cpp`, `Client/README.md` (la prueba a mano),
  el 07 (la UI en el paso), PROGRESS, el roadmap (la fila de la CL5, si cambia) y este plan.

*Nota de la CL5.2:* las imágenes de las ventanas son `UiImage` con `scaleX = scaleY = 1`, en
píxeles y con la y desde H. El ancla de `+0x9c`, el corrimiento de `+0x58` y `+0x5c` y los cuadros
de una animación no están: si una ventana los usa, se suman a `UiImageCorners` con su test.

*Nota de la CL5.3:* los textos de las ventanas van por el mismo `ITextDrawer` de la CL5.4 (la
`TextCache`), con `FontKind` y `TextColors` por llamada. Un texto que cambia en cada frame (lo que
se teclea, un contador) es una entrada nueva cada vez: el tope saca las viejas, pero un campo que
cambia mucho puede vaciar la caché; si se nota, el campo dibuja sin caché o la caché suma una
entrada por campo. `InlineColors` es para el chat (CL8); el login no lo usa.

**Interfaces:**

```cpp
namespace Client::Input
{
// InputEvent suma Kind::Text: lo que tecleó el jugador, en UTF-8, del SDL_EVENT_TEXT_INPUT.
// InputState::Text() lo junta desde el último ConsumeEdges, como la rueda.
} // namespace Client::Input

namespace Client::UI
{
struct Rect { int x, y, width, height; }; // píxeles del framebuffer

class TextField
{
public:
    TextField(std::size_t maxBytes, bool masked); // la cuenta y la clave: 10
    void Type(std::string_view utf8);              // solo ASCII imprimible, hasta maxBytes
    void Backspace();
    void Clear();
    [[nodiscard]] std::string_view Value() const&;
    [[nodiscard]] std::string Shown() const;       // con asteriscos si es la clave
};

struct ServerGroup
{
    std::uint16_t group;                       // código / 20
    std::vector<Protocol::Connection::ServerEntry> servers;
};
std::vector<ServerGroup> GroupServers(std::span<const Protocol::Connection::ServerEntry> list);

// 21 botones de grupo y 20 de servidor, uno por número (el original tiene 16: Q-C63).
class ServerListWindow
{
public:
    // Lo que eligió el jugador en este paso, si eligió.
    std::optional<std::uint16_t> Step(const Input::InputState& input,
                                      std::span<const ServerGroup> groups);
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen) const;
};

class LoginWindow
{
public:
    // Con Enter u OK y los dos campos con algo, la cuenta y la clave; el campo de la clave queda
    // vacío.
    std::optional<App::Credentials> Step(const Input::InputState& input);
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen) const;
};
} // namespace Client::UI

namespace Client::App
{
enum class SessionState : std::uint8_t
{
    Idle,
    ConnectingConnectServer,
    WaitingConnectHello,
    WaitingServerList,
    ChoosingServer,       // CL5.5: la lista llegó, se espera ChooseServer
    WaitingServerAddress,
    ConnectingGameServer,
    WaitingGameHello,
    WaitingCredentials,   // CL5.5: el GS saludó, se espera SubmitCredentials
    WaitingLoginResult,
    LoggedIn,
    Failed,
};

class ServerSession
{
public:
    // Lo de la CL4, sin las Credentials en SessionSettings, más:
    [[nodiscard]] std::span<const Protocol::Connection::ServerEntry> ServerList() const;
    void ChooseServer(std::uint16_t code);           // solo en ChoosingServer; si no, un aviso
    void SubmitCredentials(Credentials credentials); // solo en WaitingCredentials; si no, un aviso
    // El resultado del F1 01 rechazado, para el aviso.
    [[nodiscard]] std::optional<std::uint8_t> LoginResult() const;
};

// Lo que la CL4 hacía sin UI: con --auto-login, el primero de la lista y las variables de la
// pregunta 7 de la CL4. Su Poll va en el pollNetwork del SceneManager, después del de la sesión
// (CL5.4); la LoginScene no lo conoce.
class AutoLogin
{
public:
    AutoLogin(ServerSession& session, Credentials credentials);
    void Poll();
};
} // namespace Client::App
```

- [ ] **Paso 1 (PR A):** confirmar con Context7 `SDL_StartTextInput(SDL_Window*)`,
  `SDL_StopTextInput` y el `SDL_TextInputEvent` de SDL3 (el texto en UTF-8 y su vida), y que el
  IME no manda texto mientras compone.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** antes de los tests, la tabla del `TextField`: qué hace
  con una letra, una tilde (`á`, dos bytes en UTF-8), un emoji, un espacio, el byte 11 cuando ya
  tiene 10, Backspace con el campo vacío, y qué muestra la clave.
- [x] **Paso 3 (PR A):** los tests:
  - `InputStateTest.CollectsTheTypedText` y `ConsumeEdgesEmptiesIt`;
    `InputEventsTest.TranslatesTheTextInput`, `TheMouseIsInFramebufferPixels`;
  - `TextFieldTest.TakesPrintableAscii`, `DropsWhatIsNotAscii`, `StopsAtTenBytes`,
    `BackspaceOnEmptyDoesNothing`, `TheMaskedFieldShowsStars`, `ClearEmptiesIt`; y que el
    `PrintTo` de un campo con clave no la escribe (`UiPrinters.h`);
  - `ServerListModelTest.GroupsByCodeOverTwenty` (400 y 401 juntos, 380 aparte; el orden, el de la
    CL5.R: el 420 sería del grupo 21, que no tiene botón y MuRet ignora, Q-C62; el 380 sale por la
    regla de MuRet, porque el original corta en el lugar 5, vacío),
    `ServerListModelTest.AnEmptyListHasNoGroups`, y los de Q-C62 y Q-C63
    (`KeepsTheOrderOfTheOriginal`, `AListWithoutGroupTwentyShowsItsGroups`,
    `IgnoresTheGroupsWithoutAButton`, `AGroupKeepsTwentyServers`);
  - `ServerListWindowTest.AClickOnAServerChoosesItsCode`, `AClickOutsideChoosesNothing`,
    `ItIsCenteredInPixels` (en 640 × 480 y en 1024 × 768, con la ubicación de `0x65f300`),
    `ShowsTheTwentyServersOfAGroup` (Q-C63);
  - `LoginWindowTest.TabMovesTheFocus`, `EnterWithBothFieldsSubmits`,
    `EnterWithAnEmptyFieldDoesNotSubmit`, `SubmittingEmptiesThePassword`.
- [ ] **Paso 4 (PR A):** el texto tecleado en `Input` y `Platform`, y los widgets.
- [ ] **Paso 5 (PR B):** los tests de la sesión con la UI, con `FakeSocket` de CS y de GS (los de la
  CL4):
  - `ServerSessionTest.WaitsForTheChoiceAfterTheList` (llega la lista: `ChoosingServer`, y no sale
    ningún `F4 03` hasta `ChooseServer`);
  - `ServerSessionTest.ChoosingSendsTheCode` (con FoxAnti, `ChooseServer(400)` sale como
    `C1 06 F4 03 00 00`);
  - `ServerSessionTest.WaitsForTheCredentialsAfterTheGameHello` (el saludo del GS:
    `WaitingCredentials`, y el keepalive del timer de la CL4.5 no sale antes del login);
  - `ServerSessionTest.SubmittingSendsTheLogin` (con `"user0001"` y `"pass0002"`, el `F1 01` sale
    y se compara con `SameBytesWithoutValues`);
  - `ServerSessionTest.ChoosingOutOfTurnIsIgnored`, `SubmittingOutOfTurnIsIgnored`;
  - `ServerSessionTest.ARejectedLoginKeepsItsResult`;
  - `AutoLoginTest.ChoosesTheFirstAndSubmitsTheEnvironment`;
  - `LoginSceneSessionTest.TheListShowsWhenItArrives`, `TheLoginWindowShowsWithTheGameServer`,
    `ARefusedConnectShowsText402`, `APeerCloseShowsText3`, `LoggedInGoesToCharacterSelect`.
- [ ] **Paso 6 (PR B):** la `ServerSession` que espera, `AutoLogin`, `--auto-login` y la
  `LoginScene` con sus ventanas.
- [ ] **Paso 7 (a mano):** en Windows, `Main connect /u<ip del CS> /p44405 --data <Data>` contra el
  laboratorio: la lista, la elección, el login aceptado (pasa a la selección vacía) y uno con una
  clave mala (el aviso). En Linux, lo mismo si hay red al laboratorio. Las capturas, fuera de Git;
  la cuenta, solo tecleada.
- [ ] **Paso 8:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug
  ctest --preset linux-x64-debug \
    -R "InputState|InputEvents|TextField|ServerList|LoginWindow|ServerSession|AutoLogin|LoginScene"
  xvfb-run -a -s '-screen 0 800x600x24' build/linux-x64-debug/Client/Main --frames 60
  ```

## Qué no se prueba en CI, y cómo se cubre

| Qué | Cómo |
| --- | --- |
| el `SpriteBatch`, el diseño del texto, los colores, las escenas, la UI y la sesión | los tests de `render/`, `scenes/`, `ui/`, `app/`, `data/` y `support/`, sin GL: corren en los dos sistemas |
| lo que hace GL con eso (texturas, mezcla, alpha test, orden) | los de `render_gl/`, con Xvfb y Mesa en Linux (`ctest -L gl` con `MURET_CLIENT_REQUIRE_GL=1`); en Windows, a mano |
| FreeType con una fuente de verdad | con `MURET_CLIENT_FONT_FILE`, la fuente del runner (CI la pone); sin la variable se saltean, y con una ruta que no existe fallan, como D-C9 |
| las texturas, `Text.bmd` y los mundos del laboratorio | los golden, con `MURET_CLIENT_GOLDEN_DIR` (CI no tiene la carpeta) |
| que el login se vea como el del original | a mano, con capturas fuera de Git; con la traza de P-C13, los quads pasan a compararse contra ella |
| el texto de GDI contra el de FreeType | a ojo, con las capturas: no hay test que los compare (07) |
| la lista, el login aceptado y uno rechazado contra el GS del laboratorio | a mano (CL5.5, paso 7) |
| el mundo 3D detrás del login | no está en la CL5: la CL6 y la CL7 (pregunta 3) |

## Al cerrar la CL5

- [ ] Completar el cierre de la guía de la CL5.0.
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Sumar al plan de la CL7 el mundo de la escena de login (el mapa 55, `World56` y `Object56`,
  con el `IWorldView` de la CL5.4), y al de la CL6, el logo (`0xa2`) y el personaje con el cuervo.
- [ ] Escribir el plan de la CL6 (modelos y selección de personaje) en su propio PR, con lo
  aprendido: la `CharacterSelectScene` vacía de la CL5.4 es la que llena la CL6.3, con el `F3 00`
  que el original manda al aceptar el login, y el pase 3D con el viewport de `0x634680`.
