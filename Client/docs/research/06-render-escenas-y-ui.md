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
| 1 | la carga inicial | `0x622e40`, que dibuja sola, con su `SwapBuffers` |
| 2 | login (servidores, cuenta) | `0x6350b0` |
| 3 | la carga del mundo | `0x6356f0` |
| 4 | selección de personaje | `0x634680` |
| 5 | en el juego | `0x6371a0` |

Quién pasa de una a otra está en
[02](02-arranque-y-ciclo-de-vida.md#las-cinco-escenas-y-sus-transiciones), y la 1 y la 2, en
[La escena de carga y la de login](#la-escena-de-carga-y-la-de-login). *Estático*.

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
- Con el alfa (el último argumento) en más de 0 (`DAT_006b86a0`), cada vértice va con (1, 1, 1,
  alfa) y al final vuelve a (1, 1, 1, 1): **el color que había antes se pierde**, y el quad
  siguiente sin alfa sale blanco (Q-C57). Con alfa 0 usa el color que haya. *Estático*.
- Por eso conviven UI escalada desde 640 × 480 y UI en píxeles reales (el texto, por ejemplo).
- **Las cuentas, en el desensamblado** (CL5.0, `labs/cl5.0-pipeline/README.md`; *estático*).
  `0x620d50` y `0x620d80` no dividen: cargan W o H como entero (`FILD`), multiplican por el
  argumento y después por la inversa en float, `0x3acccccd` (1/640, en `0x6ba070`) y `0x3b088889`
  (1/480, en `0x6ba06c`), las dos un poco más grandes que 1/640 y 1/480: 25 escalado en 640 × 480
  da 25,000002. La cuenta va en la pila de la x87, con los 53 bits que pone Windows, y el que
  llama la guarda en float. En `0x621b60`, el `FST` de `0x621beb` guarda `H − y` (el borde de
  arriba) y lo deja en la pila; `FLD ST0` lo duplica y `0x621bf0`..`0x621bf3` le restan h: el
  borde de abajo sale de `H − y` sin redondear, no del float guardado que muestra el decompilado
  (en 1280 × 1024, con el `0x15`, 287,99997 y no 288). La arena, `0x622000`, hace lo mismo. El
  viewport de `0x620db0` divide sin signo y truncando: `MUL` por `0xcccccccd` y `SHR 9` es
  dividir por 640, y `MUL` por `0x88888889` y `SHR 8`, por 480.
- **La arena**, `0x622000(slot, x, y, w, h, u, v, du, dv)`: el mismo quad, siempre escalado, con
  la textura por `0x620780` y el color que haya, y las UV corridas un cuarto de dv de un lado:
  (u, v + 0,25 dv) arriba a la izquierda, (u, v + 0,75 dv) abajo a la izquierda, (u + du, v + dv)
  abajo a la derecha y (u + du, v) arriba a la derecha. La textura se inclina. *Estático*.
- **El quad sin textura y el estado que dejan** (CL5.2, *estático*). `0x621aa0(x, y, w, h)`
  llama antes a `0x620910(0)`, que apaga `GL_ALPHA_TEST` y `GL_TEXTURE_2D`; escala los cuatro
  argumentos siempre, con `0x620d50` y `0x620d80`, saca el borde de abajo de la pila como
  `0x621b60` (`0x621b01`..`0x621b0b`) y manda cuatro `glVertex2f` sin UV, con el color que haya.
  No vuelve a prender nada. `0x620a00` y `0x620a90` cambian la mezcla solo si es otra que la
  recordada (`DAT_07d366dc`): `glEnable(GL_BLEND)` y `(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` la
  normal, `(GL_ONE, GL_ONE)` la aditiva. Lo demás lo ponen siempre: la normal prende
  `GL_ALPHA_TEST` y `GL_TEXTURE_2D`, y la aditiva apaga el alpha test y prende la textura. No son
  las únicas: la textura la vuelven a prender las siete funciones de mezcla, una por cada valor de
  `DAT_07d366dc` (`0x620980` el 0, `0x620c90` el 1, `0x620a00` el 2, `0x620a90` el 3, `0x620b10`
  el 4, `0x620b90` el 5 y `0x620c10` el 6), y el alpha test, solo `0x620a00` y `0x620910(1)` (que
  apaga la textura). Un `0x621b60` después de un `0x621aa0` sale sin textura, solo con el color,
  hasta la próxima de esas mezclas, y sin alpha test hasta un `0x620a00` o un `0x620910(1)`
  (Q-C75). La imagen de la UI nueva (`0x647650`) deja su `glColor4ub` como el color de GL y prende
  `GL_TEXTURE_2D` si tiene slot o lo apaga si no.
- **El quad del texto** (`0x554000`): `0x621b60(0, x, y, w, h, 0, 0, (w + 0,01) / tw, (h + 0,01) /
  th, 0, escalar_posición, 0)`, con tw × th el tamaño de la textura del texto (256 × 32,
  *verificado*): el slot 0, el tamaño siempre en píxeles y la posición escalada solo si el que
  llama lo pide (su séptimo argumento). Antes acota la x: de 0 a W − w en píxeles o, escalada, a un
  tope que pasa el que llama, con el ancho pasado a 640 por `w · 640 / W` sin signo. La y solo se
  acota con `DAT_077f3b66` prendida (el login la apaga): desde 0 y, en píxeles, con el borde de
  abajo (y + h) hasta `H − 47 · H / 640`; escalada, con la y hasta `433 − h · 480 / H` (Q-C68).
  *Estático*.

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
  └─ 0x465950  dispatch virtual por el objeto de DAT_05763510 (lo elige TextOut, abajo)
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
muestra. El input de texto (`0x468320`) incluye el IME coreano.

**Qué camino corre** (CL5.R, *estático*): el objeto de `DAT_05763510` lo arma `0x4658b0` con el
valor `TextOut` del registro (02), que vale 0 si no está. Con 1, la clase de `0x465a70` (vtable
`0x6b9520`), cuyo método 1, `0x465eb0`, va a `0x466000`: la caché y los colores `0x02`. Con
cualquier otro, la de `0x466ff0` (vtable `0x6b9538`), cuyo método 1, `0x467060`, hace un
`TextOutA` del texto entero sobre el DIB y llama a `0x554000`: **no mira los `0x02`**, que le
llegan a GDI como un carácter más. Las trazas son de este camino (*inferido*: en la VM, `TextOut`
no estaba en 1). `0x467060` también ubica el texto en su caja según un modo (centrado, a la
derecha, recortado), y no le pasa a `TextOutA` un texto que empieza con un salto de línea (el
byte `0x0a`, en `0x4671e1`): su caja sube igual, con el fondo solo. Verificado aparte: el asignador
`0x466560`
(reconstruido en `mu-client` con una sonda en la VM).

**El DIB y la caja** (CL5.3, *estático*): `0x61bcd0` arma el DIB con el slot 0 (la
`FontInput.tga`, de 256 × 32): `DAT_006c7a84` (5) veces su ancho y su alto, 1.280 × 32, de 24 bits
y de arriba hacia abajo. `0x465a00(columna, ancho, filas)` pasa de ahí a la caja de 256 × 32 RGBA
del slot 0: un píxel cuyo primer byte es `0xff` va con el color del texto (`DAT_006c8e5c`) y
cualquier otro, con el del fondo (`DAT_006c8e64`). Lee desde la columna que le pasan sin mirar el
ancho del DIB: con un texto de más de 1.280 columnas, la sexta caja sale de la fila de abajo
(Q-C78). Y `0x4667a0`, el de los `0x02`, reescribe el texto en el buffer del que llama después de
ponerle 256 ceros (Q-C77). MuRet rasteriza cada caja desde su columna con FreeType en un bit
(`Render/Text/`, Q-C76), y una caché (`TextCache`) guarda las cajas de cada texto, fuente y colores
en los slots del texto, después de los 1.845 del original (`TextureSlot.h`): no se sube nada en
cada frame.

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

## La escena de carga y la de login

Leído en la CL5.R en el desensamblado de `c32bbd5f` (`objdump -d -M intel`), con el decompilado
de guía. No hay traza de GL de estas dos escenas: todo es *estático* salvo lo que se marca, y lo
que solo dirían una traza o una captura queda en P-C13 ([08](08-pendientes.md)). Los textos se
nombran por su índice en `Text.bmd` (la tabla de `0x76bd1dc`, 300 bytes por texto), nunca por lo
que dicen. Quién pasa de una escena a otra está en
[02](02-arranque-y-ciclo-de-vida.md#las-cinco-escenas-y-sus-transiciones).

### La carga (escena 1)

`0x622e40(hdc)` corre una vez y dibuja sola, sin el render loop. Pone en 0 la tabla de texturas
(1.845 slots de `0x38` bytes desde `0x7d3a198`) y sube seis de `Interface\`, todas con
`GL_LINEAR` y `GL_CLAMP` (Q-C73) salvo la barra, que repite: `lo_back_01.jpg` (`0x71e`),
`lo_back_02.jpg` (`0x71f`), `lo_mu_logo.tga` (`0x720`), `lo_121518.tga` (`0x721`),
`lo_webzen_logo.tga` (`0x722`) y `lo_lo.jpg` (`0x723`, `GL_REPEAT`). Arma su UI (`0x65eeb0`),
carga los datos del juego (`0x61ccb0`), suelta los seis slots (`0x63c150`) y pasa a la escena 2.

La UI de la carga se diseña en **800 × 600**, con y desde arriba, y no en 640 × 480: `0x65eeb0`
saca los factores multiplicando W y H por las inversas en float de `0x6ba884` (1/800) y `0x6ba880`
(1/600), y se los da a cada imagen (`0x647050(w, h, slot, …, fx, fy)`, ubicada con
`0x6472e0(x, y, 3)`):

| Slot | Tamaño | Posición (800 × 600) |
| --- | --- | --- |
| `0x71e` | 512 × 478 | (0, 60) |
| `0x71f` | 288 × 478 | (512, 60) |
| `0x720` | 93 × 48 | (3, 80) |
| `0x721` | 256 × 206 | (544, 60) |
| `0x722` | 187 × 151 | (307, 225) |
| `0x723`, la barra (`0x6402d0`, `0x6404b0`) | 656 × 15 | (72, 482) |

Las imágenes son de otra clase que el quad de `0x621b60`, **la imagen de la UI nueva**, y no solo de
la carga: la misma clase es el fondo de cada ventana (`0x669c70` la arma y la guarda en `+0x14`, y
la dibuja `0x66a050`), los campos de la cuenta, el aviso y la barra de abajo (19 llamadas a
`0x647650` en el binario). Son objetos de `0xa8` bytes (vtable `0x6baa08`, constructor `0x646fe0`,
armados con `0x647050`) que dibuja `0x647650` con un `GL_TRIANGLE_FAN`, `glColor4ub` (blanco, salvo
que se cambie: el alfa en `+0x60` y el color en `+0x61` a `+0x63`) y
`glVertex2f(x · fx, (H / fy − y) · fy)`: la y sale de `H / fy` (`+0x8`, en `0x647050`), que en la
carga, con fy = H / 600, es 600, y en las ventanas, con fx = fy = 1, es H. Las UV van a medio téxel
del borde: de (0,5 / tw, 0,5 / th) a ((w − 0,5) / tw, (h − 0,5) / th), con el ancho y el alto de la
textura en floats de su slot (`+0x20` y `+0x24`) y el 0,5 de `0x6b882c`. Atan la textura con
`glBindTexture` directo, sin pasar por `0x620780` (Q-C64). La barra (`0x640580(actual, 10)`) mide
`656 · actual / 10` (división entera sin signo) y su u va de 0 a ese ancho sobre el de la textura,
sin medio téxel: con `GL_REPEAT`, la textura se repite a lo largo.

Cada avance es un frame entero: `0x61ccb0` llama 11 veces (de 0 a 10) a `0x65f200(hdc, actual,
10)`, que hace `0x620db0(0, 0, 640, 480)`, `glClear` de color y profundidad, el 2D (`0x6219b0`),
las cinco imágenes, la barra, `0x621a90`, `0x620fc0`, `glFlush` y `SwapBuffers`. Entre uno y otro
corre la carga, sin mensajes: la ventana no responde mientras tanto (*inferido*).

### El login, al entrar

`0x634c60`, la lógica de la escena 2, llama a `0x6349a0` mientras la bandera `DAT_07d3a13d` esté
en 0. Arranca en 0 (está en la `.bss`), y la vuelta al login de `0x4c2710` la pone otra vez en 0
(`0x4c2f24`): **cada vez que se entra al login se carga todo de nuevo**. En orden:

1. el mapa pasa a `0x37`, el 55 (`DAT_006c9dd0`), y `0x61a560` carga ese mundo: `World56` (la
   carpeta de cada mapa, en [05](05-formatos-de-datos.md#terreno-de-un-mundo-dataworldn));
2. `0x61c7c0` sube las texturas de la escena: `Logo\Login_Back01.jpg` y `Login_Back02.jpg` (slots
   `0x15` y `0x16`), `Object9\sand01.jpg` y `sand02.jpg` (`0x5c2` y `0x5c3`), `Effect\FireSnuff.jpg`
   (`0x635`), 13 de `Interface\` en `0xc` a `0x1a` (sin `0x15` ni `0x16`) y el modelo `0xa2`
   (`0x60da30` con `Data\Logo\` y `"%s0%d.bmd"`: `Logo03.bmd`, con sus texturas de `Logo\`). El
   tercer y cuarto argumento van a `glTexParameteri` en `0x63b870` y `0x63bd00`: el tercero a
   `GL_TEXTURE_MAG_FILTER` y `GL_TEXTURE_MIN_FILTER` (`0x2800`, `0x2801`), el cuarto a
   `GL_TEXTURE_WRAP_S` y `_T` (`0x2802`, `0x2803`). Los fondos y casi todas las de `Interface\` van
   con `GL_NEAREST`; la arena, `FireSnuff` y `cr_mu_lo` con `GL_LINEAR`, y repiten la arena,
   `server_ex03` y `server_ex02`;
3. `0x65f300` arma las ventanas de la UI nueva (abajo);
4. `0x4c1340` conecta al ConnectServer ([03](03-red-y-cifrado.md));
5. el objeto `0xa2` en (10.680, 15.550, 1.710), con ángulos (10, 0, 36) y escala 1, y el
   personaje de la escena (`0x517270(2, 4, …)`, con el cuervo del `A9`, P-C11) en (10.603, 14.112,
   580), girado 188° en z y con escala 1,6; un efecto `0xbf`; 120 llamadas a `0x555190` con dos
   cadenas de la `.bss` (*inferido*: vacían el área de mensajes); la música (`0x4acfd0`) y una
   línea en el log.

`0x6349a0` tiene además un camino con `DAT_006c7b8c` en 0 que copia la cuenta guardada al viejo
campo de texto con un tope de 10 (`_DAT_006c8e78`). Es código muerto: `DAT_006c7b8c` está en 1 en
la `.data` y solo `0x4b93a0` lo escribe, también con 1. La cuenta guardada la pone la ventana de
la cuenta (abajo).

### El login, el dibujo

`0x6350b0`, con `DAT_07d3a13d` prendida (si no, no dibuja nada), en este orden:

1. la cámara (`0x635ae0`), la mezcla normal (`0x620a00(1)`) y el color blanco;
2. 2D (`0x6219b0`) y los dos fondos: `0x621b60(0x15, 0, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0)` y el
   `0x16` en x = 320, los dos escalados desde 640 × 480 en posición y tamaño;
3. `0x621a90` cierra el 2D, y un pase que no dibuja: `0x620db0(0, 0, w, 48)`, con w de `0x5bb8d0`
   (un ancho que depende de banderas de las ventanas del juego: *inferido*, el del mundo cuando
   una ventana lo achica), `0x5fbb00(w · (1/640), posición)` (el frustum, *inferido*) y
   `0x620fc0`, que hace `glPopMatrix` de la modelview y de la proyección. En la selección, que
   tiene la misma forma, la traza muestra esa perspectiva sin nada adentro (*verificado*, arriba);
4. `glClearColor(0, 0, 0, 1)` y el pase 3D en `0x620db0(0, 25, 640, 430)`: si la ventana de
   `+0x4578` está cerrada (su bandera de abierta, `+0xc`, es el byte `+0x4584` del administrador),
   el terreno (`0x5fc260`), los personajes (`0x510d00`), los objetos (`0x53c6a0`) y los efectos;
   después `0x6213e0`, `0x549440(0)`, `0x548550(0)` y `glPopMatrix`;
5. 2D otra vez, la mezcla normal (`0x620a00(1)`), `glColor4f(1, 1, 1, 0,5)`, la mezcla aditiva
   (`0x620a90`), `glColor3f(0,25; 0,1; 0,1)` y las dos arenas, `0x622000(slot, x, y, w, h, u, v,
   du, dv)`:
   - `0x5c2` en (0, 25, 640, 430) con u = `((int) T % 100000) · 0,0001`, v = 0 y du = dv = 0,3;
   - `0x5c3` en el mismo lugar con u = `((int) T % 100000) · 0,0005`, v = 0, du = 3 y dv = 2.

   T es `DAT_05767b88`, el `timeGetTime` absoluto pasado a float (Q-C65). Cada 100 s la u salta de
   10 a 0 en la primera y de 50 a 0 en la segunda: con `GL_REPEAT` es el mismo lugar, y no se ve;
6. tres textos con la fuente `DAT_05763554`, `glColor3f(0,8; 0,7; 0,6)`, el texto en blanco
   (`DAT_006c8e5c`), el fondo en `0x80000000` (`DAT_006c8e64`) y el ancho en `0x100`
   (`DAT_006c8e70`): el texto se rasteriza en blanco y el quad, sin alfa, toma el color de GL, así
   que sale en (0,8; 0,7; 0,6). Van apoyados abajo: y = `479 − cy · 640 / W` (con W, no con H:
   Q-C60). El 454 termina en x = 335 (`335 − cx · 640 / W`), el 455 empieza ahí y el 456, armado con
   la versión (`DAT_057630ac`, 02), va en x = 0. Las divisiones son enteras y sin signo, y los
   textos se copian a un buffer de 100 bytes de la pila (Q-C61);
7. el fundido: con `DAT_07d3a0cc` en 1 y la cuenta `DAT_006d5e30` debajo de 16, `glColor3f(k;
   0,9 k; 0,8 k)` con k = (16 − cuenta) / 16 (el 1/16 de `0x6b8850`); con 16, la bandera pasa a 2.
   No dibuja nada con ese color (Q-C66). La cuenta la lleva `0x633920`, por paso: 40 pasos con
   la bandera en 0 (el logo se aclara de a poco, en `0x604ab0`), después la pone en 1, y desde ahí
   cada 128 pasos la cámara elige otro recorrido (en el login, `rand() % 4 + 1` y `rand() % 2`;
   en la selección, siempre el 5), con FOV 45;
8. lo común a las escenas: `0x5f7bc0`, `0x554810` (los avisos centrados, con la fuente negrita
   `DAT_05763558`), `0x5554e0` (si `DAT_006c7a88` es 1 o la escena no es la 5), `0x5b0550`,
   `0x40eca0`, `0x65fe00` (la UI nueva: si hay modo, `0x63cf80` y la lista de `+0x8de0` por
   `vtable + 0x18`), `0x5ac2d0` y `0x633110` (modelos 3D dentro de la UI); al final, `0x621a90` y
   `0x620fc0`.

### Las ventanas de la UI nueva

Las arma `0x65f300` en el administrador de `0x07d53728` (lo da `0x65ee60`; constructor `0x65ec60`),
que pone el modo de la UI (`+0x8dec`) en 2: la carga lo pone en 1 (`0x65eeb0`), la selección en 4
(`0x65f4b0`) y la entrada al juego en 5 (`0x65f5f0`, desde `0x636290`). Cada ventana es una clase
con vtable sobre una base común (`0x669c70(w, h, slot, …)`): x, y, ancho y alto en `+0x18` a
`+0x24`, abierta en `+0xc` y, en `+0x14`, un puntero a la imagen del fondo (una de `0x647650`,
armada con el slot; con -1, un negro con alfa `0x80`; con -2, ninguna). `vtable + 8` la ubica,
`+0x18` dibuja el fondo y llama a `+0x34` si está abierta (`0x66a050`), `+0x30` es su lógica por
frame y `+0x34` su dibujo. Todo en **píxeles de la ventana**, como el mouse de `0x640940` (Q-C47):

| En el administrador | Clase (vtable) | Qué es | Tamaño y lugar |
| --- | --- | --- | --- |
| `+0x8` | `0x6ba938` (`0x642880`, arma `0x6429b0`) | el aviso (abajo) | 352 × 113 con los slots `0x727` y `0x728`, al centro; detrás, la pantalla en negro con alfa `0x80`; botones de 54 × 30 con `0x724` y `0x725` |
| `+0x3d0` | `0x6baa0c` (`0x6477b0`, arma `0x6478c0`) | la que abre Esc (`0x634c60`, con la lista y el menú abiertos y las demás cerradas) | sin leer |
| `+0x7e8` | `0x6ba988` (`0x643fd0`, arma `0x644120`) | sin identificar | sin leer |
| `+0xe40` | `0x6ba8c8` (`0x640ef0`, arma `0x641000`) | la barra de abajo, con dos botones (*inferido* por sus texturas, `server_menu_b_all` y `server_credit_b_all`: el menú y los créditos) | botones de 54 × 30 (`0x10`, `0x11`) y un adorno de 189 × 103 (`0x12`); ancho W − 60, en x = 30 e y = `0,945 · H − 11 − h` (truncado) |
| `+0x10e8` | `0x6ba9c0` (`0x644fd0`, arma `0x6451d0`) | la lista de servidores (abajo) | 21 botones de grupo de 108 × 26 (`0xc`), 16 de servidor de 193 × 26 (`0xd`) con una barra de 160 × 4 (`0xe`), aunque un grupo puede tener 20 (Q-C63), el marco en `0x17` a `0x19`; centrada |
| `+0x4228` | `0x6ba900` (`0x641490`, arma `0x6415c0`) | la cuenta y la clave (abajo) | 329 × 245 (`0x13`), centrada en x y a dos tercios en y: `((W − w) / 2, (H − h) · 2 / 3)` |
| `+0x4578` | `0x6ba848` (`0x63ef00`, arma `0x63f020`) | sin identificar; abierta, tapa el mundo (paso 4 del dibujo) y no deja abrir la lista (`0x4c1860`); con su propia fuente (`0x63f2f2` lee el charset) | sin leer |

Además, `+0x7f50` (vtable `0x6ba810`) es la de la selección de personaje, cuyo botón pasa a la
carga del mundo (`0x633c10`, 02); `+0x8498` (`0x6ba7d4`) la abren los avisos `0x36` y `0x37`.
Un botón marca el clic en `+0xaa` (`0x63c490` lo pone en 1, en `0x63c4fc`).

### La lista de servidores

Llega en el `F4 06` (el `F4 02` del cable, traducido por FoxAnti: [04](04-protocolo.md)) y la
procesa `0x4c1860`. La lista vive en 25 grupos de `0x21e` bytes desde `0x7d36bb0`: el nombre en
`+0`, la cantidad en `+0x14` (un byte) y cada servidor en `+0x2c + 26 · i` (el código, u16 en
`+0`, y la carga, un byte en `+2`). Primero pone en 0 las 25 cantidades. Por cada entrada, el
grupo es g = código / 20 y va a un lugar de la pantalla por una tabla de la pila:

```text
g:     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
lugar 23 22 21 20 19 18 17 16 15 14 13  9 24  8  7  6  5 12 11 10  4  2  1  0 -1
```

El lugar es `23 − tabla[g]` con la tabla
`{0..10, 14, 20, 15, 16, 17, 18, 11, 12, 13, 19, 21, 22, 23, 24}`, salvo el 12, que va al 24. El
nombre del grupo es el texto 559 para el 12, el 552 para el 19, el 1829 para el 20 y el `540 + g`
para los demás: el 12 y el 19 se cambian los nombres (Q-C62). No mira rangos: el 24 va al lugar -1,
antes del arreglo, y un código de 500 o más lee la tabla fuera de la pila; tampoco mira cuántos
servidores lleva un grupo, y el nombre lo copia con un `strcpy` sin tope (`0x4c1a8f`..`0x4c1a9b`),
una vez por servidor, justo antes de leer la cantidad (`+0x14`, en `0x4c1a9d`): un nombre de 20
bytes o más la pisa (Q-C63). Al final, si la ventana de `+0x4578` está cerrada, abre la lista
(`+0x10e8`), le carga los botones (`0x645860`) y abre el menú (`+0xe40`).

**Qué se ve** (`0x645860`): un botón por lugar, del 4 en adelante, **hasta el primer lugar
vacío**, con el nombre del grupo y el color `0xffe2e2e2`. Así, el orden es 20, 16, 15, 14, 13, 11,
19, 18, 17, 10, 9, …, 0 y 12; los lugares 0 a 3 (los grupos 21 a 23) no se ven nunca, y una lista
sin el grupo 20 no muestra ningún botón (Q-C62). Con la capa FoxAnti, el código 0 del CS del
laboratorio llega como 400, del grupo 20: va primero. Con un grupo elegido (`+0x3138`, de 0 a
20), la descripción es el texto `1717 + elegido` en dos líneas (`0x632c90`); si tiene un solo
servidor, un botón con el nombre del grupo; si tiene más, uno por servidor con el nombre y su
número, `código % 20 + 1`. En los dos casos, `0x447c70(grupo, número)` puede sumar al botón un
sufijo de la `.data` (`0x6d6738`) con el color `0xff79ffff` (`0x6459b7` y `0x645abd`). Cada
servidor muestra su carga en una barra (carga / 100) y el texto 560 (128 o más), 561 (de 100 a
127) o 562 (menos de 100). La ventana tiene 16 botones de servidor (`0x6451d0`), pero `0x645860`
arma uno por servidor sin tope (`0x645a99`), y un grupo puede tener 20 (código % 20): del 17 en
adelante escribe sobre las barras, que empiezan donde terminan los botones (`+0x12a0 + 16 · 0xe0`
es `+0x20a0`), y el clic mira solo 16 (Q-C63).

**Los clics** (`0x645e10`, el `+0x30` de la lista): un clic en un grupo lo elige y manda otra vez el
pedido de la lista (`F4 02`); un clic en un servidor, con carga menor que 100, cierra la lista
(`0x65f6c0` con la ventana misma), manda el `F4 03` con su código (u16), muestra los textos 470 y
471 en el área de mensajes (`0x555190`), y guarda el número (`código % 20 + 1`, `DAT_006d5ecc`) y el
lugar del grupo (`elegido + 4`, `DAT_006d5ec4`). Con carga de 100 a 127 abre el aviso 99 (`0x63`);
con 128 o más, nada. La lista la pide `0x4c6070` cuando el CS manda el `00` y la escena es la 2 (la
llamada está en `0x4e33e8`, en el dispatcher). La respuesta al `F4 03` (sub 3, `0x4e3341`) cierra el
CS, conecta al GS (`0x4c1340`), pone `DAT_05767918` en 1 si conectó y, conecte o no, muestra en el
área de mensajes el texto 481 armado con sus datos.

### La cuenta y la clave

La ventana de `+0x4228` la abre el `F1 00` del GS (`0x4c1e20`), con `DAT_05767928` en 0 (si no, solo
guarda `pkt[5..6]`): con `pkt[4]` en 1 la abre y pone el estado de la conexión, `DAT_057678c0`, en
2, así que **aparece con el GS ya conectado** (*estático*; antes, *inferido* por el keepalive de la
CL4.R). Si no, el aviso `0x70`. Con la versión distinta, cierra la ventana de la cuenta y abre el
`0x6f`, una vez por cada byte distinto: compara los cinco sin cortar (`0x4c1ec8`..`0x4c1f0a`).

- **Al armarse** (`0x6415c0`): los dos campos de 156 × 23 (`0x14`) en (x + 109, y + 106) y (x +
  109, y + 131) y los botones OK y cancelar (54 × 30, `0x724` y `0x725`) en (x + 150, y + 178) y
  (x + 211, y + 178). Pone la cuenta guardada en el registro (`ID`, 02) en el campo de la cuenta
  y, si no está vacía, el foco en la clave (`DAT_077f3b70` = 1); si no, en la cuenta.
- **Los campos** son dos controles `EDIT` de Win32 de verdad, hijos de la ventana del juego
  (`0x467fe0`, `CreateWindowExA` con `WS_CHILD | WS_VISIBLE`), dibujados a un DIB y de ahí al juego;
  viven en el objeto de `DAT_057634fc` (`+4` la cuenta, `+0xf0` la clave), que arma `WinMain` y
  prepara `0x469e50`. Los dos con `EM_LIMITTEXT` 10 (`0x467f30`): **10 caracteres**. La clave lleva
  `ES_PASSWORD` (`0x20`), y las dos, `ES_AUTOHSCROLL` (`0x80`), salvo que sean de varias líneas
  (`+0xc4`). El foco es un `EM_SETSEL(0, -1)` que selecciona todo (`0x469370`). El subclass
  `0x467b10` descarta Esc y los caracteres `0x19` a `0x1c`, y a Tab y Enter los toma él (salvo Enter
  en un campo de varias líneas y dos casos más, `0x467c6c`): los manda a la ventana del juego, la
  madre del campo (`+0x84`), como `WM_CHAR`; con `+0x44` prendido, Tab pasa el foco al control de
  `+0x98` (`0x467cdf`) y Enter llama a `0x490b40` (sin seguir). Si en el login `+0x44` está
  prendido, sin leer. Los filtros de solo dígitos o de letras y dígitos en mayúscula (`+0x4c`)
  quedan en 0 en estos campos (los pone en 0 la base, `0x45a210`), así que el resto pasa. El
  carácter de la clave no se cambia (`EM_SETPASSWORDCHAR` no aparece): el de Windows (*inferido*:
  asteriscos). El IME queda apagado al arrancar (`ImmSetConversionStatus(…, 0, 0)` en `WinMain`).
- **Por frame** (`0x6418b0`): un clic sobre un campo le da el foco; OK (`+0x23a`) o Enter (`==
  0x8001`, si el mouse no está tomado) mandan el login, con el sonido `0x19` en el caso de Enter;
  cancelar (`+0x31a`) o Esc (con el sonido) llaman a `0x642810`, que cierra el GS, pone el estado
  en 0, vuelve a conectar al CS y cierra la ventana: el CS manda el `00`, y vuelve la lista.
- **El dibujo** (`0x641990`): el fondo, los dos campos, el control (`0x469350`), los textos 450 y
  451 a la izquierda de cada campo (x + 30, y + 113 y y + 139) con la fuente `DAT_05763560`, en
  blanco y sin fondo, y el título, el texto 460 o 461 (según `0x447c70(grupo, número)`) con el
  nombre del grupo y el número, en (x + 111, y + 80). Las posiciones en píxeles pasan a 640 × 480
  dividiendo por la proporción y truncando (`ftol`), y `0x554000` las vuelve a escalar.
- **El emisor** (`0x641bf0`): solo con `DAT_057678c0` distinto de 0. Cierra la ventana de
  arriba, copia los campos a `0x7798bf8` (cuenta) y `0x7798cf8` (clave) con un tope de `0xff`
  (`0x4693e0`); con la cuenta vacía abre el aviso `0x64` y con la clave vacía el `0x65`; con el
  estado en 2 copia la cuenta a `DAT_0576792c`, pone `DAT_05767928` en 1 y el estado en `0x13` y
  arma el `F1 01` (Q-C43). El resto de la función llama a la tabla de hash de `0x40b240`: puede ser
  el anticheat, y no se siguió.

### Los avisos

`0x65fe50(ui, id)` abre el aviso solo si el modo de la UI no es 0, 1, 3 ni 5: **en el login y en
la selección**, no en la carga ni en el juego (Q-C70). Lo arma `0x6431b0(id)`, que elige el texto
y el tipo de botones, lo pasa a `0x643100(tipo, texto, segundo texto)` y abre la ventana de `+0x8`
(un id que no conoce no abre nada y deja `+0x3bc` en -1):

| Aviso | Texto | Quién lo abre en estas escenas |
| --- | --- | --- |
| `0x15` | 407 | `F1 01` con resultado 0 |
| `0x16` | 414 | `F1 01`, 2 |
| `0x17` | 415 | `F1 01`, 3 |
| `0x18`, 1 y 99 (`0x63`) | 416 | `F1 01`, 4; el 1 además vuelve a abrir la lista; el 99, un servidor con carga de 100 a 127 |
| `0x19`, `0x61` | 417 | `F1 01`, 5 |
| `0x1a`, `0x6f` | 405 y 406 | `F1 01`, 6; el `0x6f`, la versión distinta en el `F1 00` |
| `0x1b` | 408 | `F1 01`, 7, 14, 15, 16, 18 y cualquier otro |
| `0x1c` | 409 | `F1 01`, 8 |
| `0x25` | 433 | `F1 01`, 9 |
| `0x1d` a `0x20` | 410 a 413 | `F1 01`, 10 a 13 |
| `0x26` | 435 | `F1 01`, 17 |
| `0x22` a `0x24` | 597 a 599 | `F1 01`, 192 y 208, 193 y 209, 194 y 210 |
| `0x64`, `0x65` | 403, 404 | la cuenta vacía, la clave vacía (`0x641bf0`) |
| `0x70` | 402 | no se pudo conectar (`0x4c1340`), el `F1 00` sin el 1, y el `FD_CLOSE` (03), que antes muestra el texto 3 en el área de mensajes |

Con el resultado 1 del `F1 01` no hay aviso: el estado pasa a `0x14` (con `DAT_05767928` en 2 y
`0x4ad100`), y `0x634c60` pasa a la selección. El resultado va por una tabla de bytes
(`0x4e5f04`) a una de saltos (`0x4e5ebc`); lo que pasa de `0xd2` va al caso por defecto. El resto
de los ids es del juego o de la selección: `0x36` (1716), `0x37` (396), `0x5d` (471), `0x5e`
(1712, armado con el nombre de un personaje), `0x5f` (1713), `0x60` (1714), `0x62` (439), `0x72`
(1715), `0x79` (390), `0x7a` (391), `0x83` (1711), `0x84` (434), `0x89` (401) y `0x92` (1654).

**El botón** (`0x642bf0`, por el tipo de `+0x3c0`): todos los de esta tabla son del tipo 2, un
OK en (x + 149, y + 72); el `0x5d` es del 0 (sin botón), el `0x5e` del 3 (dos) y el `0x5f` del 4.
Con el tipo 2, Enter y Esc hacen lo mismo que el clic en OK, con el sonido `0x19` (`0x642e30`, por
frame): cierran el aviso (`0x65f6c0`) y siguen según el id (`0x643560`):

- `0x15` y `0x65`: abren otra vez la cuenta con el foco en la clave, y el estado vuelve a 2;
- `0x16` a `0x19`, `0x1b`, `0x1d` a `0x20`, `0x22` a `0x26` y `0x64`: lo mismo con el foco en la
  cuenta;
- `0x1a`, `0x1c`, `0x6f` y `0x70`: `PostMessageA(WM_CLOSE)`, **el juego se cierra** (Q-C69);
- otro: solo se cierra el aviso, salvo dos de la selección: el `0x5e` abre el `0x5f`, y el `0x5f`
  llama a `0x643740` y abre el `0x5d`. El botón de cancelar de los otros tipos (`0x6436a0`) deja el
  id en -1 y cierra.

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
- Para qué usa el juego el caché de `0x466000`, si estas escenas no lo tocan (*inferido*: solo
  con `TextOut` en 1).
- **La carga y el login, en GL** (P-C13): no hay traza ni captura de estas dos escenas; el orden,
  las posiciones y la mezcla de arriba son del binario. Tampoco se leyeron las ventanas de
  `+0x3d0`, `+0x7e8` y `+0x4578`.

Ver [08](08-pendientes.md).

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| OpenGL 1.1 de función fija, modo inmediato, matrices globales | `Render::` sobre OpenGL 3.3 core con shaders (decisión D-C3); las matrices son valores, no estado global | CL5 |
| ~95.000 vértices por frame mandados de a uno | los mismos vértices en buffers que se llenan por frame y se dibujan por lotes; **el orden de dibujo se conserva** donde hay mezcla | CL5, CL7 |
| Colores por vértice recortados a 1 por la función fija | el shader recorta el color a [0, 1] antes de usarlo; sin eso, los colores de 1,34 brillan de más | CL5 |
| Escenas por un entero global | `Scenes::SceneManager` con una clase por escena (`LoginScene`, `CharacterSelectScene`, `GameScene`) y transiciones explícitas | CL5 |
| Viewport en coordenadas de 640 × 480, ortho en píxeles reales, quad en `GL_TRIANGLE_FAN` | `Render::SpriteBatch` con las dos coordenadas: lógicas de 640 × 480 escaladas y píxeles reales, con la misma Y dada vuelta | CL5 |
| Texto por GDI → una textura que se sube por string | FreeType en un bit (D-C2), en las mismas cajas de 256 × 32, con una caché propia (`TextCache`) que las guarda en los slots del texto; los píxeles no son los de GDI (Q-C76); los `0x02`, con la tabla de `0x4665d0` (`ParseInlineColors`) | CL5.3 |
| La carga dibuja sola, con su UI en 800 × 600 y un frame entero por cada uno de los 11 avances | `LoadingScene` con su juego de texturas y la barra, dentro del `SceneManager`, con un frame por avance | CL5.4 |
| El login carga todo otra vez cada vez que se entra (mundo 55, texturas, ventanas, CS) | `LoginScene::Enter` también cada vez, con el mundo como dato de la escena | CL5.4 |
| Siete ventanas de la UI nueva en píxeles, con la imagen de la UI nueva (`0x647650`, la de la carga) en sus fondos; avisos por id con un texto de `Text.bmd` y un botón | `UI::` con la lista, la cuenta y el aviso, y sus imágenes con la forma de la carga (UV a medio téxel, la y desde arriba) en píxeles; la lista con 20 botones de servidor (Q-C63); el id del aviso y su texto en una tabla, la de arriba | CL5.5 |
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
- La carga y el login: la UI de la carga en 800 × 600 y la barra, los quads del login en 640 × 480
  y en 1024 × 768, la arena con el reloj y los tres textos con la fórmula de arriba (CL5.2 a
  CL5.4); y, con la traza de P-C13, contra ella.
- La lista: el lugar de cada grupo y sus nombres por índice, con la tabla de `0x4c1860`, y que un
  grupo de 20 servidores dé 20 botones (CL5.5, Q-C63).
- Los avisos: cada resultado del `F1 01` da el aviso y el texto de la tabla, y qué hace su OK
  (CL5.5).
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
- El asignador de slots de `0x466560` no se prueba, porque MuRet no lo reproduce (pregunta 11 del
  plan de la CL5): es interno, en las escenas medidas no corre (`0x466000` no corrió, § Texto), y
  la caché del texto de MuRet es otra, con sus slots después de los 1.845 del original (CL5.3).
