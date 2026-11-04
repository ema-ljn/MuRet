# Plan de la ola CL6: Modelos y selección de personaje

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md).
> Escrito el 2026-11-03, con lo aprendido en la CL4 y la CL5. Cada sub-ola trae su contrato, sus
> nombres y sus tests; lo que se aprenda en una se escribe acá antes de arrancar la siguiente.
>
> **Quién escribe los pasos `TODO(human)`:** por pedido de Emanuel (2026-11-02, pregunta 1 de la
> CL2), Claude los resuelve también, hasta que él diga que participa. En
> [PROGRESS](../../learning/PROGRESS.md) quedan como resueltos por Claude.

**Objetivo:** que el cliente haga lo que hace el `main.exe` desde que el GameServer acepta el login
hasta que pide entrar al mundo. Pide la lista de personajes (`F3 00`), los pone en sus cinco
lugares con su clase y su equipo, animados como el original (el esqueleto de `Player.bmd`, las
partes, la velocidad de cada acción, la mascota), deja elegir uno con el mouse y entrar con Enter
o con doble clic, y pasa por la carga del mundo (escena 3) a la escena del juego (5), que manda el
`F3 03` con el nombre. Y el login suma lo que la CL5 le dejó: el logo como modelo (`0xa2`,
`Logo03.bmd`) y el personaje con el cuervo oscuro. Todo sobre el OpenGL 3.3 core de la CL5, con
los modelos transformados en la CPU como en el original (llegan a GL en coordenadas de mundo, 06),
así que la pose de MuRet se compara vértice por vértice con las trazas.

**Lo que esta ola no dibuja todavía:** el terreno y los objetos del mapa 54 (`World55`) detrás de
la selección, y los del 55 (`World56`) detrás del login. Son de la CL7. La CL6 no los espera,
porque nada de lo que dibuja depende de la altura del terreno: el original pone a los cinco
personajes de la selección en z = 163 en cada frame (`0x634680`), el logo del login en
(10.680; 15.550; 1.710) y su personaje en (10.603; 14.112; 580), todos fijos. Los modelos van en
el pase 3D de cada escena, con su cámara y su viewport, sobre los fondos 2D que el original dibuja
antes del pase (`0x717` y `0x718` en la selección, los del login de la CL5.4) y sobre el negro
donde no llegan; el pase recibe el mapa de la escena como dato (`SceneWorlds.h`), y la CL7 dibuja
ahí el terreno de cualquier `World<N>` con la misma cámara. Tampoco van los efectos (el
`0xbf` del login, `0x443fa0`, y los de las armas), el brillo de los ítems que la traza no muestra,
la música ni la UI de la selección (los botones de crear, borrar y conectar): preguntas 3, 10 y 11.

**Arquitectura:** dos carpetas nuevas, dos que crecen y una que nace antes de su ola.
- `Client::Data::Models` (`Client/src/Data/Models/`, la que el roadmap reserva para la CL6): el
  `.bmd` como datos, sin GL y sin matemática (CL6.1). Como la CL1: bytes → tipos, errores como
  valor, los floats con los bits del archivo.
- `Client::Render::Models` (`Client/src/Render/Models/`, en `ClientLib`): la pose, el skinning, el
  avance de la animación y el brillo, en la CPU y sin GL (CL6.2). Es lo que el original hace antes
  de cada `glBegin(GL_TRIANGLES)`, y por eso se prueba sin ventana y contra la traza.
- `Client::Render` suma el lado 3D de lo que la CL5 hizo en 2D: el pase de `0x620db0`, con su
  viewport, su proyección y su vista (`Pass3D.h`), un lote de triángulos en coordenadas de mundo
  (`ModelBatch.h`, el par del `SpriteBatch`) y, en `ClientRender`, el `Gl::GlModelRenderer` que lo
  dibuja (CL6.3). La cámara que se mueve es de la CL7 (`World/`, del roadmap): le da al pase un
  `Camera3D`, como las cámaras fijas de la selección y del login.
- `Client::Scenes` llena la `CharacterSelectScene` vacía de la CL5.4, suma la `WorldLoadingScene`
  (escena 3) y una `GameScene` (escena 5) que por ahora solo manda el `F3 03`, y el login suma el
  logo y el personaje (CL6.4).
- `Client::GameLogic` (`Client/src/GameLogic/`, del roadmap para la CL7 en adelante) nace con la
  apariencia del personaje: de la clase y el equipo de la lista a los modelos de sus partes
  (`0x517270`, `0x5139f0`). La selección la necesita antes que el juego.
- `Client::Protocol` suma los `F3` de la selección y `App::ServerSession` sus estados (CL6.3).

**Stack:** C++20, OpenGL 3.3 core con GLSL 330 (un VBO por frame con los vértices ya
transformados, depth test, culling por malla, la mezcla de la CL5), glm 1.0.3 (cuaterniones,
matrices; visto con Context7 el 2026-11-03, pregunta 5), SDL3 (el `clicks` del evento del mouse
para el doble clic, también visto con Context7), GoogleTest. No entra ninguna biblioteca nueva.

**Guía de estudio:** `docs/learning/ola-cl6.0-huesos-y-cuaterniones.md`, que se escribe al
arrancar la CL6.0 (L-10). De la CL6.1 en adelante, la sección de cada sub-ola de este plan.

