# 06. Render, escenas y UI

Fuentes: `analysis/mainexe/functions/paso7-escenas.md`, `paso10-escenas-completo.md`,
`paso8-texto-y-coordenadas.md`, `paso11-texto-completo.md`, `paso12-hud.md`,
`paso13-ventanas.md`, `paso14-ventanas-interiores.md`, `paso17-mapeo-vtable-panel.md`,
`coverage-ui-sistemas.md` y el decompilado de `mainexe-vmxp-c32bbd5f/`. Además, las trazas de
OpenGL del original en
[mu-online-reverse-lab#45](https://github.com/EmaNLJN/mu-online-reverse-lab/pull/45)
(`reports/gameserver-passive-2026-10-30/graphics-01/`, commit `69be91d2`).

La arquitectura (escenas, ventanas, HUD) es **estática**: se leyó en el assembly y el
pseudocódigo. Lo que el original le pide a OpenGL ya no: GLIntercept 1.34 registró cada llamada
de GL de 5 frames de la selección de personaje y 5 de Lorencia en la VM XP, con el build
`c32bbd5f…`, y contó las llamadas de una sesión entera de 113,79 s (2.375 frames). Lo marcado
**Verificado** en este tema sale de esas trazas, cruzadas con el decompilado y con los datos de
`Data\World1`. Las trazas no tienen píxeles: dicen qué se dibuja, no cómo queda en pantalla.

## La máquina de escenas

El global `DAT_006d5dc8` dice en qué escena está el cliente:

| Valor | Escena | Render |
| --- | --- | --- |
| 2 | login (servidores, cuenta) | `0x6350b0` |
| 4 | selección de personaje | `0x634680` |
| 5 | en el juego | `0x6371a0` |

Hay además un estado de escena más fino en `0x7D3A10C` (por ejemplo `0x73` en el juego, `0x8A`
aviso) que manejan los handlers de red (`paso5-protocolo-handlers.md`).

## Lo que el original le pide a OpenGL

**Verificado** (trazas y estadísticas de la sesión entera):

- **Solo OpenGL 1.1 en modo inmediato.** En 113 s el cliente llamó 43 funciones distintas, 37 de
  GL 1.1 y 6 de `wgl`: `glBegin`/`glEnd` con `glVertex3fv`, `glTexCoord2f` y `glColor3fv` por
  vértice, texturas (`glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameteri`,
  `glTexEnvf`), estado (`glEnable`/`glDisable`, `glBlendFunc`, `glAlphaFunc`, `glDepthFunc`,
  `glDepthMask`) y matrices (`glRotatef`, `glTranslatef`, `glMultMatrixd`, `glOrtho`, pila). No
  hay vertex arrays, display lists, `glLight` ni normales, multitextura ni extensiones.
- **Unas 285.000 llamadas por frame** en promedio, casi todas por vértice: 224,8 millones de
  `glVertex3fv` en 2.375 frames, unos 95.000 vértices por frame.
- **Primitivas:** `GL_TRIANGLE_FAN` para cada celda del terreno y cada quad de la UI,
  `GL_TRIANGLES` para los modelos y `GL_QUADS` para efectos. No hay `GL_TRIANGLE_STRIP` en
  ninguna de las dos escenas.
- **Estados:** mezcla normal (`SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`) y aditiva (`ONE`, `ONE`, con
  `glDepthMask(false)`), alpha test `GL_GREATER 0.25`, depth `GL_LEQUAL`, culling que se prende y
  se apaga por malla. La niebla está apagada en las dos escenas.

## Cada frame de una escena

```text
render de la escena
 ├─ cámara       0x635ae0 + 0x44dc60 + 0x620db0 (proyección y modelview)
 ├─ pase 3D      fondo, frustum 0x5fbb00, terreno 0x5fc260, objetos 0x53c6a0
 │               [selección] preview 3D del personaje 0x5f9780 + luz 0x620980 + nombre 0x4129f0
 ├─ pase 2D      0x6219b0 (ortho) … 0x621a90 (vuelve)
 │    ├─ UI en el juego 0x62db60 (20 KB, 52 llamadas GL)
 │    ├─ texto 0x554180
 │    └─ overlays comunes (chat, avisos)
 └─ glFlush + SwapBuffers  en el render loop 0x637700
```

**Verificado** en Lorencia, en el orden en que llegan a GL (ventana de 1024 × 768):

| Pase | Viewport | Proyección | Qué dibuja |
| --- | --- | --- | --- |
| 1 | `0,0,1024,768` | perspectiva | nada (una cámara de pantalla completa; *inferido*: la de `0x637700`, que llama `0x620db0(0, 0, 640, 480)`) |
| 2 | `0,692,1024,76` | perspectiva | nada (la franja de arriba, `0x620db0(0, 0, 640, 48)`) |
| 3 | `0,77,1024,691` | perspectiva del mundo | terreno, objetos, personajes y efectos: 774 `glBegin`, 43.009 vértices, 63 texturas |
| 4 | `0,0,1024,768` | ortho de `0x6219b0` | UI y texto |
| 5 | `0,0,1024,768` | perspectiva de 1° | modelos 3D dentro de la UI (816 vértices, 3 texturas; qué son, sin identificar) |
| 6 | `0,0,1024,768` | ortho | el resto de la UI |

La selección de personaje es parecida: el mundo va en `0,40,1024,688` (6.543 `glBegin`, 74.553
vértices), y antes hay una perspectiva de 45° que no dibuja nada y un ortho con 2 quads.

### Cámara

Seis floats globales: los ángulos en X, Y y Z (`DAT_07d36868/6c/70`; en un mundo con Z hacia arriba,
Z es el yaw y X el pitch) y la posición (`DAT_07d36884/88/8c`).
`0x620db0(x, y, w, h)` recibe el viewport en coordenadas de 640 × 480, lo escala a la ventana
(`W·x/640`, `H·y/480`) y lo da vuelta en Y (`0x620d10`: `glViewport(x, H − y − h, w, h)`). Después
arma la perspectiva con `gluPerspective(FOV, w/h, near, far × 1,4)` (`0x6203c0`; FOV, near y far
en `DAT_006d5d48/40/44`, el 1,4 en `0x6b8c00`), aplica `glRotatef` ×3 (primero Y, después X si
`DAT_07d36899` es 0, al final Z) y `glTranslatef(−posición)`, habilita textura, depth test y
culling, `glAlphaFunc(GL_GREATER, 0.25)` y niebla lineal si está activa. La cámara la mueve el
mouse (botones derecho y medio, rueda; ver [02](02-arranque-y-ciclo-de-vida.md)). En el mapa 6 con
los nombres "webzen" y "webzen2" hay una cámara especial.

**Verificado** contra las trazas (el `glMultMatrixd` de la perspectiva y las rotaciones):

| | Lorencia | Selección de personaje |
| --- | --- | --- |
| llamada | `0x6371a0`: `0x620db0(0, 0, 640, 432)` | `0x634680`: `0x620db0(0, 25, 640, 430)` |
| viewport resultante | `0,77,1024,691` | `0,40,1024,688` |
| FOV vertical, near | 35°, 20 | 35°, 20 |
| far | 2.800 (2.000 × 1,4) | ≈ 16.130 (≈ 11.521 × 1,4) |
| rotaciones | Y 0°, X −48,5°, Z −45° | Y 0°, X −84,5°, Z −45° |
| posición | (15.057,1; 11.342,9; 1.015) | (13.909; 14.482; 543) |

El aspect no es el de la ventana sino el del viewport: 1024/691 = 1,4819 en Lorencia.

### Primitivas 2D

- `0x6219b0` / `0x621a90`: entrar y salir del modo 2D. `0x6219b0` pone el viewport entero y
  `gluOrtho2D(0, W, 0, H)`: **el ortho está en píxeles reales de la ventana** (1024 × 768 en las
  trazas), no en 640 × 480.
- `0x621b60(id, x, y, w, h, u, v, du, dv, escalar_tamaño, escalar_posición, alpha)`: un quad
  texturizado, `glBegin(GL_TRIANGLE_FAN)` (no `TRIANGLE_STRIP`: lo dicen el decompilado,
  `glBegin(6)`, y las trazas). Da vuelta la Y (`H − y`) y, si los flags lo piden, escala posición
  y tamaño desde 640 × 480 con `0x620d50` (`x·W/640`) y `0x620d80` (`y·H/480`). Es el átomo de toda
  la UI. `0x621aa0` es el mismo quad sin textura.
- Por eso conviven UI escalada desde 640 × 480 y UI en píxeles reales (el texto, por ejemplo).

### Terreno

**Verificado.** El renderer del terreno es esta cadena (antes P-C02):

```text
0x5fc260  pase de terreno
 └─ 0x5fc190  bloques de 4 × 4 celdas, desde DAT_07d2d364/368 (los arma el frustum 0x5fbb00);
              frustum por bloque con 0x5fba90
     └─ 0x5fc0e0  las 16 celdas del bloque, frustum por celda
         └─ 0x5fac90  vértices de la celda: x, y × 100; z = altura; atributos (bit 0x08: no dibujar,
                      bit 0x40: altura fija)
             └─ 0x5fa830  elige capas por el alfa de las 4 esquinas
                  ├─ 0x5fa5c0  UV: u = x · 64 / ancho de la textura (agua animada aparte)
                  ├─ 0x5fa260  pase opaco: glColor3fv(luz) por vértice
                  ├─ 0x5fa350  pase de alfa: glColor4f(luz, alfa) por vértice
                  └─ 0x5fa4b0  el agua del mapa 7
```

Contrastado con la traza de Lorencia y con `EncTerrain1.map`, `TerrainHeight.OZB` y
`TerrainLight.OZJ`, celda por celda (329 pases opacos y 141 de alfa: 138 con la capa 2 y 3 con el
slot de Q-C19; sin diferencias en geometría, UV, textura ni alfa):

- Cada celda es un `GL_TRIANGLE_FAN` de 4 vértices, 100 unidades de lado.
- `z = TerrainHeight[y·256 + x] × 1,5`: el cargador `0x5f9020` multiplica cada byte del BMP (desde
  el offset 1080) por la constante de `0x6b8b94`, y en el mapa 55 por la de `0x6b86b8`. La traza
  confirma el 1,5 con un solo valor: la zona dibujada es plana (byte 110 en sus 625 celdas, z = 165
  en los 1.880 vértices).
- `u = x_celda · 64 / ancho`, con el ancho de la textura del tile; el tile `i` es el slot
  `i + 0x23` de la tabla de texturas (textura GL `1351 + i` en esta sesión).
- Capas del `.map` (cabecera de 2 bytes, capa 1, capa 2, alfa): si las 4 esquinas tienen alfa 255,
  la celda se dibuja **una sola vez**, opaca, con la capa 2. Si no, se dibuja opaca con la capa 1 y,
  si alguna esquina tiene alfa > 0, **enseguida** una segunda vez con la capa 2, mezcla normal,
  sin culling y `alfa = alfa[vy·256 + vx] / 255` por vértice. Una capa 2 de valor 255 no se trata
  como vacía: se dibuja el slot `0x122`, que no es un tile (Q-C19, 3 celdas de la traza).
- La luz por vértice es el píxel de `TerrainLight.OZJ` en `(x, 255 − y)`, dividido 255: la imagen
  está dada vuelta en Y respecto de la grilla. Coincide (±1 %) en 1.746 de los 1.869 vértices que se
  pueden medir (canal rojo > 8); los otros 123 salen más brillantes, nunca más oscuros (×1,1 a
  ×3,1): luces dinámicas que se suman a la base (fuego, efectos),
  *inferido*, sin identificar quién las suma.
- **El orden importa** para la mezcla y está verificado: bloques de 4 × 4 por (fila de bloque,
  columna de bloque), dentro del bloque las celdas por (y, x), y el pase de alfa de una celda
  inmediatamente después de su pase opaco (los 28 bloques y las 141 celdas con alfa de la traza).

### Modelos y objetos

**Verificado** contra las trazas, incluido el skinning del personaje; lo que no dicen las trazas es
en qué funciones del binario se hace (P-C01):

- **Todo modelo llega en coordenadas de mundo.** Ningún bloque de `GL_TRIANGLES` tiene una matriz
  propia encima de la cámara (185 de 185 en Lorencia, 285 de 285 en la selección): el cliente
  transforma cada vértice en la CPU (hueso, rotación, escala y posición) y manda el resultado.
- **La animación se recalcula en cada frame.** Entre dos frames seguidos de Lorencia se mueven
  12.099 de los 12.507 vértices del personaje (mediana 0,37 unidades), mientras que 16.749 de los
  17.151 vértices de los objetos del mapa quedan idénticos.
- **Los objetos del mapa están donde dice el `.obj`.** 146 de los 185 bloques de Lorencia tienen
  el centro a menos de 200 unidades (2 celdas) de un registro de `EncTerrain1.obj` (descifrado con
  K16: 2.870 registros, 109 tipos). Los que no, son sobre todo el personaje y su equipo.
- **La luz es un color por vértice** (`glColor3fv`), sin normales: en el personaje, un color por
  malla (por ejemplo `0,75 0,65 0,5`). Aparecen colores mayores que 1 (1,34): el GL de función fija
  los recorta a 1 antes de rasterizar.

#### El personaje, vértice a vértice

El formato y la matemática del skinning están en [05](05-formatos-de-datos.md#modelos-bmd). Contra
la traza:

- **Qué malla es cada bloque.** Cada triángulo de una malla manda sus tres UV en orden, así que la
  secuencia de UV de un bloque identifica la malla del `.bmd`. En las dos escenas el personaje
  lleva `HelmMale17`, `ArmorMale17`, `PantMale02`, `GloveMale17` y `BootMale17`, con `Sword17` y
  `Helper02` como objetos aparte. En Lorencia hay además un guardia armado con partes de jugador
  (`HelmMale10`, `MaskHelmMale10`, `ArmorMale10`, `PantMale10`, `GloveMale10`, `BootMale10` y la
  lanza `Spear08`), y el pase 3D de 1° de la UI dibuja `Potion04` y `Potion07`.
- **Skinning rígido.** Ajustando cada hueso por separado, los vértices del `.bmd` caen sobre los de
  la traza a menos de 0,001 unidades con escala 0,9: un hueso por vértice, sin pesos.
- **La pose sale de las acciones de `Player.bmd`**, con el frame fraccionario:

  | Escena | Modelo | Acción (keys) | Frame en los 5 swaps | Error máximo |
  | --- | --- | --- | --- | --- |
  | Selección | personaje | 3 (7) | 1,737 · 3,320 · 4,896 · 6,739 · 1,578 | 0,015 |
  | Lorencia | personaje | 1 (6) | 1,820 · 1,411 · 0,731 · 0,042 · 5,358 | 0,058 sin la cabeza |
  | Lorencia | guardia | 1 (6) | 0,675 · 0,279 · 5,583 · 4,908 · 4,238 | 0,047 sin la cabeza |

  El objeto solo agrega la escala 0,9, un giro alrededor de Z (100° en la selección; 116° el
  personaje y 90° el guardia en Lorencia) y su posición. En Lorencia los dos están a z = 165, la
  altura del terreno en esa zona (byte 110 × 1,5).
- **La cabeza gira aparte.** En Lorencia el hueso `Bip01 Head` (20) no sigue la animación: queda a
  0,5 unidades en el personaje y a 4,6 en el guardia, con el resto del cuerpo a menos de 0,06. En
  la selección la cabeza sigue la animación. *Estático*, en `0x4eb860`: al hueso de la cabeza del
  modelo (su índice está en `modelo + 0x54`) le resta a los ángulos de Euler x e y un ángulo del
  objeto (`objeto + 0x28` y `+ 0x2c`, en grados, por π/180) en las dos keys que interpola, antes de
  pasar a cuaternión (`0x5fcd20`); los demás huesos usan los cuaterniones ya calculados. Quién
  escribe ese ángulo en los personajes, sin leer (probablemente hacia dónde mira, *inferido*).
- **Pases extra de brillo.** Las mallas `Male17` y la espada se dibujan tres veces más, con las
  mismas posiciones y UV que no están en el archivo (texturas GL 141, 115 y 120); las del guardia y
  las pociones, una vez más (la 120). Las UV salen de la normal de cada vértice en el mundo
  (`nx`, `ny`, `nz`) según banderas del dibujo, en `0x4ed890`. **Verificado** ajustando las UV de
  la traza sobre las normales del `.bmd` posado (residuo máximo de 0,016 en las dos escenas):

  | Textura | Bandera | u | v |
  | --- | --- | --- | --- |
  | 115 | ninguna | 0,5·nz + 0,2 | 0,5·(ny + 1) |
  | 120 | `0x4` | 0,5·nz + t | 0,5·ny + 2t |
  | 141 | `0x1000` | c·nx + (s + 0,5)·ny + nz + 3s | 1 − c·nx − s·ny − 1,5·nz − 3t |

  con t = (T mod 10000) · 0,0001, c = cos(0,001·T) y s = sin(0,002·T), donde T es el reloj del
  frame en ms (abajo). Hay más modos sin pase en estas trazas (`0x200`, `0x800`, `0x4000`,
  `0x10000`), *estáticos*. De qué depende cuántos pases lleva cada item (¿el nivel?), sin leer.

#### Cuánto avanza la animación

**Verificado** en el binario y en las trazas: **el cuerpo avanza por paso de lógica de 40 ms, con
la velocidad de su acción; la mascota y las alas, por frame dibujado.**

- **La velocidad es de la acción.** Cada acción del modelo tiene 0x10 bytes: `+0` no repite (se
  queda en la última key), `+4` la velocidad en keys por paso, `+8` la cantidad de keys y `+10`
  `lock_positions`. El cargador de `Player.bmd` (`0x60dbe0`) las fija a mano: 0,28 de la 1 a la 12,
  salvo la 3 (0,26), la 4 y la 5 (0,24) y la 7 y la 8 (0,22), y 0,3 en la mayoría de las demás.
- **El paso la aplica.** Dentro del bucle de pasos de `0x637700` ([02](02-arranque-y-ciclo-de-vida.md)),
  la lógica de la escena llega por `0x508f00` → `0x508ea0` → `0x4fd5f0` → `0x4fcd00` a `0x4f9580`,
  que multiplica la velocidad según el tipo y la acción del personaje (×1,5, ×0,5, ×2, ×4 y
  otros), y a `0x4ec200`, que hace `frame += velocidad`. Al pasar la última key vuelve con módulo
  (n keys, o n − 1 con `lock_positions`), o se queda en n − 0,01 si la acción no repite. Cambiar de
  acción (`0x4ea030`) vuelve el frame a 0 solo si la acción es otra.
- **Las trazas lo confirman con dos velocidades.** En la selección (acción 3, 0,26) el frame salta
  1,583, 1,577, 1,842 y 1,839 keys entre swaps: 8,58 y 8,84 módulo 7, o sea 33, 33, 34 y 34 pasos.
  En Lorencia (acción 1, 0,28) el personaje salta 5,59 · 5,32 · 5,31 · 5,32 keys módulo 6 y el
  guardia 5,60 · 5,30 · 5,33 · 5,33: 20, 19, 19 y 19 pasos los dos.
- **Y el reloj de la traza cierra.** El pase de brillo 120 corre con T, el `timeGetTime` que
  `0x4eb620` guarda una vez por frame al empezar `0x637700`. Entre swaps T avanza 1.296, 1.330,
  1.406 y 1.360 ms en la selección (33 a 34 pasos son 1.320 a 1.360 ms) y 782, 798, 764 y 766 ms en
  Lorencia (20 y 19 pasos son 800 y 760). Los frames son tan largos porque GLIntercept frena cada
  llamada de GL: unas 227.000 por frame en la selección y 125.000 en Lorencia, unos 6 µs por
  llamada en las dos. Con esas duraciones, los 11 fans de la textura GL 1153 de la selección
  corren su U a 0,05 por segundo.
- **Corrección.** Antes este documento decía 0,263 keys por paso con 6 o 7 pasos (selección) y 43 o
  44 (Lorencia): da los mismos saltos módulo las keys, pero la velocidad no está en la tabla y el
  reloj lo descarta. Era un alias.
- **La mascota y las alas avanzan por frame dibujado.** El dibujo del personaje
  (`0x510d00` → `0x50ba30` → `0x509340` → `0x4ec200`) anima sus partes con velocidad fija: la
  mascota (`personaje + 0x318`, tipo `0x1c13` = `Helper02`) 0,5 keys por frame, y las alas
  (`+ 0x2f8`) 0,25, o 1,0 en las acciones `0x1e` y `0x1f`. **Verificado:** `Helper02` avanza
  exactamente 0,5 por frame en las dos trazas, mientras el cuerpo avanza 19 a 34 pasos. Con más FPS
  la mascota va más rápido (Q-C20 en [07](07-quirks-y-bugs.md)).
- **Cómo se interpola.** `0x4eb860` hace slerp entre la key entera del frame anterior y la del
  actual, con la parte fraccionaria del frame: cada vez que `0x4ec200` cruza una key, guarda la
  vieja como frame anterior. Para el original, entonces, el frame de la tabla de arriba es uno más
  (módulo n): la tabla da la pose entre ⌊f⌋ y ⌊f⌋ + 1, el original guarda f + 1 y ⌊f⌋ como anterior.
  Los saltos no cambian.
- **Fin de acción adelantado en la selección.** En la escena 4, `0x4ec200` le suma 2 al frame solo
  para decidir si la acción terminó: avisa "terminó" dos keys antes, pero el módulo usa el frame
  real (Q-C21). *Estático.*
- En el guardia, el frame f y el 6 − f dan casi el mismo error (sobre sus vértices la acción 1 es
  casi simétrica); en la tabla va la rama que sigue la secuencia. En el personaje no hay duda: en
  cuatro de los cinco frames la otra rama queda a más de 4 unidades (en el quinto, cerca de la key
  0, a 0,16).

### Texto

```text
0x554180(x, y, texto, ancho, sombra, color)
  └─ 0x465950  dispatch virtual (qué método corre, sin leer)
       ├─ 0x553e50  rasteriza con GDI: TextOutA sobre un DIBSection
       └─ 0x554000  glBindTexture(slot 0) + glTexImage2D 256 × 32 RGBA, y el quad con 0x621b60
0x555b00   (lo llama 0x5a9050) hace lo mismo sin dispatch: 0x553e50 y después 0x554000
0x4667a0   colores inline: el byte 0x02 marca un cambio de color y se saca del texto
0x466000   otro camino: TextOutA → texturas propias (slots de 0x466560, 10000 + índice), caché en
           un árbol rojo-negro por hash del texto, textos de más de 256 px en dos texturas
```

**Verificado:** en las dos escenas todo el texto pasa por el slot 0 (textura GL 1): un
`glTexImage2D` de 256 × 32 por string y por frame (1 por frame en Lorencia, 4 en la selección),
sin caché del lado de GL, y el quad va en píxeles reales. `0x466000` no corrió en la sesión
medida: es la única función que llama `glPixelStorei`, y hubo cero en 113 s. `paso11-texto-completo.md`
lo pone debajo de `0x554000`, pero `0x554000` no lo llama (decompilado) y la traza tampoco lo
muestra; el rasterizado de `0x553e50` en el camino virtual es *inferido* por `0x555b00`. El input
de texto (`0x468320`) incluye el IME coreano. Verificado aparte: el asignador `0x466560`
(reconstruido en `mu-client` con una sonda en la VM).

### HUD y ventanas

- `0x5a7320` orquesta el HUD: barras de HP y MP (`0x5a89b0`), barra de skills, macro, minimapa.
- `0x5bbbc0`: nombre y barra de HP sobre jugadores y NPCs.
- `0x4383f0`: texto flotante (números de daño).
- `0x490390`: administrador de ventanas: recorre las ventanas abiertas (árbol rojo-negro por
  prioridad + lista en `+0x84`) y llama al método virtual de dibujo (`vtable + 0x14`).
- Las ventanas (inventario, personaje, party, gremio, quest, comercio, baúl, amigos) son **clases
  C++ con vtable**: 12 vtables, una fábrica `0x48ee60` con 11 casos, un marco común `0x492500` y
  un método de contenido por clase. Campos: id en `+0x10`, estado en `+0x48`, banderas de
  decoración en `+0x4c`, x/y/w/h en `+0x50..0x5c`, título en `+0x98`.
- `ALT` muestra el HP de todos; `PRINTSCREEN` guarda una captura (`0x6202e0`,
  `glReadPixels` + "Screen %02d…").

## Lo que no se sabe

- **Modelos, en el binario** (P-C01): el skinning, la pose, el avance del frame, la cabeza y las UV
  de brillo ya están (arriba). Falta quién escribe el ángulo de la cabeza de los personajes, de
  qué depende cuántos pases de brillo lleva cada item y el resto del render de modelos
  (`0x53c6a0`, tabla de 0xA10 bytes con un `switch` de tipo), leído solo en sus dispatchers. Las
  100 capturas de pantalla de la selección (`graphics-01/CHARACTER-ANIMATION-NOTES.md`, VMware
  MKS, privadas) muestran que la pose y el arma cambian con la UI quieta; no están sincronizadas
  con la traza.
- Qué es cada bloque de `GL_QUADS` (efectos) y el resto de los `GL_TRIANGLES` que no son del
  personaje: la búsqueda por UV se hizo solo contra `player\` e `Item\`.
- Efectos: solo la estela (`0x52e590`).
- El dibujo píxel a píxel de cada widget (dentro de `0x62db60`): las trazas no tienen imagen.
- Para qué usa el juego el caché de `0x466000`, si estas escenas no lo tocan.

Ver [08](08-pendientes.md).

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| OpenGL 1.1 de función fija, modo inmediato, matrices globales | `Render::` sobre OpenGL 3.3 core con shaders (decisión D-C3); las matrices son valores, no estado global | CL5 |
| ~95.000 vértices por frame mandados de a uno | los mismos vértices en buffers que se llenan por frame y se dibujan por lotes; **el orden de dibujo se conserva** donde hay mezcla | CL5, CL7 |
| Colores por vértice recortados a 1 por la función fija | el shader recorta el color a [0, 1] antes de usarlo; sin eso, los colores de 1,34 brillan de más | CL5 |
| Escenas por un entero global | `Scenes::SceneManager` con una clase por escena (`LoginScene`, `CharacterSelectScene`, `GameScene`) y transiciones explícitas | CL5 |
| Viewport en coordenadas de 640 × 480, ortho en píxeles reales, quad en `GL_TRIANGLE_FAN` | `Render::SpriteBatch` con las dos coordenadas: lógicas de 640 × 480 escaladas y píxeles reales, con la misma Y dada vuelta | CL5 |
| Texto por GDI → una textura que se sube por string | rasterizado con una biblioteca portable (FreeType o SDL_ttf), con caché propio (el resultado en pantalla es el mismo); se conserva el byte `0x02` de color | CL5 |
| Ventanas como clases con vtable de 22 métodos | `UI::Window` con una interfaz chica (dibujar, evento) y una clase por ventana; el administrador ordena por prioridad | CL8 |
| Cámara con seis globales, far × 1,4 en el mundo | `Camera` como valor, testeable: matrices de vista y proyección comparadas con las de las trazas | CL7 |
| Terreno celda por celda, dos pasadas por celda | una malla por bloque de 4 × 4 con las mismas reglas de capa, alfa y luz, y el mismo orden | CL7 |
| Modelos transformados en la CPU, un hueso por vértice, pose de `Player.bmd` | skinning en la CPU primero (se compara vértice a vértice con el original); pasarlo a la GPU queda para después, si hace falta | CL6 |
| El cuerpo avanza por paso de lógica con la velocidad de su acción | `Data::Model` guarda la velocidad de cada acción (la tabla de `0x60dbe0`) y el frame se avanza en el paso de 40 ms; el dibujo interpola entre el frame del paso anterior y el del actual (D-C4), y el modo fiel dibuja el del paso, sin interpolar | CL6 |
| Mascota y alas avanzan por frame dibujado (0,5 y 0,25 keys) | avanzan por tiempo: 0,5 keys cada 46,875 ms es la velocidad del original a 21,33 FPS, igual a cualquier FPS (Q-C20) | CL6 |
| Brillo de items con UV de la normal y el reloj del frame | un shader por modo de brillo con las fórmulas de arriba; T es el reloj del frame en ms | CL6 |

Tests que fija:

- La máquina de escenas: transiciones válidas (login → selección → juego) y que una inválida se
  rechace (CL5).
- Parseo de colores inline con `0x02` sobre strings literales (CL5).
- Las matrices de la cámara: para los ángulos, la posición y el viewport de las dos tablas de
  arriba, la proyección y la vista de MuRet contra las de las trazas (CL7).
- El terreno de Lorencia (golden): para cada celda de la traza, posiciones, UV, color, alfa,
  textura y orden, con los datos de `Data\World1` (CL7).
- El skinning (golden): la acción 3 de `Player.bmd` en el frame 3,3197, con las partes del
  personaje de la selección, la escala 0,9 y el giro de 100°, da los vértices del segundo frame de
  la traza a menos de 0,01 (CL6).
- El avance de la animación con un reloj falso: la acción 3 a 0,26 y la 1 a 0,28 keys por paso de
  40 ms, igual con 1, 25 o 240 frames dibujados por segundo; la vuelta con módulo (con y sin
  `lock_positions`) y el tope en n − 0,01 de una acción que no repite (CL6).
- Los saltos de la traza como oráculo: 33, 33, 34 y 34 pasos de la acción 3 dan los saltos de la
  selección, y 20, 19, 19 y 19 de la acción 1 los de Lorencia (CL6).
- La mascota: 0,5 keys cada 46,875 ms, a 21,33 FPS y a 240 FPS (CL6).
- Las UV de brillo: para las normales y el T de un frame de la traza, los modos 115, 120 y 141 dan
  las UV de la traza a menos de 0,02 (CL6).
- El asignador de slots: los casos de `test_texture_slots` de `mu-client` (CL5).