**Research que fija esta ola:**
- [05](../../../Client/docs/research/05-formatos-de-datos.md#modelos-bmd): el contenedor `.bmd`
  (recorrido en los 1.962 modelos del laboratorio sin bytes sobrantes), el skinning rígido, el
  esqueleto en `Player.bmd` y las partes en archivos aparte.
- [06](../../../Client/docs/research/06-render-escenas-y-ui.md): lo que el original le pide a GL,
  la cámara (§ Cámara, con la fila de la selección), § Modelos y objetos (el personaje vértice a
  vértice, los pases de brillo de `0x4ecc30`, § Cuánto avanza la animación, § El modelo en el
  binario) y § Cada frame de una
  escena (los cinco pases de la selección).
- [04](../../../Client/docs/research/04-protocolo.md#el-flujo-de-conexión): el `F3 00`, la lista
  (`C1 29`), el `F3 03` con el nombre de 10 bytes y lo que contesta el servidor; § Una sesión real
  de juego, el orden en el cable.
- [02](../../../Client/docs/research/02-arranque-y-ciclo-de-vida.md): las cinco escenas y sus
  transiciones, y qué avanza por paso, por frame y por reloj (P-C03).
- [07](../../../Client/docs/research/07-quirks-y-bugs.md): Q-C20 (la mascota y las alas por
  frame), Q-C21 (el fin de acción adelantado en la selección), Q-C22 (el reloj del brillo en un
  `float`) y Q-C23 (un modelo que falta), que la CL6.R corrige; y Q-C29 (`wolfa02`, el OZT que
  el cargador rechaza), porque qué dibuja una malla sin su textura es de esta ola (CL6.R, paso 6).
- [08](../../../Client/docs/research/08-pendientes.md): P-C01 (a) a (d) y P-C06.
- [09](../../../Client/docs/research/09-datos-disponibles.md): qué modelos hay (1.962) y faltan
  (`Monster50`), qué cargador pide cada uno (`0x60dbe0` el personaje), las texturas de los pases
  de brillo (GL 115, 120 y 141) y que **no hay volcados de memoria de modelos**: los oráculos son
  las trazas de GL.
- Las trazas de [mu-online-reverse-lab#45](https://github.com/EmaNLJN/mu-online-reverse-lab/pull/45)
  (`graphics-01/`, la selección y Lorencia, cinco frames cada una), fuera de Git (pregunta 12).
- En `client-research/` del repo de research, el decompilado de `mainexe-vmxp-c32bbd5f`:
  `0060da30`, `004f13d0`, `0060dbe0`, `0060d5f0`, `0060f7d0`, `004eb860`, `004ec200`,
  `005fcd20`, `005fce90`, `00634680`, `006342c0`, `00633f20`, `0061ca30`, `004c20a0`, `00517270`,
  `005139f0`, `006356f0`, `00636290`, `006349a0`, `004ebc80`, `004ed890` y `00620db0`.

**Lo que dice el binario de los modelos y de la selección** (salió del decompilado para este plan,
con cada dirección mirada en el desensamblado con `objdump`; la CL6.R completa y lo pasa al
research). Como en el research, *estático* es lo leído en el binario; *verificado*, lo que
mostraron las trazas; *inferido*, lo que se deduce:

| Qué | Dónde | Qué hace |
| --- | --- | --- |
| el nombre del archivo | `0x60da30(modelo, carpeta, nombre, número)` | `"%s.bmd"` con el número en -1 (`0x6c9e74`), `"%s0%d.bmd"` con uno menor que 10 (`0x6c9e68`) y `"%s%d.bmd"` con los demás (`0x6c9e5c`). Con `DAT_006c9e0c` en 1 (el de la `.data`, el modo `Data\`) lee con `0x4f13d0`; si el archivo no abre, `"%s file does not exist."` (`0x6c9e28`), `MessageBoxA` y `WM_DESTROY` **solo si el nombre es `Monster`, `Player` o `PlayerTest`** (`0x60daec`, `0x60db24`, `0x60db5c`); cualquier otro se saltea sin avisar. *Estático* |
| leer un `.bmd` | `0x4f13d0`, con `"rb"` (`0x4f1408`) | el archivo entero; el byte 3 es la versión. Con `0x0C`, el tamaño en `+4` (u32) y el resto cifrado con K16 desde `+8` (la clave, la semilla y el paso de `Data/Terrain/K16.h`, escritos otra vez adentro); con cualquier otra versión, en claro desde `+4`. No mira el `"BMD"` ni que los tamaños entren (Q-C86). La versión va a `+0x20` (un byte) y los tres int16 de la cabecera, en el orden del archivo (mallas, huesos y acciones), a `+0x24`, `+0x22` y `+0x26` (CL6.R; antes esta fila decía mallas en `+0x20`, huesos en `+0x22` y acciones en `+0x24`); de cada triángulo copia los primeros 36 de sus 64 bytes, y `0x4f0870` y `0x4f0910` pisan los de `+0x1a..+0x1f` con la adyacencia que calculan. De cada malla, lo que sigue al primer `_` del nombre de su textura, hasta el primer punto, es un script (`0x459940`: `R`, `H`, `S` y `N` en mayúscula prenden los bytes 0 a 3 de `+0x24`; otra letra, sin script; el byte 4 queda en 0, porque sus casos `DC` y `DT` son código muerto). De cada acción escribe `+0` en 0, las keys y `lock_positions`, y no `+4`: el arreglo sale de `operator new` sin limpiar, y la velocidad de una acción que `0x60dbe0` no escribe es lo que había en el heap. Calcula el cuaternión de cada key con `0x5fcd20`. Al final, `0x4f1f20(0)`: sin hueso de la cabeza (`+0x54` en -1) y las cajas de cada hueso (`0x4f1f80`). *Estático* |
| `0x4f1ac0` | con `"wb"` (`0x4f1adc`) | **escribe** un `.bmd`: `'B'`, `'M'`, `'D'` y la versión `0x0C`. Lo llama `0x60da30` solo con `DAT_006c9e0c` en 0, el modo de desarrollo que lee `Data2\`, y las tres escrituras de 0 están detrás de `DAT_07d366bc`, que nadie escribe. El research lo daba como el cargador: la CL6.R lo corrigió en 07 (Q-C23) y 09. *Estático* |
| el personaje | `0x60dbe0`, desde `0x61ccb0` (la carga, paso 1 de 10) | la tabla de modelos (`0x489d` de `0xe0` bytes, con la base corrida `rand() & 0x3ff` modelos: no se reproduce, pregunta 4); `Player.bmd` en el `0x20a`, y si trae mallas, `"Player.bmd file error!!!"` y `WM_DESTROY` (Q-C88); las partes por clase (`HelmClass` a `BootClass` del 1 al 5 en `0x2212` y siguientes) y las del equipo (`HelmMale` N en `0x1011` + N, `ArmorMale` en `0x1211` + N, y así), `Shadow` en el `0x20b` (`0x60f2f4`), 213 cargas en total (las filas, en la CL6.R), con tres huesos en el modelo (`+0x54`, la cabeza, en 20; `+0x58` en 6 y `+0x5c` en 13, los pies, *inferido*), y la velocidad de cada acción. *Estático*; los 213 archivos están en el laboratorio |
| la velocidad | `0x60dbe0`, al final: después de `Shadow` (`0x60f2f4`) y de los tres huesos (`0x60f301`..`0x60f32b`), en `0x60f335`..`0x60f754` | 181 escrituras en `+4` de las acciones de `0x10` bytes, en rangos y sueltas, y las de después pisan a las de antes: 148 acciones con velocidad, 57 sin ninguna (la 0 y de la 34 a la 63, entre otras), y cuatro que no repiten (`+0` en 1: la 68, la 193, la 195 y la 196). 0,28 de la 1 a la 12 salvo la 3 (0,26), la 4 y la 5 (0,24) y la 7 y la 8 (0,22); 0,3 de la 13 a la 33 salvo la 21 y la 29 (0,35); la tabla entera, en 06. Las 57 sin escritura quedan con lo que había en el heap (Q-C87). *Estático*; la 3 y la 1, *verificado* con el reloj de la traza (06) |
| las texturas de un modelo | `0x60d530(primero)` y `0x60d5f0(modelo, carpeta, wrap, filtro, chequear)` | `0x60d530` fija el próximo slot libre y pone el principio de la búsqueda en 0; lo llaman los cargadores, no `0x61ccb0` (la tabla de cada uno, en 06 § El modelo en el binario). Por cada malla, `0x60d550` busca su nombre desnudo, sin la carpeta y distinguiendo mayúsculas (Q-C90), en los slots desde el 0 hasta el próximo: si está, la malla usa ese; si no, carga `carpeta + nombre` en el próximo y lo corre, aunque el archivo falte. El cargador es por la letra después del primer punto, con `tolower` (`0x6a0cce`: `'t'` o `'T'` el OZT, siempre con `GL_NEAREST`; otra el OZJ, con `filtro`), y el wrap es `wrap`. Después, los nombres que empiezan con `ski` o `level` (en minúsculas, byte a byte) pasan al slot `0x12e`, `hid` al `0x12d` y `hair` al `0x137` (la piel y el pelo, que el dibujo cambia por clase), y el slot que se cargó queda ocupado. El quinto argumento de los cargadores es el modelo (su nombre de cabecera va al error) y el de `0x60d5f0`, `chequear`, vale 1 en todas las llamadas: una textura que falta cierra el juego. El login no llama a `0x60d530`: su `Logo03` va donde dejó el próximo el mundo 55, que el login carga antes, `0x455` y `0x456` (Q-C92; *inferido*, P-C15). *Estático*; el asignador, *verificado* en 750 slots contra `lor_slots.bin` |
| la pose | `0x4eb860` | por hueso, entre la key entera anterior (la acción y la key que guarda el objeto) y la de ahora, con la parte fraccionaria del frame: `0x5fce90` (slerp) entre los cuaterniones, salvo que `0x5fc700` los dé iguales (cada componente a 0,001 o menos, el double `0x6b8a88`), y lineal entre las posiciones. El hueso de la cabeza resta a su ángulo x el `+0x28` del objeto y a su z el `+0x2c` (en grados por el π/180 en float de `0x6ba140`; `0x4eb9ff`..`0x4eba1d`) y pasa a cuaternión con `0x5fcd20`. El hueso 0 (por su índice, no por no tener padre), si la acción anterior o la de ahora tiene `lock_positions`, toma x e y de la key 0 de la de ahora y suma `+0x84` a la z interpolada. La raíz: `0x5fc8f0` con los ángulos del **modelo** (`+0x78`, en grados, π/180 en `0x6ba360`) y, con el octavo argumento, la escala (`+0x68`) y la posición (`+0x6c`) del modelo, que `0x5ffdf0` copia del objeto antes de posar; con el séptimo distinto de 0, la raíz es la que dejó quien llama en `0x68c4a1c` (así cuelgan las armas). `+0x84` también es del modelo, y vale 0 en los personajes (CL6.R; antes esta fila decía que eran del objeto). Los huesos dummy no se posan. Después, `0x4ebc80` lleva cada vértice al mundo con `0x5fccc0` y cada normal con `0x5fcbd0`. *Estático*; la pose entera, *verificado* contra las trazas (06) |
| la matemática | `0x5fcd20`, `0x5fce90`, `0x5fcdb0`, `0x5fca90`, `0x5fc8f0`, `0x5fccc0`, `0x5fcbd0` | la de Half-Life: `AngleQuaternion` (medio ángulo con el double 0,5 de `0x6ba718`, Rz · Ry · Rx), `QuaternionSlerp` (da vuelta q en el lugar si Σ(p − q)² > Σ(p + q)²; lineal si 1 − cos ≤ 1e-8, el double `0x6ba728`; el caso perpendicular, con π/2 en `0x6ba720`, no corre después de dar vuelta), `QuaternionMatrix` (3 × 4), `R_ConcatTransforms`, `AngleMatrix`, `VectorTransform` y `VectorRotate`, en la x87. *Estático* |
| el avance | `0x4ec200` (`ret 0x18`) | solo si el modelo tiene acciones y la de ahora más de una key (si no, ni avanza ni termina): `frame += velocidad`; al cambiar la key entera, guarda la vieja y su acción como anteriores. Con el frame en 0 o menos, le suma las keys menos 1; al pasar la última, módulo las keys (o las keys menos 1 con `lock_positions`) si repite, o se queda en las keys menos 0,01 (`0x6b88d4`) si no. En la escena 4 decide "terminó" con el frame + 2 (`0x6b8818`, `0x4ec2ea`), y el módulo usa el real (Q-C21). *Estático*; los saltos de 33 y 34 pasos, *verificado* (06) |
| entrar a la selección | `0x633f20`, desde `0x6342c0` | el mapa 54 (`0x61a560(0x36)`, `World55`), sus texturas (`0x61ca30`), un objeto de personaje suelto con el modelo `0xa9` (`NewFace02`, `0x511290`: la vista previa de la ventana de crear, cuya clase y piel lee el `F3 01`), ningún personaje elegido (-1), la UI en el modo 4 (`0x65f4b0`), y el texto 1240 de `Text.bmd` con los dos números de `pkt[5]` de la lista, si alguno no es 0. *Estático*; lo de `0x4e79a0`, `0x40f370` y `0x40b240` es del anticheat y no se lee |
| las texturas de la selección | `0x61ca30` | `Interface\cha_id`, `cha_bt`, `deco`, `b_create`, `server_menu_b_all`, `b_connect`, `b_delete` y `character_ex` (`.tga`, slots `0xc` a `0x13`), `Logo\character_back01.jpg` y `02` (`0x717`, `0x718`), `Effect\Impack03.jpg` (`0x719`) y `Logo\chasellight.jpg` (`0x729`), todas con `0x2600` y `0x2900`; los modelos `0xa4` (`Logo05.bmd`) y `0xa8` a `0xac` (`NewFace01` a `05.bmd`), con sus texturas desde el slot `0x6f8` (31, hasta el `0x716`: Q-C91), y la velocidad 0,3 en las acciones 0 y 1 de cada `NewFace`. *Estático* |
| el dibujo | `0x634680` | sale si no entró o la conexión está antes de `0x33`; la cámara (`0x635ae0`, fija en la escena 4: pisa la ruta 5 que `0x633920` movió en la lógica) y los dos fondos 2D (`0x621b60(0x717, 0, 25, 320, 320, …)` y el `0x718` en x = 320); el pase que no dibuja (`0x620db0(0, 0, w, 48)`); `glClearColor(0, 0, 0, 1)`; el pase 3D en `0x620db0(0, 25, 640, 430)`; los cinco primeros personajes en z = 163 y con la luz `+0xf4` en (0, 0, 0); con uno elegido, la suya en (1, 1, 1) y la luz del terreno `0x5f9780` en su lugar, entre `0x620a90` y `0x620980`; el terreno (`0x5fc260`), los personajes (`0x510d00`, `0x59ec80`), los objetos (`0x53c6a0`) y los efectos; con uno elegido, su `+0xf4` en 0,15, dos `0x53fe70(0x729, …)` (la luz de abajo) y el nombre (`0x4129f0`); el 2D y la UI. El `+0xf4` entra al color por `0x50ba30`, no por `0x4ebc80`: 0,4 + `+0xf4` en la escena 4, o sea 0,4 y 1,4 (el elegido), por el factor del nivel (`0x60b3d0`); el 0,15 no llega al cuerpo. *Estático*; la cámara, el viewport, los fondos y el personaje en z = 163, *verificado* (abajo) |
| la lógica | `0x6342c0` | Con el estado antes de `0x33` no hace nada. Enter (`GetAsyncKeyState(0xd) == 0x8001`) con cuatro ventanas cerradas y uno elegido del 0 al 4 suena el `0x19`; con el bit `0x1` de su `+0x1d8`, el aviso `0x84`; si no, copia su clase, su nivel y su nombre al personaje propio, suelta los slots `0xc` a `0x1d` y `0x6f8` a `0x71d` y los modelos (`0x61cc50`) y pasa a la escena 3. Esc suena el `0x19` y abre la ventana `+0x3d0`. Con `+0x8de8` de la UI en 0, el clic elige el que está bajo el mouse (`DAT_006c8e30`, de `0x59ec80` → `0x59e700`: el segmento de la cámara al far contra la caja de cada personaje, alineada a los ejes, con una separación en 6 ejes, `0x622900`; la del jugador va de (−60, −60, 0) a (40, 40, 120), Q-C100; gana el de menor distancia al origen), y sobre ninguno deja ninguno elegido (-1); el doble clic (un reloj propio de `0x640940`, que un movimiento del mouse cancela, Q-C99), con la bandera `+0x7f5c` de la UI, elige el de abajo y entra como Enter, sin el sonido. *Estático* |
| la lista | `0x4c20a0`, `F3 00` | `pkt[4]` a `0x622b20` (lo lee la ventana de crear, `0x63d7c0`: las clases que se pueden crear, *inferido*), las dos mitades de `pkt[5]` al texto 1240, `pkt[6]` la cantidad (0: estado `0x33` y nada más), y desde `pkt[7]` una entrada de `0x22` bytes por personaje: `[0]` el lugar (0 a 4, con una posición fija cada uno), `[1..10]` el nombre, `[0xc..0xd]` el nivel (*inferido*), `[0xe]` las banderas (`0x1`: bloqueado), `[0xf]` la clase (`0x517270` recibe `((b & 0x10) | (b >> 4)) >> 1`: la clase y la segunda clase en el bit 3), `[0x10..0x20]` el equipo (`0x5139f0`) y `[0x21]` a `+0x368` (el estado en el gremio, *inferido*). Un lugar de más de 4 no carga la posición: la primera entrada se queda con basura de la pila y las otras con la del anterior (Q-C101; antes esta fila decía "la del anterior"). No mira el largo del paquete. *Estático*; `C1 29` (7 + 34) en el cable, *verificado* (04) |
| la carga del mundo | `0x6356f0` (escena 3) | sube `Interface\LSBg01` a `04.JPG` a `0x71e`..`0x721`, arma un objeto (`0x640c10`) con **las cuatro** en un mosaico de 2 × 2 en 800 × 600 (`LSBg01` arriba a la izquierda, de 400 × 512; `02` a su derecha; `03` y `04` abajo, de 88 de alto), dibuja **un** frame (`0x620db0(0, 0, 640, 480)`, `glClear`, el 2D, `0x640ea0`, `SwapBuffers`), pasa a la 5 y suelta los cuatro. *Estático* |
| el `F3 03` | `0x636290`, el primer paso de la escena 5 | loguea `> Character selected <%d> %s` (el nombre) y, con `DAT_07d366bc` en 0 (nadie lo escribe, CL5.R: siempre), pasa al estado `0x3c` y manda `C1 0E F3 03` con el nombre y el XOR32 (`0x6362b2`..`0x63638d`). **No lo manda la selección.** *Estático*; la forma, *verificado* en el cable (04) |
| el login | `0x6349a0` | `0x61c7c0` carga `Logo03.bmd` en el `0xa2` (`0x60da30(0xa2, "Data\\Logo\\", "Logo", 3)` y sus texturas con `0x60d5f0(0xa2, "Logo\\", 0x2901, 0x2601, 1)`), `0x606660(0xa2, …)` lo pone en (10.680; 15.550; 1.710) con los ángulos (10, 0, 36) y escala 1, que el mismo `0x606660` pisa con **2,2** en las escenas 2 y 4 (`0x6068d5`), con el `blendMesh` en 1 (la malla de la textura 1, aditiva) y sin luz por vértice (`+0xe8` en 0); `0x517270(2, 4, …)` arma el personaje 2 con las partes `0x102e`, `0x122e`, `0x142e`, `0x162e` y `0x182e` en +7, el `0x620` en +13 y los `0x1c17`, `0x1c30` y `0x1c16`; después la escala 1,6, los ángulos (0, 0, 188), la posición (10.603; 14.112; 580), el efecto `0xbf` (`0x443fa0`) y el cuervo (`0x420d60`: un objeto aparte de `0x460` bytes, `0x4167b0`). Con la numeración de `0x60dbe0` y `0x60ff50` (el modelo de un ítem es `0x212` + grupo · 512 + índice), es la clase 4 con el juego 29, una maza del grupo 2, el cuervo (13, 5) en la mano izquierda, la capa (13, 30) y el caballo oscuro (13, 4) de mascota (*inferido*). *Estático* |

**Lo que mostraron las trazas para este plan** (*verificado*, con un script fuera de los repos
sobre `character-selection-Frame_003207.txt`; la CL6.R lo pasa al 06): la cámara del pase del
mundo es la misma en los cinco frames (`glRotatef` Y 0°, X −84,5°, Z −45°, `glTranslatef(−13.909;
−14.482; −543)`, la perspectiva con cot 17,5° = 3,171595, o sea 35°, y el aspect 1024/688); antes
van la perspectiva de 45° que no dibuja, los dos fondos (`0x717` y `0x718`, de (0, 728) a (512,
216) y de (512, 728) a (1024, 216) en 1024 × 768) y el pase de la franja de arriba; y las mallas
del personaje caen alrededor del lugar 0 de `0x4c20a0`, (12.880; 15.046), con los pies a la
altura de z = 163 (el vértice más bajo de las botas queda apenas por debajo).

**Lo que el binario dice distinto del research o del roadmap** (la CL6.R lo corrige donde está):
- **El `F3 03` sale de la escena 5, no de la selección.** El roadmap pone "los paquetes de
  selección" en la CL6.3; el original elige en la 4, dibuja un frame en la 3 y manda en el primer
  paso de la 5. La CL6.3 arma la 3 y una 5 mínima, como la CL5.4 armó la 4 vacía (pregunta 9).
- **`0x4f1ac0` escribe, no lee** (Q-C23 y 09): el lector es `0x4f13d0`, y un modelo que falta
  cierra el juego si se llama `Monster`, `Player` o `PlayerTest`. `Monster50`, que el laboratorio no
  trae, cerraría el original cuando aparece el tipo 58 en el mapa 8, no "no se dibujaría"
  (pregunta 8).
- **La pregunta 6 de la CL5 nombra mal el slerp:** `0x5fcd20` es `AngleQuaternion`; el slerp es
  `0x5fce90`.
- **La cabeza gira en x y en z, no en x y en y** (06 § El personaje, vértice a vértice): el
  desensamblado resta el `+0x28` del objeto al ángulo x de la cabeza y el `+0x2c` al z
  (`0x4eb9ff`..`0x4eba1d`), y 06 dice x e y.
- **El roadmap no tiene CL6.0**, y L-1 del camino pide una N.0 antes de cada ola (pregunta 2).

**Terminada cuando** (roadmap: el personaje aparece animado en la selección). En concreto:
- con `--data` y contra el GS del laboratorio, el login aceptado manda el `F3 00`, la selección
  muestra cada personaje de la lista en su lugar, con su clase, su equipo y la animación que le
  toca, a la velocidad de su acción por paso de 40 ms y la mascota por tiempo; un clic elige,
  Enter o doble clic entra, la escena 3 dibuja su frame y la 5 manda el `F3 03`; el servidor
  contesta (la CL7 lo lee);
- el login muestra el logo y el personaje con el cuervo, sobre sus fondos y sin el mundo hasta la
  CL7;
- la pose de MuRet cae sobre los vértices de la traza de la selección a menos de 0,02 unidades en
  los cinco frames (`GoldenSkinningTrace`), las UV de los tres pases de brillo a menos de 0,02, y la
  cámara y el viewport dan las matrices de la traza;
- todos los `.bmd` del laboratorio se leen, todos los modelos que pide `0x60dbe0` están o son un
  faltante conocido, y nada en el código nombra un modelo, una clase, un personaje o un mapa fijo
  fuera de las tablas del original;
- en Linux con Xvfb (CI), los tests de GL dibujan un modelo con profundidad y culling y leen los
  píxeles; sin `--data`, `Main --frames 60` sigue con la `TestScene`.

## Preguntas para decidir al empezar

1. **Quién escribe la CL6.** *Decidido (Emanuel, 2026-11-02, en la CL2): Claude escribe todo*, la
   CL6.0 en `labs/` y los pasos `TODO(human)` incluidos, hasta que digas que participás. Las
   preguntas 2 a 13 siguen con su recomendación mientras no digas otra cosa.
2. **Una CL6.0.** El roadmap no la tiene, y el camino (L-1) pide una N.0 de fundamentos antes de
   cada ola; la CL3, la CL4 y la CL5 tuvieron la suya. Lo nuevo de esta ola es justo lo que no se
   aprende de pasada: rotaciones en 3D, cuaterniones, interpolarlos y una jerarquía de huesos.
   *Recomendación:* sumar la CL6.0 en `labs/cl6.0-huesos/`, antes de la CL6.R, con los cuaterniones
   y el skinning a mano comparados con glm, como la CL5.0 comparó su matriz con `glm::ortho`. La
   fila de la CL6 del roadmap se corrige con este plan, como el #59 sumó la CL5.R.
3. **Cómo dibuja la CL6 sin el terreno.** *Recomendación:* no esperar a la CL7. Cada escena arma
   su pase 3D (`Render::Pass3D`: el viewport de `0x620db0` y la cámara de `0x635ae0`), el
   `IWorldView` lo recibe junto con el mapa (en la CL6 no dibuja nada) y los modelos van detrás,
   por el `ModelBatch`, con las posiciones fijas del original: z = 163 en la selección y las del
   login. Detrás quedan los fondos 2D que el original dibuja antes del pase (`0x717` y `0x718` en
   la selección; los del login, CL5.4), y el negro donde no llegan. La CL7 dibuja el terreno en el
   mismo pase,
   antes de los personajes, como `0x5fc260` antes de `0x510d00`. Las trazas de la selección y de
   Lorencia son la evidencia, no el alcance: los tests de los mapas siguen yendo por
   `OriginalMaps` y `LabWorlds`.
4. **Dónde vive cada pieza.** *Recomendación:* el `.bmd` en `Data/Models/` (pregunta 4 de la CL5:
   lo que no ve GL va en `ClientLib`), la pose, el skinning, el avance y el brillo en
   `Render/Models/` (también `ClientLib`), el pase (`Pass3D`) y el `ModelBatch` en `Render/`, el
   `GlModelRenderer` en `Render/Gl/` (`ClientRender`), la apariencia en `GameLogic/` y las escenas
   en `Scenes/`. glm pasa a `ClientLib` como `PUBLIC` (`glm::glm-header-only`), porque los headers
   de `Render/Models/` lo usan; sin `GLM_FORCE_*` ni `GLM_ENABLE_EXPERIMENTAL` (nada de `gtx`), así
   el PCH de `ClientLib` y los que lo reusan no cambian. No se reproduce la base corrida al azar
   de la tabla de modelos (`rand() & 0x3ff`, contra quien lea la memoria), ni una tabla de todo:
   los del personaje y sus ítems se leen una vez, en el avance 1 de la carga como `0x61ccb0`, y
   viven en `main.cpp` (`GameModels`, como `GameData`), con sus texturas subidas una vez; los de
   cada escena (`Logo03` en el login, `Logo05` y los `NewFace` en la selección) son de la escena y
   se sueltan con ella, como sus texturas (pregunta 11 de la CL5).
5. **La matemática (§10).** Visto con Context7 (`/g-truc/glm`) y en el código de glm 1.0.3:
   `glm::quat(glm::vec3)` es el mismo `AngleQuaternion` de Half-Life (medio ángulo, `w = cx·cy·cz
   + sx·sy·sz`, `x = sx·cy·cz − cx·sy·sz`, Rz · Ry · Rx), `glm::mat3_cast` es su `QuaternionMatrix`
   (la misma matriz, guardada por columnas), y `glm::perspective`, `glm::rotate` y
   `glm::translate` son `gluPerspective`, `glRotatef` y `glTranslatef`. `glm::slerp` no es el de
   `0x5fce90`: da vuelta cuando el producto es negativo (el original compara Σ(p − q)² con
   Σ(p + q)², que con cuaterniones unitarios es lo mismo salvo el redondeo), pasa a lineal con
   `cos > 1 − epsilon<float>()` (el original, con `1 − cos ≤ 1e-8` en double, pero con el cos ya
   redondeado a float, `0x5fcf10` y `0x5fcf26`: en float, solo con el cos en 1) y no toca q.
   *Recomendación:* glm para los tipos, `AngleQuaternion`,
   `QuaternionMatrix`, la composición de matrices y la cámara; a mano el slerp de `0x5fce90` y la
   igualdad de `0x5fc700`, que son pocas líneas y lo que se espera de ellas es la salida del
   original (§10). `AngleMatrix` (`0x5fc8f0`, en grados) como `mat3_cast(quat(radians(…)))`, que da
   el mismo giro, con un test contra la fórmula de Half-Life. Lo que cambia la x87 (el coseno que
   queda en la pila sin redondear) se acepta con la tolerancia de los tests, y queda en 07.
   Ojo: `glm::quat(w, x, y, z)` recibe la w primero y guarda x, y, z, w; se usa
   `glm::quat::wxyz` para que se lea. La pregunta 6 de la CL5 se corrige con la dirección buena.
6. **El skinning, en la CPU o en la GPU.** El original transforma cada vértice en la CPU y manda
   coordenadas de mundo (06: ningún bloque tiene matriz propia). *Recomendación:* en la CPU, en
   `ClientLib`, y un VBO que se llena por frame, como el 2D. Así la salida se compara con la traza
   sin GL, y 95.000 vértices por frame son poco para un VBO. Si algún día hace falta la GPU, el
   `ModelBatch` no cambia de forma.
7. **Por paso o por frame.** El cuerpo avanza por paso con la velocidad de su acción, y la mascota
   y las alas por frame dibujado (Q-C20). *Recomendación:* el frame avanza en el `Step` de la escena
   (40 ms, `App::LogicStep`), como el original; el `Render` posa una copia avanzada `alpha` de un
   paso con la misma regla (D-C4: render desacoplado con interpolación), así las keys que cruza la
   interpolación guardan su anterior igual que el paso. La mascota y las alas avanzan por tiempo, a
   la velocidad que tienen a 21,33 FPS (0,5 y 0,25 keys cada 46,875 ms), que es la corrección de
   Q-C20. El fin adelantado de la selección (Q-C21) se reproduce: la CL6.R leyó que no cambia nada
   en pantalla (al "terminar" se vuelve a elegir la misma acción de parado).
8. **Un modelo que falta** (Q-C23, corregido). *Recomendación:* lo que el original exige, MuRet
   también: `Player.bmd` que falta o trae mallas corta la carga con un error que nombra el archivo,
   como una textura que falta (CL5.4); un modelo con otro nombre que falta se saltea con una línea
   en el log, una vez, y no se dibuja. `Monster` queda para la CL9, que decide con el research ya
   corregido: el original cerraría el juego por `Monster50`, que el laboratorio no tiene.
9. **Los paquetes y la sesión.** *Recomendación:* la `ServerSession` suma tres estados después de
   `LoggedIn` (`WaitingCharacterList`, el `0x32`; `ChoosingCharacter`, el `0x33`; `EnteringWorld`,
   el `0x3c`) y el keepalive corre en los cuatro. `RequestCharacterList` manda el `F3 00`, y la
   llama la `CharacterSelectScene` al entrar: es el mismo paso en que el original lo manda
   (`0x634c60` cambia de escena y manda, `0x634dce` y `0x634e0e`). `ChooseCharacter(slot)` guarda el
   elegido sin mandar nada, y `EnterWorld` manda el `F3 03` desde el primer paso de la `GameScene`,
   como `0x636290`. La respuesta del `F3 03` (`0x4c3940`, el personaje y su mapa) es de la CL7: en
   la CL6 queda sin handler, con el aviso de una vez del `Dispatcher`.
10. **Crear y borrar.** El binario los tiene (`F3 01` desde `0x63d990` y su respuesta en
    `0x4c2290`; `F3 02` desde `0x643740` y su respuesta en línea, con los avisos `0x92`, `0x60`,
    `0x62` y `0x89`), y la selección sube sus botones, pero el research no tiene sus campos (P-C06)
    y van con dos ventanas de la UI nueva. *Recomendación:* una CL6.5 al final de la ola, con la UI
    del modo 4 (los botones de crear, conectar, borrar y el menú) y los dos paquetes, con su
    contrato escrito después de la CL6.R. Si la ola se alarga, la CL6.5 pasa entera a la CL8 sin
    tocar lo demás: la selección se usa sin ella con una cuenta que ya tiene personajes.
11. **El logo y el personaje del login.** *Recomendación:* una CL6.4 después de la selección, con
    lo que ya sirve para ella (modelos, pose, apariencia, pase 3D): el `0xa2` con su luz que sube
    (`0x604ab0`) y el personaje 2 con su equipo, su mascota y el cuervo como objeto aparte. La
    acción `0x54` o `0x55` al azar de `0x634c60` va con ella. La cámara no: la del login es fija
    (`0x635ae0` en la escena 2, `LoginCamera` de la CL6.3), y las rutas de `0x633920` (en la
    escena 2, una de las 1 a 4 al azar) corren en la lógica, pero el dibujo las pisa. El efecto
    `0xbf` tampoco: es de la ola de los efectos, que el roadmap no tiene todavía (va al cerrar).
12. **La traza como oráculo.** El skinning, el brillo y la cámara se comparan con las trazas de
    GLIntercept, que están en las ramas `pr45` a `pr45d` del lab, comprimidas. *Recomendación:* una
    variable nueva, `MURET_CLIENT_GL_TRACE_DIR`, que apunta a una carpeta fuera de Git con las dos
    trazas descomprimidas (`gunzip -k`, sin zlib en los tests), con la regla de D-C9 (sin la
    variable se saltean; si apunta a algo que no es una carpeta o falta la traza, fallan). Un lector
    del formato en `tests/support/GlTrace.h` (frames por `wglSwapBuffers`, viewports, matrices y
    los bloques de `glBegin` con su textura, sus UV, sus colores y sus vértices). Los tests de la
    traza necesitan también el `Data\` (los modelos): `GoldenAndGlTraceTest`, como
    `GoldenAndWireDataTest`. Ningún número de la traza va a Git más que los que ya están en el 06.
13. **Los slots de las texturas de los modelos.** *Recomendación:* reproducir `0x60d530` y
    `0x60d550` como datos (`ModelTextureSlots`, con el primer slot de cada llamador y la búsqueda
    por nombre), porque las trazas y `lor_slots.bin` nombran slots y así se comparan. Con las
    tablas por escena, el primero de cada escena es el del original: `0x6f8` en la selección, y
    para el login el que deja el mundo 55, que el login carga antes que el logo: `0x41c` más las 57
    texturas nuevas de `Object56\`, `0x455` (*inferido*, Q-C92, P-C15). `0x60d530` no lo llama
    `0x61ccb0` sino cada cargador, con su primer slot (la tabla de 06 § El modelo en el binario), y
    `0x615700` vuelve a `0x138` (Q-C89). Las de la piel y el pelo (`0x12d`, `0x12e`, `0x137`) son
    las de `0x60f7d0`, que la selección sube con las suyas.

## Lo que más puede morder

Cada uno tiene su test en la sub-ola que lo dice. En la review se miran primero:

- **El orden del cuaternión.** El original guarda x, y, z, w; `glm::quat` guarda igual pero su
  constructor de cuatro recibe w primero. Un cuaternión armado con `glm::quat(x, y, z, w)` es otro
  giro y el personaje sale retorcido. Todo cuaternión se arma con `glm::quat::wxyz` o desde
  ángulos (CL6.2, `HalfLifeMathTest.AQuarterTurnAroundZ`).
- **Filas contra columnas.** Las matrices de Half-Life son 3 × 4 por filas (`m[fila][columna]`,
  la traslación en la columna 3); las de glm, por columnas (`m[columna][fila]`, la traslación en
  `m[3]`). Copiar índices sin dar vuelta deja la traslación en la fila de abajo (CL6.2,
  `PoseTest.TheRootCarriesTheObjectTranslation`).
- **Grados y radianes en la misma función.** Los ángulos de las keys del `.bmd` van en radianes, y
  los del objeto y los de la cabeza en grados (por el π/180 en float de `0x6ba360` y `0x6ba140`).
  `glm::rotate` y el fov de `glm::perspective` piden radianes, y `glRotatef` y `gluPerspective`,
  grados (CL6.2 y CL6.3, `Pass3DTest.MatchesTheMatricesOfTheSelectionTrace`).
- **El frame de la traza es uno menos.** El original guarda f + 1 y ⌊f⌋ como anterior, y posa
  entre las dos: los frames de la tabla del 06 (1,740, 3,320…) son los de la pose, y el estado
  del original va una key adelante. El test de la traza arma el estado como el original
  (`GoldenSkinningTrace.MatchesTheFiveFramesOfTheSelection`).
- **El slerp de glm no es el del original** (pregunta 5), y la igualdad de 0,001 de `0x5fc700`
  salta el slerp entero: dos keys casi iguales copian la primera, no interpolan
  (`HalfLifeMathTest.AlmostEqualKeysCopyTheFirst`).
- **Por paso y por tiempo.** El cuerpo avanza por paso y la mascota por tiempo: con un reloj falso
  a 21,33, 60 y 240 FPS, el cuerpo da las mismas keys por segundo, y la mascota también
  (`AnimationTest.ThePetRunsByTimeAtEveryFrameRate`).
- **El fin adelantado solo decide "terminó".** En la selección, el frame + 2 es para el aviso; el
  módulo y la pose usan el real
  (`AnimationTest.TheSelectionEndsTwoKeysEarlyButWrapsOnTheRealFrame`).
- **Las texturas de un modelo no son las de una escena.** El cargador sale de la letra después del
  primer punto (no de la extensión), las OZT van siempre sin filtro, y cuatro prefijos de nombre
  mandan la malla a los slots de la piel y el pelo (CL6.1, `ModelTexturesTest`).
- **El estado de GL entre el 3D y el 2D.** El pase 3D prende el depth test, el culling y escribe
  la profundidad; el 2D que sigue los apaga, como `0x6219b0` → `0x620870`, o los fondos del frame
  siguiente salen tapados (nota de la CL5.2; CL6.3,
  `GlModelRendererTest.TheTwoDAfterTurnsTheDepthOff`). La mezcla aditiva escribe sin profundidad
  (`glDepthMask(false)`, 06).
- **La profundidad es de 16 bits.** El original pide `cDepthBits` 16 (`0x4b8e10`), lo mismo que
  SDL3 da por defecto, y `Platform/Window.cpp` hoy no la pide. La CL6.3 la pide explícita
  (`SDL_GL_DEPTH_SIZE` en 16) y el framebuffer de los tests usa `GL_DEPTH_COMPONENT16`: así los
  tests de GL y la ventana tienen la misma precisión. GL 3.3 arranca con `GL_LESS`, `GL_BACK` y
  `GL_CCW`, y el original no llama a `glCullFace` ni a `glFrontFace` (ni el binario ni la traza):
  el pase pone `GL_LEQUAL` y deja los otros dos como vienen.
- **El color por vértice pasa de 1.** La luz del original llega a 1,34 y la función fija la recorta
  a 1 antes de rasterizar: el shader recorta igual (`GlModelRendererTest.ClampsTheColorToOne`).
- **El aspect es el del viewport.** `gluPerspective` va con el ancho y el alto del viewport
  (1024/688 en la selección), no con los de la ventana, y el far se multiplica por 1,4
  (`0x6b8c00`) antes (CL6.3, `Pass3DTest.TheAspectIsThatOfTheViewport`).
- **MSVC con `/W4 /WX`.** `near` y `far` son macros de Windows (llegan con `winsock2.h` a
  `ClientLib`): los campos se llaman `nearPlane` y `farPlane`. Las constantes double del original
  (0,5, 1e-8, 0,001) en una cuenta con floats dan C4244 sin un `static_cast<float>` explícito; un
  índice de hueso `std::size_t` a `std::int16_t` es C4267; y un literal entero por un template a un
  tipo más chico (`std::optional<std::uint8_t>{0x1D}`) es C4242. `min` y `max`, con paréntesis.
- **El nombre del personaje.** Se dibuja (es del jugador), pero no va al log ni a los tests: el
  original loguea `> Character selected <%d> %s` con el nombre, y MuRet loguea el lugar. Los tests
  usan nombres sintéticos y `CharacterEntry` no tiene `operator<<`; su `PrintTo` escribe el
  largo, como `Credentials` (CL6.3).
- **Lo del anticheat no se lee.** Las llamadas de hash de la selección (`0x4e79a0`, `0x40f370`,
  `0x40b240`) y la base al azar de la tabla de modelos se nombran y no se reproducen.
- **Ningún modelo fijo en el código.** Los modelos de cada escena, del personaje y del equipo
  salen de tablas con su dirección del original; los tests golden van por todos los `.bmd` del
  laboratorio y por cada fila de cada tabla, como `LabWorlds` en la CL1.

---

## Sub-ola CL6.0: huesos y cuaterniones (N.0, en `labs/`)

**Concepto:**
- rotaciones en 3D: ángulos de Euler, el orden en que se componen (Rz · Ry · Rx en Half-Life),
  por qué interpolar ángulos da otro giro que el del medio, y el gimbal lock;
- cuaterniones: qué son (un eje y medio ángulo), cómo se arman desde ángulos, cómo dan una matriz,
  por qué q y −q son el mismo giro, y slerp contra lerp normalizado;
- una jerarquía de huesos: cada hueso relativo a su padre, la matriz del mundo como producto de la
  cadena, y por qué el padre tiene que estar calculado antes que el hijo;
- skinning rígido: un vértice guardado en el espacio de su hueso y llevado al mundo con la matriz
  de ese hueso; la normal, solo con la rotación;
- matrices 3 × 4 por filas (Half-Life) contra matrices por columnas (glm, GL), y el frame
  fraccionario: entre qué dos keys se interpola y con qué peso.

Del C++: `glm::quat`, `glm::mat4x3` y sus conversiones (`glm::mat4(m)` suma la fila (0, 0, 0, 1)),
`glm::quat::wxyz` para que el orden se lea, y comparar floats con tolerancia (`EXPECT_NEAR`, y por
qué no `EXPECT_FLOAT_EQ` después de una cadena de productos).

**Archivos:**
- Crear: `docs/learning/ola-cl6.0-huesos-y-cuaterniones.md` (la guía; Claude)
- Crear: `labs/cl6.0-huesos/Quaternion.h/.cpp` (`AngleQuaternion` y `QuaternionMatrix` a mano,
  con un `struct Quat { float x, y, z, w; }` propio), `Slerp.h/.cpp` (el `QuaternionSlerp` de
  Half-Life a mano), `Bones.h/.cpp` (una cadena de huesos y el skinning de un vértice), sus tests
  (`test_quaternion.cpp`, `test_slerp.cpp`, `test_bones.cpp`), `README.md` y
  `jefe/test_original_pose.cpp` (Emanuel)
- Modificar: `labs/CMakeLists.txt` (la biblioteca `MuRetLabsCl60` y el ejecutable `LabCl60Tests`
  con `gtest_discover_tests(... TEST_PREFIX "LabCl60.")`, enlazado con `glm::glm-header-only`
  como `LabCl50Tests`), `labs/README.md`

- [x] **Paso 1 (Claude):** la guía, justo antes de empezar.
- [x] **Paso 2 (Emanuel, `TODO(human)`; lo resolvió Claude):**
  `Labs::AngleQuaternion(std::array<float, 3> radians) -> Quat` y `Labs::QuaternionMatrix(Quat) ->
  std::array<std::array<float, 4>, 3>` (la 3 × 4 por filas, con la traslación en 0). Primero los
  tests: `QuaternionTest.AQuarterTurnAroundZ` ((0, 0, π/2) da (0, 0, √½, √½)),
  `MatchesGlmForAnyAngles` (una grilla de ángulos de −π a π contra `glm::quat(glm::vec3)`,
  componente a componente), `TheMatrixIsTheSameRotation` (contra `glm::mat3_cast`, dando vuelta
  filas y columnas), `QAndMinusQAreTheSameMatrix`. Con `GlmTakesTheWFirst`, el orden del constructor
  de cuatro de `glm::quat`.
- [x] **Paso 3 (Emanuel; lo resolvió Claude):** `Labs::HalfLifeSlerp(Quat p, Quat q, float t) ->
  Quat`, como `0x5fce90`: la vuelta si Σ(p − q)² > Σ(p + q)², lineal si 1 − cos ≤ 1e-8 (en double,
  con el cos ya redondeado a float) y el caso perpendicular. Tests:
  `SlerpTest.HalfWayIsTheMiddleRotation`, `TheFlipIsGlmsForUnitQuaternions` (para unitarios,
  Σ(p − q)² > Σ(p + q)² es el producto negativo: mismo resultado que `glm::slerp` con tolerancia),
  `GoesLinearOnlyBelowOneInAHundredMillion` (con el cos en `std::nextafter(1.0f, 0.0f)`, o sea
  1 − cos = 2⁻²⁴, unos 6e-8: Half-Life usa senos y glm ya es lineal; con el cos en 1, los dos son
  lineales; se ve qué rama corre con `Labs::SlerpBranch(p, q)`),
  `ThePerpendicularCaseNeverRunsAfterTheFlip`. Con `SlerpBranchForCosine` (la primera comparación ve
  el cos de la pila, la segunda el redondeado: `EachComparisonSeesItsOwnCosine`),
  `ANanOrAnInfinityReachesTheOppositeBranch` y la igualdad de `0x5fc700` (`AlmostEqual`).
- [x] **Paso 4 (Emanuel; lo resolvió Claude):** `Labs::PoseChain(std::span<const Labs::Bone>, const
  Matrix34& object) -> std::optional<std::vector<Matrix34>>` (cada hueso con su padre, su posición y
  su giro; `nullopt` si un padre no está antes) y `Labs::Transform(const Matrix34&,
  std::array<float, 3>)`. Tests: `BonesTest.AChildFollowsItsParent` (un brazo de tres huesos con el
  hombro girado 90°: la mano cae donde se calcula a mano), `TheRootHangsFromTheObject` (la raíz con
  la matriz de un objeto en (100, 0, 0)), `RowsAgainstColumns` (la misma matriz en `glm::mat4x3`:
  `m[3]` es la traslación, y en la 3 × 4 es la columna 3), `TheNormalOnlyRotates`,
  `AParentAfterItsChildIsRefused` y `AngleMatrixIsTheQuaternionRotation` (`0x5fc8f0`, en grados).
- [x] **Paso 5 (Emanuel, jefe; lo resolvió Claude):**
  `OriginalPoseTest.SlerpBeatsEulerBetweenTwoKeys`: un hueso con dos keys de ángulos (0, 0, 170°) y
  (0, 0, −170°): a mitad de camino, el lerp de los ángulos da 0° (la vuelta larga) y el slerp da
  180°; y `TheOriginalStoresTheNextKey`: con el frame f de la tabla del 06, el original guarda f + 1
  y ⌊f⌋ como anterior (06 § Cuánto avanza la animación), y las dos maneras dan la misma pose. Con
  `NlerpHidesBelowTheTraceErrorAndEulerDoesNot` (los números del paso 6) y
  `TheJumpsAreStepsOfTheAction` (33, 33, 34 y 34 pasos de 0,26).
- [x] **Paso 6 (Emanuel, lo anota en `labs/README.md`; lo resolvió Claude):** por qué 05 dice que
  slerp y nlerp no se distinguen en la traza y el lerp de ángulos sí, con los números del paso 5.
- [x] **Paso 7:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "LabCl60"
  ```

## Sub-ola CL6.R: el render de modelos y el flujo `F3`, en el binario (P-C01, P-C06)

**Concepto:** carril R antes de escribir el código. Las tablas de arriba salieron del decompilado
para este plan; esta sub-ola las confirma en el desensamblado, cierra lo que la CL6 necesita y lo
pasa al research. Lo que solo se ve con una traza o una captura queda como pendiente, con cómo se
cierra. Nada del anticheat: las llamadas de hash de la selección (`0x4e79a0`, `0x40f370`,
`0x40b240`) se nombran y no se leen.

**Archivos:** modificar `Client/docs/research/05-formatos-de-datos.md` (§ Modelos bmd: el lector, la
versión, el nombre de cada archivo, los faltantes), `06-render-escenas-y-ui.md` (§ Modelos y
objetos, § Cuánto avanza la animación con la tabla entera de velocidades, y una sección nueva, "La
selección de personaje"), `04-protocolo.md` (los `F3` campo por campo),
`02-arranque-y-ciclo-de-vida.md` (la escena 3 y el `F3 03` de la 5), `07-quirks-y-bugs.md` (Q-C23
corregido, Q-C29 con lo que dibuja la CL6 y los quirks nuevos, con número), `08-pendientes.md`
(P-C01 y P-C06 cerrados o con lo que falte), `09-datos-disponibles.md` (`0x4f1ac0` y `Monster50`),
PROGRESS y este plan.

Cada paso se lee con `objdump -d -M intel --start-address=A --stop-address=B` sobre el
`main.exe` de `c32bbd5f`, con el decompilado al lado; los rangos van en cada paso.

- [x] **Paso 1 (el archivo):** `0x60da30`..`0x60dbdd`, `0x4f13d0`..`0x4f1aba` y
  `0x4f1ac0`..`0x4f1f1c`: los tres formatos del nombre, el modo de `DAT_006c9e0c` (vale 1 en la
  `.data` y `0x60dbe0` lo vuelve a poner en 1 después de `Player`: si algo lo pone en 0), qué hace
  el lector con una versión que no es `0x0C`, qué copia de cada parte (los 36 de 64 bytes del
  triángulo, los 12 bytes por key de `lock_positions`, la normal de 20 bytes), el script de la
  textura de cada malla (`0x459940` y `0x459910`: qué prende cada letra, y qué nombres del
  laboratorio lo traen), y que no escribe `+4` en ninguna acción (el arreglo sale de
  `operator new` sin limpiar: quién usa la velocidad de una acción sin escritura de `0x60dbe0`).
  `0x4f1f20` y `0x4f1f80`: el hueso de la cabeza en -1 y las cajas de los huesos (las que usa el
  picking, CL6.3). Q-C23 se corrige: el cargador es `0x4f13d0`, y los tres nombres que cierran el
  juego; 09, con `Monster50`.
- [x] **Paso 2 (el personaje):** `0x60dbe0`..`0x60f76f` entero: cada modelo que carga, con su
  número, su carpeta, su nombre y su número de archivo (la tabla que prueba `PlayerModelFiles`), las
  181 escrituras de velocidad en orden (la tabla de `PlayerActionSpeeds`), las cuatro que no
  repiten, los tres huesos de `+0x54`, `+0x58` y `+0x5c` (la cabeza en 20; el 6 y el 13, los pies,
  *inferido*) y el error de `Player.bmd` con mallas. Quién carga las armas, las alas y las mascotas
  que lleva el personaje (`0x60ff50`..`0x61184c`, y en qué avance de `0x61ccb0`..`0x61ee22`), y el
  primer slot de textura que fija `0x60d530` antes de cada cargador de `0x61ccb0`.
- [x] **Paso 3 (las texturas de un modelo):** `0x60d530`..`0x60d547`, `0x60d550`..`0x60d5e1` y
  `0x60d5f0`..`0x60d82c`: si la búsqueda por nombre distingue mayúsculas, si el slot que se cargó
  para un nombre `ski`, `level`, `hid` o `hair` queda ocupado, qué es el quinto argumento, y qué
  archivo abren `0x63b870` y `0x63bd00` con un nombre que no termina en `.jpg` ni en `.tga` (el
  golden de la CL6.1 dice si hay alguno en el laboratorio). `0x60f7d0`..`0x60ff4e`: qué textura de
  piel y de pelo sube para cada clase, en `0x12d` a `0x137`. A qué slot va `Logo03` en el login
  (quirk nuevo).
- [x] **Paso 4 (la pose, P-C01 a y d):** `0x4eb860`..`0x4ebc76` entero: los dos argumentos de
  bandera (`param_8` y `param_9`: cuándo la raíz no toma los ángulos del objeto y cuándo no toma la
  escala y la posición), quién escribe `+0x84` (la z que suma la raíz con `lock_positions`), que los
  huesos dummy no se posan (se queda lo que había), y la cabeza: el desensamblado resta el `+0x28`
  del objeto al ángulo x y el `+0x2c` al **z** (`0x4eb9ff`..`0x4eba1d`), no al y como dice 06. El
  `lock_positions` se mira en el hueso 0, con la acción anterior o con la de ahora. `0x4ebc80`..
  `0x4ec100`: `param_6` (la escala `+0x68` y la posición `+0x6c` del modelo en cada vértice, en vez
  de en la raíz) y `DAT_006c881c`, una escala de los vértices que escribe `0x5ffdf0` (1, 1,4, 1,2 o
  la `+0x94` del objeto; con otra que 1, `VectorRotate` por la escala más la traslación del
  hueso): en qué dibujos no es 1. (a): quién escribe `+0x28` y `+0x2c` en los personajes
  (`0x4fd5f0` y lo que llama). (d): si algo lee los 56 huesos de las partes, y qué hace
  `lock_positions` con sus 12 bytes por key.
- [x] **Paso 5 (el avance, P-C01 c):** `0x4ec200`..`0x4ec340` con sus seis argumentos (`ret 0x18`)
  y sus llamadores en la escena 4: qué hacen con el "terminó" adelantado (Q-C21: si cambia algo en
  pantalla, o si es para la UI); `0x4ea030` (cambiar de acción) y qué acción pone a cada personaje
  de la selección (la 3 en la traza). `0x4f9580` se nombra para la CL7: sus multiplicadores son
  del juego.
- [x] **Paso 6 (el dibujo de una malla, P-C01 b):** `0x4ed890`..`0x4ee769`: las banderas del
  dibujo (`0x1`, `0x2`, `0x4`, `0x8`, `0x40`, `0x80`, `0x2000` y los modos de brillo `0x200`,
  `0x800`, `0x1000`, `0x4000` y `0x10000`), qué mezcla pone cada una (`0x620980` opaco,
  `0x620a00(1)` normal con alpha test, `0x620a90` aditiva, `0x620b10` y `0x620c90`; y
  `0x620870`, sin profundidad), que una textura de cuatro componentes (el byte `+0x28` del slot en
  4) va con la mezcla normal, que una malla con la textura `0x12d` no se dibuja, los slots de los
  pases de brillo (`0x5c0`, `0x620` y `0x5fc`), el color de la malla (`+0x48`..`+0x50` del
  modelo: quién lo escribe en la selección), qué banderas del script de la textura mira (`+0x24`
  de la malla, paso 1) y qué dibuja una malla cuyo slot no tiene textura (Q-C29: el slot queda
  como estaba). `0x509340`..`0x50b808` y `0x50ba30`..`0x510ad4`: de
  qué hueso cuelgan el arma, el escudo, las alas y la mascota, y de qué depende cuántos pases de
  brillo lleva cada ítem (b: ¿el nivel?).
- [x] **Paso 7 (la selección):** `0x633f20`..`0x6342b2` (el objeto `0xa9` de `0x511290`, la UI en
  el modo 4 de `0x65f4b0`, el texto 1240), `0x6342c0`..`0x63467d` (el doble clic: un
  `WM_LBUTTONDBLCLK` o un reloj propio, y la bandera `+0x7f5c` de la UI que pide; los textos del
  aviso `0x84`; qué se copia al personaje propio), `0x59e700`..`0x59eb70` y `0x622900`..`0x622a5e`
  (el rayo y la caja de cada personaje, con lo que guardan `0x6203c0` y `0x620370`),
  `0x634680`..`0x63499d` (la luz `+0xf4` de cada personaje, en 0 y la del elegido en 1 y después en
  0,15, y cómo entra en el color de `0x4ebc80`; la luz `0x5f9780`, la de abajo del elegido
  `0x53fe70(0x729, …)`, el nombre `0x4129f0` y la bandera `DAT_07d36899`, que saca la rotación x de
  `0x620db0`), `0x4c20a0`..`0x4c226e` (el nivel, el `+0x368`), `0x5139f0`..`0x5140b4` (los 17 bytes
  del equipo, byte a byte: qué ítem y qué nivel da cada uno) y `0x517270`..`0x5173f8` (de la clase a
  las partes). `0x6356f0`..`0x635acb` con `0x640c10` y `0x640ea0`: qué imagen de las cuatro dibuja
  la carga del mundo, y dónde.
- [x] **Paso 8 (el flujo `F3`, P-C06):** los emisores y los handlers de `0x4e2f93` contra la sesión
  real de 04: el `F3 00` (`0x634e0e`, en `0x634c60`..`0x6350a4`), la lista (`0x4c20a0`), el `F3 01`
  (`0x63d990`..`0x63e09c`) y su respuesta (`0x4c2290`..`0x4c240a`), el `F3 02` (`0x643740`..
  `0x643e26`) y su respuesta en línea con los avisos `0x92`, `0x60`, `0x62` y `0x89`, el `F3 03`
  (`0x636290`..`0x637194`) y su respuesta (`0x4c3940`..`0x4c4b47`, para la CL7). La forma de cada
  uno contra el cable se compara sin escribir valores: el largo, el tipo y el subcódigo.
- [x] **Paso 9 (el login de la CL6.4):** en `0x6349a0`..`0x634c52`, `0x61c7c0`..`0x61c9e4` (el
  `Logo03`) y `0x606660`..`0x606dac` (el objeto); `0x517270(2, 4, …)` con las partes y el equipo; el
  cuervo (`0x420d60`..`0x420dc9`); la luz del logo (`0x604ab0`..`0x605ab3`); la cámara fija de la
  escena 2 en `0x635ae0`..`0x636259`, y que las rutas de `0x633920`..`0x633b8f` no llegan a GL (si
  algo más lee lo que dejan); y la acción `0x54` o `0x55` de `0x634c60`, con su ventana del reloj
  del frame (`DAT_05767b88`, el de `0x4eb620`). El efecto `0xbf` (`0x443fa0`) se nombra y no se lee:
  es de la ola de los efectos.
- [x] **Paso 10:** pasar todo al research con *estático*, *inferido* o *verificado* en cada cosa, y
  corregir 06 (la cabeza en x y en z), 07 (Q-C23), 09 (`0x4f1ac0` y `Monster50`) y la pregunta 6 del
  plan de la CL5 (el slerp es `0x5fce90`). Los quirks nuevos van a 07 con su número: `Player.bmd`
  con mallas cierra el juego, `Logo03` va donde dejó el próximo slot la última carga, un lugar de
  más de 4 en la lista deja la posición del anterior, y lo que salga de los pasos 1 a 9. P-C01 y
  P-C06 se cierran en 08 o quedan con lo que falte. Corregir las tablas de este plan con lo que haya
  cambiado y escribir "Lo que la CL6.R deja para las sub-olas que siguen", como la CL5.R.

**Lo que se verifica:** cada dirección de las tablas de este plan y de cada contrato es la función
que dice el research (*verificado* el 2026-11-03 con `objdump` al escribir el plan, una por una).
La CL6.R lo vuelve a mirar al leer cada una entera.

Hecho así (2026-11-03): dos lectores en paralelo, los pasos 1 a 5 y 6 a 9, todo en el
desensamblado de `c32bbd5f` con `objdump` y el decompilado solo como mapa; donde los dos tocaban lo
mismo (la bandera `+0` de las cinco mallas de ítems, el slot de `Logo03`), se volvió a mirar en el
asm y coinciden. Casi todo es *estático*. Contra datos reales quedaron **verificados** el lector de
`.bmd` (un script que lee como `0x4f13d0` recorre los 1.962 modelos sin bytes de sobra), el
asignador de los slots de textura (simulado con los modelos del laboratorio, da los nombres de los
750 slots de `0x12e` a `0x41b` de `lor_slots.bin`, uno por uno) y el orden de los pases de brillo
(la traza de la selección). Pasó al research: 05 (§ Modelos bmd: el lector, las versiones, la
normal, el triángulo y el script), 06 (§ Modelos y objetos, con la cabeza en x y en z, la tabla
entera de velocidades y § El modelo en el binario; la cámara fija; el logo y el cuervo del login;
y la sección nueva, § La selección de personaje), 04 (los `F3` 0 a 3, campo por campo), 02 (las
transiciones de la 4 a la 3 y de la 3 a la 5), 07, 08 y 09. Lo que cambió respecto de las tablas
de arriba quedó corregido en ellas. Lo que no estaba:

- el lector es `0x4f13d0` y `0x4f1ac0` escribe; `Monster50` cierra el original (Q-C23), y
  `Player.bmd` con mallas también (Q-C88); hay cuatro modelos de versión `0x00`;
- la función que dibuja una malla es `0x4ecc30`, y `0x4ed890` es código muerto; el pase 115 es la
  bandera `0x8`; cuántos pases lleva una parte lo decide su nivel (`0x60b3d0`), y cinco mallas de
  ítems no brillan nunca;
- `0x60d530` lo llaman los cargadores, no `0x61ccb0`; la búsqueda es por el nombre desnudo
  (Q-C90), y `Logo03` va detrás del mundo 55 (Q-C92);
- la cabeza la gira `0x505de0`, y el aviso adelantado de la selección no cambia nada en pantalla
  (Q-C21);
- el `+0xf4` entra por `0x50ba30` (0,4 y 1,4); el picking es un segmento contra una caja corrida,
  con 6 ejes de separación (Q-C100); el doble clic es un reloj propio (Q-C99); el `0xa9` es la
  vista previa de la ventana de crear;
- la carga del mundo dibuja las cuatro `LSBg` en mosaico; el logo va con escala 2,2, el
  `blendMesh` en 1 y sin luz por vértice, y su luz salta de 0,78 a 1,5 (Q-C105).

Los quirks nuevos van de Q-C86 a Q-C105 (Q-C83 a Q-C85 son de otros PRs), y cambiaron Q-C21,
Q-C23, Q-C29 y Q-C74. P-C01 y P-C06 quedaron cerrados; lo que pide una captura está en P-C14 (la
selección), P-C15 (el login) y P-C16 (crear y borrar). Sin leer, por ser del anticheat o de la
protección: los hashes de la selección y de la carga del mundo, el tramo `0x505ef8`.. de la cabeza
y la tabla de hash de `0x4ed890`. La pregunta 6 del plan de la CL5 quedó corregida (el slerp es
`0x5fce90`).

**Lo que la CL6.R deja para las sub-olas que siguen:**

- **CL6.1:** el contrato con los offsets corregidos (la versión en `+0x20`; huesos, mallas y
  acciones en `+0x22`, `+0x24` y `+0x26`, aunque MuRet no guarda la estructura de `0xe0`). La
  normal trae en sus últimos 4 bytes un índice de vértice (qué es, *inferido*: el golden mira que
  entre en su malla), y del triángulo, `+0x1a..+0x1f` no se usan (los pisa la adyacencia).
  `AnyVersionButTwelveIsClear` suma la `0x00`, y el golden cuenta 816, 1.142 y 4. El script de la
  textura, `TextureScriptOf(name)`: los caracteres 1 a 4 después del primer `_`, `R`, `H`, `S` y
  `N` en mayúscula (142 mallas en el laboratorio, ninguna `N`). `PlayerModelFiles`, con las 213
  filas de `0x60dbe0` (`Shadow` incluido), e `ItemModelFiles`, con las 243 de `0x60ff50` (faltan
  los tres `giftbox`, que se saltean). Quirks que fija: Q-C23
  (`ModelFilesTest.OnlyMonsterPlayerAndPlayerTestAreRequired`), Q-C86
  (`BmdTest.AnEncryptedSizeBeyondTheFileIsTooShort`), Q-C87
  (`PlayerActionsTest.FiftySevenHaveNone`), Q-C88 (`ModelFilesTest.TheSkeletonWithMeshesIsAnError`)
  y Q-C90
  (`ModelTextureSlotsTest.ANameIsFoundInAnyFolder` y `TheSearchIsCaseSensitive`, con
  `TheSearchStopsAtNext` y `ASkinNameStillTakesASlot`). Pendiente: el valor de las 57 velocidades
  sin escritura (P-C14 a).
- **CL6.2:** la pose toma del modelo la escala, la posición y los ángulos (`+0x68`, `+0x6c` y
  `+0x78`), con `+0x84` en 0 en los personajes; sus dos banderas son "la raíz la dejó quien llama"
  (las armas) y "la escala y la posición en la raíz o en cada vértice". `SetAction` guarda el frame
  sin truncar. El brillo es de `0x4ecc30`, `ShinePass::Shiny` es la `0x8`, y `ShineTest` prueba
  las tres filas del 06 que tiene `ShinePass` con un T fijo (las otras cinco entran con quien las
  pida: `0x200` y `0x800`, en la CL6.3). Las partes se
  pielan con la pose del cuerpo (`SkinningTest.APartUsesTheBodyPose`). Quirks: Q-C21
  (`AnimationTest.TheSelectionEndsTwoKeysEarlyButWrapsOnTheRealFrame`, que ya estaba), Q-C87
  (`AnimationTest.AnActionWithoutSpeedStays`), y Q-C93 y Q-C96 sin test (MuRet no tiene ese campo
  ni ese buffer). Pendiente: si la selección gira la cabeza (P-C14 b); hasta la captura, los
  ángulos de la cabeza en 0 en la selección, como muestra la traza.
- **CL6.3:** el primer slot de cada cargador, de la tabla del 06 (`0x12e`, `0x137`, `0x138`,
  `0x25a`, `0x69`, `0x3b8` y `0x6f8`), con el golden local
  `ModelTextureSlotsGoldenTest.StartupMatchesTheDump` (la carga de arranque de MuRet contra
  `lor_slots.bin` de `0x12e` a `0x41b`, fuera de CI como `GoldenAndGlTraceTest`); la piel por clase,
  `SkinIndexOf(b)` = 2 · (b & 7) + (b >> 3). El dibujo de una malla con la tabla de banderas del 06
  (`ModelDrawState`), los pases por nivel (`ItemGlow`, con el tope de `DAT_006c9dcc` y el nivel de
  la lista por `0x513340`: los 3 bits 0 a 7 dan 0, 3, 5, 7, 9, 11, 12 y 13; los niveles 11 y 12
  y el `0x840` suman a `ShinePass` la `0x200` y la `0x800`, con su fila del 06 en
  `ShineTest.EachModeHasItsFormula`), lo que
  cuelga (los huesos 33, 42, 47 y 34), la luz 0,4 y 1,4, el picking con el segmento y la caja
  corrida, la apariencia de `0x517270` y `0x5139f0` campo por campo (06, con la escala por clase y
  piel) y la carga del mundo con el mosaico. El doble clic: el `clicks` de SDL tolera un
  movimiento chico y el original no (Q-C99), así que la CL6.3 lo arma como `0x640940`, el segundo
  apretón dentro del tiempo de doble clic y sin movimiento entre los dos, y el `clicks` del
  `InputEvent` deja de hacer falta. Quirks: Q-C29 (`ModelBatchTest.AnEmptySlotDrawsUntextured`),
  Q-C89 (el golden de los slots), Q-C91 (`GoldenGameModels.TheSelectionTexturesEndAt0x716`), Q-C94
  (`HoverOutlineTest.TheSecondPassClampsAt101`), Q-C95
  (`ModelDrawStateTest.AFadingShineDarkensTheNextMeshes`), Q-C97 (`ItemGlowTest.LevelSetsThePasses`
  y `DetailCapsTheLevel`), Q-C98 (`AttachmentTest.PetIsBone34OnlyFor0x1c13`, con
  `PlayerHandsAreBones33And42` y `WingsAreBone47`), Q-C99
  (`SelectInputTest.MovingBetweenClicksIsNotADoubleClick`, con `EnterNeedsAChosenSlot` y
  `DoubleClickNeedsTheSelectWindow`), Q-C100 (`RayPickingTest.ThePlayerBoxIsOffCentre` y
  `TheNearestIsByOriginDistance`), Q-C101 (`CharacterListTest.ASlotAboveFourIsDropped`) y Q-C102
  (`CharacterAppearanceTest.AnEmptyHandIsNoItem`). Más: `SelectLightTest.ChosenIsBrighter`,
  `WorldLoadingSceneTest.DrawsTheFourTiles` y `CharacterAppearanceTest.TheScaleGoesByClassAndSkin`.
  Pendiente (P-C14): la acción de parado sin arma y con una de dos manos (hasta la captura, la de
  `0x4f2250` con lo leído, y la 3 con un arma de una mano), el color de `0x609640` (hasta leerlo,
  el pase base con el factor del nivel y sin ese caso), una mano vacía y `0x640850`.
- **CL6.4:** el logo con escala 2,2, el `blendMesh` en 1 y sin luz por vértice
  (`LoginModelsTest.TheLogoIsScaled2_2WithBlendMesh1`); sus texturas desde `LoginLogoFirstSlot`
  (Q-C92: `TheLogoTexturesStartWhereTheWorldLeft` y `LeaveReleasesTheLogo`); la luz de `0x604ab0`
  (Q-C105: `TheLogoLightJumpsWhenThePhaseChanges`), con la cuenta y la fase que la escena lleva
  como `0x633920`; el personaje y su equipo, como estaban; el cuervo se nombra y no se dibuja.
  Pendiente (P-C15): el slot de `Logo03` y la subida de la luz, con una captura del login (la
  memoria de los slots `0x450` a `0x460` y `DAT_07d366b8`, o la traza de P-C13), y la conducta
  del cuervo.
- **CL6.5:** el contrato de su sección, con Q-C103 (`CharacterCreateTest.TheNewOneHasNoEquipment`)
  y Q-C104 (`ServerSessionTest.DeleteRemovesTheSlotItSent`). Pendiente (P-C16): el `F3 01` y el
  `F3 02` contra el cable, y si la elección puede cambiar con el aviso del borrado abierto.

## Sub-ola CL6.1: parseo de modelos (`.bmd`)

**Concepto:** el `.bmd` de un modelo como datos, sin matemática: un formato con conteos que dicen
cuánto leer de cada parte, cifrado o en claro según un byte, y los floats con los bits del
archivo. Y de un modelo al archivo que lo trae y a las texturas que pide. Va en dos PRs: **PR A**,
el contenedor (`LoadBmd`, el nombre de cada archivo, los faltantes); **PR B**, el catálogo del
personaje, las velocidades de sus acciones y los slots de las texturas, apilado sobre el A.

**Contrato** (05 § Modelos bmd; la tabla de arriba; lo que diga la CL6.R):

| Qué | Original | MuRet |
| --- | --- | --- |
| el nombre | `0x60da30`: `"%s.bmd"` con -1, `"%s0%d.bmd"` con menos de 10, `"%s%d.bmd"` con los demás | `ModelFileName(name, number)` |
| leer | `0x4f13d0`, el archivo entero con `"rb"` | `ReadDataFile` (CL1) y `LoadBmd(bytes)` |
| la versión | el byte 3 (a `+0x20`): con `0x0C`, el tamaño en `+4` y K16 desde `+8`; con cualquier otra, en claro desde `+4` (816 `0x0C`, 1.142 `0x0A` y 4 `0x00` en el laboratorio) | igual, con `DecryptK16` (CL1.3); tampoco mira `"BMD"` |
| los tamaños | no los mira: lee de más (Q-C86) | `DataError::FileTooShort` (diferencia nombrada: el original sigue con memoria de otra cosa) |
| un triángulo | copia los primeros 36 de sus 64 bytes, y `+0x1a..+0x1f` los pisa la adyacencia que calculan `0x4f0870` y `0x4f0910` | `Triangle` con el tipo y los tres juegos de cuatro índices; los otros 28 bytes, sin leer, y la adyacencia, no (nada de la CL6 la usa) |
| el cuaternión de cada key | `0x5fcd20` al leer | no en Data: `MakeSkeleton` (CL6.2) |
| un modelo que falta | se saltea sin avisar, salvo `Monster`, `Player` y `PlayerTest`, que cierran el juego | `ModelLoadErrorKind::Missing`; quien carga decide con `IsRequiredModel` (pregunta 8) |
| `Player.bmd` con mallas | `"Player.bmd file error!!!"` y `WM_DESTROY` (Q-C88) | `ModelLoadErrorKind::SkeletonWithMeshes` |
| el catálogo del personaje | `0x60dbe0`: `Player.bmd` en `0x20a`, las partes de clase, las del equipo | `PlayerModelFiles()`, en el orden del original (PR B) |
| el número de un ítem | `0x60ff50`: `0x212` + grupo · 512 + índice | `ItemModel(group, index)` (PR B) |
| la velocidad | 181 escrituras en `+4` de las acciones, las de después pisan; 57 quedan con lo del heap (Q-C87) | `PlayerActionSpeeds` y `PlayerActionSpeed(action)`, con `std::nullopt` en esas 57 (PR B) |
| las texturas de un modelo | `0x60d530`, `0x60d550` y `0x60d5f0`: la búsqueda por el nombre desnudo (Q-C90) | `Render::Models::ModelTextureSlots` (PR B, pregunta 13) |

**Archivos:**
- **PR A:** crear `Client/src/Data/Models/Model.h` (los tipos), `Bmd.h/.cpp` (`LoadBmd`),
  `ModelFiles.h/.cpp` (el nombre, el error y `LoadModelFile`), `Client/tests/data/test_bmd.cpp`,
  `test_model_files.cpp`, `test_golden_bmd.cpp` y `Client/tests/support/SyntheticModels.h` (un
  `.bmd` sintético armado en memoria, en claro o cifrado con `EncryptK16` de `TerrainFiles.h`).
  Modificar `Client/CMakeLists.txt` (los fuentes nuevos en `ClientLib`),
  `Client/tests/data/CMakeLists.txt`, PROGRESS y este plan.
- **PR B:** crear `Client/src/Data/Models/PlayerModels.h/.cpp`, `PlayerActions.h/.cpp`,
  `ModelTextures.h/.cpp`, `Client/src/Render/Models/ModelTextureSlots.h/.cpp`,
  `Client/tests/data/test_player_models.cpp`, `test_player_actions.cpp`, `test_model_textures.cpp`,
  `test_golden_model_files.cpp` y `Client/tests/render/test_model_texture_slots.cpp`. Modificar
  los `CMakeLists.txt`, PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Data::Models
{
// 05 § Modelos bmd. Cada float con los bits del archivo, aunque sea un NaN (como el .obj, CL1.3).
struct Vertex
{
    std::int16_t bone = 0; // en los huesos de Player.bmd, en las partes del personaje (05)
    std::array<float, 3> position{}; // en el espacio de ese hueso
};
struct Normal // 20 B: el hueso, relleno, la normal, un índice de vértice y relleno (CL6.R)
{
    std::int16_t bone = 0;
    std::array<float, 3> normal{};
    std::int16_t vertex = 0; // el int16 de +16: un vértice de la malla (*inferido*)
};
struct TexCoord
{
    float u = 0.0f;
    float v = 0.0f;
};
struct Triangle // los 36 bytes que copia 0x4f13d0
{
    std::uint8_t polygon = 3; // 3 en todas las mallas del personaje
    std::array<std::int16_t, 4> vertices{};
    std::array<std::int16_t, 4> normals{};
    std::array<std::int16_t, 4> texCoords{};
};
struct Mesh
{
    std::int16_t texture = 0; // el int16 del archivo; el slot lo da ModelTextureSlots
    std::vector<Vertex> vertices;
    std::vector<Normal> normals;
    std::vector<TexCoord> texCoords;
    std::vector<Triangle> triangles;
    std::string textureName; // los 32 bytes hasta el primer 0
};
struct Action
{
    std::int16_t keyCount = 0;
    bool lockPositions = false;
    std::vector<std::array<float, 3>> lockedPositions; // 12 B por key, con lockPositions
};
struct BoneKeys // de una acción: n_keys posiciones y después n_keys rotaciones (radianes)
{
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> rotations;
};
struct Bone
{
    bool dummy = false; // sin nombre, sin padre y sin keys
    std::string name;
    std::int16_t parent = -1;
    std::vector<BoneKeys> actions; // una por acción del modelo
};
struct Model
{
    std::uint8_t version = 0; // el byte 3
    std::string name;
    std::vector<Mesh> meshes;
    std::vector<Action> actions;
    std::vector<Bone> bones;
    std::size_t trailingBytes = 0; // después del último hueso: el original no los mira
};

// La versión que va cifrada con K16 (0x4f13d0); cualquier otra va en claro.
inline constexpr std::uint8_t EncryptedBmdVersion = 0x0C;
// Bytes → Model, como 0x4f13d0, sin mirar "BMD". FileTooShort si una parte no entra (también el
// tamaño cifrado de +4 más allá del archivo).
[[nodiscard]] Core::Expected<Model, DataError> LoadBmd(std::span<const std::byte> file);

// 0x60da30: "Player" con -1 da "Player.bmd", "HelmMale" con 1 da "HelmMale01.bmd".
[[nodiscard]] std::string ModelFileName(std::string_view name, int number);
// Un modelo como lo pide el original: el número de su tabla, la carpeta ("Player\\"), el nombre y
// el número del archivo.
struct ModelFile
{
    std::uint16_t model = 0;
    std::string_view folder;
    std::string_view name;
    int number = -1;
};
// Los tres que cierran el juego si faltan (0x60daec, 0x60db24, 0x60db5c): Monster, Player y
// PlayerTest, sin mirar el número.
[[nodiscard]] bool IsRequiredModel(std::string_view name);
enum class ModelLoadErrorKind : std::uint8_t { Missing, Unreadable, SkeletonWithMeshes };
struct ModelLoadError
{
    ModelLoadErrorKind kind = ModelLoadErrorKind::Missing;
    std::string path; // como la pide el original ("Data\\Player\\Player.bmd")
    std::optional<DataError> data; // con Unreadable
    std::error_code system; // con Missing o Unreadable, lo que dijo el sistema, como TextureSetError
};
// Busca `folder + ModelFileName(...)` en `dataRoot` sin mirar mayúsculas (ResolveDataPath) y lo
// lee. Player con mallas es SkeletonWithMeshes.
[[nodiscard]] Core::Expected<Model, ModelLoadError> LoadModelFile(
    const std::filesystem::path& dataRoot, const ModelFile& file);
} // namespace Client::Data::Models
```

PR B suma:

```cpp
namespace Client::Data::Models
{
inline constexpr std::uint16_t PlayerSkeletonModel = 0x20a;
// Los huesos que 0x60dbe0 escribe en +0x54, +0x58 y +0x5c del modelo del personaje.
inline constexpr std::int16_t PlayerHeadBone = 20;
// 0x60ff50: el modelo de un ítem.
[[nodiscard]] constexpr std::uint16_t ItemModel(std::uint8_t group, std::uint16_t index);
enum class BodyPart : std::uint8_t { Helm, Armor, Pants, Gloves, Boots };
// Los modelos que carga 0x60dbe0, en su orden (la tabla de la CL6.R, paso 2).
[[nodiscard]] std::span<const ModelFile> PlayerModelFiles();

// Una escritura de velocidad de 0x60dbe0: las acciones de `first` a `last`, en orden.
struct ActionSpeedWrite
{
    std::uint8_t first = 0;
    std::uint8_t last = 0;
    float speed = 0.0f;
};
// Las 181 escrituras como rangos, en el orden del original (las de después pisan).
[[nodiscard]] std::span<const ActionSpeedWrite> PlayerActionSpeeds();
// La de la última escritura que toca `action`, o std::nullopt sin ninguna (lo que quede en +4 es
// lo del lector: CL6.R, paso 1).
[[nodiscard]] std::optional<float> PlayerActionSpeed(std::size_t action);
// Las que se quedan en la última key (+0 en 1).
inline constexpr std::array<std::uint8_t, 4> PlayerActionsWithoutRepeat = {68, 193, 195, 196};

enum class ModelTextureLoader : std::uint8_t { Ozj, Ozt };
// La letra después del primer punto, con tolower (0x6a0cce): 't' o 'T' es OZT (siempre con
// GL_NEAREST), otra es OZJ.
[[nodiscard]] ModelTextureLoader ModelTextureLoaderOf(std::string_view textureName);
// ski y level al 0x12e, hid al 0x12d (una malla que no se dibuja, 0x4ecc30) y hair al 0x137,
// byte a byte y en minúsculas: "Hair" no cuenta.
[[nodiscard]] std::optional<std::uint16_t> SkinSlotOf(std::string_view textureName);
} // namespace Client::Data::Models

namespace Client::Render::Models
{
// 0x60d530 y 0x60d550 como datos (pregunta 13): el próximo slot libre y la búsqueda por nombre
// desde el slot 0. Guarda las rutas que pidió, para que los string_view de las entradas no se
// muevan.
class ModelTextureSlots
{
public:
    explicit ModelTextureSlots(TextureSlot first); // 0x60d530
    // 0x60d5f0: el slot de cada malla de `model`, con las de la piel y el pelo cambiadas.
    std::vector<TextureSlot> Assign(const Data::Models::Model& model, std::string_view folder,
                                    Data::Textures::Sampling sampling);
    // Lo que hay que subir, con el slot y el muestreo de cada una (LoadTextureSet, CL5.1).
    [[nodiscard]] std::span<const Data::Textures::TextureSetEntry> ToLoad() const;
    [[nodiscard]] TextureSlot Next() const;
};
} // namespace Client::Render::Models
```

- [x] **Paso 1 (PR A):** los tests sintéticos, con el `.bmd` de `support/SyntheticModels.h` (una
  malla de tres vértices y un triángulo, dos acciones, tres huesos: una raíz, un hijo y un dummy):
  `BmdTest.ReadsAClearModel` (cada campo), `ReadsAnEncryptedModel` (el mismo, cifrado),
  `AnyVersionButTwelveIsClear` (`0x0A`, `0x0B` y `0x00`), `DoesNotLookAtTheMagic`,
  `EveryCutIsTooShort` (cada prefijo del archivo, uno por uno, es `FileTooShort`),
  `AnEncryptedSizeBeyondTheFileIsTooShort`, `CountsTheTrailingBytes`, `KeepsTheFloatBits` (un NaN
  con su carga), `ADummyBoneHasNoKeys`, `LockPositionsReadTwelveBytesPerKey`,
  `TheTriangleKeepsItsThreeIndexSets`; `ModelFileNameTest.FollowsTheThreeFormats` (-1, 0, 9, 10 y
  17); `ModelFilesTest.OnlyMonsterPlayerAndPlayerTestAreRequired`, `AMissingFileSaysItsPath`,
  `TheSkeletonWithMeshesIsAnError` y `FindsTheFileInAnyCase`.
- [x] **Paso 2 (PR A):** correrlos y verlos fallar:
  `cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "Bmd|ModelFile"`.
- [x] **Paso 3 (PR A, Emanuel, `TODO(human)`, resuelto por Claude):** `LoadBmd` con `Core::ByteReader` (CL1) y
  `DecryptK16` sobre una copia; `ModelFileName`, `IsRequiredModel` y `LoadModelFile`, con
  `ResolveDataPath` y `ReadDataFile` (el tope, `MaxModelFileBytes`, lo fija el golden).
- [x] **Paso 4 (PR A):** los golden (`GoldenDataTest`): `GoldenBmd.ReadsEveryModelOfTheLab` (cada
  archivo del `Data\` que empieza con `"BMD"`: los 1.962 se leen y ninguno deja bytes de más; las
  tablas `.bmd`, sin `"BMD"`, no son modelos), `ThePlayerIsASkeleton` (60 huesos, 10 dummy, 205
  acciones, ninguna malla), `ParentsComeBeforeChildren` (en los 1.962),
  `EveryIndexOfATriangleIsInItsMesh` (vértices, normales y UV) y `EveryModelFitsTheReadLimit`.
- [x] **Paso 5 (PR A):** verificar, con y sin la carpeta:
  ```bash
  ctest --preset linux-x64-debug -R "Bmd|ModelFile"
  MURET_CLIENT_GOLDEN_DIR=<Data del laboratorio> ctest --preset linux-x64-debug -R "GoldenBmd"
  ```
  Los mutantes a mano, cada uno con el test que lo mata: sin descifrar, con K16 desde `+4`, sin
  los 12 bytes de `lock_positions`, el triángulo de 36 bytes de paso, el padre como `uint8`, el
  `"%s%d"` sin el cero y un archivo corto que no falla.

  Hecho así (PR A, 2026-11-03, resuelto por Claude): `Data/Models/Model.h`, `Bmd.h/.cpp` y
  `ModelFiles.h/.cpp`, con 28 tests (`test_bmd.cpp`, `test_model_files.cpp` y
  `test_golden_bmd.cpp`) y el `.bmd` sintético de `support/SyntheticModels.h`, que escribe con
  basura (`0xA5`) lo que el lector no lee. Lo que cambió del contrato de arriba:
  - `Normal` suma el índice de vértice de `+16`, y `ModelLoadError` el `std::error_code` del
    sistema, como `TextureSetError`.
  - Una cantidad negativa (de mallas, huesos, acciones, de cada parte de una malla o de keys) es
    `DataError::InvalidData`: el original pediría memoria con las de una malla y las keys, y las
    tres del modelo las saltea como un 0 (`0x4f1882`) pero las deja guardadas. Ninguno del
    laboratorio la tiene (`BmdTest.ANegativeCountIsInvalidData`).
  - Lo que va en el archivo después del bloque cifrado no se lee ni se cuenta en `trailingBytes`
    (`BmdTest.BytesAfterTheEncryptedBlockAreNotRead`); el golden mira que ningún cifrado del
    laboratorio lo tenga.
  - `MaxModelFileBytes` es 8 MiB: el más grande del laboratorio es `player\playerTest.bmd`, de
    2.414.277 bytes.
  - `IsRequiredModel` compara byte a byte: `0x60daec`.. es un `strcmp` en línea.
  - `ParentsComeBeforeChildren` no vale en los 1.962: en `NPC\BloodCastle01.bmd` los huesos 43 y
    44 cuelgan del 45, y la pose de `0x4eb860` los concatena con la matriz que dejó una pose
    anterior (Q-C108, nuevo). El golden lista esos dos, y la pose de los NPC queda para la CL9.
  - Suman al golden `CountsTheVersionsOfTheLab` (816, 1.142 y los cuatro `0x00` de `Object3`) y
    `EveryNormalPointsAtAVertexOfItsMesh` (los 301.367); `EveryIndexOfATriangleIsInItsMesh`
    cuenta los 450.338 triángulos, todos de tipo 3.

  Los 14 mutantes a mano (los siete de arriba, más el orden de las cantidades, un negativo como
  cero, el nombre sin cortar en el 0, el esqueleto con mallas aceptado, los requeridos sin
  mayúsculas y el dummy solo con 1) los mata al menos un test cada uno. Sin la VM de Windows, se
  compiló y corrió en Linux con `-Werror` y sin precompiled headers, con `misc-include-cleaner` y
  `clang-format` 18 limpios.

  Revisión adversarial (2026-11-03, resuelta por Claude): un revisor del C++ y otro que contrastó
  el lector con el desensamblado, y después la decompilación de Ghidra del mismo `main.exe`. El
  formato, el cifrado y los nombres de archivo quedaron confirmados. Lo que se arregló, con un test
  que falló primero:
  - Cada hueso que se posa lleva un `BoneKeys` por acción aunque la acción no tenga keys, que no
    ocupan bytes: un `.bmd` de 1,2 MB con 32.767 acciones y 32.767 huesos pedía unos 50 GB. El
    original tampoco tiene tope: por cada hueso reserva 12 bytes por acción y tres bloques más por
    acción, aunque queden vacíos. `LoadBmd` corta con `InvalidData` pasando de
    `MaxPosedBoneActions` (65.536) huesos que se posan por acciones, y lo cuenta antes de pedir la
    memoria; el que más tiene en el laboratorio es `Player.bmd`, con 10.250
    (`BmdTest.TooManyPosedBonesTimesActionsIsInvalidData`,
    `GoldenBmd.EveryModelFitsThePosedBoneLimit`). Los tres mutantes del tope (sin descontar, con
    `>=` y contando los dummy) los mata ese test.
- [x] **Paso 6 (PR B):** los tests de las tablas, con los números de la CL6.R:
  `PlayerModelsTest.ThePlayerIsModel0x20a`, `ItemModelsAreGroupTimes512PlusIndex`,
  `ClassPartsAreOneToFive`, `HelmMaleOneIsItemZero` (el `HelmMale` N es el índice N − 1 del grupo
  7), `EveryRowIsAModelOfTheTable`; `PlayerActionsTest.TheSelectionActionIsPointTwoSix` (la 3),
  `OneToTwelveArePointTwoEightButSix` (salvo la 3, la 4, la 5, la 7 y la 8),
  `ThirteenToThirtyThreeArePointThree` (salvo la 21 y la 29, 0,35), `FiftySevenHaveNone`,
  `FourDoNotRepeat`, `AHundredAndEightyOneWrites` (la suma de los rangos),
  `LaterWritesOverrideEarlier`; `ModelTexturesTest.TheLoaderIsTheLetterAfterTheFirstDot` (`.t` y
  `.T`), `SkinPrefixesGoToTheirSlots` (solo en minúsculas);
  `ModelTextureSlotsTest.ARepeatedNameReusesItsSlot`, `ANewNameTakesTheNextSlot`,
  `TheSearchStartsAtSlotZero` (un nombre que subió otra llamada antes de `first` se reusa),
  `OztIsAlwaysNearest`, `TheSkinMeshesPointAtTheSkinSlots`.
- [x] **Paso 7 (PR B, Emanuel, resuelto por Claude):** las tablas como `constexpr`, con la dirección de cada fila en un
  comentario; `ModelTextureSlots` con lo que diga la CL6.R del paso 3 (mayúsculas, el slot que
  queda ocupado).
- [x] **Paso 8 (PR B):** los golden: `GoldenModelFiles.EveryModelOfThePlayerLoaderExists` (cada
  fila de `PlayerModelFiles`, o la lista de faltantes conocidos, vacía hasta que el golden diga
  otra cosa), `EveryMeshTextureOfThePlayerResolves` (la textura de cada malla de cada parte, con su
  cargador, en la carpeta de su llamador), `EveryPartVertexHangsFromAPosedBone` (el hueso de cada
  vértice de cada parte está en los 60 de `Player.bmd` y no es dummy, que no se posa: CL6.2) y
  `TheSelectionModelsExist` (`Logo05` y `NewFace01` a `05`, de `0x61ca30`).
- [x] **Paso 9 (PR B):** verificar como en el paso 5, con `-R "PlayerModels|PlayerActions|
  ModelTexture|GoldenModelFiles"`. Los mutantes: una velocidad de antes que pisa a la de después,
  el `+0` de las cuatro al revés, `ItemModel` con 511, la letra de la extensión en vez de la del
  primer punto, la letra sin `tolower`, `hair` al `0x12e`, y la búsqueda desde `first` en vez de
  desde 0.

  Hecho así (PR B, 2026-11-03, resuelto por Claude): `Data/Models/PlayerModels`, `PlayerActions` y
  `ModelTextures`, y `Render/Models/ModelTextureSlots`, con 36 tests más (golden incluidos, en
  `test_golden_model_files.cpp` y uno en `test_golden_bmd.cpp`). Las dos tablas y las velocidades salen de **emular** `0x60dbe0`
  (desde `0x60e8fc`, el modo `Data\`) y `0x60ff50` sobre el `main.exe` con Unicorn
  (`Client/docs/research/scripts/emulate_model_loaders.py`): cada fila lleva la dirección de su
  llamada o de su escritura. Lo que cambió del contrato de arriba:
  - `PlayerActionSpeeds` tiene 47 filas, una por instrucción del original (un bucle o una sola
    escritura), y no los 35 grupos de 06: suman las mismas 181 escrituras, con la misma velocidad
    final en cada acción. Cada float tiene los bits del inmediato del original.
  - `ItemModelFiles` son 243 llamadas con 243 modelos distintos; faltan solo los tres `giftbox`. Ojo
    al emular: arrancado a mitad de `0x60ff50`, `ebx` queda en 0 y `0x6109ed` apaga el modo `Data\`
    (daba 244 llamadas, una repetida, y diez familias desde el 0).
  - El cargador de una textura de modelo es el `TextureLoader` que ya estaba (regla 4), no un enum
    nuevo, y `BodyPart` no entra hasta que lo use la CL6.3. `PlayerFootBones` (6 y 13) sí, junto
    con la cabeza.
  - `ModelTextureLoaderOf` sin punto da el OZJ (Q-C107, corregido en la revisión de abajo).
    `TextureScriptOf` sigue a `0x459940`: hasta cuatro letras después del primer `_`,
    y una que no es `R`, `H`, `S` ni `N` deja la malla sin script, también lo que ya prendió.
  - `PlayerActionsWithoutRepeat` va en el orden en que las escribe el original (195, 196, 193 y
    68), no ordenado como en el contrato.
  - `ModelTextureSlots` suma `SetNext` (otra llamada a `0x60d530`, que puede ir para atrás, Q-C89),
    y `ToLoad` pide la carpeta, el nombre hasta el primer punto y la extensión del cargador que
    eligió la letra, así `LoadTextureSet` abre el mismo archivo que el original.
  - El golden encontró que 17 mallas de partes del personaje tienen el script `R` (`hair_R.jpg`,
    `FShoots_R.jpg`, `lowerTest_R.jpg`, `lowerTest2_R.jpg`): 05 decía que ninguna, y quedó
    corregido (`GoldenBmd.TheTextureScriptsOfTheLab`). Suman `TheItemLoaderMissesOnlyTheGiftBoxes`
    y `ThePlayerActionsFitTheSkeleton`; los modelos de la selección son los de `Logo\`
    (`0x61cb92` y `0x61cbb1`).

  Los 13 mutantes a mano (los siete de arriba y la búsqueda que pasa del próximo, la piel antes de
  pedir, la OZT con el filtro de quien pide, el script con minúsculas, con cinco letras y con una
  letra mala salteada) los mata al menos un test cada uno. La revisión encontró dos variantes que
  seguían vivas: la búsqueda que mira también el próximo (`<=`) y la que empieza en el slot del
  constructor.

  Revisión adversarial (2026-11-03, resuelta por Claude): los mismos dos revisores que la A, y
  después la decompilación de Ghidra del mismo `main.exe`. Las dos tablas, las velocidades, la piel
  y el script quedaron confirmados fila por fila. Lo que se arregló, cada cosa con un test que
  falló primero:
  - `ModelTextureSlots` no se copia: las rutas de `ToLoad` de la copia apuntaban a las del otro
    (ASan: `heap-use-after-free`). Se mueve, y las rutas siguen valiendo
    (`ModelTextureSlotsTest.MovesButDoesNotCopy`).
  - Sin punto, `0x60d68a` deja el índice en el largo del nombre, no en 0: el original mira el byte
    del archivo después del 0, y MuRet da el OZJ (Q-C107, `WithoutADotItIsAnOzj`).
  - Las dos variantes vivas: `TheSearchStartsAtSlotZero` ahora pide más abajo del slot del
    constructor y busca con el próximo más arriba, y `TheNextSlotItselfIsNotSearched` vuelve el
    próximo justo sobre un nombre.

**Qué queda para después:** el cuaternión de cada key y el esqueleto (CL6.2), las cajas de
`0x4f1f80` (CL6.3, el picking), una sola `ModelTextureSlots` para todos los cargadores, porque la
tabla del original es una y la búsqueda de los ítems ve los nombres del personaje, y la cuenta de
usos con que `0x63c150` suelta un slot y borra su nombre (CL6.3), el `+0` que `0x60ff50` prende en
cinco mallas de ítems (`0x6117fd`..`0x611847`, 06), el script de la textura de cada malla
(`0x459940`: lo lee la CL6.R, pasos 1 y 6, y lo usa el dibujo de la CL6.3), los modelos de los
monstruos (`Monster`, CL9: con el research ya corregido, `Monster50` cerraría el original) y los de
los objetos del mapa (CL7).

## Sub-ola CL6.2: skinning y animación

**Concepto:** de los datos del `.bmd` a los vértices en el mundo, como el original antes de cada
`glBegin(GL_TRIANGLES)`: el esqueleto con un cuaternión por key, la pose de cada hueso entre dos
keys, la cadena de padres, cada vértice por la matriz de su hueso; y el frame que avanza por paso
con la velocidad de la acción, la mascota por tiempo, y las UV de los pases de brillo. Todo en
`ClientLib`, sin GL. Va en tres PRs apilados: **PR A**, la matemática, el esqueleto, la pose y el
skinning, contra la traza; **PR B**, el avance de la animación; **PR C**, el brillo.

**Contrato** (05 § Modelos bmd; 06 § Modelos y objetos y § Cuánto avanza la animación; la tabla de
arriba; lo que diga la CL6.R):

| Qué | Original | MuRet |
| --- | --- | --- |
| el cuaternión de una key | `0x5fcd20`, en `0x4f13d0` al leer | `MakeSkeleton`, con `AngleQuaternion` (`glm::quat(vec3)`, pregunta 5) |
| la pose de un hueso | `0x4eb860`: slerp entre la key anterior y la de ahora con la parte fraccionaria, salvo que `0x5fc700` las dé iguales; lineal entre las posiciones | `PoseSkeleton`, con `QuaternionSlerp` y `QuaternionsAlmostEqual` a mano |
| la cabeza | el hueso de `+0x54` resta en grados el `+0x28` del objeto a su x y el `+0x2c` a su z (π/180 en float, `0x6ba140`) antes de pasar a cuaternión | `PoseSkeleton(…, headAngles, …)`; 06 dice "x e y" y se corrige (CL6.R) |
| la raíz | `0x5fc8f0` con los ángulos del modelo (`+0x78`, en grados) y, con el octavo argumento de `0x4eb860`, la escala (`+0x68`) y la posición (`+0x6c`) del modelo; si no, `0x4ebc80` las pone en cada vértice. `0x5ffdf0` los copia del objeto | `ObjectTransform` y `AngleMatrix` |
| `lock_positions` | en el hueso 0, por su índice, si la acción anterior o la de ahora lo tiene: x e y de la key 0 de la de ahora, y la z interpolada más `+0x84` | `PoseSkeleton` |
| la cadena | `0x5fca90` (`R_ConcatTransforms`) con el padre, en orden | `ConcatTransforms`; `MakeSkeleton` rechaza un padre que no va antes |
| un hueso dummy | no se posa: queda lo que había | igual: `PoseSkeleton` no lo toca |
| un vértice | `0x4ebc80`: `0x5fccc0` (`VectorTransform`) con la matriz de su hueso; la normal con `0x5fcbd0` (`VectorRotate`) | `SkinMesh`, en la CPU (pregunta 6) |
| el avance | `0x4ec200`: sin acciones en el modelo o con una key o ninguna en la de ahora, nada; si no, `frame += velocidad`, con el módulo, el tope de n − 0,01 y la key anterior | `AdvanceAnimation` (PR B) |
| cambiar de acción | `0x4ea030`: con una acción que el modelo no tiene o con la misma, nada; con otra, la de ahora y su frame (tal cual, sin truncar: `0x4ea072`..`0x4ea078`) pasan a anteriores y el frame a 0 | `SetAction` (PR B) |
| el fin en la selección | el frame + 2 para "terminó", el real para el módulo (Q-C21); con "terminó", se vuelve a elegir la acción de parado, que es la misma: no cambia nada en pantalla | `AdvanceAnimation(…, AnimationScene::CharacterSelect)` lo reproduce (PR B) |
| la mascota y las alas | por frame dibujado: 0,5 y 0,25 (1 en las acciones `0x1e` y `0x1f`), `0x509340` (Q-C20) | `AdvanceByTime`: lo mismo cada 46,875 ms, a cualquier FPS (PR B) |
| el render | posa lo que dejó el último paso | `Interpolated`: una copia avanzada `alpha` de un paso con la misma regla (D-C4, pregunta 7; PR B) |
| el brillo | `0x4ecc30` (`0x4ed890` es código muerto): ocho modos con UV de la normal en el mundo y el reloj T, tres verificados en la traza (06) | `ShineTexCoords` (PR C) |
| el reloj del brillo | `timeGetTime` en un `float` (Q-C22) | `SceneCanvas::frameClock`, ms desde que arranca el cliente |

**Archivos:**
- **PR A:** crear `Client/src/Render/Models/HalfLifeMath.h/.cpp`, `Skeleton.h/.cpp`, `Pose.h/.cpp`,
  `Skinning.h/.cpp`, `Client/tests/render/test_half_life_math.cpp`, `test_skeleton.cpp`,
  `test_pose.cpp`, `test_skinning.cpp`, `test_golden_skinning_trace.cpp`,
  `Client/tests/support/GlTrace.h/.cpp` (el lector de la traza, pregunta 12) y `test_gl_trace.cpp`.
  Modificar `Client/CMakeLists.txt` (glm `PUBLIC` en `ClientLib`, pregunta 4; los fuentes de
  `Render/Models/` en el filtro de `ClientLib`), `Client/tests/support/GoldenDir.h/.cpp`
  (`GlTraceDir`, `GoldenAndGlTraceTest`), el CI (nada: sin la variable se saltean), PROGRESS y este
  plan.
- **PR B:** crear `Client/src/Render/Models/Animation.h/.cpp` y
  `Client/tests/render/test_animation.cpp`.
- **PR C:** crear `Client/src/Render/Models/Shine.h/.cpp`, `Client/tests/render/test_shine.cpp` y
  `test_golden_shine_trace.cpp`.

**Interfaces:**

```cpp
namespace Client::Render::Models
{
// La 3 × 4 de Half-Life (m[fila][columna], la traslación en la columna 3) en un glm::mat4x3,
// que guarda por columnas: m[3] es la traslación. Copiar índices sin dar vuelta es otro giro.
using BoneTransform = glm::mat4x3;

// 0x5fcd20, 0x5fce90, 0x5fc700, 0x5fcdb0, 0x5fc8f0, 0x5fca90, 0x5fccc0 y 0x5fcbd0.
[[nodiscard]] glm::quat AngleQuaternion(const glm::vec3& radians);
[[nodiscard]] glm::quat QuaternionSlerp(const glm::quat& p, const glm::quat& q, float t);
[[nodiscard]] bool QuaternionsAlmostEqual(const glm::quat& p, const glm::quat& q);
[[nodiscard]] BoneTransform QuaternionMatrix(const glm::quat& q);
[[nodiscard]] BoneTransform AngleMatrix(const glm::vec3& degrees);
[[nodiscard]] BoneTransform ConcatTransforms(const BoneTransform& parent,
                                             const BoneTransform& child);
[[nodiscard]] glm::vec3 VectorTransform(const glm::vec3& v, const BoneTransform& m);
[[nodiscard]] glm::vec3 VectorRotate(const glm::vec3& v, const BoneTransform& m);

struct BoneKey
{
    glm::vec3 position{};
    glm::vec3 angles{}; // radianes, del archivo: la cabeza los usa (0x4eb860)
    glm::quat rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
};
struct SkeletonBone
{
    bool dummy = false;
    std::int16_t parent = -1;
    std::vector<std::vector<BoneKey>> actions; // [acción][key]
};
// La acción de 0x10 bytes del original.
struct ActionInfo
{
    bool repeats = true;      // +0 en 0
    float speed = 0.0f;       // +4, keys por paso
    std::int16_t keyCount = 0; // +8
    bool lockPositions = false; // +10
};
struct Skeleton
{
    std::vector<SkeletonBone> bones;
    std::vector<ActionInfo> actions;
    std::int16_t headBone = -1; // +0x54: -1 (0x4f1f20) salvo Player, 20
};
// Los cuaterniones de cada key, como 0x4f13d0. El error, con un padre que no va antes.
[[nodiscard]] Core::Expected<Skeleton, std::string> MakeSkeleton(const Data::Models::Model& model);
// MakeSkeleton más lo de 0x60dbe0: las velocidades, las que no repiten y la cabeza (PR B).
[[nodiscard]] Core::Expected<Skeleton, std::string> MakePlayerSkeleton(
    const Data::Models::Model& model);

// Lo que el objeto guarda de su animación.
struct AnimationState
{
    std::uint8_t action = 0;
    float frame = 0.0f;              // con la parte fraccionaria
    std::uint8_t previousAction = 0; // la de la key que guardó el avance
    std::int16_t previousKey = 0;
};
struct ObjectTransform
{
    glm::vec3 position{};
    glm::vec3 angles{}; // grados
    float scale = 1.0f;
    float lockedRootHeight = 0.0f; // +0x84 del modelo: 0 en los personajes (CL6.R, paso 4)
};
// 0x4eb860: un BoneTransform por hueso del esqueleto en `bones` (del mismo tamaño). Los dummy no
// se tocan. `headAngles` en grados: x se resta al x de la cabeza y y al z.
void PoseSkeleton(const Skeleton& skeleton, const AnimationState& animation,
                  const ObjectTransform& object, const glm::vec2& headAngles,
                  std::span<BoneTransform> bones);

struct SkinnedMesh
{
    std::vector<glm::vec3> positions; // en el mundo, uno por vértice de la malla
    std::vector<glm::vec3> normals;   // rotadas, una por normal
};
// 0x4ebc80. El hueso de cada vértice ya se miró al cargar (GoldenModelFiles, CL6.1).
void SkinMesh(const Data::Models::Mesh& mesh, std::span<const BoneTransform> bones,
              SkinnedMesh& out);
} // namespace Client::Render::Models
```

PR B suma:

```cpp
namespace Client::Render::Models
{
// La escena 4 decide "terminó" con el frame + 2 (Q-C21); las demás, con el real.
enum class AnimationScene : std::uint8_t { Other, CharacterSelect };
// 0x4ec200 con `speed` (la de la acción, o la que le pase quien llama): avanza, guarda la key
// anterior al cruzar una, y devuelve si terminó.
bool AdvanceAnimation(AnimationState& state, const ActionInfo& action, float speed,
                      AnimationScene scene);
// 0x4ea030: nada con una acción que `skeleton` no tiene o con la misma; si no, la de ahora y el
// frame tal cual (0x4ea072..0x4ea078: no lo trunca, y da lo mismo, porque la pose trunca) pasan a
// anteriores y el frame a 0.
void SetAction(AnimationState& state, const Skeleton& skeleton, std::uint8_t action);
// 1 / 21,33 FPS: el frame del modo fiel de la CL3 (64 frames cada 3 s).
inline constexpr Core::Duration OriginalFrameTime = std::chrono::microseconds{46'875};
// Q-C20 corregido: `keysPerOriginalFrame` cada OriginalFrameTime de `elapsed`, con la regla de
// AdvanceAnimation. Guarda el resto que no llega a un frame.
struct TimedAnimation
{
    AnimationState state;
    Core::Duration carry{};
};
void AdvanceByTime(TimedAnimation& animation, const ActionInfo& action, float keysPerOriginalFrame,
                   Core::Duration elapsed);
// La copia que posa el render: `alpha` de un paso más, con la misma regla.
[[nodiscard]] AnimationState Interpolated(const AnimationState& state, const ActionInfo& action,
                                          float speed, float alpha);
} // namespace Client::Render::Models
```

PR C suma:

```cpp
namespace Client::Render::Models
{
// Los pases de brillo que la traza muestra (06), con su bandera del dibujo de 0x4ecc30. Los otros
// cinco modos de la tabla del 06 entran con su bandera cuando algo los pida.
enum class ShinePass : std::uint8_t { Shiny /*0x8*/, Chrome /*0x4*/, Chrome4 /*0x1000*/ };
// Las UV de un vértice con la normal en el mundo y el reloj del frame en ms.
[[nodiscard]] glm::vec2 ShineTexCoords(ShinePass pass, const glm::vec3& worldNormal,
                                       Core::Duration frameClock);
} // namespace Client::Render::Models
```

- [x] **Paso 1 (PR A):** con Context7 (`/g-truc/glm`), confirmar la pregunta 5 otra vez antes de
  escribir: `glm::quat(glm::vec3)`, `glm::mat3_cast`, `glm::quat::wxyz`, `glm::mat4x3` y
  `glm::mat4(mat4x3)`. Si algo cambió, la pregunta 5 se corrige acá.
- [x] **Paso 2 (PR A):** los tests: `HalfLifeMathTest.AQuarterTurnAroundZ`,
  `AngleQuaternionIsTheHalfLifeFormula` (contra la fórmula de 06 escrita en el test),
  `AlmostEqualKeysCopyTheFirst` (0,001 de diferencia en cada componente: devuelve p sin slerp),
  `SlerpFlipsLikeHalfLife`, `SlerpIsLinearBelowOneInAHundredMillion`, `TheMatrixIsColumnMajor`
  (`m[3]` es la traslación), `AngleMatrixIsInDegrees` (90 da un cuarto de vuelta),
  `ConcatIsParentTimesChild`, `VectorRotateDoesNotMove`; `SkeletonTest.EveryKeyHasItsQuaternion`,
  `AParentAfterItsChildIsAnError`, `ThePlayerHeadIsBoneTwenty`; `PoseTest.AChildFollowsItsParent`,
  `TheRootCarriesTheObjectTranslation`, `TheRootScalesButTheChildrenDoNot` (la escala va en la
  raíz y llega por la cadena), `InterpolatesBetweenThePreviousKeyAndTheCurrent`,
  `TheHeadSubtractsXAndZ`, `LockPositionsTakesXAndYFromKeyZero`,
  `LockPositionsIsBoneZeroWithEitherAction`, `DummyBonesAreNotPosed` (el
  `BoneTransform` de un dummy queda como estaba); `SkinningTest.AVertexFollowsItsBone`,
  `TheNormalOnlyRotates`.
- [x] **Paso 3 (PR A):** correrlos y verlos fallar:
  `ctest --preset linux-x64-debug -R "HalfLifeMath|Skeleton|Pose|Skinning"`.
- [x] **Paso 4 (PR A, Emanuel, `TODO(human)`, resuelto por Claude):** `HalfLifeMath`,
  `MakeSkeleton`, `PoseSkeleton` y `SkinMesh`. Las constantes double del original (0,5, 1e-8, 0,001)
  con su tipo: el slerp compara en double como `0x5fce90`, y lo que vuelve a float lleva un
  `static_cast` (C4244).
- [x] **Paso 5 (PR A):** el lector de la traza: `tests/support/GlTrace.h`, con
  `ReadGlTrace(const std::filesystem::path&) -> Core::Expected<GlTrace, std::string>`: los frames
  cortados en cada `wglSwapBuffers`, y en cada frame los `glViewport`, las matrices
  (`glMultMatrixd`, `glRotatef`, `glTranslatef`, `glOrtho`) y los bloques de `glBegin` a `glEnd`
  con su textura atada, su modo, sus UV, sus colores y sus vértices. Tests sin la traza, con un
  texto sintético: `GlTraceTest.SplitsTheFramesAtTheSwap`, `ABlockKeepsItsTexture`,
  `ReadsTheMatricesInOrder`, `AMalformedLineSaysItsNumber`. Y en `GoldenDir.h`, `GlTraceDir()` con
  la regla de D-C9 y `GoldenAndGlTraceTest`, como `GoldenAndWireDataTest`, con
  `TraceFile(std::string_view name)`; `GoldenDirTest` suma sus casos.
- [x] **Paso 6 (PR A):** el golden de la pose (`GoldenAndGlTraceTest`: `MURET_CLIENT_GOLDEN_DIR` y
  `MURET_CLIENT_GL_TRACE_DIR`): `GoldenSkinningTrace.MatchesTheFiveFramesOfTheSelection`. Lee
  `Player.bmd` y las cinco partes del personaje de la traza (06 § El personaje, vértice a vértice),
  identifica cada bloque por su secuencia de UV, arma el estado como el original (la acción 3, el
  frame f + 1 de la tabla del 06 y ⌊f⌋ como anterior), el objeto del lugar 0 de `0x4c20a0` (la
  posición, 100° y la escala de la CL6.R) en z = 163, y compara cada vértice: menos de 0,02
  unidades en los cinco frames. Y `MatchesTheGuardOfLorencia` (la acción 1, sin la cabeza, como el
  06), que prueba otro personaje con otras partes; su posición sale del ajuste, como en 05 (la
  media de la diferencia), porque no está en ninguna tabla. Ningún número de la traza va al test
  que no esté ya en el 06.
- [x] **Paso 7 (PR A):** verificar:
  ```bash
  ctest --preset linux-x64-debug -R "HalfLifeMath|Skeleton|Pose|Skinning|GlTrace"
  MURET_CLIENT_GOLDEN_DIR=<Data> MURET_CLIENT_GL_TRACE_DIR=<trazas> \
    ctest --preset linux-x64-debug -R "GoldenSkinningTrace"
  ```
  Los mutantes: `glm::quat(x, y, z, w)` en vez de `wxyz`, el slerp de glm, sin la igualdad de
  0,001, la traslación en la fila de abajo, la cabeza en x e y, la escala en cada hueso, los
  ángulos del objeto en radianes, y el frame sin el + 1 del original. Cada uno con el test que lo
  mata, y los de la traza con su error máximo en el mensaje.

  Hecho así (PR A, 2026-11-03, resuelto por Claude): `Render/Models/HalfLifeMath`, `Skeleton`,
  `Pose` y `Skinning`, `tests/support/GlTrace` y `GoldenAndGlTraceTest`, con 36 tests
  (`test_half_life_math.cpp`, `test_skeleton.cpp`, `test_pose.cpp`, `test_skinning.cpp`,
  `test_gl_trace.cpp` y los dos golden de `test_golden_skinning_trace.cpp`). glm entra a `ClientLib`
  como `PUBLIC`. El paso 1 confirmó la pregunta 5 en Context7 y en el header de glm 1.0.3: la
  fórmula de `glm::quat(vec3)`, `mat3_cast`, `glm::quat::wxyz`, el orden x, y, z, w en memoria y
  `mat4(mat4x3)` con la traslación en la última columna. La matemática es la de la CL6.0
  (`labs/cl6.0-huesos`) con los tipos de glm. Lo que cambió del contrato de arriba:
  - **La escala y la posición del objeto van en el vértice, no en la raíz.** El cuerpo de un
    personaje va por `0x50ba30` → `0x6001b0(objeto, 1, …)`, que solo posa: llama a `0x4eb860` con su
    último argumento en 0 (la raíz es solo `AngleMatrix` de los ángulos). Cada parte va después por
    `0x60cf70`, que llama a `0x4ebc80` con los huesos de `objeto + 0x120` y su último argumento en
    1: escala y mueve cada vértice (*estático*: `0x50bbe4`..`0x50bbf7`, `0x600294`..`0x6002ce` y
    `0x60d186`). `0x5ffdf0`, con el mismo cableado, es el de los monstruos y los NPC (antes este
    párrafo lo daba como el del personaje; de la revisión). Las posiciones dan lo mismo; las
    normales no se escalan, y eso lo confirmó el brillo del PR C contra la traza. Por eso `SkinMesh`
    recibe el `ObjectTransform`, y `TheRootCarriesTheObjectTranslation` y
    `TheRootScalesButTheChildrenDoNot` son `PoseTest.TheRootTurnsWithTheObjectButDoesNotMoveWithIt`
    y `SkinningTest.TheObjectScalesAndMovesTheVertices`.
  - `MakeSkeleton` devuelve un `SkeletonError` con el hueso y el padre, no un `std::string`, como
    `ModelLoadError`. `PoseSkeleton` y `SkinMesh` devuelven `bool`: sin escribir, si los huesos no
    son del tamaño del esqueleto, si falta una key o si un vértice nombra un hueso que no está.
  - De `0x4eb860` (pseudo-C de Ghidra y desensamblado): la key de ahora es el frame truncado, la
    anterior la que guardó el avance, y una acción o una key de más es la 0; la parte fraccionaria
    sale del frame antes de cortarlo (`PoseTest.AFrameOutOfRangeKeepsItsFraction`); con dos keys
    iguales a 0,001 copia la anterior. El π/180 de la cabeza (`0x6ba140`) es `0x3c8efa36`, un ulp más
    que el de la raíz (`0x6ba360`): `PoseTest.TheHeadUsesTheDegreeFloatOfTheOriginal`.
  - `ThePlayerHeadIsBoneTwenty` va con `MakePlayerSkeleton`, en el PR B.
  - **El golden busca el frame.** Con los frames del 06 la selección daba 0,020 y el guardia 0,033;
    buscando cada frame a ±0,03 keys del de la tabla, de a 0,0005, los diez caen a 0,0014 o menos,
    lo que redondea un float en coordenadas de 15.000. El test pide menos de 0,005: un error de
    verdad (sin el + 1, la interpolación al revés, un eje cambiado) no se arregla corriendo el frame
    unas centésimas. Los frames buscados corrigieron la tabla del 06, y saltan justo 33 y 34 pasos
    de 0,26 en la selección y 20 y 19 de 0,28 en el guardia, lo que pide el PR B.
  - Las mallas que van al slot `0x12d` (`hide.jpg`, `hide_m.jpg`) no se dibujan, y el golden las
    saltea. El guardia se compara sin el casco: `HelmMale10` y `MaskHelmMale10` cuelgan enteros de
    la cabeza, que en Lorencia no sigue la animación.
  - `GoldenDirTest` no suma casos: `GoldenAndGlTraceTest` usa la misma regla (`RequireDirectory`), y
    sin la carpeta falla diciendo la variable (probado a mano con una que no existe).

  De los 17 mutantes a mano mueren 15: el cuaternión con `glm::quat(x, y, z, w)`, sin la igualdad de
  0,001, el giro traspuesto, la cabeza en x e y, la escala en la raíz, los ángulos del objeto en
  radianes, de la key de ahora a la siguiente (el + 1), t y 1 − t cambiados, `lock_positions` con la
  key 0 de la anterior, solo con la de ahora y en cualquier raíz, la parte fraccionaria después de
  cortar, la normal con la traslación, un padre de después aceptado y el slerp sin dar vuelta.
  Quedan dos equivalentes: el slerp de glm (da vuelta con el mismo criterio, el producto negativo, y
  usa la misma fórmula: difiere en el redondeo, 1,8e-7 en la CL6.0) y la rama lineal con el cos sin
  redondear (entre 1 − 3e-8 y 1 − 1e-8 las dos ramas dan el mismo float; la CL6.0 lo veía porque
  exponía la rama elegida).

  Revisión adversarial (2026-11-03, resuelta por Claude): un revisor del C++, con ASan, UBSan y sus
  propios mutantes, y otro que contrastó cada afirmación con el desensamblado y armó su propio
  skinning: le dieron los mismos frames, el mismo reloj y el lugar del guardia, (14.350; 12.850;
  165). Lo que se arregló, cada cosa con un test que falló primero o que mata un mutante que vivía:
  - `PoseSkeleton` leía fuera de `bones` con un `Skeleton` armado a mano con un padre que no va
    antes (ASan): ahora devuelve false (`PoseTest.ABadParentIsRefused`). Se partió en
    `LocalTransform` (regla 1).
  - La cabeza con dos keys distintas no tenía test, y tres mutantes vivían
    (`TheHeadInterpolatesBothKeys`). Los golden posan con la cabeza del personaje, el hueso 20.
  - Los bordes justos: la acción y la key que son la cantidad, una key que falta, el hueso de una
    normal justo de más y el `SkinnedMesh` que se reusa (`AnActionOrAKeyJustOutOfRangeIsTheFirst`,
    `AMissingKeyIsRefused`, `ABoneOutOfRangeIsRefused`, `ReusesItsBuffers`).
  - El chequeo de "el mejor está en el borde de la búsqueda" no saltaba nunca, por el redondeo de la
    resta: ahora mira el paso, y con t corrida 0,03 salta. La búsqueda absorbe un error que equivale
    a correr el frame; ese lo ven los `PoseTest` (y el golden del brillo, que no busca).
  - El cuerpo del personaje no va por `0x5ffdf0`, que es el de los monstruos, sino por `0x6001b0` y
    `0x60cf70`: se corrigieron las citas de arriba, de los headers y de PROGRESS.
  - El lector de la traza: la cabecera solo antes de la primera llamada, el número de
    `glBindTexture` entero y una función por llamada (regla 1).
- [x] **Paso 8 (PR B):** los tests, con un reloj falso y el `FixedStep` de la CL3:
  `AnimationTest.AdvancesBySpeedPerStep`, `WrapsModuloTheKeys`,
  `WrapsModuloKeysMinusOneWithLockPositions`, `HoldsJustBeforeTheLastKeyWithoutRepeat` (n − 0,01),
  `ANegativeFrameAddsKeysMinusOne`, `StoresThePreviousKeyWhenItCrossesOne`,
  `TheSelectionEndsTwoKeysEarlyButWrapsOnTheRealFrame` (Q-C21),
  `AnActionOfOneKeyDoesNotMove`, `SetActionResetsOnlyForAnotherAction`,
  `SetActionKeepsTheOldOneAsPrevious`, `SetActionIgnoresAnActionTheModelDoesNotHave`,
  `TheSelectionJumpsAreThoseOfTheTrace` (33, 33, 34 y 34
  pasos con 0,26 dan los saltos de 06 módulo 7), `TheLorenciaJumpsAreThoseOfTheTrace` (20, 19, 19
  y 19 con 0,28, módulo 6), `TheBodyRunsTheSameKeysPerSecondAtEveryFrameRate` (21,33, 60 y 240
  FPS), `ThePetRunsByTimeAtEveryFrameRate` (10,67 keys por segundo a las tres),
  `TheWingsRunFasterInThirtyAndThirtyOne`, `TheRenderCopyCrossesKeysLikeTheStep` (`Interpolated`
  con `alpha` 1 es un paso), `MakePlayerSkeletonSetsTheSpeeds`.
- [x] **Paso 9 (PR B, Emanuel, resuelto por Claude):** `Animation.h/.cpp`, y `MakePlayerSkeleton` con las tablas de la
  CL6.1. Verificar con `ctest --preset linux-x64-debug -R "Animation"`. Los mutantes: el módulo con
  el frame + 2, sin guardar la key anterior, la mascota por frame, una acción de una key que avanza,
  `SetAction` que no guarda la anterior, y `Interpolated` que no guarda la key que cruza.

  Hecho así (PR B, 2026-11-03, resuelto por Claude): `Render/Models/Animation` y
  `MakePlayerSkeleton` (en `Skeleton`), con 24 tests: 19 de `AnimationTest`, 4 de `SkeletonTest`
  (`MakePlayerSkeletonSetsTheSpeeds`, `ThePlayerHeadIsBoneTwenty`, que pasó de la A,
  `MakePlayerSkeletonSetsWhatTheModelHas` y `MakePlayerSkeletonKeepsTheErrors`) y el golden
  `GoldenModelFiles.TheActionsOfTheTracesAreThoseOfTheJumpTests` (la 3 de `Player.bmd` tiene siete
  keys a 0,26 y la 1 seis a 0,28, las dos repiten y sin `lock_positions`). Las reglas salen del
  pseudo-C de `0x4ec200` y `0x4ea030` y de los floats del binario (0, 1, 2 y 0,01 en `0x6b86a0`,
  `0x6b87d4`, `0x6b8818` y `0x6b88d4`). Lo que cambió del contrato de arriba:
  - **Q-C87 no se queda en el frame 0.** Con velocidad 0, el primer avance deja el frame en 0, y la
    regla de "0 o menos, más las keys menos 1" lo pasa a la última key, donde se queda
    (`AnActionWithoutSpeedGoesToItsLastKeyAndStays`). La key entera no cambió al sumar, así que la
    anterior queda la que había, y eso es lo que muestra la pose (de la revisión). La acción 0, la
    única sin velocidad con `lock_positions`, vuelve a 0 en cada avance
    (`AnActionWithoutSpeedAndWithLockStaysAtZero`). Se corrigió la fila del 07.
  - Las alas miran la acción del **personaje** (`objeto + 0x111` en `0x50ba30`): 1 key por frame con
    la `0x1e` o la `0x1f`, y 0,25 con cualquier otra (`WingKeysPerOriginalFrame`). La mascota es 0,5
    para `Helper02` (`0x1c13`, `PetKeysPerOriginalFrame`).
  - `AdvanceByTime` cuenta 0 un tiempo negativo (`ANegativeTimeCountsZero`). `Interpolated` avanza
    con la escena `Other`: el + 2 de la selección solo cambia "terminó", no el frame.
  - Los saltos de la traza usan los frames corregidos del 06 (de la A): 33, 33, 34 y 34 pasos de
    0,26 llevan cada uno al siguiente a menos de 0,002, y 20, 19, 19 y 19 de 0,28 los del guardia.
  - `MakePlayerSkeletonSetsTheSpeeds` es de `SkeletonTest`, no de `AnimationTest`: va con el
    esqueleto.

  Los 16 mutantes a mano (los seis de arriba y el tope en n − 1, `lock_positions` con el módulo de
  todas las keys, el frame negativo solo con menos de 0, el + 2 en cualquier escena, `SetAction`
  con la misma acción, con una que no está, sin las que no repiten, sin la cabeza, las alas rápidas
  solo en la `0x1e` y un tiempo negativo que resta) los mata al menos un test cada uno.

  Revisión adversarial (2026-11-03, resuelta por Claude): los mismos dos revisores que la A. Las
  reglas de `0x4ec200` y `0x4ea030`, las velocidades de las alas y la mascota, y los saltos de la
  traza quedaron confirmados. Cinco tests más matan los mutantes que vivían: la velocidad negativa
  que trunca hacia 0 (`ANegativeSpeedTruncatesTowardZero`), un paso que cruza dos keys
  (`CrossingTwoKeysStoresTheOldOne`), caer justo en la cantidad de keys
  (`LandingOnTheKeyCountIsPastTheLastKey`), el + 2 de la selección solo en las que repiten
  (`TheSelectionDoesNotLookAheadWithoutRepeat`) y Q-C87 con `lock_positions`. El comentario de
  `Truncate` decía mal qué da `__ftol` fuera de un int.
- [x] **Paso 10 (PR C):** los tests: `ShineTest.TheShinyPassIsHalfTheNormal` (`0x8`;
  u = 0,5·nz + 0,2, v = 0,5·(ny + 1)), `TheChromePassRunsWithTheClock`,
  `TheChrome4PassUsesSinAndCos`, `TheClockCountsFromTheStart` (con T = 2²⁴ + 1 ms la u cambia lo de
  un ms, que en un `float` no cambiaría: Q-C22); y el golden
  `GoldenShineTrace.MatchesTheThreePassesOfTheSelection` (las UV de los pases 115, 120 y 141 de la
  traza, con las normales del `.bmd` posado: menos de 0,02; T sale del primer vértice del pase 120,
  como en 06).
- [x] **Paso 11 (PR C, Emanuel, resuelto por Claude):** `Shine.h/.cpp`. Verificar con
  `-R "Shine"` y el golden. Los mutantes: la normal sin rotar, T en segundos, y el seno y el coseno
  cambiados de lugar.

  Hecho así (PR C, 2026-11-03, resuelto por Claude): `Render/Models/Shine`, con 6 tests: 5 de
  `ShineTest` (`ANegativeClockCountsZero` de más) y el golden
  `GoldenShineTrace.MatchesTheThreePassesOfTheSelection`. Las fórmulas y los floats (0,0001, 0,001,
  0,002, 3, 0,5 y 0,2) salen del pseudo-C de `0x4ecc30` y del binario, y son las del 06. Lo que
  cambió del contrato de arriba:
  - **T no sale del primer vértice del pase 120.** El 120 da T módulo 10.000 (t), y el 141 necesita
    T entero (c = cos(0,001·T), s = sin(0,002·T)). Ajustando c y s sobre el 141 y buscando el T que
    los da, en la selección T es 26.437.420 ms en el primer swap, y los cinco quedaron en el 06; el
    golden los usa de ahí.
  - **Las normales no llevan la escala del objeto**, lo que el PR A dejó para acá: con ellas, las UV
    de los tres pases caen a 0,00003 de las de la traza, y escaladas por 0,9 el pase 120 queda a
    0,049. El golden pide menos de 0,001, no 0,02, y que cada malla `Male17` tenga sus tres pases y
    el pantalón 02 ninguno (06: los de un nivel 13).
  - El golden va en `test_golden_skinning_trace.cpp` (`GoldenShineTrace` es la misma clase), y no en
    un `test_golden_shine_trace.cpp` aparte: usa el mismo lector, las mismas partes y la misma pose.
  - Un reloj negativo cuenta 0.

  Los 10 mutantes a mano (los tres de arriba, la normal con la escala del objeto, el 0,2 del pase
  `0x8`, la v del `0x4` con t, el reloj en un float, t sin el módulo, la v del `0x1000` sin el 3t y
  el reloj negativo) los mata al menos un test cada uno.

  Revisión adversarial (2026-11-03, resuelta por Claude): los mismos dos revisores. Las tres
  fórmulas, sus floats, la precedencia de las banderas y el reloj de la traza quedaron confirmados;
  el revisor del binario, con su propio skinning, también vio las UV a menos de 0,00005 con las
  normales sin la escala y a 0,05 con ellas. El golden del brillo se partió en `CompareShine` y usa
  el esqueleto con la cabeza y las partes del de la pose (regla 1).

  A fondo, después (2026-11-03, resuelto por Claude): las dos escenas de Lorencia, con la cabeza.
  El personaje de Lorencia seguía con los frames de antes en el 06; con la pose de MuRet son
  1,801 · 1,401 · 0,721 · 0,041 · 5,361, y su lugar y el del guardia son centros de celda,
  (14.350, 12.050) y (14.350, 12.850), así que el golden ya no ajusta la posición del guardia. Los
  cascos caen a 0,0014 con los ángulos del giro de `0x505de0` que leyó la CL6.R (un 20 % por paso
  hacia un objetivo; el del guardia, (−18, 4)°), lo que verifica ese giro y que corre una vez por
  paso y no por frame. `MaskHelmMale10` no se dibuja: el 06 lo contaba entre las partes del
  guardia. Los goldens: `GoldenSkinningTrace.MatchesThePlayerOfLorencia` y
  `MatchesTheGuardOfLorencia`, ahora con el casco, y en `GoldenShineTrace` uno por personaje:
  `MatchesThePassesOfTheSelection` (antes `MatchesTheThreePassesOfTheSelection`),
  `MatchesThePassesOfThePlayerOfLorencia` y `MatchesThePassOfTheGuardOfLorencia`, con el T de
  Lorencia del 06. Los cinco mutantes de la cabeza (en y en vez de z, el signo, x y z cruzados, sin
  los ángulos y otro hueso) los matan los de Lorencia; el de la selección, con los ángulos en 0, no
  ve ninguno.

**Qué queda para después:** cuántos pases lleva cada ítem (P-C01 b: la CL6.R lo leyó, es el nivel de
`0x60b3d0`, y va en la CL6.3 con `ItemGlow`), la luz por vértice de `0x4ebc80` (con `+0x44` del
modelo; en la traza el personaje va con un color por malla), los multiplicadores de `0x4f9580`
(CL7), los efectos, y el giro de la cabeza de `0x505de0` (el objetivo al azar y un 20 % por paso),
que la traza de Lorencia ya verifica; quién le pone el objetivo al personaje propio, sin leer
(P-C14). De la revisión, para la CL6.3: la pose con su séptimo argumento en 1 (la raíz que dejó
quien llama en `0x68c4a1c`, como cuelgan las armas, la mascota y las alas en `0x509340`), la escala
de los vértices de `0x6c881c` (1,2 y 1,4 en los modos 2 y 3 del contorno), y el contorno del
personaje bajo el mouse, que para un jugador hace `0x60cf70` por parte con otros valores que los de
`0x5ffdf0` (Q-C94 y 06 describen los de `0x5ffdf0`: verificar).

## Sub-ola CL6.3: `CharacterSelectScene` con preview 3D y los paquetes de selección

**Concepto:** de la lista de personajes que manda el GameServer a la pantalla de la selección, y
de la elección a la entrada al mundo. Un pase 3D con su viewport, su cámara y su profundidad, con
el 2D de la CL5 antes y después; los personajes armados con su clase y su equipo; el picking con
un rayo; y la sesión con tres estados más. Va en tres PRs: **PR A**, los paquetes y la sesión;
**PR B**, el pase 3D (la cuenta, el lote, el renderer de GL y el lienzo); **PR C**, las escenas
(la carga lee los modelos, la selección, la carga del mundo y el juego mínimo), la apariencia y el
picking. El B no depende del A, y el C va sobre los dos.

**Contrato** (04 § El flujo de conexión; 02; 06 § Cámara y § Cada frame de una escena; la tabla
de arriba; lo que diga la CL6.R):

| Qué | Original | MuRet |
| --- | --- | --- |
| pedir la lista | `0x634c60` pasa a la escena 4 y manda `C1 04 F3 00` con el XOR32 en el mismo paso (`0x634dce`, `0x634e0e`) | `CharacterSelectScene::Enter` llama a `ServerSession::RequestCharacterList` (`WriteCharacterListRequest`, `Xor32C1`) |
| la lista | `0x4c20a0`: `pkt[4]` las clases que se pueden crear, `pkt[5]` dos números, `pkt[6]` la cantidad, una entrada de `0x22` bytes desde `pkt[7]`; no mira el largo | `ReadCharacterList`: `TooShort` si la cantidad no entra en el paquete (diferencia nombrada) |
| antes de la lista | `0x6342c0` y `0x634680` salen con el estado antes de `0x33`, y `0x637700` no hace el `SwapBuffers`: queda el último frame del login | con sesión, `Step` y `Render` no hacen nada hasta la lista, y el frame sale con el clear del `GlFrameTarget` (diferencia nombrada); sin sesión, los fondos |
| la sesión | `0x32` al pedir, `0x33` con la lista, `0x3c` al mandar el `F3 03` | `WaitingCharacterList`, `ChoosingCharacter`, `EnteringWorld` (pregunta 9); el keepalive corre en `LoggedIn` y en los tres |
| los lugares | cinco posiciones y ángulos fijos (`0x4c20a0`): (12.880; 15.046) a 100°, (12.863; 15.251) a 75°, (12.974; 15.425) a 50°, (13.151; 15.533) a 25° y (13.349,6; 15.527) a 0°; z = 163 en cada frame (`0x634680`); un lugar de más de 4 no carga la posición (Q-C101) | `CharacterSlots`, una tabla con su dirección; `ReadCharacterList` descarta una entrada con un lugar de más de 4 |
| la clase | `0x517270(lugar, ((b & 0x10) \| (b >> 4)) >> 1, …)` | `CharacterClassOf(b)` |
| el equipo | `0x5139f0(lugar, los 17 bytes, 0, 0)` | `GameLogic::AppearanceOf` (CL6.R, paso 7) |
| el pase 3D | `0x620db0(0, 25, 640, 430)`: el viewport escalado en enteros (x · W / 640, y · H / 480), la y de GL desde abajo (`0x620d10`), `gluPerspective(fov, w / h, near, far · 1,4)` (`0x6203c0`, `0x6b8c00`), `glRotatef` en y, x (salvo `DAT_07d36899`) y z, `glTranslatef(−posición)`; prende el depth test (`GL_LEQUAL`) y el culling, escribe la profundidad | `Render::MakePass3D`; `GlModelRenderer` pone ese estado |
| la cámara de la selección | `0x635ae0`, escena 4: fov 35 (`0x6d5d48`, que en la `.data` vale 55), near 20 (`0x6d5d40`), far `0x6d5d44` (el `0x4633ff9a` de la escena 4 con `DAT_07d3a0ec` en 0, *estático*; que esa es la rama que corre, *verificado*: en la traza, unos 11.520 antes del × 1,4), z −45°, x −84,5° (`0x6ba7bc` más `DAT_07d325c8`), posición (13.909; 14.482; 543) | `SelectionCamera`, con la dirección de cada valor; *verificado* en la traza (1024 × 768: `glViewport(0, 40, 1024, 688)`) |
| la mezcla de una malla | `0x4ecc30`: opaca (`0x620980`) o normal con alpha test y sin culling (`0x620a00(1)`, una textura de cuatro componentes), aditiva sin escribir la profundidad (`0x620a90`), sin profundidad con `0x2000` | `ModelDrawState`, del `ModelBatch` |
| el color | `glColor3fv` del modelo (`+0x48`), de más de 1 en la traza; la función fija lo recorta. La luz `+0xf4` de cada personaje va en 0, y la del elegido en (1, 1, 1) (`0x634680`); entra por `0x50ba30` sumada a 0,4 (0,4 y 1,4), y `0x60b3d0` le aplica el factor del nivel; el pase base pasa además por `0x609640`, sin leer (P-C14) | el shader lo recorta a [0, 1]; la luz, 0,4 y 1,4 por el factor del nivel (`SelectLightTest.ChosenIsBrighter`) |
| el orden | los fondos 2D, el pase que no dibuja, el pase 3D (el terreno, los personajes, los objetos, los efectos), la luz del elegido y su nombre, el 2D y la UI | `CharacterSelectScene::Render` igual, sin el terreno, los objetos ni los efectos (CL7 y la ola de los efectos) |
| elegir | el clic bajo el mouse (`0x59ec80` → `0x59e700`: el segmento de la cámara al far, `0x620480`, contra la caja de cada personaje, alineada a los ejes, con una separación en 6 ejes, `0x622900`; la del jugador, de (−60, −60, 0) a (40, 40, 120), Q-C100; gana el de menor distancia² de la cámara a su origen); sobre ninguno, ninguno elegido (-1) | `Pass3D::RayThrough` (hasta el far) y `SegmentHitsBox`; `ChooseCharacter` sin ninguno lo olvida |
| entrar | Enter (`0x8001`, con `[input + 0x31]` en 0) con uno elegido, o el doble clic sobre uno (un reloj propio de `0x640940` que un movimiento del mouse cancela, Q-C99; lo elige, sin el sonido de Enter, con la bandera `+0x7f5c` de la UI): con el bit `0x1` de `+0x1d8`, el aviso `0x84`; si no, a la escena 3 | `ChooseCharacter(slot)` y `Next()` = `WorldLoading`; `LockedCharacterNotice` |
| la carga del mundo | `0x6356f0`: sube `LSBg01` a `04`, dibuja un frame con las cuatro en un mosaico de 2 × 2 en 800 × 600 (06) y pasa a la 5 | `WorldLoadingScene`: su juego de texturas, un frame, y `Game` en el paso que sigue (un paso más tarde que el original, que lo hace en la misma llamada) |
| el `F3 03` | `0x636290`, el primer paso de la escena 5: `C1 0E F3 03` con el nombre y el XOR32, y el estado `0x3c` | `GameScene::Step` llama a `ServerSession::EnterWorld` (`WriteCharacterSelect`) |
| el log | `> Character selected <%d> %s` con el nombre | el lugar, sin el nombre |

**Archivos:**
- **PR A:** crear `Client/src/Protocol/Character/Opcodes.h`, `CharacterList.h/.cpp`,
  `CharacterSelect.h/.cpp`, `Client/tests/protocol/test_character_list.cpp`,
  `test_character_select.cpp` y, en `support/`, `CharacterPackets.h` (una lista sintética con
  nombres como `"char0001"`). Modificar `Client/src/App/SessionState.h`, `ServerSession.h/.cpp`,
  `Client/tests/app/test_server_session.cpp`, `test_server_session_loopback.cpp`,
  `Client/tests/support/AppPrinters.h` (los estados nuevos y el `PrintTo` de `CharacterEntry`, que
  escribe el lugar y el largo del nombre, como el de `Credentials`), `TestGameServer.h` (contesta el
  `F3 00`), `Client/CMakeLists.txt`, PROGRESS y este plan.
- **PR B:** crear `Client/src/Render/Pass3D.h/.cpp`, `ModelVertex.h`, `ModelBatch.h/.cpp`,
  `Client/src/Render/Gl/GlModelRenderer.h/.cpp`, `Client/tests/render/test_pass_3d.cpp`,
  `test_model_batch.cpp`, `test_golden_pass_3d_trace.cpp`,
  `Client/tests/render_gl/test_gl_model_renderer.cpp`, `Client/src/Scenes/SceneCameras.h/.cpp`
  (`WorldViewport`, `SelectionCamera` y `LoginCamera`), `Client/tests/scenes/test_scene_cameras.cpp`
  y, en `support/`, `RecordingWorldView.h`.
  Modificar `Client/src/Platform/GlFunctionList.h` (`glDepthFunc` y `glDepthMask`),
  `GlFramebuffer.h/.cpp` (una textura de profundidad, `GL_DEPTH_COMPONENT16`, con las funciones que
  ya están), `Client/src/Platform/Window.cpp` (`SDL_GL_DEPTH_SIZE` en 16, el `cDepthBits` de
  `0x4b8e10`), `Client/src/Render/Gl/Shaders.h` (el par del 3D), `GlSpriteRenderer.cpp` (apaga el
  depth test y el culling antes del 2D, como `0x6219b0` → `0x620870`), `GlFrameTarget.h/.cpp`,
  `Client/src/Scenes/SceneCanvas.h`, `IWorldView.h`, `LoginScene.cpp` (arma su pase con
  `WorldViewport` y `LoginCamera()`, la fija, y se lo pasa al `IWorldView`),
  `Client/tests/support/RecordingFrameTarget.h`, PROGRESS y este plan.
- **PR C:** crear `Client/src/GameLogic/CharacterAppearance.h/.cpp`,
  `Client/src/Render/Models/RayPicking.h/.cpp`, `Client/src/Scenes/GameModels.h/.cpp`,
  `CharacterSlots.h`, `WorldLoadingScene.h/.cpp`, `GameScene.h/.cpp`,
  `Client/tests/gamelogic/CMakeLists.txt`, `test_character_appearance.cpp`,
  `Client/tests/render/test_ray_picking.cpp`, `Client/tests/scenes/test_character_select_scene.cpp`
  (crece), `test_world_loading_scene.cpp`, `test_game_scene.cpp`, `test_game_models.cpp`,
  `test_golden_game_models.cpp` y `test_character_select_session.cpp`. Modificar
  `CharacterSelectScene.h/.cpp`, `LoadingScene.h/.cpp` (lee los modelos en su avance 1),
  `TextureSets.h` (los juegos de la selección y de la carga del mundo), `UI/Notices.h/.cpp` (el
  `0x84`), `Input/InputEvent.h`, `InputState.h/.cpp` y `Platform/InputEvents.cpp` (el doble clic),
  `main.cpp` (la fábrica de escenas con las tres), `Client/tests/CMakeLists.txt`
  (`add_subdirectory(gamelogic)`), `Client/README.md`, PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Protocol::Character
{
inline constexpr std::uint8_t CharacterOpcode = 0xF3;
inline constexpr std::uint8_t ListSubcode = 0x00;   // pedir la lista, y la lista
inline constexpr std::uint8_t CreateSubcode = 0x01; // CL6.5
inline constexpr std::uint8_t DeleteSubcode = 0x02; // CL6.5
inline constexpr std::uint8_t SelectSubcode = 0x03; // entrar al mundo
inline constexpr std::size_t NameSize = 10;
inline constexpr std::size_t EquipmentSize = 17;
inline constexpr std::size_t EntrySize = 0x22;

// C1 04 F3 00, con el XOR32 (0x634e0e).
[[nodiscard]] Outgoing WriteCharacterListRequest();
struct CharacterEntry // 0x22 bytes, 0x4c20a0
{
    std::uint8_t slot = 0;                       // [0], de 0 a 4
    std::array<std::byte, NameSize> name{};      // [1..10], sin terminador
    std::uint16_t level = 0;                     // [0xc..0xd] (inferido)
    std::uint8_t flags = 0;                      // [0xe]: 0x1, bloqueado
    std::uint8_t classByte = 0;                  // [0xf]
    std::array<std::byte, EquipmentSize> equipment{}; // [0x10..0x20]
    std::uint8_t guildStatus = 0;                // [0x21], a +0x368 (inferido)
};
inline constexpr std::uint8_t LockedCharacterFlag = 0x01;
struct CharacterList
{
    std::uint8_t creatableClasses = 0; // pkt[4], a 0x622b20
    std::uint8_t notice = 0;           // pkt[5]: las dos mitades del texto 1240
    std::vector<CharacterEntry> characters;
};
[[nodiscard]] Core::Expected<CharacterList, ProtocolError> ReadCharacterList(
    const Network::Packet& packet);
// ((b & 0x10) | (b >> 4)) >> 1: la clase en los bits 0 a 2 y la segunda clase en el 3.
[[nodiscard]] constexpr std::uint8_t CharacterClassOf(std::uint8_t classByte);
// C1 0E F3 03 con el nombre de 10 bytes, con el XOR32 (0x6362b2..0x63638d).
[[nodiscard]] Outgoing WriteCharacterSelect(std::span<const std::byte, NameSize> name);
} // namespace Client::Protocol::Character

namespace Client::App
{
// SessionState suma, después de LoggedIn:
//   WaitingCharacterList, // F3 00 salió (0x32)
//   ChoosingCharacter,    // la lista llegó (0x33); se espera EnterWorld
//   EnteringWorld,        // F3 03 salió (0x3c); su respuesta es de la CL7: hasta ahí, sin tope
class ServerSession
{
public:
    // En LoggedIn, manda el F3 00. En otro estado, un aviso y nada.
    void RequestCharacterList();
    // La lista del último F3 00, desde que llega hasta el próximo Start o la próxima lista.
    [[nodiscard]] const std::optional<Protocol::Character::CharacterList>& Characters() const;
    // En ChoosingCharacter, guarda el de `slot` si está en la lista, y con std::nullopt o un
    // lugar que no está, ninguno (el -1 de 0x6342c0); no manda nada.
    void ChooseCharacter(std::optional<std::uint8_t> slot);
    [[nodiscard]] const Protocol::Character::CharacterEntry* ChosenCharacter() const;
    // En ChoosingCharacter con uno elegido, manda el F3 03 con su nombre. En otro estado, un aviso.
    void EnterWorld();
};
} // namespace Client::App

namespace Client::Render
{
// Lo que 0x620db0 recibe: un rectángulo en 640 × 480 y la cámara.
struct ViewportRect
{
    int x = 0;
    int y = 0;
    int width = 640;
    int height = 480;
};
struct Camera3D
{
    glm::vec3 position{};
    glm::vec3 angles{};   // grados, los de los tres glRotatef
    bool skipXRotation = false; // DAT_07d36899
    float fieldOfView = 35.0f;  // grados
    float nearPlane = 20.0f;    // `near` y `far` son macros de Windows
    float farPlane = 0.0f;      // sin el × 1,4: MakePass3D lo multiplica
};
struct Pass3D
{
    App::PixelSize screen;
    glm::ivec4 viewport{};   // x, y desde abajo, ancho y alto, en píxeles de GL
    glm::mat4 projection{1.0f};
    glm::mat4 view{1.0f};
    // El rayo del píxel (x, y) de la pantalla, con la y desde arriba (0x6203c0 y 0x620370).
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> RayThrough(float x, float y) const;
    // Dónde cae un punto del mundo, en píxeles con la y desde arriba (el nombre del elegido).
    [[nodiscard]] std::optional<glm::vec2> Project(const glm::vec3& world) const;
};
[[nodiscard]] Pass3D MakePass3D(App::PixelSize screen, const ViewportRect& rect,
                                const Camera3D& camera);

struct ModelVertex // lo que sube GlModelRenderer, en este orden
{
    glm::vec3 position{}; // en el mundo
    glm::vec2 uv{};
    Color color{};        // sin recortar: lo recorta el shader
};
enum class ModelBlend : std::uint8_t
{
    Opaque,   // 0x620980
    Alpha,    // 0x620a00(1): sin culling y con alpha test
    Additive, // 0x620a90: sin culling y sin escribir la profundidad
    // 0x620b10 y 0x620c90 suman su valor si la CL6.R los ve en el dibujo de un modelo (paso 6).
};
struct ModelDrawState
{
    std::optional<TextureSlot> texture;
    ModelBlend blend = ModelBlend::Opaque;
    bool depthTest = true; // false con la bandera 0x2000 (0x620870)
};
struct ModelDrawCommand
{
    ModelDrawState state;
    std::size_t firstVertex = 0;
    std::size_t vertexCount = 0; // de a 3
};
// El par del SpriteBatch para el 3D de un pase: triángulos en el mundo y sus tramos, en orden.
class ModelBatch
{
public:
    void Begin(const Pass3D& pass);
    void SetState(const ModelDrawState& state);
    void Triangle(const std::array<ModelVertex, 3>& corners);
    [[nodiscard]] const Pass3D& Pass() const;
    [[nodiscard]] std::span<const ModelVertex> Vertices() const;
    [[nodiscard]] std::span<const ModelDrawCommand> Commands() const;
};
} // namespace Client::Render

namespace Client::Render::Gl
{
// El shader del 3D, su VAO y su VBO: sube el lote una vez por pase y dibuja cada tramo con el
// estado de 0x620db0 y el de su mezcla. Se mueve y no se copia, y necesita el contexto vivo.
class GlModelRenderer
{
public:
    [[nodiscard]] static Core::Expected<GlModelRenderer, std::string> Create(
        const Platform::GlFunctions& gl);
    void Draw(const ModelBatch& batch, const TextureTable& textures, Core::Log& log);
};
} // namespace Client::Render::Gl

namespace Client::Scenes
{
// SceneCanvas suma: Render::ModelBatch& models; std::function<void()> flushModels;
// IWorldView::Draw pasa a Draw(SceneWorld world, const Render::Pass3D& pass, float alpha).

// Los modelos del personaje (0x60dbe0) y de los ítems que lleva (0x60ff50), con sus texturas, que
// la carga lee en su avance 1 (0x61ccb0) y viven en main.cpp, como GameData. Las texturas se suben
// una vez y quedan, como en el original.
class GameModels
{
public:
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot,
                                                         Render::ITextureStore& textures,
                                                         Core::Log& log);
    [[nodiscard]] const Render::Models::Skeleton* Player() const;
    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const;
};
struct CharacterSlot
{
    glm::vec2 position{};
    float angle = 0.0f; // grados, alrededor de z
};
inline constexpr std::array<CharacterSlot, 5> CharacterSlots = {/* los cinco de 0x4c20a0 */};
inline constexpr float CharacterSlotHeight = 163.0f; // 0x634680, en cada frame
// El pase 3D de las dos escenas: 0x620db0(0, 25, 640, 430) en 0x6350b0 y en 0x634680.
inline constexpr Render::ViewportRect WorldViewport{0, 25, 640, 430};
// Las fijas de 0x635ae0 en la escena 4 y en la 2, con la dirección de cada valor. Las rutas de
// 0x633920 corren en la lógica, pero 0x635ae0 las pisa antes de que lleguen a GL.
[[nodiscard]] Render::Camera3D SelectionCamera();
[[nodiscard]] Render::Camera3D LoginCamera();
// En TextureSets.h: los 12 de 0x61ca30 (0xc a 0x13, 0x717, 0x718, 0x719 y 0x729) y los cuatro
// LSBg de 0x6356f0 (0x71e a 0x721).
//   inline constexpr std::array<Data::Textures::TextureSetEntry, 12> SelectionTextureSet{…};
//   inline constexpr std::array<Data::Textures::TextureSetEntry, 4> WorldLoadingTextureSet{…};

class CharacterSelectScene final : public IScene
{
public:
    // `data`, `models` y `session` tienen que vivir más. `session` es nullptr sin servidor: la
    // escena dibuja los fondos y no pide nada.
    CharacterSelectScene(const SceneContext& context, const GameData& data,
                         const GameModels& models, App::ServerSession* session);
    // Como 0x633f20: su juego de texturas (0x61ca30), ninguno elegido, y el F3 00.
    Core::Expected<void, std::string> Enter() override;
    // 0x6342c0: el clic elige, Enter o doble clic entra; el avance de cada personaje.
    void Step(const Input::InputState& input) override;
    // 0x634680 sin el terreno, los objetos ni los efectos.
    void Render(SceneCanvas& canvas, float alpha) override;
    // Suelta sus slots (0xc a 0x1d y 0x6f8 a 0x71d).
    void Leave() override;
    // WorldLoading después de entrar; Login si la sesión falló.
    [[nodiscard]] std::optional<SceneId> Next() const override;
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
};
// La escena 3: sube WorldLoadingTextureSet, dibuja un frame y pide Game; suelta sus cuatro.
class WorldLoadingScene; // WorldLoadingScene(const SceneContext& context)
// La escena 5 mínima: en su primer paso, ServerSession::EnterWorld; no dibuja nada hasta la CL7.
class GameScene; // GameScene(const SceneContext& context, App::ServerSession* session)
} // namespace Client::Scenes

namespace Client::UI
{
inline constexpr NoticeId LockedCharacterNotice = 0x84; // 0x6342c0, con los textos de la CL6.R
} // namespace Client::UI

namespace Client::Render::Models
{
struct Box // la de un personaje, en el mundo (CL6.R, paso 7)
{
    glm::vec3 min{};
    glm::vec3 max{};
};
// 0x622900: si el segmento de `origin` a `end` (el far de 0x620480) toca la caja, por separación
// en 6 ejes (las 3 aristas y su producto con el segmento, 0x6227b0). El más cercano lo elige
// 0x59e700 por la distancia² de la cámara al origen del personaje, no por dónde toca.
[[nodiscard]] bool SegmentHitsBox(const glm::vec3& origin, const glm::vec3& end, const Box& box);
} // namespace Client::Render::Models

namespace Client::GameLogic
{
// De la clase y el equipo de la lista a los modelos de cada parte (0x517270 y 0x5139f0).
struct CharacterAppearance
{
    std::uint8_t characterClass = 0;
    std::array<std::uint16_t, 5> bodyParts{}; // el modelo de cada BodyPart
    std::optional<std::uint16_t> rightHand;
    std::optional<std::uint16_t> leftHand;
    std::optional<std::uint16_t> wings;
    std::optional<std::uint16_t> pet;
    std::array<std::uint8_t, 5> bodyPartLevels{}; // para los pases de brillo (P-C01 b)
};
using Equipment = std::span<const std::byte, Protocol::Character::EquipmentSize>;
[[nodiscard]] CharacterAppearance AppearanceOf(std::uint8_t characterClass, Equipment equipment);
} // namespace Client::GameLogic

namespace Client::Input
{
// InputEvent suma `std::uint8_t clicks = 0;` (el de SDL_MouseButtonEvent, antes de `text`), e
// InputState, `[[nodiscard]] bool WasDoubleClicked(MouseButton button) const;`.
} // namespace Client::Input
```

- [x] **Paso 1 (PR A):** los tests de los paquetes, con `SameBytes` sobre bytes armados a mano
  (sintéticos, sin valores del cable): `CharacterListTest.TheRequestIsC104F300`,
  `ReadsTheEntriesInOrder`, `AnEmptyListHasNoEntries` (`pkt[6]` en 0),
  `ACountBeyondThePacketIsTooShort`, `KeepsTheNameBytes` (un nombre de 10 bytes sin terminador),
  `TheClassHasTheSecondClassInBitThree` (los 16 valores de los bits 4 a 7), `TheLockFlagIsBitZero`,
  `ASlotAboveFourIsDropped` (Q-C101);
  `CharacterSelectTest.TheRequestIsC10EF303WithTheName`. El XOR32 sale de `Xor32C1`, que ya está
  probado: el test descifra con `Xor32DecodeBytes` y compara el plano.
- [x] **Paso 2 (PR A):** los de la sesión, con `FakeSocket` y el `TestGameServer`:
  `ServerSessionTest.RequestCharacterListSendsF300`, `TheListMovesToChoosingCharacter`,
  `AnEmptyListAlsoChooses` (el `0x33` con ninguno), `ChooseCharacterSendsNothing`,
  `EnterWorldSendsF303WithTheChosenName`, `EnterWorldWithoutAChoiceWarns`,
  `ChoosingNothingForgetsTheChoice`,
  `TheKeepaliveRunsInTheThreeNewStates`, `WaitingCharacterListHasTheReplyTimeout`,
  `EnteringWorldHasNoTimeout`, `AStartForgetsTheList`; y el loopback,
  `ServerSessionLoopbackTest.FromLoginToEnterWorld` (el GS de prueba contesta el `F3 00` con una
  lista sintética y recibe el `F3 03`). Golden, con `GoldenAndWireDataTest`:
  `GoldenWireCharacterSession.TheRequestsHaveTheShapeOfTheCable` (el `F3 00` y el `F3 03` de MuRet
  contra los del `muproxy.log`, con `SameBytesWithoutValues`: el nombre sale del cable en el test,
  y no se escribe ni se imprime).
- [x] **Paso 3 (PR A, Emanuel, `TODO(human)`, resuelto por Claude):** los paquetes, los estados y
  las tres llamadas.
  Verificar: `ctest --preset linux-x64-debug -R "CharacterList|CharacterSelect|ServerSession"`.
  Los mutantes: la lista sin mirar la cantidad, la clase sin el bit 3, el `F3 00` en claro, el
  `F3 03` desde `ChooseCharacter`, y el keepalive solo en `LoggedIn`.

  Hecho así (PR A, 2026-11-04, resuelto por Claude): `Protocol/Character` (`Opcodes.h`,
  `CharacterList` y `CharacterSelect`) y la `ServerSession` con los tres estados, con 35 tests:
  9 de `CharacterListTest`, 2 de `CharacterSelectTest`, 19 de `ServerSessionTest`,
  `ServerSessionLoopbackTest.FromLoginToEnterWorld`, 2 golden y 2 de los printers. El keepalive
  corre en `LoggedIn` y en los tres con el ritmo que empezó con el login, y la lista se espera con
  `ReplyTimeout`; `AutoLogin` y `LoginScene` suman los tres estados donde estaba `LoggedIn`. Lo que
  cambió del contrato de arriba:
  - **`IsLocked(entry)` y `MaxCharacterSlot`**, una función y una constante con nombre en lugar de
    la máscara y el 4 sueltos en quien los usa.
  - **El `F3 00` lo contesta el GS de `support/FakeServers`**, no `TestGameServer.h`, que no tiene
    un servidor (son las claves y el descifrado): después del login acepta el `F3 00` en claro, una
    vez, contesta una lista en claro con el `0xAA` de FoxAnti, y guarda el `F3 03`.
  - **El golden es `GoldenCharacterPackets`** (`protocol/test_golden_character.cpp`), con
    `WireDataTest` y no `GoldenAndWireDataTest`: los `F3` son C1 con el XOR32 y no hace falta el
    `Data\` ni las claves. Compara los 7 `F3 00` y los 7 `F3 03` del `muproxy.log` con los de MuRet
    (`TheRequestsHaveTheShapeOfTheCable`) y lee las listas del GS con `ReadCharacterList` sin que
    descarte ninguna (`TheListsOfTheCableRead`), algo que el plan no pedía.
  - **El `PrintTo` de `CharacterEntry` va en `ProtocolPrinters.h`**: GoogleTest lo busca por ADL en
    `Protocol::Character`. Sin él, una entrada que falla sale con los bytes del nombre (el test lo
    vio en rojo). `AppPrinters.h` suma los tres estados.
  - **Tests de más:** lo que llega o se llama fuera de turno (pedir, la lista, elegir y entrar), la
    lista que no se lee, otro subcódigo, perder el GS eligiendo y el log sin los nombres.

  Los 14 mutantes a mano (los cinco de arriba, el `F3 03` cifrado, el nivel en big-endian, sin
  descartar el lugar de más de 4, el keepalive que vuelve a empezar al cambiar de estado, un lugar
  que no está que queda elegido, `Start` sin olvidar la lista, la lista sin tope, la lista aceptada
  fuera de turno y entrar sin mirar el estado) los mata al menos un test cada uno.
- [x] **Paso 4 (PR B):** los tests sin GL: `Pass3DTest.ScalesTheViewportInIntegers` (`(0, 25, 640,
  430)` en 1024 × 768 da `glViewport(0, 40, 1024, 688)`, y en 800 × 600, `(0, 32, 800, 537)`: la
  y escalada es 31 y el alto 537, con la división entera, y la de GL, 600 − 31 − 537),
  `TheGlYIsFromTheBottom`, `TheAspectIsThatOfTheViewport`,
  `TheFarIsOnePointFourTimesTheCamera`, `MatchesTheMatricesOfTheSelectionTrace` (la proyección y
  la vista de `SelectionCamera` en 1024 × 768 contra los números de la traza que ya están en este
  plan y en 06: cot 17,5° = 3,171595, el aspect 1024/688 y los tres `glRotatef`),
  `SkipXRotationLeavesOnlyYAndZ`, `TheRayGoesThroughThePixel`, `ProjectIsTheInverseOfTheRay`;
  `ModelBatchTest.TrianglesWithTheSameStateShareACommand`, `NeverReorders`,
  `BeginEmptiesTheBatch`. Golden: `GoldenPass3DTrace.EveryPassOfTheSelectionHasItsViewport` (los
  tres `glViewport` y las tres perspectivas de cada frame de la traza).
- [x] **Paso 5 (PR B):** los de GL, con Xvfb (`ctest -L gl`):
  `GlModelRendererTest.DrawsATriangleWithDepth` (dos triángulos que se cruzan: en cada píxel, el más
  cerca), `CullsTheBackFaces` (el de atrás no sale con `Opaque` y sí con `Alpha`),
  `TheAdditiveDoesNotWriteTheDepth`, `ClampsTheColorToOne` (un color de 1,34 da 255),
  `TheAlphaTestCutsAtAQuarter`, `UsesTheViewportOfThePass`, `TheTwoDAfterTurnsTheDepthOff` (un quad
  del `SpriteBatch` después del pase sale entero, aunque el pase dejó la profundidad escrita),
  `AMissingSlotIsSkippedWithAWarning`.
- [x] **Paso 6 (PR B, Emanuel, resuelto por Claude):** `MakePass3D` con `glm::perspective`,
  `glm::rotate` y `glm::translate` (los tres de §10: dan las matrices de `gluPerspective`,
  `glRotatef` y `glTranslatef`), `ModelBatch`, `GlModelRenderer`, el lienzo con `models` y
  `flushModels`, y el `IWorldView` con el pase. Verificar:
  ```bash
  ctest --preset linux-x64-debug -R "Pass3D|ModelBatch|LoginScene"
  xvfb-run -a ctest --preset linux-x64-debug -L gl
  ```
  Los mutantes: el viewport en float, la y de GL desde arriba, el aspect de la ventana, sin el ×
  1,4, el 2D que no apaga la profundidad, el color sin recortar, y la aditiva que escribe la
  profundidad.

  Hecho así (PR B, 2026-11-04, resuelto por Claude): `Render/Pass3D`, `ModelVertex.h`,
  `ModelBatch`, `Render/Gl/GlModelRenderer` y `Scenes/SceneCameras`, con el lienzo (`models` y
  `flushModels`) y el `IWorldView` con el pase. 34 tests: 10 de `Pass3DTest`, 4 de
  `ModelBatchTest`, 3 de `SceneCamerasTest`, 2 golden, 10 de `GlModelRendererTest`, 2 de
  `GlFrameTargetTest`, `LoginSceneTest.TheWorldGetsThePassOfTheLoginCamera` y los de las
  funciones de GL nuevas en el cargador. Lo que cambió del contrato de arriba:
  - **`Pass3D` guarda el far de la cámara** (`farPlane`, sin el × 1,4), y `RayThrough` va de la
    posición de la cámara al punto del píxel a esa distancia, como `0x620480` (06 § El picking),
    y no hasta el far de la perspectiva.
  - **El golden son dos:** `GoldenPass3DTrace.TheSelectionCameraPassesMatch` (la franja y el mundo
    con `SelectionCamera`, en cada frame: viewport, perspectiva y vista) y
    `TheLorenciaWorldPassMatches` (con la cámara de cada frame de la traza). Cada frame de la
    selección tiene cuatro viewports y no tres: el primero es el `0x620db0(0, 0, 640, 480)` de
    `0x637700`, con la cámara de una ruta, y va antes del `glClear` (06 § Cámara, corregido).
  - **Un slot que la tabla no tiene sale sin textura** (Q-C29, como pide la CL6.R) y no se saltea:
    `GlModelRendererTest.AMissingSlotDrawsUntexturedWithAWarning`. El `ModelBatch` no conoce la
    tabla, así que el test es del renderer y no del lote.
  - **`BeginFrame` prende la máscara de la profundidad antes del `glClear`**, como el primer pase
    del original: la aditiva la deja apagada (`GlFrameTargetTest.EachFrameClearsTheDepth`).
  - **No hay `RecordingWorldView.h`:** el `RecordingFrameTarget` anota el pase con el mundo, y un
    evento `Models` con los tramos y los vértices de cada `flushModels`; un `Present` con modelos
    sin su fin hace fallar el test.
  - **Tests de más:** el orden Y, X, Z con los tres ángulos (las trazas tienen la Y en 0),
    `ModelBatchTest.AStateWithoutTrianglesAddsNothing`, la profundidad apagada (`0x2000`), la
    opaca sin mezcla y el color recortado antes de la textura (1,34 por un gris al 50 % da 128).

  Los 16 mutantes a mano (los siete de arriba, la X antes que la Y, el rayo hasta el far × 1,4,
  `Project` sin dar vuelta la y, el lote que junta tramos que no son seguidos, la opaca que mezcla,
  la normal con culling, el slot que falta que no se dibuja, `GL_GREATER` en lugar de `GL_LEQUAL` y
  el frame sin la máscara antes del `glClear`) los mata al menos un test cada uno; el de la X antes
  que la Y vivió hasta `TheRotationsGoInTheOrderYXZ`.
- [x] **Paso 7 (PR C):** los tests de la apariencia y el picking, con los casos de la CL6.R:
  `CharacterAppearanceTest.WithoutEquipmentWearsTheClassParts` (para cada clase de la tabla de
  `0x517270`, `HelmClass` N a `BootClass` N), `TheSecondClassKeepsTheParts`,
  `EachEquipmentByteGivesItsItem` (los 17 bytes, uno por uno, desde la tabla de la CL6.R),
  `AnEmptySlotIsTheClassPart`; `RayPickingTest.HitsTheBoxInFront`, `MissesBeside`,
  `TheNearestWins`.

  Hecho así (PR C, 2026-11-04, resuelto por Claude): el PR C de arriba se partió en dos. Este es
  **el personaje**, de los bytes de la lista al lote; las escenas, `GameModels`, el doble clic y
  `main.cpp` van en un **PR D**. Lo que entró, con 46 tests:
  - **`GameLogic/CharacterAppearance`** (11 tests): `ClassAppearance` y `AppearanceOf`, campo por
    campo, con la cabeza de los cascos abiertos y la escala de `0x512930`. La piel que pasa la
    lista es 0 (`0x4c20a0` llama a `0x517270(lugar, clase, 0, …)`, en el pseudo-C del lab), y las
    mascotas de `b[11]` pisan en orden, el bit 0 y después el 2. Q-C102: la mano `0xfff` es sin
    ítem.
  - **`Render/Models/RayPicking`** (6 tests): `SegmentHitsBox` con los 6 ejes, `PlayerBoxAt`
    (Q-C100) y `NearestHit`, por la distancia² al origen y no a dónde entra el segmento.
  - **`Render/Models/ItemGlow`** (3 tests) y los modos `0x200` y `0x800` de `Shine` (2 más): los
    pases por nivel de `0x60b3d0` (Q-C97), el recorte por el byte de las opciones, y el `0x42` y el
    `0x840` de excelente y de set. Los niveles 3 a 6 no laten, porque `DAT_07d36ba8` no se leyó
    (P-C18).
  - **`Render/Models/Attachment` y `PoseAttached`** (5 tests): de qué hueso cuelga cada cosa (Q-C98)
    y la pose con la raíz de quien llama. **Verificado** contra la traza
    (`GoldenSkinningTrace.TheSwordAndThePetHangFromTheirBones`): la espada del 33 y la mascota del
    34 con (20, 0, 0) sin la escala, a 0,0014.
  - **`Scenes/CharacterDrawing`** (3 tests y un golden): `DrawCharacter`, el `0x50ba30` de la
    selección, con el slot de la piel y del pelo por clase y la oculta afuera. Con el personaje de
    la traza, `GoldenCharacterDrawing.MatchesTheCharacterOfTheSelection` encuentra en el lote, uno a
    uno, los 35 bloques que el original le mandó a GL por él en cada frame, con sus posiciones y sus
    UV, y nada más. Los colores de los pases son los de las Male17 en la traza; los de otros ítems
    salen de `0x609640`, sin leer (P-C14: en la traza, el 120 lleva tres colores distintos según la
    parte).
  - **Para el PR D:** `GameModels` (la carga de los modelos y sus slots, con
    `ModelTextureSlotsGoldenTest.StartupMatchesTheDump`, Q-C89 y Q-C91), las tres escenas, el doble
    clic de `0x640940` (Q-C99), el contorno bajo el mouse (Q-C94) y el oscurecido de Q-C95, que solo
    se ve con un alpha de menos de 0,99, y la luz 0,4 y 1,4 (`SelectLightTest`).

  Los 17 mutantes a mano (la segunda clase sin su corrimiento, el `0x1ff` que no es vacío, las
  mascotas en otro orden, la tabla de niveles corrida, la escala sin la piel, la cabeza con
  cualquier casco, el picking sin los ejes producto, la caja centrada, el más cercano por dónde
  entra, el brillo sin el recorte, el 13 en otro orden, el desplazamiento sin girar, la mascota del
  33, la oculta que se dibuja, sin los pases, cualquier mascota y las manos sin su nivel) los mata
  al menos un test cada uno.
- [x] **Paso 8 (PR D):** los de las escenas, con `RecordingFrameTarget`, `FakeTextureStore`,
  `FakeClock` y una sesión con `FakeSocket`: `LoadingSceneTest.ReadsTheModelsOnAdvanceOne` y
  `AMissingPlayerFailsTheLoad` (con el nombre del archivo);
  `CharacterSelectSceneTest.EnterRequestsTheList`, `DrawsNothingBeforeTheList`,
  `PlacesEachCharacterInItsSlot` (las cinco posiciones y z = 163), `DrawsInTheOrderOfTheOriginal`
  (los fondos, el pase que no dibuja, el pase 3D, el nombre del elegido, el 2D),
  `AClickChoosesTheOneUnderTheMouse`,
  `AClickOnNothingClearsTheChoice`, `EnterNeedsTheKeyDownAndPressed`, `ADoubleClickEnters`,
  `ALockedCharacterShowsTheNotice`, `EnterGoesToWorldLoading`, `NoCharacterChosenDoesNotEnter`,
  `LeaveReleasesItsSlots`, `TheBodyAdvancesPerStepAndThePetPerTime`, `WithoutASessionOnlyDraws`;
  `WorldLoadingSceneTest.DrawsOneFrameAndGoesToTheGame` y `ReleasesItsFourImages`;
  `GameSceneTest.TheFirstStepSendsF303` y `TheLogHasTheSlotNotTheName` (con `RecordingSink`);
  `InputStateTest.ADoubleClickIsTheSecondClick` y `InputEventsTest.CopiesTheClicks`. Golden:
  `GoldenGameModels.LoadsEveryModelOfThePlayerLoader` y `TheSelectionTextureSetLoads`.
- [x] **Paso 9 (PR D, Emanuel, resuelto por Claude):** la apariencia, el picking, `GameModels`, las
  tres escenas y el doble clic, y en `main.cpp` la fábrica con la selección, la carga del mundo y el
  juego. Los modelos de cada personaje: el esqueleto de `Player.bmd`, sus cinco partes y lo que
  lleva, con la textura de cada malla de `GameModels::MeshTextures`, la mezcla de `0x4ecc30` (CL6.R,
  paso 6) y los pases de brillo de su nivel. Verificar:
  ```bash
  ctest --preset linux-x64-debug -R "CharacterAppearance|RayPicking|Scene|InputState|InputEvents"
  MURET_CLIENT_GOLDEN_DIR=<Data> ctest --preset linux-x64-debug -R "GoldenGameModels"
  ```
  Los mutantes: los lugares en otro orden, la z de la lista y no 163, el clic que elige el primero,
  el clic sobre nada que deja el elegido, la escena que dibuja antes de la lista, Enter con solo el
  flanco, el doble clic de dos botones distintos, el `F3 03` desde la selección,
  y la escena que no suelta sus slots.

  Hecho así (PR D, 2026-11-04, resuelto por Claude): las escenas y la carga de los modelos, con 40
  tests:
  - **`Data/Models/ModelTextureCalls`** (7 tests) y **`Scenes/GameModels`** (5 golden): los 456
    modelos de las dos tablas y sus texturas, que no salen de las tablas sino de las 505 llamadas
    de `0x60f7d0` y `0x611850` a `0x60d5f0`, cada una con su carpeta (los `Helper` de `Player\`
    con las de `Item\`; `0x1c15` a `0x1c17` con las de `Skill\`), repitiendo y sin filtro
    (`0x2901` y `0x2600`), y las 13 fijas de `0x60f7d0` (la piel, el pelo y las cuatro capas). Lo
    encontró la primera corrida de `Main` con `--data`: con la carpeta del `.bmd` faltaban 14
    texturas, entre ellas las de la mascota que se dibuja, y el original se cierra con una que
    falta. **Verificado:** una emulación de los dos cargadores sobre el `main.exe` da las mismas
    505 llamadas, en orden; con el laboratorio no falta ninguna
    (`GoldenGameModels.EveryTextureTheLoadersAskForIsThere`), y
    `ModelTextureSlotsGoldenTest.StartupMatchesTheDump` da los 489 nombres de `lor_slots.bin` de
    `0x12e` a `0x370`, con el próximo en `0x371`, donde sigue `0x615700`.
  - **`Input/DoubleClick`** (4 tests): el reloj de `0x640940` (Q-C99), con los 500 ms justos
    todavía doble (`0x640940` arma con `límite < ahora − primero`).
  - **`Scenes/CharacterSelectScene`** (15 tests): el `F3 00` al entrar, nada antes de la lista (ni
    un clic que cuente para el doble), los personajes en sus lugares con la acción 3, el clic que
    elige con el rayo, Enter y el doble clic que entran, el aviso `0x84` con el bloqueado (y con el
    aviso abierto, nada más), la luz 0,4 y 1,4, y el login si la sesión falla.
  - **`WorldLoadingScene`** (2 tests), **`GameScene`** (3 tests: el `F3 03` en el primer paso,
    una vez, y el login si la sesión falla), `LoadingScene` con los modelos en el avance 1 (1 test
    más) y `main.cpp` con las cinco escenas. Con `--data`, `Main` carga los 453 modelos y sus 504
    texturas y llega al login.

  Los nombres del paso 8 que cambiaron: `AClickOnNothingClearsTheChoice` va dentro de
  `AClickChoosesTheOneUnderTheMouse`; `NoCharacterChosenDoesNotEnter`, dentro de
  `EnterGoesToWorldLoading`; `ReadsTheModelsOnAdvanceOne`, en `AMissingPlayerFailsTheLoad`;
  `InputStateTest.ADoubleClickIsTheSecondClick` es
  `SelectInputTest.TheSecondPressInTimeIsADoubleClick`, e `InputEventsTest.CopiesTheClicks` no hace
  falta (el `InputEvent` no lleva el `clicks` de SDL); `TheBodyAdvancesPerStepAndThePetPerTime`
  queda en los tests de la CL6.2 (`AdvanceAnimation` y `AdvanceByTime`), y
  `TheSelectionTextureSetLoads` en `GoldenTextureSets`. Las decisiones:
  - **Enter con el flanco solo**, sin `EnterNeedsTheKeyDownAndPressed`: pedir además la tecla abajo
    (el `0x8001`) solo pierde un Enter corto, el bug de Q-C45 que MuRet no reproduce, como el login
    (`AShortEnterAlsoEnters`).
  - **El contorno bajo el mouse (Q-C94), el nombre del elegido (`0x4129f0`) y el oscurecido de
    Q-C95 van en un PR E**: son lo que marca a un personaje, y piden leer `0x60cf70` y `0x4129f0`
    antes. La luz de abajo del elegido (`0x53fe70`, la `chasellight`) va con los efectos, y la del
    terreno (`0x5f9780`), con la CL7.
  - **La carga lee todo en el avance 1**: el original reparte los modelos y las texturas entre los
    avances 1 a 4; la barra se queda más en el 1 y pasa más rápido por los otros.
  - Q-C91 (`Logo05` y los `NewFace` desde `0x6f8`) va con la creación (CL6.5); la acción de parado
    es la 3 para todos (P-C14 c); la pose del primer segundo y el tiempo del doble clic, en P-C19.

  Los 31 mutantes a mano (el clic que no elige, Enter con la tecla abajo, el bloqueado que entra, la
  luz, las escenas que no sueltan sus slots, el dibujo y el paso antes de la lista, entrar sin
  elegido, la lista sin pedir, otra acción de parado, el aviso que deja pasar la entrada, el login
  si la sesión falla en las dos escenas, el doble clic sin el movimiento, con menos de 500 y
  triple, el `F3 03` dos veces, la carga del mundo antes de dibujar, la carga que falla y no
  cierra, los modelos en otro avance, `0x1c15` con `Item\`, dos llamadas en otro orden, el lazo
  de `0x412` en otro orden, el casco 23, los modelos con filtro, los ítems sin su `0x60d530` y sin
  las fijas): los mata al menos un test cada uno, salvo uno equivalente (la segunda llamada a un
  modelo que no pisa la primera: encuentra los mismos nombres y da los mismos slots).

  Hecho así (PR E, 2026-11-04, resuelto por Claude): el contorno bajo el mouse, con 10 tests. Leído
  en el binario: para un jugador lo hace `0x60cf70`, no `0x5ffdf0`. Antes de cada una de las seis
  partes (la cabeza también; lo que cuelga, no) dibuja dos pases de `0x60cd60`, aditivos, sin
  textura ni máscara: en (0,1; 0,03; 0) con los vértices a 1,2 de su hueso (`objeto + 0x94`) y en
  (0,7; 0,2; 0) a 1,18. Entraron `SkinMeshAroundBones` (el `DAT_006c881c` de `0x4ebc80`, 2 tests),
  `DrawnCharacter::outlined` (6 tests: los dos pases antes de todas las mallas de la parte, sin la
  OZT ni la oculta, y sin lo que cuelga) y, en la selección, el de bajo el mouse del último paso,
  con nadie mientras el aviso está abierto (2 tests). Lo demás del PR E del PR D:
  - **El nombre del elegido no se dibuja**: `0x4129f0` es la asignación de un string (copia el
    nombre en el objeto de `0x6eed7c`), así que `0x634680` no dibuja ningún nombre.
  - **Q-C95 va con el primer modelo que se desvanezca** (CL9): en la selección nada lo hace, y el
    personaje de MuRet no tiene alpha.
  - **Para un PR F de la CL6.3**, leído al leer el contorno: `DrawCharacter` no dibuja el script de
    las texturas (el `R` del `blendMesh`, aditivo con su luz; el `H`; el `S`, que corre las UV; y el
    `N`, sin brillo, más las cinco mallas que `0x60ff50` marca), ni la máscara de la clase 4 (los
    cascos `0x1012`, `0x1017`, `0x1018`, `0x101a` y `0x101b` van con el modelo + `0x328b`, con la
    luz del `blendMesh` latiendo en sin(0,001 · T) · 0,1 + 0,7; 06 § La clase 4 en el lazo de las
    partes).

  Los 17 mutantes a mano (el contorno a 1, sin el 0,02, los colores cruzados, con textura, opaco,
  después de la parte, con lo que cuelga, sin la cabeza, con la OZT, con la oculta, la escala en la
  traslación, el aviso que lo deja, el del elegido y el de todos) los mata al menos un test cada
  uno, salvo uno equivalente: con la escala en 1, el otro camino da lo mismo.
  La revisión adversarial de la CL6.3 (2026-11-04), con un revisor del C++ y otro contra el binario,
  arreglada sobre el PR E con un test que falló primero cada vez:
  - **Las texturas de los pases de brillo no se subían** (`0x5fc`, `0x5c0` y `0x620`, que
    `0x61ccb0` sube en su avance 0): un ítem de +7 o más salía con un lavado parejo, y de +11 una
    silueta blanca. Ahora las sube `GameModels` (`GlowTextureSet`;
    `TextureSetsTest.TheGlowSetIsTheOneOf0x61ccb0` y el golden de `GameModels`).
  - **Una falla de la sesión en la selección volvía al login sin aviso**: el original abre el
    `0x70` (Q-C70) y su OK cierra el juego (Q-C69), y en el juego sigue sin conexión
    (`AFailedSessionShowsTheConnectNoticeAndItsOkQuits`, `AFailureBeforeTheListShowsTheNoticeToo`
    y `GameSceneTest.AFailedSessionKeepsTheGame`).
  - **Un `.bmd` roto se leía fuera** al dibujarlo (un índice de un triángulo sin su vértice, su
    normal o su UV): esa malla no se dibuja (`AMeshThatNamesWhatItLacksIsNotDrawn`).
  - Dos de las reglas: el `.cpp` del login con su header primero, y `PlayerSkeletonModel` en vez
    de un `0x20a` repetido.

  Y la del binario, arreglada en tres commits más, con un test que falló primero y 7 mutantes:
  - **La sombra** (`0x20b`, 0x50ba30 y 0x4eff40): negra, opaca, sin textura ni profundidad, y
    aplastada sobre el piso. **Verificado:** `GoldenCharacterDrawing` encuentra ahora 36 bloques por
    frame; el de la sombra es el de 318 vértices en z = 168 de la traza. Con esto, el "uno a uno"
    del PR C era de 35 bloques de 36, más los 14 del contorno, que MuRet no dibujaba entonces.
  - **El rayo del mouse** se estira en y por el alto de la pantalla sobre el del viewport, como
    `0x6203c0` (Q-C109): la zona que elige a cada personaje queda donde la del original.
  - **Los colores de los pases de brillo**, con las tablas de `0x608e00`, `0x6093d0` y `0x609560`
    emuladas para cada modelo. Un +13 sin elegir brillaba 2,8 veces de más. El 1,35 de la traza
    es la luz del "héroe" por 0,9. El excelente late y no va con el set, y lo que cuelga no lleva
    el set.
  - **La luz del lugar 0**, el "héroe" (`DAT_0743a170`), es 0,5 y no 0,4, y **las alas** avanzan
    en la selección.
  - **Las saetas, las flechas y el cuervo** no van en la mano. Que las dos primeras vayan a la
    espalda (`0x509340` con su bandera y `local_20`) va en el PR F; el cuervo como objeto aparte
    (`0x420cf0`) va con los efectos o las mascotas (CL8), como dice su fila de la CL6.4.
  - Quedan para después (minors): el orden de las partes (de las botas a la cabeza), de lo que
    cuelga y de los pases (por pasada y no por malla); la capa (`0x1c30`, una tela); el `F3 03`
    con `strlen` y ceros; y una textura de modelo que falta, que el original no tolera (Q-C23).

  Hecho así (PR F, 2026-11-04, resuelto por Claude): lo que el PR E dejó del dibujo, leído en
  `0x4ef0c0`, `0x4ecc30`, `0x60cd60`, `0x60b3d0`, `0x60b130`, `0x60b300`, `0x50ba30`, `0x509340`,
  `0x511290` y `0x60ff50` (06 § El script en un personaje, § Los pases de brillo, § Cuántos pases y
  § Lo que cuelga del personaje). Todo *estático*: ninguna traza lo tiene (P-C20).
  - **El script en el dibujo:** la `H` no se dibuja; la `R`, con la textura de su índice, es el
    `blendMesh`, aditivo con su textura y por la luz del `blendMesh` en el base, el `0x42` y el
    contorno; con `N`, o en las cinco mallas que marca `0x60ff50` (`MeshesWithoutShine`), sin pases
    de brillo; la `S` no hace nada en un jugador (U y V en 0). Lo que comparten los modelos de un
    personaje en el frame va en `CharacterFrame`.
  - **La clase 4:** la luz del `blendMesh` late (sin(0,001 · T) · 0,1 + 0,7) y los cascos 1, 6, 7,
    9 y 10 van con su `MaskHelmMale` (`HelmsWithMask`, `MaskHelmOffset`).
  - **Los casos por modelo de `0x60b3d0`** que alcanza un personaje: la malla que ocultan en sus
    pases `0xc17`, `0x226`, `0x227` y `0x619` (`PassHiddenMesh`), el cromo de `Spear10` antes de
    sus pases (por el tipo del objeto: en lo que cuelga, el de su ítem; `ModelUse`) y el set, que
    va con el alpha sin(0,001 · T) · 0,5 + 0,4 y se apaga malla a malla (Q-C95, `SetAlphaAt`). Esto
    último corrige el PR E, que dibujaba el set sin ese alpha.
  - **El carcaj:** las saetas de la izquierda o las flechas de la derecha, a la espalda del hueso de
    las alas por `AngleMatrix(70, 0, 90)` y (−10, 5, 10) (`PlacedRoot`, `QuiverPlacement`), con el
    nivel 2L + 1 (`QuiverGlowLevel`), el excelente de la mano y sin el set.
  - **Tras la revisión adversarial** (dos revisores, el C++ y el binario): lo que cuelga lleva el
    objeto que `0x509340` le arma con `0x607b60` (`Render/Models/ObjectState`: el `blendMesh`, en −2
    todas las mallas, la luz que late o de `rand()`, la U y la V y la malla que no dibuja el pase
    base), con la luz que `0x60b3d0` les escribe a `Wing05`, `Wing06` y `Staff12`; la luz del
    personaje la escriben también `PantMale25` y `HelmMale25` y la vuelven a 1 las `Test20`, y queda
    de una parte a la siguiente, con el contorno antes de que `0x60b3d0` escriba la suya; el `0x240`
    y el `0x1040` de una malla del `blendMesh` no van por su luz; `0x60b130` pasa el `+0x58` del
    objeto con `0x231`, `0x81c`, `0xe22` y `0x61a` a `0x61f`; el chrome de `Spear10` corre su V, y el
    `0x1040` lleva la U y la V del objeto; y el orden es el del original: las partes de la 5 a la 0,
    la espalda, las alas, la mascota y las manos, y cada modelo pase por pase (`RenderBody`), lo que
    verifica `GoldenCharacterDrawing` contra la traza.
  - **Decisiones:** el cuervo va en la CL8 (la fila del cuervo de la CL6.4 y P-C15 c); la luz del
    personaje empieza cada frame en la que dejó el último que la escribe, con el reloj de este (el
    original la guarda en el objeto: difiere al sacarse la parte y en la diferencia de un frame); los
    `rand()` de `0x607b60` salen de un `std::minstd_rand` con el reloj de semilla en cada frame; el
    carcaj se posa con la acción 0 y el frame 0 (sus dos modelos tienen una acción de una key); y
    `local_20` queda en 0, sin la zona segura (CL7).
  - **Tests:** 54 nuevos (38 del dibujo, 7 de `ObjectState`, 3 de las tablas de `PlayerModels` y su
    golden, 3 de `PassColors`, 2 de `Attachment` y 1 de `ItemGlow`); el de las manos, dos del dibujo
    y uno de `PassColors`, reescritos; y el orden, en `GoldenCharacterDrawing`. 67 mutantes a mano, todos en rojo: 40
    antes de la revisión y 27 después. Tres vivían hasta sus tests (el contorno del `blendMesh` sin
    la luz, la máscara en otra parte que el casco y un `rand()` fijo), y doce no compilaban hasta
    usar el parámetro que dejaban sin uso.

- [ ] **Paso 10 (a mano):** contra el GS del laboratorio, con `--data`: el login aceptado, la
  selección con los personajes de la cuenta de prueba animados en sus lugares, el clic, el doble
  clic, la carga del mundo y el `F3 03`; el log del GS muestra la entrada. Sin capturas en Git.
  Cómo correrlo, paso por paso, en el
  [README del cliente](../../../Client/README.md#contra-el-servidor-del-laboratorio-a-mano), con la
  prueba de cancelar en la cuenta; qué falta y qué se manda, en P-C21 del
  [08](../../../Client/docs/research/08-pendientes.md). Sigue sin correr (2026-11-04).

**Qué queda para después:** el terreno y los objetos del mapa 54 (CL7, en el mismo pase), la
respuesta del `F3 03` (`0x4c3940`, CL7), la UI del modo 4 con crear y borrar (CL6.5), el texto
1240 (CL6.5), y la luz de abajo del elegido si la CL6.R no la lee entera: es un `0x53fe70`, el
dibujo de un efecto, y va con los efectos.

## Sub-ola CL6.4: el logo y el personaje del login

**Concepto:** lo que la CL5 le dejó al login, con lo que ya sirve para la selección: un modelo
suelto con su objeto (el logo), un personaje armado desde una tabla y no desde la lista, y un
número al azar, en una ventana del reloj, que el test fija. La cámara es la fija de la CL6.3
(`LoginCamera`). Va en un PR.

**Contrato** (06 § El login, al entrar y § El login, el dibujo; la tabla de arriba; lo que diga la
CL6.R, paso 9):

| Qué | Original | MuRet |
| --- | --- | --- |
| el logo | `0x61c7c0`: `0x60da30(0xa2, "Data\\Logo\\", "Logo", 3)` y `0x60d5f0(0xa2, "Logo\\", 0x2901, 0x2601, 1)`, sin un `0x60d530` antes: sus dos texturas van al próximo que dejó el mundo 55, `0x41c` más las 57 nuevas de `Object56\`, o sea `0x455` y `0x456` (Q-C92; *inferido*, P-C15). La salida del login (`0x61c9f0`) no las suelta | `LoginScene::Enter` con `LoadModelFile` y `ModelTextureSlots` desde `LoginLogoFirstSlot` (`0x455`, un dato con su cuenta mientras la CL7 no cargue el mundo 55; la CL7 lo saca de la carga); se suelta en `Leave`, con la diferencia nombrada (Q-C92) |
| su objeto | `0x606660(0xa2, …)`: (10.680; 15.550; 1.710), ángulos (10, 0, 36) y escala 1, que `0x606660` pisa en las escenas 2 y 4 (`0x6068d5`): **escala 2,2**, el `blendMesh` en 1 (la malla de la textura 1 va aditiva) y sin luz por vértice (`+0xe8` en 0); antes esta fila decía escala 1 | `LoginLogo`, una tabla con su dirección, con el `blendMesh` y sin luz por vértice |
| su luz | `0x604ab0` (`0x604cdb`): con la fase de las rutas (`0x7d3a0cc`) en 0, la cuenta (`0x6d5e30`) · 0,02, de 0 a 0,78 en 40 pasos; con la fase en 1 (o cualquiera que no sea 0), 1,5 (Q-C105). Va a `+0xf4..+0xfc` y a `+0x68`, la luz del `blendMesh` | `LoginLogoLight(count, phase)`, con la cuenta y la fase que lleva la escena como `0x633920`, aunque la cámara no use las rutas |
| el personaje | `0x517270(2, 4, …)`, las partes `0x102e`, `0x122e`, `0x142e`, `0x162e` y `0x182e` en +7, el arma `0x620` en +13, y `0x1c17`, `0x1c30` y `0x1c16`; escala 1,6, ángulos (0, 0, 188), posición (10.603; 14.112; 580) | `LoginCharacter`: una `GameLogic::CharacterAppearance` armada desde la tabla (no desde bytes de la lista) y su objeto |
| el cuervo | `0x420d60`: si la mano izquierda es `0x1c17`, `0x421d60` y un objeto de `0x460` bytes (`0x4167b0`) en `+0x430`, con su propio paso, sin leer (P-C15) | se nombra y no se dibuja: va con los efectos o con las mascotas (CL8) |
| su acción | `0x634c60`, en cada paso: con el reloj del frame (`DAT_05767b88`, de `0x4eb620`, Q-C22) en los primeros 100 ms de cada 2.300, `rand() % 6` en 1 pone la `0x54` y en 2 la `0x55` (`0x4ea030`); los otros cuatro, y fuera de esa ventana, nada | con un `std::mt19937` de la escena, sembrado desde afuera como el de la `Connection`: el test lo fija con `FixedSeed`; la ventana, con el reloj de `SceneCanvas::frameClock`, que `IScene::Step` hoy no recibe y la CL6.4 le pasa |
| la cámara | `0x635ae0` en la escena 2, fija: z −36°, x −96,5° (`0x6ba7c0` más `DAT_07d325c8`), posición (12.778; 12.678; 579), far 20.400 (`0x469f6000`, con `DAT_07d3a0ec` en 0); las rutas de `0x633920` (en la escena 2, una de las 1 a 4 al azar) quedan pisadas | `LoginCamera`, de la CL6.3; nada nuevo |
| el efecto `0xbf` | `0x443fa0` | no: la ola de los efectos |
| el pase | el 3D de `0x6350b0`, entre los fondos y la arena | el mismo pase de la CL5.4: el `IWorldView` y después los dos modelos |

**Archivos:** crear `Client/src/Scenes/LoginModels.h/.cpp` (el logo y el personaje como datos y su
dibujo) y `Client/tests/scenes/test_login_models.cpp`. Modificar `LoginScene.h/.cpp`,
`Client/tests/scenes/test_login_scene.cpp`, `test_golden_login_scene.cpp`, `main.cpp` (la escena
recibe `GameModels` y su semilla), PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Scenes
{
struct ModelObject // un objeto suelto del original: el modelo y dónde va
{
    std::uint16_t model = 0;
    Render::Models::ObjectTransform transform;
};
inline constexpr std::uint16_t LoginLogoModel = 0xa2;
// El próximo que deja el mundo 55: 0x41c (0x61a0e8) más las 57 texturas nuevas de la carpeta
// Object56 (Q-C92; inferido hasta la captura de P-C15).
inline constexpr Render::TextureSlot LoginLogoFirstSlot{0x455};
[[nodiscard]] ModelObject LoginLogo();                        // 0x606660
// 0x604ab0: la luz del logo y de su blendMesh, con la cuenta y la fase de las rutas (Q-C105).
[[nodiscard]] float LoginLogoLight(int routeCount, int routePhase);
[[nodiscard]] GameLogic::CharacterAppearance LoginCharacter(); // 0x517270(2, 4, …)
[[nodiscard]] Render::Models::ObjectTransform LoginCharacterTransform();
// LoginCamera() (CL6.3, PR B) ya es la del login: fija, como 0x635ae0 en la escena 2.
// LoginScene suma, en el constructor, `const GameModels& models` y
// `std::mt19937::result_type seed`.
} // namespace Client::Scenes
```

- [x] **Paso 1:** los tests: `LoginModelsTest.TheLogoIsModel0xA2AtItsPlace`,
  `TheCharacterIsClassFourWithItsSet` (las cinco partes, el arma, el cuervo, la capa y la mascota,
  por número), `TheActionOnlyChangesInItsWindow` (fuera de la ventana de cada 2.300 ms del reloj,
  nunca; adentro, con `FixedSeed`, las dos salen en 100 pasos y en otros se queda la que tenía),
  `TheLogoTexturesStartWhereTheWorldLeft` (Q-C92), `TheLogoIsScaled2_2WithBlendMesh1`,
  `TheLogoLightJumpsWhenThePhaseChanges` (Q-C105); `LoginSceneTest.DrawsInTheOrderOfTheOriginal`
  (crece: el pase 3D tiene el logo y el personaje después del `IWorldView`), `LeaveReleasesTheLogo`;
  y el golden `GoldenLoginScene.TheLogoLoads`.
- [x] **Paso 2 (Emanuel, `TODO(human)`):** `LoginModels` y lo que suma la escena. Verificar:
  ```bash
  ctest --preset linux-x64-debug -R "LoginModels|LoginScene|TerrainLighting"
  MURET_CLIENT_GOLDEN_DIR=<Data> ctest --preset linux-x64-debug -R "GoldenLoginScene|GoldenTerrainLighting"
  ```
  Los mutantes: el logo en grados por radianes, el personaje sin la escala 1,6, una acción fija, la
  tirada en cada paso sin la ventana del reloj, y los slots del logo desde 0.

  Hecho así (2026-11-04, resuelto por Claude). Antes, una lectura del binario (CL6.4.R, *estático*,
  en el 06 § El logo en el binario y § La luz del terreno en un punto), porque el contrato de arriba
  no alcanzaba:
  - el logo es un objeto del mundo y lo dibuja `0x6046a0`, después de los personajes, no
    `0x53c6a0`. Emulado sobre el `main.exe`: `0x6002e0` hace con el `0xa2` un solo RenderBody(2)
    con el objeto, sin brillo;
  - el color del logo es 0,1 de la luz del terreno en su lugar más la suya (`0x5ffce0`), y el
    personaje va con la luz del terreno más 0,3, que le pone `0x517270` (`0x50ba30` fuera de la
    escena 4). Esa luz no es la del OZJ sola: `0x5f8b10` y `0x5f8d90` la sombrean con las alturas;
  - el personaje para en la acción `0x4a`, la de montar, que le da `0x4f2250` con el caballo
    oscuro; el caballo no lo dibuja `0x50ba30` (lo dibuja `0x4453b0`: es el `0xbf`, abajo);
  - el logo avanza a 0,16 por paso (`+0xd8` de `0x606660`), pero `Logo03.bmd` tiene una acción de
    una key: no se anima;
  - la luz del logo corre antes que la cuenta de las rutas, que arranca en −1, y solo con el logo
    ya dibujado (Q-C105, corregido en la revisión);
  - la ventana de la acción va de 0 a 100 ms, por la x87 (Q-C110);
  - la fase de las rutas no vuelve a 0 al volver al login (Q-C111).

  En un PR, con un commit por concepto:
  - **`World/TerrainLighting`** (8 tests, 1 golden, 16 mutantes): la sombra de cada celda y la luz
    en un punto, bilineal, y `LoadTerrainLighting` con la carpeta del mapa. `World/` nace acá; era
    de la CL7.
  - **`Scenes/LoginModels`** (10 tests, 26 mutantes): el logo, su luz y la cuenta de las rutas, el
    color de un objeto sin luz por vértice, el personaje, su lugar, su luz y su acción.
  - **`DrawObject` y la luz por canal de `DrawnCharacter`** (6 tests, 8 mutantes, uno equivalente):
    el pase base de un objeto del mundo, con el armado de las mallas que comparte con el dibujo del
    personaje (`MeshDrawsOf`).
  - **La `LoginScene`** (8 tests con un `Logo03.bmd` y un `World56` sintéticos, el golden
    `GoldenLoginScene.TheLogoLoads`, 16 mutantes): la carga al entrar, el paso como `0x634c60` y el dibujo, con
    `Scenes/CharacterAnimation` (el avance del personaje, que ahora comparte con la selección).

  Lo que cambió del contrato de arriba:
  - `LoginScene` recibe un `IModelLibrary` y no los `GameModels`.
  - El reloj del paso no sale de `IScene::Step`, que tiene 83 llamadas: las escenas lo ven en el
    `SceneContext` (el del bucle).
  - Se nombran también `SingleModelLibrary` (el logo) y `UnlitObjectColor`.

  Decisiones, con lo que cuestan si están mal:
  - Ruling: el caballo (el `0xbf`) y el cuervo no se dibujan en este PR: el cuervo es de la CL8, y
    el caballo va en el PR que sigue de la CL6.4 (falta leer su dibujo y su carga). Si está mal, el
    login se ve distinto hasta ese PR, con el personaje en la pose de montar sin el caballo.
  - Ruling: la luz del terreno se calcula en todas las celdas, sin la ventana de `0x5fbf60` ni las
    luces dinámicas. Si está mal, un lugar fuera de la ventana da 0 en el original.
  - Ruling: el frustum de los objetos no se reproduce: el logo se dibuja siempre y se avanza desde
    que se dibujó una vez, que es lo que hace el `+0x16c` con la cámara fija. Si está mal: con otra
    cámara, un objeto fuera de la vista seguiría avanzando.
  - Ruling: la fase de las rutas vuelve a 0 en cada entrada al login (Q-C111). Si está mal, la
    segunda vez el logo de MuRet se aclara de a poco y el del original no.
  - Ruling: el reloj del paso es el del bucle cuando corre el paso, no el del frame. Si está mal, la
    ventana de la acción se corre menos de un frame.

  Con los datos del laboratorio, `Main` entra al login y dibuja el logo y el personaje: hay una
  captura en 1024 × 768, fuera de Git. 1888 de 1888 tests con los golden y GL. En CI no hay golden.

  Tras la revisión adversarial (dos revisores, uno del C++ y otro contra el binario, que emuló
  `0x602e90`, `0x604ab0` y la ventana de `0x5fbf60`, y decodificó el `.att` del mundo 56):
  - **Del C++ y del binario:** el personaje no volvía a la de parado. `0x4fcd00` le pone la `0x4a`
    otra vez cuando la `0x54` o la `0x55` terminan; MuRet las repetía para siempre.
    `StepCharacterAnimation` devuelve ahora si terminó, y el login le vuelve a dar la de parado
    (`TheCharacterStandsAgainWhenItsGestureEnds`).
  - **De los dos:** el primer paso no le escribe la luz al logo. `0x606560` solo corre los objetos
    con `+0x16c`, que prende el dibujo. Ahora el logo cambia desde que se dibujó
    (`TheLogoDoesNotChangeUntilItIsDrawn`), y el 06, Q-C105 y P-C15 quedaron corregidos.
  - **Del binario:** el `0xbf` es el caballo (`Skill\DarkHorse.bmd`), un objeto ayudante que sigue
    al jinete, y no un efecto. Además hay dos cosas que el original hace en la escena 2 y MuRet no:
    la tela de la clase 4 (`0x50ba30`) y la cabeza que mira al azar (`0x505de0`). Quedaron en el 06
    y en P-C15 (d, g y h).
  - **Del binario:** la luz del terreno coincide con un cálculo aparte en Python sobre los mismos
    archivos, en el lugar del personaje (0,988) y en el del logo (0,698; 0,678; 0,655). Quedó en
    `GoldenTerrainLighting.TheLightOfTheLoginMatchesAnIndependentComputation`, que mata los dos
    mutantes que vivían (sin la sombra, y con las alturas por 1,5). Los dos lugares caen en la
    ventana de `0x5fbf60`.
  - **Del C++, chicos:**
    - el paso de la orden de cerrar movía el mundo (`AQuitOrderDoesNotMoveTheLoginWorld`);
    - las texturas del logo se sueltan antes que sus slots;
    - un comentario había quedado fuera de lugar.
  - **Minors para después:**
    - las mallas que van después de la del blendMesh heredan su color en el original (P-C20 i);
    - el contorno al volver de la selección (P-C15 i).

  **PR B, el caballo oscuro** (2026-11-04, resuelto por Claude). Antes, otra lectura del binario
  (*estático*; el dibujo del `0xbf` emulado sobre el `main.exe`; en el 06 § El caballo oscuro):
  - lo crea `0x6349a0` con `0x443fa0(0xbf, …)` en el lugar del jinete, invisible (el alpha en 0), y
    `0x444010` lo mueve en cada paso, después del logo y antes de los personajes: el alpha sube un
    décimo de lo que le falta, y la acción y la velocidad van por la del jinete (la 0 a 0,3 con la
    `0x4a`; la 5 y la 6 a 1 con los gestos);
  - lo dibuja `0x4453b0` entre los personajes y los objetos, con la escala 1,6. El caso `0xbf` de
    `0x6002e0` lo pinta en blanco con la luz por vértice, que ningún modelo de MuRet usaba, le suma
    el cromo de la malla 12 en (1; 0,8; 0,3) y la sombra de la malla 8, mezclada al 0,7;
  - sus texturas las pide `0x615700` desde `0x3b8`, después de las de tres modelos más: caen de
    `0x3bc` a `0x3c6`, y dos se encuentran en `0x26b` y `0x26c`. **Verificado** en `lor_slots.bin`;
  - la celda del login no es zona segura, que lo apagaría; dos quirks nuevos (Q-C112 y Q-C113).

  En un PR, con un commit por concepto:
  - **La carga:** `GameModels` lee el caballo y los tres de antes (`SkillModelFiles` y
    `SkillTextureCalls`). 3 tests, y el golden de los slots compara 15 más.
  - **El dibujo:** `Render::Models::NormalLight`, el alpha y la luz por vértice de `DrawnObject`, y
    `ModelBlend::BlendedShadow`. 8 tests.
  - **El caballo:** `Scenes/DarkHorse` (el paso) y `DrawDarkHorse` (el dibujo). 8 tests.
  - **La `LoginScene`:** lo crea al entrar, lo mueve con la acción del jinete y lo dibuja entre el
    personaje y el logo. 5 tests.

  48 mutantes a mano, todos en rojo: dos se reescribieron para compilar, y uno (el culling de la
  sombra) vivía hasta separar su test. (Abajo, lo que cambió con la revisión.) Con los datos del laboratorio, `Main` dibuja el caballo
  debajo del jinete (captura fuera de Git).

  Decisiones, con lo que cuestan si están mal:
  - Ruling: el caballo de la selección no entra. `0x5139f0` se lo crea a un personaje con el bit 0
    del byte 11, pero sin la acción de parado de `0x4f2250` por la mascota (P-C14 c) el jinete no va
    montado. Si está mal, un Dark Lord con caballo sale en la selección sin él, como hoy.
  - Ruling: la zona segura, el polvo (`0x5f2`) y los efectos de sus acciones no se reproducen. La
    celda del login no es segura, y los efectos son de su ola. Si está mal, el original levanta polvo
    cada tanto y MuRet no.
  - Ruling: `+0x68` (la luz del blendMesh, Q-C112) no se lleva: el caballo no tiene blendMesh. Si
    está mal, no cambia nada.

  Tras la revisión adversarial (dos revisores: uno del C++, sin Critical ni Important; otro contra el
  binario y las trazas de GL):
  - **Crítico, del binario:** la luz por vértice va con L = (1,3; 0; 2) y no con (0; −1,5; 0).
    `DAT_006c8818` vale 1 desde el `.data` y es su única referencia: las otras dos ramas no corren
    nunca (Q-C115). Las trazas de GL ajustan con (1,3; 0; 2). Con el modelo real, 624 de las 904
    normales del caballo cambiaban en más de 0,1 (`SkinningTest.TheVertexLightComesFromXAndZ`).
  - **Importante, del binario:** en un objeto de `0x5ffdf0`, la normal lleva la escala del objeto
    (1,6 en el login), para la luz y para las UV del cromo (`TheNormalsOfAnObjectCarryItsScale`,
    `TheHorseChromeTakesTheScaledNormal`).
  - **Del binario, que se arreglan con el mismo pase:** la sombra va sin alpha test (`0x4eff40` lo
    apaga, y en el mapa 7 va en 0,2: `TheBlendedShadowHasNoAlphaTest`), y el cromo va en las mallas
    12 a 15 que existan, cada una por el alpha otra vez (`TheHorseChromeGoesOnMeshesTwelveToFifteen`).
  - **Del C++:** el test de la luz por vértice no distinguía el índice de la normal del del vértice,
    y nada miraba las UV del cromo: los dos mutantes vivían, y ahora mueren.
  - **Docs:** la zona segura mira la celda del jinete y, en ella, el caballo avanza igual; la fila de
    la `0x4b`; el polvo, para la ola de los efectos; P-C15 (d) (el alpha va por 1 − 0,9ⁿ: el cuerpo
    se ve desde el paso 3, por el alpha test).
  - **Minors para después:** la cabeza del jinete en el hueso 7 del caballo (no se ve: es una raíz que
    ningún vértice usa); una malla del caballo con `H` dibujaría la sombra en el original y no en
    MuRet (las de serie no tienen script); P-C14 (e) infiere el 1,35 con "el piso 0,2 de la luz por
    vértice", y la L era otra.

  4 tests y 10 mutantes más, todos en rojo. 1921 de 1921 con GL y los golden.
- [ ] **Paso 3 (a mano):** con `--data`, el login con el logo y el personaje sobre los fondos,
  contra el original en la VM (P-C15 y P-C21); sin traza del login (P-C13), se mira a ojo contra
  una captura fuera de Git.

## Sub-ola CL6.5: crear y borrar personajes, con la UI de la selección

**Concepto:** las ventanas del modo 4 (`0x65f4b0`) y los dos paquetes que faltan, con su contrato
escrito después de la CL6.R (pasos 7 y 8): sus campos ya están en 04 (§ La selección de personaje,
P-C06 cerrado), y lo que pide el cable, en P-C16. Si la ola
se alarga, pasa entera a la CL8 sin tocar lo demás (pregunta 10). Va en dos PRs: **PR A**, los
paquetes y la sesión; **PR B**, la UI.

**Lo que ya se sabe** (*estático*, de este plan): el `F3 01` sale de `0x63d990` y su respuesta llega
a `0x4c2290`; el `F3 02` sale de `0x643740` y su respuesta se lee en línea, con los avisos `0x92`,
`0x60`, `0x62` y `0x89`; la ventana de crear lee las clases que se pueden crear de `pkt[4]` de la
lista (`0x63d7c0`, *inferido*) y muestra un `NewFace0N.bmd` por clase (`0xa8` a `0xac`, con la
velocidad 0,3 en sus acciones 0 y 1); los botones son los de `0x61ca30` (`b_create`, `b_connect`,
`b_delete`, `server_menu_b_all`, `cha_id`, `cha_bt`, `deco` y `character_ex`); el texto 1240 lleva
los dos números de `pkt[5]`.

**Contrato** (04 § La selección de personaje, de la CL6.R; *estático*, y el cable en P-C16):

| Qué | Original | MuRet |
| --- | --- | --- |
| crear | `0x63d990`: C1 de 15 bytes, `F3 01` con el nombre en 10 (con ceros) y un byte con la clase y la piel de la vista previa; el estado `0x34`. Antes de mandar, por el nombre, los avisos `0x72` (1715), `0x79` (390) y `0x7a` (391) | `WriteCharacterCreate(name, class, skin)`, con `Xor32C1` |
| su respuesta | `0x4c2290`: `pkt[4]` en 1, creado (el nombre, el lugar, el nivel y la clase); en 0, el aviso `0x36` (1716); en 2, el `0x37` (396); los dos vuelven a abrir la ventana. El nuevo sale sin equipo, con la clase del paquete (Q-C103) | `ReadCharacterCreateResult`; el nuevo en su lugar, sin equipo (`CharacterCreateTest.TheNewOneHasNoEquipment`) |
| borrar | `0x643740`: C1 de 24 bytes, `F3 02` con el nombre del elegido (10, con ceros) y los 10 bytes del campo de la ventana (`0x7798bf8`) como vienen; el estado `0x38` | `WriteCharacterDelete(slot, code)`, con el código como la clave |
| su respuesta | en línea (`0x4e2fe9`): `pkt[4]` en 0, el aviso `0x92` (1654); en 1, el `0x60` (1714), que borra el elegido **al llegar** (Q-C104); en 3, el `0x62` (439); otro, el `0x89` (401) | `ReadCharacterDeleteResult`; se borra el lugar que se mandó (`ServerSessionTest.DeleteRemovesTheSlotItSent`) |
| la vista previa | el objeto suelto `0x7437050` con el modelo `0xa9` (`NewFace02`) y la clase 1 (`0x633f20`); el `F3 01` lee su clase y su piel; que la ventana cambie de `NewFace` por clase, *inferido* | `CreateCharacterPreview`: el `NewFace` de la clase elegida (`0xa8` + clase) |

**Lo que falta escribir al arrancarla:** los archivos, las interfaces y los pasos, como en las
sub-olas de arriba: los cuatro de la tabla, dos llamadas más de la `ServerSession`
(`CreateCharacter`, `DeleteCharacter`) y las ventanas en `UI/`, con los widgets de la CL5.5. Dos
reglas valen desde ya: el `F3 02` lleva los 10 bytes del campo de la ventana de borrar (en otras
versiones, un número personal de la cuenta), y se tratan como la clave (un campo enmascarado,
nunca en el log, `PrintTo` con el largo); y el nombre que se escribe para crear se dibuja pero no
va al log ni a los tests, que usan nombres sintéticos.

**PR A, los paquetes y la sesión** (2026-11-04, resuelto por Claude). Antes, una lectura de los dos
emisores y las dos respuestas (*estático*, en el 04 § La selección de personaje), que corrigió esta
sección:
- el byte de la clase del `F3 01` es la clase por 16 más la piel, y la piel nadie la escribe;
- los tres chequeos del nombre van así:
  - menos de 4 bytes, el aviso `0x79`;
  - una palabra prohibida, el `0x72`;
  - un carácter fuera de rango, el `0x7a`;
- el nombre de los dos pedidos va hasta su primer cero, con ceros hasta 10;
- la respuesta de borrar no cambia el estado, y la de crear lo cambia solo con 1.

En un PR, con un commit por concepto:
- **Los paquetes** (`Protocol/Character/CharacterCreate`, `CharacterDelete` y `CharacterName`, 9
  tests):
  - `WriteCharacterCreate(name, class)` y `ReadCharacterCreateResult`, con el nuevo cuando sale 1;
  - `WriteCharacterDelete(name, code)` y `ReadCharacterDeleteResult`;
  - `NameUpToItsEnd` y `NameFromText`.
- **La sesión** (12 tests):
  - `CreateCharacter(name, class)` y `DeleteCharacter(code)`, desde `ChoosingCharacter`;
  - dos estados que esperan con el tope y el keepalive: `WaitingCreateResult` (`0x34`) y
    `WaitingDeleteResult` (`0x38`);
  - `CreateResult()` y `DeleteResult()`, para los avisos de la UI;
  - el nuevo entra a la lista con `CharacterEntry::withoutEquipment` (Q-C103).

21 mutantes a mano: 20 en rojo (3 reescritos para compilar, 2 vivían hasta su test: el elegido que
no se olvidaba al borrar y el `Start` que no olvidaba los resultados) y uno equivalente (sacar al
elegido o al que se mandó es lo mismo: el elegido no cambia mientras se espera).

Decisiones, con lo que cuestan si están mal:
- Ruling: un nombre de más de 10 bytes no sale. El original lo manda entero y el paquete crece; la
  ventana de MuRet no deja escribir más. Si está mal, un nombre largo que el GS aceptaría no llega.
- Ruling: con un resultado de crear que no es 0, 1 ni 2, la sesión se queda esperando, como el
  original (Q-C114), y el tope de la espera la cierra. Si está mal, el jugador pierde la conexión
  donde el original queda trabado.
- Ruling: el nuevo va con la clase del paquete. El original arma sus partes con la de la vista
  previa y después le escribe la del paquete (Q-C103). Si está mal, con un GS que contesta otra clase,
  MuRet dibuja las partes de esa y el original las de la vista previa.
- Ruling: después de cualquier respuesta, la sesión vuelve a `ChoosingCharacter`. El original queda
  en `0x34`, `0x35` o `0x38`, y nadie leyó si algo los trata distinto del `0x33` (P-C16 c). Si está
  mal, en el original se podría hacer algo que acá no, o al revés, hasta la próxima lista.

**PR B, la UI** (2026-11-04, resuelto por Claude). Antes, un contrato de la UI del modo 4 leído en
el binario (*estático*, y *emulado* el lugar de las ventanas y el armado de los avisos), que está
ahora en el 06 § La barra de la selección y el borrado. Con él, la UI va en dos PRs: **B1**, la
barra, los avisos y el borrado; **B2**, la ventana de crear, con la vista previa y los filtros del
nombre.

**B1** (un commit por concepto):
- **El botón deshabilitado** (`UI::Button`, 2 tests): sin clic, con su cuadro y sus dos colores.
- **Los avisos de la selección** (`UI::Notices` y `UI::NoticeWindow`, 11 tests):
  - los 14 ids de la selección con su texto;
  - los tipos: sin botón (`0x5d`), OK y cancelar (`0x5e`), y el del código (`0x5f`): la caja
    `0x728`, hasta 7 dígitos enmascarados y vaciado al abrir;
  - el texto partido en dos renglones (`SplitIntoLines(texto, 2, 52)`) y el nombre en el `%s` del
    1712, el único texto que es un formato;
  - el OK del `0x5e` abre el `0x5f`, y el del `0x5f` manda el borrado.
- **La barra** (`UI::SelectionBar`, 5 tests, y `Render::Text::CenteredAt`, 1): el lugar de las
  cuatro pantallas, la habilitación de `0x63eb40`, el clic de `0x63eb90` en su orden, y el dibujo
  de `0x63ec00` con los textos 1710, 436 y 437.
- **Las revisiones del borrado** (`UI::DeleteChecks`, 6 tests): el gremio, el nivel 80 con el
  nombre del grupo contra el texto 559, y las banderas `0x12`. Y `NameAsText` y
  `DeleteCodeFromText` (2 tests).
- **La escena** (`CharacterSelectScene`, 10 tests y 2 cambiados):
  - la barra encima de los personajes, que tapa el picking, con los textos de la cuenta bloqueada;
  - conectar como Enter;
  - borrar: las revisiones, la confirmación, el código, el `F3 02` y el `0x5d`, y el aviso de la
    respuesta;
  - la lista de la sesión en cada paso: el que crea entra (sin equipo, Q-C103) y el que borra se
    va, y los demás siguen con su animación.
- Y el golden de los textos del login, que ahora deja afuera los seis textos vacíos de la
  selección y fija que están vacíos (1).

38 tests. La suite entera, 1.981 en verde, con GL y los golden. 52 mutantes a mano:
- 48 en rojo, 5 de ellos reescritos para compilar;
- 3 vivían hasta su test: el creado sin equipo, la cuenta bloqueada y el formato de los otros
  avisos, que era un error (`NoticeWindow` pasaba por `PlaceArguments` el texto de cualquier
  aviso);
- 3 equivalentes:
  - el botón deshabilitado ya suelta la captura;
  - los botones de la barra no se tocan en ningún ancho real, así que el orden de los clics no
    cambia nada;
  - ningún personaje cae bajo la barra: el contorno no se puede probar ahí;
- uno se fue con el arreglo del formato.

Y un hallazgo del primer test del dibujo: la franja sin textura deja la textura apagada, y faltaba
el `0x620a00(1)` con `glColor4f(1, 1, 1, 1)` antes de los textos.

Decisiones, con lo que cuestan si están mal:
- Ruling: la UI va en dos PRs, B1 y B2. La ventana de crear tiene su vista previa en 3D y los
  filtros del nombre, y es tan grande como el resto. Si está mal, nada: es el orden de los PRs.
- Ruling: en B1, crear y el menú solo dejan una línea en el log, y Esc no abre nada. Crear abre su
  ventana en la B2; el menú y las opciones son de la CL8, como los del login (P-C13). Si está mal,
  el jugador ve dos botones que no hacen nada hasta esas olas.
- Ruling: la habilitación de la barra se calcula en cada paso, no en los cuatro momentos de
  `0x63eb40` (el armado, el clic que elige, el crear que salió bien y el `0x60`). La lista y el
  elegido solo cambian en esos momentos, así que da lo mismo. Si está mal, un botón cambiaría un
  paso antes que en el original.
- Ruling: sin servidor elegido (MuRet sin ConnectServer), las revisiones del borrado no miran el
  grupo. El original pasa siempre por la lista. Si está mal, con un GS directo se pide borrar a uno
  de nivel 80 que el original no dejaría pedir; el GS decide igual.
- Ruling: el string del primer `0x0D` de la barra (`+0x511`) no se dibuja: MuRet no lee el `0x0D`.
  Si está mal, falta ese texto a la izquierda de la franja.
- Ruling: cancelar el `0x5f` cierra el campo con el aviso (Q-C116). Si está mal, MuRet no
  reproduce un EDIT invisible que se queda con el teclado.
- Ruling: el `0x5d` se abre solo si la sesión quedó esperando la respuesta. El original lo abre
  siempre, pero la sesión de MuRet solo no espera fuera de la elección, un estado que el original no
  tiene. Si está mal, en ese caso no se ve el "espere".
- Ruling: sin sesión (sin servidor), la escena dibuja la barra, con crear habilitado y nada que
  hacer. Si está mal, esa escena de prueba muestra la barra donde el original no tiene ese caso.

**Tras la revisión adversarial de B1** (dos revisores, uno del C++ y uno contra el binario):
- **Un personaje creado se podía borrar como del gremio** (Importante, del binario): `0x511290` deja
  `+0x368` en `0xff` (`0x511ca6`) y la respuesta de crear no lo toca; la entrada de la sesión salía
  con 0. Arreglado en la sesión (`NoGuildStatus`), con `TheNewOneGoesToItsSlotWithoutEquipment` y
  `ACreatedOneCanBeDeleted` en rojo antes.
- **La respuesta del borrado tapaba al `0x70`** (Importante, del C++): con la respuesta y el cierre
  del GS en la misma lectura, el aviso de la respuesta quedaba arriba y el de la falla se perdía.
  Ahora, con la sesión caída, queda el `0x70` (`AResultThatComesWithTheCloseLeavesTheFailureNotice`).
- **Faltaban dos tests** (Importante, del C++): que los que siguen guardan su animación
  (`TheOthersKeepTheirAnimation`) y que el que se crea en un lugar ocupado lo pisa
  (`ACreatedOneReplacesTheOneInItsSlot`).
- El contorno, con el mouse sobre la barra o con un aviso abierto, se queda en el último en vez de
  irse (del binario: `0x634680` no llama a `0x59ec80` con `+0x8de8`). Cierra P-C19 (c) y cambia el
  test de la CL6.3 (`WithTheNoticeOpenTheOutlineStaysWhereItWas`).
- Los botones de la barra sueltan el mouse con un aviso abierto (`SelectionBar::StepWithoutTheMouse`).
- Un aviso que se abre en un paso se atiende desde el siguiente: un Enter de ese paso no lo cierra
  antes de que se vea (`TheResultNoticeIsSeenEvenWithAnEnterInItsStep`).
- El código no se queda en la ventana: cancelar lo borra y `TakeCode` lo da y lo borra.
- Se fue la rama que cerraba el `0x5d` sin respuesta, a la que no se llegaba.
- Menores: las precondiciones de `CancellingTheDeletionSendsNothing`, la línea "ahora" del log una
  sola vez, tres comentarios de tests que habían quedado sobre el test equivocado y los de
  `Notices.h`.

12 tests más (50 en la B1). La suite, 1.990 en verde. 12 mutantes de la pasada: 11 en rojo y uno
equivalente en efecto (comparar el nombre de la entrada: la apariencia sale de la clase y el equipo).

**B2, la ventana de crear** (2026-11-04, resuelto por Claude). La vista previa en 3D quedó para una
**B3**: cargar los cinco `NewFace` con sus texturas desde `0x6f8`, su cámara y su viewport es un
concepto entero. En B2, un commit por concepto:
- **Los filtros del nombre** (`Data::Text::LoadFilterWords`, 7 tests, 2 contra la `Data` del
  laboratorio; y `LoadGameData`, 3): `FilterName.bmd` y `Filter.bmd`, con el XOR3 por registro y
  el checksum de las tablas (la paridad de `Item.bmd`, claves `0x2bc1` y `0x3e7d`).
- **Los chequeos del nombre** (`UI::NameChecks`, 6 tests): los tres de `0x63d990` en su orden, con
  la tabla de nombres emulada del contrato.
- **La ventana** (`UI::CreateCharacterWindow`, 9 tests): el lugar de `0x63d4e0`, las clases de la 0
  a `pkt[4]` con el Dark Knight marcado, el campo de 10 bytes, OK, cancelar, Enter y Esc, y el dibujo
  de `0x63e0a0` sin la vista previa.
- **La escena** (7 tests): el botón de crear abre la ventana, modal; el OK pasa por los chequeos, y
  si van, manda el `F3 01` y abre el `0x5d`; la respuesta cierra el `0x5d` o vuelve a abrir la
  ventana con el `0x36` o el `0x37`.

32 tests (44 con los de la revisión). La suite, 2.021 en verde antes de la revisión, con GL y los
golden. 32 mutantes a mano (45 con los 13 de la revisión, todos en rojo):
- 30 en rojo: 2 reescritos para compilar, y uno vivía hasta su test (los botones de la ventana bajo
  un aviso);
- 2 equivalentes: el tope de `pkt[4]` a 4, que sobraba (el lazo solo tiene los cinco) y se fue, y
  sacar los espacios antes de filtrar, que no se ve porque el espacio ya filtra.

Decisiones, con lo que cuestan si están mal:
- Ruling: la vista previa va en una B3. Si está mal, nada: es el orden de los PRs; hasta la B3, la
  ventana se ve sin el personaje.
- Ruling: sin `FilterName.bmd` o sin `Filter.bmd`, `LoadGameData` sigue con el filtro vacío. El
  original avisa y se cierra. Si está mal, con una `Data` incompleta se pueden pedir nombres que el
  original no dejaría; el GS decide igual.
- Ruling: el campo del nombre se dibuja con el estilo de los del login (la fuente normal, blanco y
  el `_` de cursor): el dibujo del EDIT (`0x468320`) no se leyó entero (P-C16 f). Si está mal, el
  campo se ve un poco distinto.
- Ruling: la búsqueda de las palabras avanza de a un byte, no de a un carácter de la página de
  códigos (`_mbclen`). Con las letras que toma el campo (ASCII y Latin-1, de un byte cada una) da lo
  mismo. Si está mal, con una página de códigos de dos bytes por letra MuRet filtraría algo que el
  original no.
- Ruling: el campo del nombre toma ASCII y las letras de Latin-1 (U+00A0 a U+00FF, que en esa franja
  son las de CP1252); las de CP1252 de 0x80 a 0x9f y las de otras páginas, no. Si está mal, un
  jugador con otra página de códigos no puede escribir su nombre como en el original.

**Tras la revisión adversarial de B2** (los dos revisores, sin críticos):
- **El campo del nombre solo tomaba ASCII** (Importante, del binario): el EDIT de `0x63d670` no
  filtra, y el original manda "Peña" con su `0xf1`. `TextField` toma Latin-1 en el campo del nombre
  (`ALatin1FieldTakesTheLettersOfItsPage`, `TheNameTakesLatin1Letters`).
- **Ningún test miraba la respuesta del `F3 01` con la sesión caída** (Importante, del C++): la
  guarda estaba, pero un mutante que la sacaba vivía
  (`ACreationResultWithTheCloseLeavesTheFailureNotice`).
- Q-C118 se reproduce: la ventana guarda el buffer de `0x7798bf8`, y el costo que decía la decisión
  ("con ASCII no cambia nada") estaba mal, porque el espacio se escribe.
- Con un aviso arriba, lo que se escribe sigue yendo al nombre (el EDIT no pierde el foco).
- Un filtro con los 1.000 registros llenos no filtra nada (Q-C120).
- Tests que faltaban: el Enter en el paso de la respuesta, `/GMxx` (las dos listas en su lugar), el
  orden de los chequeos ("GM" es corto) y que el nombre no va al log.
- Menores: `Draw` en tres partes, un `SetBlend` de más, y comentarios.

**B3, la vista previa** (2026-11-04, resuelto por Claude). Con la `Data` en el repo, los `NewFace` y
sus texturas están en el CI. La lectura que faltaba de `0x50ba30`, para un modelo que no es el del
personaje, va a `0x602e90`, el mismo camino del caballo de la CL6.4 (`DrawObject`), sin la sombra.
Lo de la ventana es todo de la CL6.R y del contrato. En un PR:
- **Los modelos** (`Scenes::SelectionModels`, 2 tests, uno contra el volcado de Lorencia): Logo05 y
  los cinco NewFace, con sus texturas desde `0x6f8` siguiendo la tabla de `GameModels`
  (`ModelTextureSlots::ContinuedAt`, 1 test).
  - Contra el volcado: los 31 slots tienen el tamaño del buffer y los componentes de lo que sube
    MuRet (Q-C91, verificado).
  - Cada NewFace lleva la velocidad 0,3 en sus acciones 0 y 1.
  - El 0xa4 va con GL_LINEAR y los NewFace con GL_NEAREST.
- **La vista previa** (`Scenes::CreatePreview`, 4 tests):
  - la cámara, el viewport y la tabla de clases de `0x63e460`;
  - la acción 1, que se arma con cada clase (`0x63e3e0`), avanza a 0,6 keys y al terminar vuelve a
    la 0 (`0x63e430`).
- **La escena** (3 tests): la vista previa en su propio pase 3D, entre la base de la ventana y lo
  demás, con la clase de la ventana; y `Leave`, que suelta sus slots.

10 tests. La suite, 2.043 en verde. 17 mutantes a mano: 16 en rojo, 2 de ellos reescritos y 3 que
vivían hasta su test (las velocidades, el filtro del 0xa4 y el de los NewFace), y uno equivalente con
esta `Data`: seguir la tabla de la carga, porque ningún NewFace comparte nombre de textura con los
modelos de antes.

Decisiones, con lo que cuestan si están mal:
- Ruling: la vista previa avanza una vez por paso y solo con la ventana arriba. El original la avanza
  también desde la lógica de la escena, así que con la ventana arriba va dos veces más rápido, y
  sigue avanzando bajo un aviso (D-C4). Si está mal, la vista previa saluda a la mitad de velocidad.
- Ruling: el objeto va en (50, 50, 0), sin la z del terreno del mapa 54, que es de la CL7. La cámara
  va con el objeto, así que no se ve. Si está mal, nada, hasta que algo del mundo entre en el
  viewport.

**Tras la revisión adversarial de B3** (un revisor del C++ y del binario, sin críticos):
- **La vista previa temblaba con un aviso arriba** (Importante): no avanza, pero el dibujo la
  interpolaba con un paso de más. Ahora va quieta (`UnderANoticeThePreviewStaysStill`).
- La luz es otra: `0x511290` prende la luz por vértice, y `0x6002e0` les pone el color en 4,8 a los
  NewFace. MuRet la dibuja así, y el shader la recorta.
- La vista previa se arma al entrar a la escena, no con la lista (`ThePreviewGreetsFromTheStart`).
- Tests más fuertes:
  - la base antes de la vista previa;
  - una acción 1 que repite, que termina por la regla de la escena 4;
  - los nombres de GL seguidos del volcado.

20 mutantes más de la pasada y de los tests nuevos, todos en rojo.

## Qué no se prueba en CI, y cómo se cubre

| Qué | Cómo |
| --- | --- |
| el `.bmd`, la pose, el skinning, el avance, el brillo, la apariencia, los paquetes, la sesión y las escenas | los tests de `data/`, `render/`, `gamelogic/`, `protocol/`, `app/` y `scenes/` con modelos sintéticos (`support/SyntheticModels.h`), sin GL: corren en los dos sistemas |
| lo que hace GL con el 3D (profundidad, culling, mezcla, el color recortado, el 2D después) | los de `render_gl/`, con Xvfb y Mesa en Linux (`ctest -L gl` con `MURET_CLIENT_REQUIRE_GL=1`); en Windows, a mano |
| los 1.962 modelos, el catálogo del personaje y sus texturas | los golden, con `MURET_CLIENT_GOLDEN_DIR` (CI no tiene la carpeta) |
| la pose, el brillo y las matrices contra el original | los golden de la traza, con `MURET_CLIENT_GOLDEN_DIR` y `MURET_CLIENT_GL_TRACE_DIR` (las trazas de `graphics-01/`, descomprimidas, fuera de Git) |
| la forma del `F3 00` y del `F3 03` contra el cable | con `MURET_CLIENT_GOLDEN_DIR` y `MURET_CLIENT_WIRE_DIR`, sin imprimir valores (`SameBytesWithoutValues`) |
| que la selección y el login se vean como los del original | a mano, con capturas fuera de Git; la traza ya fija la pose, las UV y las matrices de la selección, y el login no tiene traza (P-C13) |
| la lista, la elección y la entrada contra el GS del laboratorio | a mano (CL6.3, paso 10) |
| el mundo detrás de la selección y del login | no está en la CL6: la CL7, en el mismo pase (pregunta 3) |

## Al cerrar la CL6

- [ ] Las pruebas a mano en la VM: P-C21 (el paso 10 de la CL6.3 y cancelar en la cuenta) y el
  paso 3 de la CL6.4, con las trazas y capturas de P-C13 a P-C20 que entren en la misma sesión.
- [ ] Completar el cierre de la guía de la CL6.0.
- [x] Actualizar `docs/learning/PROGRESS.md`.
- [x] Sumar al plan de la CL7: el mundo de la selección (el mapa 54, `World55`) y el del login (el
  55, `World56`, que ya pedía el cierre de la CL5) en el pase 3D de la CL6.3, antes de los
  personajes y con la misma cámara, como `0x5fc260` antes de `0x510d00`; la respuesta del `F3 03`
  (`0x4c3940`) y el tope de `EnteringWorld`; las velocidades del keepalive
  (`ServerSession::SetKeepaliveSpeeds`) con lo que traiga esa respuesta; los multiplicadores de
  `0x4f9580`; y que la cámara que se mueve (CL7.3) dé un `Render::Camera3D`. La CL7 no tiene plan
  todavía (se escribe al arrancarla): va en el roadmap, en
  [Lo que dejó la CL6](../specs/2026-10-30-cliente-design.md#lo-que-dejó-la-cl6), con la z del
  objeto de la vista previa y la zona segura del caballo.
- [x] Decidir dónde van los efectos (el polvo del caballo, `0x5f2`, la luz de abajo del elegido si no
  entró en la CL6.3, los de las armas): el roadmap no tiene esa ola. El `0xbf` del login resultó ser
  el caballo, y lo dibuja la CL6.4 PR B. Van en una CL7.6, al final de la CL7, porque se dibujan en
  el pase 3D del mundo con su cámara; la luz de abajo del elegido no entró en la CL6.3.
- [x] Sumar al plan de la CL9 los modelos de los monstruos con el research corregido: `Monster50`
  falta en el laboratorio y cerraría el original. En el roadmap, con la pose de los NPC (Q-C108) y
  Q-C95.
- [x] Si la CL6.5 pasó a la CL8, sumarla a su plan. No pasó: entró entera en la CL6 (PR A, B1, B2
  y B3). A la CL8 van el menú de la selección (`+0x3d0` y `+0x7e8`), el área de mensajes con el
  texto 1240, el string del `0x0D` en la barra (`+0x511`) y el cuervo del login, en el roadmap.
