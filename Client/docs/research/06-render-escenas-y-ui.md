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
 ├─ pase 3D      fondo, frustum 0x5fbb00, terreno 0x5fc260, personajes 0x510d00, objetos 0x6046a0
 │               [selección] personajes 0x510d00, el elegido 0x5f9780 y 0x53fe70 (0x4129f0 no dibuja)
 │               [juego] terreno, objetos 0x6046a0, suelo 0x52dc20, pájaros, personajes 0x510d00,
 │                       peces, caballo, hojas, efectos (§ La escena del juego)
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
culling y `glAlphaFunc(GL_GREATER, 0.25)`. Con `DAT_07d368a0` prendida pondría niebla
**`GL_EXP2`** (`0x801`), con la densidad 0,0004 de `0x6d5d4c` y el color de `0x6d5d50`
(`0x620f60..0x620f8d`), pero esa bandera solo se escribe con 0 (`0x622f23`, `0x6346a7`,
`0x6350cc`, `0x635a33` y `0x63723a`): **la niebla está muerta** (*estático*; la traza tiene un
`glDisable(GL_FOG)` en cada pase, *verificado*). La única niebla del juego es la del mapa 30,
lineal de 2.000 a 2.700, que arma aparte `0x42b0a0`. **El mouse no mueve la cámara**: ni el botón
derecho, ni el del medio, ni la rueda (*estático*, [La cámara del juego](#la-cámara-del-juego)).
En el mapa 6, un héroe cuyo nombre **contiene** "webzen" tiene una cámara de espectador:
`0x576960` busca un substring, así que la rama de "webzen2", que también lo contiene, está muerta
(Q-C152). Antes este documento decía niebla lineal, que el mouse movía la cámara y que había dos
nombres.

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

En el login y en la selección la cámara es fija (CL6.R, *estático* en `0x635ae0`): fov 35 al
entrar (`0x6d5d48`), near 20 (`0x6d5d40`) y el far de `0x6d5d44` según `DAT_07d3a0ec` (con 0:
20.400 en la escena 2, 11.519,9 en la 4 y 2.000 en las otras; con 1 a 4: 2.500, 2.600, 2.950 y
3.200), que `0x620db0` multiplica por 1,4. En la escena 2, x −96,5° (`0x6ba7c0`) más
`DAT_07d325c8`, z −36° y la posición (12.778; 12.678; 579); en la 4, x −84,5° más `DAT_07d325c8`,
z −45° y (13.909; 14.482; 543), la de la tabla. Las rutas de `0x633920` (desde `0x634c60` y
`0x6342c0`) escriben la posición, los ángulos y un fov de 45, pero `0x635ae0` los pisa antes de
`0x620db0`: no llegan a los pases de la escena. Sí llegan al primero de cada frame, el
`0x620db0(0, 0, 640, 480)` de `0x637700`, que no dibuja nada: en la traza de la selección va con fov
45, X −87°, Z −10° y la posición (335,88; −753,12; 250) (*inferido*: los de una ruta; antes este
documento decía que no llegaban a GL). Ese pase va **antes del `glClear`** del frame, y su
`glDepthMask(true)` es lo que deja limpiar la profundidad cuando la mezcla aditiva del 2D del
frame anterior apagó la máscara (*verificado* en los cinco frames de las dos trazas). La
perspectiva del 2D, antes de su `glOrtho`, va sin el × 1,4. MuRet prende la máscara antes de
limpiar, sin el pase (CL6.3, `GlFrameTargetTest.EachFrameClearsTheDepth`), y `MakePass3D` da la
perspectiva y la vista de la franja y del mundo de la selección, y del mundo de Lorencia con su
cámara, en cada frame (`GoldenPass3DTrace`).

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
  está dada vuelta en Y respecto de la grilla. Coincide (±1 %) en 1.746 de los 1.875 vértices que se
  pueden medir (el rojo de la traza por encima de 8 / 255, en los dos pases); los otros 129 salen más
  brillantes, nunca más oscuros (×1,1 a ×3,1): las luces dinámicas que `0x5f9780` suma a la base.
  De los 27 vértices distintos, 23 rodean el `FireLight01` del registro 1933 (*verificado* por el
  lugar) y 4 siguen sin dueño ([Lo propio de Lorencia](#lo-propio-de-lorencia), P-C28 e) (CL7.1,
  `GoldenTerrainTrace.TheCellsOfLorenciaMatchTheTrace`; antes este documento daba 1.869 y 123).
- **El estado de GL de los dos pases** (*verificado* en la traza, CL7.1): el opaco (`0x5fa260`,
  `0x620980`) va sin mezcla, con culling y depth test y escribiendo la profundidad, en los 329; el de
  alfa (`0x5fa350`, `0x620a00(1)`) con `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`, el alpha test de la
  traza (`GL_GREATER`, 0,25), sin culling, con depth test y escribiendo la profundidad, en los 141. Son
  las mezclas `Opaque` y `Alpha` del `ModelBatch`.
- **La luz de cada vértice** sale de `0x7b4ccc4` (*estático*, `0x5fa260` y `0x5fa350`): la luz
  sombreada de `0x5f8d90`, que `0x5fbf60` copia de `0x7c0ccc4` en cada paso y donde se suman las
  luces dinámicas ([La luz del terreno en un punto](#la-luz-del-terreno-en-un-punto)). En la zona plana
  de la traza da lo mismo que el píxel del OZJ.
- **El orden importa** para la mezcla y está verificado: bloques de 4 × 4 por (fila de bloque,
  columna de bloque), dentro del bloque las celdas por (y, x), y el pase de alfa de una celda
  inmediatamente después de su pase opaco (los 28 bloques y las 141 celdas con alfa de la traza).
- **Qué bloques y qué celdas** (*verificado* en las dos trazas y *emulado*, CL7.1, `World::TerrainCulling`;
  la lectura y los scripts, `scripts/login/frustum_reference.py` y `frustum_emu.py`):
  - **El rango** (`0x5fbb00`, con la modelview de `0x620db0` que lee de GL): D = far · 0,9 en float
    (el far sin el × 1,4, `DAT_006d5d44`), w = tan(fov · `0x3c0efa33`) · D · k + 100 en el registro
    (k = ancho de `0x5bb8d0` · 0,0015625, 1 con 640), W = w y H = w · 0,75 en float; los cinco puntos
    (0, 0, 0) y (∓W, ±H, −D) pasan al mundo con la modelview transpuesta más la cámara; con el mínimo y
    el máximo de x e y (desde 25.600 y 0, comparando estricto), x0 = (−1 − ftol(min · −0,01f) / 4) · 4 y
    x1 = 4 − (ftol(max · −0,01f) / 4) · 4, con el producto en 53 bits y la división de C, y cada uno entre 0
    y `0xfb`. El 251 no es múltiplo de 4: el lazo de a 4 para en el bloque 248, que llega a la fila 251.
  - **El trapecio** (`0x5fb570`, con el nivel de cámara 0): lo lejos K y el factor de ancho f de cada
    escena (login, selección y el resto), lo cerca N = K · 0,19 y el objetivo T = K · 0,47; las esquinas
    (−A · 1.190; K − T), (A · 1.190; K − T), (A · 540; N − T) y (−A · 540; N − T), con A = 640 · f, giradas
    solo en yaw (−yaw de la cámara en el mapa `0x37`, 45° en los demás), más el héroe y por 0,01 en float.
  - **El test** (`0x5fba90`): los cuatro lados (3, 0), (0, 1), (1, 2) y (2, 3), con
    (Y[j] − y)(X[i] − x) − (X[j] − x)(Y[i] − y) en double; pasa si los cuatro dan más que el margen. Cada
    bloque se prueba en (x + 2, y + 2) con −40 (`DAT_006c9d60`) y cada celda en (x + 0,5, y + 0,5) con 0.
    En las escenas que no son 2, 4 ni 5 pasa todo.
  - En Lorencia (cámara (15.057,106; 11.342,894; 1.015), héroe (14.350; 12.050; 165), escena 5) dan las
    329 celdas de la traza en su orden; en la selección (héroe (12.975; 15.423; 333), escena 4), 7.746
    pasan y 5.109 se dibujan, las de su traza en su orden (`GoldenTerrainTrace`). Que el margen o el
    centro sean otros rompe los dos goldens, y un test sin traza (`TheGamePolygonCutsByBlockWithItsMarginAndThenByCell`)
    pide celdas que solo se dibujan por el margen de su bloque. Lo único que no sale del binario es la
    modelview, que arma el driver. MuRet le pasa la de glm (`Render::MakePass3D`), que difiere de la de
    Mesa en los últimos bits en el 84 % de las cámaras: en las tres de las trazas y en 18.000 al azar el
    rango, el trapecio y las celdas dan lo mismo que la referencia, y en 101 de 19.082 cámaras puestas al
    lado del borde de un bloque el rango se corre un bloque (con la de Mesa, en ninguna). Con la cámara
    fija del login no cambia nada; con la que se mueve en el juego, P-C24.

### Modelos y objetos

**Verificado** contra las trazas, incluido el skinning del personaje. En qué funciones del binario
se hace lo leyó la CL6.R ([El modelo en el binario](#el-modelo-en-el-binario)):

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
  (`HelmMale10`, `ArmorMale10`, `PantMale10`, `GloveMale10`, `BootMale10` y la lanza `Spear08`), y
  el pase 3D de 1° de la UI dibuja `Potion04` y `Potion07`. `MaskHelmMale10` no se dibuja: de sus
  cuatro mallas solo `guard_hair` tiene un bloque, y es el de la malla igual de `HelmMale10` (con
  las dos habría dos). Antes este documento la contaba entre las partes del guardia.
- **Skinning rígido.** Ajustando cada hueso por separado, los vértices del `.bmd` caen sobre los de
  la traza a menos de 0,001 unidades con escala 0,9: un hueso por vértice, sin pesos.
- **La pose sale de las acciones de `Player.bmd`**, con el frame fraccionario:

  | Escena | Modelo | Acción (keys) | Frame en los 5 swaps | Error máximo |
  | --- | --- | --- | --- | --- |
  | Selección | personaje | 3 (7) | 1,740 · 3,320 · 4,900 · 6,740 · 1,580 | 0,0014 |
  | Lorencia | personaje | 1 (6) | 1,801 · 1,401 · 0,721 · 0,041 · 5,361 | 0,0014 |
  | Lorencia | guardia | 1 (6) | 0,681 · 0,281 · 5,601 · 4,921 · 4,241 | 0,001 |

  Los frames son los de la pose de MuRet (CL6.2, `GoldenSkinningTrace`), buscados de a 0,0005
  keys: con ellos cada vértice, la cabeza incluida (abajo), cae a 0,0014 del de la traza, lo que
  redondea un float en coordenadas de 15.000. Antes la tabla daba 1,737 · 3,320 · 4,896 · 6,739 ·
  1,578 (0,015), 1,820 · 1,411 · 0,731 · 0,042 · 5,358 (0,058 sin la cabeza) y 0,675 · 0,279 ·
  5,583 · 4,908 · 4,238 (0,047), ajustados con otra medida del error. Los nuevos saltan justo lo
  que dan 0,26 y 0,28 keys por paso (abajo): 1,58 y 1,84 módulo 7, y 5,60 y 5,32 módulo 6.

  El objeto solo agrega la escala 0,9, un giro alrededor de Z (100° en la selección; 116° el
  personaje y 90° el guardia en Lorencia) y su posición. En Lorencia los dos están en el centro de
  una celda, a z = 165, la altura del terreno en esa zona (byte 110 × 1,5): el personaje en
  (14.350, 12.050), la celda (143, 120), y el guardia en (14.350, 12.850), la (143, 128). El lugar
  del guardia sale de la traza: en el `MonsterSetBase.txt` del servidor en uso
  (`client-research/mu-golden/servidor-en-uso-vm2003/` de mu-server-repack-research-102i) no hay
  nada del mapa 0 cerca de esa celda.
- **La cabeza gira aparte.** En Lorencia el hueso `Bip01 Head` (20) no sigue la animación: con la
  pose de la animación sola, el casco queda a 0,51 unidades del de la traza en el personaje y a 10
  en el guardia, con el resto del cuerpo a 0,001. En la selección la cabeza sigue la animación.
  *Estático*, en `0x4eb860`: al hueso de la cabeza del modelo (su índice está en `modelo + 0x54`) le
  resta a los ángulos de Euler x y **z** un ángulo del objeto (`objeto + 0x28` al x y `+ 0x2c` al z,
  en grados, por el π/180 de `0x6ba140`, que es `0x3c8efa36`, un ulp más que el de `0x6ba360`;
  `0x4eb9ff`..`0x4eba1d`) en las dos keys que interpola, antes de pasar a cuaternión (`0x5fcd20`);
  los demás huesos usan los cuaterniones ya calculados. (Antes este documento decía x e y.) El
  ángulo lo escribe `0x505de0` (`0x506972`..`0x5069a9`): `+0x28` y `+0x2c` giran hacia `+0x34` y
  `+0x38` (× 0,2, con `0x4e9560` y `0x4e96f0`), y a quien no es el personaje propio ni se está
  muriendo, con `rand() % 32` en 0, le elige un objetivo al azar (x de −64 a 63; z de −16 a 15, más
  360 si es negativo). No corre con `+0x16c` en 0 (no visible, `0x5fba90`) ni con los bits `0x20` o
  `0x1000` de `+0x78`, y `0x4e8090` los pone en 0. *Estático*; el tramo de antes, `0x505ef8`..,
  parece del anticheat y no se leyó. Si la cadena de la selección llega a `0x505de0`, sin leer: la
  traza dice que ahí la cabeza sigue la animación (P-C14).

  **Verificado** en Lorencia (CL6.2, `GoldenSkinningTrace`): con los ángulos que siguen, cada
  vértice del casco cae a 0,0014 del de la traza, lo que confirma la resta en x y en z, su signo y
  los grados. El guardia mira a (−18, 4)°, un objetivo entero dentro de los rangos del azar: en el
  primer swap está en (−22,399, 0,0205)°, y 20 pasos de un 20 % lo dejan en (−18,0507, 3,9541)°,
  donde la traza lo pone en (−18,0505, 3,9545)°; 19 más, en (−18,0007, 3,9993)°, y después llega.
  El giro corre una vez por paso de la animación, como el avance del cuerpo, y no una vez por frame
  dibujado: con un 20 % por frame, en el segundo swap estaría en (−21,52, 0,82)°. El personaje
  propio mira a (0, −0,65)° en los cinco swaps (en el primero le falta 0,0065° en x): quién le pone
  ese objetivo, que no sale del azar, sin leer (P-C17 a).
- **Pases extra de brillo.** Las mallas `Male17` y la espada se dibujan tres veces más, con las
  mismas posiciones y UV que no están en el archivo (texturas GL 141, 115 y 120); las del guardia y
  las pociones, una vez más (la 120). Las UV salen de la normal de cada vértice en el mundo
  (`nx`, `ny`, `nz`) según banderas del dibujo, en `0x4ecc30` (no en `0x4ed890`, que es código
  muerto: § El modelo en el binario). **Verificado** ajustando las UV de la traza sobre las normales
  del `.bmd` posado (residuo máximo de 0,016 en las dos escenas):

  | Textura | Bandera | u | v |
  | --- | --- | --- | --- |
  | 115 | `0x8` (slot `0x5fc`) | 0,5·nz + 0,2 | 0,5·(ny + 1) |
  | 120 | `0x4` (slot `0x5c0`) | 0,5·nz + t | 0,5·ny + 2t |
  | 141 | `0x1000` (slot `0x620`) | c·nx + (s + 0,5)·ny + nz + 3s | 1 − c·nx − s·ny − 1,5·nz − 3t |

  con t = (T mod 10000) · 0,0001, c = cos(0,001·T) y s = sin(0,002·T), donde T es el reloj del frame
  en ms (abajo). En la selección, T es 26.437.420 · 26.438.716 · 26.440.046 · 26.441.452 ·
  26.442.812 ms en los cinco swaps (unas 7,3 horas de encendida la VM, pares porque el float de
  `0x05767b88` ya no guarda los impares): el pase 120 da T módulo 10.000 y el 141 el resto, y con
  ellos las UV de las tres pasadas caen sobre las de la traza con las normales giradas por los
  huesos y **sin la escala** del objeto (escaladas por 0,9, el pase 120 queda a 0,049). CL6.2,
  `GoldenShineTrace`. En Lorencia, T es 30.530.748 · 30.531.530 · 30.532.328 · 30.533.092 ·
  30.533.858 ms (unas 8,5 horas), y caen igual los tres pases del personaje y el 120 del guardia, a
  0,00002, con la cabeza girada como en § La cabeza gira aparte. (Antes este documento daba el 115
  sin bandera: es la `0x8`.) Los otros cinco modos, sin pase en estas trazas, son *estáticos*, y
  cuántos pases lleva cada ítem lo decide su nivel: los dos, en
  [El modelo en el binario](#el-modelo-en-el-binario). Los tres pases del personaje de las trazas
  son los de un nivel 13 (*inferido* por cuáles y en qué orden).

#### Cuánto avanza la animación

**Verificado** en el binario y en las trazas: **el cuerpo avanza por paso de lógica de 40 ms, con
la velocidad de su acción; la mascota y las alas, por frame dibujado.**

- **La velocidad es de la acción.** Cada acción del modelo tiene 0x10 bytes: `+0` no repite (se
  queda en la última key), `+4` la velocidad en keys por paso, `+8` la cantidad de keys y `+10`
  `lock_positions`. El lector no escribe `+4` (Q-C87, [05](05-formatos-de-datos.md#modelos-bmd)):
  el cargador del personaje (`0x60dbe0`) las fija a mano en las acciones de `Player.bmd`, al final
  y después de `Shadow` (`0x60f349`..`0x60f754`, *estático*). Son 181 escrituras en este orden, y
  las de después pisan a las de antes (acciones y velocidad):

  ```text
  1..12 0,28 · 3 0,26 · 4..5 0,24 · 7..8 0,22 · 13..33 0,3 · 21 0,35 · 29 0,35
  152..194 0,32 · 195..196 0,45 · 197..204 0,4 · 194 0,4 · 171..172 0,28 · 201..202 0,2
  64..65 0,2 · 138 0,24 · 139..140 0,3 · 141 0,24 · 67 0,45 · 72..74 0,3 · 75 0,3 · 76 0,2
  77 0,28 · 78..79 0,3 · 80 0,28 · 81..82 0,3 · 83 0,2 · 84..85 1,0 · 86..125 0,45
  106..117 0,71 · 118..121 0,4 · 93 0,3 · 88 0,3 · 90 0,3 · 89 0,28 · 91 0,3 · 137 0,3
  ```

  Quedan 148 acciones con velocidad y 57 sin ninguna (la 0, de la 34 a la 63, la 66, de la 68 a
  la 71, de la 126 a la 136 y de la 142 a la 151), con lo que dejó el heap: la 0 la usa el
  preview de la ventana de crear (`0x63e430`), y la 68 se multiplica por 0,5 en `0x4f9580`, que
  pasa a 0 una velocidad negativa o un NaN. Las cuatro que no repiten (`+0` en 1) son la 195, la
  196, la 193 y la 68. Los `NewFace` de la selección (`0xa8` a `0xac`) llevan 0,3 en sus acciones
  0 y 1 (`0x61ca30`).
- **El paso la aplica.** Dentro del bucle de pasos de `0x637700` ([02](02-arranque-y-ciclo-de-vida.md)),
  la lógica de la escena llega por `0x508f00` → `0x508ea0` → `0x4fd5f0` → `0x4fcd00` a `0x4f9580`,
  que multiplica la velocidad según el tipo y la acción del personaje (×1,5, ×0,5, ×2, ×4 y
  otros; los `NewFace` × 2, de 0,3 a 0,6; con el bit `0x1000` de `+0x78`, no avanza), y a
  `0x4ec200`, que hace `frame += velocidad` con la acción de `modelo + 0xa0` (sus argumentos 5 y 6
  no se usan). Con el frame en 0 o menos le suma n − 1; al pasar la última key vuelve con módulo
  (n keys, o n − 1 con `lock_positions`), o se queda en n − 0,01 si la acción no repite. Cambiar
  de acción (`0x4ea030`) vuelve el frame a 0 solo si la acción es otra, y guarda el de antes tal
  cual, sin truncar (`0x4ea072`..`0x4ea078`): da lo mismo, porque la pose trunca.
- **Las trazas lo confirman con dos velocidades.** En la selección (acción 3, 0,26) el frame salta
  1,58 · 1,58 · 1,84 · 1,84 keys entre swaps: 8,58 y 8,84 módulo 7, o sea 33, 33, 34 y 34 pasos.
  En Lorencia (acción 1, 0,28) el personaje y el guardia saltan 5,60 · 5,32 · 5,32 · 5,32 keys
  módulo 6: 20, 19, 19 y 19 pasos los dos. Con los frames de la tabla, los saltos son exactos.
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
  real (Q-C21). Con el aviso, `0x4fcd00` (`0x4fce25`) pone `+0x3c4` del personaje en −1 y `+0x3e0`
  y `+0x3e1` en 0, y vuelve a elegir la acción de parado (`0x4fcb10` → `0x4f2250`); si es la misma,
  `0x4ea030` no hace nada: en pantalla no cambia nada. *Estático.* La acción de parado sale de la
  mascota (`+0x318`), de las alas (`+0x2f8`) y del arma; la 3 de la traza va con un arma de una
  mano (*inferido*, P-C14).
- En el guardia, el frame f y el 6 − f dan casi el mismo error (sobre sus vértices la acción 1 es
  casi simétrica); en la tabla va la rama que sigue la secuencia. En el personaje no hay duda: en
  cuatro de los cinco frames la otra rama queda a más de 4 unidades (en el quinto, cerca de la key
  0, a 0,16).

#### El modelo en el binario

Leído en la CL6.R en el desensamblado de `c32bbd5f`: todo *estático* salvo lo que se marca. El
archivo y su lector están en [05](05-formatos-de-datos.md#modelos-bmd); acá, lo que pasa después.

- **Lo que carga cada cargador** (CL6.1, *verificado* emulando el `main.exe` con
  [`scripts/emulate_model_loaders.py`](scripts/emulate_model_loaders.py)): `0x60dbe0` hace 213
  llamadas a `0x60da30` (de `Player.bmd` a `Shadow01.bmd`) y las 181 escrituras de velocidad de
  abajo; `0x60ff50`, 243, todas con un modelo distinto, de las que faltan en el laboratorio los
  tres `giftbox`; y `0x61ca30`, `Logo05` y `NewFace01` a `05` de `Logo\`. Las filas, con la
  dirección de cada llamada, son `PlayerModelFiles` e `ItemModelFiles` de
  `Client/src/Data/Models/PlayerModels.cpp`.
- **El modelo y el objeto.** La tabla de modelos tiene `0x489d` de `0xe0` bytes, con la base
  corrida `rand() & 0x3ff` modelos (contra quien lea la memoria). Del modelo cuentan acá `+0x44`
  (con luz por vértice), `+0x48..+0x50` (el color), `+0x54` (el hueso de la cabeza: 20 en
  `Player.bmd`, que pone además `+0x58` en 6 y `+0x5c` en 13, los pies, *inferido*), `+0x68` (la
  escala), `+0x6c` (la posición), `+0x78` (los ángulos), `+0x84` (la z de más del hueso 0 con
  `lock_positions`), `+0x88` (la malla que va sin luz), `+0x98` y `+0x99` (la piel, abajo) y
  `+0xa0` (la acción). `0x5ffdf0(objeto, bandera, modo, _)` los copia del objeto (la escala de
  `+0xc`, la posición de `+0x10` y los ángulos de `+0x1c`), pone `+0x84` en 0 y posa con
  `0x4eb860`: el frame (`objeto + 0x114`), el anterior (`+0x118`), la acción anterior (`+0x112`),
  los ángulos, los de la cabeza (`objeto + 0x28`), 0 y `!bandera`, con las matrices en
  `objeto + 0x120` o en el global `0x68b181c`. `+0x84` queda en 0 en los personajes (`0x5ffdf0`,
  `0x6001b0` y `0x509340`); lo cambian `0x5e0720` (un valor por parte) y `0x5208a0` (±30).
- **La pose** (`0x4eb860`, `thiscall`, `ret 0x20`). La key anterior es el frame anterior truncado,
  en 0 si es negativa o se pasa de las keys de la acción anterior. Dos cuaterniones iguales a
  0,001 (`0x5fc700`) copian el primero; si no, slerp (`0x5fce90`). El hueso 0, con
  `lock_positions` en la acción anterior o en la de ahora, toma x e y de la key 0 de la de ahora y
  suma `+0x84` a la z interpolada; no lee las posiciones de `lock` de la acción. El séptimo
  argumento en 0 arma la raíz con los ángulos del modelo (`0x5fc8f0`, en el global `0x68c4a1c`);
  con otro valor usa lo que dejó quien llama en ese global (así cuelgan las armas, abajo). El
  octavo, con el séptimo en 0, multiplica la raíz por la escala y le pone la posición; en 0, la
  raíz queda sin las dos y las pone `0x4ebc80` en cada vértice. Los hijos, con
  `R_ConcatTransforms` y el padre; los dummy no se tocan.
- **Los vértices** (`0x4ebc80(matrices, objeto + 0x124, + 0x130, + 0x13c, bandera)`, `ret 0x14`).
  Cada vértice por la matriz de su hueso y, con la bandera, por la escala más la posición del
  modelo. Con `DAT_006c881c` distinto de 1, además, `VectorRotate` por esa escala más la
  traslación del hueso: lo pone `0x5ffdf0` después de la pose, en 1 salvo en el modo 3 (1,4) y el
  2 (1,2), los dos desde `0x602e90`, y en el modo 1, el contorno del personaje bajo el mouse
  (`0x510d00` con `DAT_006c8e30`): dos pases de `0x60cd60` sin luz, en (0,1; 0,01; 0) a 1 + 0,1 /
  escala y en (0,7; 0,07; 0) a 1 + 0,04 / escala, o con la escala de `objeto + 0x94` si no es 1,2
  comparada por bits (Q-C94), y en verde con `objeto + 0x88` en 4 fuera de los mapas 18 a 23. La
  luz por vértice, solo con `+0x44` (copia de `objeto + 0xe8`): L = (1,3; 0; 2), la de
  `DAT_006c8818`, que vale 1 desde el `.data` y nadie escribe (las ramas de (0; −1,5; 0) y del mapa
  30, (0,5; −1; 1), no corren nunca: Q-C115, verificado en la revisión de la CL6.4 PR B), y cada
  normal girada por su hueso, sin normalizar, da max(0,2; 0,8 · L·n + 0,4). La
  misma función deja la caja de picking en `objeto + 0x13c` (`+0x124` y `+0x130` más la
  posición) y escribe `modelo + 0xc4` con un valor de la pila sin inicializar (Q-C93).
- **El dibujo de una malla.** `RenderBody` (`0x4ef0c0`, `ret 0x20`: las banderas, el alpha, el
  `blendMesh`, su luz, U, V, la malla oculta y una textura forzada) abre un `glPushMatrix`,
  recorre las mallas y llama a `0x4ecc30` (`thiscall`, `ret 0x20`), la que dibuja. `0x4ed890`
  (`ret 0x28`), la que decía este documento, es **código muerto**: su único llamador, `0x4ef290`,
  no tiene llamadas ni aparece su dirección en el archivo (lo mismo `0x4efea0`, desde `0x4ef340`),
  y adentro tiene una tabla de hash (`0x4ee075`..`0x4ee500`, protección: no se leyó). Del script
  de la textura ([05](05-formatos-de-datos.md#modelos-bmd)), `0x4ef0c0` mira el byte 0 (esa malla
  es el `blendMesh`), el 1 (no se dibuja) y el 4 (un segundo pase en negro con `0x21` o `0x22`,
  que no corre: `0x459940` lo deja en 0), y `0x4ecc30` el 2 y el 3. `0x4ecc30`:
  - sale con la malla fuera de rango o sin triángulos; T = `ftol(DAT_05767b88) % 10000` y
    t = T · 0,0001;
  - el slot es `modelo + 0x38[textura]`: `0x12d` no se dibuja, `0x12e` va a `0x12e` más
    `modelo + 0x98` y `0x137` a `0x12f` más `modelo + 0x98` (los dos salteados con
    `modelo + 0x99`), `0x41` a `0x41` más `DAT_07d2d360`, y una textura forzada distinta de -1
    pisa todo;
  - corre las UV con (U, V) distintos de 0 en el `blendMesh`, o con el byte 2 del script en la
    malla de `modelo + 0x88`;
  - la malla de `modelo + 0x88` va con `glColor3fv(color)` y sin luz; las demás, con `+0x44`, con
    la luz de cada normal por el color;
  - las banderas:

  | Bandera | Qué hace |
  | --- | --- |
  | `0x1` | sin textura: `0x40` aditiva (`0x620a90`), `0x80` `0x620b10`, si no opaca (`0x620980`); `0x2000` sin profundidad (`0x620870`); alpha < 0,99 → `0x620a00(1)` y `glColor4f` |
  | la malla cuya textura es el `blendMesh` (todas, con uno de −2 o menos) | con textura, aditiva (`0x80`: `0x620b10`), `0x2000` sin profundidad, y `glColor3f(color · luz del blendMesh)` |
  | `0x2` | con textura: `0x40` aditiva, `0x80` `0x620b10`, alpha < 0,99 o un slot de cuatro componentes (`+0x28` en 4) → `0x620a00(1)`; si no, opaca |
  | `0x40` sin `0x2` | aditiva y sin UV; un slot de cuatro componentes no se dibuja |
  | `0x20` | la sombra plana: X = x' + (x' + 2000) · z' / (z' − 4000) + px, Z = pz + 5 |
  | `0x400` | la onda: v + sin((T + 931 · i) · 0,007) · n · 28, con i el vértice |
  | `0x4`, `0x8`, `0x200`, `0x800`, `0x1000`, `0x4000`, `0x8000`, `0x10000` | un pase de brillo (abajo) |

  Las mezclas no tocan `glDepthFunc` y guardan la última en `0x7d366dc`: `0x620980` opaca (sin
  mezcla, con culling y máscara, sin alpha test, la niebla según `0x7d368a0`), `0x620a00(b)`
  `SRC_ALPHA/ONE_MINUS_SRC_ALPHA` sin culling, con alpha test y la máscara si b, `0x620a90`
  `ONE/ONE` sin culling, máscara ni niebla, `0x620b10` `ZERO/ONE_MINUS_SRC_COLOR`, `0x620b90`
  `ONE_MINUS_SRC_COLOR/ONE`, `0x620c10` la normal sin máscara ni alpha test y `0x620c90`
  `ZERO/SRC_COLOR`. `ModelBlend::Multiplicative` reproduce este último: también llega por
  el modo raw1 del backend, sin culling, alpha test ni escritura de profundidad. Tres pruebas
  con GL real observan el producto de color, la profundidad y la cara posterior; no se deduce
  el resultado de comparar sólo el enum o las llamadas del backend.
- **El script en un personaje** (CL6.3 PR F, *estático*). `RenderBody` recibe de su objeto cinco
  valores: `+0x58` (la malla que no dibuja), `+0x64` (el `blendMesh`), `+0x68` (su luz) y `+0x6c` y
  `+0x70` (U y V). `0x511290` arma el del jugador con −1, −1, 1, 0 y 0, y `0x60b3d0`, `0x60b130`,
  `0x60b300`, `0x609640` y el contorno (`0x60cd60`) se los pasan; lo que cuelga lleva otro objeto
  (abajo, "Lo que cuelga"). Así, en cada pase:
  - una malla con `H`, o la del `+0x58` del objeto, no se dibuja (`RenderBody` no llama a
    `0x4ecc30`);
  - `RenderBody` le pasa a `0x4ecc30` el `blendMesh` del objeto o, con `R`, el índice de la malla
    (`local_8`, que vuelve al del objeto en cada malla), y `0x4ecc30` lo compara con **su textura**
    (`+2`): la malla va por esa rama si son iguales o si el `blendMesh` es menor que −1 (todas). En
    las 32 mallas `R` de `Player\` y de `Item\` del laboratorio, la textura es la de su índice. Esa
    rama va antes que la de `0x2` y la de `0x40`: en el pase base, en el `0x42` y en el contorno, la
    malla va con su textura, `0x620a90` (aditiva, sin escribir la profundidad) y el color por la luz
    del `blendMesh`, aunque sea de cuatro componentes. Los pases de brillo van antes en `0x4ecc30` y
    no cambian, ni su color: el `0x240` y el `0x1040` van con el color base, sin esa luz. En el
    laboratorio son 17 de `Player\`, el pelo de los cascos (`hair_R.jpg`: `HelmMale26` a `29`,
    `HelmClass05`, `HDK_HelmMale05` y los cinco `MaskHelmMale`), `FShoots_R` de `HelmMale25` y
    `PantMale25` y `lowerTest_R` de los cuatro `Test20`, y 15 de `Item\` (`Bow18` a `20`, `Mace08` y
    `12`, `Staff11` y `12`, `Sword20`, `Wing05` y `06`, `shield17`, `spear11` y `skillScroll`);
  - con `N`, o con el `+0` de `0x60ff50`, no hay pases de brillo; el `0x42` sí;
  - con U o V distintos de 0, `0x4ecc30` se los suma a las UV (`local_5`) de la malla cuyo **índice**
    es el `blendMesh` que recibe (con `R`, la suya), de la que tiene `S` y de la de `modelo + 0x88`
    (−1, salvo en el pase del chrome de `Spear10`), en los pases con textura; y a las de todas en el
    modo `0x1000` (el `0x1040`) y el `0x8000`. En un jugador U y V son 0. La única malla con `S` del
    laboratorio es la de `Item\Quest03`.

  La luz del objeto del jugador la escriben dos: `0x50ba30` en la parte del casco de la clase 4
  (abajo) y `0x60b3d0` por el modelo que dibuja, antes de sus pases y después del contorno:
  sin(0,001 · T) · 0,4 + 0,6 con `PantMale25` y `HelmMale25` (`0x142a` y `0x102a`, los de los
  `FShoots_R`) y 1 con `HelmMaleTest20`, `GloveMaleTest20` y `BootMaleTest20` (`0x1025`, `0x1625` y
  `0x1825`). Nadie más la vuelve a 1, así que queda de una parte a la siguiente y de un frame al
  otro. MuRet la recorre en el orden del lazo y empieza cada frame con la que dejó el último que la
  escribe, con el reloj de este (o 1).
- **Los pases de brillo.** No corren si la malla tiene el byte 3 del script (`N`) o su byte `+0`
  en 1 (`0x4ed07a`..`0x4ed092`): `0x60ff50` lo prende al final (`0x6117fd`..`0x611847`) en cinco
  mallas de ítems, la 1 de `Spear01`, `Sword11`, `Mace07` y `MagicBox07` y la 2 de `Staff07`, que
  así no brillan nunca. Con f = (T % 5000) · 0,00024 − 0,4, c = cos(0,001 · T), s = sin(0,002 · T)
  y la normal en el mundo:

  | Bandera | u | v | Slot |
  | --- | --- | --- | --- |
  | `0x200` | (nz + nx) · 0,8 + 2f | nx + ny + 3f | `0x620` |
  | `0x800` | L·n, con L = (0; −0,1; −0,8) | 1 − L·n | `0x620` |
  | `0x1000` | c·nx + s·ny + nz + 0,5·ny + 3s | 1 − (c·nx + s·ny + nz) − (0,5·nz + 3t) | `0x620` |
  | `0x4000` | c·nx + s·ny + nz + 5s + 3ny | 1 − (c·nx + s·ny + nz) − (2,5·nz + t) | el de la malla |
  | `0x10000` | (nz + nx) · 0,8 + 2f | igual que u | `0x6b2` |
  | `0x8000` | nx | ny | el de la malla |
  | `0x4` | 0,5·nz + t | 0,5·ny + 2t | `0x5c0` |
  | `0x8` | 0,5·nz + 0,2 | (ny + 1) · 0,5 | `0x5fc` |

  El color de cada pase lo pone `0x60b3d0` antes de su `RenderBody`: el `0x44` y el `0x48`, con
  `0x60b130` → `0x608e00(modelo, alpha, 1, color)`, uno por ítem que no mira la luz ((1; 0,5; 0),
  salvo 165 modelos: las Male17, (0,75; 0,65; 0,5)); el `0x240` y el `0x1040`, con `0x60b300` →
  `0x6093d0`, que multiplica el color base (la luz por el factor del nivel) por 1, salvo 29 modelos;
  el de set (`0x840`), `0x609560`, (0,1; 0,6; 1) salvo 15 partes doradas; y el `0x42` del
  excelente, k = (sin(0,002 · T) + 1) · 0,5 → (k; 0,3k; 1 − k), con `0x60b3d0` que vuelve después:
  excelente y set no van juntos, y ni el excelente ni el set van en `0x1a12..0x1a18` ni en
  `0x1c30`. Lo que cuelga no lleva el set (`0x509340` le pasa 0). **Verificado:** las tres tablas
  salen de emular las tres funciones sobre el `main.exe` para cada modelo (`Render/Models/
  PassColors`), y dan el (0,75; 0,65; 0,5) de las Male17 y el (1; 0,5; 0) de `Sword17` de la traza.
  El de set va con el alpha sin(0,001 · T) · 0,5 + 0,4 (`0x60b3d0`), que nunca llega a 0,99: cada
  malla que dibuja el pase multiplica en el lugar el color por él, y la k-ésima va por a^k (Q-C95,
  CL6.3 PR F, *estático*). `0x609560` no mira ese alpha. Y `0x60b130` (los `0x44` y `0x48`) y
  `0x60b300` (`0x240`, `0x1040` y el de set) le pasan a `RenderBody` una malla oculta por ítem: la 1
  de `0xc17` y `0x226` y la 0 de `0x227` en todos; en los de `0x60b130`, la 2 de `0x619` (`Mace08`:
  pone `+0x58` en 2 y después en −1) y el `+0x58` del objeto con `0x231`, `0x81c`, `0xe22` y `0x61a` a
  `0x61f` (en lo que cuelga, 2, 1, 2 y −1, de `0x607b60`); las demás, −1. El `0x42` del excelente,
  −1.
  Los slots `0x5fc`, `0x5c0` y `0x620` los sube `0x61ccb0` en su avance 0 y nadie los suelta:
  `Effect\Shiny01` con filtro y sin repetir, `Effect\Chrome01` con filtro y repitiendo, y
  `Effect\Chrome02` sin filtro ni repetir (en MuRet, `GlowTextureSet`, que sube `GameModels`).
  Una textura forzada pisa el slot. `0x800`, `0x1000`, `0x4000` y `0x40` van aditivos y, con
  alpha < 0,99, multiplican **en el lugar** el color del modelo por el alpha (Q-C95: lo ven las
  mallas que siguen en el mismo `RenderBody`); los demás, `0x80` `0x620b10`, `0x10` `0x620c90`,
  alpha < 0,99 `0x620a00(1)`, si no opaca, y `0x2000` sin profundidad. El bucle de las UV va por
  las normales y, con más de 15.000 en una malla, escribiría una de más (Q-C96). Las filas de `0x4`,
  `0x8` y `0x1000` son las verificadas arriba (y `0x1000` lleva UV, *verificado*: la traza dibuja
  la 141 con `glTexCoord`); las otras cinco, *estáticas*.
- **Cuántos pases: el nivel.** `0x60b3d0` saca el nivel de `(arg >> 3) & 0xf` (con casos por
  modelo) y lo recorta a 2 · `DAT_006c9dcc` + 5 si ese byte de las opciones (4 en la `.data`; lo
  escriben `0x623e70` y `0x644730`) es menor que 4. De los casos por modelo, a un personaje le tocan
  las saetas y las flechas (`0xa19` y `0xa21`: el 0 queda en 0 y L va a 2L + 1, así que de +7 para
  arriba no brillan) y las alas `0x1a12` a `0x1a18` y la capa `0x1c30` (a 0); los demás (`0x1c15`,
  `0x1c1c`, los de `0x1a19` a `0x1a31`, `0x1e1e` en adelante y `0x223a` a `0x224a`) son de cosas que el
  personaje no lleva o no dibuja. Hay también casos por el **tipo del objeto** (`+2`): en las partes
  es el del personaje, pero `0x509340` arma para lo que cuelga un objeto con el modelo de su ítem.
  De lo que alcanza un personaje: `0x81b` (`Spear10`), antes de los pases, `RenderBody(2)` con el
  `0x5c0` forzado, el color (0,5; 0,5; 1,5) y `modelo + 0x88` en 0, con que `0x4ecc30` le corre a la
  malla 0 las UV con la V de su objeto, y después lo de siempre (`0x60b5a1`..`0x60b5e5`; con
  `GL_LEQUAL` el pase base lo tapa, salvo donde mezcla); y la luz del objeto (`+0x68`, en
  `0x60c5c0`) de `Wing05` (`0x1a16`, sin(0,001 · T) + 1,1), `Wing06` (`0x1a17`, (sin(0,001 · T) + 1)
  · 0,25) y `Staff12` (`0xc1d`, la de `0x607b60`). `Wing07` (`0x1a18`) suelta partículas (CL8).
  Cada pase es un `RenderBody` sobre todas las mallas del modelo, uno detrás del otro. *Estático*
  (CL6.3 PR F). Con L la luz del personaje:

  | Nivel | Color base | Pases |
  | --- | --- | --- |
  | 0 a 2 | L | el base |
  | 3 a 6 | L, latiendo con `DAT_07d36ba8` | el base |
  | 7 y 8 | L · 0,8 | el base y `0x44` |
  | 9 y 10 (con el byte > 1) | L · 0,9 | el base, `0x44` y `0x48` |
  | 11 y 12 (> 2) | L · 0,9 | el base, `0x240`, `0x48` y `0x44` |
  | 13 (4) | L · 0,9 | el base, `0x1040`, `0x48` y `0x44` (`0x60ca5a`..`0x60cb0d`) |
  | 14 y 15 | L | el base (Q-C97) |

  Un ítem excelente (`param_6 & 0x3f`) suma un pase `0x42`, y `param_7 & 3` en 1 o 2, uno `0x840`.
  El orden base → 141 → 115 → 120 está **verificado** en la traza de la selección: la textura GL
  384 opaca con el color 0,27, `glBlendFunc(GL_ONE, GL_ONE)`, la 141 con 1,35 y la 115 y la 120
  con (0,75; 0,65; 0,5), y la secuencia se repite (la 141, 25 veces): un nivel 13.
- **El color del personaje.** La luz `+0xf4` no entra por `0x4ebc80`: `0x50ba30` (`0x50cc29`)
  arma `personaje + 0x3b4` = base + `+0xf4`, con la base en (0,4; 0,4; 0,4) en la escena 4 y la
  luz del terreno (`0x5f99e0(x, y)`) en las otras (el modelo `0x189`, en 0,6), y `0x60cf70` se la
  pasa a `0x60b3d0`, que escribe `modelo + 0x48..+0x50` con el factor del nivel. En la selección,
  0,4 y 1,4 (el elegido) antes de ese factor, salvo el "héroe": `DAT_0743a170`, el objeto 0 desde
  el arranque (el del lugar 0), sigue hasta `0x50d219`, que en la escena 4 le da 0,5 más `+0xf4`,
  también a lo que cuelga. **Verificado** en la traza: su personaje está en el lugar 0 y elegido,
  y `Helper02` sale con 1,5 y `Sword17` con 1,35 = 1,5 · 0,9. El pase base pasa además por
  `0x609640` (2,5 KB de casos por ítem), sin leer; el 0,27 de la traza sería 1,35 por el piso 0,2 de
  la luz por vértice de `0x4ebc80` (`0x6b8854`; *inferido*, P-C14). Los demás modelos toman el color de `0x5ffce0`: la luz del terreno más `objeto +
  0xf4`, o por 0,1 con `objeto + 0xe8` en 0.
- **Lo que cuelga del personaje.** `0x511290` pone los huesos de cada modelo; para `0x20a`, la
  mano derecha en 33 (`+0x2bd`) y la izquierda en 42 (`+0x2dd`). `0x50ba30`: las alas en 47
  (`+0x2fd`, a 1,0 keys por frame en las acciones `0x1e` y `0x1f` y a 0,25 en las demás, con el
  desplazamiento (0, 0, 15)) y la mascota en 34 (`+0x31d`, a 0,5, con (20, 0, 0)), que solo se
  dibuja si es `0x1c13` (Q-C98). Las cuelga `0x509340`: con la bandera en 0, `0x4ec190`
  (`VectorRotate` del desplazamiento por el hueso, por la escala, y el hueso al global
  `0x68c4a1c`); con la bandera, un `AngleMatrix` por modelo encadenado. Las avanza con `0x4ec200`
  (la velocidad de la parte; el arma, la de la acción 1 de `Player.bmd`) y las posa con
  `0x4eb860(…, 1, 1)`, cuya raíz es la de `0x68c4a1c`. Con la bandera (el byte bajo del noveno
  argumento, que `0x50ba30` prende con un modelo de `0x212` a `0x1011`), la raíz es el hueso por un
  `AngleMatrix` y una traslación de cada modelo (`0x509481`..`0x5095e2`): (0, 20, 180) y (−10, 8, 40)
  los de `0xa1a` a `0xa20`, `0xa22`, `0xa24` y `0xa25`; (110, 180, 90) y (−10, 5, −10) `0xc1b`;
  (−60, 0, −80) y (−5, 20, 0) `0xa26`; (70, 0, 90) con (−10, 5, 10) los demás de `0xa12` a `0xc11` y
  `0xc1a`, y con (−20, 5, 40) el resto. `0x50ba30` usa ese camino para la espalda, del hueso de las
  alas (`+0x2fd` en 47) y con su lugar: con las armas en la mano (`local_20` en 0, como en la
  selección), las saetas (`0xa19`) de la mano izquierda o, si no, las flechas (`0xa21`) de la
  derecha; con `local_20` en 1 (`+0x3e2`, las acciones `0x99` a `0xb8`, o las `0x15` y `0x1d` en los
  mapas 7, `0x18` a `0x1d` y `0x24`; nunca en los 11 a 23), el arco de la izquierda, o la ballesta o el
  bastón de la derecha (`0x50dd44`). *Estático*; el carcaj lo dibuja MuRet desde la CL6.3 PR F, y lo de
  `local_20` en 1 va con la zona segura (CL7). Con la acción del personaje (`+0x376`) de `0x84` a
  `0x86`, el camino con la bandera usa otro lugar por modelo (`0x5093d2`), sin leer (P-C20). El lazo
  de las partes va de la 5 a la 0 (`0x50d3dc`: las botas, los guantes, el pantalón, la armadura, el
  casco y la cabeza), y el de las manos (`0x50e6af`), con `local_20` en 0, después de la espalda
  (`0x50de55`), las alas (`0x50df80`) y la mascota (`0x50dfcf`): lo aditivo no escribe la
  profundidad, así que el orden se ve. **Verificado** en la traza de la selección: sus bloques van
  en ese orden, y cada modelo pase por pase (`GoldenCharacterDrawing`, CL6.3 PR F). Las partes del
  cuerpo (`0x60cf70`) no se posan: van con las matrices de la pose del cuerpo.
- **El objeto de lo que cuelga.** `0x509340` le arma a cada cosa que cuelga, en cada frame, un objeto
  con el tipo de su ítem (`0x5093b4`), lo inicializa con `0x607b60` y se lo pasa a `0x60b3d0`. Lo de
  siempre es el del jugador (−1, −1, 1, 0, 0), y por tipo, con P1 = sin(0,004 · T) · 0,3 + 0,7, P2 =
  sin(0,004 · T) · 0,2 + 0,8 y P3 = sin(0,004 · T) · 0,2 + 0,9 (los floats de `0x6b89e8`, `0x6b89dc`,
  `0x6b8a5c`, `0x6b8854`, `0x6b8a38` y `0x6b8a1c`):

  | Tipo | `blendMesh` | Luz | U, V | `+0x58` |
  | --- | --- | --- | --- | --- |
  | `Bow07`, `CrossBow06` y `07`, `Staff07` (`0xa18`, `0xa1f`, `0xa20`, `0xc18`) | −2 (todas) | P1 | | |
  | `Bow17`, `Staff09` (`0xa22`, `0xc1a`) | −2 | P3 | | |
  | `Sword06`, `Sword14`, `Staff10`, `Helper01` (`0x217`, `0x21f`, `0xc1b`, `0x1c12`) | 1 | | | |
  | `Sword11`, `Mace07`, `Spear01`, `Shield12` a `14` (`0x21c`, `0x618`, `0x812`, `0xe1d`..`0xe1f`) | 1 | P1 | | |
  | `Sword15` (`0x220`) | 1 | P1 | U = (rand() % 10) · 0,1 | |
  | `Mace05` (`0x616`) | 1 | P2 | | |
  | `Staff08` (`0xc19`) | 1 | (rand() % 11) · 0,1 | | |
  | `Shield15` (`0xe20`) | 1 | | U y V de rand() % 10 · 0,1 | |
  | `Staff01` (`0xc12`) | 2 | | | |
  | `Staff06` (`0xc17`) | 2 | | U y V de rand() % 10 · 0,1 | |
  | `Staff12` (`0xc1d`) | 2 | P1 | | |
  | `Mace06`, `Wing01`, `Wing04` y las gemas (`0x617`, `0x1a12`, `0x1a15`, `0x1a1a`..`0x1a1d`, `0x1a26`) | 0 | | | |
  | `Spear10` (`0x81b`) | | | V = (T % 2000) · −0,0005 | |
  | `Sword32`, `Mace08`, `Shield17` (`0x231`, `0x619`, `0xe22`) | | | | 2 |
  | `Spear11` (`0x81c`) | | | | 1 |

  Así lo que cuelga no ve la luz del personaje (ni la de la clase 4): una malla `R` en la mano va
  por la de su objeto. *Verificado* emulando `0x607b60` sobre el `main.exe` con T = 1000 y 2500
  (`Render/Models/ObjectState`); en la traza, `Sword17` y `Helper02` tienen el de siempre. El
  `rand()` es el de todo el cliente: MuRet usa, en cada frame, un `std::minstd_rand` con el reloj de
  semilla.
  **Verificado** en la traza de la selección (CL6.3,
  `GoldenSkinningTrace.TheSwordAndThePetHangFromTheirBones`): `Sword17` cuelga del hueso 33 sin
  desplazamiento, y `Helper02` del 34 con (20, 0, 0) girado por
  el hueso y **sin** la escala del personaje (con ella, a 1,8), cada uno posado desde esa raíz y
  llevado al mundo con la escala y la posición del personaje en cada vértice, como las partes: cada
  vértice a 0,0014 del de la traza en los cinco swaps (desde el 42, la espada cae a 170). La
  mascota está en el frame 1,5 · 2 · 2,5 · 3 · 3,5 (el de la pose, como la tabla de arriba).
- **La sombra.** Antes de las partes, `0x50ba30` dibuja `Shadow01` (`0x20b`) con las matrices
  del cuerpo y `+0x98` en 1 (con el alpha en 0,5 o más, fuera de los mundos 10, `0x18..0x1d` y
  `0x24`, y sin las mascotas que se montan, `0x1c14` y `0x1c15`). `0x60b3d0` pone la opaca y el
  negro, y `0x4eff40` apaga la textura y la máscara y aplasta cada vértice, con x' y z' desde la
  posición del objeto: X = (x' + 2000) · z' / (z' − 4000) + x' + px, Z = pz + 5 (2500 en el mundo
  30). Saltea la malla de `+0x58` y el `blendMesh`, pero no mira la textura: la de `Shadow01` es la
  oculta (`hide_m`). **Verificado** en la traza: el bloque de 318 vértices en z = 168, antes de
  las botas, uno a uno con el de MuRet (`GoldenCharacterDrawing`, CL6.3).
- **El contorno bajo el mouse, en un jugador.** `0x510d00` le pasa a `0x50ba30` un 1 con el
  personaje de `DAT_006c8e30` (el que dejó bajo el mouse el picking del frame anterior: `0x59ec80`
  corre después del dibujo) o de `DAT_006c8e2c`, y `0x50ba30` lo pasa a `0x60cf70` como su
  duodécimo argumento en las seis partes del lazo (de `+0x1f8`, la cabeza, a `+0x298`); lo que
  cuelga (`0x509340`) no lo lleva. Con el 1, y si el alpha del objeto pasa de 0,01, `0x60cf70`
  dibuja antes de la parte dos pases de `0x60cd60`: cada uno pone la luz por vértice en 0 y
  `DAT_006c881c` en su escala, vuelve a pasar los vértices con `0x4ebc80` (s · R·v + t, la escala
  después del giro y antes de la traslación del hueso) y dibuja todas las mallas con `0x40` (en
  `0x4ecc30`: aditivo, sin textura, sin escribir la profundidad y sin alpha test; una malla de
  cuatro componentes no va). Fuera de los mapas 18 a 23, el primero en (0,1; 0,03; 0) a la escala
  de `objeto + 0x94` (1,2, que pone `0x511290`) y el segundo en (0,7; 0,2; 0) a ella menos 0,02;
  en los mapas 18 a 23, (0,1; 0,01; 0) a 1 + 0,1 / escala y (0,7; 0,07; 0) a 1,02 + 0,04 / escala
  menos 0,02; con `objeto + 0x88` en 4, el verde (0,02; 0,1; 0) y (0,16; 0,7; 0). Son otros
  valores que los de `0x5ffdf0` (arriba), que son de los modelos sin partes (Q-C94).
  **Verificado** en la traza de la selección, cuyo personaje estaba bajo el mouse: dos pases sin
  textura y `GL_ONE, GL_ONE` por parte, en (0,1; 0,03; 0) y (0,7; 0,2; 0), a 1,2 y 1,18 del hueso,
  sin la malla de cuatro componentes ni lo que cuelga.
- **La clase 4 en el lazo de las partes.** Con `(personaje + 0x1d4) & 7` en 4, en la parte del
  casco (`+0x218`), `0x50ba30` pone `objeto + 0x68`, la luz del `blendMesh` que `RenderBody` le
  pasa a `0x4ecc30`, en sin(0,001 · T) · 0,1 + 0,7, y dibuja los cascos `0x1012`, `0x1017`,
  `0x1018`, `0x101a` y `0x101b` con el modelo + `0x328b`, su máscara (`MaskHelmMale01`, `06`, `07`,
  `09` y `10`), con la piel del casco. La ven la cabeza y, en el frame siguiente, las partes de antes
  del casco, salvo donde `0x60b3d0` escribe la suya ("El script en un personaje"); lo que cuelga no,
  porque lleva su objeto. MuRet lo hace desde la CL6.3 PR F. *Estático*.
- **Las texturas de un modelo.** `0x60d530(n)` pone el principio de la búsqueda
  (`DAT_07d366b4`) en 0 y el próximo libre (`DAT_07d366b8`) en n; nadie más escribe el principio.
  `0x60d5f0(modelo, carpeta, wrap, filtro, chequear)` busca cada nombre de textura del modelo con
  `0x60d550` en los slots de 0 al próximo de la tabla de `0x7d3a198` (`0x38` bytes cada uno: el
  nombre en `+0`, el ancho y el alto en `+0x20` y `+0x24`, los componentes en `+0x28`, el nombre
  de GL en `+0x2c`, la cuenta de usos en `+0x30`). Compara con `strcmp` el **nombre desnudo**,
  sin la carpeta y distinguiendo mayúsculas (Q-C90). Si está, la malla usa ese slot y suma un uso;
  si no, carga `carpeta + nombre` en el próximo (la letra después del primer punto, con
  `tolower`, o sin punto el byte que sigue al 0 del nombre, Q-C107: `t` es OZT con `0x63bd00`, siempre con `GL_NEAREST`; otra, OZJ con `0x63b870` y el
  filtro), guarda el nombre y corre el próximo, aunque el archivo falte. **Después** los nombres
  que empiezan con `ski` o `level` (en minúscula, byte a byte) pasan al slot `0x12e`, `hid` al
  `0x12d` y `hair` al `0x137`: el que se cargó queda ocupado. El quinto argumento de los cargadores
  es el modelo, por su nombre de cabecera en el error (`"%s of %s - File not exist."`), y
  `chequear` vale 1 en todas las llamadas del binario: una textura de modelo que falta cierra el
  juego (Q-C23). `0x63c150` suelta un slot (con la cuenta en 0 borra el nombre). Quién fija el
  próximo:

  | Cargador | `0x60d530` | Qué carga desde ahí |
  | --- | --- | --- |
  | `0x60f7d0` (avance 2 de la carga) | `0x12e`, `0x137` y `0x138` | la piel y el pelo de cada clase, en slots fijos (abajo); las partes del personaje desde `0x138` |
  | `0x611850` (avance 4) | `0x25a` | los ítems (los modelos son de `0x60ff50`, el avance 3) |
  | `0x615700` (avance 5) | `0x69`, `0x138` y `0x3b8` | antes del primero, siete habilidades y efectos (`0x14a` a `0x14d`, `0x109`, `0xf9` y `0x10f`) donde dejó `0x611850`, desde `0x371`; `StoneCoffin` desde `0x69`; `LaceArrow` desde `0x138`, encima de dos de la piel (Q-C89); las habilidades desde `0x3b8` |
  | `0x616ec0` (cada mundo) | `0x69` y `0x41c` | los modelos especiales del mapa; los `ObjectNN` del mundo |
  | `0x61ca30` (la selección) | `0x6f8` | `Logo05` y los `NewFace` (Q-C91) |
  | `0x615500` | `0x548` | nada: lo llama `0x61eeb0`, detrás de `DAT_07d366bc` |
  | `0x612270`, `0x612d90` (monstruos y NPC a pedido) y `0x61c7c0` (el login) | ninguno | donde dejó el próximo el último (Q-C92) |

  **Verificado:** un script que repite `0x60d530`, `0x60d550` y `0x60d5f0` con los modelos del
  laboratorio da, para los 750 slots de `0x12e` a `0x41b`, los mismos nombres que el volcado
  `lor_slots.bin` de la tabla (sacado en Lorencia después del login y la selección), uno por uno.
  En esa carga, 29 nombres se encuentran en un slot que cargó otra carpeta y su modelo **no
  tiene** ese archivo en la suya (4 de `0x611850` y 25 de `0x615700`: anda porque otro lo cargó
  antes), y uno con otro archivo: `Helper04` (`0x1c15`, `Item\`) dibuja el de `Skill\` (`0x285`).
  La misma simulación sigue con el mundo del login (*inferido*: ningún volcado es de ahí): 17
  nombres más en otra carpeta con los mismos bytes y 2 sin el archivo en la suya.
  **Qué modelos y con qué carpeta** no sale de las tablas de los modelos: `0x60f7d0` hace 214
  llamadas a `0x60d5f0`, todas con `Player\`, y `0x611850` 291, con `Item\` salvo `0x1c16`,
  `0x1c17` y `0x1c15` (`Skill\`), `0x224c` (`Monster\`), `0x1c2f` (`Npc\`) y `0xdb`
  (`Item\Bow\`, que no carga ninguna tabla). Los `Helper01` a `03` (`0x1c12` a `0x1c14`) están en
  `Player\` y sus texturas salen de `Item\`; 26 modelos se piden dos veces, y la segunda encuentra
  los nombres de la primera. Todas con `0x2901` y `0x2600`: repitiendo y **sin filtro** (el logo del
  login, `0x61c7c0`, pide `0x2601`). Fuera de los lazos, `0x60f7d0` sube a mano, sin nombre en la
  tabla, las cuatro capas: `Robe01` y `Robe02` en `0x250` y `0x251`, `Robe03.tga` en `0x252` y
  `DarklordRobe.tga` en `0x257`. Con la carpeta de cada `.bmd`, 14 de esas texturas no están en el
  laboratorio (entre ellas las de las mascotas): el original, que se cierra con una que falta, no
  las pide así. **Verificado:** una emulación de los dos cargadores sobre el `main.exe` da las
  mismas 505 llamadas que `Data::Models::PlayerTextureCalls` e `ItemTextureCalls`, en el mismo
  orden y con la misma carpeta.
- **La piel y el pelo.** `0x60f7d0` sube a mano, con `GL_NEAREST` y `GL_CLAMP`: `0x12e`
  `skin_barbarian_01`, `0x12f` `level_man022`, `0x130` `skin_wizard_01`, `0x131` `level_man01`,
  `0x132` `skin_archer_01`, `0x133` `level_man033`, `0x134` `skin_special_01`, `0x136`
  `level_man02` y `0x137` `hair_r` (de `Player\`); `0x12d` y `0x135` quedan vacíos (**verificado**
  en `lor_slots.bin`: los otros nueve tienen textura). Antes de cada parte, `0x50d41c`..`0x50d431`
  pone `modelo + 0x98` = 2 · (b & 7) + (b >> 3), con b la clase del personaje (`+0x1d4`): la clase
  c usa `0x12e + 2c` y su segunda clase el `level_man` de al lado; la clase 4 (`Class05`) va a
  `0x136`, y su `HelmClass05`, la única parte con pelo, al de `0x137`. Ningún nombre del
  laboratorio empieza con `Hair` o `Skin` en mayúscula. Qué número es cada clase, *inferido*.

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

**El DIB y la caja** (CL5.3, *estático*): `0x61bcd0` arma el DIB con el slot 0 (la `FontInput.tga`,
de 256 × 32): el ancho por `DAT_006c7a84` (5) y el mismo alto, 1.280 × 32, de 24 bits y de arriba
hacia abajo (el alto negativo). `0x465a00(columna, ancho, filas)` pasa de ahí a la caja de 256 × 32
RGBA del slot 0: un píxel cuyo primer byte es `0xff` va con el color del texto (`DAT_006c8e5c`) y
cualquier otro, con el del fondo (`DAT_006c8e64`). Lee desde la columna que le pasan sin mirar el
ancho del DIB: con un texto de más de 1.280 columnas, la sexta caja sale de la fila de abajo
(Q-C78). Y `0x4667a0`, el de los `0x02`, reescribe el texto en el buffer del que llama después de
ponerle 256 ceros (Q-C77). MuRet rasteriza cada caja desde su columna con FreeType en un bit
(`Render/Text/`, Q-C76), y una caché (`TextCache`) guarda las cajas de cada texto, fuente y colores
en los slots del texto, después de los 1.845 del original (`TextureSlot.h`): no se sube nada en cada
frame.

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
`656 · actual / 10` (división entera sin signo) y `0x647370(ancho, 0, 1)` le pone ese ancho: la u
de la derecha llega a ese ancho sobre el de la textura, sin medio téxel, y la de la izquierda queda
a medio téxel; con `GL_REPEAT`, la textura se repite a lo largo. La v de abajo también llega a
15 / th: `0x6402d0` arma la imagen con 4 de ancho (`0x647050(4, 15, 0x723, …)`, desde
`0x65f0f9`) y, con el rectángulo de 656 × 15 que le pasa `0x65eeb0`, llama a `0x647370(0, 15, 2)`
(*estático*).

Cada avance es un frame entero: `0x61ccb0` llama 11 veces (de 0 a 10) a `0x65f200(hdc, actual,
10)`, que hace `0x620db0(0, 0, 640, 480)`, `glClear` de color y profundidad, el 2D (`0x6219b0`),
las cinco imágenes, la barra, `0x621a90`, `0x620fc0`, `glFlush` y `SwapBuffers`. Entre uno y otro
corre la carga, sin mensajes: la ventana no responde mientras tanto (*inferido*). Qué carga en
cada avance (CL6.R, *estático*): el 0, las texturas fijas de la interfaz; el 1,
`0x60dbe0` (el personaje); el 2, `0x60f7d0` (sus texturas); el 3, `0x60ff50` (los modelos de los
ítems, 243 cargas, con el número `0x212` + grupo · 512 + índice); el 4, `0x611850` (sus
texturas); el 5, `0x615700` (habilidades, efectos y algunos monstruos); el 6, `0x61ae60` (más
texturas de la interfaz); el 7, `0x61b350` (los sonidos); y el 8, las tablas. Cada cargador de
texturas fija su primer slot ([El modelo en el binario](#el-modelo-en-el-binario)).

### El login, al entrar

`0x634c60`, la lógica de la escena 2, llama a `0x6349a0` mientras la bandera `DAT_07d3a13d` esté
en 0. Arranca en 0 (está en la `.bss`), y la vuelta al login de `0x4c2710` la pone otra vez en 0
(`0x4c2f24`): **cada vez que se entra al login se carga todo de nuevo**. En orden:

1. el mapa pasa a `0x37`, el 55 (`DAT_006c9dd0`), y `0x61a560` carga ese mundo: `World56` (la
   carpeta de cada mapa, en [05](05-formatos-de-datos.md#terreno-de-un-mundo-dataworldn));
2. `0x61c7c0` sube las texturas de la escena: `Logo\Login_Back01.jpg` y `Login_Back02.jpg` (slots
   `0x15` y `0x16`), `Object9\sand01.jpg` y `sand02.jpg` (`0x5c2` y `0x5c3`), `Effect\FireSnuff.jpg`
   (`0x635`), 13 de `Interface\` en `0xc` a `0x1a` (sin `0x15` ni `0x16`) y el modelo `0xa2`
   (`0x60da30` con `Data\Logo\` y `"%s0%d.bmd"`: `Logo03.bmd`, con sus texturas de `Logo\` por
   `0x60d5f0`, sin un `0x60d530` antes: van al próximo slot que dejó el mundo 55, que termina su
   carga con `0x60d530(0x41c)` (`0x61a0e8`) y 57 texturas nuevas de `Object56\`, así que a `0x455`
   y `0x456`. De los 65 nombres de `Object56\`, tres son del dragón, que la carga del mapa ya puso en
   `0x69..0x6b`, y cinco están en la tabla de las habilidades de `0x615700` (`castlegate02`,
   `cswalltop` y `castlegatehide` en `0x3e5..0x3e7`, `fire02` y `fire01` en `0x3ef` y `0x3f0`, en
   `lor_slots.bin`): con esos nombres, la regla da `0x455` (`GoldenWorldModels`). Que el logo quede ahí
   en una corrida del original sigue *inferido* (P-C15, Q-C92). El tercer y cuarto argumento van a `glTexParameteri` en `0x63b870` y `0x63bd00`: el
   tercero a `GL_TEXTURE_MAG_FILTER` y `GL_TEXTURE_MIN_FILTER` (`0x2800`, `0x2801`), el cuarto a
   `GL_TEXTURE_WRAP_S` y `_T` (`0x2802`, `0x2803`). Los fondos y casi todas las de `Interface\` van
   con `GL_NEAREST`; la arena, `FireSnuff` y `cr_mu_lo` con `GL_LINEAR`, y repiten la arena,
   `server_ex03` y `server_ex02`;
3. `0x65f300` arma las ventanas de la UI nueva (abajo);
4. `0x4c1340` conecta al ConnectServer ([03](03-red-y-cifrado.md)). El `connect` bloquea, y del 1 al
   4 corre todo en el primer paso de la escena: hasta que termina, en la pantalla queda la carga con
   la barra llena, y si el connect falla, el primer frame del login ya trae el aviso `0x70` (Q-C85);
5. el objeto `0xa2` en (10.680, 15.550, 1.710), con ángulos (10, 0, 36) y escala 1, que
   `0x606660` pisa (abajo), y el personaje de la escena (`0x517270(2, 4, …)`, con el cuervo, que crea
   `0x420d60` por el `0x1c17` de la mano izquierda, [El cuervo](#el-cuervo); el `A9` solo pide su
   nivel, P-C11) en (10.603, 14.112, 580), girado 188° en z y con escala 1,6; el caballo oscuro en
   que monta, el `0xbf` (`0x443fa0`, abajo; antes este documento lo daba como un efecto); 120 llamadas a `0x555190` con dos
   cadenas de la `.bss`, que llenan el chat de renglones vacíos (CL7.R, *estático*); la música (`0x4acfd0`) y una
   línea en el log.

El logo (CL6.R, *estático*): `0x606660` arma un objeto del mundo de `0x1d4` bytes en la grilla de
16 × 16 sectores (la posición por 0,000625; fuera de 0 a 15 no crea nada), vivo, con el modelo,
la escala, alpha 1, luz por vértice (`+0xe8` en 1), la luz en 0, sin malla oculta (`+0x58` en
-1), sin `blendMesh` (`+0x64` en -1) y su luz en 1 (`+0x68`), y la caja de (−40, −40, 0) a
(40, 40, 80): son los argumentos de `RenderBody` (`0x60ce68`). En las escenas 2 y 4 tiene casos por
modelo, y el `0xa2` (`0x6068d5`) **pisa la escala con 2,2**, pone el `blendMesh` en 1 (la malla de
la textura 1 va aditiva) y apaga la luz por vértice (`+0xe8` en 0). (Antes este documento daba la
escala 1.) Su luz la sube `0x604ab0` (abajo, en el dibujo).

El personaje (`0x517270(2, 4, 0, 0, 0, 0)`, *estático*): `0x517270` le pone la luz `+0xf4..+0xfc` en
0,3 (`0x3e99999a`), y `0x4f2250`, con el caballo oscuro de mascota (`+0x318` en `0x1c16`, y `+0x3e2` en
0), la acción `0x4a`, la de montar. Lo demás que el original hace con él en la escena 2 (de la
revisión de la CL6.4, *estático*):
- **El caballo** es el modelo `0xbf`, `Skill\DarkHorse.bmd` (lo carga `0x615700`). `0x443fa0(0xbf,
  posición, personaje)` lo crea como un objeto ayudante en `0x7d335d0`; su lógica (`0x445230` →
  `0x444010`) sigue la posición, los ángulos y la cabeza del jinete, y elige su acción por la de él:
  la 5 (51 keys) con la `0x54`, la 6 (31 keys) con la `0x55`, las dos a 1, y la 0 a 0,3 con las
  demás. Lo dibuja `0x4453b0` → `0x445260`, con la escala 1,6 en la escena 2, entre los personajes y
  los objetos del mundo. `0x50ba30` no dibuja la mascota `0x1c16` (Q-C98): el caballo va por este
  camino. Todo, abajo, en [El caballo oscuro](#el-caballo-oscuro).
- **La vuelta a la de parado:** cuando la `0x54` o la `0x55` terminan (`0x4ec200` da la vuelta y
  `0x4f9580` devuelve 0), `0x4fcd00` (`0x4fce29`) pone `+0x3c4` en −1 y `0x4fcb10` le vuelve a dar
  la de parado (`0x4f2250`: la `0x4a`). El gesto se ve una vez, 2,04 s o 1,24 s.
- **La tela de la clase 4:** con la clase 3 o 4, el modelo `0x20a`, `+0x3e3` en 0 y fuera de los
  mapas `0x12` a `0x17`, `0x50ba30` arma tres telas (`0x50d5b9`): dos con la textura `0x256`
  (`Monster\iui06.jpg`) en el hueso 20 y una con la `0x257` (`DarklordRobe`) en el hueso 19, la
  tercera solo con `+0x2f8` en `0x1c30`; las simula con 5 pasos por frame (`0x455190`) y las dibuja
  (abajo, [La tela de la clase 4](#la-tela-de-la-clase-4)). La capa misma, `0x1c30` (`Item\DarkLordRobe.bmd`, el plano del inventario: un hueso y 15 vértices,
  de 168 de alto), no cuelga del hueso 47: `0x509340` sale sin dibujar con ese modelo, desde
  cualquier lugar (`0x509363`: `CMP EDI,0x1c30` y `JZ` al final; *estático*). Lo que se ve es la tela.
  MuRet lo hace así desde la CL6.6 A (`CharacterDrawingTest.TheLordsCapeDoesNotHangAsAModel`).
- **La cabeza:** `0x505de0`, en cada paso, con un personaje que no es el propio (el 0) y que se ve:
  con `rand() % 32` en 0 elige un objetivo nuevo, de −64 a 63 de giro y de −16 a 15 de inclinación,
  y se acerca un 20 % por paso (`0x6b8854`).

En la escena 2 lleva la ropa fija, el
casco y lo demás de `0x102e`, `0x122e`, `0x142e`, `0x162e` y `0x182e` (el juego 29), la maza
`0x620` en la derecha, el cuervo `0x1c17` en la izquierda, la capa `0x1c30` y el caballo oscuro
`0x1c16`; `0x512930` le deja la cabeza en -1. `0x420d60` le cuelga al cuervo un objeto aparte, de
`0x460` bytes (`0x4167b0`, en `personaje + 0x430`), con su propio paso: el pájaro que se ve, el `0xc0`, leído en la CL7.R
([El cuervo](#el-cuervo)). La acción la elige `0x634c60` en cada paso: con `ftol(T) % 2300` entre 0 y 99,
`rand() % 6` en 1 pone la `0x54` y en 2 la `0x55` (`0x4ea030`). La condición es `ftol(resto ·
0,01)` en 0, con el producto en la x87: 100 · 0,01f da 0,99999998, así que la ventana va de 0 a 100
ms (Q-C110). Las dos acciones van a 1 por paso (`0x60f679` y `0x60f68b`).

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
   una ventana lo achica), `0x5fbb00(w · (1/640), posición)` (el frustum, *verificado* en § Terreno) y
   `0x620fc0`, que hace `glPopMatrix` de la modelview y de la proyección. En la selección, que
   tiene la misma forma, la traza muestra esa perspectiva sin nada adentro (*verificado*, arriba);
4. `glClearColor(0, 0, 0, 1)` y el pase 3D en `0x620db0(0, 25, 640, 430)`: si la ventana de
   `+0x4578` está cerrada (su bandera de abierta, `+0xc`, es el byte `+0x4584` del administrador),
   el terreno (`0x5fc260(0)`), los personajes (`0x510d00`), `0x4453b0`, los objetos del mundo
   (`0x6046a0`, abajo: el logo), los joints (`0x53c6a0(0)`), los efectos con modelo
   (`0x52d620(0)`, el dragón), `0x549510` y las hojas (`0x5306a0`, las brasas); después `0x6213e0`,
   los sprites (`0x549440(0)`), las partículas (`0x548550(0)`) y `glPopMatrix` (CL6.4: antes este
   documento daba los objetos en `0x53c6a0`; los nombres, de la CL7.R, en
   [Los efectos del login](#los-efectos-del-login));
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
   que sale en (0,8; 0,7; 0,6), con la mezcla aditiva de la arena: ni `0x554180`, ni `0x467060`,
   ni `0x554000` la cambian, así que el fondo con alfa `0x80` no oscurece nada (Q-C80). Van
   apoyados abajo: y = `479 − cy · 640 / W` (con W, no con H:
   Q-C60). El 454 termina en x = 335 (`335 − cx · 640 / W`), el 455 empieza ahí y el 456, armado con
   la versión (`DAT_057630ac`, 02), va en x = 0. Las divisiones son enteras y sin signo, y los
   textos se copian a un buffer de 100 bytes de la pila (Q-C61);
7. el fundido: con `DAT_07d3a0cc` en 1 y la cuenta `DAT_006d5e30` debajo de 16, `glColor3f(k;
   0,9 k; 0,8 k)` con k = (16 − cuenta) / 16 (el 1/16 de `0x6b8850`); con 16, la bandera pasa a 2.
   No dibuja nada con ese color (Q-C66). La cuenta la lleva `0x633920`, por paso: 40 pasos con
   la bandera en 0 (el logo se aclara de a poco: `0x604ab0`, en las escenas 2 y 4, le pone al
   `0xa2` la luz cuenta · 0,02 en `+0xf4..+0xfc` y en `+0x68`, la de su malla aditiva, de 0 a 0,78,
   y 1,5 con la bandera en 1: salta, Q-C105; y corre la V de los `0xa0` y `0xa1` con
   −(T % 4000) · 0,00025), después la pone en 1, y desde ahí
   cada 128 pasos la cámara elige otro recorrido (en el login, `rand() % 4 + 1` y `rand() % 2`;
   en la selección, siempre el 5), con FOV 45;
8. lo común a las escenas: `0x5f7bc0`, `0x554810` (los avisos centrados, con la fuente negrita
   `DAT_05763558`), `0x5554e0` (si `DAT_006c7a88` es 1 o la escena no es la 5), `0x5b0550`,
   `0x40eca0`, `0x65fe00` (la UI nueva: si hay modo, `0x63cf80` y la lista de `+0x8de0` por
   `vtable + 0x18`), `0x5ac2d0` y `0x633110` (modelos 3D dentro de la UI); al final, `0x621a90` y
   `0x620fc0`.

### El logo en el binario

Leído en la CL6.4 (*estático*; el dibujo del `0xa2`, emulado sobre el `main.exe` con el objeto de
`0x606660` en el mapa 55 y la escena 2).

- **El dibujo.** Los objetos del mundo (los de `0x606660`, en las 256 listas de `0x7d325d0`) los
  dibuja `0x6046a0`, después de los personajes: para cada sector de 16 × 16 celdas que pasa el
  frustum (`0x5fba90`, en el centro del sector, con un margen de −180) y cada objeto vivo que lo
  pasa (en su posición por 0,01, con `+0xdc` de margen, −30), prende `+0x16c` y llama a `0x602e90`
  (`0x5ffdf0` y, si devuelve 1, `0x6002e0`) y a `0x602ed0` (los efectos de cada mapa, que tira un
  `rand()` por objeto y en el 55 solo hace algo con los tipos `0x54` y `0x59`). `0x5ffdf0`
  no dibuja con el alpha `+0x174` debajo de 0,01; si no, le copia al modelo la luz por vértice
  (`+0xe8`), la escala (`+0xc`), la posición (`+0x10`) y la acción (`+0x111`), y lo posa con
  `0x4eb860` (los ángulos de `+0x1c`, los de la cabeza de `+0x28`, la raíz con la escala y la
  posición) y `0x4ebc80`. `0x6002e0` tiene un caso por tipo y por mapa; para el `0xa2` en el mapa
  55 (emulado) hace **un solo** `RenderBody(2, +0x174, +0x64, +0x68, +0x6c, +0x70, +0x58, −1)`: el
  pase base con el alpha, el blendMesh, la luz, la U, la V y la malla oculta del objeto, sin pases
  de brillo (`+0x78 & 2` en 0 y `+0x74` distinto de `0x80`: ningún pase más).
- **El color** (`0x5ffce0`, en `0x5ffdf0`): con luz por vértice, la del terreno en su lugar más
  `+0xf4..+0xfc`; sin ella, como el `0xa2`, la del terreno por 0,1 (`0x6b87b8`) más
  `+0xf4..+0xfc`. Los tipos `0x189` (0,6) y `0xe4` (un seno del reloj) tienen el suyo. Sin luz por
  vértice, `RenderBody` hace un solo `glColor3fv(+0x48)` al empezar, y una malla normal del modo 2
  no lo vuelve a poner: la rama del blendMesh de `0x4ecc30` hace `glColor3f(luz · color)`, y **las
  mallas que vienen después la heredan** (las de después de un pase de script 1 o 2, el blanco que
  pone `0x4ef0c0`). En `Logo03` el blendMesh es la última malla y no pasa; en otro modelo, sí
  (P-C20 i).
- **La lógica** (`0x606560`, en cada paso del login desde `0x634c60`, solo para los objetos que se
  vieron en el último dibujo): `0x604ab0` avanza la animación con `0x4ec200` a la velocidad del
  objeto (`+0xd8`, 0,16 desde `0x606660`), no a la de la acción, y en las escenas 2 y 4 le escribe al
  `0xa2` la luz de `+0xf4..+0xfc` y de `+0x68`: la cuenta de las rutas · 0,02 con la fase en 0, y
  1,5 con otra (Q-C105). `0x606560` solo corre los objetos con `+0x16c`, que `0x606660` deja en 0 y
  prende el dibujo: el primer paso, el que crea el logo (`0x6349a0` desde `0x634c60`), no lo toca, y
  el primer frame sale con la luz de `0x606660`, 1 en `+0x68` y 0 en `+0xf4`. `0x606560` corre
  antes que `0x633920`, que sube la cuenta, y la cuenta arranca en −1 (`0x6349a0` y la `.data`): el
  paso 2 da 0, el n da (n − 2) · 0,02, el 41 da 0,78 y el 42, 1,5 (verificado con una emulación de
  `0x604ab0` en la revisión de la CL6.4).
- **`Logo03.bmd`**: dos mallas, `logo.jpg` (555 vértices) y `logo2.jpg` (4 vértices y 2 triángulos:
  la de la textura 1, la del blendMesh, aditiva), dos huesos y una acción de una key: el avance no
  hace nada y el logo no se anima.

### El caballo oscuro

Leído en la CL6.4 PR B (*estático*; el dibujo del `0xbf`, emulado sobre el `main.exe` con el objeto
de `0x443d70` en el mapa 55 y la escena 2; los slots, **verificados** en `lor_slots.bin`).

- **Quién lo crea.** `0x443fa0(tipo, posición, jinete, …)`, fuera de los mapas `0x12` a `0x17` y
  con el jinete del tipo `0x20a` (o el tipo `0x1c12`), llama a `0x443d70` con cada uno de los 10
  ayudantes de `0x7d335d0` (de `0x1d4` bytes, en la .bss) hasta que uno devuelva 0: `0x443d70`
  devuelve 1 con el lugar vivo y 0 después de crearlo (o en los mapas `0x12` a `0x17`, que vuelve a
  mirar). Con los 10 vivos no crea nada. La tabla es una para las escenas 2, 4 y 5. En el login,
  `0x6349a0` lo crea con el personaje de `0x517270` (`0x443fa0(0xbf, +0x880, +0x870)`, `0x634a88`); en
  la selección, `0x5139f0` (el armado desde la lista, desde `0x4c20a0`) con el bit 0 del byte 11
  (`+0x318` en `0x1c16`). `0x513430`, que también llama a `0x443fa0` (`0x51359e..0x513654`), no tiene
  referencias: ni una llamada ni su dirección en el archivo (*estático*); antes este documento la
  daba como de la selección. En el juego lo crean (*estático*, por las llamadas a `0x443fa0`):
  `0x5bc740` con el Dark Horse (`0x1a04`) en el lugar 8 (`0x5bc997`), desde el `F3 10` ([El F3 10, el
  inventario](#el-f3-10-el-inventario)); a `0x5bc740` la llaman además otros 18 lugares, sin leer;
  `0x5be150` (el ítem del
  cursor, `0x5be530..0x5be5ea`); `0x4c8380` (el `0x25`, `0x4c87cf`); y `0x5139f0` desde el `0x12`
  (`0x4c8860`), el `0x68` (`0x4e0980`), un subcódigo del `F3` (`0x4e316e`), `0x4c8340` y `0x497810`.
  `0x443d70` lo deja vivo con el alpha (`+0x174`) en 0 y su meta (`+0x170`) en 1, la luz por vértice
  (`+0xe8`) prendida, el blendMesh y la malla oculta en −1, la luz del blendMesh (`+0x68`) en 1,
  `+0xf4..+0xfc` en 3, la velocidad (`+0xd8`) en 0,5, el frame (`+0x114`) y la key anterior (`+0x118`)
  en 0, sin verse (`+0x16c`), los ángulos del jinete y, con el `0xbf`, la escala en 1. **No escribe la
  acción (`+0x111`) ni la anterior (`+0x112`)**: el lugar que se vuelve a usar se queda con las del
  ayudante de antes (Q-C221).
- **Quién lo apaga.** `0x443d40(jinete)` pone en 0 el byte vivo de los lugares vivos de ese jinete y no
  toca lo demás. La llaman el `F3 10` (`0x4c5e56`), `0x4c7523` (`0x4c7b68`), el `0x25` (`0x4c8722`), el
  `0x23` (`0x4d10d0`, `0x4d120e`), `0x513430` y `0x5139f0` antes de crear (`0x513561`, `0x513b2f`),
  `0x5bc740` (`0x5bd4e3`), `0x5bdbae` (`0x5bde46`), `0x5bec50` (`0x5c2d9c`) y `0x6431b0` (`0x643390`);
  `0x510f70` hace lo mismo en línea con cada personaje que mata (`0x510fa1..0x510fc0`).
- **Su paso** (`0x445230` → `0x444010(lugar, 0)` en los 10, en el login desde `0x634c60`, después de
  `0x606560` (el logo) y antes de `0x508f00` (los personajes): ve la acción que el jinete tiene en ese
  paso; en el juego desde `0x6370f4`, después de los peces y antes de `MoveHero`, también en la
  espera). **En la escena 5**, con el jinete muerto o con su `+0x88` distinto de 1, lo apaga y no hace
  más (`0x44402a..0x444047`, Q-C225). `0x4e9980` lleva el alpha hacia su meta un décimo (`0x6b87b8`) de lo que falta con `+0x16d` en 0, y
  baja `+0x68` al alpha si es mayor: como el alpha sube, `+0x68` queda en el del primer paso, 0,1
  (Q-C112). En una celda de zona segura (el bit 0 del atributo en `0x7d0d348`, la del **jinete** por
  0,01, truncada, con el byte bajo de cada coordenada (`0x5f82a0`): `0x4440b8` lee `[EDI + 0x14]` con
  `EDI = [ESI + 0x108]`; la de su posición, no la lógica de `+0x41c` que lee `0x505de0` para `+0x3e2`,
  Q-C222) el alpha va a 0 y salta al avance, con la acción y la `+0xd8` de antes, sin copiar nada del
  jinete: la celda (106, 141) del mundo 56 tiene
  `0x08`, y el mundo 56 no tiene ninguna con el bit 0. Si no, copia del jinete la posición, los
  ángulos y `+0x28..+0x30` (la cabeza), pone el hueso de la cabeza del modelo (`+0x54`) en 7 (en
  `DarkHorse.bmd` es una raíz sin hijos que ningún vértice usa: no se ve) y elige su acción por la
  del jinete (`+0x111`), con `0x4ea030`:

  | La del jinete | La del caballo | `+0xd8` |
  | --- | --- | --- |
  | `0x53` | 3 | 0,34 |
  | `0x4b` | 1 (y su polvo: en el mapa 10, siempre, con `rand() & 63` primero, la pose del dibujo anterior (`0x4ec110`), `0x518cf0(0x6eb)` y `0x53fe70(0x5f2)`; en los demás, con `rand() % 2` en 0, fuera de Kalima (24 a 29 y 36) y sin `+0x78 & 0x4000` del jinete, tres `rand()` para el lugar (de −32 a 31 en x y en y, y de −16 a 15 en z, alrededor del caballo) y el `0x5f2` en el 2 y el `0x5f3` en los otros, con la luz (1, 1, 1)) | 0,34 |
  | `0x4e` a `0x52` | 2 | 0,34 |
  | `0x54` | 5 | 1 |
  | `0x55` | 6 | 1 |
  | otra (la `0x4a` del login) | 0 (y `+0x8e` en 0) | 0,3 |

  Después, con la acción distinta de 1 y sin `+0x78 & 0x4000` del jinete, `rand() % 3`, y con 0 el
  aliento (`0x5f2`, o `0x607` en el mapa 7; antes este documento lo daba como polvo, [Los efectos del login](#los-efectos-del-login)), que `0x53fe70` pone en el arreglo de `0x743a198` (de `0x78` bytes; lo mueve `0x5446e0` desde
  `0x634c60` y lo dibuja `0x548550` desde `0x6350b0`), desde el hueso 27 (`0x4ec110` con
  `0x68b1d2c`, *inferido*: en el paso, ese buffer tiene la pose del frame anterior); con la 3,
  `0x52e470` con el héroe publicado y `0x4453e0` (que sube `+0x8e` y, con el contador de pasos
  `0x7d3a0f0` múltiplo de 10, crea un `0x6eb`); con `jinete + 8` en 1, doce `0x60f` (`0x530860`, de a
  tres en cuatro giros), y `+8` vuelve a 0; el byte vivo pasa a ser el del jinete
  (`0x44475c..0x444764`). Y avanza con `0x4ec200` a `+0xd8`, con la acción en `+0xa0` del modelo.
- **Su dibujo** (`0x6350b0`: `0x510d00`, los personajes; `0x4453b0`, los ayudantes; `0x6046a0`, los
  objetos; en el juego, desde `0x637508`, después de los peces y antes de las hojas).
  `0x4453b0` llama a `0x445260(lugar, 0)` con los 10. `0x445260` prende `+0x16c` con el frustum
  (`0x5fba90`, en su lugar por 0,01, −20) y, visto y con el jinete `0x20a` (o el tipo `0x1c12`), le
  pone la escala 1,6 en la escena 2, 1 en la 4 y, en las demás, 1 salvo a los de Fenrir (`0x14a` a
  `0x14c`), y llama a `0x602e90(caballo, 0, 0, 0)` (10 con `+0x78 & 0x4000` del jinete, que
  `0x6002e0` lleva por otro camino desde `0x60033c`). Después saca `rand() % 30 + 70` (`0x445336`),
  una luz que solo usa el `0x1c12` (con `0x5491e0`), aunque `0x5ffdf0` no lo haya dibujado (Q-C223).
  `0x5ffdf0`, en los mapas `0x12` a `0x17`, le baja el alpha con `+0x1a1` del héroe publicado
  (`0x5ffe09`); no lo dibuja con el alpha por debajo de 0,01; si no, con `+0x78 & 0x1000` del jinete
  le pone el frame en 0 (`0x5fff0d`, Q-C224), y el color de `0x5ffce0` con luz por vértice (la del terreno más 3), que `0x6002e0`
  pisa: el caso `0xbf` pone el color en (1, 1, 1) y hace `RenderBody(2, +0x174, +0x64, +0x68, +0x6c,
  +0x70, +0x58, −1)`; después el color en (1; 0,8; 0,3) y `0x4ecc30(12..15, 0x44, …)` directo, el
  cromo aditivo de la malla 12 (`dkneck2`; las 13 a 15 no existen y `0x4ecc30` sale, Q-C113); y,
  fuera de los mapas 10, `0x18` a `0x1d` y `0x24`, `0x620a00(1)`, `glColor4f(0, 0, 0, 0,7)` (0,2
  en el mapa 7) y `0x4eff40(−1, −1, 8, 9)`: la sombra de la malla 8 (`dkhorsebase`), aplastada
  como la del personaje, con la mezcla normal y sin culling (de `0x620a00`), y sin alpha test, sin
  textura y sin escribir la profundidad (`0x620910(0)` y `0x6208b0`, que `0x620890` vuelve a prender
  al final).
- **La luz por vértice.** Con `+0x44` del modelo (la copia de `+0xe8`), `0x4ebc80` deja por cada
  normal girada por su hueso max(0,2; 0,8 · L·n + 0,4), con L = (1,3; 0; 2): la rama de
  `DAT_006c8818`, que vale 1 desde el `.data` y nadie escribe (Q-C115). El ángulo con que se gira L
  (`+0x8c` del modelo) lo escribe solo la rama del mundo 30, que no corre. La normal no se normaliza
  y, en un objeto de `0x5ffdf0`, lleva su escala: `0x4eb860` (con Translate) pone la escala en la
  raíz, y `0x4ebc80` (sin Translate) gira la normal con esa matriz (emulado: largo 1,6 en el login).
  `0x4ecc30` la multiplica por el color en el pase con textura (el modo 2): con el blanco del caballo
  y la escala 1,6, va de 0,2 a 0,8 · 1,6 · 2,385 + 0,4 = 3,45, que el GL recorta a 1. **Verificado**
  contra las trazas de GL: el color por vértice de los bloques con luz por vértice ajusta con
  (1,3; 0; 2) (R² mediano 0,93 en la selección y 0,42 en Lorencia, contra −0,26 y −0,41 con
  (0; −1,5; 0)). El cromo (el modo 4) va con el color parejo, con las UV de la normal con su escala
  (u = 0,5 · nz + t, v = 0,5 · ny + 2t) y, con el alpha por debajo de 0,99, por el alpha (Q-C95). El
  pase base con ese alpha va con `0x620a00(1)` y `glColor4f`, con su alpha test (0,25): en los pasos
  1 y 2 (0,1 y 0,19) el cuerpo no se ve, aparece en el 3 (0,271) y queda opaco en el 44 (0,9903); la
  sombra al 0,7 y el cromo por el alpha se ven desde el primero.
- **La carga.** `0x615700` (desde `0x61ccb0`, después de `0x611850`) carga `Data\Skill\DarkHorse.bmd`
  (`0x615f46`) y, después de su `0x60d530(0x3b8)` (`0x61692a`), les da texturas de `Skill\`, con
  `0x2901` y `0x2600`, a `0x145`, `0x146` y `0x147` (`elf_skill`, `WaveForce` y `deathsp_eff`:
  cuatro nuevas, de `0x3b8` a `0x3bb`) y al `0xbf` (`0x6169a7`): 11 nuevas, de `0x3bc` a `0x3c6`, y
  `dkhead2` y `dkheadwing`, que encuentra en `0x26b` y `0x26c` (las subió `0x1c16`, `DarkHorseHorn`,
  también de `Skill\`, en `0x611850`). **Verificado:** los 15 nombres de `0x3b8` a `0x3c6` son los
  de `lor_slots.bin` (`ModelTextureSlotsGoldenTest.StartupMatchesTheDump`).
- **`DarkHorse.bmd`**: 13 mallas (`dkfurop.TGA`, de cuatro componentes, y 12 JPG), 60 huesos y siete
  acciones (7, 7, 6, 10, 41, 51 y 31 keys).
- **En MuRet.** El del login es `Scenes::DarkHorse` en `LoginScene`, con su paso (`StepDarkHorse`, sin
  celda segura: el mundo 56 no tiene ninguna) y su dibujo (`DrawDarkHorse`, con la sombra por mapa). El
  del juego (CL7, hito 1, T12) es `Scenes::HelperTable`, la tabla de `0x7d335d0` en
  `WorldState::Helpers`: `CreateHelper` (`0x443fa0` → `0x443d70`, solo el `0xbf`: los otros tipos
  cortan), `TurnOffHelpersOf` (`0x443d40`, desde el `F3 10`; `0x510f70` en `KillOtherActors`),
  `StepHelpers` (`0x445230` en la escena 5, en `GameScene::StepWorld`, en `0x6370f4`) y `DrawHelpers`
  (`0x4453b0` en la escena 5). Cortan con error, después de los `rand()` que el original saca antes,
  el polvo (en el mapa 10, antes de su primer `rand()`), el aliento, la acción 3, el `+8` del jinete,
  el dibujo con `+0x78 & 0x4000` y los mapas `0x12` a `0x17` del dibujo. En el juego no se crea
  ninguno: `0x5bc740` corta antes con el `0x1a04` (`0x4225d0`, P-C36) y los demás que lo crean no están
  portados; la tabla vacía no hace nada. `DrawHelpers` no corre: el dibujo del juego corta antes, a más
  tardar en los ítems del piso (`0x6374fe`). La tabla no lleva los caballos del login ni de la selección (P-C47).

### La tela de la clase 4

Todo de la CL6.6, leído en el ensamblador y **verificado** con el código real del `main.exe` emulado
con unicorn (`scripts/emulate_cloth.py`): una reimplementación escrita solo a partir de esta sección
(`scripts/cloth_reference.py`) y la de MuRet (`Render/Models/Cloth`, `Scenes/CharacterCloth`) dan lo
mismo que el binario **bit a bit** en nueve casos que pasan por todas las ramas (los vectores de
`Client/tests/vectors/cloth`). La cobertura de la emulación: 2.971 de las 3.093 instrucciones de la
tela, de sus wrappers de GL y del bloque; las que faltan no tocan la física (los `new` que dan NULL,
la tabla de protección llena, la lista vacía del administrador).

**Dónde corre.** El bloque `0x50d58b..0x50db57` de `0x50ba30`, después del lazo de las partes
(`0x50d3dc`) y antes de lo que cuelga, en cada frame **dibujado** de un personaje vivo y visible
(`0x510d00`): cada tela hace 5 pasos de dt = 0,005 (`0x3ba3d70a`, o 0 con `objeto + 0x78 & 0x1000`)
y se dibuja antes de la siguiente. La tela avanza con los frames, no con el tiempo (Q-C124); MuRet
corre un frame del bloque cada 46,875 ms de reloj, como la mascota (Q-C20).

**Cuándo hay.** bVar23: la clase 3 o 4, el modelo `0x20a` y `+0x3e3` en 0, o el monstruo `0x37`;
nunca en los mapas `0x12` a `0x17`, salvo con los nombres de un duelo o de una guerra de guild, que
la prenden en cualquier clase con el color (1; 0,2; 0) o (0; 0,2; 1) (`0x50c929..0x50cc29`; qué son
esos nombres, abierto). El color, si no, es (1, 1, 1).

**El armado** (con `objeto + 0x190` en 0). La clase 4: `new` de cuatro telas (`0x6a0210(…, 0x60, 4,
0x4548b0, 0x454980)`) y `+0x18c` = 3:

| Tela | Hueso, (x, y, z), columnas × filas, ancho × alto, texturas, banderas | Viento (`+0x49`, `+0x48`) | Esferas (x, y, z, radio, hueso) |
| --- | --- | --- | --- |
| 0 | 20, (0, 0, 20), 6 × 5, 30 × 70, `0x256`, `0xc0000205` | 10, 50 | (−10, 20, 20, 27, 17) y (10, 20, 20, 27, 17) |
| 1 | 20, (0, 5, 18), 5 × 5, 30 × 70, `0x256`, `0x80002205` | 8, 40 | las mismas dos |
| 2 | 19, (0, 8, 10), 10 × 10, 180 × 180, `0x257`, `0x1005` | — | (±10, −10, −10, 25, 17) y (±10, −10, 20, 27, 17) |
| 3, con `+0x238` = `0x221e` (`ArmorClass05`) | 18, (0, 10, −5), 5 × 5, 50 × 90, `0x6ea`, `0x1406` | — | (0, −15, −20, 30, 2) |

Las demás, una tela y `+0x18c` = 1: el monstruo `0x37`, 19, (0, 10, 0), 10 × 10, 55 × 140, `0x252`,
`0x1001`, con (±10, −10, −10, 35, 17) y (0, −10, −40, 50, 19); si no, 19, (0, 10, 0), 10 × 10,
75 × 120, `0x250` y `0x251` (Robe01 y Robe02), `0x405`, con las cuatro de la tela 2. Las texturas:
`0x256` es `Monster\iui06.jpg` (`GL_NEAREST`, `GL_CLAMP`) y `0x6ea` `Player\kaa.tga` (`GL_LINEAR`,
`GL_CLAMP`), de `0x61ccb0`; `0x250` a `0x252` y `0x257` (`DarklordRobe`), de las del personaje.

Con el arreglo armado y la clase 4: con `0x221e` y `+0x18c` en 3, la tela 3 se vuelve a armar
**encima, sin soltarla**, con (0, 8, −5) y `0x1407`, la esfera otra vez y `+0x18c` = 4 (Q-C123); con
otra armadura y `+0x18c` en 4, se suelta (`0x454f80`) y `+0x18c` = 3. En el lazo, la tela 2 de la
clase 4 no va sin las alas `0x1c30` y con el color blanco.

**El init** (`0x454a50`, vtable `0x6b8cd8` + 4): las banderas con OR; dx = f(ancho / (columnas − 1)),
dy = f(alto / (filas − 1)); la matriz M = AngleMatrix de los ángulos del objeto con la traslación del
hueso (la columna 3), y la grilla provisoria: con `& 0xc` = 4 el ancho de la fila crece,
W_j = ancho · ((j · 0,4) / (filas − 1) + 0,6) con el 0,4 de `0x6b8cf4`, que es `0x3ecccccc` y no el
0,4f del compilador; x = f(i · dx_j − f(W_j · 0,5)), z = f(−j · dy), y = 20, o 20 − t² · 10 con
`& 3` = 1 (t = |i / (columnas − 1) − 0,5| · 2), o 0 con 2; cada punto, TransformPosition(M). Los
resortes, en este orden para cada k: el vertical (bandera 2 | 4, o 2 con `0x100`), el horizontal (3),
la diagonal hacia abajo (1, o 0 con `0x100`) y, **solo desde la fila 2**, la diagonal hacia arriba
(Q-C122); el largo L = f(|p_a − p_b|) en la grilla provisoria y el mínimo f(L · 0,8). Después la fila 0
se ancla en esa misma M: en el primer paso salta al hueso (Q-C126).

**El anclaje** (`0x455020`): cada partícula de la fila 0 a TransformPosition(m, (z, −y', xᵢ)), con
xᵢ = f((i · dx − W · 0,5) + x) (W y dx en double con `& 0xc` = 4: 0,6 del ancho) e y' = y, o
f(y − t² · 10) con el arco: la x del hueso es la z del anclaje, la y es −y' y la z reparte las
columnas. TransformPosition (`0x4ec110` con su cuarto argumento en 1) es s · VectorTransform + el
origen, con la escala del modelo **después** de la traslación del hueso y s · tx sin redondear.

**El paso** (`0x4551d0`), en orden:

1. el viento: con `& 0x300` = `0x200`, w = f((rand() % `+0x48` + `+0x49`) · 0,01); si no, en el mapa
   `0x37`, f((rand() % 40 + 10) · 0,02); si no, el viento base `0x7245f0`. Con a = (ángulo z del
   objeto + 180) · π/180: `0x7245f4` = f(sin a · w) y `0x7245f8` = f(−(cos a · w)); la z
   (`0x7245fc`) no la escribe nadie;
2. las fuerzas (`0x4552a0`): V, el contador protegido de pasos de lógica (`0x7d3a0f0`), cuyo acceso
   al cerrarse saca 4 `rand()` (Q-C125); el centro c = ((V / 10) · 101) mod N; para cada partícula
   no anclada, d = la distancia de Manhattan a c en la grilla y el peso P de u = 5 − d sin signo: 6 si
   u ≥ 4, 0 si u = 0, u + 2 si no (d = 5 da 0 y d ≥ 6, 6: Q-C121); Fₖ = f(P · vientoₖ − velₖ · 0,01),
   la z más f((w + 0,1) · P) con `0x100` o f(P · w) con `0x200`, y menos 0,0025 · 9,8 · 100 (· 180
   con `0x400`);
3. los resortes con la bandera 2 (`0x4555d0`): solo tiran, si ℓ > L + 0,01, con f(e · dₖ / ℓ) (por
   L / 32 con `0x20000000`, por 3 con `0x100`) a b y su opuesto a a;
4. el anclaje de la fila 0 con el hueso de la tela;
5. la integración (`0x454350`): v = (400 · F) · dt + vel, vel = f(v) y pos = f(v · dt + pos) con v
   sin redondear;
6. las esferas (`0x455770`): su centro con (z, −y, x) desde su hueso; cada partícula a menos que el
   radio, la corrección (radio − r) / r · d; el promedio;
7. la relajación (bandera 1, Jacobi): ((ℓ − L) · 0,5) / ℓ · d repartido, y el promedio;
8. la restricción (bandera 4, Gauss-Seidel sobre la partícula de abajo): si ℓ > 20 · L, el paso da 0
   y `0x4fb1a0` suelta todas las telas (el frame siguiente las arma de nuevo desde la pose de ese
   momento); si ℓ > L o ℓ < f(0,8L), mueve la de abajo a la distancia.

Cada cuenta va en double y se redondea a float donde el original hace `FSTP`; la palabra de control
de la x87 de 53 bits (`0x27F`) da lo mismo que la de 64 (`0x37F`), y la de 24 cambia 7.736 de 38.854
valores. Los que no se redondean y es fácil equivocarse: el cos del ángulo 2 de `AngleMatrix`, s · tx
de TransformPosition, el medio ancho del anclaje, la velocidad de la integración, e · d_z de la
esfera y Δ_z de la relajación y de la restricción.

**Lo global.** `rand()` es el de la CRT (`0x69fd70`, sembrado una vez con `srand(time(NULL))`). El
viento base lo pasea `0x456b10` cinco veces por paso de lógica (desde `0x637700`, en las escenas 2,
4 y 5): b = f(b + (rand() % 200 − 100) · 0,001) entre −0,2 y 1. Por frame, el Dark Lord saca 75
`rand()` en el login (el mapa `0x37`) y 70 fuera de él; el orden con los demás que lo usan en el
frame no se siguió (P-C22).

**El dibujo** (`0x455930`): la mezcla por `& 0x3000` (0 opaca, `0x1000` la normal con alpha test,
`0x2000` aditiva, `0x3000` la que estaba), el color de `0x50ba30` y, con `0x40000000`, el brillo
t = f(sin(T · 0,001) · 0,1 + 0,4), que pone el color (t, t, t) y la mezcla aditiva y pide 30 sprites
`Effect\flare01.jpg` (`0x5491e0`, con la escala f(i · 0,2 + 0,4) y la luz f((columnas − i) · t · 0,2)
que cambian cada `filas` sprites); los dibuja después `0x549440` → `0x549290`, como billboards
aditivos de un frame (CL7.R, [Los efectos del login](#los-efectos-del-login)).
Después `GL_QUADS` en coordenadas del mundo: la cara de adelante (i, j), (i+1, j), (i+1, j+1),
(i, j+1) y la de atrás al revés, salvo con la aditiva y el brillo; u = f(i / (columnas − 1)) y
v = j / (filas − 1) hasta 0,99. Sin normales ni luz (`GL_MODULATE`). Sin culling fuera de la opaca:
con la normal y la aditiva van las dos caras.

### La luz del terreno en un punto

Lo que usan los modelos sin luz por vértice: el personaje fuera de la escena 4 (`0x50ba30`) y los
objetos como el logo (`0x5ffce0`). *Estático*, leído en la CL6.4.

- **Al cargar un mundo** (`0x5f8f40`): `0x63b490` lee `TerrainLight.OZJ` en `0x78ecc10`; `0x5f8b10`
  saca la normal de cada celda (x, y) del triángulo (x + 1, y), (x + 1, y + 1), (x, y + 1), con las
  celdas a 100 unidades y las alturas de `0x7accca4` buscadas con x + 1 e y + 1 en un byte (la
  columna y la fila 255 toman las de la 0; las posiciones, no): `0x5fd000` da (p2 − p1) × (p3 − p1)
  normalizada, y con largo 0 no escribe; y `0x5f8d90` sombrea en `0x7c0ccc4` cada canal de la luz
  con n.x · 0,5 − 0,5 · n.y + 0,5 · n.z + 0,5, de 0 a 1 (en el mapa 30, −1 y 1 en vez de −0,5 y
  0,5). En un terreno plano la sombra es 1: la zona de la traza de Lorencia es plana, así que no
  distingue esta luz de la del OZJ (*inferido* qué tabla lee el terreno).
- **Bit a bit** (*estático*, el ASM de `0x5f8b10`, `0x5fd000` y `0x5f8d90`; `World::FaceNormal` y
  `World::ShadeTerrainLight` desde la CL7). `0x5fd000` es la misma rutina que la del picking
  ([Caminar](#caminar), el rayo contra el terreno): uy, uz, vz, nz y el largo quedan en el registro, y
  vy, nx, vx, ux y ny pasan por un float de la pila. Los anchos son 100, así que nz vale 10.000 y el
  largo nunca es 0. `0x5f8d90` hace (b · n.z + a · n.y) + n.x · 0,5 y después + 0,5 en el registro,
  da 0 si queda menor que 0 o NaN (`FCOM`, `C0`) y 1 si pasa de 1, y con la sombra todavía en el
  registro hace `FMUL` float y `FSTP` float en cada canal. Con PC53 (la CW `0x027f` que se observó en
  el hilo de main, [La cámara del juego](#la-cámara-del-juego)), cada canal se redondea a 53 bits y
  después a float, y la sombra nunca pasa por float. MuRet usa ese perfil; no fija la CW dentro de
  estas funciones. Con la normal y la sombra en float, como hasta la CL7, cambian 5.682 celdas de
  Lorencia y 14.628 de Devias.
- **Los volcados** de `0x78ecc10` (`lor_light.bin` y `s31_light.bin`, P-C15 e) son la luz **sin
  sombrear**, la que `0x5f8d90` lee: dan los bits del OZJ en las 65.536 celdas de los dos mapas, y
  difieren de la sombreada en 14.736 celdas de Lorencia y 29.446 de Devias, las que tienen pendiente
  y luz. La sombreada (`0x7c0ccc4`) no se volcó. Los bits de `GoldenTerrainLighting` salen de un cálculo
  aparte del mismo ASM, [`scripts/game/maps/terrain_light.py`](scripts/game/maps/terrain_light.py), no
  del original.
- **En cada paso** (`0x5fbf60`, desde la lógica del login, de la selección y del juego, en
  `0x636de8`): copia `0x7c0ccc4` a `0x7b4ccc4` en las celdas de la ventana que se ve (`0x7d2d364`
  a `0x6c9d68` + 3 en x, `0x7d2d368` a `0x6c9d6c` + 3 en y), donde después se suman las luces
  dinámicas (`0x5f9780`, [Lo propio de Lorencia](#lo-propio-de-lorencia)). MuRet ya comparte el
  rango y la tabla RGB entre escenas ([El buffer compartido de luz dinámica](#el-buffer-compartido-de-luz-dinámica));
  eso no completa los productores de luz ni comparte los buffers de olas.
- **En un punto** (`0x5f99e0`): con la escena distinta de 0, 1 y 3 y el terreno cargado
  (`0x7d2d35c`, que pone `0x5f9000` al cargar), la celda es x · 0,01 y y · 0,01 truncados, y entre
  0 y 254 la luz es la bilineal de `0x7b4ccc4` entre las cuatro de alrededor (primero cada columna
  en y). Fuera, (0, 0, 0). Como `ftol` trunca hacia el 0, de −1 a 0 todavía es la celda 0.

### El mundo del login

Leído en la CL7.R, en el barrido del login (*estático* salvo lo que se marca; lo *emulado* corre el
código real del `main.exe` con los scripts de [`scripts/login/`](scripts/login/README.md)). Lo que el
jugador ve detrás del Dark Lord es el mapa 55 entero: el terreno, 214 objetos del `.obj`, un dragón
que vuela y las partículas de las secciones que siguen. Lo que se nombra "del video" es del recorte
del login del original que mandó Emanuel (314 × 340, 15 s, fuera de Git): qué cosa del video es cada
fuente es *inferido*, por comparación. MuRet, hasta la CL7, no dibuja nada de eso.

- **La carga** (`0x61a560(0x37)` → `0x616ec0`). `0x6070a0` suelta los modelos 0 a 159, vacía las
  256 listas de objetos, suelta los slots `0x23` a `0x67` y apaga el byte "vivo" de varias tablas,
  entre ellas la de efectos (`0x7491fd8`): solo ese byte, lo demás de cada entrada queda del uso
  anterior (Q-C138). Después `0x616ec0` sube `Object8\drop01.jpg` (`0x607`), el agua de los modelos
  (`Object8\wt00..wt31.jpg`, `0x41..0x60`), `Object8\light01.jpg` (`0x68`) y, en el caso del mapa
  `0x37` (`0x6188ae`), el modelo `0xb4` con `0x60da30(0xb4, "Data\Object56\", "Dragon", -1)`, sus
  texturas (`drr_01..03.jpg`, en `0x69..0x6b` por la regla de los slots; `lor_slots.bin`, sacado en
  Lorencia después del login, todavía tiene `drr_03` en `0x6b`) y **un objeto
  del mundo `0xb4` en (5.600; 23.000; 0)**: el dueño del dragón (abajo). El mapa 55 no tiene sonidos
  de ambiente (el caso no llama a `0x41eac0`). El lazo genérico (`0x61a0c4..0x61a0e1`) carga **el
  modelo k (0 a 159) de `Data\Object56\Object<k+1>.bmd`** (`"%s0%d.bmd"` hasta el 9), con sus
  texturas de `Object56\` desde `0x41c`; los archivos que faltan se saltean sin avisar (Q-C23). En
  el `Data\` faltan los 01, 17, 36, 39, 41, 51, 59, 60, 79, 81 a 83 y del 92 al 160; de los que pide el
  `.obj`, solo el `Object83.bmd`: el tipo 82 queda sin mallas. `Dragon2.bmd` y `Object28_.bmd` no los carga nadie (Q-C127).
- **Los archivos del mundo:** `World56\EncTerrain56.map`, `.att` y `.obj`, las alturas de
  `TerrainHeight` por 3 (`0x6b86b8`, solo en el mapa `0x37`; 1,5 en los demás) y `TerrainLight`, sin
  rama del 55. Los tiles son los de siempre (`0x4377a0(0x37)` devuelve 0): `TileGrass01..02`
  `0x23..0x24`, `TileGround01..03` `0x25..0x27`, **`TileWater01` `0x28`**, `TileWood01` `0x29`,
  `TileRock01..07` `0x2a..0x30`, las TGA del pasto en `0x32..0x34` (`TileGrass03.tga` no está en
  `World56`), las hojas en `0x64` y `0x65` y la lluvia de `World1` y `World10` en `0x66..0x68`. La de
  `0x68` pisa el `light01.jpg` de `0x616ec0`, sin que se vea en el login (Q-C136).
- **Los objetos** (`EncTerrain56.obj`, 276 registros; *inferido* qué se ve, con `objvis.py` y la cámara
  de `LoginCamera`). Caen en el viewport **217**, y se ven 214: los otros 3 son los marcadores ocultos.
  De los 59 que no, 9 son del tipo 82 y 50 quedan fuera, en los bordes del mapa. Se ven en tres planos:
  - **adelante**, a 1.300–2.500 de la cámara: las rocas del acantilado de la izquierda (tipos 1 y 27),
    **78 matas de hojas animadas** (tipos 83 y 88, `severleaf01/02.tga`, 13 keys) que cubren el borde
    de abajo y hacen de piso bajo el caballo, la catapulta rota (43, 44), las lanzas (75, 76), una
    bandera (12) y **tres haces de luz** (65, `sunlights_R.jpg`, 25 keys) que cruzan la pantalla de
    arriba abajo;
  - **el castillo**, a 6.400–12.700: postes, muros, la puerta, gárgolas, banderas, cuernos, rocas
    grandes y el farol (60), sobre el horizonte, con **un dragón de piedra fijo** (`Object34`, tipo
    33, `floordragon`) en (9.428; 22.658; 2.188), sobre una torre;
  - **el cielo**: tres remolinos (abajo) y el dragón que vuela.

  **En el login no se recorta nada antes de GL:** el polígono 2D de `0x5fb570`, girado 36° en el mapa
  `0x37` (45° en los demás) alrededor del héroe, abarca los 256 sectores, los 276 objetos y todas las
  celdas del terreno (*emulado* el polígono en la CL7.1: (−1.627,81; 478,11), (336,16; 1.905,01),
  (965,29; −88,87) y (74,07; −736,37) en celdas). El héroe es el personaje 0, no el
  Dark Lord: `0x6350b0` le escribe la posición (11.679; 14.191; 333) en cada frame.
- **Al crearse** (`0x606660`, *estático* y *emulado* con `run_objs.py`): en las escenas 2 y 4 el farol
  (tipo 60) pasa a escala 0,8 y luz 0,2 (el archivo trae 1,10), y en el mapa `0x37` (caso
  `0x606d03`) **los remolinos 86 y 90 van a (8.600; 25.000; 5.000), con ángulos 0 y escalas 60 y
  110**, pisando lo del archivo. Los demás, con lo del archivo y lo de siempre: luz por vértice,
  velocidad de animación 0,16.
- **En cada paso** (`0x606560` → `0x604ab0`, solo los objetos dibujados el frame anterior): el alpha
  va hacia su meta y la animación avanza 0,16 keys, 4 keys por segundo: las banderas (9 y 10 keys,
  2,25 y 2,5 s por vuelta), las hojas (13 keys, 3,25 s) y los haces (25 keys, 6,25 s). En el mapa
  `0x37`, los tipos 84, 85, 87 y 89 quedan con `+0x58 = −2` y desde su primer paso no se dibujan
  (en el primer frame sí, Q-C135; en este `.obj` están dos 84, las cajas `BosBB` de `Object85` a
  escala 2,86 y 4,56, y el 89, el fuego de `Object90`; el 85 y el 87 no): son marcadores, los emisores de [Los efectos del login](#los-efectos-del-login). El
  remolino **86** gira −0,6° de yaw por paso (24 s por vuelta), con alpha 0,2, luz (1, 0, 0) y todas
  las mallas por la rama del blendMesh (`+0x64 = −2`); su `+0x68` queda en 0,28 desde el segundo
  paso. El **90** gira −0,23° por paso (62,6 s por vuelta), con alpha 1 y luz blanca. El yaw de los
  dos baja sin acotarse.
- **Al dibujarse** (`0x6046a0` → `0x602e90` → `0x5ffdf0` + `0x6002e0`; los argumentos de `RenderBody`
  y de `RenderMesh`, *emulados* para los 31 tipos con modelo, y las mezclas de cada malla, *estáticas*):
  un solo `RenderBody(2, …)` por objeto, como el logo, con el color de `0x5ffce0` (la luz
  del terreno en su lugar más `+0xf4`) y la luz de cada normal. Las mallas JPG van opacas; las hojas
  (TGA de cuatro componentes) con la mezcla normal y el alpha test; los haces de luz, aditivos y sin
  escribir la profundidad, porque el script `R` hace de la malla su propio blendMesh. El remolino 86
  sale aditivo, (0,47; 0,18; 0,18) con la luz del terreno sombreada en su lugar, y como **hay dos en
  el mismo lugar, con el mismo giro, se suma dos veces**. El 90 tiene un caso propio del mapa 55: la malla 0 sin luz, en blanco, con
  **`glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR)`**, sin culling ni escribir la profundidad:
  oscurece el cielo donde su textura es clara. Los tres están en el sector (5, 15), y el 90 va
  primero. En el primer frame los remolinos se dibujan sin haber pasado por la lógica (Q-C135).
- **El orden:** los objetos del mundo van **después de los personajes y del caballo**, sector por
  sector (x, después y) y en el orden del archivo dentro de cada uno, y antes de los efectos. Con
  objetos aditivos que no escriben la profundidad, el orden se ve.
- **En MuRet** (CL7.2): `Scenes::WorldModels` carga los modelos (el dragón `0xb4` desde `0x69` y el
  lazo genérico desde `0x41c`, con la tabla de la carga), `World::WorldObjects` crea, avanza y prepara
  el dibujo de cada objeto, y `Scenes::WorldObjectsDrawing` los recorre por sector, con el trapecio de
  la CL7.1: el sector en su centro (x · 16 + 8, y · 16 + 8: `local_18 = 8; += 0x10` en `0x6046a0`) con
  −180 y el objeto en su lugar por 0,01f, guardado en float, con −30. `0x6046a0` guarda en `+0x16c` el
  resultado del test de cada objeto, y `0x606560` apaga el de los objetos de un sector que no pasó, sin
  avanzarlos, y avanza los demás sector por sector, como el dibujo (*estático*, revisión de la CL7.2).
  El sector de cada objeto sale de la posición del archivo, antes de los casos por tipo, con el
  producto por 0,000625f en la x87, sin redondear (`0x60666c`): en los múltiplos de 1.600 el float
  daría el sector siguiente. `0x604ab0` sale después del alpha si quedó por debajo de 0,01, y
  `0x5ffce0` sin luz por vértice suma al rojo el producto sin guardarlo en float. **Los 276 objetos dan
  lo mismo que el código real emulado**, bit a bit, al crearse, en tres pasos y en lo que le pasan a
  `RenderBody` (el modo, el alpha, el blendMesh, su luz, la U, la V, la malla oculta, el color y la
  luz por vértice) o a `RenderMesh` (la malla, el alpha, el blendMesh, su luz, la U, la V y el color)
  (`GoldenWorldObjects`, `tests/vectors/world55`). El remolino
  90 va por la mezcla nueva `ModelBlend::Subtractive`. El logo es uno más de la lista, al final (lo
  crea `0x61c7c0` después de la carga), con su paso de la CL6.4. Dos diferencias con el original, por
  la tabla de la carga de MuRet, que todavía no tiene las de `0x615700` más allá de las cuatro
  primeras: los cinco nombres de las habilidades se piden de `Object56\` (los del castillo están y
  suben; `fire01` y `fire02` no, y el fuego 89 sale sin textura en su único frame, Q-C135), y el logo
  queda en `0x45a` en vez de `0x455` (P-C15 j).

### El terreno del login

`0x5fc260(0)`: el 0 es el pase de dibujo (con otro valor, el picking). El corrimiento del agua es
`(ftol(T) % 20000) · 5e-5`; después la mezcla opaca y el pase de bloques, y con la mezcla normal el
pase del pasto (prendido en el 55 por `0x5f87b0`), `0x549130` (las marcas del suelo) y el depth test
de vuelta. *Estático.*

- **Qué bloques** (*emulado*, CL7.1, [Terreno](#terreno)): con far 20.400 · 0,9 = 18.360 y la cámara
  del login, el rango da x 0..128 e y 120..251 (de 316, a `0xfb`), y los bloques de 4 × 4 que empiezan en
  x 0..128 y en y 120..248, 1.089, es decir las celdas x 0..131 e y 120..251; el trapecio las deja pasar
  todas, y las 3.244 que quedan sin el `0x08` van de la (90; 153) a la (104; 251)
  (`GoldenTerrainTrace.TheLoginDrawsTheCellsOfTheEmulatedCode`). La x1 da 128 y no 132 porque
  12.800 · −0,01f es −127,99…: el `ftol` trunca a −127. Un temblor de ±10° (`DAT_07d325c8`) no cambia
  nada.
- **Qué se ve** (la cuenta, `terrain_count.py`): en ese rango, **14.180 de las 17.424 celdas tienen el
  bit `0x08` del `.att`** y `0x5fac90` no las dibuja (*estático* el test). Quedan 3.244 (3.242 en el
  viewport): **un lago de `TileWater01`** (2.553 celdas con el agua de base: la capa 2 si las cuatro esquinas van
  en alfa 255, la 1 si no) con una orilla rocosa al fondo, en una franja
  de y = 323 a 419 en 640 × 480. La celda del personaje y todo lo cercano a la cámara están en `0x08`: ahí se ven los
  fondos 2D, y lo que hace de piso son las hojas y las rocas de arriba.
- **El agua**, sin rama del 55 (*estático*, CL7.1): `0x5fa830` marca agua (`local_8`) la capa de abajo
  cuando es la capa 1 en 5, con alguna esquina por debajo de 1, y la de alfa solo si además la capa 2 es
  5; una celda con las cuatro esquinas en 1 dibuja la capa 2 quieta aunque sea agua. Con agua,
  `0x5fa5c0` suma a la u el corrimiento (`0x78ecbec`, que `0x5fc260` saca en cada frame del reloj
  `0x5767b88`: `(ftol(T) % 20000) · 5e-5`; en el mapa 8, `(ftol(T) % 40000) · 2,5e-5`; en el `0x1e` lo
  resta) y a la v la ola · 0,002: la de la esquina (x, y) en las dos de la izquierda y la de (x + 1, y)
  en las dos de la derecha. La ola (`0x78acbd8`, por celda) la escribe `0x5fbf60` en cada **paso**, en
  la ventana del rango con 3 celdas más: `sin(x · 5 + (ftol(T) % 720000) · 0,002) · 10`, con la x de la
  celda (50 en el mapa 8); las de afuera quedan como estaban.
- **El slot `0x122`** (Q-C19): es `Interface\accept_box01.jpg`, que carga `0x61ae60` (*estático*: es la
  única carga a ese slot). En el login hay 147 celdas visibles con capa 2 = 255 y alfa, casi todas en
  la orilla del lago, con alfa de hasta 1 (la cuenta, `terrain_count.py`; que se vea, *inferido*).
- **Los tiles** (`0x61a560`, *estático*): `World<N>\TileGrass01.jpg` a `TileRock07.jpg` en `0x23..0x30`
  con `GL_NEAREST` y repitiendo; solo el primero lleva el sexto argumento de `0x63b870` en 1, el que
  avisa "File not exist" si falta: los demás se saltean sin avisar, y uno que está pero no se lee
  devuelve false y sigue (Q-C14). MuRet saltea los que no avisan, falten o no se lean, con un aviso en
  el log; sin `TileGrass01` o el `0x122` (que también avisa), el login va sin terreno
  (`GoldenLoadedWorld`). Después vienen el pasto
  (`0x32..0x34`, que lee el pase del pasto, [El pasto](#lo-propio-de-lorencia)), las hojas (`0x64`,
  `0x65`) y la lluvia (`0x66..0x68`), que el terreno no usa. MuRet sube el pasto solo en el juego
  (`TerrainTiles::Load` con `GrassTextures::Upload`): el login y la selección no lo dibujan (P-C48 b). Los
  mapas `0x18` a `0x1d` y el `0x24` arman los suyos (`0x4377a0`).
- **El pase del pasto**, en el 55, no dibuja nada en esta vista (*estático* desde el cierre): corre
  (`0x6c9d58` en 1 por `0x5f87b0`, salvo en los mapas `0x12..0x17` y `0x1e`), con la mezcla normal, en las
  celdas sin el `0x08` y con las cuatro esquinas en alfa 0: 2.402 en la ventana del login, con la capa 1 en
  5 (2.147), 2 (121), 3 (88), 4 (27), 10 (12) u 8 (7). El slot es `0x32` más la capa 1: el `0x34`
  (`TileGrass03.tga`, que `World56` no tiene) o del `0x35` al `0x3c`, que `0x61a560` no carga; ninguna tiene
  la capa 0 o 1, las únicas con textura.
- **Sin niebla** (`0x6350b0` pone `DAT_07d368a0` en 0 en cada frame) y **sin cielo 3D**: detrás
  quedan los fondos 2D y el negro de `glClearColor`.

### El dragón de fondo

*Estático* y *emulado* (`dragon_emu.py`: `0x518cf0` y 1.500 pasos de `0x5208a0` reales).

- **Quién lo crea.** No está en el `.obj` ni en el código del login: `0x606660`, en las escenas 2 y 4,
  con el objeto `0xb4` que crea la carga del mapa, le pone `+0x58 = −2` (el dueño no se dibuja), luz
  blanca, y llama a `0x518cf0(0xb4, posición, ángulos, luz, 0, objeto, −1, 0, 0, 0, 1,6)`. `0x518cf0`
  es el creador de **efectos** (la tabla `0x7491fd8`, 200 entradas de `0x1d4`); su caso `0xb4` deja:
  escala 1,6, posición (5.600; 23.000; **3.400**), ángulos 0, luz por vértice, velocidad de
  animación 1, **velocidad de vuelo (0, −35, 0)**, vida 1.000, `+0xdc = +0xe4 = +0xec = 1` y el punto
  de partida en `+0x17c`. Cada vez que se entra al login la tabla se vacía y el dragón vuelve a
  salir desde el principio. Solo existe en el login: la selección no tiene el tipo 180.
- **El modelo**: `Data\Object56\Dragon.bmd` (en el `Data\` está en minúscula): 3 mallas
  (`drr_01..03.jpg`), 93 huesos, **una acción de 41 keys**. Mira hacia −y, hacia donde vuela.
- **Cómo se mueve** (`0x5208a0`, caso `0x52096c..0x520b72`, por paso de lógica desde `0x52d220`):

  ```text
  d = |pos − partida|
  yaw y alabeo: fuera de (−360, 360) vuelven a 0
  meta del alabeo (+0x84) = 30 con el estado 0, −30 con el 1, 0 con otro
  alabeo += 0,5 hacia la meta
  si d ≤ 800: estado = 2; +0xec = 0; +0xdc = 1
  si no:
    si +0xdc ≠ 0: +0x84 = yaw; +0xe4 = −(+0xe4); +0xdc = 0
    si yaw − 60 ≤ +0x84:        yaw += +0xe4 · 1,5; estado = 1
    si no, si yaw + 60 ≥ +0x84: yaw += +0xe4 · 1,5; estado = 0
    si no:                      +0xdc = 1; estado = 2
  si vida ≤ 10: vida = 1000
  si frame ≥ 40: frame = 0; anterior = 0; acción = 0
  ```

  Después, la cola común: la animación a velocidad 1, **posición += AngleMatrix(ángulos) ·
  (0, −35, 0)** (`0x51fc80`) y la vida − 1. El resultado (*emulado*): vuela siempre a z 3.400, a 35
  por paso, en **un ocho** alrededor de la partida: 23 pasos derecho, 199 girando −1,5° por paso
  (un círculo de radio ≈ 1.337) por el oeste, 46 derecho cruzando la partida y 199 girando +1,5° por
  paso por el este; los lazos que siguen al primero duran 198 pasos. **Un ocho cada 489 o 490 pasos,
  unos 19,6 s**, sin azar (el único `rand()` del paso es una luz que el `0xb4` no usa) y sin morir
  nunca. El alabeo va a −30° en los dos lazos (Q-C128). La animación avanza una
  key por paso, del 1 al 40, sin mezcla entre keys: 1,6 s por aleteo.
- **Cómo se dibuja** (`0x52d620(0)`, después de los objetos del mundo y de los joints): la esfera
  de 400 contra el frustum, y un solo `RenderBody(2, 1, −1, 1, 0, 0, −1, −1)`, opaco, con el color de
  la luz del terreno en su lugar más (1, 1, 1) y la luz de cada normal. No tiene sombra. En pantalla
  va arriba, sobre el horizonte: el centro entre x 175 y 518 e y 102 y 184 en 640 × 480, de 50 a 98
  píxeles de ancho y de 33 a 81 de alto, a 11.600–14.400 de la cámara (*inferido*: posado y proyectado
  con `dragon_screen.py` en toda la trayectoria emulada).
- **El paso, leído en el asm** (CL7.6 A, con capstone): `+0x84` es un solo campo con dos usos. Al
  empezar cada paso vale la meta del alabeo del estado (30 con el 0, −30 con el 1, 0 con otro), y al
  alejarse de la partida con `+0xdc` prendido pasa a guardar el yaw, con el que se compara el giro de
  ±60°. La distancia va en la x87 (53 bits) hasta la comparación con 800, el alabeo y el giro se suman
  en 53 bits y se guardan en float, y el giro de la velocidad es `0x5fc8f0` (los senos y cosenos de x
  e y y el seno de z en float, el coseno de z en el registro) y `0x5fcbd0`. Las comparaciones sin
  orden (un NaN) van como la x87: un `+0xdc` en NaN no da la vuelta y un frame en NaN no vuelve a 0.
  `0x518cf0` pone la escala de 0 o menos en 0,9 (`0x518d3c`) y, en el `0xb4`, la z del dueño más 3.400
  (`0x518fc9`), el alabeo en 0 (`0x518fb3`) y la luz en blanco (la salida `0x51a97b`); en el login el
  dueño está en (5.600; 23.000; 0), con ángulos 0, luz blanca y 1,6. El creador escribe todo lo que lee
  el paso del `0xb4`, así que una entrada que se vuelve a usar (Q-C138) no cambia su vuelo. La cola común (`0x52cb98`) avanza la
  animación con `0x4ec200` a `+0xd8`; si devuelve 0 (la acción terminó o dio la vuelta), el frame queda
  en sus keys y la acción en `0xff`, cosa que al dragón no le pasa: el caso lo vuelve a 0 en el 40.
- **En MuRet** (CL7.6 A): `World::ModelEffects` (la tabla de 200, solo el `0xb4`) y
  `Render::Models::OriginalRotation` (`0x5fc8f0` y `0x5fcbd0`). **La creación y 1.500 pasos dan bit a
  bit lo mismo que el código real emulado** (`DragonTest`, `make_dragon_vectors.py`), y el giro, 400
  casos al azar (`OriginalRotationTest`, `make_rotation_vectors.py`). El login vacía la tabla y crea el
  dragón en cada entrada, lo avanza en cada paso después de los objetos, el caballo y los personajes
  (`0x606560` → `0x445230` → `0x508f00` → `0x52d220`) y lo dibuja después de los objetos, con el frame
  entero y la posición `alpha` de un paso más (D-C4; el original la dibuja tal cual, a 25 por segundo).
  Sin la esfera de 400 contra el frustum: en el login siempre está en pantalla. El `rand()` que tira
  cada paso (`0x5208c5`, una luz que el `0xb4` no usa) no va: es del `rand()` común (P-C22 a, CL7.6 B).

### El cuervo

*Estático* y *emulado*: `emulate_raven_ctor.py` corre el constructor real y `emulate_raven.py` el paso
real, desde el estado inicial de abajo escrito a mano (con la altura del terreno de `World56`, que
el emulador no tiene); `raven_reference.py`, escrito solo desde esta lectura, da **bit a bit lo
mismo** en 8 semillas × 5.000 pasos, y `raven_stats.py` lo corre en 40 semillas.

- **Qué es.** `0x1c17` en la mano izquierda es solo el ítem (`Item\SpiritBill.bmd`), y el lazo de las
  manos de `0x50ba30` lo saltea (`0x50e764`): en la mano no se dibuja nada, y MuRet ya lo saltea.
  El pájaro que vuela es otro modelo, **el `0xc0`, `Data\Skill\DarkSpirit.bmd`**: 5 mallas, 77 huesos
  y 4 acciones de 6, 4, 4 (con `lock_positions`) y 4 keys; la 0 aletea y la 1 planea (*inferido* de
  las keys). `0x420d60(personaje)`, con el `0x1c17` en la mano, crea un objeto de `0x460` bytes
  (`0x4167b0`, vtable `0x6b8904`: el paso en `+4`, `0x416a20`, y el dibujo en `+0x10`, `0x4182f0`) y
  lo guarda en `personaje + 0x430`. El objeto del pájaro es `objeto + 0xc`, el principio de un
  `CHARACTER` entero. La mascota de MuRet (`CharacterAnimation::pet`) es la de `+0x318`: no es el
  cuervo.
- **El modelo y sus texturas**: lo carga `0x615700` en el arranque, después del caballo; sus
  texturas son la **quinta** llamada después del `0x60d530(0x3b8)`, de `Skill\`, repitiendo y sin
  filtro: `0x3c7` a `0x3ca` (`dkthreebody`, `dkswing`, `dkonebody`, `foot`), y `dksbody.tga` la
  encuentra en `0x26f`, donde la subió el `0x1c17` (**verificado** en `lor_slots.bin`).
- **El estado inicial** (*emulado*, salvo la altura): escala **0,7** (ningún camino lo cambia en la escena 2), luz 0,5,
  luz por vértice, estado y acción 0, dirección (0, 0, 0), alpha 1 (aparece de golpe) y la posición
  (celda x · 100 + 50, celda y · 100 + 50, altura del terreno + 300). El constructor saca 12
  `rand()` (los cierres de las celdas protegidas y del contador, Q-C125). **En el login nace en
  (50, 50, 750)**: el personaje de `0x517270` tiene las celdas en (0, 0), porque `0x6349a0` solo le
  escribe la posición, y la altura del terreno ahí es 450 (150 · 3; el mundo ya está cargado, porque
  `0x6349a0` lo carga antes de crear al personaje) (Q-C130).
- **El paso** (`0x416a20`), una vez por paso de lógica, al final de `0x4fd5f0` del dueño (desde
  `0x508f00`: después del logo, del caballo y de las brasas, y antes de la cabeza, los efectos y las
  partículas). Con `f()` el redondeo a float y la x87 en 53 bits:

  ```text
  si dueño.flags(+0x78) & 0x1000: salir
  [zona segura del dueño (+0x3e2): en el login, 0; con 1 va a posarse, estados 3 y 4]
  frame += f(0,4); al pasar la última key, la acción por el estado (tabla 0x417f0c):
    estado 0 → acción 1 (planea), 1 → 0 (aletea), 2 → 3, 3 → 2, 4 → 1, 5 → 3
  dx = f(dueño.x − x); dy = f(dueño.y − y)
  si (dueño.flags & 0x4000) == 0:                               la chispa
    hueso = rand() % 66; p = la traslación de ese hueso en la pose del último dibujo
    0x53fe70(0x5c6, p, ángulos, (0,3; 0,4; 0,7), 5, 0,8, 0)      (1 rand() adentro)
  d2 = dy² + dx² (double); d2f = f(d2)
  si d2 ≥ 22.500: yaw = f(TurnAngle(yaw, f(CreateAngle(x, y, dueño.x, dueño.y)), f(rand() % 15 + 5)))
  v = VectorRotate(dirección, AngleMatrix(ángulos)); posición += v (cada una con f)
  H = dueño.z + 350 en el mapa 0x37 (250 en los demás, Q-C131)
  div = 28; si z < H: pitch = f(pitch − 2) (≥ −15), div = 10
            si no, si z > H + 100: pitch = f(pitch + 2) (≤ 15), div = 20
  si rand() % div == 0:                                         el sorteo
    cerca (d2f < 22.500): base = f((rand() & 7 + 32) · −0,1); yaw = f(rand() % 60 + yaw)
    lejos:                base = (rand() & 63 + 128) · −0,1
    dirección = (0, f((base + dirección.y) · 0,5), f((rand() & 63 − 32) · 0,1))
  dirección.y < −12 → estado 0; si no → estado 1    (cada cambio pone el contador +0x60 en 0)
  si rand() % 100 == 0 y [0x7d3a0f0] % 60 == 0: el grito 0x1d1
  si dueño.z − 200 > z o la distancia horizontal² > 409.600 (640²): contador += 1
  si contador > 90: posición = (dueño.x, dueño.y, f(dueño.z + 250)); contador = 0
  ```

  `CreateAngle` (`0x4e93f0`), `TurnAngle` (`0x4e9560`, con ramas que no son simétricas),
  `AngleMatrix` (`0x5fc8f0`: cos(yaw) queda sin redondear a float) y `VectorRotate` (`0x5fcbd0`)
  están transcritos del asm en `raven_reference.py`. El orden de los `rand()` de un paso: `% 66`, el
  de la chispa (`0x540714`), `% 15` (solo a más de 150), `% div` y, si sale 0, `& 7` y `% 60` (cerca)
  o `& 63` (lejos), `& 63`, y por último `% 100`.
- **Qué hace en el login** (`raven_stats.py`, 40 semillas × 5.000 pasos): viaja invisible hacia el
  Dark Lord y **salta a su lado** cuando el contador pasa de 90, a 250 sobre su origen: como pronto en
  el paso 91, y en las 40 semillas **entre los pasos 94 y 140, de 3,8 a 5,6 s después de entrar**.
  Después ronda alrededor: la distancia horizontal tiene mediana 119 (p95 166), la altura sobre el
  origen del jinete va de 318 a 515 (p5 a p95), a unas 4,6 unidades por paso, en el estado lento el
  92 % del tiempo. Dentro de los 150 no gira hacia él, y los sorteos le suman yaw siempre del mismo
  lado: describe curvas en un solo sentido. Nunca se posa: eso es solo en zona segura.
- **El dibujo** (`0x4182f0`, al final de `0x50ba30` del dueño: dentro de los personajes, antes del
  caballo y del logo). `0x5ffdf0` con la luz del terreno en su (x, y) más 0,5, la escala 0,7 y sus
  propios huesos; `0x6002e0`, caso `0xc0`: `glColor3f(1, 1, 1)`, la **malla 3** (`dksbody.tga`, mezcla
  normal con alpha test) y, con el nivel de la mascota por debajo de 20, la **malla 2**
  (`dkonebody.jpg`, opaca). El nivel lo trae el `A9`, que en el login no llega: es 0. Los vértices,
  con la luz de cada normal con la escala 0,7, como el caballo (Q-C115). Después, **la sombra**: las
  cinco mallas, negras y opacas, aplastadas sobre la altura del terreno bajo el cuervo con la regla
  de siempre (Q-C132). Si el cuervo no pasa el frustum, el dibujo le pone la velocidad en 3 y, salvo en el
  estado 4 (`0x418375`), el estado en 0, sin poner el contador en 0.
- **En MuRet** (CL7.6 C1): `Scenes::DarkSpirit` con el constructor y el paso **bit a bit contra el código
  real** (`tests/vectors/world55/raven.txt`, de `make_raven_vectors.py`: 7 casos con el constructor real y
  1.680 pasos, la chispa entera que pide a `0x53fe70` y el grito), el modelo y sus texturas en
  `GameModels` (0x3c7 a 0x3ca como en `lor_slots.bin`), y el dibujo: el trapecio con −20 (sin pasarlo,
  la velocidad en 3 y el estado en 0), las mallas 3 y 2 con la luz del terreno más 0,5 y la luz por
  vértice, y la sombra de las cinco sobre la altura del terreno bajo el cuervo. Entre dos pasos se dibuja
  con la posición de un paso más (D-C4), y la pose que le queda a la chispa es la del estado del paso.
  En el juego, desde 2026-11-11, `Scenes::DrawGameRaven` al final de cada jugador (ver
  [los efectos del jugador](#los-efectos-del-jugador)): con la luz del buffer dinámico, sin interpolar
  entre pasos (como el personaje), con las reglas de la sombra por mapa y el alpha de Chaos Castle.
  Las 12 `rand()` del constructor salen de la CRT de la tela al entrar. Antes del primer dibujo la chispa
  sale de su posición (Q-C133). El grito lee `0x7d3a0f0` crudo, que sin un acceso abierto es la basura
  cifrada del contador (Q-C125): MuRet le pasa los pasos de la tela, y no suena (no hay sonido): el paso
  lo devuelve. Las chispas, desde la CL7.6 C2, bit a bit contra el código real (`particles.txt`, 8
  casos de la chispa): el paso genérico de `0x5446e0` (la vida y el movimiento, con el doble giro de
  Q-C134), lo propio del subtipo 5 (`0x545209` → `0x545f9e`: la escala por 0,9f, `0x3f666666`, y
  la luz por 0,97087383f, `0x3f788b30`: el float de 1 / 1,03f, que no es el float de 1 / 1,03) y el
  dibujo, el camino de siempre de `0x548550` (`0x54889a`): el billboard de `0x621400` con
  `Effect\Spark03.jpg` (`0x5c6`, 32 × 32, que sube `0x61d737` con `GL_LINEAR` y `GL_CLAMP`) por la
  escala, aditivo (3 componentes), sin giro y con toda la textura. Ninguno saca `rand()`. Nace con vida
  10 y el paso de las partículas le baja uno al final del mismo paso: en cada dibujo hay 9. La pose de
  la chispa es la de MuRet, a nivel de float: `0x4eb860` acumula la posición desde la raíz en el mundo,
  y MuRet la suma al final.
- **Lo que crea**: una **chispa por paso** (`0x5c6`, `Effect\Spark03.jpg`, subtipo 5), desde un
  hueso al azar de la pose del último dibujo (Q-C133): vida 10 pasos,
  velocidad R(yaw) · (−(`rand() % 2` + 2), 0, 0), color (0,3; 0,4; 0,7) y escala 0,8, que cada paso se
  achica × 0,9 y se apaga × 1/1,03; la velocidad se gira dos veces por el yaw (Q-C134). Son los
  puntitos azules del video. Y muy de vez en cuando el grito `Sound\DSpirit_Shout.wav` (`0x1d1`).

### Los efectos del login

*Estático* (leído en el asm; no se emuló), salvo el billboard y el brillo del cetro, **verificados**
en la CL7.6 B1 contra el código real emulado en 300 casos cada uno (`tests/vectors/world55/billboard.txt`
y `scepter.txt`). El paso del login corre, después de los objetos y del
caballo, `0x530490` (las hojas), `0x508f00` (los personajes y el cuervo), `0x52d220` (los efectos),
`0x53c670` (los joints) y `0x5446e0` (las partículas). El dibujo, después de los objetos:

| Función | Qué es | Arreglo | Entradas |
| --- | --- | --- | --- |
| `0x53c6a0(0)` | **joints**: cintas con colas, `GL_QUADS` en el mundo; paso `0x53c670` → `0x535840` | `0x74a8d78` | 500 × `0xa10`; ninguno en el login (*inferido*: los creadores alcanzables del paso piden `jinete + 8`, `+0x1a0` o `+0x3e5`, en 0 en la primera entrada; el caballo crea doce `0x60f` con `jinete + 8` en 1 y lo vuelve a 0, y `0x511290` no lo limpia, así que una entrada después de otro uso podría heredarlo; los 13 `0x530860` de `0x505de0` no se leyeron, P-C27) |
| `0x52d620(0)` | **efectos** con modelo (`CreateEffect`, `0x518cf0`; paso `0x5208a0`) | `0x7491fd8` | 200 × `0x1d4`: el dragón y el `0xc2` |
| `0x549510` | prende `+0x16c` de los sprites vivos | — | — |
| `0x5306a0` | **hojas** (lluvia, hojas, nieve, burbujas; en el 55, brasas), quad en el mundo | `0x75e34b8` | 200 × `0x78`; 80 en el mapa 55 |
| `0x6213e0` | `glPushMatrix` y `glLoadIdentity`: lo que sigue va en coordenadas de ojo | — | — |
| `0x549440(0)` → `0x549290` | **sprites** de un frame (los pide el dibujo, `0x5491e0`, y se apagan al dibujarse) | `0x760f990` | 1.000 × `0x1d4` |
| `0x548550(0)` | **partículas** (`0x53fe70`; paso `0x5446e0`, 25 por segundo) | `0x743a198` | 3.000 × `0x78` |

- **El billboard** (`0x621400(tex, pos, w, h, luz, rot, u, v, du, dv)`), el de los sprites y las
  partículas: la posición pasa por la vista de la cámara (`0x7d366f0`), se arma un cuadrado en el
  plano de la pantalla, girado `rot` grados alrededor del eje de la vista, con la fila 0 de la
  textura arriba, y `GL_QUADS`. Con una textura de 3 componentes, `glColor3fv(luz)`; con 4,
  `glColor4f(r, g, b, r)` (con alpha 1 en la textura `0x5e4`). Todas las de esta escena son de 3 (**verificado** en `lor_slots.bin`). La
  mezcla no la elige el billboard: la de cada partícula sale de su textura (3 componentes, aditiva,
  `0x620a90`; 4, `0x620a00(0)`), con casos por tipo, como las nubes; la de cada sprite, de su `+4`
  (abajo). Las del login van aditivas, con depth test y sin escribir la profundidad, salvo las nubes.
- **Las partículas** (`0x53fe70`): `+4` el tipo (= la textura), `+0xc` el subtipo, `+0x10` la escala,
  `+0x14` la posición, `+0x20` los ángulos, `+0x2c` la luz, `+0x3c` la vida en pasos, `+0x44` la
  rotación del billboard, `+0x4c` "mover con la velocidad", `+0x50` la velocidad vertical propia y
  `+0x54` la velocidad. Cada paso: vida − 1, `pos += AngleMatrix(+0x20) · (+0x54)` con `+0x4c`, y el
  caso de su tipo; al empezar el paso saca 2 `rand()` (un viento global). Se crean en el primer
  lugar libre; con las 3.000 llenas no se crea.
- **Los sprites** (`0x5491e0(tex, pos, escala, luz, dueño, rot, mezcla)`): la mezcla va en `+4`: 0
  aditiva (`0x620a90`), 1 `0x620b10`, 2 normal (`0x620a00(1)`), 3 `0x620b90`, y la textura `0x637`
  siempre normal. Nacen con el alpha (`+0x114`) en 1. `0x549510` prende `+0x16c` de los vivos antes
  del pase, y `0x549290` le suma 0,1 al alpha con tope 1 (con `+0x16c` en 0, le resta 0,1 con piso
  0,2); el tamaño es la textura × la escala × el alpha, y `0x549440(0)` apaga cada uno al
  dibujarlo. En el login, el cetro y los destellos van siempre con alpha 1 y a tamaño completo.
- **Las hojas** (`0x530490` / `0x5306a0`): cada paso, cada una de las 80 apagadas renace con el
  creador del mapa (`0x42b1e0` en el `0x37`) y cada viva se mueve con el suyo (`0x5303a0`). Se
  dibujan aditivas con un solo `glColor3f(1, 1, 1)`, un `glTranslatef` y `AngleMatrix(+0x20)` por
  hoja, y `0x621320(a, b, M)`: un quad (−a, −a, −b), (a, a, −b), (a, a, b), (−a, −a, b) girado por
  M, con a = 2 · escala y b = 4 · escala.

Lo que se ve en el video, una fuente por vez:

- **Las brasas naranjas**: 80 hojas de `Effect\FireSnuff.jpg` (`0x635`, la que sube la entrada del
  login), siempre en juego: la que pasa 20 pasos en el suelo muere y renace en el paso siguiente.
  Nacen (`0x42b1e0`, 10 `rand()` cada una) con escala `(rand() % 50) · 0,01 +
  0,5`, en (`rand() % 1600 − 800`, `rand() % 1400 − 500`, `rand() % 1000 + 50`) más la posición del
  héroe, con la velocidad x `(rand() % 64 + 64) · −0,1` (con y < cámara.y + 400 es `3,2 − vx`, con el
  producto sin guardar en float; con el héroe en su lugar no pasa, porque nacen con y ≥ 13.691 y
  cámara.y + 400 = 13.078, así que vx queda negativa y van hacia −x), y e z `(rand() % 32 − 16) · 0,1`,
  y tres más que las brasas no usan (`+0x60`, `+0x64` y `+0x68`: `(rand() % 16 − 8)`, `(rand() % 64 −
  32)` y `(rand() % 16 − 8)`, por 0,1). La luz nace en (1, 0, 0), pisando el (1, 1, 1) que les pone
  `0x530490`, pero el dibujo no la usa: va con un solo `glColor3f(1, 1, 1)`; solo decide cuándo
  mueren. Los ángulos no se escriben y el quad queda vertical, en el plano x = y del mundo (Q-C139).
  Cada paso (`0x5303a0`), con el terreno por debajo (`0x5f93c0`, o un NaN), cada componente de la
  velocidad suma `(rand() % 16 − 8) · 0,1` (un sesgo de −0,05) y la posición, la velocidad (la z, con la
  de z antes de guardarla en float); si no, la z queda en la del terreno, la luz baja 0,05 por paso y,
  con el rojo en 0 o menos (`test ah, 0x41`), muere: a los 20. En el aire duran 5 s de mediana
  (*simulado*, `embers_sim.py`, con las alturas de `World56`). En el primer paso el héroe todavía está
  en (0, 0, 0) (`0x6350b0` le escribe la posición recién en el dibujo), y la cámara también: al entrar,
  `0x6349a0` la deja en (0, 0, 0) (`0x634b80`) y `0x635ae0` le escribe los 12.678 de y recién en el
  dibujo (`0x635f7e`). Las primeras 80 nacen lejos: las de y por debajo de 400 van hacia +x y las demás,
  hacia −x (Q-C137). Si corren dos pasos sin un dibujo en el medio, el segundo lee la cámara que dejó la
  ruta de `0x633920` en el paso anterior (`0x633b7a`, que corre después de las brasas); MuRet no calcula
  esa posición y usa la de `LoginCamera` (P-C23 d).
  **Verificado** en la CL7.6 B3 contra el código real emulado (`0x530490`, con `0x42b1e0`, `0x5303a0` y
  `0x5f93c0` reales, y `0x5306a0`): 3 casos de 150 pasos sobre un mapa de alturas de una fórmula (el
  primero, el del primer paso: el héroe y la cámara en (0, 0, 0)), con el estado del `rand()` y el CRC de
  la tabla en cada paso, la tabla entera cada 30 y lo que el dibujo le pide a GL, bit a bit
  (`tests/vectors/world55/leaves.txt`). La altura de `0x5f93c0`: con x o y negativos, 0. La columna sale
  de x · 0,01 guardado en float; la fila, de y · 0,01 en 53 bits (`fst` deja el producto en la pila y
  `ftol` lo trunca: 20.000 · 0,01f es 199,9999955, la fila 199 aunque en float sea 200, y la fracción de
  y, el float menos la fila, da 1); el índice, fila · 256 + columna en 32 bits con signo. Fuera de la
  grilla o con el `0x40` de su atributo, 1.200 (`0x6c9d5c`); con el índice negativo (desde la fila
  2^23), el original lee antes de la tabla de atributos y MuRet da 1.200. Si no, entre las cuatro
  esquinas (la fila y la siguiente, la columna y la siguiente, con la vuelta de 255 a 0), primero por la
  fracción de y en cada columna y después por la de x, en 53 bits. Los vectores llevan, además de 40
  alturas al azar por caso, 15 bordes: x o y en 0, y múltiplos de 100, la fila 255 con la vuelta, la
  columna 256 que cae en la fila siguiente, el índice 65.536 y la fila 2^24, que da la vuelta en 32 bits. El paso global de `0x530490` (`0x6c8c5c`, `0x75fdc3c..0x75fdc44`, con el reloj y sin `rand()`)
  es de las hojas de otros mapas, y MuRet no lo lleva.
- **Las bocanadas blancas**: **el aliento del caballo**, no polvo. `0x444010`, en cada paso, con
  el jinete (`+0x108`) sin su `0x4000` (`+0x78`), la acción del caballo distinta de 1 y
  `rand() % 3 == 0` (`0x44453f..0x4445d9`), crea la partícula `0x5f2` (`Effect\smoke01.jpg`, que
  `0x61ccb0` sube con `GL_LINEAR` y `GL_CLAMP`, `0x61d74f`; subtipo 0) con los ángulos del caballo
  (`+0x1c`, los del jinete), su luz (3, 3, 3, de `0x443e2f`) y escala 1, en `0x4ec110` **sin
  trasladar** desde `0x68b1d2c`: el **hueso 27 del buffer global de la pose** (`0x68b181c`) con el
  desplazamiento (50, −4, 0). En el paso del login, los ayudantes (`0x445230`, desde `0x634d09`)
  corren antes que cualquier otra pose, así que ese hueso es el que dejó el dibujo anterior: el
  último modelo posado sin buffer propio (`+0x11c` en 0) que **tiene** el hueso 27 y no es dummy
  (`0x4eb973` saltea los dummy sin escribirlos), y `0x4eb860` lo deja en el mundo (con su octavo
  argumento en 1, la raíz va por la escala del cuerpo y con su origen). El dragón se posa después del
  caballo (`0x52d620` → `0x602e90(e, 0, 0, 0)`), pero en `dragon.bmd` el 27 es uno de sus 14 dummy, y
  ningún objeto del mapa 55 llega a 28 huesos (los que más tienen, `Object13` y `Object19`, 8): queda
  el del caballo, `Bone03` (hijo de `Bip01 Head`), con la pose del dibujo anterior: en `0x444010` el
  avance de la animación (`0x4ec200`, `0x445039`) corre después del aliento, aunque la acción que mira
  el aliento es la de este paso (`0x4ea030`). Antes de que el caballo se vea (con el alpha por debajo
  de 0,01, antes de su primer paso), el hueso no lo escribe nadie del login. Se ve junto a su cabeza,
  como en el video del original. Al crearse
  (`0x53fe9c..0x53ff37` y el caso `0x54184b`): vida 16, escala `(rand() % 32 + 48) · 0,01`, el ángulo
  x `rand() % 360` (los otros dos, del caballo) y la rotación `ftol(T) % 360`; cada paso
  (`0x545ec4`) la luz es vida · 0,125, la velocidad vertical propia suma 0,2 y la z, esa velocidad
  antes de guardarla en float, y la escala suma 0,05. La que llega a vida 0 se apaga, pero ese paso
  la sube igual. Se dibuja (`0x548550`) del tamaño de la textura por la escala, girada `+0x44`,
  aditiva. Se ve 15 pasos: blanca los 8 primeros, y se apaga en los 7 últimos. **Verificado** en la
  CL7.6 B2 contra el código real emulado: la creación, el paso (con el viento) y el dibujo, en 20
  casos de 20 pasos (`tests/vectors/world55/particles.txt`), dada la posición. De dónde sale el hueso es
  *estático*, y la posición sale de la pose de MuRet (por cuaterniones, no por el camino de la x87):
  igual a nivel de float, no bit a bit. El
  polvo de los cascos solo sale con el jinete en la acción `0x4b`, que el login no usa.
- **El viento de las partículas** (`0x5446e0`, al empezar cada paso): para x e y, el cambio
  (`0x7609bc4`) suma `(rand() % 2001 − 1000) · 0,0006` con tope ±0,6, y el viento (`0x7609bb8`)
  suma el cambio con tope ±1,7, con las comparaciones de la x87 (un NaN pasa). El aliento no lo usa.
- **El brillo del cetro**: `0x509340`, caso `0x620` (tabla `0x50b81c`), pide **5 sprites por frame**,
  aditivos, con luz **(1; 0,3; 0)**, en `TransformPosition(hueso 0 del cetro, desplazamiento)`:

  | # | Textura | Desplazamiento | Escala | Rotación |
  | --- | --- | --- | --- | --- |
  | 1 | `0x5fd` `Shiny02` | (0, p − 100, p), p = `(rand() % 60) · 0,05 − 1,5` | s = `(rand() % 10) · 0,05 + 1,4` | 0 |
  | 2 | `0x5fd` | el mismo | s − 0,3 | 20° |
  | 3 | `0x5ac` `flare01` | (0, p − 100, p), p nuevo | `(rand() % 10) · 0,05 + 1` + 0,3 | 0 |
  | 4 | `0x5fd` | (0, p + 100, p), p = `(rand() % 40) · 0,05 − 1` | s = `(rand() % 8) · 0,05 + 0,4` | 0 |
  | 5 | `0x5fd` | el mismo | s − 0,2 | 90° |

  Los tres grandes van en el tridente del `HDK_Mace` y los dos chicos en el pomo (*verificado* en el
  archivo: la y de −130 a −30 es el tridente y la de 70 a 110, el pomo). Parpadean porque la posición
  y la escala se sortean en cada frame (6 `rand()`, más el `rand() % 30` que `0x509340` saca al
  empezar con cualquier parte, `0x509a92`). **Verificado** en la CL7.6 B1 contra el código real
  emulado, el caso desde `0x50a169` hasta su `ret` con `0x4ec110` y `0x5491e0` reales, y la escala
  (`+0x68`) y el origen (`+0x6c`) del cuerpo del modelo del ítem puestos por el generador:
  `0x4ec110` es el `TransformPosition` de ese modelo, VectorTransform (`0x5fccc0`) por esa escala más
  ese origen. Cada cuenta va en 53 bits y se guarda en float donde el binario guarda: p, la escala y
  la y del desplazamiento; en la posición, el producto de la x por la escala no se guarda antes de
  sumarle el origen, y los de y y z sí. La escala del segundo y del quinto se resta al pedirlos, y la
  del tercero suma su 0,3 ahí. El `rand() % 8` del pomo va con el idiom de `and 0x80000007`, que con
  un `rand()` no negativo es lo mismo. *Estático*, lo que pone esos dos campos: la escala es la del
  personaje (`+0xc` de su objeto, `0x509397`) y el origen, desde una mano, la posición del personaje
  más el desplazamiento girado por el hueso y por el `+0x68` del modelo **del personaje**
  (`0x4ec190`, con `Models[objeto + 2]`, y `0x50966b`; el de las manos es 0), y por el camino del
  carcaj, la posición (`0x509607`); `0x60b3d0`, que dibuja el ítem antes del caso, no los toca.
  `0x4ec190` copia el hueso sin cambios a la raíz de la pose (`0x68c4a1c`) y deja el desplazamiento
  solo en el origen: el hueso 0 que lee el caso es el del ítem colgado de ese hueso.
- **El blanco del casco**: la tela 0 de la clase 4, aditiva, más sus **30 destellos** (`0x455930`):
  con t = sin(T · 0,001) · 0,1 + 0,4, para k = 0..29 y p = k div 5, el sprite `0x5ac` (`flare01`) en
  la partícula k, con la escala p · 0,2 + 0,4 y la luz (6 − p) · t · 0,2, aditivo. Es lo que arma
  `CharacterCloth::Flares()` ([La tela de la clase 4](#la-tela-de-la-clase-4)); ahora está leído cómo
  se dibuja.
- **Las nubes oscuras** (sin confirmar en el video): los dos objetos `0x54` del mapa 55, invisibles,
  en (10.811; 14.804; 219) y (10.598; 16.408; −95), en cada frame dibujado en que se ven, con
  `rand() % 5 == 0`, crean la partícula `0x622` (`Effect\clouds.jpg`, subtipo 10): vida 500,
  posición por eje `rand() % 400 − 200`, escala la del objeto más
  `(rand() % 15 + 10) · float32(0x3d888889)` (aproximación a 1/15; no `0,0667`),
  velocidad (v, v, 0) con v = `(rand() % 10 + 5) · 0,12`, que se suma **dos veces** por paso
  (Q-C129). La luz sube 0,01 por paso hasta 0,15 cerca de donde nació y baja más allá de 300; a más
  de 500 se apaga. Se dibujan con **`GL_ZERO, GL_ONE_MINUS_SRC_COLOR`** y una rotación de ±30° por
  segundo: oscurecen lo de atrás.
  **Precisión del contrato estático (2026-11-06):** se contrastaron con el PE `c32bbd5f`
  los caminos de `0x602ed0`, `0x53fe70`, `0x5446e0` y `0x548550`, incluidas sus tablas de saltos.
  El guard de emisión es **mapa 55 y tipo `0x54`**, no una condición interna de escena 2.
  Al azar común de cada objeto visto se suma uno para el guard; si pasa y hay lugar libre,
  el creador consume otros cinco, en orden X, Y, Z, escala, velocidad. Con los 3.000 lugares
  ocupados conserva el estado de la tabla y no consume esos cinco. La creación copia la
  posición perturbada como origen y pone RGB en cero. No inicializa `+0x60`; conserva el
  valor del uso anterior del lugar. El paso escribe `1,5f` en ese campo y suma la velocidad
  dos veces con ángulos cero: una en el movimiento común y otra en el caso específico.
  A distancia 3D `≤ 300`, RGB suma `float32(0x3c23d70a)`; cuando el R intermedio alcanza
  `float32(0x3e19999a)`, los tres canales toman ese mismo valor. Entre 300 y 500 inclusive,
  cada canal se multiplica por `float32(0x3f73cf3e)`; por encima de 500 se apaga sin ese fade.
  Vencer la vida apaga la partícula pero no evita el resto del último paso. La rotación se
  **escribe al dibujar**: `float32(T · campo60 · (índice par ? +0,02f : −0,02f) + origenY)`.
  El índice es el lugar real del pool, incluidos los inactivos; `origenY` es el mismo campo
  `+0x70`, no un ángulo independiente. No se acumula ni normaliza. La mezcla sustractiva no
  lee el alpha `+0x38` en este subtipo. No equivale a activar `GL_FOG`: el login lo apaga.
  La correspondencia visual con la neblina observada por Emanuel sigue necesitando una
  captura nativa; esta lectura estática no la sustituye.
- **El brillo del farol**: `0x602ed0`, en la escena 2, tiene un caso para el tipo `0x3c` (60, el farol
  del castillo, que se ve; `0x602f01..0x602fc6`): en cada frame pide **2 sprites** `0x5ac` (`flare01`)
  de escala 1, con la mezcla 0 (aditiva) y el color (L; 0,5 · L; 0,2 · L), con L = `(rand() % 30 + 70)
  · 0,01`, en las traslaciones de los huesos 8 y 9 del buffer global de la pose (`0x68b199c` y
  `0x68b19cc`). `Object61.bmd` tiene un solo hueso: esas matrices son las que dejó el último modelo
  posado antes (Q-C140). El `rand()` de L (`0x602ef4`) se saca **por cada objeto** que llega a
  `0x602ed0`, antes de mirar la escena o el tipo: todos los vistos, aunque `0x6002e0` no los dibuje.
  `0x4ec110` va sin trasladar (el quinto argumento en 0): solo `0x5fccc0` del (0, 0, 0) con la
  matriz, su traslación, que ya está en el mundo; el modelo del farol en `ecx` no se usa. **Quién deja
  los huesos 8 y 9** (CL7.6 B4): `0x5ffdf0` posa en el buffer global cada objeto con el alpha desde
  0,01 (`0x5fff7a`; con menos sale antes, `0x5ffe3d`), también los de `+0x58` en −2, que `0x6002e0`
  no dibuja. De los 276 objetos del `.obj` (todos dentro de la grilla: los 44 de y entre −324 y 0 caen
  en el sector 0, porque `ftol` trunca hacia 0) ninguno llega al 8: los que más tienen, los tipos 12 y
  18, tienen 8; el logo tiene 2. Pero antes que todos ellos, la carga del mapa 55 (`0x61a560` →
  `0x616ec0`, `0x6188ae..0x61891b`) carga `Data\Object56\Dragon` y crea el **dueño del dragón**,
  `0x606660(0xb4, (5.600; 23.000; 0), (0, 0, 0), 1)`, en el sector (3, 14), antes que el farol (5, 14):
  no se dibuja, pero se posa (emulado: llega a `0x4eb860` con el buffer `0x68b181c` y el octavo
  argumento en 1), con la animación de siempre (0,16 por paso). En `Dragon.bmd` (93 huesos) el 8 y el 9
  son `Bip01 L Toe0` y `Bip01 L Toe01`: el brillo del farol sale **en los dedos del pie del dragón
  del dueño, el que no se dibuja**, a unos 120 de altura sobre (5.640; 23.130), donde el terreno está entre 196 y
  207: queda bajo tierra y no se ve (el caballo, que se posa antes, también tiene el 8 y el 9, pero el
  dueño los pisa). MuRet crea el dueño en `m_objects` desde la CL7.6 B4, antes de los del `.obj`, y
  dibuja así el brillo (`ObjectFrameEffects` en `DrawWorldObjects`, con el `rand()` de la CRT de la
  tela).
- **El fuego del `0x59`** (sin confirmar): el objeto `0x59` en (8.954; 24.178; 492), también
  invisible, con `ftol(T) % 8000 > 7949` crea el efecto `0xc2` (`Skill\Fire01.bmd`, subtipo 9) a
  ± 1.024 en x y en y (`rand() & 2047 − 1024`) y a 3.000–3.600 de altura: una bola de fuego que cae, lejos a la derecha. Su paso y su dibujo no se
  leyeron.

**Lo que no corre en el login:** la `chasellight` (`0x53fe70(0x729)`) es solo de la selección; el
juego completo pide nivel ≥ 9 en las cinco partes y el juego 29 va a +7; no hay alas; el brillo por
nivel son pases de malla que MuRet ya tiene; los demás manejadores de `0x602ed0`, fuera del farol, el
`0x54` y el `0x59`, son de otros mapas.

**Los `rand()` del frame** (P-C22 a), además de los 75 de la tela: las brasas (unos 240 por paso, y
10 por cada una que renace), los efectos con modelo (uno por cada efecto vivo y por paso, `0x5208c5`:
el dragón), el aliento (1 o 3 por paso), el viento de las partículas (2 por paso), el dibujo de
cada ayudante (`rand() % 30` en `0x445336`, por frame), el
cuervo (4 a 7 por paso), `0x509340` (uno por cada parte del personaje que dibuja, por frame), el cetro
(6 más por frame), el gesto del login (`rand() % 6` con `ftol(T) % 2300 < 100`), `0x602ed0` (1 por
objeto visible por frame, que es también el del farol; 1 más en cada `0x54`, 5 más por nube y 3 más
cuando dispara el `0x59`), `0x508f00` (8 por personaje vivo por paso), `0x635ae0` (8 por
frame) y, por personaje visto y por paso (cierre del login): `0x508ba0` (8, `0x508c75` y `0x508ce9`),
`0x4fd5f0` (1, `0x4fd7e4`, sin condición), `0x432bd0` (1, `0x432bf1`, desde `0x4fcee0` con el frame en 3 o
más), las celdas de `0x505de0` (8, `0x506362` y `0x506833`), la cabeza (1, y 2 más cuando sortea) y
`rand() & 7` (`0x506cac`). También las rutas de la cámara (2 cada 128 pasos de la fase 1, `0x633a21` y
`0x633a38`) y el contador protegido (8 por paso y 4 más cada 313, Q-C125). Ahora se sabe quién saca cada
uno. MuRet lleva, con el de la CRT de la tela, los de la tela, el cetro (una vez por frame del original
desde el cierre), el aliento, el viento de las partículas, las brasas, el cuervo, el gesto, la cabeza, 4
de los 8 del contador y, desde la CL7.6 B4, el de cada objeto de `0x602ed0`; no lleva los demás, y los
del contador y el viento los saca al principio del paso y no después de la escena, así que su `rand()` no
sigue al del original bit a bit: habría que correrlos todos, en su orden.

### Lo demás del login

El resto de `0x634c60` (por paso) y de `0x6350b0` (por frame), llamada por llamada (*estático*):

- **Lo que falta y se ve o se oye:**
  - **el área de mensajes** (`0x5554e0`, abajo): los renglones que deja elegir un servidor (P-C27 a);
  - **las nubes y el fuego** del mapa (los `0x54` y el `0x59`, P-C23 e);
  - **la música**: la entrada llama a `0x4acfd0("data\music\main_theme.mp3", 0)`, que con la música
    prendida y si no es la que ya suena la toca en bucle (*inferido* el bucle). El archivo está en
    `Client/assets/Data/Music/`;
  - **el clic** `Sound\iButtonClick.wav` (`0x19`) en Esc y en los botones de las ventanas;
  - **Esc**, con la lista y la barra abiertas y lo demás cerrado: el clic y la ventana de `+0x3d0`;
    y la de créditos (`+0x4578`). Las dos, sin leer.
- **El cursor, desde el cierre** (`0x5ac2d0`): el original esconde el de Windows (`ShowCursor(FALSE)` en
  WinMain, `0x4ba238`, y en el WndProc, `0x4b3327`) y dibuja el suyo al final de la UI de cada escena (el
  login, `0x635560`; la selección, `0x634980`, que antes de la lista sale sin llegar ahí; el juego,
  `0x6376d7`): `0x620a00(1)`, `glColor3f(1, 1, 1)` y `0x621b60(slot, x − 2, y − 2, 24, 24, 0, 0, 1, 1, 1, 1,
  0)`, escalado desde 640 × 480 y con alfa 0. La x es `0x7d3682c` y la y `0x7d36828`, al revés de lo que
  sugieren las direcciones: el WndProc (`0x4b358f`) las escribe como `LOWORD · 640 / W` y `HIWORD · 480 / H`,
  sin signo y sin acotar (Q-C47), así que en 1024 × 768 el cursor salta de a 1,6 píxeles. El slot es el 2
  (`Interface\Cursor.tga`), o el 3 (`CursorPush.tga`) con el botón izquierdo abajo (`0x7d36874`, que en el
  login y en la selección solo escriben el `WM_LBUTTONDOWN` y el `WM_LBUTTONUP`: ninguno de los que lo
  borran se alcanza desde sus pasos ni sus dibujos); los demás (4 a `0xa`, `0x65a`, `0x68d`) los eligen
  globales del juego (`0x6c8e28` a `0x6c8e34`, `0x78abb0c`, `0x78abb20`, `0x7d3a10c` y `0x77f3b5c`), en −1 o
  en 0 fuera de él, y con `0x6c8e30` distinto de −1 el `+0x3e2` del héroe, `0x558bb0` y `0x775857c`. Con
  `0x6c8e2c`, `0x6c8e34` y `0x6c8e30` en −1, como en el login y la selección, `0x5ac61c` pregunta además
  `0x659290(this = [0x576352c], 0x200)`, que con true elige el `0x65a`: en la traza de la selección da false
  (el último quad es el slot 2); en el login no está verificado (P-C27 h). Los sube `0x61ccb0` en su avance 0 con `GL_LINEAR` y `GL_CLAMP`. Verificado en la traza
  de la selección: el último quad del frame es el slot 2 en blanco, de 38,4 × 38,4 en 1024 × 768. MuRet:
  `UI::DrawGameCursor` en el login y la selección, con el input del contexto, y el cursor del sistema
  escondido (`Platform::Window::HideSystemCursor`); en la selección sin la lista, también encima del aviso
  que MuRet dibuja ahí (`CharacterSelectSceneTest.TheNoticeBeforeTheListHasTheGameCursor`). El juego lo dibuja con su UI (CL8).
- **La cabeza del Dark Lord, desde el cierre:** `0x505de0`, en cada paso de `0x508ea0` (después de `0x4fd5f0`, el cuerpo y
  el cuervo), con el personaje visto (`+0x16c`, que `0x508ea0` escribe al empezar con `0x5fba90` en su lugar por 0,01f y
  −20) y sin las banderas `0x20` ni `0x1000` de `+0x78`. Si no es el héroe (`0x743a170`) ni se está muriendo (`+0x389`), con
  `rand() % 32` en 0 sortea el blanco: `+0x34` = `rand() % 128` − 64 y `+0x38` = `rand() % 32` − 16, con 360 más si da
  negativo. Después `+0x28` y `+0x2c` giran hacia el blanco con `0x4e9560`, a lo sumo `f(0x4e96f0(cabeza, blanco, 1) · 0,2f)`.
  La pose (`0x4eb860`) le resta `+0x28` al ángulo x del hueso de la cabeza y `+0x2c` al z. En promedio cada 1,28 s el casco
  gira hasta ±64° y se inclina entre −16° y 15°, y lo siguen las dos telas del casco y sus 30 destellos. MuRet:
  `Scenes::StepCharacterHead`, bit a bit contra el código real emulado (`tests/vectors/world55/head.txt`, 3.400 pasos), y la
  cabeza del paso en el dibujo: el original no la interpola. Desde CL7 hito 1 la corre también `Scenes::StepCharacters`
  con cada `0x20a` (`ActorState::headTurn`), en el juego y en el login, que lo llama en `0x634d13` (P-C56,
  [§ Los efectos de los personajes](#los-efectos-de-los-personajes-0x505de0)).
- **La cámara, confirmada:** `0x635ae0` es fija en la escena 2. El único corrimiento posible es el
  del bit `0x80` del atributo de la celda del héroe, y no aplica: el héroe tiene las celdas en
  (0, 0), porque en el login solo se le escribe la posición, y esa celda de `EncTerrain56.att` vale
  `0x0c`. El fundido no dibuja nada, y su color no le llega a nadie (Q-C66, *estático* desde el cierre).
- **No muestran nada en el login:** `0x5f7bc0` (el ítem bajo el mouse), `0x554810` (los avisos),
  `0x5b0550` (la ayuda), `0x40eca0` (un cartel de diálogo), `0x633110` (modelos 3D de la UI) y `0x5bb8d0`
  (el ancho del mundo con las ventanas: el entero).
- **El área de mensajes sí** (`0x5554e0`, que `0x6350b0` llama en la escena 2, `0x63552f..0x63553f`; antes
  este documento decía que no mostraba nada): seis renglones de la tabla de 120 de `0x77d9868`, desde el
  `0x6c8eb4` (`0x72` en la `.data`, los seis últimos), arriba a la izquierda en y = 0 a 65 de a 13,
  salteando los vacíos (`0x555541`). Las 120 llamadas de la entrada (`0x634b4a`) la dejan vacía; el clic en
  un servidor agrega el 470 y el 471 (`0x64630f` y `0x646320`, del tipo 1: el texto `0xffff9664` sobre
  `0x96000000`), el cancelar de la cuenta el 472 y el 473 (`0x6427d9` y `0x6427ea`) y la respuesta al
  `F4 03` el 481; `0x5554b0` suma un renglón vacío cada 150 pasos desde el último, así que suben y se van.
  MuRet no la tiene (*estático*; P-C27).

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
| `+0x3d0` | `0x6baa0c` (`0x6477b0`, arma `0x6478c0`) | la que abre Esc (`0x634c60`, con la lista y la barra de abajo abiertas, `+0x10f4` y `+0xe4c`, y las demás cerradas) | sin leer |
| `+0x7e8` | `0x6ba988` (`0x643fd0`, arma `0x644120`) | sin identificar | sin leer |
| `+0xe40` | `0x6ba8c8` (`0x640ef0`, arma `0x641000`) | la barra de abajo, con dos botones, el menú y los créditos (abajo) | botones de 54 × 30 (`0x10`, `0x11`) y un adorno de 189 × 103 (`0x12`); ancho W − 60, en x = 30 e y = `−11 − (int) (H · −0,945) − h` |
| `+0x10e8` | `0x6ba9c0` (`0x644fd0`, arma `0x6451d0`) | la lista de servidores (abajo) | 21 botones de grupo de 108 × 26 (`0xc`), 16 de servidor de 193 × 26 (`0xd`) con una barra de 160 × 4 (`0xe`), aunque un grupo puede tener 20 (Q-C63), el marco en `0x17` a `0x19`; centrada |
| `+0x4228` | `0x6ba900` (`0x641490`, arma `0x6415c0`) | la cuenta y la clave (abajo) | 329 × 245 (`0x13`), centrada en x y a dos tercios en y: `((W − w) / 2, (H − h) · 2 / 3)` |
| `+0x4578` | `0x6ba848` (`0x63ef00`, arma `0x63f020`) | la de los créditos (*inferido*: la abre el botón de los créditos de la barra); abierta, tapa el mundo (paso 4 del dibujo) y no deja abrir la lista (`0x4c1860`); con su propia fuente (`0x63f2f2` lee el charset) | sin leer |

Además, `+0x7f50` (vtable `0x6ba810`) es la de la selección de personaje, cuyo botón pasa a la
carga del mundo (`0x633c10`, 02); `+0x8498` (`0x6ba7d4`) es la ventana de crear: la abre el botón
de crear de la barra, y los avisos `0x36` y `0x37` la vuelven a abrir (§ La ventana de crear).
Un botón marca el clic en `+0xaa` (`0x63c490` lo pone en 1, en `0x63c4fc`).

**Cuál va arriba** (*estático*). Las ventanas viven en una lista doble del administrador (la cabeza
en `+0x8ddc` y la cola en `+0x8de0`): `0x65f300` las suma con `0x644b60`, que pone cada una en la
cabeza, y `0x65fe00` las dibuja desde la cola hasta la cabeza con `0x644cd0`, así que la cabeza va
arriba. Abrir una (`0x65f690`, que llama a `0x65f610`) la saca y la vuelve a poner en la cabeza, y
cerrarla (`0x65f6c0`) la pasa a la cola con `0x644b90`: **la última que se abrió va arriba**.

El mouse sigue el mismo orden. En cada frame, `0x65fc30` les pone `+0xa9` en 0 a los botones de
todas las ventanas (`0x669ed0`) y en 1 a los de la primera, desde la cabeza, que tiene el mouse
adentro de su rectángulo (`vtable + 0xc` con 0, que es `0x669df0`), aunque no dibuje nada ahí; un
botón con `+0xa9` en 0 nunca está debajo del mouse (`0x63c470`). En el frame en que se aprieta el
botón izquierdo (el `+0x1c` de `0x640940`), esa ventana pasa además a la cabeza con `0x65f610`, que
le pone `+0xd` (`vtable + 0x14` con 1) y se lo saca a la que estaba. Y `0x669f40`, que atiende cada
ventana abierta, corre su `+0x30`, la lógica que mira los clics, solo con `+0xd`: atiende sus clics
la de la cabeza. El aviso es de toda la pantalla: `0x6429b0` lo arma de W × H
(`0x669c70(W, H, −1, 0)`, el negro de atrás) y su `vtable + 8` (`0x642bc0`) mueve solo la caja, así
que su rectángulo queda en (0, 0) y, mientras está en la cabeza, ninguna otra ventana tiene el
mouse. El arrastre (`0x65f720`) y el resto de `0x65fc30` quedan para la CL8 (P-C08).

**La barra de abajo** (`+0xe40`; *estático*). `0x641000` arma dos botones de 54 × 30 con tres
cuadros (`0x63c330(54, 30, slot, 3, 2, 1, …)`, como los de servidor: el 0 normal, el 1 con el
mouse encima y el 2 apretado), el del menú con `server_menu_b_all` (`0x10`) y el de los créditos
con `server_credit_b_all` (`0x11`), la ventana sin fondo (`0x669c70(W − 60, 30, −2, 0)`, con el W
del momento en que se arma) y una imagen con `deco.tga` (`0x12`, `0x647050(189, 103, 0x12, …)`)
que lleva en `+0x58` y `+0x5c` el punto (105, 59): `0x6472e0(x, y)` la ubica con ese punto en
(x, y), es decir, con la esquina en (x − 105, y − 59). `0x6410c0` (`vtable + 8`) pone el menú en la
esquina de la barra, los créditos contra su borde derecho (x + ancho − 54) y el adorno con su punto
en la esquina de los créditos: el botón queda adentro del adorno, a 105 de su izquierda y a 59 de
su borde de arriba. `0x65f300` ubica la barra en x = 30 y en y = `−11 − (int) (H · −0,945) − 30`:
la constante es el float `0xbf71eb85` (`0x6baae0`), un poco más chico que 0,945 en valor absoluto,
`fmul` multiplica el alto (`fild`) por él en la precisión de la x87, y `_ftol` trunca hacia el 0;
con 600 da 525 (con 0,945 exacto, o con el producto redondeado a float, daría 526). El dibujo
(`0x641440`) es el adorno y después los botones (`0x66a090`). La abre `0x4c1860` con la lista,
después de ella: va arriba de la lista. Apretar en la lista la pasa arriba de la barra, hasta que
el `F4 06` que pide el clic en un grupo (Q-C82) vuelve a abrir las dos. Fuera de `0x4c1860`, de
`0x65f300`, del constructor y el destructor del administrador y de `0x634c60`, que mira si está
abierta (`+0xe4c`) antes de abrir la de Esc, ninguna función nombra la barra, y ninguna la cierra:
en el login sigue abierta con la cuenta y con los avisos. En 640 × 480 su rectángulo (de 30 a 610
en x y de 412 a 442 en y) pisa la parte de abajo del botón del grupo 20 de la lista, que va de 409
a 435 (Q-C84).

Su lógica (`0x6411b0`, `vtable + 0x30`) mira los dos clics (`+0xea` y `+0x1ca`, el `+0xaa` de
cada botón). El del menú abre la ventana de Esc (`0x65f690` con `+0x3d0`). El de los créditos manda
al CS `C1 04 F4 06` en claro, sin el motor (como el `F4 02` de `0x4c6070`, 04), con el mismo
`send` y la misma cola de `WSAEWOULDBLOCK` que el clic en un grupo, abre la ventana de `+0x4578`
y llama a `0x4acf60` con `main_theme.mp3` y a `0x4acfd0` con `Mutheme.mp3`, que es la música del
login (*inferido*: deja una y pone la otra). Qué contesta el CS a ese `F4 06`, *a confirmar*.

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

**El marco y la descripción** (*estático*: `0x6451d0`, `0x645520`, `0x6457c0`, `0x645860`,
`0x645ba0`, `0x645c70`, `0x645ca0` y `0x6463b0`). La ventana no tiene fondo (`0x669c70` con -2). Lo
que lleva alrededor de los botones son cuatro imágenes de `server_deco_all` (`0xf`, de 159 × 95),
cada una con su lugar en la textura (`0x647260`, que le da a `0x647050` un cuadro con ese origen) y
con un punto propio en `+0x58` y `+0x5c`: `0x6472e0(x, y)` pone ese punto en (x, y), es decir, la
esquina de la imagen en (x − px, y − py).

| Imagen | En la textura | Su punto | Dónde va el punto | Se ve |
| --- | --- | --- | --- | --- |
| `+0x2da0` | (0, 0), 68 × 95 | (8, 19) | la esquina de arriba a la izquierda del botón de grupo 0 | con la lista abierta |
| `+0x2e48` | (68, 0), 68 × 95 | (60, 19) | la de arriba a la derecha del botón 10 | con más de 10 botones de grupo (`+0x3134`) |
| `+0x2ef0` | (136, 0), 23 × 29 | (1, 2) | la de arriba a la derecha del botón elegido | con el elegido de 0 a 9 |
| `+0x2f98` | (136, 30), 23 × 29 | (23, 2) | la de arriba a la izquierda del botón elegido | con el elegido de 10 a 19 |

Con el 20 elegido, o sin elegido, no hay marca. Las dos primeras quedan detrás de las primeras
filas de cada columna, y las otras dos, al lado del botón elegido, del lado de los servidores.

La caja de la descripción (`+0x3040`) es otra clase (vtable `0x6bab24`, constructor `0x66a120`):
un marco de cinco imágenes que arma `0x66a1c0` y que dibuja `0x66a960` en este orden, todas
ubicadas desde la esquina de la caja (`0x66a3d0`) y con los altos de `0x66a4e0`:

| Imagen | Textura, cuadro | Dónde | Tamaño |
| --- | --- | --- | --- |
| el fondo | `server_ex03` (`0x17`), (0, 0) | (3, 3) | 506 × (alto − 6), repetida en u y en v |
| arriba | `server_ex01` (`0x18`), (0, 0) | (0, 0) | 512 × 6 |
| abajo | `server_ex01`, (0, 6) | (0, 6 + 10 · th) | 512 × 6 |
| la izquierda | `server_ex02` (`0x19`), (0, 0) | (0, 6) | 3 × 10 · th, repetida en v |
| la derecha | `server_ex02`, (3, 0) | (509, 6) | 3 × 10 · th, estirada |

El alto de la caja es 10 líneas de th, el alto del buffer de la textura de `0x19` (4 en el
laboratorio), más 6 y 6: 52. Las líneas son 10 porque `0x66a4e0` acota lo que se le pide a
[1, 10] y el `vtable + 0x1c(10)` de `0x6451d0` pide 10. El fondo y la izquierda van con la UV del
lado cambiado hasta el tamaño (`+0x98` en 1, `0x647370`), y como las dos texturas repiten, se
repiten; la derecha no, porque `0x647050` pone `+0x98` en 0 a toda imagen que tiene cuadros, y la
suya empieza en (3, 0): sus UV quedan en los 3 × 4 téxeles con que la arma `0x6451d0`, de (3, 0) a
(6, 4) sea cual sea el buffer, estirados al alto. La caja va centrada abajo de la ventana: en
(x − (512 − 465) / 2, y + alto de la ventana − 52), que es x − 23. Se abre con la lista solo si
hay un grupo elegido (`0x645ba0`).

El texto lo arma `0x645860`: vacía los dos renglones (`+0x308c` y `+0x30df`, de 83 bytes cada uno)
y, con un elegido de 0 a 20, parte el texto `1717 + elegido` con `0x632c90(texto, renglones, 2,
83)`. `0x632c90` copia carácter por carácter (el largo de cada uno es el de `_mbclen`,
`0x6a01fa`, con la tabla de la página de códigos del CRT: en una de un byte, todos miden 1,
*inferido*) y, cuando el que sigue haría llegar el renglón a 83 bytes, lo corta: si hubo un
espacio en ese renglón a menos de 10 bytes (el menor de 83 / 2 y 10) del carácter que no entró,
vuelve hasta después de ese espacio, que queda al final del renglón, y sigue desde ahí; si no,
corta justo antes del carácter. Con el último renglón lleno, lo que sigue no va. Al volver al
espacio no vuelve a medir: copia el largo del carácter que no entró, que en una página de dos
bytes puede no ser el del que copia.

El dibujo, `0x6463b0` (`vtable + 0x34`), va así:

1. la caja (`vtable + 0x18` de `+0x3040`: si está abierta, las cinco imágenes);
2. la fuente `DAT_05763560`, el fondo del texto en 0 y el texto en blanco, y para cada renglón i
   (0 y 1): la marca de la columna i (`+0x2da0` o `+0x2e48`), la del elegido de esa columna
   (`+0x2ef0` o `+0x2f98`) y el renglón con `0x5542f0` en (x + 11, y + 12 + 20 i) de la caja, en
   píxeles pasados a 640 × 480 (`_ftol` de dividir por la proporción); un renglón vacío no se
   dibuja;
3. los botones con sus rótulos (`0x66a090`) y después las 16 barras de la carga (`0x6407e0`);
4. si el nombre del grupo elegido es el texto 1829, el del grupo 20 (`strcmp` con `0x7743138`,
   en el lugar `4 + elegido`; sin elegido, el lugar 3, que no tiene nombre), los textos 565, 566 y
   567 con `0x554180` en (90, 104), (90, 119) y (90, 134) de 640 × 480, con la misma fuente y el
   color del último rótulo que se dibujó (Q-C83).

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
  0x8001`) mandan el login, con el sonido `0x19` en el caso de Enter; cancelar (`+0x31a`) o Esc
  (con el sonido) llaman a `0x642810`, que cierra el GS, pone el estado en 0, vuelve a conectar al
  CS y cierra la ventana: el CS manda el `00`, y vuelve la lista. Antes de Enter y de Esc mira el
  byte `+0x31` del mouse (`0x640850`), como el aviso (`0x642e30`), pero solo lo escribe su init, en
  0 (`0x640926`): ninguna de las 35 funciones que piden el mouse lo cambia, así que valen siempre,
  también con un botón abajo (*estático*).
- **El dibujo** (`0x641990`): el fondo, los dos campos, el control (`0x469350`), los textos 450 y
  451 a la izquierda de cada campo (x + 30, y + 113 y y + 139) con la fuente `DAT_05763560`, en
  blanco y sin fondo, y el título, el texto 460 o 461 (según `0x447c70(grupo, número)`) con el
  nombre del grupo y el número, en (x + 111, y + 80). Las posiciones en píxeles pasan a 640 × 480
  dividiendo por la proporción y truncando (`ftol`), y `0x554000` las vuelve a escalar.
- **El caret del control activo** (barrido estático del PE `c32bbd5f`, 2026-11-06):
  `0x6419c2` consulta el selector `0x6c7b8c`, inicializado en 1 en este PE. En ese modo llama
  a `0x469350`, cuyos dos dispatch virtuales llegan a `0x468320`, uno por campo. El literal
  `_` de `0x553ae0` pertenece al camino legacy del selector 0; no corresponde añadirlo al
  texto del login activo. Los 1.613 bytes de `0x468320` se cotejaron con el PE sin diferencias.
  El control activo sí compone un caret rectangular sobre su bitmap: `GetFocus() == hwnd`
  y el resto **signed** de su contador por 24 menor que 12. El contador avanza por llamada
  con HWND válido y visible, también sin foco; no usa un temporizador. Con IME desactivado,
  el ancho es `1,5f`: los límites X estrictos dejan una columna en `caret.x + 1`; Y va desde
  `caret.y` inclusive hasta `caret.y + altura` exclusivo. La altura se mide con `Q` y queda
  cacheada. No es un glifo ni forma parte de la cuenta o la clave. Colores y fuentes efectivos,
  recorte y composición del EDIT requieren sus contratos de configuración; retirar el `_`
  artificial de MuRet no completa por sí solo la reproducción de ese bitmap.
- **El emisor** (`0x641bf0`): solo con `DAT_057678c0` distinto de 0. Cierra la ventana de arriba,
  copia los campos a `0x7798bf8` (cuenta) y `0x7798cf8` (clave) con un tope de `0xff` (`0x4693e0`);
  con la cuenta vacía abre el aviso `0x64` y con la clave vacía el `0x65`; con el estado en 2 copia
  la cuenta a `DAT_0576792c`, pone `DAT_05767928` en 1 y el estado en `0x13` y arma el `F1 01`
  (Q-C43). El resto de la función llama a la tabla de hash de `0x40b240` ([La tabla de
  protección](#la-tabla-de-protección)): puede ser el anticheat, y no se siguió.

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

## La selección de personaje

Leída en la CL6.R en el desensamblado de `c32bbd5f`: todo *estático* salvo lo que se marca, y lo
**verificado** sale de la traza de la selección (§ Modelos y objetos). Los textos van por su
índice en `Text.bmd`. Los paquetes, campo por campo, están en
[04](04-protocolo.md#la-selección-de-personaje-f3), y quién pasa de una escena a otra, en
[02](02-arranque-y-ciclo-de-vida.md#las-cinco-escenas-y-sus-transiciones).

### Al entrar

`0x6342c0`, la lógica de la escena 4, no hace nada mientras el estado de la conexión
(`DAT_057678c0`) esté antes de `0x33` (la lista), y la primera vez llama a `0x633f20`:

- `0x7d3a101` en 1, `0x775857c` y `0x7d3a10c` en 0, y el mapa 54 (`0x61a560(0x36)`, `World55`);
- `0x61ca30`: las texturas de la escena (las de `Interface\` en `0xc` a `0x13`, `character_back01`
  y `02` en `0x717` y `0x718`, `Impack03` en `0x719` y `chasellight` en `0x729`) y, desde
  `0x60d530(0x6f8)`, los modelos `0xa4` (`Logo05`) y `0xa8` a `0xac` (`NewFace01` a `05`) con las
  suyas, y la velocidad 0,3 en las acciones 0 y 1 de los `NewFace`;
- `0x511290(0x7437050, 0xa9, 0, 0, 0)`: un objeto de personaje suelto, de `0x438` bytes y fuera
  de los 400 de la tabla, con el modelo `0xa9` (`NewFace02`), la clase en 1 (`+0x1d4`) y `+0x88`
  en 0. Es la vista previa de la ventana de crear: el `F3 01` lee de ahí la clase y la piel
  (`0x7437224` y `0x7437225`);
- ninguno elegido (`DAT_006d5edc` en -1) y la UI en el modo 4 (`0x65f4b0`): el aviso de `+0x8`
  (352 × 113, al centro), las ventanas de `+0x3d0` y `+0x7e8`, la de la selección (`+0x7f50`, en x
  22, abierta) y la de `+0x8498` (454 × 406, al centro); los botones, con `0x63eb40`;
- `0x633f96`..`0x63417a`: abre el registro propio (`[0x7681eb4]`, por la tabla de hash de `0x57630c8`), pone en
  0 `+0x6a` y `+0x6c..+0xab` de `[0x7681eb0]` y ocho dwords de `[0x7681eb4]` (`+0xb70`, de a `0x48`) y lo
  cierra: con la cuenta en 0, `0x40b4d0` y sus 3.848 `rand()` (Q-C238; [El paso de los personajes en la
  selección y el login](#el-paso-de-los-personajes-en-la-selección-y-el-login));
- vacía el área de mensajes (120 líneas, `0x555190`) y, si alguno de los dos números de `pkt[5]`
  de la lista (`0x5767958`, `0x576795c`) no es 0, escribe una línea con `sprintf` y el texto 1240
  **como formato** (`0x63420a`).

"La primera vez" es la bandera `0x7d3a13f`, que la vuelta desde el juego (el `F1 02` con 1) pone en
0: `0x633f20` corre otra vez, con la lista nueva ([La vuelta a la selección](#la-vuelta-a-la-selección)).

### La lógica

Cada paso, `0x6342c0` corre las olas (`0x5fbf60`), los objetos (`0x606560`), el caballo (`0x445230`), los
personajes (`0x508f00`, `0x6342f9`), la vista previa (`0x508ea0(0x7437050)`, `0x634303`), los efectos
(`0x52d220`), los joints (`0x53c670`), las partículas (`0x5446e0`) y las rutas de la cámara (`0x633920`), en
ese orden ([El paso de los personajes en la selección y el
login](#el-paso-de-los-personajes-en-la-selección-y-el-login)). Después:

- **Enter:** con `[input + 0x31]` en 0, `GetAsyncKeyState(VK_RETURN) == 0x8001`, las ventanas de
  `+0x14`, `+0x84a4`, `+0x3dc` y `+0x7f4` cerradas y uno elegido de 0 a 4: el sonido `0x19`
  (`0x41ed10`); con el bit `0x1` de su `+0x1d8`, el aviso `0x84` (el texto 434); si no, la tabla de
  hash (no se leyó) y copia al personaje propio (`[0x7681eb0]`) el nombre (`+0x1d9`, a `+0`), la
  clase (`+0x1d4`, a `+0xb`), la piel (`+0x1d5`, a `+0xc`) y el nivel (el `word` de `+0x1d6`, a
  `+0xe`); `0x61cc50` suelta los slots `0xc` a `0x1d` y `0x6f8` a `0x71d` y los modelos `0xa8` a
  `0xac` (no el `0xa4`), pone `0x7d3a13e` en 0 y pasa a la escena 3.
- **Esc:** con las mismas ventanas cerradas, el sonido y la ventana de `+0x3d0`.
- Con la ventana de `+0x8ddc` cerrada (`+0x8de8` en 0), el **doble clic** (`[input + 0x1f]`) con
  la ventana de la selección abierta (`+0x7f5c`) y un personaje bajo el mouse (`DAT_006c8e30`, de
  0 a 4) lo elige y hace lo de Enter **sin el sonido** (y el aviso `0x84` si está bloqueado); si
  no, el **clic** (`[input + 0x1c]`) elige el que está bajo el mouse, o ninguno (-1), y
  `0x63eb40` acomoda los botones: crear si hay un lugar vacío de 0 a 4, conectar y borrar si hay
  uno elegido.
- El doble clic es un reloj propio de `0x640940`, no `WM_LBUTTONDBLCLK`: el segundo
  `GetAsyncKeyState(VK_LBUTTON) == 0x8001` dentro de `GetDoubleClickTime()` (con el reloj de
  `0x459bc0`, `QueryPerformanceCounter` o `timeGetTime`, en el `double` de `+0x40`), con el
  primero pendiente en `+0x60`, prende `+0x1f`. Cualquier movimiento del mouse entre los dos
  borra `+0x60` (`0x6409c3`, Q-C99).
- Quién escribe `[input + 0x30]` y `[input + 0x31]` (`0x640850`), que cambian el botón y traban
  Enter, sin leer (P-C14).

### El picking

`0x620480(mx, my, salida, 1)` pasa el mouse de 640 × 480 a píxeles (· W / 640 y · H / 480) y arma
el punto `(dx · tx · d, −dy · ty · d, −d)` con d = `0x6d5d44` (el far de antes del × 1,4: 11.519,9
en la escena 4) y las tangentes de `0x6203c0`: tan(fov · π/360) por el viewport en x y, en y, además
por el alto de la pantalla sobre el del viewport (`0x62045c..0x62046c`: `DAT_006d5d64 /
DAT_07d36860`, 480/430 en la selección, Q-C109); lo gira con la inversa de la
vista (`0x5fcc30`) y le suma la posición de la cámara (`0x7d36834`, sacada de la `GL_MODELVIEW`
traspuesta que `0x620370` guarda en `0x7d366f0`): el punto lejano queda en `0x7d366c0`.
`0x59e700(máscara)` recorre los 400 personajes (vivos, visibles, con alpha > 0, `+0x389` en 0,
sin el bit `0x10000000` de `+0x7c` ni `+0x42c`, y la clase de `+0x88` en la máscara; en la escena
4 no saltea al propio) y prueba el **segmento** de la cámara a ese punto contra la caja de cada uno
con `0x622900`, una separación en 6 ejes (las 3 aristas de la caja y su producto con el rayo,
`0x6227b0`). Gana el de menor distancia² de la cámara a su origen (desde 10¹²). La caja es la de
`0x4ebc80` (§ El modelo en el binario): alineada a los ejes y sin girar, con las medidas de
`+0x124` y `+0x130`, que `0x511290` pone en (−60, −60, 0) y (40, 40, 120) para el `0x20a`
(`0x512270`, `0x5122d4`; Q-C100). Con `0x77f3b18` en 2 (nadie lo escribe) usaría la de los
vértices. `0x59ec80` llama con `0x22` y 1 (y 4 a `0x6c8e2c`). `0x59e700` pone además `+0xe8` en 1
y un tinte en `+0xf4`, según `+0x1d6` (de 0 a 4), en todos los de la máscara; en la escena 4 lo
pisa `0x634680` antes del dibujo siguiente.

### El dibujo

`0x634680` sale con `0x7d3a13f` en 0 o el estado antes de `0x33`, con `al` en 0: sin ese verdadero, `0x637700` no presenta el frame (`0x6385e1..0x6385f7`), así que el clear no se ve y en la pantalla queda el último frame del login hasta que llega la lista (MuRet retiene el frame hasta que un paso vio la lista, salvo con un aviso abierto: `HoldsTheLastFrameUntilAStepSawTheList`). La niebla en 0; los fondos
(`0x621b60(0x717, 0, 25, 320, 320, …)` y el `0x718` en x 320); `0x620db0(0, 25, 640, 430)`; el
rayo del mouse (`0x620480`); los cinco primeros con z = 163 y `+0xf4` en 0 y, con uno elegido, la
mezcla aditiva, su `+0xf4` en (1, 1, 1), `0x5f9780(x, y, &(1, 1, 1), 1, 0x7b4ccc4)` (una luz en el
terreno, en su lugar) y la opaca; los personajes (`0x510d00`, los 400); el picking (`0x59ec80`, con
`+0x8de8` en 0); y con uno elegido, su `+0xf4..+0xfc` en 0,15 (`0x6348a8`..`0x6348c4`), dos
`0x53fe70(0x729, posición, ángulos, &luz, 0 y después 1, 1,0, 0)` (la `chasellight` debajo del
elegido, con esa luz), y copia su nombre (`+0x1d9`) en un string del objeto de `0x6eed7c`
(`0x4129f0` es la asignación de un string, `thiscall`: no dibuja nada). El 0,15 no
llega al cuerpo: el frame siguiente lo vuelve a 0 o a 1 antes de `0x510d00`. Así, la luz de cada
personaje es 0,4 y la del elegido 1,4, y la del lugar 0 (el "héroe") 0,5 y 1,5, antes del factor
del nivel (§ El modelo en el binario).
`DAT_07d36899`, que saca el giro en x de `0x620db0` (`0x620e94`), no lo escribe nadie (no hay un
`mov` a esa dirección). **Verificado** en la traza (1024 × 768): la cámara y el viewport de
§ Cámara, los fondos de (0, 728) a (512, 216) y de (512, 728) a (1024, 216), antes del pase del
mundo la perspectiva de 45° que no dibuja y el pase de la franja de arriba, y los pies del
personaje a la altura de z = 163.

El contraste CL7.3 B precisa la escritura de Z: después de la cámara (`0x6346ca`),
`0x6347c3..0x6347e7` escribe los bits `43230000` (163) en `+0x18` de los **cinco
primeros slots**, sin comprobar vivo ni presencia de slot0. Sí se aplican las salidas
iniciales del renderer mientras se espera la lista. Hero apunta al slot0 desde
WinMain (`0x4bafdf`), por lo que esa escritura queda compartida al salir de selección.
Un LoginStep anterior al primer LoginRender la puede leer al crear brasas: no alcanza
con aplicar 163 únicamente al transform temporal del personaje dibujado. Esto fija
el estado después de un dibujo habilitado; no prueba la Z del constructor antes de él.

**El trapecio** (T11b del hito 1; *estático*). Antes de la cámara, `0x6346b5..0x6346c3` deja en un local el
punto (12.975; 15.423; 333), fijo, que no es ningún personaje. Después de los fondos, `0x634736..0x634743`
arma el pase de la franja (`0x620db0(0, 0, w, 48)` con el ancho de `0x5bb8d0`, que no dibuja) y `0x63475c`
llama a `0x5fbb00(w · [0x6ba070], &punto)`: el rango de bloques y, por `0x5fbeef`, el trapecio y el nivel
efectivo globales de `0x5fb570` ([El trapecio completo](#el-trapecio-completo)). Nada de
`0x634736..0x634769` saca `rand()`. El trapecio lo leen el pase del terreno de la selección (`0x5fc260(0)` en
`0x634844`: antes este documento decía que la selección no dibuja el terreno, y lo llama; [El paso de los
personajes en la selección y el login](#el-paso-de-los-personajes-en-la-selección-y-el-login)), el paso de sus
personajes (`0x508f00` en `0x6342f9`, que MuRet no corre: P-C50) y, después de la entrada al mundo, el
primer paso del juego (Q-C216). Con la cámara de § Cámara, en el mapa 54, sus esquinas en celdas son (29,5;
157,6), (126,4; 254,5), (179,1; 148,9) y (135,1; 104,9): con el margen −20 de los personajes pasan los
centros de 11.281 celdas, que en Lorencia tienen 2.014 de la zona segura (el oeste del pueblo, con (133, 127)
y (143, 120), no el borde del este en (172, 128)) y en Devias ninguna. MuRet lo calcula en cada frame del
original detrás de la cámara (`CharacterSelectScene::StepTheCameraAndTheTrapezoid`, con el punto en
`Scenes::SelectionCullingPoint`); un error de `0x5fb570` corta el frame y deja la escena en `Failure`
([ADR0009](../../../docs/adr/0009-errores-del-trapecio.md)). Tests:
`CharacterSelectSceneTest.EachFrameLeavesTheTrapezoidOfItsFixedPoint` y `ATrapezoidErrorCutsTheFrame`.

### El mundo de la selección

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; los tests son simulación del port, sin
emulación nueva).

**La carga.** `0x633f20` escribe `0x36` en `0x6c9dd0` (`0x633f44`) y llama a `0x61a560(0x36)`: el terreno de
`World55`, los 65.536 `rand()` del pasto (`0x5f8300`, Q-C146), los 192 objetos de `EncTerrain55.obj` (sin
`rand()`) y los modelos de `Object55` (`0x616ec0`, sin especiales para el 54: Q-C305). MuRet lo hace con
`LoadMap(54)` sobre el `WorldState` de la sesión (`SelectionState::InitializeSelection`), y la selección presta
ese terreno; el mapa queda en 54 aunque la carga falle, como en `0x633f44`. `Scenes::SelectionWorld` sube los
tiles con el pasto (`0x61a560`) y los modelos de `Object55` (`0x616ec0`) en el primer paso o dibujo que los pide.
Las texturas de efectos son las de la carga del juego (`0x61ccb0`, `GameModels`), incluida `Flame01` en `0x5de`
(`0x61d67f`), y `0x719`/`0x729` las de la escena (`0x61ca30`, `SceneTextures::MetadataOf`).

**El paso** (`0x6342c0`). Las olas (`0x5fbf60`) después de la copia de la luz; `0x606560` sobre los objetos
vistos en el último dibujo, que en el 33 y el 54 llega a `0x4250f0` por la cadena de `0x605a62` (`0x425070`):
la V del `0x19` y del `0x1c` baja 0,015 (`0x6b8a24`); el `0x1e` y el `0x47` sacan un `rand() % 5` (por 0,01,
que queda en la x87) y suman a `0x7b4ccc4`, con radio 2, la luz (b + 0,4; b + 0,6; b + 0,4) o (b + 0,9;
b + 0,2; b + 0,2); el `0x29` a medio alpha; el `0x38` a `0x3b`, el `0x3e`, el `0x3f`, el `0x43` y el `0x46` con
`+0x58` en −2; el `0x40` a 0,05 keys por paso; y, con cualquier tipo, el sonido `0x246` (`0x41ed10`). Después de
la vista previa, `0x52d220` (la mariposa) y `0x53c670`, y `0x5446e0` con los dueños de las neblinas
(`0x634315`).

**El dibujo** (`0x634680`, después de [El dibujo](#el-dibujo)): con el elegido vivo, su luz en 1 y
`0x5f9780(x, y, (1, 1, 1), 1)` entre `0x620a90` y `0x620980`; `0x5fc260(0)` (terreno, pasto y marcas);
`0x6046a0` con los casos de `0x602ed0` → `0x425260` (por `0x6044ed`, después del `rand()` común de
`0x602ef4`); `0x510d00`; `0x59ec80` con `+0x8de8` en 0; `0x4453b0`, `0x52e590` (cintas), `0x53c6a0(0)`
(juntas), `0x52d620(0)` (efectos con modelo) y `0x549510` (los sprites, vistos); con el elegido, la luz 0,15 y
las dos `0x729`; y el pase de ojo con la modelview en la identidad (`0x6213e0`): `0x549440(0)` (sprites),
`0x548550(0)` (partículas) y `0x548eb0(0)` (números), y `0x6213f0`. Los casos de `0x425260`:

- `0x1e` y `0x47`: dos `0x5c6` por hueso, del buffer global de la pose (`0x68b181c` + hueso · `0x30`, con
  `0x4ec110` sin trasladar), en los huesos 6, 7, 8, 12, 13 y 17 con sus desplazamientos; el primero con la
  escala del hueso (5,5 a 4 el azul, 6 a 5 el rojo) y luz (0,1; 0,1; 0,3) o (0,5; 0; 0) en el 6 y (0,25; 0; 0)
  en los demás; el segundo de escala 3 con 0,15 (el rojo, solo en R). Un hueso que nadie publicó lee la `.bss`
  en cero. El dueño del sprite no se guarda (Q-C303).
- `0x38`: con `rand() & 1` en 0, la cascada `6f0/2`; `0x39`: siempre, `6f2/4`; `0x3a`: con `rand() % 3` en 0,
  `6f1/2`; con luz blanca, la escala del objeto y sin dueño.
- `0x3b`, `0x3e` y `0x3f`: con `+0x58` distinto de −2, veinte `622/1` con el objeto de dueño y la luz del tipo
  (Q-C306).
- `0x3c`: con `timeGetTime() & 0x3ff` debajo de 10 (Q-C304), la mariposa `0xae` /3 con el objeto de dueño
  (`0x518cf0`; en la escena 4 sale oculta, `0x51913a`); siempre deja el objeto en −2.
- `0x43`: la luz del objeto (`+0xf4`) en 0,3 y la llama `5de/7` con esa luz.
- `0x46`: con `rand() % 5` en 1 y el reloj debajo de 30, el efecto `0xb3`; `0x29`, `0x41` y `0x42` vuelven a
  dibujar su malla 0 con `0x4ecc30` y la U del reloj. Son de Aida (ninguno en el 54): ver
  [Los objetos de Aida](#los-objetos-de-aida).

Las partículas (`World/SelectionParticleKernels.h`): la `622/1` (`0x5435cf`, siete `rand()`; paso `0x5476f0`:
el dueño visto le renueva la vida a 50 y, si muere sin verse, le vuelve `+0x58` a 0; la altura es el seno del
reloj del mundo por 20 y el giro sigue al reloj), la `5de/7` (`0x54150e`, cinco o seis `rand()`; paso
`0x545e3f`: gira ±2 según el lado de su partida, sube y se achica hasta apagarse) y las `729/0` y `/1`
(`0x543840`, la `/1` con `Impack03` en `0x719`; paso `0x547880`: la luz sube 1,16 hasta que le quedan 10 pasos y
después baja 0,862). Las de Kalima con el subtipo de los objetos (`6f0/2`, `6f1/2` con 40 de vida y `6f2/4`)
suman la escala del argumento (`0x543f04`, `0x543f49`, `0x54417f`). En el pase de ojo, la `622/1` va al
billboard común (`0x548bf0`), y la `5de/7` y la `729` también (`caseD_5c5`). La mariposa (`0x520d11`): el giro
según `+0x88`, la deriva y el aleteo de la velocidad z, el freno contra la altura del terreno (debajo de +50
sube, encima de +150 baja), el salto de z, la luz del subtipo y el sprite `0x5ac` de escala 1; la `/3` suelta,
con `rand() & 1` en 0, chispas `5c5/7` que caen 0,6 por paso (`0x544f7b`). Con `+0x58` en −2, el dibujo del
efecto publica la pose y sale (`0x600328`).

**Los dibujos intermedios de MuRet** (sin frame del original): `SelectionWorld::BeginFrame` copia la luz
dinámica, el `rand()`, las partículas, los efectos, los objetos y la cola, y dibuja sobre las copias; con un
frame del original presta los del mundo. La entrada al juego desde la selección sigue con el 54 cargado
durante la espera del `F3 03`: el juego avanza esos objetos y la mariposa (`GameScene::StepTheObjects` y
`StepTheModelEffects`). MuRet: `Scenes::SelectionWorld`, `Scenes::StepAidaObjectFrame`,
`World::StepAidaObject`, `World::ModelEffects::CreateButterflyEffect` y el caso `0xae` de
`World::StepGameModelEffect`. Tests: `AidaObjectsTest`, `AidaObjectServicesTest`, `SelectionParticlesTest`,
`GameModelEffectsTest.TheButterfly*`, `ModelEffectsTest.TheButterflyIsCreatedHiddenInTheSelectionWithOneDraw`,
`GameDrawTailTest.AHiddenModelEffectPublishesItsPoseWithoutGeometry` y
`CharacterSelectSceneTest.TheChosenOneGlowsAndRaisesItsTwoChaseLightsOnNativeFrames`.

### La barra de la selección y el borrado

Leída en la CL6.5 (*estático*, y *emulado* el lugar de las ventanas y el armado de los avisos).

**La barra** (`+0x7f50`: `0x63e820` la arma, `0x65f4b0` la ubica, `0x63eb90` la atiende y
`0x63ec00` la dibuja). Va en x = 22 e y = −ftol(H · −0,945f) − 54, con el producto en la x87 como la
barra del login pero sin el −11: 399 en 480, 512 en 600, 671 en 768 y 913 en 1024. Mide (W − 44) ×
54 y no es modal: solo tapa su rectángulo, y con el mouse ahí `+0x8de8` queda en 1 y la escena no
elige ni hace el doble clic, y `0x634680` no llama a `0x59ec80` (`0x63485b`): `DAT_006c8e30` queda
con el último que estuvo bajo el mouse, y **el contorno se queda en él**. Lo mismo con un aviso
abierto, que tiene el mouse; entonces `0x65fc30` le da el mouse solo a la ventana de la cabeza, y
los botones de la barra siguen con su estado (`0x669f40` corre `0x63c490` en todas), sin el mouse
encima. Enter no mira `+0x8de8`. Lleva:

| Pieza | Textura (slot) | Tamaño | Lugar |
| --- | --- | --- | --- |
| el adorno | `deco.tga` (`0xe`) | 189 × 103 | (W − 189, y − 46) |
| la franja | ninguna, negra con alfa `0x8f` | (W − 44) × 21 | (22, y + 33) |
| crear | `b_create.tga` (`0xf`), 4 cuadros | 54 × 30 | (22, y) |
| el menú | `server_menu_b_all.tga` (`0x10`), 3 cuadros | 54 × 30 | (77, y) |
| conectar | `b_connect.tga` (`0x11`), 4 cuadros | 54 × 30 | (W − 131, y) |
| borrar | `b_delete.tga` (`0x12`), 4 cuadros | 54 × 30 | (W − 76, y) |

Los cuadros: 0 normal, 1 con el mouse encima, 2 apretado y 3 deshabilitado (el menú no se
deshabilita). `0x63eb40` habilita crear si hay un lugar vacío de 0 a 4, y conectar y borrar si hay
uno elegido. El dibujo: el adorno y la franja, `0x620a00(1)` y `glColor4f(1, 1, 1, 1)` (la franja
apagó la textura), el string del primer aviso `0x0D` de tipo 1 que llegue en la escena (`+0x511`),
el texto 1710 en (W − 464, y + 40) en `0xff27d9ff` y, si algún personaje de la lista tiene el bit
`0x10` de sus banderas (`+0x543`, que calcula el armado una vez), los textos 436 y 437 centrados en
(320, 330) y (320, 348) de 640 × 480 (`0x623690`: x − (cx · 640 / W) / 2, en enteros sin signo),
negros sobre `0x8000ffff`; después, los cuatro botones. Un clic por paso, en este orden: conectar
(lo de Enter, `0x633c10`), el menú (`+0x3d0`), crear (`+0x8498`) y borrar (`0x63edf0`).

**Borrar** (`0x63edf0`), con el elegido: con gremio (`+0x368` distinto de `0xff`), el aviso `0x92`;
de nivel 80 o más (el byte `0x6d5e24`) y con el nombre del grupo del servidor elegido distinto
(`strcmp`) del texto 559, el `0x83`; con las banderas `0x12`, el `0x61`; si no, el `0x5e`, con el
nombre en el `%s` del texto 1712. Su OK o Enter abre el `0x5f`: el texto 1713, la caja
`delete_secret_number.tga` (`0x728`, 171 × 23) en (+32, +76) y el EDIT de `WinMain` en (+42, +84),
con `ES_PASSWORD`, solo dígitos y hasta 7, vaciado al abrir (`0x6436c0`). Su OK o Enter
(`0x643740`) manda el `F3 02` con el nombre del elegido y los 10 bytes del campo (el texto y ceros:
uno vacío se manda igual) y abre el `0x5d` (471, sin botón). Cancelar o Esc en cualquiera de los
dos cierra sin mandar. La respuesta cambia el `0x5d` por su aviso: 0 → `0x92`, 1 → `0x60` (que al
abrirse borra al elegido y deja a ninguno, Q-C104), 3 → `0x62`, otro → `0x89`.

Los avisos parten su texto en dos renglones de menos de 52 bytes (`0x632c90`): uno va al medio en
y + 38; dos, en (+25, +32) y (+25, +51). Los tipos (`0x642bf0`): 0 sin botón; 2 un OK en (+149,
+72); 3 un OK en (+98, +72) y un cancelar en (+200, +72), con Esc que cancela; 4, la caja del código
con el OK en (+209, +72) y el cancelar en (+264, +72). Los textos 1711 a 1716 están vacíos en el
`Text.bmd` del laboratorio (**verificado**): el `0x83`, el `0x5e`, el `0x5f`, el `0x60`, el `0x72` y
el `0x36` salen sin texto.

Un personaje que arma `0x511290` sale sin gremio: `0x511ca6` pone su `+0x368` en `0xff`, y la
respuesta de crear (`0x4c2290`) no lo toca. El que se crea en la sesión se puede borrar sin otra
lista.

### La ventana de crear

Leída en la CL6.5 (*estático*; *emulado* el lugar en cuatro pantallas y los chequeos del nombre con
una tabla de nombres). La ventana `+0x8498` (`0x63d1a0` la arma, `0x63d4e0` la ubica, `0x63d7c0`
cambia la clase, `0x63d880` la atiende y `0x63e0a0` la dibuja) es modal: su base es un negro de alfa
`0x80` de toda la pantalla. Con (x, y) = ((W − 454) / 2, (H − 406) / 2), en 640 × 480 (93, 37):

| Pieza | Textura (slot) | Tamaño | Lugar |
| --- | --- | --- | --- |
| el fondo del nombre | `cha_id.tga` (`0xc`) | 346 × 38 | (x, y + 317) |
| los atributos | negra, alfa `0x8f` | 108 × 80, o 96 con el Dark Lord | (x + 346, y + 50) |
| la franja de abajo | negra, alfa `0x8f` | 454 × 51 | (x, y + 355) |
| las clases 0 a 2 | `cha_bt.tga` (`0xd`), 4 cuadros | 108 × 26 | (x + 346, y + 157 + 26 i) |
| las clases 3 y 4 | la misma | 108 × 26 | (x + 346, y + 246 + 26 (i − 3)) |
| OK y cancelar | `0x724` y `0x725`, los del aviso | 54 × 30 | (x + 346, y + 325) y (x + 400, y + 325) |
| el campo del nombre | el EDIT de `WinMain` (`DAT_05763504`) | 180 × 14 de 640 × 480 | (x + 78, y + 338), en 640 × 480 |

Las clases van con el cuadro 3 marcadas y el 0 deshabilitadas, y con los textos 20 a 24 de
rótulo. `pkt[4]` de la lista es la clase más alta que se puede crear: `0x63d7c0` habilita de la 0 a
ella. Con 5 o más escribe fuera de los cinco botones (Q-C119). El armado marca al Dark Knight
(`+0x890` en 1): con `pkt[4]` en 0 queda marcado y deshabilitado, y el OK lo crea igual. La clase no
vuelve a la 1 al cerrar y abrir. El campo, sin `ES_PASSWORD`, sin filtro y con tope de 10 bytes,
se vacía al abrir y al cerrar; toma los bytes de la página de códigos de Windows (en Occidente,
CP1252: "Peña" se manda con su `0xf1`), y como nadie le saca el foco, sigue tomando lo que se escribe
con un aviso arriba. El dibujo, después de la base y de la vista previa (que no se oscurece): el fondo
del nombre, las dos cajas, los siete botones, los atributos (los valores de `0x6d657c..0x6d659c` en
`0xff00b4ff` y los rótulos 1701 a 1704 y 1738 en blanco, de a 17 desde ay + 10), la descripción
(el texto 1705 + clase en dos renglones de menos de 75 bytes, en (x + 10, y + 367 + 19 j)) y el
campo. Con este `Text.bmd` los textos 1655 a 1799 están vacíos: se ven solo los números.

**El OK, o Enter** (`0x63d990`), después de las clases y antes de cancelar y Esc:
1. menos de 4 bytes: el aviso `0x79`;
2. `0x622c70`: alguna palabra de `FilterName.bmd` en el nombre sin espacios; alguna de `Filter.bmd`
   en el nombre sin espacios si no empieza con `/` (`0x622b40`); o, en el nombre tal cual, el
   espacio, el punto, `\xa1\xa1`, `\xa1\xa4`, `\xa1\xad`, las cuatro "Webzen" o los textos 457
   y 458: el `0x72`. La búsqueda (`0x576960`) distingue mayúsculas, y una palabra vacía está en
   cualquier nombre (Q-C117);
3. `0x447ca0`, parchado con NOPs en este binario: mira los bytes 0, 2, 4… y uno es malo si es menor
   que `0x21`, está de `0x7f` a `0xaf` o es mayor que `0xf7` (Q-C118): el `0x7a`;
4. si pasa: el `F3 01` con el nombre y la clase, cierra la ventana y abre el `0x5d`.

El OK copia el campo con su cero en `0x7798bf8` (`GetWindowTextA`), que solo se vacía al abrir: detrás
del nombre quedan los bytes de un intento anterior más largo, y `0x447ca0` los mira (Q-C118). Un
aviso del nombre se abre arriba: su OK lo cierra y la ventana queda con el nombre. La respuesta
(`0x4c2290`): con 1 se cierra el `0x5d` y el nuevo entra a la lista; con 0 o 2, la ventana vuelve a
abrirse, vacía, con el `0x36` o el `0x37` arriba.

**La vista previa** (`0x63e460`, después de la base de la ventana y antes de lo demás, así que no se
oscurece). El objeto suelto `0x7437050` lleva el NewFace de la clase:
- `0x63e3e0` lo arma de nuevo con `0x511290` en (50, 50) y la acción 1. Corre al entrar a la escena
  (`0x633f20` → `0x65f4b0` → `0x63d1a0` → `0x63d7c0`) y con cada clase que se elige;
- `0x50ba30` lo dibuja por `0x602e90`, porque no es el `0x20a`, y sin la sombra (los `0xa8` a `0xac`
  quedan afuera);
- `0x511290` le prende la luz por vértice (`+0xe8`, `0x511be6`), y `0x6002e0` les pone a los `0xa8`
  a `0xac` el color en 4,8 antes de `RenderBody(2)` (`0x6007d7`). Cada esquina queda en 4,8 ·
  max(0,2; 0,8 · L · n + 0,4): 1 al recortarse, salvo de espaldas a la luz.

La cámara (`0x6338a0`) queda en el objeto más (10, −500, 48), mirando con (−90, 0, 0), con el fov en
10 y el near y el far de la escena 4. El viewport es `0x620db0(ftol(x / rx), ftol(y / ry), ftol(410 /
rx), ftol(335 / ry))` desde la esquina de la caja, y nadie limpia la profundidad. Por clase, los
ángulos y la escala son (0, 0, −40) y 5,9; (0, 0, −12) y 6,05; (8, 0, 5) y 9,1, más 4,8 en x; (8, 0,
−13) y 6, más 1,8 en z; y (8, 0, −18) y 6. El corrimiento se suma en cada frame a la posición del
objeto después de armar la cámara, y crece sin tope: en pantalla es un corrimiento fijo respecto de
la cámara.

La animación va a 0,6 keys por avance: la velocidad 0,3 que `0x61ca30` pone en las acciones 0 y 1,
por el 2 de `0x4f9580` con los NewFace. Al terminar la acción 1, `0x63e430` pone la 0. La ventana la
avanza una vez por paso y la lógica de la escena otra, así que con la ventana arriba va el doble, y
sigue bajo un aviso. Desde la tarea 3 de CL7, MuRet reproduce esos dos avances sobre el mismo
ActorState: uno siempre desde 0x634303 y otro solo cuando la ventana tiene la UI. El ruling provisional
de B3 queda reemplazado; D-C4 conserva la interpolación del dibujo.

Los modelos, `Logo05` en el `0xa4` y `NewFace01` a `05` en `0xa8` a `0xac`, vienen de `Data\Logo`.
Sus texturas suben desde `0x60d530(0x6f8)` siguiendo la tabla global, y terminan en `0x716`
(**verificado** contra el volcado de Lorencia: los tamaños, los componentes y los nombres de GL,
Q-C91). Las del `0xa4` van con GL_LINEAR y las de los NewFace con GL_NEAREST, las dos repitiendo
(*estático*: el volcado no guarda el filtro).

### Los personajes de la lista

`0x4c20a0` ([04](04-protocolo.md#la-selección-de-personaje-f3)) pone cada personaje en
`chars[lugar]` con `0x517270`, en la posición de su lugar (x, y y el ángulo en z):

| Lugar | x | y | Ángulo |
| --- | ---: | ---: | ---: |
| 0 | 12.880 | 15.046 | 100° |
| 1 | 12.863 | 15.251 | 75° |
| 2 | 12.974 | 15.425 | 50° |
| 3 | 13.151 | 15.533 | 25° |
| 4 | 13.349,6 | 15.527 | 0° |

Las mallas del personaje de la traza caen alrededor del lugar 0 (**verificado**). Con un lugar de
más de 4 la posición no se carga (Q-C101).

`0x517270(índice, clase, piel, x, y, ángulo)` llama a `0x511290(chars[índice], 0x20a, 0, 0,
ángulo)` (la z, de `0x5f93c0`) y pone `+0x1f4` en el índice, la luz `+0xf4` en 0,3, X/Y,
la clase, la piel, y `+0x78` y `+0x7c` en 0. En la escena 2 viste la ropa fija del login (§ El
login, al entrar); en las otras, casco, armadura, pantalón, guantes y botas en `0x2212`, `0x221a`,
`0x2222`, `0x222a` y `0x2232` más `clase & 7`, y las manos, las alas y la mascota en -1. Después,
`0x4e6910(0x1000000, c)`, `+0x1d8` en 0, `0x512930` y `0x4f2250` (la acción de parado).

Contraste estático CL7.3 B: `0x511290` guarda su quinto argumento en `+0x24`
(`0x512292`, yaw), y calcula Z en `+0x18` (`0x51226d`) **antes** de que `0x517270`
reemplace X/Y (`0x5172c4/0x5172ca`). La altura usa las coordenadas iniciales (50,50),
el mundo vigente, el bit0 del atributo nuevo y el equipo previo; no la posición final de
selección ni el valor 100 del ángulo.

**Cierre estático de la altura inicial (CL7.3 B).** `0x511b9f..0x511bb9` recalcula
`+0x3e2` desde el atributo de la celda nueva `(0,0)`; `+0x3e3` recibe 0 en `0x511d68`.
`0x512208` lee el **word318 anterior**; sólo `0x51245c` lo reemplaza por `ffff`.
La Z es `float(Height(50,50) + delta)`: delta es 30 si el mundo no es −1, el word
anterior es `1c15` y el bit0 nuevo no está puesto; es 90 en mundos 8 y 10. Sin ese
predicado guarda Height directamente, sin sumar cero. La suma precede al único
guardado float (`0x51226d`), sin redondear Height a float primero. Se conserva el
contrato numérico PC53/nearest de `TerrainHeightAt`; el constructor no configura
el control word. Para tipo `020a`, los finalizadores `0x43a250` y `0x42d7f0`
retornan AL0 sin modificar el actor.

| Frontera del original | Mundo y efecto relevante |
| --- | --- |
| `0x6349b2/0x6349bc`, init del login | Escribe 55 y carga sus tablas; construye su avatar visible en **slot2**, no Hero0. |
| `0x634dce/0x634dd8`, login aceptado | Escena 4, estado 32 y pedido F3/00; todavía no carga 54. |
| `0x4c20a0`, F3/00 | Construye cada registro con el contexto vigente y termina en estado 33, incluso con lista vacía. `0x622b20` sólo escribe el byte de clases disponibles. |
| `0x6342de/0x6342e5`, siguiente paso habilitado | Marca inicialización y llama `0x633f20`: escribe 54 y carga sus tablas. El preview `0x7437050` está fuera del pool; en escena 4, `0x61a560` omite su limpieza. |
| `0x634680`, dibujo habilitado | Exige flag de inicialización distinto de 0 y estado signed ≥33; calcula cámara antes de escribir Z163 a los cinco slots. |

La primera F3/00 anterior al Step de inicialización usa 55 y todavía no habilita el
dibujo; las creaciones posteriores al init usan 54, incluso sin otro Step.
Cada F3/00 aceptado y alta F3/01 exitosa reconstruye aunque sea idéntica.
Si se repite un slot, se ejecutan todas las construcciones y queda el último registro;
el renderer recorre los cinco slots una sola vez por índice. F3/02 apaga el slot,
conserva word318 y no construye. Tras Render163, otra creación reemplaza Z antes de
otro dibujo; un retorno al login puede consumirla en su primer Step.

El historial empieza en cero por `REP STOSD` de WinMain (`0x4bafa6..0x4bafb3`):
401 registros de `0x438` bytes; Hero apunta al primero (`0x4bafdf`). `0x510f70`
conserva word318 para cualquier clave preservada: `0x401040` elimina nodos/strings
propios sin desreferenciar el owner, y `0x4fb1a0` libera cloth, partículas y esferas
de allocations separadas. Se cotejaron vtables y destructores, sin deducir la
postcondición sólo de stores locales. Login reemplaza el historial de slot2 por
`1c16`, que no satisface `==1c15`; los demás slots lo conservan. La composición
supone objetos válidos y los contratos ordinarios de loader, UI, CRT, socket e
interfaces de valores lógicos ya investigados; no porta su representación protegida
ni certifica sus árboles completos.

MuRet mantiene esos cinco historiales en los cinco primeros registros del pool de 401 de
`Scenes::WorldState`, que guarda también el mundo en curso (`DAT_006c9dd0`, el 54 que deja
`0x633f20`), y las tablas CPU en `Scenes::SelectionState`, que construye sobre ese pool; main
es dueño de los dos. Los eventos poseídos de `ServerSession::TakeCharacterEvents`
conservan orden y repeticiones; la vista UI conserva sólo la última entrada de cada
slot. Main aplica eventos después de Poll y antes de Step/Request/Start. Login presta
CPU mientras vive y la transfiere antes de Release; libera las texturas con la escena.
Las revisiones reconstruyen también visuales idénticos, incluso con Render sin Step.
El atributo de cámara usa la celda lógica 0 acreditada en estas fases; la celda y
reasignación de Hero al entrar al mundo siguen perteneciendo a L1/P. Faltar tablas
produce un error explícito, sin inventar Z.

Evidencia nueva sólo estática, del PE `c32bbd5f`: constructor completo, 1.537
instrucciones/5.203 bytes; init login, 168/690; init selección, 259/914; cleanup, 45/125;
cadena de cola, 107/275; revisión de ownership e historial, 1.135/3.486 en 26 bloques.
Todos esos cotejos terminaron sin diferencias de bytes. Los tests nuevos usan
entradas sintéticas y datos del repo, sin nuevas capturas ni ejecución o emulación
del original.

`0x512930` pone la cabeza (`+0x1f8`) en `0x2212 + (clase >> 3) · 5 + (clase & 7)` solo con los
cascos `0x1012`, `0x1014` y `0x101c` a `0x101f` (si no, en -1), y la escala (`+0xc`) por clase y
según la piel sea 0 o no: 0,9 o 0,93 en las clases 0 y 1, 0,88 o 0,86 en la 2, 0,95 en la 3 y
0,92 en la 4.

`0x5139f0(lugar, equipo, 0, 0)` lee los 17 bytes del equipo (b), solo para el `0x20a`. El tipo de
un ítem es grupo · `0x200` + índice, y su modelo, `0x212` más el tipo:

| Qué | De los 17 bytes | Modelo |
| --- | --- | --- |
| la mano derecha | tipo `((b[11] & 0xf0) << 4) \| b[0]` | `0x212` + tipo; el vacío se compara con `0x1fff` (Q-C102) |
| la izquierda | tipo `((b[12] & 0xf0) << 4) \| b[1]` | igual; la clase 4 con el tipo `0xa05` va a `0x1c17` (`0x420cf0`) |
| el casco | índice `((b[8] >> 7) + (b[12] & 0xf) · 2) · 16 + (b[2] >> 4)` | `0x1012` + índice |
| la armadura | `(((b[8] >> 6) & 1) + (b[13] >> 4) · 2) · 16 + (b[2] & 0xf)` | `0x1212` + índice |
| el pantalón | `(((b[8] >> 5) & 1) + (b[13] & 0xf) · 2) · 16 + (b[3] >> 4)` | `0x1412` + índice |
| los guantes | `(((b[8] >> 4) & 1) + (b[14] >> 4) · 2) · 16 + (b[3] & 0xf)` | `0x1612` + índice |
| las botas | `(((b[8] >> 3) & 1) + (b[14] & 0xf) · 2) · 16 + (b[4] >> 4)` | `0x1812` + índice |
| las alas | v = `(b[4] >> 2) & 3`; con 3, v = `b[8] & 7` (0, sin alas; 5, `0x1c30`) | `0x1a12` + v, o `0x1a14` + v con 3 |
| la mascota | v = `b[4] & 3`; con 3, `b[9] & 1` da `0x1c15`, `b[11] & 1` `0x1c16` (con el caballo `0xbf`) y `b[11] & 4` `0x1c37` (con `+0x31b` = `b[15] & 3`) | `0x1c12` + v |
| los niveles | L = `b[5] << 16 \| b[6] << 8 \| b[7]`, 3 bits cada uno, que `0x513340` pasa al nivel del ítem (0 a 7 dan 0, 3, 5, 7, 9, 11, 12 y 13; la tabla de `0x51338c`): la derecha en 0 a 2, la izquierda 3 a 5, el casco 6 a 8, la armadura 9 a 11, el pantalón 12 a 14, los guantes 15 a 17 y las botas 18 a 20 | las alas y la mascota, nivel 0 |
| excelente | `b[9]` (bits 7 a 3 las cinco partes, el 2 la derecha y el 1 la izquierda) a `parte + 3`; `b[10]` igual a `parte + 4`, con su bit 0 a `+0x358`; `b[11] & 2` a `+0x360` | |

Un índice `0x1ff` en una parte del cuerpo es "vacío" y deja la de la clase (`0x2212`, `0x221a`,
`0x2222`, `0x222a` y `0x2232` más `(clase >> 3) · 5 + (clase & 7)`). Después, `0x414340`,
`0x4053f0` y `0x512930` (la cabeza y la escala). Con `+0x3e3` no toca la ropa.

### La carga del mundo (escena 3)

`0x6356f0`, la primera vez: la tabla de hash (no se leyó), `Interface\LSBg01` a `04` en `0x71e`
a `0x721` (`GL_LINEAR`, `GL_CLAMP`) y un objeto de `0x2a8` bytes (`0x640c10`, `0x640c90`) con **las
cuatro** en un mosaico de 800 × 600: `LSBg01` arriba a la izquierda (400 × 512), `02` arriba a la
derecha (en x 400), `03` abajo a la izquierda (400 × 88, en y 512) y `04` abajo a la derecha, con
la escala W / 800 y H / 600 (`0x6ba884`, `0x6ba880`), como la UI de la carga. Dibuja un frame
(`0x620db0(0, 0, 640, 480)`, `glClear`, `0x640ea0`, que llama a `0x647650` con cada una,
`glFlush` y `SwapBuffers`), borra el objeto, pasa a la escena 5 y suelta las cuatro texturas. Ese
frame queda en pantalla hasta que llega la respuesta del `F3 03`, porque el dibujo de la escena 5 no
presenta mientras tanto ([La entrada al mundo](#la-entrada-al-mundo)).

**Las dos ramas** (*estático*, CL7 hito 1, T5). `0x63571d` mira la bandera `0x7d3a13e`. En 0, en
orden: abre el contador `0x77f3b04` por la tabla de protección, le escribe `0x98967f` (`0x6357eb`)
y lo cierra con `0x412890` (`0x635963`, 4 `rand()` si deja la cuenta en 0); sube la bandera
(`0x635980`); sube las cuatro texturas (`0x635987..0x6359e1`) y arma el objeto
(`0x6359e6..0x635a20`). Las dos ramas siguen por el mismo final (`0x635a25..0x635ab4`): el viewport
de 640 × 480, `glClear(0x4100)`, `0x6219b0`, `0x640ea0([ui + 0x8dd0])`, `0x621a90`, `0x620fc0`,
`glFlush`, `SwapBuffers`, borra el objeto (`0x635a87`), la escena 5 (`0x635a91`), suelta `0x71e` a
`0x721` (`0x63c150`) y `0x553570(1)`, que vacía los campos de texto de la UI (`0x7798bf8`). **Con la
bandera en 1** saltea el contador, la bandera, las texturas y el objeto, y dibuja con
`[ui + 0x8dd0]`, que fuera de esta función es nulo (`0x65ed2e`, `0x635a87`): `0x647650` lo leería
en `[8 + 0xa0]`. Esa rama no se alcanza: la bandera tiene cinco referencias en `.text`, la bajan los
tres caminos de la selección justo antes de pasar a la escena 3 (`0x633efe`, `0x634469`,
`0x634631`) y la sube solo `0x635980`, y a `0x6356f0` la llama solo `0x6394f3`. MuRet
(`WorldLoadingScene::Enter`) da un error con la bandera en 1, en vez de presentar un frame.

## La escena del juego

Leída en el barrido de Lorencia (2026-11-05): cuatro lecturas del `main.exe` `c32bbd5f`, la entrada
y el esqueleto de la escena, la cámara, caminar y los cambios de mapa. Se contrastó con la traza de
Lorencia (`lorencia-Frame_016215.txt`, la de [Modelos y objetos](#modelos-y-objetos)), con el
`muproxy.log` y la PCAP de [04](04-protocolo.md#una-sesión-real-de-juego-2026-10-30), con
`lor_slots.bin` y con el código real emulado (unicorn). Cada punto lleva su clase. Los paquetes,
campo por campo, están en [04](04-protocolo.md#movimiento-y-mundo); los modelos de cada mapa y
`Gate.bmd`, en [05](05-formatos-de-datos.md#los-modelos-de-cada-mundo); lo que pide una corrida del
original, en [08](08-pendientes.md) (P-C28 a P-C31). MuRet, hoy, manda el `F3 03` desde
`GameScene`, retiene el frame mientras espera (CL7 hito 1, T5), aplica la respuesta con
`Scenes::ApplyWorldEntry` (T7, [abajo](#la-entrada-al-mundo)), también una segunda con el héroe ya en
el mundo (T13), y dibuja la escena 5 hasta el primer pase del terreno, donde corta (T8, [El dibujo
del juego](#el-dibujo-del-juego)).

### La entrada al mundo

Del Enter de la selección al primer frame de Lorencia, *estático* salvo lo que se marca:

1. **La selección** (`0x6342c0`, `0x633c10`) copia el nombre, la clase, la piel y el nivel al
   registro propio (`[0x7681eb0]`), suelta lo suyo (`0x61cc50`), baja `0x7d3a13e` y pasa a la
   escena 3 ([La lógica](#la-lógica)). La copia va entre la apertura y el cierre del registro
   propio por la tabla de protección, en los tres caminos (*estático*, T5): Enter
   (`0x6343e2..0x634470`, abre con `0x4e79a0` y cierra con `0x40f370`), el doble clic
   (`0x634546..0x634638`, abre en línea con `0x40b440` y cierra con `0x40f370`) y el botón
   (`0x633c10..0x633f05`, en línea y cierra con `0x40b4d0`). `0x40f370` resta 1 a la cuenta
   (`[entrada + 0xf08]`) y, en 0, llama `0xf08` veces a `rand()` (`0x69fd70`): el cierre gasta
   3.848. Lo copiado es lo que `0x4c20a0` le puso al personaje con la lista: la clase de `0x517270`,
   la piel 0 (`0x4c21d9`), el nivel en `+0x1d6`, las banderas en `+0x1d8` y el nombre en
   `+0x1d9..+0x1e2` con el 0 de `+0x1e3` (`0x4c21dd..0x4c220c`). El nombre va con un `strcpy`
   (`0x634433..0x63444e`): lo que sigue al cero en el registro queda como estaba. Después de
   `0x61cc50`, la bandera en 0 y la escena 3. Deja `0x7d3a101` en 1 (`0x633f30`, dentro de
   `0x633f20`, antes de cargar el 54), y solo lo bajan los dos caminos de `0x4c2710` (`0x4c2f39`,
   `0x4c38f9`); también lo pone en 1 el login, al cargar el 55 (`0x6349ab`). El paso de la escena 5
   corre entero con lo que dejó la selección hasta que la respuesta recarga todo.
2. **La escena 3** (`0x6356f0`, un solo frame) pone el contador `0x77f3b04` en 9.999.999
   (`0x98967f`, por la tabla de protección `0x57630c8`; el cierre, 4 `rand()`), sube `0x7d3a13e`,
   dibuja el mosaico de `LSBg` ([La carga del mundo](#la-carga-del-mundo-escena-3)), presenta, pasa
   a la 5 (`0x635a91`) y llama a `0x553570(1)`.
3. **El primer paso de la 5** (`0x636290`, con `0x7d3a140` en 0, que pone en 1 en `0x6362fb`): el
   log `"> Character selected <%d> %s"`; el `F3 03` y el estado `0x3c` (con `0x7d366bc` en 0, que
   nadie escribe); la UI en el modo del juego (`0x65ee60`, `0x65f5f0`); **el yaw de la cámara en
   −45°** (`0x7d36870 = 0xc2340000`, `0x636838`); `0x553570` y las 120 líneas del área de mensajes
   vacías (`0x555190`); unas siete ventanas, de `0x45db50` a `0x447ae0`; y, si el byte
   `[0x710148] + 4` vale `0xff`, el `A0` en C3 (`0x63693a`). En el cable el `A0` sale detrás del
   `F3 03`, antes o después de la respuesta según lo que tarde el servidor (*verificado*,
   [04](04-protocolo.md#una-sesión-real-de-juego-2026-10-30)).
4. **Mientras no llega la respuesta**, el dibujo `0x6371a0` lee el contador por la tabla de
   protección y con más de 30 devuelve 0 (`cmp esi, 0x1e`, `0x63722e`): sin ese verdadero no hay
   `SwapBuffers` ([02](02-arranque-y-ciclo-de-vida.md#qué-corre-en-cada-paso-y-qué-en-cada-frame)),
   y **queda en pantalla el mosaico de la escena 3**. La lectura abre el acceso en
   `0x6371b7..0x637214`, lee en `0x637219` y cierra con `0x40b800` en `0x637229`, antes de
   comparar: con la cuenta en 0 gasta 4 `rand()` en cada frame, aunque después no dibuje. Con
   `0x7d3a101` en 0 sale antes, en `0x6371a6`, sin leerlo. En cada paso, `MoveHero` abre el contador
   dos veces (8 `rand()`) y sale en `0x59afd6`, antes de `0x57cfc0`: mientras espera no abre el
   registro propio ([Caminar](#caminar)). Cuántos pasos corren así, P-C28 b.
5. **La respuesta** va por el camino de [03](03-red-y-cifrado.md): `FD_READ`, la cola, `0x4e1d50`,
   `Dec2` y el rearmado a C1, FoxAnti, `0x4e2b10` y el subcódigo 3 de `0x4e2f93` (`0x4e306b`), que
   la pasa a `0x4c3940(pkt, cifrado)`. En el cable llega en el mismo segmento que `26`, `27`, `0F`,
   `03`, `F3 10` (C4) y `F3 11` (*verificado*).
6. **El paso siguiente** corre con el estado `0x3d`; `0x59adf0` (el `MoveHero` de
   [Caminar](#caminar)) baja el contador de 30 a 29 (`0x59af0a`) y el dibujo ya pasa: es el primer
   frame de Lorencia.

**La respuesta, `0x4c3940`.** Sin cifrar (`param_2 == 0`, `0x4c396a`) no entra al mundo: arma un
`C1 .. F1 03 …` y lo manda, sin leer qué lleva. Cifrada lee los campos
([04](04-protocolo.md#la-entrada-al-mundo-la-respuesta-del-f3-03-y-el-f3-10)) y después hace esto,
en orden (`0x4c41d4..0x4c4b60`, *estático*):

| # | Qué | Dónde |
| --- | --- | --- |
| 0 | `0x7d36874 = 0` y los stats al registro propio, entre su apertura y **el primer cierre en línea** (3.848 `rand()` si lo deja sin accesos) | `0x4c405a`, `0x4c41c4` |
| 1 | el mapa a `0x6c9dd0` y **`0x61a560(mapa)`** (abajo); en el mapa 34, además, el `C1 04 BD 00` | `0x4c41cf`, `0x4c41d4`, `0x4c421a` |
| 2 | `0x447d70(mapa)`: los objetos de los mapas de evento (9 y 11 a 17 por el argumento, 18 a 23 leyendo `0x6c9dd0`); en los demás no hace nada | |
| 3 | **el héroe es `chars[rand() % 400]`** (`idiv 0x190`; el índice en `0x57678b0`), con `0x511290(héroe, 0x20a, X, Y, ángulo)` y `+0x1f4` = la clave `0x57678bc` (Q-C145) | `0x4c44ad`, `0x4c44fc`, `0x4c4516` |
| 4 | la clase en `+0x1d4` y la piel en 0 entre la apertura y **el segundo cierre en línea** del registro; el PK (`+0x375` = `pkt[0x2c]`), el CtlCode (`+0x1d8` = `pkt[0x2d]`) y `+0x88 = 1` | `0x4c46f0` |
| 5 | `0x512a30(héroe)`: los modelos del equipo, desde el registro todavía viejo (abre y cierra el registro por su cuenta) | `0x4c4712` |
| 6 | el puntero del héroe en `0x743a170`, **después** de `0x512a30`: mientras viste, `0x743a170` sigue en el héroe anterior (o en `chars[0]`, `0x4bafdf`); el nombre en `+0x1d9` | `0x4c4725` |
| 7 | **vacía los 12 lugares del equipo** del registro (desde `+0xb6c`, de a `0x48`: el tipo en −1, el dword de `+4` y el byte de `+0x1b` en 0), junto con el nombre y antes del **tercer cierre en línea**: el equipo lo trae el `F3 10` | `0x4c47c8..0x4c47da`, `0x4c481e` |
| 8 | `+0x1e3 = 0` y el primer efecto de llegada, `0x518cf0(0x61f, posición, ángulos, luz, 0, héroe, −1, …)` | |
| 9 | el estado `0x3d`, `0x77f3b67 = 0` y `0x553780(1, 0)` | `0x4c4854` |
| 10 | **el contador `0x77f3b04` en 30**: desde acá el dibujo pasa; el cierre es `0x412890` (4 `rand()`) | `0x4c48eb` |
| 11 | `0x77f3b10 = 0`, `0x6c8dcc = 6` y `0x420d60(héroe)`: el cuervo, si la mano izquierda (`+0x2d8`) lleva el `0x1c17` (`0x421d60`, un `new 0x460` y `0x4167b0`, que saca `rand()`); si no, nada | |
| 12 | el segundo `0x518cf0(0x61f, …)`, con los mismos argumentos | |
| 13 | **el alpha del héroe en 0** (`+0x174`): entra transparente | |
| 14 | `0x657a90` (la UI) y los elegidos en −1: `0x6c8e4d = 0xff` y `0x6c8e28..0x6c8e38`. Con todas las ventanas cerradas, `0x657a90` → `0x657ab0(−1, 0)` saca **2 `rand()`**: los de `0x659290` con la máscara `0x20` (`0x657fc1`, entrada `0x7d53711`, cierre `0x4c0ec0`) y con `0x10000` (`0x658e0e`, entrada `0x7d53710`, cierre `0x4c1190` en `0x65970d`), un `rand()` por cierre de una entrada de 1 byte; los demás están detrás de ventanas abiertas, y el cierre de `0x65903e` no corre con `ebx = −1` | `0x657fc1`, `0x658e0e` |
| 15 | `0x78abb0c`, `0x78abb10` y `+0x378` del héroe (el `Movement`) en 0 | |
| 16 | `0x4f2250(héroe)`: la acción de parado ([Caminar](#caminar)) | |
| 17 | los sonidos de los mapas de evento, `0x41ee00(id, 1)`: el `0x8b` fuera de Blood Castle (mapa menor que 11 o mayor que 17) y el `0x99` y el `0x9e` fuera de Chaos Castle (menor que 18 o mayor que 23) | `0x4c49d6..0x4c4a11` |
| 18 | el chequeo de ResourceGuard (un objeto de `0x2c` bytes, `GetModuleFileNameA`, `0x4292e0` y `0x681ec0`), sin efecto: el `jmp` de `0x4c4ad5` saltea el aviso y el `0x4e8380` (Q-C147) | `0x4c4ad5` |
| 19 | `0x65dc30` y el renglón `" Bienvenido"` más el nombre del mapa en el área de mensajes: `sprintf("%s%s", texto 484, 0x5f0b40(mapa))` y `0x555190(0x5767978, …, 1)` (el texto, *verificado* en el `s31_text.bin` del volcado de Devias) | |

Las velocidades del keepalive (`+0x4c` y `+0x58` del registro, [04](04-protocolo.md)) no las
escribe este handler; quién las calcula después, sin leer (P-C28 i).

**El héroe**, *estático* salvo lo que se marca:

- `0x511290(héroe, 0x20a, X, Y, ángulo)` guarda la celda cifrada en `+0x41c`/`+0x420` (la tabla de
  protección) y en claro en `+0x39a`/`+0x39b`; la posición en el centro de la celda, `+0x10 = X ·
  100 + 50` y `+0x14 = Y · 100 + 50` (`0x6b86b4`, `0x6b86b0`); la altura del terreno nuevo, `+0x18
  = 0x5f93c0(x, y)`, más 30 (`0x6b8858`; 90 en los mapas 8 y 10, `0x6b8948`) si el `+0x318` que
  traía el personaje era el `0x1c15`, la celda no es segura y el mapa no es −1 (el `fstp` a float va
  después de la suma); el ángulo en `+0x24`;
  **la zona segura en `+0x3e2`, el bit `0x01` del `.att` de la celda**; y el estado de partida:
  vivo, `+0x16d = 1`, el alpha y su meta en 1, la caja de (−60, −60, 0) a (40, 40, 120), `+0x88 =
  1`, `+0x371 = 'C'` y `+0x188` con el contador de pasos. En el mismo bloque de stores, sin rama por
  el tipo, deja en 0 `+0x16c` (el personaje visto, `0x511bd8`) y `+0x358` (`0x511c99`, el que
  después escribe `0x40d010` en el héroe publicado; T13). El ángulo es (dir − 1) · 45
  (`0x4c44bc..0x4c44d1`): la dirección 0 da −45° (Q-C175). En la traza, el héroe está en (14.350;
  12.050; 165), el centro de la celda (143, 120), con z = 110 · 1,5 (*verificado*); la sesión del
  cable entra en (133, 127) del mapa 0 con la dirección 0.
- **`0x511290` gasta 28 `rand()`** con la tabla de protección cerrada (*estático*, T7): siete
  accesos de 4 bytes, cada uno con su cierre de 4: escribe X, escribe Y, lee Y y X para la zona
  segura, lee X y Y para la posición y lee el contador de pasos `0x7d3a0f0` para `+0x188` (este
  último no gasta si el contador ya está abierto). Deja `+0x3e3 = 0`, el PK en 3, la escala en 0,9,
  las dos acciones en 1 y las partes sin ítem; la clave `+0x1f4` no la toca (la pone quien llama:
  `0x4c4516` acá, `0x517270` en `0x5172a8` en la selección, con el lugar de la lista). Con otro tipo
  que el `0x20a`, MuRet corta con error: esos casos del `switch` no están leídos.
- **El equipo viene del `F3 10`** (`0x4e309c` → `0x4c55a0`), no de la lista de la selección: cada
  entrada va a `0x5bc740`, que la escribe en el registro propio y llama a `0x512a30` con el héroe
  publicado (`0x743a170`), el que deja la fila 6 ([El F3 10, el inventario](#el-f3-10-el-inventario),
  Q-C204). Si el `F3 10` se despacha en la misma vuelta de `WinMain` que la respuesta, el héroe sale
  vestido desde el primer frame (P-C28 a).
- **Entra transparente y aparece en 20 pasos**: `0x4fd5f0` (`0x4fd72e`) → `0x4e9980` sube el alpha
  0,05 (`0x6b8a48`) por paso hasta la meta, con `+0x16d` en 1. El primer frame va con 0,05 y a los
  0,8 s es opaco.
- **El efecto de llegada `0x61f` se crea dos veces** en el mismo lugar. Lo crea el caso
  `0x61e`/`0x61f` de `0x518cf0`, con 20 pasos de vida y el subtipo 0 (`0x51eebb`: `+0x60 = 0x14`
  y sale por `0x51db73` **sin `rand()`**; la cabecera común deja `+0xd8 = 0,3` y la escala 0,9 si
  llega en 0 o menos); su paso (`0x5208a0`) solo
  cuenta, y su dibujo (`0x52dc20`) es un calco en el suelo con la textura `0x61e` (`0x5fb2f0`) y
  dos giros de `0x53d2c0(0x61f, …, ±ángulo)` que se apagan con la vida. Al ser dos iguales, se suma
  dos veces.
- **La acción de parado** (`0x4f2250`) depende del arma, de la zona segura, de la mascota y del
  mapa ([Caminar](#caminar), `SetPlayerStop`). Sentado arma un `C1 .. 18`: la tabla del XOR32 va en
  línea (P-C14 c).
- **La luz del héroe en la escena 5** (`0x50ba30`, desde `0x50cc55`) es la del terreno en su lugar,
  **del buffer dinámico** `0x7b4ccc4` (`0x5f99e0`), más `+0xf4..+0xfc`, que `0x511290` deja en 0. En
  la selección es 0,4 fijo, y en el login, 0,3 más el terreno. `0x50ba30` tiene además una rama
  propia del héroe (`0x50cca9`) que lee el registro propio, sin leer en detalle.

**La carga del mapa** (`0x61a560` → `0x616ec0`, *estático* salvo lo que se marca). La llaman tres
paquetes, el `F3 03`, el `1C` y el `F3 04` ([Los cambios de mapa](#los-cambios-de-mapa)). En orden:

1. Si el mapa y `0x6c9dd0` valen 32, pasa a 9 (Q-C58).
2. **Suelta** (`0x6070a0`) los modelos 0 a 159, las 256 listas de objetos (`0x7d325d0`), las
   texturas `0x23` a `0x67` y el byte "vivo" de cada tabla de efectos: los sprites `0x760f990`
   (1.000), los efectos con modelo `0x7491fd8` (200), los pájaros `0x7d2dca8` (40), los peces
   `0x7d35350` (10), las hojas `0x75e34b8` (200), las partículas `0x743a198` (3.000), la de
   `0x7609bd0` (100), los joints `0x74a8d78` (500) y la de `0x7d34818` (200; *inferido*, los lugares
   para sentarse). Lo demás de cada entrada queda (Q-C138). Después, `0x612230` y `0x612d50` sueltan
   los modelos `0x152..0x207` y los sonidos `0xa0..0x1c1` (los de los monstruos, *inferido*).
3. **Fuera de la selección** (con la escena distinta de 4): `0x607b40`, los ítems del piso (1.000 de
   `0x220` bytes en `0x77f4714`), y `0x510f70(0x57678bc)`, que mata a todo vivo cuya clave
   `+0x1f4` no sea la del héroe: `+0` en 0, `0x401040` y el caballo de la tabla `0x7d335d0` (10 de
   `0x1d4`) cuyo `+0x108` es ese personaje. Lo que le cuelga lo suelta **con los 400**: el muerto,
   el vivo de la misma clave y el que ya estaba muerto saltan los tres a `0x510fc2`, que llama a
   `0x421d60` (borra el cuervo de `+0x430`), `0x4fb1a0(c, c, 0)` (las telas de `+0x190`, con la
   cuenta en `+0x18c`, y las de la cabeza y las cinco partes, `+0x210 + 0x20 · k`) y `0x414340`
   (borra `+0x364` y, si había algo, pone `+0x360` en 0): el héroe que sobrevive pierde también su
   cuervo y sus telas (Q-C194, *estático*, T13).
   Recorre hasta `0x69780`, **400 registros de `0x438`**, uno menos que el pool (el 401 no lo
   mira), y compara la clave **con signo** (`movsx` en `0x510f8a`) contra el DWORD que recibe: una
   clave de `0x8000` o más nunca coincide, así que mata hasta al dueño de esa clave (*estático*, T7).
   Después, también en la selección (`0x61a5b2`, el destino del salto de la escena 4), `0x451d10` →
   `0x451590` borra de la lista `0x7245c8` lo que no tiene la clave del héroe publicado
   (`[0x743a170] + 0x1f4`, `0x4515ae`), y el objeto de `0x6eeda0` queda con `+4 = 1`, `+8 = 0xf3`,
   `+0xc = 400` y `+0x18 = 0` (sin leer qué son la lista ni el objeto).
4. **Los modelos del mapa** (`0x616ec0`): `Object8\drop01.jpg` en `0x607`, los especiales del mapa
   desde `0x60d530(0x69)`, `Object8\wt00..wt31.jpg` en `0x41..0x60`, `Object8\light01.jpg` en `0x68`
   y los 160 del mundo desde `0x60d530(0x41c)`. **El mapa 0 no usa el lazo genérico
   `Object<k+1>.bmd`**: carga 112 modelos con nombre de `Data\Object1\`. Las tablas, en
   [05](05-formatos-de-datos.md#los-modelos-de-cada-mundo). Como `0x6c9e0c` vale 1 en la `.data`, el
   primer `switch`, el de los `.smd` de `Data2\` (en el 0, `bird.smd`, `fish.smd` y
   `treesmall.smd`), está muerto. El segundo `switch` (`0x617325..0x61733e`, índices en
   `0x61a23c`, saltos en `0x61a1fc`) carga y crea con `0x606660` los especiales de algunos mapas,
   antes del `.obj`. En el 2 (`0x61744d..0x61751b`) carga `0x201` = `Data\Npc\obj_donkey` y
   `0x202` = `Data\Npc\obj_flag` (`0x60da30`, las texturas con `0x60d5f0(0x2901, 0x2600, 1)`) y
   crea el `0x201` en (19.100; 1.600; 270) y el `0x202` en (19.100; 1.700; 270), sin ángulo y con
   escala 1. *Verificado* en la captura de los objetos de Devias de la VM (2026-11-09): son el
   primer nodo de las listas 176 y 177 (la y de 1.600 por el float de 1/1.600, `0x3a23d70a`, da
   0,99999… y cae en el sector 0), y el total es 2.146 = los 2.144 registros de
   `World3\EncTerrain3.obj` más estos dos. En el 3, el `0x142` en (22.300; 3.000; 10)
   (`0x617520`); en el 55, lo de `0x6188ae`, sin leer. El 0 no tiene (*estático*, T7). `Scenes::LoadMap`
   crea los dos objetos del 2; sus dos modelos los carga `Scenes::WorldModels`, con los del `.obj`, la
   primera vez que el dibujo o el paso del juego los piden ([Los objetos del
   juego](#los-objetos-del-juego); [08](08-pendientes.md) P-C53 f). Los dos registros de `EncTerrain3.obj` con la y en NaN (492 y 493, tipo 86) caen en la
   lista 32: `_ftol(NaN)` da ahí 0 en el dword bajo; MuRet todavía los descarta (otro PR).
5. **El número del mundo** ([05](05-formatos-de-datos.md#terreno-de-un-mundo-dataworldn)), con
   `0x42a760` antes, que solo actúa en el mapa 30.
6. **Los archivos**: el `.map` (`0x5f87b0`, cuyo `0x5f8300` limpia las capas y **llena el
   corrimiento del pasto con 65.536 `rand()`**, Q-C146), el `.att` (`0x5f8390`), el `.obj`
   (`0x607370`, que crea cada objeto con `0x606660`), las alturas (`0x5f9000`, por 1,5) y la luz
   (`0x5f8f40`). Las alturas se cargan **después** del `.obj`; en el mapa 0 no importa, porque
   `0x606660` no mira la altura. Con otro número de mundo en el `.map`, el `.att` o el `.obj`, el
   `MessageBox` "file corrupted" y `WM_DESTROY`; sin `TerrainHeight`, "%s file not found" y
   `WM_DESTROY` (`0x5f9167..0x5f91bb`, Q-C180).
7. **`0x4377a0(mapa)`** arma el mundo especial de `0x723c48` solo en Kalima (los mapas 24 a 29 y
   36). En Lorencia da 0, así que `0x437850()` da falso y el dibujo hace el terreno y no el segundo
   pase.
8. **Los tiles** (`0x23..0x30`), el pasto (`0x32..0x34`), las hojas (`leaf01` en `0x64`, la TGA y
   después la JPG; `leaf02` en `0x65`) y la lluvia (`World1\rain01.tga` en `0x66`, `rain02` en
   `0x67` y `World10\rain03.tga` en `0x68`, que pisa el `light01`, Q-C136). En `World1` faltan
   `TileRock03..07`, que se saltean sin avisar; el pasto trae solo `TileGrass01.OZT` y las hojas
   solo `leaf01.tga`. *Verificado* contra la traza: el pasto es la textura de GL 1360, la que sigue
   a los nueve tiles que están (1351 a 1359).

**Lo que porta MuRet** (CL7 hito 1, T7). `Scenes::LoadMap` sigue los pasos 1 a 7 en ese orden y
`Scenes::ApplyWorldEntry` la tabla de la respuesta; lo que no está leído o no está portado corta con
un error que nombra la dirección, nunca en silencio:

- **Los mapas.** Con la `Data` del repo cargan el 0, 1, 2, 3, 4, 6 a 11, 18, 24 a 29, 30, 31, 33,
  34 (con el estado de Crywolf en 0, [Los objetos de Crywolf](#los-objetos-de-crywolf)), 36 y 54. Cargar los aliases
  de Kalima no completa sus ramas de paso/dibujo.
  El 3 incorpora en hito 2 el binder síncrono de la torre `0x142` y sus cinco efectos, antes del
  pasto ([Creación de objetos](#los-objetos-del-juego)). Cortan: el 5 por el archivo que falta
  en `World6`, el 35 por el ATT ausente de `World36`, el 30 con el asedio prendido en `0x61a6ea` (sin él,
  `0x42a760` carga los lanceros, [Los objetos de Castle Siege](#los-objetos-de-castle-siege)), el 34 con el estado de
  Crywolf en 1 o más en `0x61a6f4` (el `.att` y la luz según `0x42fe50`, antes del `BD`, que por eso no
  sale) y el 55 en `0x6188ae`. Sin
  `TerrainHeight.OZB`, el error nombra el archivo, con el pasto ya sacado (Q-C180).
- **Aviso y cierre por datos fatales (CL7b, Q-C180).** MAP/ATT y OBJ se procesan antes de
  alturas/luz, conservando los owners publicados antes del fallo. Un MAP que no abre permite
  consultar el ATT para alcanzar su aviso nativo; si el ATT existe, el MAP sigue siendo error,
  sin terreno sustituto. World6 pide `Data\World6\TerrainHeight.OZB file not found.`;
  World36 pide `Data\World36\EncTerrain36.att file not found.`. El primer mensaje queda en
  `WorldRuntime::fatalDataMessage`, separado del diagnóstico del parser o de Poll. Los archivos
  cortos rechazados de forma segura y las fronteras del 30 y del 34 (con otro estado de Crywolf) no inventan un pedido de cierre nativo.
  `main` detiene el bucle y pide `Window::ShowErrorMessage` con la ventana viva, usando SDL3;
  al volver sale con fallo. Tests de datos/sesión observan ese pedido durable y su orden,
  no un modal visible. La confirmación del modal y el cierre en Windows/VM siguen pendientes.
- **Los mapas de evento** (`0x447d70`: 9, 11 a 23) arman su objeto de evento después de la carga
  ([Los objetos de Devil Square y de Blood Castle](#los-objetos-de-devil-square-y-de-blood-castle)).
- **La respuesta**, sin las tablas de los conjuntos, corta dentro de `0x512a30`: en `0x550d10`, en su
  primera llamada, `0x40e1b0` ([Los stats del registro propio](#los-stats-del-registro-propio)). Con
  ellas (`main.cpp` las carga), `0x512a30` vuelve entero: `0x505820` corre si el lugar sorteado es el
  del héroe anterior (`0x512ee7`, con `0x743a170` todavía en él) y, si no, `0x550990` lee lo que dejó
  el último dibujo de los personajes, el de la selección ([El conjunto completo de
  `0x505820`](#el-conjunto-completo-de-0x505820)); corta ahí solo si MuRet no sabe qué dejó (un frame
  del juego en el medio). Cuando corta, la sesión queda en `Failed` con el héroe sin publicar
  (`0x743a170` sin tocar), el contador sin bajar a 30 y los `rand()` gastados hasta ahí: el primer y el
  segundo cierre del registro, el pasto, el lugar del héroe, los 28 de `0x511290` y lo que `0x512a30`
  sacó antes de cortar. Lo que sigue a `0x512a30` (filas 6 a 17) es `Scenes::SettleWorldEntry`, que
  también se prueba directo; el cuervo de la fila 11 corta en `0x420d60`. Lo que MuRet no modela no se escribe: `0x7d36874`, `0x77f3b67`, `0x553780`,
  ResourceGuard (Q-C147) y la fila 19 (los mensajes, CL9). Hito 2 incorpora `0x77f3b10`,
  `0x6c8dcc`, los elegidos de la fila 14 y `0x78abb0c`/`0x78abb10` como estado durable en
  `WorldRuntime::movement` y `WorldRuntime`; los disparadores de sonido quedan en `lastSoundId`,
  sin reproducción hasta la CL10.
- **La clave del héroe** (`0x57678bc`) es la del `F1 00`, que la escribe en `0x4c1ec0` antes del
  chequeo de versión ([04](04-protocolo.md)).
- **Divergencia previa**: la selección (escena 4) todavía no gasta los 28 de `0x511290` ni los
  65.536 del pasto al cargar el 54.

**Una segunda respuesta** (*estático*, CL7 hito 1, T13). Nada la frena: `0x4e2f93` pasa el
subcódigo 3 a `0x4c3940` sin mirar el estado ni la escena (`0x4e306b`), y la rama cifrada de
`0x4c3940` no tiene otra salida que su final (`0x4c4b65`): la única temprana es la del paquete en
claro (`0x4c396a`). Con el héroe ya en el mundo corre entera otra vez, en el mismo orden, sobre lo
que dejó la primera:

- **El mapa se carga siempre**, sea el mismo o no: `0x61a560` no compara el mapa con el cargado
  (`0x61a569..0x61a5df` solo mira el 32 y la escena). Suelta lo del anterior, así que de los dos
  efectos de llegada de la primera no queda ninguno y la segunda crea otros dos; vuelve a crear los
  objetos, a sacar los 65.536 `rand()` del pasto y a leer los tiles. El `1C`, en cambio, carga solo
  si cambia el mapa ([Los cambios de mapa](#los-cambios-de-mapa)).
- **El lugar del héroe se sortea de nuevo** (`0x4c44ad`), sin mirar dónde está el anterior:
  `0x57678b0` solo lo escribe `0x4c44d4`.
- **El héroe anterior sobrevive** si el sorteo no cae en su lugar (Q-C196): tiene la clave del
  jugador, y `0x510f70` (desde `0x61a5aa`) solo mata a los de otra clave. Queda vivo en su registro,
  con su celda, su posición, su equipo y su nombre; pierde su cuervo y sus telas (Q-C194), pero no su
  caballo: `0x510f70` solo apaga los de los que mata, y `0x443d40` en un `F3 10` (`0x4c5e56`), los del
  publicado, que desde `0x4c4725` es el nuevo (*estático*). Si el
  sorteo cae en su lugar (1 en 400), `0x511290` lo reconstruye en el mismo registro, con su `+0x318`
  para el corrimiento de la altura.
- **Mientras viste al nuevo, el publicado es el anterior**: `0x743a170` solo lo escriben WinMain
  (`0x4bafdf`) y `0x4c4725`. Lo leen, antes de `0x4c4725`, `0x451590` (la clave con la que limpia la
  lista `0x7245c8`, la misma del jugador), `0x512a30` en `0x512ee2` y `0x40d010`, desde `0x550d10`
  (Q-C197). Así, `0x505820` corre solo si el lugar sorteado es el del anterior; si no, `0x550990` lee
  lo que dejó el último `0x505820`, que después de un frame del juego es el del último personaje que
  dibujó `0x510d00` (Q-C188), y `0x40d010` toma la clase del anterior y le escribe a él el `+0x358`.
- Lo demás, como en la primera: los stats y los tres cierres del registro propio, que vacía el equipo
  que trajo el `F3 10`; el `0x3d` otra vez; el contador en 30; los efectos; el alpha del nuevo en 0;
  y la acción de parado. Lo que no toca: el yaw de la cámara (`0x636838` corre solo en el primer paso
  de la escena 5, con `0x7d3a140`) y `0x7d3a101`. Ninguna de las 365 funciones que se alcanzan por
  llamadas directas desde `0x4c3940` (sin la CRT) escribe por su dirección el nivel del trapecio
  (`0x7b4ccb8`), las olas (`0x78acbd8`), el corrimiento del agua (`0x78ecbec`), el yaw (`0x7d36870`),
  el zoom (`0x7d3689c`), el temblor (`0x7d325c8`), la escena (`0x6d5dc8`), `0x7d3a101`, `0x7d3a13e` ni
  `0x7d3a140`; de lo del dibujo, la carga escribe solo `0x6c9d58`, el pase del pasto (`0x5f87b0`).

Con todo cerrado, la segunda saca los mismos `rand()` que una primera con el mismo registro y el
mismo mapa (P-C39). Lo que `0x61a560` suelta antes del pasto no llama a `rand()` directamente
(`0x6070a0`, `0x612230`, `0x612d50`, `0x607b40`, `0x510f70` con `0x401040`, `0x421d60`, `0x4fb1a0` y
`0x414340`, y `0x451d10`/`0x451590`; sin seguir las llamadas virtuales de las telas en `0x4fb1a0`), y
los objetos del 0 y del 2 no lo piden al crearse.

MuRet (`Scenes::ApplyWorldEntry`) no la distingue: corre todo de nuevo sobre el `WorldState` que dura
la sesión, con `LoadMap` siempre y el héroe publicado en `WorldRuntime::hero`, que cambia recién en
`SettleWorldEntry`. Lo reproduce el comportamiento del original, con el anterior vivo y el `+0x358`
en él (`WorldEntryTest.ASecondEntryReloadsAndRedrawsTheHeroSlot`,
`ASecondEntryToTheOtherReferenceMapLoadsItAndKeepsThePreviousHero`,
`TheSecondEntryRecalculatesThe358OfThePreviousHero` y `AnEntryInTheWorldIsAppliedAgainByTheSession`).
Después de un frame del juego, `GameScene::PrepareFrame` deja `WorldRuntime::fullSetScan` sin valor:
una segunda respuesta a otro lugar corta en `0x550990`, con el anterior todavía publicado, el nuevo
construido y el contador como estaba (`AfterAGameFrameASecondEntryElsewhereStopsAt0x550990`, P-C40 c);
al mismo lugar, `0x505820` vuelve a correr con el héroe reconstruido y la entrada sigue
(`AfterAGameFrameASecondEntryOnTheSameSlotScansTheRebuiltHero`). MuRet no tiene en el mundo ni
cuervos ni telas que soltar, ni la lista `0x7245c8`. Los tiles los sube el dibujo del juego
(`TerrainTiles`, con el pasto), y los modelos de los objetos, `WorldModels`. CL7b invalida esos
caches por revisión de carga, incluso al recargar el mismo mapa; la identidad del archivo no
conserva por sí sola el estado mutable de sus modelos. Los dos `0x61f/0` de llegada ya no imponen
el antiguo corte incondicional del suelo y la fuente ya puede crear sus gotas desde la pose vigente;
los casos de suelo pendientes, la pose global retenida, las ramas incompletas del jugador y los ítems
del piso conservan sus fronteras (T8 y [08](08-pendientes.md) P-C55). Un error sigue siendo terminal y
no se presenta ese frame. La comprobación nativa del primer frame y de las reentradas sigue en P-C43.

### El equipo propio en el personaje

`0x512a30(c)` (493 instrucciones, 1.733 bytes, cotejados con el PE `c32bbd5f`; *estático*) pasa el
equipo del registro propio (`[0x7681eb4]`) a un personaje. La llaman la respuesta del `F3 03` (la
fila 5 de la tabla de [La entrada al mundo](#la-entrada-al-mundo)), `0x5bc740` con cada entrada del
`F3 10` y el `1C` ([Los cambios de mapa](#los-cambios-de-mapa)). En orden:

1. Con un tipo (`+2`) que no es `0x20a` vuelve sin abrir ni tocar nada (`0x512a42`).
2. Abre el registro por la tabla de protección (`0x512a48..0x512aba`, [La tabla de
   protección](#la-tabla-de-protección)).
3. Lee los lugares del registro de a `0x48`, con el tipo en `+0xb6c + 0x48 · k`, **solo del 0 al
   8**: del 9 al 11 no lee nada. El modelo es `0x212` más el tipo, o −1 con el tipo en −1; el nivel,
   `(B1 >> 3) & 0xf`, con B1 el byte de `+4` del lugar. Las manos (`+0x2b8` y `+0x2d8`, los lugares
   0 y 1) llevan además el byte `+0x1b` del lugar (a `+0x2bb` y `+0x2db`); las alas (`+0x2f8`, el
   7) y la mascota (`+0x318`, el 8), solo el modelo y el nivel (`0x512abf..0x512b9b`).
4. **Llama a `0x4f2250`** ([Caminar](#caminar), `SetPlayerStop`; `0x512bd4`) salvo en los mapas 18
   a 23 o con la acción (`+0x111`) en `0xc5..0xcc` o en `0x22..0x97` (`0x512ba1..0x512bcd`): la
   acción de parado se elige con las manos, las alas y la mascota nuevas.
5. Con `+0x3e3` distinto de 0 (`0x512bdc`) cierra el registro (`0x512bea..0x512cc6`) y vuelve: no
   toca la ropa.
6. Si no, las cinco partes del cuerpo (`+0x218`, de a `0x20`; los lugares 2 a 6): el modelo o, con
   el lugar vacío, la de la clase (`0x2212`, `0x221a`, `0x2222`, `0x222a` y `0x2232` más `(clase >>
   3) · 5 + (clase & 7)`), el nivel y los bytes `+0x1b` y `+0x1c` (`0x512cc7..0x512ec9`).
7. `0x4053f0` (`0x512ecf`; 44 instrucciones, 139 bytes): en los otros mapas no hace nada. En los 18
   a 23 llama a `0x4053a0` (`0x405407`; 77 bytes), que deja sin ítem (`0xffff`) y con el nivel en 0
   las manos, las alas y la mascota, y en 0 el byte `+0x1b` de las manos (`+0x2bb` y `+0x2db`; los
   de las alas y la mascota quedan). Después elige el arma por `+0x1d4 & 7` (`0x40540c..0x405472`),
   con el nivel en 0: con 1, 3 y 4, `0x222` en las dos manos (el 0 16 de `Item.bmd`, `Sword of
   Destruction`); con 2, `0xa25` en la derecha (el 4 19, `Great Reign Crossbow`); con 0, `0xc17` en
   la derecha (el 5 5, `Legendary Staff`); con 5 a 7, nada.
8. `0x512930` (`0x512ed5`; 56 instrucciones, 205 bytes): la cabeza y la escala, como en la
   selección ([Los personajes de la lista](#los-personajes-de-la-lista)). Las tablas de saltos de
   `0x512a00` y `0x512a14` dan la escala por clase; en las clases 5 a 7 no la toca.
9. Si el personaje es el de `0x743a170` (el héroe publicado; `0x512ee2`), `0x505820` (`0x512ee7`),
   que mira si lleva un conjunto completo +9 y escribe `[0x743a178]` y `[0x743a17c]`; con la clase 3 y
   el conjunto abre otra vez el registro, que sigue abierto: el cierre no saca `rand()` ([El conjunto
   completo de `0x505820`](#el-conjunto-completo-de-0x505820)). Lo que devuelve no lo lee nadie.
10. [`0x550d10`](#los-stats-del-registro-propio) (`0x512ef5`, `thiscall` sobre el registro): los stats.
11. Cierra el registro (en línea, `0x512efa..0x5130cf`).

En la respuesta del `F3 03` el héroe todavía no está en `0x743a170` (lo pone la fila 6), así que esa
primera llamada no corre `0x505820`, salvo que el lugar sorteado sea el del héroe anterior: `0x550990`
lee lo que dejó el último dibujo de los personajes (Q-C188). MuRet (`Scenes::ApplyActorEquipment`) hace
los once pasos: el 9 con `Scenes::ScanFullSet` sobre `WorldRuntime::fullSetScan`, que recibe en
`ActorEquipmentContext::fullSetScan`, y el 10 con `Scenes::RecalculateOwnStats`, que corta con un error
explícito donde falta lo que lee (abajo), también en `0x550990` si MuRet no sabe qué dejó el último
`0x505820`. El cierre del registro gasta los 3.848 `rand()` también cuando corta.

### El conjunto completo de `0x505820`

`0x505820(c)` (`cdecl`, 914 bytes, `0x505820..0x505bb1`, cotejados con el PE `c32bbd5f`; *estático*)
mira si el personaje `c` lleva un conjunto completo de nivel 9 o más y lo deja en dos globales de la
`.bss` (0 al arrancar) que solo escribe ella: `[0x743a178]` (DWORD) y `[0x743a17c]` (byte). Devuelve
en `al` si hay conjunto. En orden:

1. Mira las partes del cuerpo de las botas (`+0x298`) al casco (`+0x218`), de a `0x20`; con la clase
   (`+0x1d4 & 7`) en 3, de las botas a la armadura: el casco no cuenta (`0x50582c..0x505846`). Pone
   `[0x743a178]` en 0 (`0x50584b`).
2. Con alguna en `0xffff` (el modelo, `+0`) no hay conjunto (`0x50585f`). `0x512a30` nunca deja ahí
   `0xffff`: un lugar vacío pone la parte de la clase, cuyo índice no coincide con el de otra parte.
3. El índice de las botas es `movsx(modelo) − 0x212` con el resto con signo de 512 (`and 0x800001ff`
   y el ajuste de `jns`, `0x505871..0x505893`): con un modelo menor que `0x212`, negativo. De las botas
   para arriba, cada parte tiene que tener el nivel (`+2 & 0xf`) en 9 o más (`jl`, `0x5058b4`) y el
   mismo índice (`0x5058b6..0x5058d1`), de cualquier grupo; si no, `[0x743a178]` vuelve a 0 y no hay
   conjunto (`0x5058ea`). Cada nivel menor o igual que el más chico hasta ahí lo escribe en
   `[0x743a178]` (`0x5058d3..0x5058db`): al final, el más chico.
4. Pone `[0x743a17c]` en 1 (`0x5058fb`). Con la clase 3 y el conjunto abre el registro propio por la
   tabla de protección (`0x50591f..0x505a96`, [La tabla de protección](#la-tabla-de-protección)),
   mira el tipo de su armadura (`[0x7681eb4] + 0xc44`, el lugar 3) y, si no es `0x100f`, `0x1014`,
   `0x1017` ni `0x1020`, pone `[0x743a17c]` en 0 (`0x505aa7..0x505ac3`). Después lo cierra
   (`0x505aca..0x505ba3`), con los 3.848 `rand()` si deja la cuenta en 0. Con la tabla llena, la
   apertura avisa `Hash table full!!! - GetIndex` (`0x6c637c`, por `0x41fa40` en `0x5059b3`) y el
   cierre vuelve sin cerrar (`0x505b55`), como en los otros accesos en línea.

No lee nada más: ni las tablas de los conjuntos (`[0x6eed3c]`) ni `Item.bmd`. La armadura que mira es
la del registro propio, no la del personaje: dibujar a otro jugador de la clase 3 con un conjunto mira
la del héroe (Q-C188). En el `Item.bmd` del repo, las cuatro son el 8 15, el 8 20, el 8 23 y el 8 32
(`Storm Crow`, `Thunder Hawk`, `Hurricane` y `Volcano Armor`), las únicas del grupo 8 con nombre sin
casco con nombre en el mismo índice del grupo 7 (`GoldenFullSetScan`): que sean los conjuntos de la
clase 3, que no lleva casco, es *inferido*.

**Quién la llama.** `0x512a30` con el héroe publicado (el paso 9 de [arriba](#el-equipo-propio-en-el-personaje)),
y `0x50ba30`, el dibujo de un personaje, en `0x50c904..0x50c926`: con el tipo (`+2` del objeto) en
`0x20a` y `+0x3e4` en 0. Lo que devuelve decide un bloque del mismo dibujo (`0x50fde6..0x510a7a`), que
también lee `[0x743a178]`, sin leer en detalle ni portar. `0x50ba30` llega a `0x50c904` salvo que salga
antes: con el bit `0x10000000` de `+0x7c` o sin las acciones del modelo (`+0x26` en 0, `0x50ba87`). La
llaman `0x510d00` (`0x510ea1`), que recorre el pool del 0 al 399 en orden con los vivos y visibles
(`+0x16c`), en el login, la selección y el juego; el inventario (`0x4959d5`, con `+0x3e4` en 1 en
`0x4959a1`) y la vista previa de crear (`0x63e637`, con el `NewFace` de la clase de tipo, `0xa8` a
`0xac`) no la corren. En el juego, `0x6374dd` llega a `0x510d00` en cada frame con el mundo cargado y el
contador de la espera en 30 o menos.

Así, lo que lee `0x550990` depende de quién se dibujó último. En la respuesta del `F3 03`, si el lugar
sorteado es el del héroe publicado (el 0 de WinMain, que es el del primer lugar de la selección, o el
de la entrada anterior), `0x505820` corre con el héroe recién construido y no importa lo de antes; si
no, cuenta lo del último personaje de la lista en el último frame que dibujó la selección. Sin ningún
dibujo, el 0 de la `.bss`.

MuRet (`Scenes/FullSetScan.h`): `ScanFullSet` sobre un `ScannedCharacter` (la clase y las cinco
partes; `ScannedCharacterOf` de un `ActorState` o de la `CharacterAppearance` de la lista y del
login), con el acceso al registro como `Render::Models::LogicalAccess` (sin el aviso de la tabla
llena). Los dos globales son `WorldRuntime::fullSetScan`, que arranca en 0 y 0 y dura entre escenas.
Lo escriben `ApplyActorEquipment` con el héroe publicado y el dibujo del login y de la selección
(`ScanTheDrawnCharacter`), con cada personaje en el orden del pool, antes de `DrawCharacter`, una vez
por frame del original (`OriginalFrameCarry`, como la tela) y solo si están las acciones de
`Player.bmd`. MuRet no dibuja los personajes del juego (corta en el primero visto, `0x510ea1`):
`GameScene::PrepareFrame`, en un frame que llega a `0x510d00`, lo deja sin valor (`nullopt`), y
`0x550990` corta con un error si lo lee antes de otro `0x505820` (P-C40). El bloque del dibujo que decide
lo que devuelve no está portado.

### Los stats del registro propio

`0x550d10` (1.698 bytes, cotejados con el PE `c32bbd5f`; *estático*) es un `thiscall` sobre el
registro propio (`ecx = [0x7681eb4]`, `0x512eef`), que guarda en `ebx` para todo lo demás. Lo que
devuelve en `eax` no lo lee nadie: `0x512a30` lo pisa en `0x512efa`. En orden:

1. `0x40e1b0` con `ecx = [0x6eed3c]` (`0x550d24`), las opciones de los conjuntos (abajo).
2. Los 16 bytes de `[0x6eed3c] + 0x937c` en 0 (`0x550d29..0x550d3f`).
3. `0x54ef00` (`0x550d44`; 31 bytes): los WORD `+0x2a`, `+0x2c`, `+0x2e`, `+0x30`, `+0x36`, `+0x32`
   y `+0x34` del registro en 0.
4. Los doce lugares del equipo (`+0xb6c`, de a `0x48`, también del 9 al 11; `0x550d49..0x550db1`):
   con la durabilidad (`D+0x1a`) distinta de 0, `0x40dc50(lugar, &v)` (`0x550d6d`; 66 bytes; `ecx =
   [0x6eed3c]`, que no usa) escribe en `v` el WORD `(D+0x1c >> 2) & 3` y devuelve el byte `+0x44`
   del registro de `Item.bmd` del tipo, leído con signo (`*[0x7758550] + tipo · 0x54`, `movsx` en
   `0x40dc77`). Con 1, 2, 3 o 4 suma `5 · v`, en 16 bits, a `+0x2a`, `+0x2c`, `+0x30` o `+0x2e` (la
   tabla de saltos de `0x5513b4`); con otro valor, nada. En el `Item.bmd` del repo, el `+0x44` vale
   1 en 57 registros (los grupos 0 a 3, como `Kris`, y 3 del 13), 2 en 20 (el grupo 4 y 3 del 13), 3
   en 12 (el grupo 5, `Rune Blade` y 2 del 13) y 4 en 161 (los grupos 6 a 11 y 2 del 13); en los
   demás, 0. Con el tipo en −1 y la durabilidad distinta de 0 lee fuera de la tabla (Q-C184).
5. `0x40da20` sobre `[0x6eed3c]` cinco veces (`0x550dc2..0x550e12`; 151 bytes): `+0x2a` con `0x8c`,
   `+0x2c` con `0x8d`, `+0x30` con `0x8e`, `+0x2e` con `0x8f` y `+0x36` con `0x90`. Suma al WORD,
   una sola vez cada uno, los valores de la lista que dejó `0x40e1b0` (de a 3 bytes desde `+0x934c`,
   tantos como `+0x9348` más `+0x9349`; los valores de a 8 desde `+0x938c`) cuyo primer byte es el
   selector menos `0x8c`, y los marca en `+0x937c`, lo que el paso 2 puso en 0.
6. Si el lugar 7 (`[0x7681eb4] + 0xd64`) tiene el tipo `0x1a1e` (el 13 30, `Cape of Lord`;
   `0x550e17..0x550e30`), abre el registro (`0x550e36..0x551038`), llama a `0x54e540(+0x36, 0x90,
   lugar 7)` (`0x551051`; `Scenes::AddOwnItemOptionValues`) y lo cierra (`0x551056..0x551284`, con
   los 3.848 `rand()` si la cuenta llega a 0). Desde `0x512a30` el registro ya está abierto y ese
   cierre no gasta.
7. Ocho helpers con `ecx` en el registro (`0x551287..0x5512ba`): `0x54efe0`, `0x54f9b0`,
   `0x54fd40`, `0x54fe10`, `0x5507e0`, `0x550990`, `0x550c30` y `0x550c60` ([Los ocho helpers de
   los stats propios](#los-ocho-helpers-de-los-stats-propios)). Escriben los WORD `+0x4c` a `+0x66`
   y `+0xef4`; ninguno escribe `+0xee2..+0xef2`, que lee la cola.
8. La cola (`0x5512bf..0x5513b1`), con WORD que envuelven:
   - `+0xef8 = +0xef0 − +0xee6` y `+0xefa = +0xef2 − +0xee6`;
   - un `rand()` (`0x5512f2`) y `+0xefc = rand() % (+0xee4 − +0xee2 + 1) + +0xee2 − +0x62`: los dos
     WORD sin signo y el `idiv` de 32 bits con signo. Con `+0xee2 = +0xee4 + 1` divide por 0
     (Q-C185); con `+0xee2` mayor, el divisor es negativo y el resto es el `rand()` entero;
   - `+0xefe = +0x4e − +0xeec` y `+0xf00 = +0x60 − +0xeea`, cada uno con tope 100 si pasa de 100 sin
     signo (`jbe`);
   - otro `rand()`: `+0xf02` en 1 si `rand() % 100` es menor que `+0xefe` (`jge`), si no en 0; y
     otro, igual, para `+0xf03` con `+0xf00`.

`0x40e1b0` (1.290 bytes; *estático*, cotejado con el PE `c32bbd5f` en la CL7 hito 1, T10b) es un
`thiscall` sobre `[0x6eed3c]`, el objeto de las [tablas de los
conjuntos](05-formatos-de-datos.md#las-tablas-de-los-conjuntos) que carga `0x40c600`. Usa una lista
local de 30 bytes en 0 (`0x40e1bb..0x40e1d4`), de la que `0x40cfa0` y `0x40d010` recorren solo las
cuatro primeras entradas de 3 bytes: el conjunto, cuántas piezas suyas se contaron y la clase de
pieza de la última. En orden:

1. Los 16 bytes de `+0x934c` en 0 (`0x40e1ce..0x40e1e3`): cinco entradas enteras de la lista de
   opciones y el primer byte de la sexta.
2. La primera pasada (`0x40e1e6..0x40e3a1`): los lugares 0 a 11 salvo el 7 y el 8, y en cada uno
   abre y cierra el registro por la tabla de protección (`0x57630c8`; el cierre, `0x40b4d0`). El 1 y
   el 11 no cuentan si tienen el mismo tipo y el mismo `+0x1c & 3` que el 0 o el 10
   (`0x40e27a..0x40e2b3`), que se guardan en `-0x7c` y `-0x60` al pasar por el 0 y el 10, cuenten o
   no; al entrar no tienen valor, pero el 0 siempre los escribe antes de que el 1 los lea. Un tipo
   negativo (−1, el lugar vacío) no cuenta (`0x40e301`); los demás van a `0x40cfa0(lista, tipo,
   +0x1c)` (`0x40e31b`).
3. `0x40d010(lista)` (`0x40e3ae`).
4. Abre el registro, copia la fuerza, la agilidad, la vitalidad y la energía (`+0x18`, `+0x1a`,
   `+0x1c` y `+0x1e` de `[0x7681eb0]`, el mismo bloque) y lo cierra (`0x40e3b3..0x40e494`), y llama a
   `0x40de10` con las cuatro copias (`0x40e4ac`).
5. La lista local otra vez en 0 y los 16 bytes de `+0x934c` en 0xff (`0x40e4b1..0x40e4cc`).
6. La segunda pasada (`0x40e4cf..0x40e6a1`), como la primera, pero antes de mirar el par salta los
   ítems con un requisito mayor que su copia, sin signo: `+0x20` contra la agilidad, `+0x22` contra
   la energía y `+0x1e` contra la fuerza (`0x40e563..0x40e592`). Un ítem saltado no escribe `-0x7c`
   ni `-0x60`, que vienen de la primera pasada: si el 0 no da sus requisitos, el 1 se compara con el
   10. El cierre de los que cuentan es `0x40f370` (`0x40e658`), con lo mismo que los otros.
7. `0x40d010(lista)` (`0x40e6ae`).

Son 33 cierres: 10, 1, los 12 de `0x40de10` y 10. Con `0x512a30` afuera, ninguno gasta.

`0x40cfa0` (100 bytes) toma el conjunto del byte `(+0x1c & 3) − 1` del registro del tipo en
`ItemSetType.bmd` (`[0x6eed3c] + 4 + tipo · 4`, sin mirar que el tipo esté en la tabla); con `& 3` en
0, o con 0 o 0xff, nada. Busca en las cuatro entradas la de ese conjunto o la primera libre
(`0x40cfd2..0x40cfe3`), le suma una pieza y le pone la clase de pieza `(+0x1c & 3) − 1`; sin lugar,
nada.

`0x40d010` (1.298 bytes) lee la clase del héroe publicado, `[0x743a170] + 0x1d4` (`& 7` la clase y
`>> 3` la evolución), y pone en 0 `+0x9307`, los dos nombres (`+0x9308` y `+0x9328`), los conjuntos
(`+0x934a`, `+0x934b`), las cuentas (`+0x9348`, `+0x9349`) y el `+0x358` del héroe
(`0x40d01d..0x40d07d`). Por cada entrada con dos piezas o más (`0x40d08e`), con n = piezas − 1:

- la bandera de la entrada (`0x40d0d0..0x40d14d`): 1 en las clases 0 a 2 si su marca
  (`+0x47 + clase`) es 1, o 2 con evolución; en la 3, solo con 1; en la 4, nunca;
- el conjunto a `+0x934a + clase de pieza`, y el nombre (`0x40d16d..0x40d233`): con el de su clase
  vacío o igual (`strcmp`), `strcpy` ahí; si no, al primero vacío de los dos o al segundo, con
  `+0x9307` en n. `strcpy` copia hasta el 0 y deja lo que sigue;
- las opciones de los pares y las de `+0x38` ([05](05-formatos-de-datos.md#las-tablas-de-los-conjuntos);
  `0x40d233..0x40d358`) y, con n ≥ `+0x3c` − 2, las cinco del completo, cada una con `+0x9307` + 1
  si no era 0, y el `+0x358` del héroe en 1 (`0x40d35e..0x40d457`). Una opción 0xff no entra; una de
  0x18 en adelante, solo con la marca de la clase distinta de 0 y la evolución ≥ marca − 1
  (`0x40d29c..0x40d2bd`). Van a tres listas locales de 16 por clase de pieza (la opción, la bandera
  y el valor), sin mirar si caben.

Al final (`0x40d473..0x40d515`) copia a `+0x934c` (de a 3: la opción, la bandera y un byte que no
escribe) las de la clase de pieza 0 y detrás las de la 1, cada valor a los dos DWORD de `+0x938c +
8 · i`, y las cuentas a `+0x9348` y `+0x9349`.

`0x40de10` (898 bytes) recorre los doce lugares, abriendo y cerrando el registro en cada uno; con la
durabilidad distinta de 0, `0x40dc50` y, por la tabla de saltos de `0x40e194`, suma `5 · v` a la
copia de la fuerza (1), la agilidad (2), la energía (3) o la vitalidad (4). Después, `0x40da20` con
`0x8c`, `0x8d`, `0x8e` y `0x8f` sobre esas copias (`0x40e14c..0x40e17c`), que deja marcadas esas
opciones: por eso el paso 2 de `0x550d10` borra las marcas.

`0x40da20` (151 bytes) es el código de `0x40d690` ([abajo](#los-ocho-helpers-de-los-stats-propios)):
recorre las `+0x9348 + +0x9349` opciones de la lista; las que valen `selector − 0x8c` y no están
marcadas en `+0x937c` suman su primer DWORD y quedan marcadas, y con la suma distinta de 0 hace
`add word` con sus 16 bits de abajo (`0x40daac`).

Con las tablas del repo, ningún conjunto llega a la clase de pieza 1 o 2 ni a una opción de 0x18,
y lo más que se junta son 16 opciones. Lo que el original lee o escribe fuera de sus tablas y listas
con otros datos es Q-C187.

MuRet (`Scenes::RecalculateOwnStats`) corre los pasos 1 a 6 en orden: `RecalculateOwnItemSets`
(`0x40e1b0`, con `AddItemSetPiece`, `ApplyItemSetOptions` y `AddOwnBonusesToStatCopies`; en
`Scenes/OwnItemSets.h`), `ClearItemSetOptionMarks`, `ClearOwnItemStatWords`, `AddOwnItemStatBonuses`,
`AddOwnItemSetOptionValues` (`0x40da20` cinco veces, con `Scenes::AddItemSetOptionValues`, el de
`0x40d690`) y `AddCapeOfLordOptions`, con los accesos al registro donde los abre y los cierra el
original. Lo de `[0x6eed3c]` que dura de una llamada a otra es `Scenes::ItemSetState` (en
`WorldState`): `+0x9307` y los nombres, y la lista de `+0x9348..+0x940b` como
`Scenes::ItemSetOptionList`, la misma que reciben los helpers. El `+0x358` del héroe es
`ActorState::byte358`. Sin las tablas o sin ese estado corta en el paso 1, antes de tocar nada. Si
no, sigue con los ocho helpers en su orden ([abajo](#los-ocho-helpers-de-los-stats-propios)) y la cola
(paso 8, `UpdateOwnDerivedStatsTail`), con lo que `OwnStatsContext` trae para ellos: el ítem de
`0x7879f80` (`held`, que por defecto no reemplaza nada) y lo de `0x505820` (`scan505820`).

`[0x743a178]` y `[0x743a17c]` solo los escribe `0x505820` (`0x50584b`, `0x5058db`, `0x5058ea`,
`0x5058fb` y `0x505ac3`; *estático*), desde `0x512a30` y desde el dibujo de los personajes ([El
conjunto completo de `0x505820`](#el-conjunto-completo-de-0x505820)): `0x550990` lee lo que dejó la
última llamada. `ApplyActorEquipment` le pasa `WorldRuntime::fullSetScan`, después de escribirlo si
viste al héroe publicado. Sin valor (un frame del juego en el medio, que MuRet no dibuja),
`RecalculateOwnStats` corta en `0x550990` (`0x5512ac`), con los cinco helpers de antes hechos; con un
valor, llega al final de la cola. En la entrada al mundo, con las tablas de los conjuntos cargadas,
`0x512a30` vuelve entero.

`[0x7879fbc]`, que decide si hay ítem en `0x7879f80`, está en la `.bss` (0 al arrancar) y fuera de
`0x550d10` solo lo escriben `0x4d1c89` y `0x4d29cb` con un valor y varios sitios con 0 (*estático*):
al entrar al mundo por primera vez vale 0, y `held` por defecto es eso. `+0xee2..+0xef2`, que lee la
cola, se ponen en 0 en el login con el resto del registro (`0x4baf79..0x4bafc2`: `new(0xf08)` y
`rep stos` de `0x3c2` DWORD) y nadie más los escribe por desplazamiento (P-C38 e): en MuRet quedan en
0.

### Los ocho helpers de los stats propios

`0x550d10` llama en `0x551287..0x5512ba`, en este orden, a `0x54efe0` (2.304 bytes), `0x54f9b0`
(873), `0x54fd40` (193), `0x54fe10` (2.471), `0x5507e0` (425), `0x550990` (653), `0x550c30` (46) y
`0x550c60` (63), todos `thiscall` con `ecx` en el registro propio (cotejados con el PE `c32bbd5f`;
*estático*). Abajo, S es `+0x2a` más `+0x18` (la fuerza), A `+0x2c` más `+0x1a`, V `+0x2e` más
`+0x1c`, E `+0x30` más `+0x1e` y L `+0x36` más `+0x20`, sumados en 16 bits; la clase es `+0xb & 7`.
Las divisiones por constantes son de WORD sin signo (`imul` por el número mágico) y todo lo que
escriben son WORD que envuelven. Las opciones de un ítem van por `0x54e540(w, tag, ítem)`, que suma
el valor de cada opción con ese tag si el ítem tiene tipo y durabilidad, y `0x54e590(w, tag, ítem,
f)`, que suma `w · f / 100` (`imul` de 32 bits con signo) por cada una, si tiene tipo. "Reducido"
es `v − _ftol(v · x)`, con `x` lo que devuelve `0x54ee00` y el `_ftol` de `0x69fd3c`.

`0x54ee00` (`cdecl`, 187 bytes) recibe la durabilidad, el byte `+0x2d` del registro de `Item.bmd` del
tipo (`movsx` del tipo, `*[0x7758550] + tipo · 0x54`), `D+4`, `D+0x1b` y `D+0x1c`, y devuelve un
`float`. El máximo es el `+0x2d` más 1 por cada uno de los primeros 4 niveles (`(D+4 >> 3) & 15`) y
2 por cada uno de los otros, más 20 con `D+0x1c % 4` en 1 o 2 o, si no, 15 con `D+0x1b & 0x3f`.
`r = 1 − durabilidad / máximo` (`fild`, `fidiv` y `fsubr`): 0.5 con `r > 0.8f`, 0.3f con `r > 0.7f`,
0.2f con `r > 0.5f` y 0 si no. Con el perfil PC53 ([ADR 0012](../../../docs/adr/0012-perfil-fp-del-movimiento-original.md))
cada paso redondea como en doble: 6 de 20 deja `r = 0.7` en doble, que pasa `0.7f`, y da 0.3f (con
PC24 daría 0.2f). Con el máximo en 0 la x87, con las excepciones enmascaradas, da infinito o NaN y
ninguna comparación pasa: 0. Con las alas los que llaman pasan 0 y 0 en lugar de `D+0x1b` y
`D+0x1c`.

`0x54efe0`, `0x54f9b0` y `0x54fe10` leen el ítem de `0x7879f80` en lugar del de un lugar con
`[0x7879fbc] > 0` (con signo), `[0x78ab9b8] == 0x78a9bb0` y el lugar en `[0x78a6758]` (sin signo,
`ja` con 11), si está en su tabla de saltos: los lugares 0, 1, 9, 10 y 11 en `0x54efe0`; 0, 9, 10 y 11
en `0x54f9b0`; 0, 1, 5, 10 y 11 en `0x54fe10`. Qué son esos globales no está leído (P-C38 d).

1. `0x54efe0`, `+0x50`, `+0x52`, `+0x54` y `+0x56`:
   - con una ballesta en la derecha (`0x808..0x80e` o `0x810..0x9ff`, con signo) o un arco en la
     izquierda (`0x800..0x806`, `0x811`, `0x814` o `0x815`), con durabilidad, `S/14 + A/7` y `A/4 +
     S/8` (`0x54f09b..0x54f108`); si no, por la clase (la tabla de saltos de `0x54f904`): en la 1,
     `S/6` y `S/4`; en la 2, `(S + A)/7` y `(S + A)/4`; en la 3, `S/6 + E/12` y `S/4 + E/8`; en la 4,
     `S/7 + E/14` y `S/5 + E/10`; en las otras, `S/8` y `S/4`. `+0x54` y `+0x56` copian a `+0x50` y
     `+0x52`;
   - las alas (el lugar 7, sin reemplazo), con tipo y el `+0x2d` distinto de 0 (no mira la
     durabilidad): sus opciones `0x50`, reducidas, a los cuatro (`0x54f2f8..0x54f3c0`);
   - la derecha, con tipo y durabilidad: `D+0xc` y `D+0xe` más sus `0x50`, reducidos, a `+0x50` y
     `+0x52`, o a `+0x54` y `+0x56` con un tipo de `0xa00` a `0xc00`; después sus `0x5d` y sus `0x5e`
     por 2 a `+0x50` y `+0x52`. La izquierda, igual, a `+0x54` y `+0x56`;
   - el lugar 9: `0x5d` y `0x5e` por 2 a los dos pares; el 10 y el 11: `0x5e` por 10 a los cuatro;
   - la lista de los conjuntos (abajo): `0x40d690` con `0x91` a `+0x54` y `+0x50` y con `0x92` a
     `+0x56` y `+0x52`, y `0x40d7f0` con A (`0xa5`) y con S (`0xa4`) a los cuatro;
   - con las dos manos de `0x800` a `0x9ff`: Bolt (`0x807`) con nivel en la izquierda deja `w +
     _ftol(w · (2 · nivel + 1) · 0.01f + 1.0f)` en `+0x50` y `+0x52`; si no, Arrow (`0x80f`) con
     nivel en la derecha, en `+0x54` y `+0x56` (`0x54f7b0..0x54f8d6`). Con el `float`, 160 con nivel
     2 da 168 y no 169.
2. `0x54f9b0`: `+0x5a = E/9` y `+0x5c = E/4`; las alas, con tipo (no mira el `+0x2d` ni la
   durabilidad), sus `0x51` reducidas; la derecha, con tipo y durabilidad, sus `0x51` (`0x50` con el
   0 21, el 0 23 y el 0 31) reducidas, y `0x5f` y `0x60` por 2; el lugar 9, `0x5f` y `0x60` por 2, y
   el 10 y el 11, `0x60` por 10. Al final (`0x54fccf..0x54fd0f`) `0x40d690` con `0x93` sobre `+0xef4`,
   que no pone en 0 antes (Q-C190), suma `+0xef4` a los dos, lo pone en 0, `0x40d7f0` con E y `0xa8`
   sobre `+0xef4`, y lo suma otra vez.
3. `0x54fd40`: `+0x4e` es `S/6 + L/10 + 5 · A/2 + 5 · nivel` en la clase 4 y `3 · A/2 + 5 · nivel +
   S/4` en las otras (el nivel, `+0xe`, por 5 en 16 bits); después `0x40d690` con `0x95`.
4. `0x54fe10`: `+0x4c` y `+0x58` (abajo, las dos), las velocidades del keepalive
   ([04](04-protocolo.md), el `0E`; P-C28 i): A sobre 50 y 50 en la clase 2, 15 y 20 en la 1 y la 3,
   10 y 10 en la 4, y 20 y 10 en las otras (`0x54fe1e..0x54fefb`). Después:
   - el `D+0x17` de cada mano, o 0 sin durabilidad, si el tipo es de 0 a `0xbff` (con signo) y no es
     `0x807` ni `0x80f`; con las dos, el promedio (Q-C193);
   - Wizard's Ring (`0x1a14`) con durabilidad y `D+4 >> 3` (`sar`, sin la máscara del nivel) en 0 o
     3, en el lugar 11 o en el 10: 10 a las dos y el bit `0x04` de `+0x3c` prendido, entre una
     apertura y un cierre del registro en línea (`0x550075..0x5506fb`; el cierre, con los 3.848
     `rand()` si la cuenta llega a 0). Sin anillo y con el bit prendido, lo apaga igual. El bit se lee
     antes de abrir;
   - los guantes (el lugar 5), con durabilidad y tipo, su `D+0x17`; con el bit `0x01` o el `0x08` de
     `+0x3c`, 20;
   - las `0x61` de los lugares 0, 1, 9 y 8 del registro, sin reemplazo (`0x55073b..0x5507ad`).
5. `0x5507e0`: `+0x60` es A/4 en la clase 2, A/7 en la 4 y A/3 en las otras. Con el lugar 1 con tipo
   y durabilidad suma su `D+0x10` reducido y sus `0x52` reducidas, y `0x5a` por 10; después, `0x5a`
   por 10 de los lugares 2 a 6, 11 y 10 (sin mirar la durabilidad).
6. `0x550990`: `+0x62` es A/10 en la clase 2, A/3 en la 1, A/4 en la 0, A/7 en la 4 y A/5 en las
   otras. Suma, en 32 bits, los lugares 1 a 7 con durabilidad: `0x54e680` (`D+0x12` más sus `0x53`, o
   0 con el tipo −1) reducido, las alas con 0 y 0; el `+0x2d` lo lee también con el tipo −1
   (Q-C192). Con `[0x743a17c]` distinto de 0 (lo deja `0x505820`), `w + _ftol(w · f)` con f 0.05f,
   0.1f, 0.15f o 0.2f para `[0x743a178]` de 10 a 13 y 0 para los otros. Después `0x40d690` con
   `0x96`, `0x40d7f0` con A (`0xa6`) y con V (`0xa7`) y, con un tipo de `0xc00` a `0xdff` en el lugar
   1, `0x40d730` con `0xa2`; al final, las `0x96` del lugar 8 y, si el 7 es Cape of Lord, las suyas.
7. `0x550c30`: `+0x64` es `0x54e6b0` (`D+0x14`, o 0 con el tipo −1) del último de los lugares 2 a 7
   con durabilidad. Sin ninguno, queda como estaba (Q-C191).
8. `0x550c60`: `+0x66` es `0x54e6d0` (`D+0x18`) del lugar 6 con durabilidad, más el del 7 con
   durabilidad. Sin durabilidad en el 6 no lo pone, y el del 7 se suma a lo que había (Q-C191).

La lista de los conjuntos es de `[0x6eed3c]`: `+0x9348` y `+0x9349` dicen cuántas entradas hay (la
suma), `+0x934c` tiene 16 de 3 bytes (el primero es el selector menos `0x8c` y el segundo lo pide
`0x40d7f0`), `+0x937c` 16 marcas y `+0x938c` 16 valores de 8 bytes (un DWORD con signo). Las tres
funciones son `thiscall` sobre `[0x6eed3c]` y toman cada entrada sin marcar del selector una sola
vez, marcándola:

- `0x40d690` (151 bytes; `0x40da20`, la del paso 5, es el mismo código salvo las direcciones) suma
  los valores en 32 bits y le suma al WORD los 16 de abajo;
- `0x40d730` (185) suma los valores en S y hace `w += w · S / 100`, con `imul` e `idiv` de 32 bits
  con signo;
- `0x40d7f0` (197) toma solo las que tienen el segundo byte distinto de 0 y hace `w += (base · V /
  100) · N`, con V el valor de la última y N cuántas fueron.

Con más de 16 entradas las tres leerían las marcas como entradas. Ninguno de los ocho escribe los
WORD `+0xee2..+0xef2`, que la cola de `0x550d10` lee: en `.text` no hay otra escritura con esos
desplazamientos, y `0x54efc0`, que copia 7 DWORD a `+0xed4`, no tiene referencias en el PE (P-C38 e).

MuRet (`Scenes::OwnStatHelpers`) tiene una función por helper (`UpdateOwnWords50To56`,
`UpdateOwnWords5aAnd5c`, `UpdateOwnWord4e`, `UpdateOwnAttackAndMagicSpeeds`, `UpdateOwnWord60`,
`UpdateOwnWord62`, `UpdateOwnWord64` y `UpdateOwnWord66`) y `ItemDurabilityFactor`, y
`Scenes::ItemSetOptionList` las tres de la lista, sobre una proyección tipada de `+0x9348..+0x940b`.
El ítem de `0x7879f80` y sus condiciones van en `OwnEquipmentOverride`, que por defecto no reemplaza
nada, y lo de `0x505820`, en `Scan505820` (`Scenes/FullSetScan.h`); el bit `0x04`, con un
`Render::Models::LogicalAccess`.
Donde el original lee lo que MuRet no tiene devuelven un error, con lo escrito hasta ahí: sin
`Item.bmd` o con un tipo fuera de la tabla, sin la lista de `0x40e1b0`, con más de nueve opciones en
un ítem o más de 16 entradas en la lista.

### El F3 10, el inventario

Leído en la T9 del hito 1 de la CL7 (*estático*, cotejado con el PE `c32bbd5f`): `0x4c55a0` (661
instrucciones), `0x5bc740` (hasta `0x5bd70c`) y `0x54b3a0`. `0x4e309c` llama a `0x4c55a0(pkt, cifrado)`
con el mismo flag que el `F3 03` en `0x4e306b`; devuelve 1 por la rama cifrada y 0 por la otra.

**La inicialización común** (`0x4c55c4..0x4c56e0`) corre antes de mirar el flag (`0x4c56e2`): también
sin cifrar y con la cantidad en 0.

1. Abre el registro propio (`0x40b240`; `+0xf08` más 1, y `0x40b440` si pasa a 1; si no está, `new
   0xf09` con la cuenta en 1 y `0x40ad90`).
2. En los 12 lugares del equipo (desde `+0xb6c`, de a `0x48`; `0x4c563c..0x4c565f`) pone el tipo en
   −1, el dword de `+0x3c` y el byte de `+0x1b` en 0. No es el vaciado de la entrada (`0x4c47c8`, que
   pone en 0 el dword de `+4`): este deja `+4`, y ninguno de los dos toca la durabilidad de `+0x1a`
   (Q-C203).
3. Lo cierra (`0x4c5661..0x4c56a3`, `0x40b4d0`): 3.848 `rand()` si lo deja sin accesos.
4. Ya cerrado, lo mismo en las dos grillas de la `.bss`, que WinMain no toca: las 64 casillas de
   `0x78a9bb0` (`0x4c56a8..0x4c56c3`, hasta `0x78aadec − 0x3c`) y las 32 de `0x78ab0b8`
   (`0x4c56c5..0x4c56e0`, hasta `0x78ab9f4 − 0x3c`), de `0x48` bytes. Qué muestra cada una en la
   interfaz no está leído (CL9); la segunda podría ser la tienda personal (*inferido* del tamaño).

**Sin cifrar** (`0x4c56ed..0x4c5e3c`) arma el `C1 06 F1 03 00 F3` con la tabla XOR32 y lo manda en C3
con el serial de `0x5767913` (la entrada de un byte de la tabla de protección, `0x40b3b0` y `0x40b3f0`)
y un `rand()` de relleno (`0x4c5a8e`), sin leer el paquete: el mismo aviso que el `F3 03` sin cifrar
(`0x4c3d14` es su `rand()`). El inventario queda vacío (Q-C205).

**Cifrado** (`0x4c5e4f..0x4c5eb6`): `0x443d40` con el héroe publicado (`[0x743a170]`) apaga las
entradas vivas de la tabla de ayudantes de `0x7d335d0` (10 de `0x1d4`) cuyo jinete (`+0x108`) es él, y
`0x421d60` suelta su cuervo (`+0x430`, con `0x69eff0`). Después, por cada entrada (la cantidad, un u8
en `p+5`, se relee después de cada llamada; `i` con signo; desde `p+6`, de a 8):
`0x5bc740(0x78a9bb0, 8, 8, entrada[0], entrada + 1, 1)`. No mira si un lugar se repite.

**El tipo**, `0x54b3a0(d)`: `d[0] + 2 · (((d[5] & 0xf0) << 4) + (d[3] & 0x80))`, es decir el byte 0, el
bit `0x80` del byte 3 como bit 8 y el nibble alto del byte 5 como bits 9 a 12. Nadie lee `d[6]` ni el
nibble bajo de `d[5]`.

**`0x5bc740`** con esa grilla y el último argumento en 1. Con el tipo `0x1fff` vuelve sin tocar nada
(`0x5bc75d`). Con otra grilla toma otro camino, que el `F3 10` no usa. Al final (`0x5bd3f2`), con
`0x78a5a20` llamaría a `0x5bc470`, y con el último argumento distinto de 0 vuelve.

- **Del lugar 0 al 11, el equipo** (`0x5bc77f..0x5bcc0c`, con signo). Abre el registro propio en línea,
  y en el ítem de `+0xb6c + lugar · 0x48` escribe `+0x1a` = d[2] (la durabilidad), `+0x1c` = d[4], el
  tipo, `+0x1b` = d[3] y `+0x3c` = 1. Llama a `0x54bf40(ítem, B1, B3, B4)` con B1 = d[1] y B4 = d[4]
  sobre los 24 bits de arriba del puntero del registro (`0x54bf40` usa el byte de abajo de B1 y `B4 &
  3`) y B3 = d[3] sobre los de `lugar · 9` (0 del 0 al 11). Después `0x512a30([0x743a170])`, el héroe
  publicado, no el de `0x57678b0` (Q-C204), y con el héroe que relee de `0x743a170`:
  - el **lugar 8** fuera de los mapas 18 a 23 (`0x5bc8a0..0x5bc8ad`): un `switch` sobre el tipo menos
    `0x1a00` (`0x5bd728`, `0x5bd710`). Con `0x1a00` (Gaurdian Angel en `Item.bmd`),
    `0x443fa0(0x1c12, héroe + 0x10, héroe, 0, 0)`; con `0x1a02`, `0x1a03` y `0x1a04` (Horn of Unilia,
    Horn of Dinorant y Dark Horse), lo mismo con `0xc6`, `0x110` y `0xbf`; con `0x1a25` (Horn of Doom,
    el de Fenrir) copia antes el `+0x1b` del ítem a `+0x31b` del héroe (el de su mascota) y usa `0x14a`
    con 1, `0x14c` con 2 y `0x14b` con otro. Con esos cuatro, si `+0x3e2` (la zona segura) es 0, además
    `0x518cf0(0x61e, héroe + 0x10, héroe + 0x1c, héroe + 0xf4, 1, héroe, −1, 0, 0, 0, 0)`. Así se
    crea el caballo en el juego ([El caballo oscuro](#el-caballo-oscuro)). Con `0x1a01` y los otros,
    nada;
  - el **lugar 1** con `0x1a05` (Dark Raven): `0x420cf0(héroe)`, que suelta el cuervo con `0x421d60` y,
    fuera de los mapas 18 a 23, crea otro con `new 0x460` y `0x4167b0` en `+0x430`;
  - el **lugar 11**, o el **10** con `+0x360` del héroe fuera de 1 a 3, con `0x1a14` (Wizard's Ring)
    de nivel 3 (`+4 & ~7 == 0x18`): `0x414340(héroe)` (suelta `+0x364` y pone `+0x364` y `+0x360` en
    0) y `+0x360` = 4.

  Lo cierra en línea (`0x5bcaae..0x5bcc0c`, el lazo de `0x40b4d0` con su `rand()` en `0x5bcbd5`). Los
  accesos de `0x512a30` y de `0x550d10` anidan en este.
- **Del 12 al 75, la grilla de `0x78a9bb0`** (`0x5bcc11..0x5bcd05`), y **del 76 en adelante, la de
  `0x78ab0b8`** (`0x5bcf99..0x5bd085`, sin tope arriba). El origen es el lugar menos 12 (o menos 76); la
  columna y la fila, `origen % 8` y `origen / 8` (8 fijo, con signo). El ancho y el alto son los bytes
  `+0x24` y `+0x25` del registro de `Item.bmd` del tipo (Kris 1 por 2, Small Shield 2 por 2, Wings of
  Elf 3 por 2; un registro vacío, 0 por 0, no escribe nada). En cada casilla `fila · 8 + origen +
  columna`, sin tope (Q-C200 y Q-C201), escribe `+0x1c` = d[4], `+0x42` y `+0x43` la columna y la fila
  del origen, `+0x1a` = d[2], el tipo, `+0x1b` = d[3] y `+0x3c` en 1 solo en la de arriba a la
  izquierda, y llama a `0x54bf40` con B1 y B3 sobre los 24 bits de arriba de `fila · 8 + origen` y B4
  sobre los del tipo. Ese `0x54bf40` corre **con el registro cerrado**: si lee el nivel
  (`[0x7681eb0] + 0xe`, en `0x54cd97` y `0x54cdec`, la familia tardía de las armas con el bit `0x10`
  de B3) lee lo que dejó el último cierre (Q-C202). Después, con `[0x78a6758]` en menos de 12 (con
  signo, `0x5bcd0b` y `0x5bd08b`), abre el registro, llama a `0x54fe10` una vez por cada WORD, `+0x3e`
  y `+0x42`, que no sea 0 (`0x5bcdd9`, `0x5bcdf0`), y lo cierra (`0x5bcf8f`). `[0x78a6758]` está en la
  `.bss` y lo escriben quince lugares de la interfaz del inventario y del juego (`0x44add8`, `0x561b6d`,
  `0x561bf7`, `0x59bf3c`, `0x5c2d5c`, `0x5c5131`, `0x5c5154`, `0x5c51c3`, `0x5c51f8`, `0x5c5277`,
  `0x5c5301`, `0x5c5330`, `0x5c6c15`, `0x5c8edd` y `0x5c8f53`); que ninguno corra antes del primer
  `F3 10`, *inferido* (P-C42 a).

**El azar.** Con todo cerrado, un `F3 10` gasta 3.848 por el cierre común, 3.848 más lo de adentro de
`0x512a30` (el sufijo de `0x4f2250`, hasta dos más con Fenrir, y los 3 de la cola de `0x550d10`) por
cada lugar del equipo, y 3.848 por cada ítem de una grilla con `[0x78a6758]` en menos de 12.

MuRet: `Scenes::ApplyInventory` y `ClearForInventoryReply` (`0x4c55a0`), `Scenes::PlaceInventoryItem` e
`InventoryItemType` (`0x5bc740` y `0x54b3a0`) y `Scenes::OwnInventory`, las dos grillas, en
`WorldState::Inventory`; `[0x78a6758]` es `WorldRuntime::slot78a6758`, en 0. El `F3 10` sin cifrar
llega al aplicador como `App::ClearInventoryReceived`, y la sesión manda el aviso. `0x443d40` apaga los
ayudantes del héroe publicado (`Scenes::TurnOffHelpersOf`); `0x421d60` no suelta nada: MuRet no tiene
el cuervo en el mundo, como en `0x510f70`. Las mascotas del lugar 8 van a `Scenes::CreateHelper`, que
crea el `0xbf` y corta con los otros (`0x443d70`); el `0xbf` no llega: corta antes, en `0x4225d0`.
Fuera de la zona segura, el `0x61e` de `0x518cf0` corta. Cortan con error, nombrando la dirección y
después de lo que el original hace antes (el registro se cierra igual): las mascotas del lugar 8 que
no son el caballo (`0x443fa0` → `0x443d70`), el cuervo del lugar 1 (`0x420cf0`),
el anillo de nivel 3 (`0x414340` y `+0x360`), el Dark Horse en cualquier lugar (`0x4225d0`, desde
`0x54cc5c`, sin correr `0x54bf40`), la lectura del nivel con el registro cerrado (Q-C202), una casilla
fuera de su grilla (Q-C200) y `Item.bmd` sin cargar.

### El paso de ondas de Kalima

`0x41dbe0` (53 instrucciones, 146 bytes, cotejados con el PE) recorre, por fila y columna, el
interior 1..254 de un plano 256 × 256 de DWORD. El selector raw elige el destino y su XOR 1 la
fuente; el método **no cambia el selector**. Para cada celda suma arriba, izquierda, derecha y
abajo con wrap de 32 bits, hace SAR 1, resta el destino previo con wrap de 32 bits y resta de ese
resultado su SAR 4. Conserva la fuente completa y las 1.020 celdas del borde destino: no hay wrap
espacial; las esquinas fuente no alimentan el interior, pero los demás bordes sí. No hay calls,
FP, RNG, clamp ni restricción de amplitud. `World/KalimaWaves` porta esta transformación aislada
para los slots 0/1 mediante dos planos de bits u32 y un bool: es una proyección, no el layout de
la clase ni un adaptador de cualquier raw no cero. Otros valores raw pueden seleccionar otros
slots o producir aliases nativos; quedan fuera de esa API. Sus defaults cero/false son propios.
El caller mantiene el estado completo y definido, estable y con un único dueño sin observadores
concurrentes del barrido; aloja directamente en heap los 512 KiB de planos y evita copias grandes
en stack. El paso no asigna memoria. No reproduce ABI, faults ni estado residual de CPU, ni cierra
perturbación, `0x41d880`, `0x41d2d0`, render o integración/lifetime de Kalima.

### El mundo y las dos capas de agua de Kalima

`World/KalimaWorld` conserva el estado de `0x41cca0`, los planos periódicos de
`0x41d880` y la malla de `0x41d2d0` que consume `0x41cf00`. El dueño es independiente
del byte de mapa: los mapas nominales 24 a 29 y 36 pertenecen a Kalima, pero esa pertenencia
no construye por sí sola el objeto de `0x723c48`. La conexión del cargador y la escena se
verifica aparte del kernel.

`0x41cca0` perturba cada cuarenta pasos: cierra la lectura X del héroe, sortea el centro,
cierra Y y aplica el disco antes de alternar el selector y ejecutar `0x41dbe0`. Después lee
X e Y para las ondas periódicas. Los callbacks del port conservan ese orden y el prefijo
alcanzado si falla una lectura; no sustituyen el contador lógico por un contador de dibujos.
La perturbación conserva el SAR19 y los límites estrictos del disco.

Las alturas tienen consumidores distintos: la malla inicial queda a350, mientras la consulta
`0x4378e0` devuelve175. El calco tiene su propio plano y escala; no se deduce de esos175.
El dibujo emite las diagonales alternadas de la grilla42×42, suma las normales en el orden
nativo y no adelanta ondas ni CRT. La capa base usa alpha y la segunda aditiva. En el brillo,
`0x41d257` guarda una copia float de la luz sin sacar ST0: rojo y verde usan esa copia; azul
multiplica el valor retenido por3 antes del store. El vector no plano de T1000 distingue
el azul `0xbe7aa15f` del spill anticipado `0xbe7aa160`.

Los recursos físicos son `Object25\water1.tga`, slot35 LINEAR/REPEAT, y
`Object25\water2.jpg`, slot36 NEAREST/CLAMP. El catálogo no inventa tiles ordinarios deWorld25.
Las pruebas usan esos archivos reales con transporte GPU falso declarado; no acreditan por
sí solas la presentación de toda la escena, los objetos vivos deWorld25 ni identidad universal
con FSIN/FCOS del original. `CloneForFrame` copia el estado computacional para dibujos
intermedios sin modificar el dueño ni el CRT durables.

### El paso del juego

El paso de la escena 5 es `0x636290` entero (3.844 bytes), después de `0x623e70`, la UI del juego
([02](02-arranque-y-ciclo-de-vida.md#qué-corre-en-cada-paso-y-qué-en-cada-frame)). Después del
bloque de la entrada, en orden (*estático*):

| Dónde | Qué llama | Qué hace | En MuRet |
| --- | --- | --- | --- |
| `0x636da3` | el estado `0x3d` pone `0x7d3a101 = 1` (`0x636dac`) | si no, y `0x7d3a101` está en 0, sale (`0x636dbc`) | sí (`GameScene::StepWorld`) |
| `0x636dc2..0x636ddb` | el temblor `0x7d325c8` · 0,2 | [La cámara del juego](#la-cámara-del-juego) | sí (`GameScene::StepWorld`) |
| `0x636dd4` | `0x652940`, si el héroe tiene `+0x371 == 'C'` (siempre: lo pone `0x511290`) | lo del asedio y del gremio (`0x4294b0`) | no hace falta: sin asedio no hace nada ([08](08-pendientes.md) P-C41 c) |
| `0x636de8` | **`0x5fbf60`** | copia la luz sombreada `0x7c0ccc4` al buffer dinámico `0x7b4ccc4` en la ventana del rango y escribe las olas | `DynamicWorldLight::Update` copia el RGB con el rango compartido de `WorldState::DynamicLighting()`; `LoadedWorld::Step` y `GameScene::StepWorld` actualizan las olas con ese rango, pero sus buffers siguen siendo de cada escena ([08](08-pendientes.md) P-C41 b) |
| `0x636e11..0x63705b` | con `0x7d36899` en 0: `0x5ecdc0`, `0x46b7b0`, `0x490490`, `0x5ebca0`, `0x435bd0`, `0x5ee140`, `0x59ff10`, `0x5a7cc0`, `0x657930`, `0x659290`, `0x45a9d0` y dos por vtable | la UI, el chat y los paquetes de la interfaz | CL8, CL9 |
| `0x63706a` | **`0x606560` → `0x604ab0`** | los objetos del mundo que vio el último dibujo ([Los objetos del juego](#los-objetos-del-juego)) | en el login, con los casos del 55; en el juego, `GameScene::StepTheObjects` con los techos y los casos que MuRet tiene del 0 y del 2, y corta en el primero que no tiene; en los demás mapas, con un objeto visto, corta en `0x606560` ([08](08-pendientes.md) P-C53) |
| `0x637078` | `0x608880` | los ítems del piso que caen | CL9.3 |
| `0x637082..0x6370e5` | **`0x530490`**, en el mapa 0 si la capa 1 de la celda del héroe (`0x77f36d0`) no es 4 | las 80 hojas | solo las brasas del 55 |
| `0x6370ea` | **`0x446120`** | los pájaros | `Scenes::StepGameBirds`, pool durable y servicios reales; [familias y límites](#la-fauna-y-sus-recursos) |
| `0x6370ef` | **`0x447030`** | los peces | `Scenes::StepGameFish`, después de aves con la misma CRT; [familias y límites](#la-fauna-y-sus-recursos) |
| `0x6370f4` | `0x445230` | el caballo oscuro ([El caballo oscuro](#el-caballo-oscuro)), también en la espera | `Scenes::StepHelpers`, con la tabla de `WorldState::Helpers`, que en el juego queda vacía ([08](08-pendientes.md) P-C47) |
| `0x6370f9..0x637105` | `0x557650`, `0x451d10`, `0x451c10` | contadores y listas, sin leer | — |
| `0x63710a` | **`0x59adf0`**, el `MoveHero` | el héroe con el mouse ([Caminar](#caminar)); baja `0x77f3b04` y lo vuelve a leer (dos accesos, 8 `rand()`) y, con más de 30, sale; si no, escribe en `0x77f36d0` la capa 1 de la celda del héroe (`0x59e2ba`) y, con `0x57cfc0`, abre y cierra el registro propio: 3.848 `rand()` ([La tabla de protección](#la-tabla-de-protección)) | el contador (`GameScene::StepWaitCounter`), sin los gates de antes (P-C28 b); lo demás falta |
| `0x63710f` | **`0x508f00` → `0x508ea0`** | por personaje: el trapecio en (x, y) · 0,01 con −20 a `+0x16c`, `0x508ba0`, `0x4fd5f0` (la animación y el alpha), `0x505de0` (la zona segura, la cabeza y las luces) y `0x42da90`; antes, el `0x02` del `TerrainWall`, con 8 `rand()` por personaje vivo. Corre también en la espera ([El paso de los personajes](#el-paso-de-los-personajes)) | después de la espera, `Scenes::StepCharacters`: el `TerrainWall`, `+0x16c` con el trapecio global (T11b), el cuerpo de cada `0x20a` quieto (`0x4fd5f0`, [El cuerpo de los personajes](#el-cuerpo-de-los-personajes-0x4fd5f0)) y su `0x505de0` hasta el final, con la zona segura (T11) y la cabeza ([Los efectos de los personajes](#los-efectos-de-los-personajes-0x505de0)); de los demás, también `0x508ba0` (CL7, hito 1); en la espera, frontera ([08](08-pendientes.md) P-C41 a) |
| `0x637114` | `0x548f40` | los números de daño (`0x7609c0c`) | CL9.2 |
| `0x637119` | `0x53eaf0` | la tabla `0x75fe028` (`0x51fc80`), sin leer | — |
| `0x63711e` | **`0x52d220`** | los efectos con modelo | `StepGameModelEffect`: prefijo CRT por vivo, Noria 322..324 y teleport 5c6 además del kernel previo; después del daño y antes de joints/partículas ([El paso de los efectos con modelo](#el-paso-de-los-efectos-con-modelo)) |
| `0x637123` | `0x53c670` | los joints | no |
| `0x637128` | **`0x5446e0`** | el viento y las partículas | sí (`StepParticles`) |
| `0x63712d` | `0x549090` | las marcas del suelo (`0x760cab0`, [Las marcas del suelo](#las-marcas-del-suelo)): les baja la vida | después de la espera, `World::StepGroundMarks` (CL7, hito 1); en la espera, frontera (P-C49 c) |
| `0x637138` | `0x403590` | `0x4033d0` y compañía (el recorrido de la cámara), sin leer | — |
| `0x637144..0x63717a` | `0x5fc0d0` y `0x5fc0b0`, con `0x7d2d35e` | funciones de un `ret` | nada |
| `0x63717f` | `0x5ac8d0` | F1 a F4 (CL3.3) | CL8 |

### La fauna y sus recursos

**Estático, ampliación de 2026-11-10:** cuerpos funcionales del `main.exe` SHA-256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`, imagen mapeada con
base `0x400000`, leídos con desensamblado i386. Esta ampliación no ejecuta ni emula el original.
Las cuentas del port se comprueban con los tests citados; no son una nueva captura nativa.

`0x446120` usa 40 registros de `0x1d4` en `0x7d2dca8`; `0x447030`, 10 en `0x7d35350`.
`0x60712a..0x607150` sólo apaga su byte vivo, y `0x4455c0` sólo el de las aves: no reconstruyen
registros ni borran ángulos, seen o campos retenidos. `WorldState::Fauna()` posee esos mismos
slots entre pasos y escenas; las vistas de aves/peces y los dueños de los joints apuntan al slot
físico, no a una copia temporal. El prefijo que avanza el paso depende del mapa; el dibujo recorre
siempre los 40 o los 10:

| Paso | Mapas | Lugares procesados |
| --- | --- | --- |
| aves, `0x446120` | ordinarios / Atlans 7 / Icarus 10 / Kalima 24..29 y 36 | 5 / 40 / 13 / 2 |
| peces, `0x447043..0x447064` | Atlans 7, Tarkan 8, Kalima y Crywolf 34 / demás | 10 / 3 |

El 33 comparte ramas de nacimiento del 8, pero **no** su prefijo de 10. El paso productivo conserva
el orden hojas → aves → peces → helpers de `0x637082..0x6370f4`, con la CRT común. Los handlers
usan la altura real de `0x5f93c0`, incluido el `fixedHeight` durable de la cámara para el atributo
`0x40`; no fijan de nuevo 1200 si la cámara ya escribió 1201. `0x4ea030` selecciona la acción,
`0x4ec200` avanza la animación y `0x4e9980` conserva el final de alpha incluso tras apagar el slot.
El campo `+0xc` es la escala física; `+0xd8` es velocidad de movimiento de aves/peces. No se usa
la escala física como velocidad de animación de las aves.

- `0x445b90` conserva los `if` sucesivos: el ave AD/BC puede pasar 0 → 1 → 3 en un paso y tomar
  dos sorteos de altura. Posada (`+0x110 == 2`) evita el movimiento común y sus sorteos.
- `0x445ec0` llama al flock `0x4e9a40` con el pool físico completo, incluidos vecinos vivos fuera
  del prefijo procesado. Las distancias 80/400, la X retenida y la Y guardada no se reemplazan por
  una suma vectorial reordenada. El rumbo `0x4e97a0` y el giro **entero** `0x4e94c0` son distintos
  de CreateAngle/TurnAngle: igualdad 90 → 90 devuelve 180; 359 → 0 con paso 13 termina en 12.
- `0x4455e0`, Icarus: índices 0..2 preparan Monster32 y gastan seis sorteos del nacimiento; los
  restantes crean el `0x10f` y su joint con el dueño durable. `0x445d60` usa yaw en radianes y el
  producto sin×cos de dos fases derivadas del contador, antes del sorteo de muerte `%5120`.
- `0x445890`, Atlans: `heroY*0,01f` conserva el producto sin spill antes de compararlo con 128.
  Por ello 12800 todavía toma la rama baja; 12801 toma la alta. Nacimiento admitido: seis sorteos.
- `0x439680`, Kalima: lee y cierra el acceso lógico al contador antes del gate `%100`; el cierre
  exterior cuesta cuatro CRT. En el gate admitido, otros seis sorteos; X=`heroX−1000−rand()%200`,
  Y=`heroY−500+rand()%200`, Z=`heroZ−800`. `0x439aa0` gira, sortea `%5` y, con timer<20,
  cambia alpha/velocidad/pitch; el decremento de timer pertenece a la cola del caller.
- Peces: los dos sorteos del candidato X/Y se conservan aun al rechazarlo. En Lorencia sólo admite
  capa 1 igual a 5; Crywolf lee el flag del héroe. Move selecciona acción; FullMovement hace flock,
  matriz, sorteo de longitud y altura que pisa Z. Los efectos `0x618/4`, `0x697/0` y `0x610/8`
  reciben el dueño del mismo slot. Los sorteos condicionales de sonido/timer permanecen en orden.

En tormenta, el prefijo de aves conserva `%40`, posición, efecto `0xc2/3`, sonido lógico `0x42`
y luz dinámica negativa alrededor del héroe. Su creador `0x518cf0 → 0x51ae9b → 0x51a9b1` no
sortea: vida 80, escala 0,3 y velocidad (0,−12,0). `0x5208c5..0x525148` añade el sorteo común,
la luz, la dirección `0x53d1a0` por índice físico, fuego `0x5d9/8` y sprite `0x5fd`, antes de la
cola de animación/movimiento/vida. Un lugar libre usa ocho CRT con el contador cerrado; pool de
partículas lleno, seis; un acceso exterior abierto evita los cuatro del cierre. Fuego 8 nace con
dos sorteos (`0x541172..0x54140a`), escala por 0,95 y rotación+5 (`0x54585f`), y usa el atlas
4×1 girado por `+0x44` (`0x548850`), distinto del ángulo X de la rama default.

**Carga diferida `0x612d90`.** Los índices `0x1f/0x21` cargan `Monster\Monster32.bmd` y
`Monster\Monster34.bmd`, modelos `0x171/0x173`. Con `[0x6c9e0c]=1`, acciones o mallas ya
presentes hacen retornar antes de recargar (`0x612dbc/0x612dc7`). Texturas de `Monster\`,
Repeat/Nearest y cursor actual, sin otro `0x60d530`. Los stores comunes `0x612e5a..0x612e9f`
ponen velocidades 0..6 = 0,25/0,2/0,34/0,33/0,33/0,5/0,55 y acción 6 no repetible. Monster32
además deja acción 1 no repetible; después de registrar sus sonidos, `0x6140ac..0x6140c9` pone
3=0,5, 4=0,7, 1=0,8 y 7=0,8. Registra `0x14d/0x14e/0x14f` con mYeti1/mBullAttack1/mYetiDie;
Monster34, `0x154/0x155` con mBahamut1/mYeti1. Las tablas de diez WORD de `0x612c80` son
`{7b,7b,7c,7c,7d,−1,…}` y `{82,82,83,83,82,−1,…}` (valores hex). MuRet conserva pedidos
y tablas lógicos de audio; esto no añade reproducción WAV.

`FaunaModels` conserva CPU entre mapas, pero no inventa slots privados de textura. `0x6070a3`
destruye BMD 0..159 y `0x61cc50` no libera `0x171/0x173`. El destructor BMD libera **por malla**
(`0x4f07b0..0x4f07c3`, salvo slot `0x12e`); una coincidencia por nombre suma el refcount byte
(`0x60d645`). `0x63c18d..0x63c1b3` borra nombre/GL/datos sólo al bajar a cero. Una nueva subida
suelta una referencia y luego **reemplaza el registro aun con referencias restantes** (`0x63bfcf`):
un BMD cacheado puede observar otra imagen. `ModelTextureTable`, compartida por WorldModels y
FaunaModels, conserva nombres/refcounts/metadata física mientras el cursor se reinicia. La búsqueda
`0x60d550` excluye next; un nombre justo allí se vuelve a subir. Persistir sólo el BMD o liberar una
vez por upload no reproduce este contrato.

**Límite del pez Kalima −1.** El caller omite LookupModel/AdvanceModel, pero alcanza SetAction
`0x4ea030` con índice firmado −1. `0x60dbe0` construye 18.589 BMD de `0xe0` y publica la base
con desplazamiento `rand()&1023` (`0x60dc4c..0x60dc7a`). El constructor `0x6154ab..0x6154b3`
deja los contadores en cero. Con desplazamiento>0, BMD[-1] es un objeto construido, sin acciones;
con desplazamiento=0 la lectura sale antes del primer objeto. `FaunaStepContext::prefixModel`
permite un préstamo explícito sólo para esa selección de acción. `ConstructedBmdPrefix` presta
el guard sin acciones únicamente para los offsets1..1023. En offset0 conserva la ausencia: la
lectura allocation−182 queda fuera de los objetos construidos y sus bytes no se inventan.
La carga elige el offset una sola vez, antes del primer archivo BMD, con la CRT global; un fallo
de archivo, Bind o Release no vuelve a sortearlo. `GameScene` presta ese guard al paso de fauna.
Esta evidencia funcional no reconstruye la ubicación/protección de la tabla ni cierra por sí
sola todo el mundo de Kalima.

**Perfil numérico.** Sin/cos de fauna usan `OriginalRetainedMath`: mantienen el resultado con
64 bits de significando antes del primer producto PC53 y de los spills f32. `0x445d60` multiplica ambos retenidos
antes de redondear a 53; `0x53d1a0` conserva sus productos y spills separados. No equivale a dos
sin/cos double ya redondeados, ni a std::sin del host. El perfil portable PC53/RN y sus dominios
explícitos no acreditan universalmente FSIN/FCOS, CW, NaN, traps o todo el stack x87 del proceso.
La validación nativa/VM sigue separada del port; no se declara cerrada la CL7.

Oráculos rastreados:
[estado](../../tests/scenes/test_fauna_state.cpp),
[paso](../../tests/scenes/test_fauna_step.cpp),
[recursos](../../tests/scenes/test_fauna_models.cpp),
[tabla física](../../tests/scenes/test_model_texture_table.cpp),
[efectos](../../tests/scenes/test_fauna_effects.cpp) y
[matemática retenida](../../tests/render/test_fauna_retained_math.cpp).
Entre los casos: `CommonMovementSeesLivingNeighborsOutsideTheStepPrefix`,
`LandingAndTakingOffRunTheirNewStatesInTheSameStep`, `IcarusSpawnHasSixDrawsAndWritesTheNativeFlightFields`,
`KalimaBirthClosesCounterBeforeSixSpawnDraws`, `KalimaSetActionNeedsTheExplicitSignedPrefixModelLoan`,
`BothRealMonsterResourcesPrepareTheirMeshesTexturesAndActionTables` (Data real),
`AMapTransitionSharesMonsterReferencesAndACachedMonsterSeesAReusedSlot` y
`MultipliesBothRetainedResultsBeforeTheFirstBinary53Round`. Prueban servicios productivos y sus
prefijos; un kernel aislado o un pool vacío no acredita el recorrido completo de escena.

### La pose compartida entre escenas

**Estático, mismo hash anterior.** `0x4eb860` no escribe los huesos dummy: permanecen en el
buffer global `0x68b181c`, junto con las colas no escritas de vértices/normales. Importan tanto la
última pose como los campos por modelo de escala/origen. No se reconstruyen identidades por objeto.
`WorldState::PoseCarry()` posee esa historia entre login, selección y juego. `DrawnCharacter.poseBones`
presta el scratch antes de PoseSkeleton/PoseAttached; onPose publica las matrices ya calculadas,
incluida la cabeza y la raíz attached. Los cuerpos especializados consumen los meshes publicados,
sin volver a posar. Los objetos de malla oculta −2 también publican si pasaron el gate de pose.

Las aves dibujan con el flag de pose 1 y los peces con 0 (`0x446b60/0x446f00`). El recorrido
suma/resta yaw90 aritméticamente incluso en muertos (aves `0x10f`, excepción), publica seen por
el trapecio con margen −20 y conserva los glows AE/BC. La sombra usa su propia geometría y alpha
0,2; falta en mapa 10 y en peces b9/ba. Antes de mirar las mallas, `0x446e9d..0x446eb3` escribe
XYZ del origen del modelo a (x,y,Height), conservando escala. El siguiente BC transparente, que no
volvió a posar, usa ese origen del suelo y los huesos retenidos. Los dos pases puros 48→44 de
171/counter1 (`0x446c35..0x446c80`) siguen visibles en frames intermedios; los creadores de
tormenta y sus sorteos sólo corren en frames nativos.

El piso tiene otro gate: `0x608b0a..0x608b20` posa sin traslación aun con alpha0; `0x60cf70` sólo
publica el skin/cuerpo con alpha>0,01. `PublishFaunaFloorPose` conserva esa diferencia. Los objetos,
piso, personajes, helpers y efectos comparten el mismo carry en orden. En frames de proyección de
MuRet se usa **una copia temporal para todo el frame**: cada escritor actualiza ese scratch y el
siguiente lo lee, sin cambiar la historia durable ni repetir RNG/seen/yaw nativos. Copiar y descartar
el carry en cada pase o saltar el publisher mezclaría meshes de otro modelo.

Regresiones en [dibujo de fauna](../../tests/scenes/test_fauna_drawing.cpp):
`AShadowPublishesTheRetainedOriginUsedByTheNextTransparentBcGlow`,
`AShadowPublishesItsOriginEvenWhenTheModelHasNoMeshes`,
`AProjectedStormBirdKeepsBothPureShinePassesWithoutCreatingEffects`,
`DurableFixedHeightFeedsBirdAndFishShadowsAndTheirModelOrigin`,
`TransparentFloorItemsPublishBonesWithoutReplacingTheMeshScratch`,
`AProjectedFrameKeepsItsPosesInTheBorrowedFrameScratch` y
`CharacterBodiesInitializeDummyMatricesFromTheSharedScratchBeforePosing`.
[Login](../../tests/scenes/test_login_scene_models.cpp) y
[selección](../../tests/scenes/test_character_select_scene.cpp) comprueban publicación nativa y
retención entre frames/Leave; [GameCharacters](../../tests/scenes/test_game_characters.cpp),
`BodyGeometryAndPublishedPoseUseTheSameRetainedDummyMatrices`, comprueba geometría y publicación
con el mismo dummy, también en proyección. Los tests de
[WorldObjectsDrawing](../../tests/scenes/test_world_objects_drawing.cpp) cubren marcadores ocultos
con pose nativa/proyectada. Son regresiones del contrato portable, no una nueva comparación de
poses o píxeles con la VM. El lector de las gotas de la fuente también consume ese carry
([Las partículas de la fuente](#las-partículas-de-la-fuente-en-el-juego)); los casos
`TheFountainReadsThePreviousWorldMatricesForDummyAndTransparentBones`,
`AFullFountainPoolStillReadsTheRetainedBonesAtTheirNativeRandomPrefix` y
`AnEmptyRetainedFountainLoanStopsAfterTheFirstOffsetInsteadOfReposing`, en
[GameParticlesTest](../../tests/scenes/test_game_particles.cpp), comprueban la lectura sin volver a
posar y los prefijos del fallo, incluso con el pool de partículas lleno.

### El paso de los personajes

`0x508f00`, desde `0x63710f`, en cada paso de la escena 5 con el mundo cargado, **también en la espera**:
con el contador en más de 30, MoveHero sale en `0x59afd6`, pero el paso sigue (*estático*). En orden:

1. `0x412140(héroe)` (`0x508f0c`): fuera del mapa 30 llama a `0x412560`, que solo suelta el objeto de
   `0x5763530` si existe, y ese objeto lo crea `0x412140` en el mapa 30 (lo del asedio).
2. El `TerrainWall` (`0x508f14..0x508f34`): el `0x02` sale de los 65.536 WORD de `0x7d0d348`.
3. El primer lazo, por los 400 primeros registros (`0x508f3e..0x5091a9`; el 401 no, Q-C208): vivo (`+0`),
   sin morirse (`+0x389` en 0) y con `+0x88` distinto de 8, lee la celda lógica por la tabla de protección,
   la Y (`+0x420`) y la X (`+0x41c`), cada una en un acceso que se cierra (8 `rand()` si quedan en 0), y le
   pone el `0x02` a la celda de `0x5f82a0(X, Y)` (`0x509165`). Después, vivo o no, `+0x16c` =
   `0x5fba90(f(x · 0,01f), f(y · 0,01f), −20)` (`0x50916d..0x5091a0`), con el trapecio global del último
   `0x5fb570`: entre la selección y el primer frame del juego, el del último dibujo de la selección, el de su
   punto fijo y no el del héroe (Q-C216; [El dibujo](#el-dibujo) de la selección).
4. El segundo lazo, por los mismos 400 (`0x5091af..0x5091ce`): `0x508ea0`, que sin vivir no hace nada; si
   no, vuelve a escribir `+0x16c` igual (`0x508eac..0x508ed2`) y llama a `0x508ba0` (los otros personajes:
   con el héroe sale en `0x508bad`), `0x4fd5f0` (el cuerpo, la animación y el alpha, con un `rand() % 6`
   sin condición en `0x4fd7e4`; [§ El cuerpo de los personajes](#el-cuerpo-de-los-personajes-0x4fd5f0)),
   `0x505de0` y `0x42da90` (fuera del mapa 30 devuelve 0 en `0x42da9a`).
5. `0x52e500` (`0x5091d0`): el paso de las estelas de `0x75e9870` (100 de `0x2f4`), sin `rand()`. Las crea
   `0x52e470`, desde habilidades, efectos y la acción 3 del caballo (`0x444671`).

**`0x505de0` con un jugador** (entero en [§ Los efectos de los
personajes](#los-efectos-de-los-personajes-0x505de0)). El `0x20a` tiene acciones; un modelo sin acciones
(`+0x26` de `0x5769ad8 + tipo · 0xe0` en 0) va a la caja de `0x505e15` y sale. Copia la posición, la escala y
la acción al modelo compartido (`0x505ec6..0x505ee4`) y, sin verse (`+0x16c` en 0), sale (`0x505ef2`). Visto:

- **la zona segura** (`0x505ef8..0x5068b2`): la celda lógica otra vez, la Y y la X con sus cierres (8
  `rand()`), y `+0x3e2` en 1 si el bit `0x01` del byte bajo de `0x7d0d348[0x5f82a0(X, Y)]` está, y en 0 si
  no (Q-C206; [Caminar](#caminar));
- la cabeza (`0x5068b9..0x5069ab`, [Lo demás del login](#lo-demás-del-login)): con el héroe no sortea ni
  gasta `rand()`; gira hacia `+0x34` y `+0x38`, que MoveHero escribe con el mouse (`0x59b1c2..0x59b287`); con
  los demás, sin morirse, sortea con `rand() % 32` y dos más si da 0 (`0x5068eb..0x506970`);
- con `+0x3e5` positivo, veinte efectos con `rand()` (`0x5069ae`): solo lo sube `0x4c9790` (`0x4c982b`);
- con `+0x375` debajo de 6, sin signo (`0x506acf`), la luz de cada mano, la derecha y la izquierda: `0x21e`
  con un 3 y `0x225`, `0xa24`, `0xc1c` y `0x61f` con un 2, por `0x5f9780` al buffer dinámico `0x7b4ccc4`;
- con `+0x3d8` positivo (`0x506b64`), los efectos de `+0x3d4` (`0x611` y `0x612`): en los personajes,
  `+0x3d8` solo lo baja `0x4fd5f0` (`0x4fd75e`); los demás que escriben `+0x3d4` y `+0x3d8` son de la UI
  (*estático*);
- **`rand() & 7` sin condición** (`0x506cac`), que el camino del `0x20a` no lee (Q-C207);
- el caso del tipo (`0x506ccc..0x506cf1`; el `0x20a` cae en `0x506cf8`): en la escena 5 y el mapa 7, un
  `0x607` con `ftol(0x5767b88) % 10000` debajo de 1000; en el mapa 9, dos `rand() & 3` que pueden sacar un
  `0x68` cada uno; en las acciones `0x44` y `0x45`, efectos `0x5ac` y `0x5189f0`; en la `0x96`, diez `0x5d9`
  en huesos de `rand()`; y un `0x4ec110` a un local que nadie lee (`0x506fc1..0x507001`);
- los pasos del héroe (`0x50862f..0x5086ca`), solo con el héroe publicado (`0x508641`): con las acciones
  `0x0d` a `0x21`, `0x8b`, `0x8c` y `0x4b`, el sonido de `0x505770` según el frame (`+0x114` contra `0x6b8b94` y
  `0x6b8ae0`) con `+0x3e0` y `+0x3e1`, sin `rand()`;
- en los mapas 24 a 29 y 36, con las acciones `0x15`, `0x1d` a `0x21` y `0x4b`, un `0x6f4` con dos `rand()`
  (`0x5086cf..0x508801`).

Con el héroe visto, de pie y sin nada de eso, el paso de los personajes saca 8 + 8 + 1 `rand()` más los de
`0x4fd5f0`; sin verse, 8 más los de `0x4fd5f0` (*estático*). Los de `0x4fd5f0` con el héroe quieto son 3.848 + 1
y, desde el frame 3, uno más ([§ El cuerpo de los personajes](#el-cuerpo-de-los-personajes-0x4fd5f0)).

**En MuRet** (`Scenes::StepCharacters`, T11 del hito 1 y [§ El cuerpo de los
personajes](#el-cuerpo-de-los-personajes-0x4fd5f0)): después de la espera, desde `GameScene::StepWorld`, con el
trapecio global de `WorldRuntime` (T11b): en el primer paso después de la espera, el del último dibujo de la
selección (Q-C216), y sin ningún dibujo antes, los ceros de la .bss (Q-C217). Porta el `TerrainWall`, `+0x16c`
de los 400, la celda lógica, el cuerpo (`0x4fd5f0`) por el camino del `0x20a` quieto (con el registro propio del
héroe, Q-C246, el `rand() % 6` y el de `0x432bf1`) y `0x505de0` hasta su final con todos los `0x20a`: la cabeza,
que sortea con los que no son el héroe, y el `rand() & 7` (CL7, hito 1).
Corta con error, en el orden del original:

- en el mapa 30 con el héroe en un gremio (`0x412193`, [Los objetos de Castle
  Siege](#los-objetos-de-castle-siege)) y sin el mapa cargado (`0x508f14`);
- en el `0x508ba0` de otro personaje vivo: el `0x154` que se muere (`0x508e89`) y el que camina (`0x508e27`);
- en lo que el cuerpo no tiene ([§ El cuerpo de los personajes](#el-cuerpo-de-los-personajes-0x4fd5f0); sin
  Player.bmd, en `0x4f9580`);
- con otro modelo que el `0x20a` (`0x505e0b`);
- con la luz de una mano (`0x5f9780`);
- sin el pool de partículas: en el 7 para la burbuja (`0x506d77`) y en el 9 para la gota que salió (`0x506dff` y
  `0x506e4f`);
- en las acciones `0x44` y `0x45` (`0x506e5d`) y `0x96` (`0x506f62`) y, con el héroe, en las de los pasos
  (`0x508668`);
- con otro que el héroe, en Kalima y en las acciones del `0x6f4`, después de sus dos `rand()` (`0x5087fc`).

Con el héroe, el `0x6f4` de los mapas 24 a 29 y 36 no se alcanza sin cortar antes en los pasos. Su cabeza gira
hacia un blanco que MuRet todavía no escribe (MoveHero, P-C44 b). `0x52e500` es frontera, como las de T8, porque
corre también en la espera (P-C44). En la espera, `0x508f00` entero sigue siendo frontera (P-C41 a).

Tests: `CharacterStepTest`, `GoldenCharacterStep.TheSafeZoneFollowsTheLogicalCellAhead` y
`TheHeroAtRestDrawsTheSameBudgetOnLorenciaAndDevias`, y
`GameSceneDrawTest.TheFirstStepAfterTheWaitReadsTheTrapezoidOfTheSelection`,
`WithoutAPreviousDrawTheFirstStepSeesWithTheTrapezoidOfTheBss` y `WithoutPlayerBmdTheStepStopsAtTheBody`. De la
selección al primer frame del juego, `WorldEntryTest.TheFirstGameStepReadsTheTrapezoidOfTheLastSelectionDraw`.

### El paso de los personajes en la selección y el login

`0x508f00` corre también desde la lógica de la selección y la del login, con el trapecio global del último
`0x5fb570` (Q-C216), el que dejó el dibujo anterior de la escena. Leído en el desensamblado de `c32bbd5f`
(CL7, hito 1): todo *estático*, salvo lo que se marca.

**Dónde.** La selección (`0x6342c0`), con el estado en `0x33` o más (`0x6342cb`) y, la primera vez, después de
`0x633f20` (`0x6342de..0x6342e5`), en este orden: `0x5fbf60` (las olas), `0x606560` (los objetos), `0x445230`
(los ayudantes), **`0x508f00`** (`0x6342f9`), **`0x508ea0(0x7437050)`** (`0x634303`, la vista previa de la ventana
de crear, fuera de los 400), `0x52d220`, `0x53c670`, `0x5446e0` y `0x633920` (las rutas de la cámara); después,
el input (`0x640850`). El login (`0x634c60`): `0x6349a0` la primera vez (`0x7d3a13d`), el gesto del personaje
del lugar 2 (`0x634c99..0x634ced`, el `rand() % 6` de § Lo demás del login) y, con la ventana de `+0x4578`
cerrada (su byte `+0x4584`, `0x634cf0..0x634cfd`; la de créditos, según § Lo demás del login), `0x5fbf60`,
`0x606560`, `0x445230`, `0x530490` (las brasas), **`0x508f00`** (`0x634d13`), `0x52d220`, `0x53c670`,
`0x5446e0` y `0x633920`. Con esa ventana abierta no corre nada de eso, tampoco los personajes (Q-C237).

**Quiénes.** Los dos lazos de `0x508f00` recorren los 400 (Q-C208). En la selección están vivos los lugares de
la lista (`0x4c20a0` → `0x517270` → `0x511290`, que les deja `+0x88` en 1 y las dos copias de la celda en
(0, 0): la cifrada, `+0x41c` y `+0x420`, y la de `+0x39a` y `+0x39b`). El héroe publicado es el lugar 0 hasta la
primera entrada al mundo; después de una vuelta, el lugar del juego (Q-C213), que `0x510f70(−1)` mató salvo
que su clave sea `0xffff` (Q-C212): ahí los cinco son otros. En el login, el lugar 2 (`0x6349a0`); el lugar 0
está vivo solo si alguna lista lo construyó antes.

**`0x508ea0` con cada vivo** hace lo mismo que en el juego (§ El paso de los personajes): `+0x16c` otra vez,
`0x508ba0`, `0x4fd5f0`, `0x505de0` y `0x42da90`.

- **`0x508ba0`** (`0x508ba0..0x508e98`). Con el héroe publicado sale (`0x508bad`). Con otro: muriéndose
  (`+0x389`) sale, y el `0x154` antes toma la altura de `0x5f93c0` (`0x508e76..0x508e8e`); caminando (`+0x378`)
  sigue su camino (`0x508e26..0x508e6e`: `0x4f37c0`, `0x4ea270(c, 1)` y, si terminó, `+0x378` en 0, `0x4f2250`
  y el ángulo z desde `+0x388`; después, `0x508a80`); con `+0x3e5` sale; el `0x159` sale. Si no, lee la X
  lógica (`+0x41c`) en un acceso que se cierra con `0x412890` y la compara con `+0x39a`, y la Y (`+0x420`) en
  otro que se cierra con `0x40b800`, contra `+0x39b`: con las dos iguales sale en `0x508cfb`, con 8 `rand()` si
  las dos cuentas quedan en 0. Con la X distinta no lee la Y; con una distinta, lee otra vez la Y y la X (8
  más) y busca el camino con `0x4eac00`, que con éxito pone `+0x378` en 1 (Q-C236). Los de la lista y el del login tienen las dos copias
  iguales: gastan 8 por paso.
- **`0x4fd5f0`** (`0x4fd5f0..0x5054b3`, 32 KB y un solo `ret`; entero en [§ El cuerpo de los
  personajes](#el-cuerpo-de-los-personajes-0x4fd5f0)). Antes de su primer `rand()`: con el tipo
  `0xc5`, el nombre y `+0x1d6` a `0x4381f0`; el modelo compartido (posición, escala y acción, `0x4fd69b..0x4fd6ef`), `0x4fbe10`, `0x4fc450`,
  `0x4fb380`, `0x4fb9e0`, el alpha (`0x4e9980`, `0x4fd72e`), `+0x3d8` (`0x4fd736..0x4fd75e`), la animación
  (`0x4fcd00`, `0x4fd770`) y, muriéndose, `+0x389` + 1 y `0x4f5660` desde 15 o en los mapas 11 a 17 con
  `+0x1a1`. Después, el `rand() % 6` sin condición de `0x4fd7e4`, y en el resto 27 llamadas directas más a
  `rand()` y 54 a `0x518cf0` (los efectos), según el tipo, la clase, el equipo y el mapa. Del personaje del login
  se leyó lo que saca (§ Los efectos del login: el de `0x4fd7e4`, el de `0x432bd0` desde `0x4fcee0` y el
  cuervo, `0x416a20`). Los de la lista, quietos, van por el mismo camino del `0x20a`, con su equipo: de pie
  no piden estela. El `0xa9` está leído y portado por el camino genérico descrito abajo.
- **`0x505de0`** ([§ Los efectos de los personajes](#los-efectos-de-los-personajes-0x505de0)), como con el
  héroe del juego, salvo que con otro que el héroe la cabeza sortea (`0x5068eb`, `rand() % 32` y dos más si da
  0; § Lo demás del login) y no hay pasos (`0x508641`), y que el `0x607` del mapa 7 es solo de la escena 5
  (`0x506cf8`). Visto, saca los 8 de la celda lógica, la cabeza y el `rand() & 7` (Q-C207).
- `0x42da90`, fuera del mapa 30, devuelve 0.

**La vista previa** (`0x7437050`). `0x633f20` la arma con `0x511290(…, 0xa9, 0, 0, 0)`: viva, con las dos
copias de la celda en (0, 0). `0x508ea0` la pasa en cada paso de la selección después de los 400, también con
la ventana de crear cerrada: su `+0x16c` con el trapecio, los 8 `rand()` de su `0x508ba0` y lo que hagan
`0x4fd5f0` y `0x505de0` con el `0xa9`. En `0x505de0` el `0xa9` no es el `0x20a`: su cabeza pasa por `0x4ea190`
(`0x5068e3`, hasta tres `rand()` según la acción) y, debajo de `0x152`, su caso es el de `0x5085db`, con las seis
funciones de los tipos sin caso propio: `0x43bdd0`, `0x42de90`, `0x441210`, `0x42f550`,
`0x431470` y `0x426ce0` salen para `0xa8..0xac` en el mapa 54 sin más `rand()`.
`0x4f9580` multiplica por 2 la velocidad; al terminar, `0x4fcbb0` no cambia la acción del
NewFace. La estela no se crea, pero desde el frame 3 corre el `rand()` incondicional de
`0x432bf1`. Con acción 0, `0x4ea190` sortea primero con `%32`; si gana, X `%128−64` e Y
`%48−16`. Después corre también el sorteo común de `0x5068fd`, con Y `%32−16`: puede
sortear dos blancos en un paso. Con acción 1, el primer helper pone el blanco en 0.
Con la ventana abierta la avanza además `0x63e430`; con un aviso solo corre `0x634303`.

**Lo que saca por paso**, hasta donde está leído. La selección con N personajes de la lista, el héroe entre
ellos: 8·N del `TerrainWall`; 8·(N − 1) de los `0x508ba0`; por cada uno, lo de su `0x4fd5f0` (al menos 1) y,
visto, lo de su `0x505de0` (8 de la celda, la cabeza de los que no son el héroe, 1 o 3, y el `rand() & 7`); y la
vista previa, 8 más lo de su `0x4fd5f0` y su `0x505de0`. Después de una vuelta, con el héroe fuera de los cinco,
8·N de los `0x508ba0`. El login con su personaje: 8 + 8, lo de `0x4fd5f0` y lo de `0x505de0`.

**El dibujo de la selección llama al terreno.** `0x634844` llama a `0x5fc260(0)` (`edi` en 0 desde `0x6346e2`),
después de la luz del elegido y antes de los objetos (`0x6046a0`) y de los personajes (`0x510d00`): el
corrimiento del agua, el pase del terreno (`0x5fc190` con `0x7d0d31c` en 0) en el rango y el trapecio que dejó
`0x63475c`, el del pasto (con `0x6c9d58`, que `0x5f87b0` pone en 1 en el 54, `0x5fc332..0x5fc353`) y las
marcas del suelo (`0x549130`, `0x5fc365`). Cuántas celdas salen y si se ven, sin cotejar con la traza de la
selección (P-C50). MuRet no dibuja el mundo en la selección (`EmptyWorldView`).

**Lo que saca la entrada** (`0x633f20`), antes del primer paso, sin mirar lo que saquen `0x61ca30`, `0x65f4b0`
y `0x5bbe10`. `0x61a560(0x36)`: en el 54 no hay especiales
(`0x61a23c[0x36]` es el caso 15, el salto por defecto a `0x618923`); los 65.536 del corrimiento del pasto
(`0x5f8300`, Q-C146) y lo que saque `0x606660` con los objetos de `EncTerrain55.obj` (sin leer). Después,
`0x511290` con la vista previa (sus `rand()` de `0x511541` y `0x511824` y sus cierres de `0x412890`, los que
`Scenes::ConstructActor` porta para el juego) y `0x633f96..0x63417a`, el registro propio: 3.848 si su cuenta
queda en 0 (Q-C238). Antes, cada `0x517270` de la lista corre su `0x511290`, con sus `rand()`.

**En MuRet** (CL7, hito 1):

- `Scenes::StepCharacters` recibe la escena (`CharacterStepContext::scene`) y porta `0x508ba0` con los que no
  son el héroe: las salidas de antes de la celda y los dos accesos, con sus 8 `rand()`. Guarda una sola copia
  de la celda, así que las dos siempre coinciden y el camino no se alcanza.
- El cuerpo (`0x4fd5f0`) corre con todos los `0x20a` por el camino del jugador quieto ([§ El cuerpo de los
  personajes](#el-cuerpo-de-los-personajes-0x4fd5f0)).
- Todos los `0x20a`, en cualquier escena, hacen su `0x505de0` entero ([§ Los efectos de los
  personajes](#los-efectos-de-los-personajes-0x505de0)): los demás con la cabeza que sortea y sin los pasos, y el
  `0x607` del mapa 7 solo en la 5.
- También corta en `0x508e89` (el `0x154` que se muere) y en `0x508e27` (el que camina).

**Login y selección cableados (2026-11-10).** El login llama al paso común en `0x634d13` (P-C56).
La selección llama a `StepCharacters` en `0x6342f9` con el terreno vivo de `SelectionState`,
Player.bmd e Item.bmd de la carga, y después a `StepCharacter` con el NewFace suelto en
`0x634303`. El registro propio anterior a la primera entrada conserva sus tipos 0: el helper
`0x54efe0` necesita Item.bmd incluso en esta escena (Q-C246). Un corte publica `IScene::Failure`,
conserva el estado y los rand anteriores y congela los pasos siguientes.

Antes del pool, `0x6342f4` pasa los diez ayudantes por `0x445230` → `0x444010`.
En la escena 4 no corre la guarda del dueño muerto o con `+0x88` distinto de 1:
`0x44402a` solo la aplica en la escena 5. El primer ayudante vivo llega al alpha
de `0x4e9980` (`0x444053`) aunque su dueño esté muerto. MuRet conserva ese prefijo
y corta en `0x444059`, antes del cuerpo todavía no portado. Los lugares muertos no
cambian; los posteriores al corte tampoco. Con la tabla vacía el tramo no hace nada.
El paso de la selección sigue parcial por este límite; no reutiliza el de la escena 5.

Después del preview, `0x634315` ejecuta `0x5446e0` sobre las partículas durables del
mundo. Sus dos rand del viento corren también con el pool vacío; las chispas del
cuervo nacidas en este paso bajan de vida 10 a 9 en ese mismo paso. Un tipo o binding
no portado publica el error después de su prefijo de vida/movimiento, conserva los
lugares anteriores y no toca los posteriores.

El wrapper de ambas escenas deja el viento de tela y el incremento del contador
después de la lógica normal: `0x637cab` / `0x637cb2` llaman al login / selección;
`0x637cec` hace los cinco paseos del viento y `0x637f8c..0x637f91` incrementa el
contador. Por eso el constructor, el cuervo y las cabezas leen el contador anterior
y sortean antes de esos cinco rand y los cuatro del cierre con el contador cerrado. Si la
escena corta con error, no consume ese sufijo ni incrementa el contador.

`ConstructSelectionActor` reproduce el `0x517270` de la lista: `0x511290` (28 rand desde
cuentas cerradas), la clase, las partes base, el lugar, su clave, luz y banderas, y parado sin
armas (un rand). Solo después aplica el equipo de `0x5139f0`, el nombre, nivel, control y gremio.
La aparición conserva Q-C102/Q-C103. Los helpers de mascota se crean en el orden de los bits,
aunque otro bit posterior sobrescriba `+0x318`: pueden quedar varios del mismo dueño. El cuervo
se crea antes de los helpers de mascota. Los cuatro campos de cabeza no se reinician.

La inicialización hace los dos constructores del NewFace: `0x633f65`, y el de `0x63e3e0`
durante la creación de ventanas (56 rand). Después limpia `+0x6a`, `+0x6c..+0xab` y solo D+4
de los ocho primeros ítems propios (`0x634049..0x634077`), con 3.848 rand al cerrar. El NewFace
vive en `SelectionState`, fuera de los 400; la cabeza persiste aun al recrear el objeto de escena.
El dibujo deja los corrimientos acumulados de `0x63e460` en ese mismo actor y toma la cámara
antes de sumarlos. En modo desacoplado solo acumula los frames originales del carry (D-C4).

Las escenas completas dibujan el cuerpo y la cabeza del pool. Los tests de UI sin WorldState ni
terreno usan un adaptador de presentación explícito: no simula un constructor nativo ni inventa
tablas. No sustituye los cortes del camino real. Siguen pendientes en P-C50 el cuerpo contextual
de los ayudantes, el pasto y los objetos de la entrada, el terreno dibujado de selección y los
créditos; no se declara cerrado el presupuesto
global de la escena ni su observación nativa. Evidencia estática: ASM c32bbd5f, sin nuevas emulaciones.

Regresiones del orden: `CharacterSelectSceneTest.ThePreviewSortsBeforeBothGlobalWindsAndTheStepCounter`,
`RavenSparksAgeAfterThePreviewAndTheirSlotsAreReused`,
`ASelectionHelperKeepsTheSceneFourAlphaPrefixBeforeTheMissingBody` y
`AParticleBindingFailureKeepsItsPrefixAndSkipsClothAndLaterSlots`;
`GoldenLoginWorld.TheLoginPublishesTheCharacterCutAndKeepsItsRandomState` conserva los
808 rand anteriores al corte, el contador y el viento. El pipeline prueba el pet nativo en
`AListPetReachesTheHelperBoundaryBeforeActorsParticlesAndCloth`.

**El lugar 2 del login, verificado estáticamente (2026-11-10).** `0x634a43` llama a
`0x517270(2, 4, 0, 0, 0, 0)`: construye el `0x20a` con `0x511290`, con X e Y lógicas en 0. Los siete cierres
de cuatro bytes (escribir X/Y, leer Y/X para el atributo, leer X/Y para la posición y leer el contador)
consumen 28 `rand()` con las cuentas en cero. Luego la clave 2, la luz propia `(0,3; 0,3; 0,3)`, la clase 4,
piel 0 y `+0x78/+0x7c` en 0. El equipo de `0x5172f5..0x51736d`: las partes `0x102e`, `0x122e`, `0x142e`,
`0x162e`, `0x182e`, todas de nivel 7; `0x620` de nivel 13 a la derecha, `0x1c17` a la izquierda, capa `0x1c30`
y mascota `0x1c16`. `0x512930` deja la cabeza en `0xffff`; después corre `0x4f2250`.

El caballo fuera de zona segura elige **`0x4a` antes de Item.bmd** (`0x4f22fb..0x4f230f`), y el sufijo
`0x4f36be` consume uno. El bloqueo de Item.bmd atribuido al final del gesto no aplica al lugar 2 mientras
`+0x3e2` siga en 0. `0x634a5a..0x634a81` pisa escala 1,6, ángulos `(0; 0; 188)` y posición
`(10.603; 14.112; 580)`. Crea el caballo y luego el cuervo (`0x634a9d`, `0x420d60`): sus 12 `rand()` son de
las celdas y el contador; nace en `(50; 50; altura(50,50) + 300)`, porque la celda lógica no cambió. Total
después del logo: **41**, con las cuentas cerradas. `ConstructLoginActor` reutiliza `ConstructActor` y
`SetActorStop`, conserva la cabeza anterior (ninguna escritura de `0x511290` la reinicia), y guarda el
cuervo en `ActorState::darkSpirit`. Con las dos celdas y el contador ya abiertos, sus cierres no gastan:
queda solo el `rand()` del sufijo (`LoginActorTest.OpenCellAndCounterAccessesKeepTheirBirthRandoms`).

El héroe sigue publicado en el lugar 0 al arrancar, pero **no está vivo**: es el cero de WinMain y
`0x6349a0` no lo construye. Puede quedar vivo al reentrar tras una lista: la carga (`0x61a5a4..0x61a5aa`)
pasa la clave del jugador a `0x510f70`, que solo mata otras claves. El paso no fuerza su estado ni su lugar;
un superviviente que necesite el registro propio puede cortar en Item.bmd.

El cuerpo mueve el cuervo en **`0x50544b`**, después de `0x4fcee0` y antes de `0x505de0`, también con su dueño
fuera del trapecio. La interfaz del paso recibe el `LoadedWorld` 55 y escribe sus paredes y su celda; no
exige una segunda copia en `WorldState::Map()`. El dibujo lee posición, partes, animación y cabeza del pool.
El cuervo conserva su pose para la chispa del paso siguiente. Los estados distintos de 0/1 o la zona segura
del cuervo, todavía sin portar, cortan con su dirección y los `rand()` anteriores.

Con el port, por paso, con la animación quieta (sin el `rand()` de `0x432bf1` desde el frame 3 ni el del sufijo
al terminar la acción) y los cinco lugares vistos con el trapecio de su dibujo (*simulado*):

| Escena | `rand()` por paso |
| --- | --- |
| la selección con los cinco y el héroe en el lugar 0, sin la vista previa | 40 del `TerrainWall`, 3.848 + 1 + 9 del héroe (su registro propio, su cuerpo y su `0x505de0`) y 8 + 1 + 8 + 1 + 1 de cada uno de los otros cuatro (su `0x508ba0`, su cuerpo, su celda, su cabeza y el `rand() & 7`): 3.974, y 2 más por cada cabeza que sortea |
| la selección después de una vuelta, sin la vista previa | 40 + 5 · 19 = 135, y 2 más por cada cabeza que sortea |
| el login con su personaje, con el cetro y el cuervo en las manos, sin crear el cuervo | 8 + 8 + 1 + 8 + 1 + 1 = 27, y 29 cuando la cabeza sortea |
| el lugar 2 construido, con su cuervo vivo en los estados 0/1 | 27 más los 4 a 7 del cuervo; 2 más cuando sortea la cabeza, 1 desde el frame 3 de `0x4fcee0` y 1 del sufijo de `0x4f2250` al terminar la acción; fuera de esta cuenta, el gesto de `0x634cca`, el viento, los ayudantes, las brasas y los demás efectos |

Tests: `GoldenCharacterStep.TheSelectionStepsItsFiveSlots`, `AfterTheReturnTheFiveSlotsAreOthers`,
`TheLoginStepsItsCharacter` (64, 64 y 200 pasos, con la cabeza de cada uno contra `Scenes::StepCharacterHead`) y
`WithoutItemBmdTheSelectionHeroStopsAtItsWings`. Las salidas de `0x508ba0`, en
`CharacterStepTest.AnotherCharacterRunsItsWholeStepBeforeTheHero` (Lorencia y Devias),
`TheExitsOf508ba0BeforeTheCell`, `AnotherCharacterWithOpenCellAccessesSpendsNothingOnThem` y
`OutsideTheGameTheHeroTakesTheSamePath`.

### El cuerpo de los personajes (`0x4fd5f0`)

`0x4fd5f0(c, o)`, desde `0x508ea0` (`0x508edf`), con `c` y `o` el mismo registro de `0x438` bytes. Ocupa
`0x4fd5f0..0x5054b3` (32.452 bytes), con un solo `ret` (`0x5054b3`) y `0x9e0` bytes de locales. `b` es el
modelo compartido del tipo, `[0x5769ad8] + tipo · 0xe0` (`0x4fd6b0`). Leído entero en el desensamblado de
`c32bbd5f` (CL7, hito 1): todo *estático*.

**La estructura**, en orden:

1. `0x4fd603..0x4fd693`: con el tipo `0xc5`, arma con `[0x6b851c]` (`sprintf`) el nombre (`+0x1d9`) y `+0x1d6`, y
   llama a `0x4381f0`.
2. `0x4fd69b..0x4fd6ef`: copia la posición (`+0x10..+0x18`), la escala (`+0xc`) y la acción (`+0x111`) a `b`
   (`+0x6c..+0x74`, `+0x68` y `+0xa0`).
3. Seis pasos sin condición en el que llama:
   - `0x4fbe10` (`0x4fd6f5`), que solo actúa en los mapas 18 a 23: compara la celda lógica del héroe con la
     anterior (`0x743a180..0x743a188`), la manda con `send` (`[0x6b8588]`) y, con error, cierra el socket
     (`0x4e8380`).
   - `0x4fc450(c)` (`0x4fd6fe`): con el héroe publicado (`[0x743a170]`), los tres WORD del registro propio
     (Q-C246).
   - `0x4fb380(c, o)` (`0x4fd70e`): con `+0x398` o `+0x399` gira y se corre; el `0xf1` cae (`0x4fb910`).
   - `0x4fb9e0(c, o, b)` (`0x4fd722`): con `+0x389`, muriéndose, sus partículas.
   - `0x4e9980(o)` (`0x4fd72e`): el alpha y la luz del blendMesh (Q-C112).
   - `0x4fd736..0x4fd75e`: con `+0x3d8` positivo, `+0x3d8 −= 0,03` (`0x6b8bd4`).
4. `0x4fcd00(c, o, b)` (`0x4fd770`), la animación:
   - Con `+0x88` en 4, los `0x20a` de subtipo `0xd1` a `0xd3` giran en los mapas 0 y 2, y `0x4fc790` sortea un
     mensaje.
   - Con el nombre de `0x6c88b0`, el mensaje de `0x557170` una vez cada más de 4.000 ms (Q-C248).
   - `0x4f9580`: la velocidad, con sus factores, y `0x4ec200` ([Caminar](#caminar)). Devuelve 0 cuando la acción
     terminó, y también con `+0x78 & 0x1000`, sin avanzar.
   - Si terminó: `+0x3c4 = −1` y `+0x3e0 = +0x3e1 = 0`. Después, según el tipo:
     - el `0x20a`, a `0x4fcb10`, que lo vuelve a la de parado con `0x4f2250` (las `0xc3` y `0xc4` van antes a
       `0x52e810`);
     - el `0xc5`, a `0x4fcbb0`;
     - el `0x1f5`, a un `rand() & 31` (`0x4fcea6`).
5. Muriéndose (`0x4fd77b..0x4fd7dc`): `+0x389 += 1` y `0x4f5660` desde 15, o en los mapas 11 a 17 con `+0x1a1`.
6. **`rand() % 6` sin condición** (`0x4fd7e4`, Q-C247).
7. Fuera del mapa 30 y con `+0x1a0` positivo (`0x4fd835..0x4fdada`): por cada hueso de `b` con padre, seis
   `rand()` y un `0x530860(0x614)`; después, `+0x1a0 −= 1`.
8. Las acciones `0x4d`, `0x4f` y `0x5a` pasado el frame 5,5 (`0x6b8a40`): `0x53fe70(0x5ac)` en el hueso 42
   (`0x4fdae0..0x4fdb92`).
9. **La habilidad** (`0x4fdb9a..0x50539e`):
   - Con `+0x381` positivo, `0x4f97b0(c, o)`, `0x4f5b70(c)` y `+0x381 += 1`.
   - Con `+0x381` debajo de `[0x6c88ac]` (15, en `.data`) salta a `0x50539e`. Si no:
     - `+0x381 = 0` y `o+0x8a = 0x558eb0(c, [0x6c8e30])`;
     - la habilidad de `+0x382`, que lee por la tabla de protección (`0x57630c8`, [§ La tabla de
       protección](#la-tabla-de-protección));
     - el `switch` de `0x4fdeeb`, de la 5 a la `0x4c` (índices en `0x505530`, saltos en `0x5054b4`);
     - lo de después del `switch`, con el blanco de `+0x3a4` y sin él (`0x502aff..0x50533f`);
     - el cierre del registro propio (`0x40f370`, `0x50534a`);
     - por último, `+0x382 = 0` y `+0x380 = 0`.
10. La cola (`0x5053a2..0x5054aa`):
    - `+0x428` positivo baja en 1 y, si queda entre −127 y 0, pasa a −128 y llama a `0x511070(c, o)`.
    - `0x42a2b0(o)`, que solo actúa con el `0x1a8`.
    - `0x4fcee0(c, o, b)` (`0x505410`): la estela del arma.
    - Con el `0xf1`, `0x52f360`.
    - `0x420dd0(c)`: con `+0x430`, una llamada virtual al objeto de `+0x430` (el cuervo del login, `0x416a20`;
      [§ Los efectos del login](#los-efectos-del-login)).
    - Muriéndose, apaga los bits `0x8000` y `0x10000` de `o+0x78`.

`0x50539e` salta a la mitad de una instrucción. En `0x5053a0` (`ff 15`), un `objdump` lineal muestra un
`call [0x8308458b]` que no existe, y el código sigue en `0x5053a2`. Lo mismo pasa en `0x50534f` y en `0x505367`.

**El camino del `0x20a` quieto.** Es el de un jugador vivo, de pie y sin habilidad. Por los valores que deja
`0x511290`, salta lo condicionado de arriba:

| Campo | Lo que deja `0x511290` | Quién más lo escribe |
| --- | --- | --- |
| `+0x381` | 0 | solo el punto 9 (*estático*) |
| `+0x1a0` | 0 (`0x511c62`) | lo sube solo `0x4f97b0` (`0x4f9ba2`), desde la habilidad |
| `+0x398` | 0 | lo escriben paquetes que MuRet no aplica |
| `+0x399` | 0 (`0x511d3e`) | lo escriben paquetes que MuRet no aplica |
| `+0x3d8` | 0 | en los personajes, solo la resta del punto 3 |
| `+0x428` | −128 (`0x511c77`) | los paquetes lo ponen en `0x96` |
| `+0x430` | 0 (`0x511878`) | — |

Le quedan los seis pasos del punto 3, la animación, el `rand() % 6`, la estela y `0x42a2b0` y `0x420dd0`, que
no hacen nada con él. En `0x4fcee0` (`0x4fcee0..0x4fd5df`), antes del frame 3 (`0x6b86b8`), o con NaN, y fuera de
la `0x8d`, copia la posición a `+0x17c` y sale. Si no, mira el arma, que es la mano izquierda con las acciones
`0x25` y `0x26` y la derecha con las demás, con el modelo como WORD con signo. Pide una estela (`0x52e470`, hasta
`0x4fd236`) en estos casos:

- las acciones `0x84`, `0x85` y `0x4c`, y de la `0x39` a la `0x3c`;
- un modelo de `0x212` a `0x411`, con las acciones `0x23` a `0x29` o con la `0x8d`;
- el `0x415` o un modelo de `0x417` a `0x811`, con las acciones `0x38` a `0x3c`;
- un modelo de `0x812` a `0xa11`, con las acciones `0x2a` a `0x2d`.

Sin estela, llama en `0x4fd5c1..0x4fd5df` a `0x441260`, `0x42fa40`, `0x432bd0` y `0x426d90`. De esas cuatro, solo
`0x432bd0` saca `rand()` con el `0x20a`: uno sin condición en `0x432bf1`, antes de mirar el tipo.

**Los `rand()`.** En el cuerpo de la función hay 28 llamadas directas a `0x69fd70`:

| Dónde | Qué | Con qué condición |
| --- | --- | --- |
| `0x4fd7e4` | `rand() % 6` | ninguna (Q-C247) |
| `0x4fd962`, `0x4fd98f`, `0x4fd9bc`, `0x4fda0d`, `0x4fda3a`, `0x4fda67` | `rand() % 41`, seis por hueso | fuera del mapa 30, con `+0x1a0` positivo |
| `0x4ffc65`, `0x50042c`, `0x500ba9`, `0x501432`, `0x501b52` | `rand() & 0xff` en un lazo de `0xf08`: el cierre en línea del registro propio, como `0x40b4d0` | las habilidades `0x26`, `0x27`, 5, `0xd` y `0xe`, con la cuenta del registro en 0 |
| `0x4ffd5c` | `rand() % 50`, diez veces, cada una con un `0x518cf0(0x108)` | la habilidad `0x27` |
| `0x502579`, `0x50261e`, `0x502647`, `0x502762`, `0x50278b`, `0x502887`, `0x5028bb`, `0x5028e4` | `rand() % 360` (`% 40` en `0x502887`), en los lazos de sus blancos | la habilidad `0x4c` |
| `0x5040fb`, `0x504127`, `0x504153` | `rand() & 63`, tres por cada uno de diez `0x53fe70(0x5e4)` | después del `switch`, con un blanco (`+0x3a4`) con `+0x384` en 1 o más y de otro tipo que el `0x159` |
| `0x5041db`, `0x50421d` | `rand() & 1` cinco veces y, cuando da 0, `rand() % 30` y un `0x518cf0(0x10b)` | lo mismo, con el blanco `0x18e` |
| `0x5052fd`, `0x505322` | `rand() & 3`, el sonido de `0x41ed10`: `0x4b` más el sorteo con el arma de `0x800` a `0x9ff`, y `0x46` más el sorteo con las demás | después del `switch`, en el caso `0x505224` del segundo `switch` (`0x5051f1`) |

La habilidad `0x9` cierra el registro propio con `0x40b4d0` (`0x50233a`).

Los de las llamadas, por función:

| Función | `rand()` | Cuándo |
| --- | --- | --- |
| `0x4fc450` | 3.848, el cierre en línea del registro propio | con la cuenta en 0 (Q-C246) |
| `0x4fb9e0` | 8 (`0x4fbba8..0x4fbda4`) | muriéndose |
| `0x4fc790`, desde `0x4fcd00` | 1 (`0x4fc7a4`) | con `+0x88` en 4 |
| `0x4fc8c0`, desde `0x4fcd00` | 6 (`0x4fc8c5..0x4fcaba`) | los subtipos `0xd1` a `0xd3` |
| `0x4fcbb0`, desde `0x4fcd00` | 3 (`0x4fcc9a..0x4fccdf`) | el `0xc5` |
| `0x4fcd00` | 1 (`0x4fcea6`) | el `0x1f5` |
| `0x4f2250`, desde `0x4fcb10` | 1, el de su sufijo | al volver a la de parado; en la escena 4, con el frame + 2 (Q-C21) |
| `0x432bd0`, desde `0x4fcee0` | 1 (`0x432bf1`), sin condición | cuando la acción pasa del frame 3 sin dejar estela |
| `0x432bd0`, desde `0x4fcee0` | 2 más (`0x432d6f` y `0x432d74`) | con los tipos `0x1ab` a `0x1b4` |
| `0x4f5660` | 1 | muriéndose |
| `0x52f360` | 8 | el `0xf1` |
| `0x4f97b0` y `0x4f5b70` | 11 y 36 sitios | la habilidad |
| `0x518cf0`, `0x530860` y `0x53fe70` | 212, 85 y 312 sitios en sus cuerpos | según lo que creen (54, 29 y 3 llamadas) |

Los que quedan dentro de la habilidad (punto 9) solo corren con `+0x381` en 15 o más.

**Las llamadas.** 56 destinos directos, más tres por puntero: `[0x6b851c]` dos veces y una virtual por
`0x57630c8`, en cada lectura protegida. Los tamaños van desde la entrada hasta el último `ret` (*estático*). Las
columnas son la dirección, los bytes, qué es y qué tiene MuRet.

| Llamada | Bytes | Qué es | En MuRet |
| --- | --- | --- | --- |
| `0x69fd70` | 34 | `rand()` | `Render::Models::CrtRandom` |
| `0x69f403` | 14 | `operator new` (CRT) | — |
| `0x69eff0`, `0x6a1920`, `0x6a19e0`, `0x6a1a90` | 11, 20, 20, 20 | funciones de la CRT | — |
| `0x40ad90`, `0x40ae60`, `0x40b0a0`, `0x40b240`, `0x40b2f0`, `0x40b3b0`, `0x40b3f0` | 194, 574, 413, 168, 183, 53, 78 | la tabla de protección (`0x57630c8`) | no se porta (protección) |
| `0x40b4d0`, `0x412890`, `0x40f370` | 157, 137, 526 | los cierres de la protección, con sus `rand()` | solo los `rand()` (`Render::Models::LogicalAccess`) |
| `0x412810` | 114 | un acceso de la protección | no se porta (protección) |
| `0x41fa40` | 49 | un aviso con formato (`0x6c637c`), cuando la tabla no tiene la entrada | no se porta (protección) |
| `0x41ed10`, `0x41ee00` | 156, 65 | el sonido | — (CL10) |
| `0x4381f0` | 500 | el nombre del `0xc5` | corta en `0x4fd603` |
| `0x4fbe10` | 1.599 | la celda del héroe por la red, en los mapas 18 a 23 | corta |
| `0x4fc450` | 819 | los WORD del registro propio | `StepTheOwnTimers` |
| `0x4fb380` | 1.619 | `+0x398`, `+0x399` y el `0xf1` | el `0xf1` corta en `0x4fb910` |
| `0x4fb9e0` | 1.058 | muriéndose | corta |
| `0x4e9980` | 191 | el alpha | `World::StepObjectAlpha` |
| `0x4fcd00` | 473 | la animación | `StepTheAnimation`, con el `0x20a`; corta en `0x4fcd0c` y `0x4fcda2` |
| `0x4f5660` | 1.185 | las partículas del que se muere | corta antes, en `0x4fb9e0` |
| `0x4ec110` | 120 | un punto de un hueso | `Render::Models::OriginalRotation`; acá no se llega |
| `0x530860`, `0x53fe70`, `0x518cf0` | 17.516, 15.049, 27.599 | los joints, las partículas y los efectos | aparte, para el caballo y el login; acá cortan antes |
| `0x4e93f0` | 195 | el rumbo de un punto a otro | `Render::Models::OriginalAngles`; acá no se llega |
| `0x5f93c0` | 349 | la altura del terreno | `World::TerrainHeightAt`; acá no se llega |
| `0x5588d0`, `0x558eb0` | 731, 327 | leen Ctrl (`Input/Key.h`), desde la habilidad | — |
| `0x4f97b0`, `0x4f5b70`, `0x59fb80`, `0x53e2b0`, `0x5526f0`, `0x517630`, `0x42ea00`, `0x51fff0`, `0x52e740`, `0x534f90`, `0x534fd0`, `0x4f2230`, `0x4f5b10`, `0x437860`, `0x4e79a0` | 6.489, 13.523, 652, 1.667, 660, 494, 435, 382, 197, 62, 69, 32, 93, 43, 667 | sin leer: solo los llama la habilidad (punto 9) | — |
| `0x52f360` | 482 | el `0xf1` (`0x505433`), sin leer | corta antes, en `0x4fb910` |
| `0x511070` | 84 | con `+0x428` entre −127 y 0, sin leer | no se llega |
| `0x42a2b0` | 542 | el `0x1a8` | con el `0x20a` no hace nada |
| `0x4fcee0` | 1.806 | la estela del arma | `StepTheWeaponTrail`: sin estela; corta en `0x4fd236` |
| `0x420dd0` | 23 | la virtual de `+0x430`, el cuervo del login (`0x416a20`) | `ActorState::darkSpirit`, dentro del cuerpo en 0x50544b; los estados 0/1 del login; sin recursos o fuera de ese alcance, error explícito |

**Los globales.**

| Global | Qué es | Cómo |
| --- | --- | --- |
| `[0x6c9dd0]` | el mapa | lee |
| `[0x6d5dc8]` | la escena | lo lee `0x4ec200`, desde `0x4f9580` (Q-C21) |
| `[0x743a170]` | el héroe publicado | lee |
| `[0x743a16c]` | la base de los registros de los personajes | lee, en la habilidad |
| `[0x7681eb0]`, `[0x7681eb4]` | el registro propio y su copia protegida | leen y escriben `0x4fc450` y la habilidad |
| `[0x6eed3c]` | las tablas de los conjuntos | `0x54efe0` y `0x54f9b0`, desde `0x4fc450` |
| `[0x5769ad8]` | los modelos compartidos | lee y escribe `b` |
| `[0x6c88ac]` | 15, el umbral de `+0x381` | lee |
| `[0x6c8e30]` | −1 | lo pasa a `0x558eb0` |
| `0x6c88b0` | el nombre del mensaje | lo lee `0x4fcd00` |
| `0x743a174`, `0x7437488`, `0x743a190` | el bit de la primera vez, la hora del último mensaje y cuál de los dos va (Q-C248) | lee y escribe `0x4fcd00` |
| `0x743a180..0x743a188` | la celda anterior del héroe | `0x4fbe10` |
| `0x57630c8..0x57630d4` y `0x57630f0` | la tabla de protección | todas las lecturas protegidas |
| `[0x7879fbc]`, `[0x78ab9b8]`, `[0x78a6758]` | el ítem que reemplaza al del lugar (P-C38 d) | en el sonido |
| `[0x5767948]` | — | un byte que lee la habilidad `0x31` |
| `0x6b8xxx` | constantes de `.rdata` (0,1f, 0,03, 5,5, 3, …) | lee |

**El presupuesto del héroe quieto** (*simulado*, con el port). Es el héroe en la zona segura de Lorencia (el 0,
en (172, 128)) y de Devias (el 2, en (220, 76)), visto, desarmado y en la acción 1 de Player.bmd (6 keys, 0,28 por
paso, repite), con el registro propio de la respuesta del F3 03 (equipo vacío y los tres WORD en 0). Cada paso
de `0x508f00` saca:

| Parte | `rand()` |
| --- | --- |
| el `TerrainWall` (dos accesos a la celda lógica) | 8 |
| `0x4fc450` (el cierre del registro propio) | 3.848 |
| `0x4fd7e4` | 1 |
| `0x505de0` (la celda) | 8 |
| `0x505de0` (el `rand() & 7`) | 1 |
| **total** | **3.866** |

Desde el paso 11, cuando el frame pasa de 3, `0x432bf1` saca uno más: 3.867. En el paso 22 la acción termina
(6,16), vuelve al frame 0,16 y `0x4f2250` saca el de su sufijo en lugar del de `0x432bf1`: 3.867. Después el ciclo
se repite. Es el mismo en los dos mapas (`GoldenCharacterStep.TheHeroAtRestDrawsTheSameBudgetOnLorenciaAndDevias`).

**En MuRet** (`Scenes::StepCharacters`, `StepTheBody`), con el `0x20a`:

- Corre, en el orden del original, lo de arriba: el registro propio (`0x4fc450`, con los helpers que ya estaban:
  `UpdateOwnAttackAndMagicSpeeds`, `UpdateOwnWords50To56` y `UpdateOwnWords5aAnd5c`), el alpha (`0x4e9980`, con
  `+0x16d` y la luz del blendMesh, que `0x511290` pone en 1 y en 1,0), la animación (`0x4fcd00` → `0x4f9580` →
  `0x4ec200`, y `0x4fcb10` → `0x4f2250` al terminar), el `rand() % 6` y el `rand()` de `0x432bf1`.
- Los campos que este camino no alcanza no existen en `ActorState`: lo de arriba queda escrito en comentarios.
- Corta con error, después de los `rand()` de antes:
  - en los mapas 18 a 23 (`0x4fbe10`);
  - con el `0xc5` (`0x4fd603`) y el `0xf1` (`0x4fb910`);
  - muriéndose (`0x4fb9e0`);
  - con `+0x88` en 4 (`0x4fcd0c`) y con el nombre de `0x6c88b0` (`0x4fcda2`);
  - con otro modelo o sin Player.bmd (`0x4f9580`), y con una acción fuera de él (`0x4f95c0`);
  - en las acciones `0x94`, `0x51` y `0x5b` de la clase 4 (`0x4f969d`);
  - en las `0xc3` y `0xc4` que terminan (`0x4fcb87`);
  - en la partícula de las `0x4d`, `0x4f` y `0x5a` (`0x4fdb70`);
  - en la estela (`0x4fd236`).
- Sin Item.bmd, `0x54efe0` corta con equipo que no sea el vacío de la respuesta del F3 03 (Q-C246).
- Tests: `CharacterStepTest` y `GoldenCharacterStep`, `GameSceneDrawTest.WithoutPlayerBmdTheStepStopsAtTheBody` y
  `ActorConstructionTest.LeavesThePlayerUnseenAndNotDying`.
- Lo que falta, en P-C52.

### Los efectos de los personajes (`0x505de0`)

`0x505de0(c, o)`, desde `0x508ea0` (`0x508ee6`), después del cuerpo, con `c` y `o` el mismo registro (`ebp+8` y
`ebp+0xc`; `ebx` es `o`). Ocupa `0x505de0..0x50880a` (10.794 bytes), con dos `ret`: el de la caja (`0x505ec5`) y el del
final (`0x508809`). `b` es el modelo compartido del tipo, `[0x5769ad8] + tipo · 0xe0` (`0x505ded..0x505e08`). Leído
entero en el desensamblado de `c32bbd5f` (CL7, hito 1), con un personaje que no es el héroe publicado y en las
escenas 2, 4 y 5: todo *estático*. Es la misma función para todos: el héroe y los demás se separan solo en la cabeza
(`0x5068eb`) y en los pasos (`0x508641`), y las escenas, solo en el `0x607` del mapa 7 (`0x506cf8`).

**La estructura**, en orden:

1. `0x505e0b`: sin acciones en `b` (`+0x26` en 0) va a **la caja** (`0x505e15..0x505ec5`): `+0x13c..+0x144` la
   posición más `+0x124..+0x12c`, `+0x148`, `+0x158` y `+0x168` el tamaño (`+0x130..+0x138` menos `+0x124..+0x12c`), y
   los demás de `+0x14c..+0x164` en 0. Sale, sin `rand()`.
2. `0x505ec6..0x505ee4`: la posición, la escala y la acción (`+0x111`) van a `b` (`+0x6c..+0x74`, `+0x68` y `+0xa0`).
3. `0x505ef2`: sin verse (`+0x16c` en 0), sale (`0x508804`).
4. **La zona segura** (`0x505ef8..0x5068b2`): la Y lógica (`+0x420`) y la X (`+0x41c`) por la tabla de protección,
   cada una en un acceso que se cierra en línea con cuatro `rand() & 0xff` si su cuenta queda en 0 (`0x506362` y
   `0x506833`), y `+0x3e2` en 1 si el bit `0x01` del byte bajo de `0x7d0d348[0x5f82a0(X, Y)]` está, y en 0 si no
   (Q-C206).
5. **La cabeza** (`0x5068b9..0x5069ab`), con `+0x78` sin `0x20` ni `0x1000` (`0x5068be..0x5068d4`):
   - con otro modelo que el `0x20a`, `0x4ea190(c)` (`0x5068e3`), que según la acción sortea otro blanco con hasta tres
     `rand()` (`0x4ea1ab`, `0x4ea1c6` y `0x4ea1e2`), mira a otro personaje (`0x4ea0e0`), no hace nada o lo pone en 0
     (P-C17);
   - con otro que el héroe publicado (`0x5068eb`, `c` contra `[0x743a170]`) y sin morirse (`+0x389`, `0x5068f3`):
     `rand() % 32` (`0x5068fd`) y, si da 0, el blanco: `+0x34 = rand() % 128 − 64` (`0x506913`) y
     `+0x38 = rand() % 32 − 16` (`0x50692f`), cada uno con 360 más si da negativo (`0x506953..0x506970`);
   - con todos, también el héroe: `+0x28` y `+0x2c` giran hacia `+0x34` y `+0x38` con `0x4e9560`, a lo sumo
     `f(0x4e96f0(ángulo, blanco, 1) · 0,2f)` (`0x506972..0x5069a9`). Del héroe, el blanco lo escribe MoveHero con el
     mouse (`0x59b1c2..0x59b287`). [§ Lo demás del login](#lo-demás-del-login).
6. Con `+0x3e5` positivo (`0x5069ae`, sin signo): `+0x3e5 − 1` y veinte vueltas (`0x5069d1..0x506ac6`) con la luz
   `+0xf4..+0xfc` en 1 y, en cada una, una posición de tres `rand()` (`% 64 − 32`, `% 64 − 32` y `% 32 − 16`, en
   `0x5069e4`, `0x506a06` y `0x506a26`), `rand() % 10` en 0 (`0x506a46`) para una partícula `0x5f3`, y `rand() % 10` en
   0 (`0x506a7b`) para un efecto `0xc8` o `0xc9` (`rand() & 1` en `0x506aa0`, `0x518cf0`). Solo lo sube `0x4c9790`
   (`0x4c982b`).
7. Con `+0x375` debajo de 6, sin signo (`0x506acf`): la luz de cada mano, la derecha (`+0x2b8`) y la izquierda
   (`+0x2d8`): `0x21e` con un 3 y `0x225`, `0xa24`, `0xc1c` y `0x61f` con un 2, por `0x5f9780` al buffer dinámico
   `0x7b4ccc4`, sin `rand()`.
8. Con `+0x3d8` positivo (`0x506b64`): con `+0x3d4` en `0x611`, `rand() & 3` (`0x506b87`) y, si da 0, una posición de
   tres `rand()` (`% 100 − 50`, `% 100 − 50` y `% 180`, en `0x506b9a`, `0x506bb6` y `0x506bd2`); con `0x612`, un
   `0x4ec110` por cada tantos huesos de `b` (`ftol(0x6b8a44 / +0x3d8)`, al menos 1) a un local. Ninguno de los dos crea
   nada (Q-C266). En los personajes, `+0x3d8` solo lo baja `0x4fd5f0` (`0x4fd75e`).
9. **`rand() & 7` sin condición** (`0x506cac`): `(r + 2) · 0,1f` (`0x6b87b8`) a `ebp+0xc`, que leen algunos casos de
   abajo (el `0x15d` en `0x507e2a`, por ejemplo) y el del `0x20a` no (Q-C207).
10. **El caso del tipo** (`0x506ccc..0x506cf1`): `tipo − 0x152` contra `0xb8`, con los índices en `0x508884` y los
    saltos en `0x50880c`; fuera del rango (todos los tipos debajo de `0x152`, como la vista previa `0xa9`) o sin caso
    propio, el de `0x5085db`. Abajo, la tabla.
11. **Los pasos del héroe** (`0x50862f..0x5086ca`): con el `0x20a` y `c` el héroe publicado (`0x508641`), en las
    acciones `0x0d` a `0x21`, `0x8b`, `0x8c` y `0x4b`, el sonido de `0x505770` con el frame (`+0x114`) desde 1,5
    (`0x6b8b94`) y `+0x3e0`, y desde el de `0x6b8ae0` y `+0x3e1`, sin `rand()`. Los demás no tienen pasos acá.
12. **Kalima** (`0x5086cf..0x508801`): con el `0x20a` en las acciones `0x15`, `0x1d` a `0x21` y `0x4b`, en los
    mapas 24 a 29 y 36, la partícula `0x6f4` (`0x53fe70`, subtipo 1, escala 1, luz 0,3) en (0, −40, 0) girado por
    `+0x1c` (`0x5fc8f0` y `0x5fcbd0`) más la posición, con `rand() % 64 − 32` en la x (`0x508793`) y en la y
    (`0x5087b6`), y la z más 50 (`0x6b86b0`). Con el héroe esas acciones son también de sus pasos, que en MuRet
    cortan antes.

**El caso del tipo** (`0x506cf1`). Cada caso termina en `0x50862f`. Los `rand()` directos y lo que crean:

| Tipo | Caso | `rand()` directos | Lo que hace |
| --- | --- | --- | --- |
| `0x20a` | `0x506cf8` | `0x506db7`, `0x506e07` y `0x506f7a` | en la escena 5 y el mapa 7, un `0x607` con `ftol(0x5767b88) % 10000` debajo de 1000; en el mapa 9, dos `rand() & 3` que en 0 sacan un `0x68` cada uno; en las acciones `0x44` y `0x45`, con `+0x120`, partículas `0x5ac` por hueso y `0x5189f0` (2 `rand()`); en la `0x96`, diez `0x5d9` en el hueso `rand() % huesos`; y un `0x4ec110` del hueso de `+0x2bd` a un local que nadie lee (`0x506fc1..0x507001`) |
| `0x185` | `0x507006` | — | doce joints (`0x530860`) por huesos |
| `0x186`, `0x189`, `0x16d`, `0x16c`, `0x161` | `0x5073c6`, `0x5073ed`, `0x507ab7`, `0x507ade`, `0x507e63` | — | escriben campos con el reloj (`ftol`) |
| `0x182` | `0x507414` | `0x507414`, `0x507429` y `0x50744b` | `rand() % 5` en 0: dos `rand() % 21 − 10` y una marca del suelo `0x5e3` (`0x548fe0`, que saca uno más) |
| `0x17e` | `0x50749c` | — | partículas, luces (`0x5f9780`) y `0x505bc0`, `0x505c50` (3 `rand()`) y `0x505d50` (2) |
| `0x17f`, `0x1aa` | `0x507493` | — | siguen en el del `0x17d` (`0x5078a0`) |
| `0x17d`, `0x17c`, `0x17b`, `0x179` | `0x50789c`, `0x5078b5`, `0x50798e`, `0x5079ad` | — | `0x505bc0`, `0x505c50` y `0x505d50`, y en el `0x17c` partículas y una luz |
| `0x177` | `0x5079c0` | — | escribe campos |
| `0x16f` | `0x507a20` | `0x507a49` | con el reloj, `rand() & 1` en 0 saca un `0x5d9` |
| `0x172` | `0x507b05` | `0x507d5f` | según la acción, `0x5ca` y `0x5d9` en huesos; en la 6, uno en el hueso `rand() % huesos` |
| `0x15d` | `0x507dab` | `0x507dab` y `0x507de3` | un `rand()` y, según `+0x1d6`, `0x5d9` en huesos de `rand()` y una luz |
| `0x1d8`, `0x1d3` | `0x507e8a`, `0x507eb7` | `0x507e8a`, `0x507eb7` | un sonido (`0x41ed10`) con `rand() & 63` o `& 255` |
| `0x1ce` | `0x507ee6` | `0x508005` y `0x508028` | sonido, luz, un joint `0x617` y un `0x5c5` |
| `0x1f5` | `0x508093` | `0x508128`, `0x508148` y `0x508188` | luz, un joint `0x617` y, con `rand() & 1`, un `0x5c5` |
| `0x154` | `0x5081c9` | `0x508209` | en la acción 3, `rand() % 32` y un `0x5d9` |
| `0x155`, `0x158`, `0x166` | `0x50826d` | `0x5082f6`, `0x50830d`, `0x50832d` y `0x50834d` | un sprite `0x5ac` (`0x5491e0`) y, con `rand() & 3`, un `0x5f2` |
| `0x173` | `0x5083a1` | `0x5083c0`, `0x5083d7`, `0x5083f7` y `0x508417` | con `+0x1d6` en 1, `rand() & 3` y un `0x5f3` |
| `0x157` | `0x50845e` | — | `0x505c50` (3 `rand()`) |
| `0x15e`, `0x15f` | `0x50846c` | `0x50846c` | `rand() & 3` y sigue en el del `0x152` (`0x5085ac`) |
| `0x152` | `0x5084b3` | `0x50856a` | según la acción y el frame, `rand() & 1` y un `0x5f2` |
| `0x183`, `0x184`, `0x188` | `0x50862f` | — | nada |
| los demás | `0x5085db` | — | `0x43bdd0`, `0x42de90`, `0x441210`, `0x42f550`, `0x431470` y `0x426ce0`, en ese orden, hasta que uno devuelva verdadero: cada uno mira el tipo o el mapa |

**Los `rand()`.** 46 llamadas directas a `0x69fd70`. Con el `0x20a`, que es lo que MuRet tiene:

| Dónde | Qué | Con qué condición |
| --- | --- | --- |
| `0x506362`, `0x506833` | `rand() & 0xff`, cuatro cada uno: los cierres de la celda lógica | visto, con la cuenta de cada acceso en 0 |
| `0x5068fd` | `rand() % 32` | visto, sin `+0x78 & 0x20` ni `& 0x1000`, otro que el héroe y sin morirse |
| `0x506913`, `0x50692f` | `rand() % 128` y `rand() % 32`, el blanco de la cabeza | lo mismo, con el de `0x5068fd` en 0 |
| `0x5069e4`, `0x506a06`, `0x506a26`, `0x506a46`, `0x506a7b`, `0x506aa0` | tres de la posición, dos `% 10` y `& 1`, veinte veces | visto, con `+0x3e5` positivo; el de `0x506aa0`, con el de `0x506a7b` en 0 |
| `0x506b87`, `0x506b9a`, `0x506bb6`, `0x506bd2` | `rand() & 3` y tres de una posición | visto, con `+0x3d8` positivo y `+0x3d4` en `0x611`; los tres, con el primero en 0 |
| `0x506cac` | `rand() & 7` | visto, sin condición (Q-C207) |
| `0x506db7`, `0x506e07` | `rand() & 3` | visto, en el mapa 9 |
| `0x506f7a` | `rand() % huesos`, diez veces | visto, en la acción `0x96` |
| `0x508793`, `0x5087b6` | `rand() % 64` | visto, en Kalima y en las acciones `0x15`, `0x1d` a `0x21` y `0x4b` |

Los otros 28 son de los casos de otros tipos (la tabla de arriba). Los de las llamadas: `0x4ea190` (3, con otro
modelo), `0x5189f0` (2), `0x548fe0` (1), `0x505c50` (3), `0x505d50` (2), `0x43bdd0` (4), `0x42f550` (2), `0x431470`
(6), y los de `0x53fe70`, `0x518cf0` y `0x530860` (312, 212 y 85 sitios en sus cuerpos). `0x4e96f0`, `0x4e9560`,
`0x5f9780`, `0x505770`, `0x4ec110`, `0x5491e0`, `0x41ed10`, `0x5fc8f0` y `0x5fcbd0` no sacan.

**Las llamadas.** 30 destinos directos, más la virtual de la tabla de protección (`[0x57630c8] + 0xc`) en cada
lectura de la celda. Los tamaños van desde la entrada hasta el último `ret` (*estático*):

| Llamada | Bytes | Qué es | En MuRet |
| --- | --- | --- | --- |
| `0x69fd70` | 34 | `rand()` | `Render::Models::CrtRandom` |
| `0x69f403`, `0x69eff0`, `0x69fd3c` | 14, 11, 39 | `operator new`, `operator delete` y `ftol` de la CRT | — |
| `0x41fa40` | 49 | el aviso de la tabla de protección sin la entrada | no se porta (protección) |
| `0x5f82a0` | — | el índice de la celda | `Data::Terrain::CellIndex` |
| `0x4ea190` | 208 | la cabeza de otro modelo | corta antes, en `0x505e0b` |
| `0x4e96f0`, `0x4e9560` | 169, 394 | la distancia entre dos ángulos y el giro | `Render::Models::AngleDistance` y `TurnAngle` (`Scenes::StepCharacterHead`) |
| `0x53fe70`, `0x518cf0`, `0x530860` | 15.049, 27.599, 17.516 | las partículas, los efectos y los joints | aparte, para el login; acá cortan antes |
| `0x5f9780` | 904 | la luz al buffer dinámico | corta en `0x5f9780` |
| `0x4ec110` | 120 | un punto de un hueso | `Render::Models::OriginalRotation`; el del `0x20a` (`0x506ffc`) va a un local que nadie lee |
| `0x5189f0` | 206 | los efectos de las `0x44` y `0x45` | corta en `0x506e5d` |
| `0x548fe0` | 168 | llena una marca del suelo ([Las marcas del suelo](#las-marcas-del-suelo)) | solo el `0x182`; no se llega |
| `0x505bc0`, `0x505c50`, `0x505d50` | 132, 242, 129 | luces y partículas de los `0x157` y `0x179` a `0x17e` | no se llega |
| `0x4e9810` | 186 | un rumbo (`0x4e93f0` y `0x4e9560`) | no se llega |
| `0x5491e0` | 161 | un sprite del frame | no se llega |
| `0x41ed10` | 156 | el sonido | — (CL10) |
| `0x505770` | 161 | los pasos del héroe | corta en `0x508668` |
| `0x5fc8f0`, `0x5fcbd0` | 198, 84 | el giro de los ángulos y de un vector | `Render::Models::OriginalRotation`; el `0x6f4` corta en `0x5087fc` |
| `0x43bdd0`, `0x42de90`, `0x441210`, `0x42f550`, `0x431470`, `0x426ce0` | 657, 91, 71, 1.229, 2.609, 173 | los de los tipos sin caso propio | no se llega |

**Los globales.** `[0x5769ad8]` (los modelos compartidos), `[0x743a170]` (el héroe publicado, en `0x5068eb` y
`0x508641`), `[0x6d5dc8]` (la escena, en `0x506cf8`), `[0x6c9dd0]` (el mapa, en `0x506d01`, `0x506d7f` y `0x508700`),
`0x5767b88` (el reloj del mundo, con `ftol`), `0x7d0d348` (el `TerrainWall`), `0x7b4ccc4` (el buffer dinámico de
luces), la tabla de protección (`0x57630c8..0x57630d4`, `0x57630f0` y `0x6c78c8`) y constantes de `.rdata` (0,2f,
360, 0, 0,1f, 32, 50, …).

**Con otro que el héroe**, quieto y visto, el `0x20a` saca por paso, en `0x505de0`: 8 de la celda, 1 de la cabeza
(3 cuando sortea, uno de cada 32 pasos en promedio) y el `rand() & 7`, si no corta. Sin verse, ninguno. En la escena
4 y en la 2 es igual, sin el `0x607` del mapa 7.

**En MuRet** (`Scenes::StepCharacters`, `StepTheEffects`, CL7 hito 1), con el `0x20a`, el héroe y los demás:

- la zona segura, la cabeza (`Scenes::StepCharacterHead` sobre `ActorState::headTurn`, `+0x28..+0x38`), que con
  `+0x78 & 0x20` o `& 0x1000` no se mueve; con el héroe gira sin sortear hacia un blanco que todavía no escribe nadie
  (MoveHero, CL7.4);
- el `rand() & 7` y el caso del `0x20a`, con los pasos solo del héroe;
- corta con error, después de los `rand()` de antes:
  - con otro modelo (`0x505e0b`);
  - con la luz de una mano (`0x5f9780`);
  - en el 9 (`0x506d8b`) y, en el mapa 7 de la escena 5, sin el pool de partículas para la burbuja (`0x506d77`);
  - en las acciones `0x44` y `0x45` (`0x506e5d`) y `0x96` (`0x506f62`);
  - en los pasos del héroe (`0x508668`);
  - en el `0x6f4` de Kalima, después de sus dos `rand()` (`0x5087fc`).
- `+0x3e5` lo sube un paquete que MuRet no aplica (`0x4c9790`) y `+0x3d8`, fuera de los personajes, la UI
  (Q-C266): quedan en 0 y esas ramas no se alcanzan.
- Tests: `CharacterStepTest.AnotherCharacterRunsItsWholeStepBeforeTheHero`,
  `TheHeadOfAnotherCharacterSortsOneStepInThirtyTwo`, `TheHeroTurnsItsHeadWithoutRandoms`,
  `TheFlagsOf78KeepTheHeadStill`, `TheStepSoundsAreOnlyTheHeros` y
  `AfterItsHeadAnotherCharacterTakesThePathOfTheHero`, en Lorencia y en Devias, y
  `KalimaParticleCutsAfterItsTwoRandoms`, en los mapas de Kalima y los de al lado; las escenas, en [§ El paso de
  los personajes en la selección y el login](#el-paso-de-los-personajes-en-la-selección-y-el-login).
- Lo que falta, en P-C54.

### El dibujo del juego

`0x6371a0`, en el orden de sus llamadas (*estático*; lo que está en la traza, *verificado*). La
última columna indica qué está conectado actualmente en MuRet:

| Dónde | Qué llama | Qué hace | ¿Como en el login? |
| --- | --- | --- | --- |
| `0x6371a6` | con `0x7d3a101` en 0, devuelve 0 | sin leer el contador | `GameScene::PrepareFrame` |
| `0x6371b7..0x63722e` | el contador por la tabla de protección, con su cierre (`0x40b800`, `0x637229`); **con más de 30, devuelve 0** (`jle` con signo) | no dibuja ni presenta | `GameScene::PrepareFrame` (T5) |
| `0x63723a` | `0x7d368a0 = 0` | sin niebla | sí |
| `0x637241` | **`0x635ae0()`** y, fuera del mapa 6, `0x44dc60(&p)` (`0x4034b0` con el recorrido) | la cámara y el punto del recorte; la rama de `0x63724a` no corre nunca (Q-C189) | [La cámara del juego](#la-cámara-del-juego): `GameScene`, con los dos accesos a la celda del héroe (8 `rand()`) y `World::CameraTargetOf`; en el mapa 6 con "webzen" en el nombre, la cámara de espectador sin esos accesos y el punto (6.400; 15.400) con la z del héroe ([Los objetos de la Arena](#los-objetos-de-la-arena)) |
| `0x63734d` | `0x620db0(0, 0, w, 48)` | la franja, que no dibuja | sí (`MakePass3D`) |
| `0x637366` | **`0x5fbb00(w / 640, &p)`** | el rango de bloques y el trapecio | sí (`TerrainRangeOf`, `TerrainPolygonOf(Game)`; en el juego, `StepTheTerrainPolygon` con el trapecio y el nivel efectivo globales de `WorldRuntime`, T11b) |
| `0x637391..0x637404` | `glClearColor` por mapa: en el 0, (0,0390625; 0,078125; 0,0546875) | no se usa (Q-C143) | ver abajo |
| `0x637410` | `0x620db0(0, 0, w, 432)` (480 con `0x7d36899`) | el pase del mundo | sí (`GameWorldViewport`) |
| `0x63741d` | en el mapa 30, con el héroe en el rectángulo de `0x42b0a0`, la niebla; si no, `glDisable(GL_FOG)` | | afuera, como siempre; adentro corta en `0x637481` ([Los objetos de Castle Siege](#los-objetos-de-castle-siege)) |
| `0x6374aa` | `0x620480(mouse x, mouse y, &0x7d366c0, 1)` | el rayo del mouse, con el mouse de 640 × 480 vuelto a la pantalla en enteros (`0x620486..0x6204b0`) | `GameScene::MouseRay`, para [Caminar](#caminar) |
| `0x6374c6` | **`0x5fc260(0)`**, si `0x437850()` es falso y el mapa no es 10 | el terreno: el pase opaco, el de alfa, **el del pasto** y `0x549130` | `LoadedWorld::DrawTerrain`, **sin el pasto ni las marcas** (P-C48 b, P-C49); el juego dibuja el primer pase, el del pasto y las marcas del suelo (`0x5fc365`); en el mapa 7, el agua de `0x5fa4b0` y sin el pasto ([Los objetos de Atlans](#los-objetos-de-atlans)), y en los 18 a 23 corta antes de la primera celda (`0x5fa83f`, Q-C229). Abajo, por dentro |
| `0x6374ce` | **`0x6046a0` → `0x602e90` y `0x602ed0`** | objetos por sector | `DrawGameWorldObjects` en0/2/3, con la fuente69 que lee matrices retenidas y los servicios de Noria; otros mapas/casos conservan fronteras |
| `0x6374d3` | **`0x52dc20`** | los efectos del suelo (`0x61d`, `0x61e`, `0x61f`…), con `0x5fb2f0` ([Después de los objetos](#el-dibujo-del-juego)) | `Scenes::DrawGroundEffects`: filtra vivos y vistos, consume el rand común y dibuja los casos acreditados; `GroundEffectFrame` evita repetir sus mutaciones entre frames originales (Q-C278) |
| `0x6374d8` | **`0x446b60`** | los pájaros, con su sombra | `Scenes::DrawGameBirds`, los 40 lugares durables, cuerpo/glows/sombra y [pose compartida](#la-pose-compartida-entre-escenas) |
| `0x6374dd` | **`0x510d00` → `0x50ba30`** | los personajes vistos (`+0x16c`), por índice; `0x42afe0` para los jugadores, solo en el mapa 30; al final, las puertas (`0x42da60`, Q-C276) | `Scenes::DrawGameCharacters`: jugador, sombra, scan, luz dinámica, color del héroe y partes/tela reutilizando `DrawCharacter`, y lo que cuelga con `Scenes::DrawGameHanging` (espalda, alas, Helper02 y manos); cortes explícitos en monstruos y efectos pendientes; abre puertas al completar el recorrido |
| `0x6374ed` | `0x5fc260(1)` con `0x77f3b18` | el picking del terreno con esa bandera (*inferido*: el editor) | — |
| `0x6374fe` | `0x608a00`, con `0x7d36899` en0 | ítems del piso | `DrawFloorItems`: ramaSword01..19 nivel0 y pose global; las opciones/familias CL9.3 restantes conservan error, ya no corta universalmente |
| `0x637503` | **`0x446f00`** | los peces, con su sombra | `Scenes::DrawGameFish`, los 10 lugares durables y sus excepciones de sombra; [fauna](#la-fauna-y-sus-recursos) |
| `0x637508` | `0x4453b0` | helpers/caballo oscuro | `DrawHelpers` con recursos, reloj, RNG y pose retenida del mismo pase; caller conectado después de peces |
| `0x63750d` | **`0x5306a0`** | hojas del mundo | `DrawGameLeaves`, con texturas físicas64..68 cargadas porTerrainTiles; paso/productores por mapa |
| `0x637516` | `0x53c6a0(local_8)` | joints, modo0/1 | `DrawJoints`, familias portadas y errores de UV/colores especiales; recursos610/613/617/618/697 cargados |
| `0x63751d` | **`0x52d620(0)`** | efectos modelados | `DrawGameModelEffects`, con scratch publicado/consumido y copia para cuadros intermedios; familias ausentes conservan error |
| `0x637525` | `0x52e590` | cintas en `GL_TRIANGLE_FAN` | `DrawRibbons` consume el pool durable; sus productores/recursos5bb..bd/639 siguen enCL9–CL11 |
| `0x63752a` | **`0x549510`** | marca los sprites del frame | `MarkSeen`, muestra previa al primer ojo para replay y buffer proyectado de cuadros intermedios |
| `0x63752f` | `0x6213e0` | la modelview en la identidad | sí |
| `0x63754c` | `0x5306a0`, mapa2 y capa signed distinta de3 y menor que10 | hojas de ojo | `GameTailPasses` aplica la guarda en ambos ojos |
| `0x637552` | **`0x549440(local_8)`** | sprites, modo0/1 | `DrawFrameSprites`; modo1 conserva vivos y modo2 filtra/consume la banda correspondiente |
| `0x637558` | **`0x548550(local_8)`** | partículas | `DrawGameParticles`, pool durable y filtros350/300; conserva cortes por familia |
| `0x63756a` | `0x548eb0(local_8)`, sólo si437850 esfalse | números de daño | `DrawDamage`, atlas1FontTest real; paso548f40 sin apagar los slots en dibujo |
| `0x637572` | `0x6213f0` | el `glPopMatrix` | sí |
| `0x637577..0x6375e5` | segunda consulta437850;620db0,4378a0 y cola | segunda capa | orquestador y modos portados; backend/loader Kalima permanecen explícitos437850/4378a0/4377a0 |
| `0x637641` | `0x59ec80` | qué personaje está bajo el mouse | [Caminar](#caminar) |
| `0x637646..0x6376e6` | `0x6219b0`, `0x4383f0`, `0x5bbbc0`, `0x5a7320(1)`, `0x5a7eb0`, `0x490390`, `0x5f37f0`, `0x62db60`, `0x5f7bc0`, `0x554810`, `0x5554e0`, `0x5b0550`, `0x40eca0`, `0x65ee60`/`0x65fe00`, `0x5ac2d0` × 2, `0x633110`, `0x621a90`, `0x620fc0` y uno por vtable | el 2D: el HUD, las ventanas, el texto y el cursor | CL8 |

**Por dentro de `0x5fc260(0)`** (*estático*), en el orden de sus llamadas:

| Dónde | Qué hace | MuRet |
| --- | --- | --- |
| `0x5fc26b..0x5fc2c0` | el corrimiento del agua con el reloj del frame (`0x78ecbec`) y la mezcla opaca (`0x620980`) | `WaterOffsetAt` |
| `0x5fc2d4..0x5fc2de` | `0x7d0d31c = 0` y `0x5fc190(0)`: el pase opaco y el de alfa de cada celda | `World::DrawTerrain` |
| `0x5fc32b` | `0x620a00(1)`: `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`, sin culling y con alpha test | la mezcla de cada quad del pasto (`ModelBlend::Alpha`) |
| `0x5fc332..0x5fc345` | sigue con `0x6c9d58` (`0x5f87b0` lo pone en 1 salvo en los mapas 18 a 23 y 30, `0x5f8928..0x5f8947`) y fuera del mapa 7 | `HasTheGrassPass`; el 7, el 30 y los 18 a 23 ya cortaron antes |
| `0x5fc349..0x5fc353` | `0x7d0d31c = 2` y `0x5fc190(0)`: **el pase del pasto**, en los mismos bloques y celdas ([El pasto](#lo-propio-de-lorencia)). No saca `rand()` fuera de los mapas 18 a 23 (Q-C229) | `World::DrawGrass` (CL7, hito 1) |
| `0x5fc35b..0x5fc360` | sin depth test (`0x620870`) y con culling (`0x6208d0`), que la mezcla de `0x549130` vuelve a apagar | el estado del lote (`GameScene`) |
| `0x5fc365` | **`0x549130`**: las marcas del suelo ([Las marcas del suelo](#las-marcas-del-suelo)) | `World::DrawGroundMarks` (CL7, hito 1): con la tabla vacía no dibuja nada; con una marca prendida corta en `0x549152` (P-C49) |
| `0x5fc36a` | el depth test de vuelta (`0x620850`) | sí |

**Hasta dónde llega MuRet** (T8 del hito 1 de la CL7, el pase del pasto, las marcas del suelo y, en
el hito 3, los objetos del 0 y del 2 y lo que les sigue hasta los ítems del piso). `GameScene::Render`
corre lo de arriba en ese orden y corta con error en la primera etapa que no tiene: en el 0 y el 2,
adentro de los objetos (los casos de `0x602ed0` que no tiene o una pose global anterior que necesita
la fuente, [Los objetos del juego](#los-objetos-del-juego)), en una rama de suelo o de personaje
todavía no portada o, si pasa todo eso, en los ítems del piso (`0x6374fe`); en
los demás mapas que carga, en los objetos (`0x6374ce`). Antes dibuja el pase del pasto y las marcas del suelo, que no gastan `rand()`; los
objetos, uno por objeto visto que llega a `0x602ed0` y el guard de la fuente, más sus ocho sorteos
cuando crea dos gotas. Los efectos vivos y vistos consumen el común de `0x52dc46` y los casos propios;
los personajes, sus accesos al registro y el de `0x50db57`. Las aves y los peces ya tienen pool y
servicios productivos ([La fauna y sus recursos](#la-fauna-y-sus-recursos)).
Con la tabla de la .bss las marcas no dibujan nada, y una marca prendida, que MuRet no sabe crear,
corta en `0x549152` (P-C49). Los mapas 18 a 23, que
`LoadMap` ya corta en `0x447d70`, cortan antes de la primera celda (`0x5fa83f`, Q-C229) si llegan con el
mundo puesto a mano.
El corte es terminal (`IScene::Failure`): el frame no se presenta y el juego cierra con el error. En
Lorencia, con el héroe y la cámara de la traza, son las 329 celdas de la traza en su orden
(`GoldenTerrainTrace.TheGameSceneDrawsTheCellsOfLorencia`) y 25 quads de `TileGrass01` de z 165 a 293,
como en la traza (`GameSceneDrawTest.TheReferenceMapsDrawTheTerrainTheGrassTheGroundMarksAndTheObjects`;
la cuenta y las alturas, *verificado* contra lo que dice [El pasto](#lo-propio-de-lorencia) de la traza;
cada vértice, sin comparar, P-C48 a). Lo que falta, [08](08-pendientes.md) P-C41, P-C48, P-C49, P-C53 y
P-C55.

**Las marcas del suelo** (`0x549130`): la tabla de `0x760cab0`, quién la llena, cómo envejece y cómo se
dibuja, en [Las marcas del suelo](#las-marcas-del-suelo).

**Después de los objetos** (`0x6374d3..0x6374fe`; *estático*, leído en el hito 3 de la CL7; las cuentas
de MuRet, *simulado*):

- **Los efectos del suelo** (`0x52dc20`). Recorre los 200 lugares de los efectos con modelo
  (`0x7491fd8`) y solo mira los vivos con `+0x16c`: con cada uno, un `rand()` (`0x52dc46`), `0x620a90` y
  el caso de su tipo (`0x61d`, `0x5de`, `0x5d5`, `0x61e`, `0x61f`, `0x636` y `0x6eb`), con los calcos de
  `0x5fb2f0`. `+0x16c` lo escriben `0x52d620` (`0x52d64d`: la esfera de 400 contra el frustum, con
  `0x5fbf00`), el dibujo de los efectos con modelo, que en el juego va después (`0x63751d`), y `0x52b634`,
  que lo pone en 0 en un caso del paso; ni el alta (`0x518cf0`) ni la carga del mapa (`0x6070a0`, que solo
  apaga el byte vivo) lo tocan (Q-C278). Sin efectos vivos y vistos no saca `rand()`. MuRet lleva ese
  estado en `ModelEffect::seen`, publica la esfera en `DrawModelEffects` y usa el mismo pool durable
  entre login, selección y juego. El pase del suelo reproduce los siete tipos con los límites que
  se detallan abajo. Los dos `0x61f/0` de llegada ya tienen sus anillos y calco; en un proceso nuevo
  heredan seen=false y este pase los omite. Esto desbloquea ese corte de T9, pero el frame completo
  sigue detenido a más tardar en los ítems del piso (P-C55 f).
- **Los pájaros** (`0x446b60`). Con cada uno de los 40 lugares de `0x7d2dca8` que no es un `0x10f`, vivo
  o no, suma 90 a `+0x24` y se lo resta al final (Q-C277); los vivos van con su sombra (`0x602e90`,
  `0x60b130`, `0x5491e0` y `0x4eff40`). Sin pájaros vivos no dibuja ni saca `rand()`. Los crea `0x446120`,
  desde el paso (`0x6370ea`), y los apagan `0x6070a0` y `0x4455c0` (desde `0x4d4ab2` y `0x4e363e`). En el
  frame de cero pasos de una entrada recién limpiada no hay vivos. MuRet conserva los 40 registros y
  sus campos anteriores, apaga sólo active al limpiar y los llena desde `StepGameBirds`; no trata el
  kernel puro como integración final ([La fauna y sus recursos](#la-fauna-y-sus-recursos)).
- **Los personajes** (`0x510d00`). Los 400 lugares en orden. Fuera del héroe, la rama del asedio
  (`0x510d25`, con `0x4294b0`) pide `0x7239ec`, que solo escribe `0x4294c0` desde paquetes que MuRet no
  aplica. El héroe con `+0x1d8` & 4, en la escena 5, no se dibuja: `0x510e57` le pone 1000 en `+0x13c` y 1
  en `+0x148`, `+0x158` y `+0x168` (sin leer quién los lee, P-C55 e) y sigue. Los vivos con `+0x16c` van a
  `0x50ba30(c, c, bandera)` (`0x510ea1`) y, los jugadores, a `0x42afe0`, que solo corre en el mapa 30. Al
  final, `0x42da60`: `0x7239dc` y `0x7239e0` en 0, las puertas de `0x6c72bc` con `0x7239d9` en 0
  (`0x42da40` → `0x42a110(0, 0, 0, 1)`, Q-C276) y `0x7239d9` en 0. Las puertas no miran el mapa: en
  Lorencia y en Devias abren las paredes de (118..121, 115) y, en Devias, las de (92..95, 205), desde el
  primer frame que llega acá. MuRet (`Scenes::DrawGameCharacters`) conserva esas operaciones y porta
  el jugador ordinario: sombra, scan, luz, rama del héroe, partes, tela y lo que cuelga (abajo, "Lo
  que cuelga en el juego"). Las puertas se abren solo si termina el recorrido. Las ramas pendientes
  devuelven su dirección después del prefijo ya ejecutado. El overload histórico sin recursos
  conserva su corte en `0x510ea1`.
- `0x6374e2..0x6374f5`: el picking del modo editor (`0x77f3b18`) y la vista de arriba (`0x7d36899`) no
  corren. Sigue `0x608a00`, los ítems del piso (`0x6374fe`), donde MuRet corta.

Con el héroe de la traza en Lorencia (143, 120), MuRet saca en el frame 4 + 8 + 84 `rand()` (el contador,
la cámara, los 83 objetos vistos y el `rand() % 2` del `0x69`, impar con la semilla del test) y corta en
`0x6374fe`; en Devias (220, 30), con 4 + 8 + 54, también. Tests: `WorldObjectsDrawingTest`
(`TheLorenciaWaterspoutDrawsFourLooseMeshes` y `TheLorenciaWaterspoutRunsItsFrameCase`),
`GroundEffectDrawingTest`, `GameParticlesTest`, `GameCharacterRenderTest`, `GameCharactersTest`, `TerrainWallsTest` y
`GoldenTerrainWalls.TheGatesOpenInLorenciaAndDevias`, y
`GameSceneDrawTest.TheReferenceMapsDrawTheTerrainTheGrassTheGroundMarksAndTheObjects`.

**El orden del pase 3D no es el del login.** En el juego van el terreno, los objetos, el suelo, los
pájaros, los personajes, los peces, el caballo, las hojas, los efectos con modelo, los sprites y las
partículas; en el login (`0x6350b0`, [El mundo del login](#el-mundo-del-login)) los objetos van
**después** de los personajes y del caballo. Con aditivos que no escriben la profundidad (el
`light_02` y el `ston02` de la traza van con `GL_ONE, GL_ONE`), el orden se ve. En la traza, en ese
orden (*verificado*): 470 fans del terreno; 25 quads del pasto; 120 bloques con texturas de
`0x41c..0x47e` (hasta la línea 59.594); 11 de los pájaros (`0x69`, de la línea 59.888 a la 65.847,
cinco con `glColor4f(0, 0, 0, 0,2)`: las sombras); 54 de los personajes desde la 66.171 (el
guardia, la NPC de la tienda, otro personaje, el héroe con el set `_bd` y un Satan); y 105 quads: 80
hojas (`0x64`), 2 sprites `0x5ac` y 23 partículas `0x5d9` y `0x5f2`.

**El fondo del mapa 0 no se ve** (Q-C143). En cada frame de la traza, `glClearColor(0, 0, 0, 1)` y
el único `glClear` del frame (`0x6384f5`, `0x638516`, en `0x637700`), y después el `glClearColor`
de `0x6371a0`, que nadie usa hasta que el frame que viene lo pisa (*verificado*). El color del
frame lo pone `0x637700` (`0x63835d..0x6384f5`): Icarus (10), (0,0117; 0,0977; 0,1719); Kalima (24
a 29 y 36), (0,1172; 0,1563; 0,1563); y negro en los demás (*estático*). No hay cielo: Icarus no
dibuja el terreno y pone las nubes (`0xbb`, `0x622`, `0x623`), y Kalima dibuja su agua en un pase
aparte (`0x4378a0` → `0x41cca0`, `0x41cf00`).

### Los globales del dibujo de ítems

WinMain genera una tabla de cien ángulos con cien llamadas `rand()%360`
(`0x4bada2..0x4bade0`), después de sembrar la CRT y antes de construir los actores.
La tabla de monedas de `0x608a00` la presta; dibujar no la regenera ni consume otros sorteos.
`GameItemRenderState` conserva esa tabla al cambiar de mapa y hace idempotente su inicialización.

El writer `0x639482..0x6394ab` pertenece al orquestador `0x63945d`, antes del despacho
de escena: publica `float(sin(T*0.004f)*0.15f+0.6f)` en `0x7d36ba8`. Los FST intermedios
no sacan ST0 ni obligan a recargar el float. No es un writer tardío del HUD. Este pulso es
distinto del par de datos `0x6c9de4/0x6c9de8`, iniciales0.5/0.01, que muta `0x609640`.
Las selecciones antiguas de brillo se conservan por BMD, no por instancia del ítem.
Las pruebas separan los tres owners y comprueban la CRT después de los cien sorteos;
los vectores de seno del host no acreditan identidad universal con la CRT original.

### Ampliación CL7b del piso y de sus efectos

Lectura **estática** del mismo PE `c32bbd5f…a26a`, sin nueva ejecución del original. El dispatcher
`0x608a00` ya no queda limitado a Sword01..19 de nivel cero: `GameFloorItemDrawing` conserva la
pose antes del filtro de alpha, los remapeos de las cinco familias de armadura al modelo `0x20a`,
las alturas de raíz, los casos `0x1e1e`, monedas, niveles, excellent/ancient y los offsets de agua
de mapa 10 y Kalima. Las monedas prestan la tabla de cien ángulos; no sortean otra. El offset
acuático se aplica después de la pose y antes del skin; un error conserva el prefijo y la
posición temporal. Al completar se restaura la posición y se proyecta su Z original más 30.

`GameItemSpecialDrawing` cubre los 51 IDs comparados por `0x609640`, además de su default;
`GameItemGlowSpecialDrawing` conserva los retornos tempranos de `0x60b3d0`, y
`GameItemGlowEffects` los stores y emisiones de `0x60be05..0x60c5c3`. El casco `0x121b` exige
su préstamo privado `object+0x120`: no sustituye una matriz ausente por un hueso global. La
validación ocurre al usar cada recurso, para preservar geometría y stores previos a un error.
Color RGB, selección por BMD y alpha durable se publican sólo donde el ASM escribe; el alpha
temporal de un pase no se copia de vuelta al efecto. El reloj de los módulos usa el low32
firmado del FTOL64; las fases de seno/coseno conservan el float completo y su fracción.

Los efectos `0xf4/0xfa` reproducen el inicializador `0x607b60`. `0xfa` toma luz antes de publicar
pose; `0xf4`, después. Las cintas permiten count negativo sin desplazar historia y luego
reemplazan ambas cabezas. Helper07 publica joints `0x614/14` y `0x613/4` sobre el owner durable
del ítem; el siguiente tick de `0x614/14` y el creador/paso/dibujo de Thunder `0x5ca/0` ya están
portados con su recurso físico. Las pruebas angulares usan un generador independiente con el
literal binary32 `0x42652ee0` y atan retenido a 64 bits antes del producto a 53.

Pruebas: [piso](../../tests/scenes/test_game_floor_item_drawing.cpp),
[casos especiales](../../tests/scenes/test_game_item_special_drawing.cpp),
[efectos](../../tests/scenes/test_game_special_model_effects.cpp),
[cintas](../../tests/scenes/test_game_ribbon_creation.cpp),
[siguiente tick](../../tests/scenes/test_game_joint_step.cpp) y
[generador](scripts/make_helper_joint_vectors.py).
Tres pruebas de GL real observan ZERO/SRC_COLOR, profundidad y culling del modo multiplicativo.
Esto no completa los productores de equipo/armas/alas ni acredita todo P-C55; el gate integrado,
Windows de la fuente nueva y la observación nativa siguen separados.

### Ampliación CL7b de Kalima: recursos y objetos vivos

`KalimaWorld` porta la carga y los dos planos de 65.536 celdas de `0x4377a0`, la actualización
`0x41cca0` y la geometría de `0x41cf00`. Su existencia sigue siendo distinta del byte que
clasifica el mapa. `CloneForFrame` conserva los owners para un cuadro intermedio sin avanzar
la CRT durable. El prefijo BMD firmado−1 proviene del initializer real de carga: no se inventa
un modelo sustituto cuando faltan los bytes acreditados.

La escena conserva los 2.266 objetos reales de World25 para los siete aliases 24..29/36. El
dispatcher agrega cuerpo, cáusticas, sombra `0x22`, sprite del tipo12 y hide 37..40. Las 32
texturas `Object8/wt00.jpg` a `wt31.jpg` ocupan slots 65..96 **decimales**, LINEAR/REPEAT.
El contador `0x7d2d360` pertenece al paso común de tela (`0x637d63/0x637d7c`), parte de cero y
avanza módulo32; no se deriva del reloj ni se incrementa por objeto.

El prefijo global `0x4385a0` corre antes del early-return por ausencia de objetos vistos y
antes de resetear el contador de selección. Preserva sonidos, apertura/cierre lógico del
registro propio, tabla de clase/nivel `0x4379a0`, lecturas XY separadas y orden de los sorteos.
Un error anterior al reset conserva el contador. Los sonidos son solicitudes lógicas, sin
atribuir reproducción de CL10.

El ciclo vivo completa esos tres cuerpos públicos: movimiento `0x438ad0`, partículas de los
marcadores `0x438f81` y productor ambiental `0x4389ca`. `KalimaObjectMovement` conserva velocidad,
incrementos angulares, la ventana unsigned de acciones0d..21 y los stores binary32. Los productores
35..40 preservan sus owners, guards, sonidos y stores posteriores aunque el pool esté lleno.

Las familias `5ac/6`, `5ac/7`, `6ed/0` y `6f0..6f4/0` tienen creación, paso y dibujo. La pisada
`0x5087d7` solicita además **6f4/1**: conserva los dos sorteos del caller y el del creador. Las seis
texturas globales físicas fantaB/WATERFALL1..5 usan LINEAR/CLAMP. `ParticleOwner` distingue Actor de
WorldObject con identidad64, sin truncar punteros del host. El backend de `0x620850` habilita
**depth test** real, no culling.

El ambiental crea también el modelo9/0: vida30, velocidad10, dos movimientos en el paso, guard
word+8a y perturbación del plano correcto. Su dibujo emite cuerpo y cáusticas con BMD Object10
real:204vértices; sin binding65 conserva102del cuerpo y falla en `0x4395ba`. StopAtTheEnd recibe
el TerrainWall vivo al consultar `0x4f23c1`. No se inventa una altura ni una tabla caminable.

La revisión Astra corrigió tresP2 con RED→GREEN: el drift5ac/6 usa el patrón `0x39d1b718` de
`[0x6ba3e8]`; `StepGameParticles` presta el reloj del caller por referencia; y MissingScene de6f1
conserva Initialize, vida, rotación, escala y tresCRT antes de fallar en `0x543fcd`, todavía sin
escribir vz ni jitter. No se reemplaza la escena ausente por cero.

Los siete aliases completan entrada poblada64,64, hot1C antes de Step, varios cuadros, modelo9
durante30ticks y reentrada24→0→24. El recorrido usa el ATT real62,67→64,67: picking, D3 literal
`C1 07 D3 3E 43 32 33`, eco fakeGS, MoveHero, emisión6f4/1 y nuevo cuadro con agua/partículas.
PrepareFrame conserva su acceso común y sus cuatroCRT; la prueba toma el snapshot después y
exige cero mutaciones durables adicionales en Render(frame0), sin excepción por mapa.

El gate privado inicial fue328/328, incluidos11GL requeridos; las tres correcciones pasan81/81
focales. Astra aprueba objetos/ciclo y el delta de partículas. El gate integrado y Windows se
registran en el traspaso. **No se cierra Kalima en toda configuración ni T16 universal**:
quedan otras familias de mapas, equipo/acciones y contraste numérico/visual nativo. No hubo
ejecución ni emulación del original, ni trabajo de VM o protecciones.

Pruebas: [recursos](../../tests/scenes/test_kalima_object_resources.cpp),
[servicios](../../tests/scenes/test_kalima_object_services.cpp),
[paso global](../../tests/scenes/test_kalima_object_step.cpp),
[objetos vivos](../../tests/scenes/test_kalima_live_objects.cpp) y
[matriz T16](../../tests/scenes/test_game_t16_matrix.cpp),
[caminata](../../tests/scenes/test_game_t16_walk.cpp) y
[partículas](../../tests/world/test_kalima_particles.cpp). Son evidencia estática y simulada;
los recursos y la geometría son reales, mientras transporte/GPU de escena son falsos y las
pruebas GL específicas ejecutan el backend real del port.

### La cola y sus cambios de pase

Lectura **estática** de `0x63750d..0x6375e5`, `0x620db0` y `0x6213e0` del build
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
El byte por mapa vale 1 en 24..29/36 y 0 en los demás; no reemplaza a `0x437850`, que consulta
si `0x723c48` es distinto de null. La primera consulta omite daño cuando hay mundo Kalima; la
segunda, posterior al primer `0x6213f0`, decide repetir la cámara, dibujar agua y recorrer la
segunda capa. No hay reflexión de cámara en este caller. `0x4378a0` llama actualización
`0x41cca0` y dibujo `0x41cf00` antes de joints 2, efectos modelados 1, cintas, MarkSeen, ojo y
sprites/partículas/daño 2.

**`0x620db0` y `0x6213e0` tienen efectos distintos.** `0x6213e0` sólo hace push y loadIdentity
de modelview. `0x620db0` reconstruye la cámara y en `0x620eea..0x620f4c` apaga alpha test,
activa textura/depth/culling y escritura de profundidad, usa LEQUAL y fija alpha GREATER 0.25.
Esos resets no cambian por analogía blend ni binding. Un vaciado del lote diferido tampoco
representa otra llamada a `0x620db0`.

MuRet distingue ese inicio en `ModelBatch::InitializesMaterial`. `GlFrameTarget::FlushModels`
ejecuta comandos aunque no haya vértices, conserva el material del lote y deja la continuación
sin otro reset nativo. `BeginGameTailPass` captura antes del flush; el ojo conserva material y
el regreso al mundo publica sólo los resets acreditados. `GlModelRenderer` sube matrices/VBO
por lote y aplica la inicialización sólo cuando está solicitada. Las regresiones observan GL
real entre lotes parciales y también un inicio World sin geometría:
[test_gl_frame_target](../../tests/render_gl/test_gl_frame_target.cpp) y
[test_game_scene_kalima_tail](../../tests/scenes/test_game_scene_kalima_tail.cpp), **simulado**.

La carga meteorológica `0x61ac3e..0x61ad7a` publica 64/65 del mundo y 66/67/68 de lluvia,
respetando la alternativa TGA→JPG del 64, opcionalidad y sampling. El atlas de daño no era otro
selector: `0x621799/0x621889` pasan 1 al binder; `0x61bdb4` carga `Interface\FontTest.tga` con
NEAREST/CLAMP. Los joints emitidos por fauna usan 610 Flare02 (LINEAR/REPEAT), 613 JointSpirit01,
618 JointLaser01 y 697 ScolTail (LINEAR/CLAMP); 617 Spark01 ya estaba cargado.
[Texturas meteorológicas](../../tests/scenes/test_weather_textures.cpp),
[handles GL](../../tests/render_gl/test_weather_texture_handles.cpp) y
[atlas/joints](../../tests/scenes/test_game_tail_textures.cpp) contrastan archivos reales y píxeles.

El orquestador no crea el estado `0x723c48` ni el backend `0x4378a0`: LoadMap conserva la frontera
`0x4377a0`. Tampoco porta productores de cintas ni todas las familias de piso/joints/efectos
CL9–CL11. No se ejecutó ni emuló el original, y el contraste de VM sigue pendiente.

### Los casos del suelo y los dos anillos de llegada

Lectura **estática** de `0x52dc20`, `0x53d2c0` y `0x5fc9c0`, build `c32bbd5f…a26a`, sin nuevas
emulaciones. `Scenes::DrawGroundEffects` filtra vivo y seen antes de consumir `0x52dc46` y emitir
la mezcla aditiva. `World::DrawGroundEffectCase` continúa desde ese punto; conserva las operaciones
de estado aunque no produzca vértices o corte después. Los tipos sin caso no dibujan.

| Tipo | Contrato estático y límite de MuRet |
| --- | --- |
| `0x61d` | Solo /1: calco con scale, luz propia, giro −z y lift5; después luz por `0x3f68ba2e` y scale+0,05f. |
| `0x5de` | Salvo /3: otro rand; gris `R32((r%4+8)*0,1f)`, lado2, giro −z. No se sustituye por un literal0,9f. |
| `0x5d5` | scale+0,2f antes del calco; gris `R32(life*0,1f)`. |
| `0x61e` | Brillo1 o `1−(5−life)*0,2f` con life<5; lado `(20−life)*0,15f`, salvo /4,/9,/10 que usan scale. Colores por la tabla original. /7 corta en `0x52ded0` por dueño ausente; los nacimientos de /6 y /8, en `0x52e04c` y `0x52e0a2`; subtipo fuera de tabla, en `0x52dfdc` por scratch retenido sin representar. |
| `0x61f` | Repite aditiva. Ángulo = low32 firmado de ftol(reloj), resto3600, por0,1f. /0: anillos ±ángulo y calco `0x61e` naranja. /2 no tiene anillos; con life≤10 corta en `0x52e312` por lado local no escrito; con life>10, lado `(20−life)*0,55f`. |
| `0x636` | Mezcla alfa; ancho/alto de `+0x17c/+0x180`, luz propia, sin giro. |
| `0x6eb` | Calco con scale y luz propia; en24..29/36, salvo /6, conserva depthWrite=false y corta en `0x52e12f` antes de `0x437940`. |

Cada anillo de llegada tiene doce quads: radio90 y blanco en z0; radio130 y negro en z200; alpha1.
El giro usa la matriz **transpuesta** de `0x5fc9c0`, con sus stores f32. No se atenúa con life; el
calco sí. Con life20, su lado0 aún produce las UV no finitas del original; con life0 el brillo
resulta levemente negativo por el literal float0,2. No se recortan esas rarezas. Se cargan las OZJ
reales `Magic_Ground2` y `Magic_Circle1`, slots `0x61e/0x61f`, LINEAR/CLAMP (`0x61d6e0/0x61d6fc`).
Las texturas de los demás tipos todavía no se suman a la carga de efectos del juego.

`GroundEffectFrame` conserva el estado **anterior** al frame original (efectos, CRT, reloj y mapa).
Entre frames vuelve a dibujar desde copias: conserva la imagen anterior sin repetir mutaciones ni
azar en el mundo. Un replay sin snapshot o con otro mapa devuelve error. Esto prueba la política
de ritmo de MuRet; no sustituye una captura nativa. `GroundEffectDrawingTest`,
`GroundEffectFrameTest` y `WorldObjectsDrawingTest` cubren casos, orden, RNG y prefijos de error.

### El paso de los efectos con modelo

**Ampliación estática del hito 2 (2026-11-10).** `World::StepGameModelEffect` envuelve los casos
nuevos de `0x5208a0` en la pasada `0x63711e`, después del daño y antes de joints/partículas. Usa
los pools durables, el reloj y CRT de la escena; no guarda referencias a recursos temporales.
El caller carga los modelos antes de fauna/personajes/efectos. Esto amplía los kernels previos,
sin afirmar todos los casos del switch ni precisión nativa CW/FSIN.

- Cada efecto vivo toma un CRT común en `0x5208c5`, incluso si no usa esa luz local. Tipo no
  soportado conserva ese prefijo y devuelve error. Sólo anima el rango **`0xad<=tipo<0x151`**
  (`0x52cbf4..0x52cc04`): Noria 322..324 sí; 5c6/61f no, aunque se preste Skeleton. Después
  vienen movimiento común y vida−1. Dar un esqueleto no habilita animación fuera del rango.
- Noria (`0x525e9d..0x525f15`): RGB=`sin(T*f)*0,2f+0,01f`, con factores f32
  R 0,0011/G 0,0017/B 0,0013. B guarda el argumento en float antes del seno; R/G retienen PC53.
  Ángulo Y=`F32(turnDirection+ánguloY+4)`. Reutiliza `SineProductPC53`; no ejecuta FSIN nativo.
- 5c6 (`0x52ab04..0x52ab7b`), nacido en gate 0 con vida 10: emite 18 partículas 5c6/subtipo 1,
  Z acumulada+24, escala 12 primera/6 restantes, luz `F32(life*0,1f)`. Cada alta con lugar toma
  tres CRT para ángulos `%360` y `VectorRotate(0,−50,0)` (`0x54059f..0x54061c`), vida 2.
  Pool lleno no toma los sorteos del alta: con 0/2/18 huecos, la pasada usa 1/7/55 CRT incluido
  el efecto. Se retienen `+0x38/+0x60` de la partícula anterior. El paso de Spark1 resta 2 a
  escala y apaga bajo 0,2 o unordered (`0x5450a9`), sin fade RGB. Las partículas recién nacidas
  avanzan después, en este mismo tick; no se difieren al siguiente.

`GameModelEffectsTest`, `MapModelEffectServicesTest`, `MapLoadTest` y los tests de partículas
comprueban creación, retención, orden/pool lleno y presupuestos CRT. No son una captura nativa
ni una validación visual de la torre o del teleport.

### Las partículas de la fuente en el juego

**Estático**, `0x603011..0x60313f`: después del rand común del objeto, luz1 y V calculada con NEG
envolvente del low32 de ftol(reloj), resto firmado1000, por `0x3a83126f`; U queda igual. El guard
`rand()%2` impar termina. Par: X/Z = `rand()%32−16`, Y=−20, hueso1, alta `0x5f2/0`; después nuevos
X/Z, Y=−80, hueso4 y otra alta. Cada alta con lugar libre toma dos rand más. Son9 sorteos con dos
altas,7 con una y5 con pool lleno, además del común del objeto. Los sorteos de la segunda gota van
después del alta de la primera. Cada punto usa `0x4ec110` con Translate=0: la matriz ya está en
coordenadas del mundo y no recibe otra escala/origen.

`StepGameWaterspoutFrame` conserva ese orden y cualquier primera gota si falla la segunda.
`GameScene` presta `WorldState::Particles`, el mismo pool y viento del login y la selección; las
cargas apagan alive sin resetear el viento. `GameObjectPass.retainedPose` presta el
[carry global](#la-pose-compartida-entre-escenas) a este lector. `0x6030a5/0x603121` pasan Translate=0:
`0x4ec16d..0x4ec17e` aplica directamente la matriz retenida de los huesos 1 y 4, sin consultar
escala/origen del modelo actual. Con alpha<0,01 o hueso dummy lee lo dejado por el modelo anterior,
sin volver a posar. Si el préstamo no contiene ese hueso corta después de los X/Z de esa gota;
un préstamo vacío no se reemplaza por identidad ni por una pose reconstruida. Sin préstamo se
conserva la API compatible para huesos escritos y su error explícito en los demás. El contraste de
los puntos y de la pose portable con la ejecución nativa sigue pendiente.

`StepGameParticles` reutiliza `StepParticlesChecked` desde `0x637128`, después de personajes y antes
de las marcas del suelo; consume los dos rand del viento incluso con la tabla vacía.
`DrawGameParticles` reutiliza `DrawParticles` para `0x637558` en espacio de ojo. Su módulo está
probado, pero la escena todavía no lo alcanza por `0x6374fe`. `GameParticlesTest` y los tests de
entrada cubren los presupuestos, nacimientos, pool lleno y prefijos; no son observaciones nativas.

### El buffer compartido de luz dinámica

**Estático**, `0x5fbf60` y sus datos: `0x7b4ccc4` nace en cero en la cola virtual de `.data`.
El rango no nace vacío: firstX/Y están en `.bss`, pero lastX/Y (`0x6c9d68/0x6c9d6c`) contienen255.
La primera copia cubre la grilla; las siguientes copian la ventana publicada por el dibujo
anterior, ampliada3 y envuelta en256. Fuera de esa ventana se conserva el buffer anterior. Dibujar
solo publica el rango; no hace una copia implícita de la base.

`DynamicWorldLight` pertenece a `WorldState`. Login, selección y juego copian la base sombreada al
principio del paso y publican el rango después de calcular la cámara del dibujo. Terreno, objetos
y personajes leen ese buffer; `LoadedWorld::Lighting` sigue exponiendo la base para la copia.
La carga y el cambio de escena no lo borran. Faltan los productores de luz de los fuegos y otros
casos del mundo: este dueño y sus lectores no cierran H3.4 ni la comparación nativa de luces.
`DynamicWorldLightTest` y los golden de login y terreno fijan el primer estado, la ventana y que
dibujar con otra luz no reemplace la base.

### El jugador visto en el dibujo del juego

**Estático**, `0x50ba30`: `DrawGameCharacters` conserva el filtro del héroe, el orden de400 lugares
y las puertas finales. Para un jugador `0x20a` con esqueleto/acciones: verifica las ramas anteriores,
dibuja la sombra (`0x50bcec`), publica `0x505820` (`0x50c91e`), toma la luz dinámica (`0x50cc55`) y
el color del héroe. Ese color abre/cierra una vez el registro propio:3.848 rand cuando queda cerrado,
aun con sus bits de ajuste apagados. Cada máscara de color multiplica con stores f32; el bit0 usa
el literal0,9f del binario. Reutiliza `DrawCharacter` por fases Parts y Cloth, conserva el rand de
`0x50db57` aun sin ítems y aplica después el color para los colgantes.

Los índices de resaltado `0x6c8e30/0x6c8e2c` nacen en−1. Los monstruos, la ropa especial y los
efectos pendientes cortan donde corresponde; lo que cuelga sigue abajo. Una tela ausente conserva sombra/scan/luz/partes
anteriores, sin gastar todavía `0x50db57`. Un dibujo entre frames usa `DrawCurrent` sin crear ni
simular telas ni consumir RNG; al recuperar varios frames se conserva la simulación y se descartan
los destellos de los frames intermedios. El mapa30 conserva su frontera `0x510eb3`.
`GameCharacterRenderTest` y `CharacterDrawingTest` cubren estas ramas y los prefijos hallados en la
revisión adversarial. Quedan abiertas la pose nativa, las ramas pendientes y las trazas GL originales.

### Lo que cuelga en el juego

**Estático**, `0x50db57..0x50e9fa` y `0x509340` (2026-11-11, el corte `0x50de55` que vio Emanuel al
entrar a Devias armado). `Scenes::DrawGameHanging` corre, sobre el cuerpo ya dibujado:

- **La luz.** `rand() % 30 + 70` por 0,01 (`0x50db57`, `[ebp+0xc]`), aun sin ítems, y `+0x3b4` con la luz
  del terreno más la del personaje, o (1; 0,1; 0,1) con `+0x375` desde 6 (`0x50db94`). Lo que no es un
  `0x20a` corta en `0x50e060`.
- **La espalda** (`0x50dcda..0x50de60`). `local_20` como en la selección (zona segura, las acciones
  `0x99` a `0xb8`, o `0x15` y `0x1d` en los mapas 7, 24 a 29 y 36; nunca en los 11 a 23). Con 1: el arco
  izquierdo, la ballesta derecha o, si no, la mano de índice 0 (EAX en 0 desde `0x50dcd1`); con 0, las
  saetas izquierdas o las flechas derechas. El guardia de Lorencia (`+0x88` en 4, subtipos `0xd1` a
  `0xd3`) lleva `0xa1b` +8. Cuelga con la parte de las alas: hueso 47, la velocidad de la acción 1 de
  `Player.bmd` con la bandera, y al volver solo repone el frame (Q-C296). La raíz con bandera es
  `0x5fc8f0` en el buffer local más `0x5fca90` con el hueso, con los bits de la x87
  (`Render::Models::OriginalConcatTransforms`: la primera fila suma en el orden natural y las otras
  dos desde la columna 2); el origen es la posición del personaje.
- **Los GM** (`0x50df01`, `0x413fe0`): con "webzen" en el nombre (`0x576960`), `+0x78 & 0x1000000` o el
  CtlCode en `0x20` u 8 (el personaje del laboratorio tiene `0x20`), suelta una capa que no sea la 5
  (`0x414340`) y pone `+0x360` en 5. Sin capa en `+0x364` y con `+0x360` del 5 al 13 la crea: la 5 es
  `0x414370(0x12a, 0x14, 1, 70, −5, 0, 0, 0, 45)`, Skill\MuSign.bmd (`0x6166fe`, texturas en `0x616c5c`);
  las del 6 al 13 (`0x4147c0` con el `0x637`) cortan: en ese rango `+0x360` lo escriben solo los paquetes (el
  `0x12` en `0x4c89e1`, `0x40c140`, y `0x42ab40`, `0x42ad50` y `0x42ae60` con las banderas de `+0x78`), y su
  dibujo (`0x414820`) lee `+0x360` y `+0x368` (CL9.5). Después, `0x4142f0` (`0x50df07`), sin
  `+0x78 & 0x4000`, la dibuja con `0x414470`: el punto (70, −5, 0) del hueso 20 del cuerpo (`0x4ec110`),
  los ángulos fijos (0, 0, 45), `0x4ec200` a 0,5 sobre la acción 0 y `0x602e90(objeto, 1, 0, 0)`, que en
  `0x6002e0` hace `RenderBody(2)` con la luz en 1 y `RenderBody(0x44)` con (1; 0,5; 0), con la luz por
  vértice y la pose en el buffer global. Sin `rand()`. `+0x360` queda en 5 en el registro (Q-C301). En
  Chaos Castle corta (`0x5ffe09` lee `+0x1a1` del héroe), y en Blood Castle (`0x50de63`, antes de la capa)
  el 5 de un GM corta en `0x50de84`, el arma del evento.
- **Las alas y Helper02**, fuera de los mapas 18 a 23: las alas en 47 a (0, 0, 15) y Helper02 en 34 a
  (20, 0, 0) y 0,5 keys por frame, con el excelente del arma de la espalda (Q-C297), y su destello rojo
  `0x5ac` de 1,5 en (20, 0, 15) del hueso 34.
- **Las manos** (`0x50e298..0x50e9ea`), sin espalda: cada mano abre y cierra `+0x38e` antes de mirarla;
  la derecha no cuelga en la acción `0x3e` hasta pasar el frame 4, ni con `')'` en la `0x3d`. Saltea
  `0xa19`, `0xa21` y `0x1c17`; la acción y la velocidad salen de la del personaje (`0x50e76e`). Después
  de `0x509340`, la luz por nivel (desde 7, 5 y 3, `0x50e896`) y los sprites de cada arma
  (`0x50e921..0x50f828`), salvo con `+0x375` desde 6 o `+0x1d6` en 4 (Q-C300).
- **`0x509340`.** Sale con el `0x1c30` o con `+0x78 & 0x4000`; 0x607b60 con sus `rand()` (uno para
  `0x220` y `0xc19`, dos para `0xc17` y `0xe20`); un acceso a `+0x38e` (Q-C299); la acción `1` si la parte
  es `0x1a18` en la zona segura (Q-C298); 0x4ec200 solo en un frame del original, salvo con bandera y
  `0xa12..0xc11` o con `+0x78 & 0x1000`; la pose en el buffer global (`0x68b181c`), 0x60b3d0 con el
  nivel, el excelente, sin set y 2 (`0x102` con `+0x376` en `0x43` o `0x89`), y los sprites por modelo
  (`0x509a92..0x50b801`), con su `rand() % 30 + 70` por 0,005 aun sin caso. Las texturas Shiny03
  (`0x5fe`, `0x61d937`) e Impack03 de Object9 (`0x5ad`, `0x61e799`) se suben con las demás de 0x61ccb0.
- **Los joints y las partículas de las armas** (2026-11-11), sobre los joints (`FrameTail`) y las
  partículas (`FrameParticles`) del frame, que `GameCharacterPass` le pasa a `GameHangingPass`:
  - el `0x227` y el `0x231` en la acción `0x8c` (`0x50a005..0x50a138`), fuera del mapa 10 y con un `rand()`
    par: el hueso 1 corrido (`rand() % 30 − 15` en x e y, +20 en z) suelta cuatro veces el joint `617/0`
    (escala 10, sin dueño) y la partícula `5c5/0` blanca, cada par con los ángulos (`rand() % 60 + 150`; 0;
    z del personaje);
  - el arco `0xa27` (`0x50af03..0x50afa7`), con un `rand()` impar: la chispa `5c6/0xb` de 0,8 en los huesos
    4 y 12 con (0,8; 0,8; 0,2) y los ángulos del personaje. La chispa (`0x540977`, `0x545368`) pisa la
    escala con la suya y muere con la cuenta de `+0x38` (Q-C360);
  - el bastón `0xc1e` (`0x50b411..0x50b763`): Spark03 en (0; p − 170; p) del hueso 0, que queda en `+0x40`
    del personaje (`ActorState::effectOrigin`) y donde nace el joint `618/0x11` (ancho 30, el personaje
    como dueño, vida 12, la luz (0,8; 0,2; 1), los puntos del uso anterior del lugar: Q-C361), que en cada
    paso vuelve a `+0x40` o se apaga si el personaje murió (`0x53820e`). Otra p sin efecto (Q-C362), un
    destello, dos Shiny02 y la chispa `5c6/0xb` con (1; 0,4; 1), y seis Shiny02 en los huesos 2 y 1 que
    giran con el reloj;
  - la espada `0x223` (`0x50eb43..0x50ecf0`): dos joints `610/4` de (0; −20; −40) a (0; −160; −10) y de
    (0; −10; 28) a (0; −145; 18) de la mano, de ancho sin(reloj · 0,004f) · 10 + 20 y con el personaje como
    dueño. Nacen con diez tramos hacia el segundo punto y vida 0; en su único paso el destino sale de
    `+0x17c` del personaje (`ActorState::trailOrigin`, que `0x4fcee0` pone al día antes del frame 3 y fuera
    de la `0x8d`) y se corre tres tercios hasta su posición (Q-C363 a Q-C365);
  - la espada `0x224` (`0x50eda0..0x50ee6e`): después de su Shiny02, tres joints `614/10` sin dueño hasta
    (0; −133; 7) de la mano, de ancho v · 20 + 20, que avanzan como el `614/14` (Q-C366);
  - el bastón `0xc18` (`0x50f454..0x50f676`): después de Spark02 y Shiny03, cuatro chispas `5c5/1` corridas
    con (l; l · 0,6; l · 0,4) (la `5c5/1` sortea la escala como la `/0` y la vida `(rand() & 7) + 8`,
    `0x5403eb`) y, de 0 a −180, un destello en tres de cada cuatro `rand()`.

Entre frames del original, lo que cuelga se dibuja con una copia del personaje y del `rand()`, sin
avanzar; sus joints y partículas van a la copia de la cola, que no avanza. Cortan con su dirección: el
origen del `0xef` (`0x5096ac`; solo lo cuelga el monstruo con `+0x376` en `0x2a`, `0x50c79a`, CL9.1), el
lugar de los monstruos `0x84` a `0x86` (`0x5093f3`), las capas del 6 al 13 (`0x4140d5`), la de los GM en
Chaos Castle (`0x5ffe09`), el arma del evento de Blood Castle (`0x50de84`) y el efecto cada 30 pasos de
`+0x78 & 0x800` (`0x50e0c0`). `GameHangingTest` y `GameCharacterRenderTest` cubren la espalda (hueso 47 y
su lugar), la mano (hueso 33), las saetas, Helper02, los sprites de `0x21d` y `0xc19`, los joints y las
partículas de las armas, la luz por nivel, MuSign en el hueso 20 con sus dos pases, las cuentas de `rand()`
y los cortes; `HangingJointsTest` y `WeaponSparkParticlesTest` cubren la creación y el paso de los joints
`614/10`, `618/0x11` y `610/4` y de la chispa `5c6/0xb`; `WorldEntryTest` ve la capa en el héroe del cable
(CtlCode `0x20`) en Lorencia.

### Los efectos del jugador

**Estático**, `0x50ea20..0x510ab6` y `0x420df0` (2026-11-11). Después de lo que cuelga,
`Scenes::DrawGameCharacterEffects` corre el caso del `0x20a` del switch de `0x50e9fa` (el índice 10 de la
tabla de `0x510b80`) en el orden del ASM, y `Scenes::DrawGameRaven` el cuervo. Sobre la misma copia del
personaje y del `rand()` que lo que cuelga entre frames; lo que nace en las tablas durables, sólo en un
frame del original:

- **El corrimiento.** `[ebp−0x50]` arranca en 0 (`0x50ea23`) y es el corrimiento de cada `0x4ec110` de la
  cola: la piel especial lo deja en (−4, 11, 0) para las manos de después (Q-C350), y las partículas de
  las botas lo pisan.
- **La piel especial del mago** (`0x50ea38`): con `+0x1d4 & 7` en 0, `+0x1d5 & 0xf8` en 8 y sin
  `+0x78 & 0x4000`, Spark03 (`0x5c6`) de 0,6 y Shiny02 (`0x5fd`) de `sin(T · 0,001f) · 0,4f` en el
  hueso 19, con luz 1 y sin `rand()`. La piel sólo la escribe la lista de la selección (`0x517270`); el
  F3 03 la deja en 0.
- **Las acciones** `0x94`, `0x51` y `0x5b` (`0x50f828`), que sólo escriben los paquetes (`0x4cc530`,
  `0x4ce000`): fuera de la clase 4, la `0x94` sin `+0x78 & 0x4000` pone tres lightning2 (`0x5d5`) en el
  hueso de la mano derecha (`+0x2bd`), con s = frame · 0,1f: s · 0,3f con (0,1; 0,1; 1), s con la luz
  propia y −ftol(T) · 0,1f de giro, y s · 2,5 con ftol(T) · 0,1f. La clase 4, la `0x51` y la `0x5b` tienen
  ventanas: en [1,2f; 1,6f) el efecto `621/2` y el sonido `0x1cc` (corta en `0x50f9b3`); por debajo de 2,
  con el grupo (`0x78aba48`, CL9, 0 en MuRet), `0x59e680` y los joints `0x618`; en [7, 8), dos `117/2` en
  la mano derecha (corta en `0x50fb64`).
- **Las banderas** `+0x78 & 4` o `0x2000000`, sin `0x4000` (`0x50fba4`): un `rand() % 30` por mano, los
  sprites con ese color y los cinco joints `10f/4` si no hay uno. `+0x78` sólo lo escriben los paquetes:
  corta en `0x50fbd5` con esa condición.
- **Chaos Castle** (`0x50fd61`): en los mapas 18 a 23 salta al final, sin el contador ni el conjunto.
- **El contador** (`0x50fd74`): con `+0x358` (el conjunto antiguo de `0x40d010`), `+0x35c` cuenta cada
  dibujo; con la acción por debajo de `0xd` y 100 o más, el efecto `5ac/3` en el personaje con
  (0,2; 0,7; 0,9) y el contador en 0 (Q-C353). El efecto no está portado: corta en `0x50fdc3`.
- **El conjunto completo** (`0x50fddb`, lo que devolvió `0x505820`): el color de `0x608e00` con las botas
  (`+0x298`) y el alpha por 0,5, con un `fstp` por componente; sin `0x4000`, flare01 (`0x5ac`) de 1,3 en
  los huesos b, b − 6 y b − 7 de cada mano. Con el nivel de `[0x743a178]` por encima de 9: las botas de
  `0x182f` a `0x1833` cambian la luz propia por la suya y sacan, por nivel, `rand() & 3`, `rand() % 3`,
  `rand() & 1` o nada (13) para tres partículas `6f1/3` en (0, ∓18 o ∓20, 50) y (0, 0, 70) del hueso 0
  (cortan en `0x510158`, `0x51008d` y `0x51020c`). Después, con `rand() % 20` en 0 y la luz en 1: el 10, la
  partícula `60f/0` de 0,19f; el 11, con `rand() & 7` la partícula y si no el joint `60f/0`; el 12, con los
  pasos múltiplos de 6 dos joints `60f/0x12` y si no, de 3, la partícula; el 13, los joints con 6 y con 4
  la partícula y el joint `610/7`. El 14 y el 15, nada (Q-C351). Cada creación corta con su dirección
  (`0x510339`, `0x51037c`, `0x5103be`, `0x510435` y `0x51048b`), después de sus `rand()`.
- **El efecto de `+0x369`** (`0x510a7a`, `0x510ef0`): en la escena 5, con `+0x88` en 1 y fuera de los
  mapas 11 a 23 y 30, con `+0x369` en 1 el efecto `0x140` cada 5000 ms de `+0x36c` (Q-C352). `+0x369` es de
  los paquetes (`0x4d9260`): corta en `0x510f4d`.
- **El cuervo** (`0x510abd`, `0x420df0` → `0x4182f0`): sin pasar el trapecio, MissTheView. Si lo pasa,
  `0x5ffdf0` (el alpha de Chaos Castle con `+0x1a1` del héroe y, por encima de 0,01, la pose con la luz
  del buffer dinámico) y `0x6002e0`: las mallas 3 y 2 y la sombra sobre la altura de `0x5f93c0`, que no
  va en los mapas 10, 24 a 29 y 36 y va con alpha 0,2 en el 7 (`0x6009a9..0x6009e5`). Con `+0x78 & 0x4000`
  del dueño, `0x4182f0` recibe 10 y `0x6002e0` corta en `0x60033c`; `0x418920` corta en los estados 5 y 6
  (`0x418957`, las órdenes del A7). Se dibuja como lo dejó el paso, igual que el personaje, y en un frame
  del original guarda la pose para la chispa.

`GameCharacterEffectsTest` y `GameRavenTest` cubren cada rama con sus sprites, sus `rand()` (CRT con la
semilla 1) y sus cortes; `GameCharacterRenderTest` y `UnarmedGameCharacterTest`, que la cola no corta sin
las ramas.

### Los objetos del juego

Los objetos del `.obj` en el juego, en los mapas de referencia, Lorencia (0) y Devias (2): el dibujo
(`0x6046a0`, desde `0x6374ce`) y el paso (`0x606560` → `0x604ab0`, desde `0x63706a`). Lo común con el
login está en [El logo en el binario](#el-logo-en-el-binario) y [El mundo del login](#el-mundo-del-login);
lo de cada tipo del 0, en [Lo propio de Lorencia](#lo-propio-de-lorencia). Leído en el hito 3 de la CL7
(*estático*; las cuentas de MuRet, *simulado*: con el port y la Data del repo).

**El dibujo** (`0x6046a0`). Al entrar sube el contador `0x5763538`, que da la vuelta de 1 a 41
(`0x6046be..0x6046ce`; sin leer quién lo usa). Después, sector por sector (x afuera, y adentro):

1. El sector, en su centro, contra el trapecio con −180 (`0x604703`); si no pasa, sus objetos no se miran.
2. Cada objeto vivo, en su lugar por 0,01f y con su margen (`+0xdc`), contra el mismo trapecio
   (`0x60476b`); el resultado queda en `+0x16c` (`0x604773`), lo lee el paso que sigue, y sin verse no
   sigue (con `0x7d36899`, la vista de arriba, sí).
3. **En el 2, el `0x64`** (`0x604793..0x604a3e`) sigue solo con el nivel: la clase del héroe
   (`[0x743a170] + 0x1d4`, & 7) en 3 o 4 pide `0x35` (53) y las demás, `0x50` (80). El nivel (`+0xe` de
   `[0x7681eb0]`) se lee con el registro propio abierto por la tabla de protección y se compara con signo
   (`jl`, `0x604955`); al cerrarlo (`0x604a1a`), con la cuenta en 0, `0x40b4d0` lo vuelve a cifrar: **3.848
   `rand()` por `0x64` visto y por frame** (Q-C257). Sin el nivel salta al siguiente (`0x604a3e`): ni se
   dibuja ni corre `0x602ed0`, pero ya quedó visto y el paso lo avanza.
4. `0x602e90(objeto, 0, 0, 0)` (`0x604a47`): `0x5ffdf0` (el color y la pose) y, si posó, `0x6002e0`.
   `0x5ffdf0` y su color (`0x5ffce0`) solo miran tipos que el 0 y el 2 no tienen (`0xe4`, `0x172`,
   `0x189`, `0x18e` y `0x18f`), y en `0x6002e0` el único caso de los dos mapas es el `0x69` del 0
   (`0x600a4a`, [Lo propio de Lorencia](#lo-propio-de-lorencia)). Los demás, también el burro (`0x201`) y
   la bandera (`0x202`) del 2, van por el `RenderBody(2, …)` común, como en el login.
5. `0x602ed0(objeto)` (`0x604a4d`): **un `rand()` por objeto** (`0x602ef4`), antes de mirar el tipo, y el
   caso del mapa. En el 0 (`0x602ffb`), el `0x38` (`0x603149`: sus dos sprites salen en NaN, Q-C256) y el
   `0x69` (`0x603011`). En el 2
   (`0x6031e5`), el `0x64` (`0x603251`): dos sprites `0x5d5` de 2,5 en (0, 0, 150) por el hueso de
   `0x68b181c`, que giran con el reloj, ±(`ftol(T · 0,1)` % 360); y el `0x67` (`0x6031f7`): con el cuadro
   del modelo (`+0xa8`) en el último de su acción, `rand()` & 31 elige la acción con `0x4ea030`: la 1 si da
   0, la 0 si no. El `+0xa8` lo escribe `0x4eb860` cuando `0x5ffdf0` posa el objeto: `(short)ftol` del frame
   (`+0x114`), en 0 si es negativo o no es una key de la acción del modelo (`+0xa0`, la del objeto que copió
   `0x5ffdf0`, en 0 si el modelo no la tiene). La key que se compara (`0x603205`) es la de la acción del
   objeto (`+0x111`) sin acotar. El `0x67` es `Object104.bmd`: dos acciones de seis keys, así que en la
   key 5 de cualquiera de las dos sale un `rand()` y, una vez en 32, pasa a la 1 (o se queda en ella).

**El paso** (`0x606560`, también en la espera): sector por sector, como el dibujo. Los objetos de un
sector que no pasó quedan con `+0x16c` en 0 sin avanzar, como en el login ([El mundo del
login](#el-mundo-del-login)), y los vistos van a `0x604ab0`. `0x6061c0` al empezar, `0x6063d0` y
`0x5ff960` con cada uno y el `0x6c9ddc--` del final no hacen nada en el 0 ni en el 2. En `0x604ab0`, en
orden:

1. **Los techos**, antes del alpha. En el 0, el `0x7d` y el `0x7e` (`HouseWall05` y `06`) llevan la meta
   del alpha (`+0x170`) a 0 con la capa 1 de la celda del héroe (`0x77f36d0`) en 4, y a 1 si no
   (`0x604bc4`). En el 2, el `0x51`, el `0x52`, el `0x60`, el `0x62` y el `0x63`, con la capa en 3 o desde
   10 (`0x604c06`; *inferido*: el héroe bajo techo).
2. El alpha hacia su meta (`0x4e9980`); por debajo de 0,01 sale. La animación (`0x4ec200`).
3. El `switch` del mapa (`0x604d68`). En el 0 (`0x604d6f`, por el tipo − `0x32` en la tabla de
   `0x605b58`), lo de [Lo propio de Lorencia](#lo-propio-de-lorencia): la V del `0x76` y el `0x77`, la luz
   del `0x75` y el `0x7a`, los fuegos (`0x52f360`) y las luces de la lámpara y la vela (`0x5f9780`). En el
   2 (`0x60506d`, por el tipo − `0x14` en `0x605c08`): el `0x4e` sortea la luz del blendMesh como las
   ventanas del 0, (`rand()` & 3 + 4) · 0,1f (`0x60508f`); el `0x1e` y el `0x42` prenden un fuego con
   `0x52f360` (`0x6050b9`); y las puertas `0x14`, `0x41`, `0x56` y `0x58` (`0x6050d4`): sin el modo editor
   (`0x77f3b18` en 0), la distancia al héroe decide si se abren, con el sonido `0x11` o `0x12`
   (`0x41ed10`), o si vuelven con `0x4e9560`.

**Los `rand()`.** En un frame del original con el 0 o el 2: los 4 del contador y los 8 de la cámara;
después, uno por objeto visto que llega a `0x602ed0`, con los 3.848 de cada `0x64` de Devias antes del
suyo y, en el `0x69` de Lorencia, el `rand() % 2` de sus gotas y, si es par, los de cada una
([Lo propio de Lorencia](#lo-propio-de-lorencia)). En el paso, antes de MoveHero, los de los casos de cada
objeto visto, en el orden de los sectores: uno por luz que titila (`0x75`, `0x7a`, `0x4e`, y la lámpara y
la vela) y varios por fuego.

**En MuRet** (CL7, hito 3). El dibujo es `Scenes::DrawGameWorldObjects`, desde
`GameScene::DrawTheWorldPass`, con el recorrido de `DrawWorldObjects` del login (el sector, el objeto,
`+0x16c`, el color de `0x5ffce0` y el `RenderBody` común) y la pose del último paso, como el original
(sin la interpolación de D-C4). Con un frame del original saca el `rand()` de `0x602ef4` y abre el
registro propio con `LogicalAccess` en el `0x64`; en un dibujo intermedio, ninguno de los dos. El paso es
`Scenes::StepGameWorldObjects` → `World::StepWorldObjectChecked`, desde `GameScene::StepTheObjects`, con
la capa del héroe de `WorldRuntime::heroCellLayer` y el reloj del mundo. Los dos cortan con la dirección
en lo primero que MuRet no tiene, con lo de antes ya hecho: en el dibujo, los casos de `0x602ed0` que no
tiene, después de su `rand()` (el `0x38` del 0 deja sus dos sprites en NaN con
`Scenes::StepMerchantAnimalFrame`, Q-C256). Los del 2 están en `Scenes::StepDeviasObjectFrame`: los dos
sprites del `0x64` con `ObjectBonePoint` sobre la pose global retenida, y el `0x67` con el `+0xa8` que
calcula del frame y la acción del objeto, porque `0x5ffdf0` lo acaba de posar; sin posar (el alpha por
debajo de 0,01, que el `0x67` no baja porque no es un techo), el `+0xa8` es de otro dibujo y corta en
`0x60320a`. El `0x69` se dibuja con
`Scenes::DrawWaterspout` y crea las dos gotas con `Scenes::StepGameWaterspoutFrame` sobre
`WorldState::Particles`; si el modelo actual no escribió un hueso, conserva el corte de la lectura de
pose anterior (`0x6030a5/0x603121`), con los sorteos y las gotas previos ya hechos
([Las partículas de la fuente en el juego](#las-partículas-de-la-fuente-en-el-juego)). Los objetos ya
reciben `WorldState::DynamicLighting().Lighting()`, no la tabla de base; siguen pendientes en el paso
los fuegos, las luces que van a ese buffer (la lámpara y la vela, después de su `rand()`) y las puertas
del 2. Los modelos los carga `Scenes::WorldModels` la primera vez que el dibujo o el paso los piden:
ya incluye los modelos con nombre de Lorencia y las familias de `WorldSpecialModels`, con sus archivos,
carpetas de texturas y stores concretos de acciones. Ese catálogo no cambia la carga a demanda ni
porta los casos del mundo; el original los carga en `0x61a560`. Lo que queda, en
[08](08-pendientes.md) P-C53. Con la Data del repo (*simulado*):

| Mapa, héroe | Vistos | Hasta | `rand()` del pase | Corte del dibujo | El paso que sigue |
| --- | --- | --- | --- | --- | --- |
| 0, (143, 120), el de la traza | 83, de los sectores (7, 7), (8, 7), (8, 8), (9, 6), (9, 7) y (9, 8), con el `0x69` (1720) | todos | 84: los 83 y el `rand() % 2` del `0x69`, impar con la semilla del test | `0x6374fe` | con la capa en 4, el techo 1400 (`0x7d`) va a 0 y los tres `0x7a` (1366, 1372 y 1690) sacan su luz; corta en el primer fuego visto, el `0x32` 1933 (`0x604eb0`), antes de MoveHero |
| 2, (220, 30) | 54, de (12, 1) a (14, 2), sin el `0x64` ni casos | todos | 54 | `0x6374fe` | con la capa en 3, los techos 1282, 1471, 1472 y 1473 van a 0 y los tres `0x4e` (1262, 1462 y 1464) sacan su luz; corta en la puerta 1481 (`0x41`, `0x6050d4`), antes de MoveHero |
| 0, (96, 126) | 20, sin casos | todos | 20 | el jugador visto ya tiene un pase parcial; la frontera depende de su rama pendiente (P-C55 d) | — |
| 2, (220, 76) | 44, sin casos | todos | 44 | `0x6374fe`, con el héroe sin ver | — |

En la traza de Lorencia, los objetos son 120 bloques con texturas de `0x41c..0x47e`, entre el pasto y los
pájaros ([El dibujo del juego](#el-dibujo-del-juego)); MuRet todavía no los compara (P-C53 h). Tests:
`WorldObjectsDrawingTest` (`TheGamePassDrawsTheLiveObjectsWithARandEach`,
`TheLorenciaMerchantAnimalLeavesTwoGlowsAtNaN`, `TheLorenciaWaterspoutDrawsFourLooseMeshes`,
`TheLorenciaWaterspoutRunsItsFrameCase`, `TheLeveledObjectOfDeviasReadsTheOwnRecord`,
`TheDeviasBeamObjectLeavesTwoSpritesOverItsFirstBone`, `TheDeviasSwitchingObjectChangesItsActionOnTheLastKey`,
`AnIntermediateGameDrawTakesNoRandomness`, `TheGamePassKnowsOnlyTheReferenceMaps` y
`TheGameStepStopsAtTheFirstCaseMuRetDoesNotHave`), `WorldObjectsTest`
(`TheLorenciaWaterspoutDrawsItsOwnMeshes`, `TheWaterspoutFrameScrollsItsVAndDropsHalfTheTime`,
`TheWaterspoutFrameNeedsACreditedClock`,
`TheRoofsFollowTheLayerOfTheHero`,
`TheWindowsFlickerWithARand`, `TheBlendMeshOfTheHousesScrollsWithTheClock`,
`TheCasesMuRetDoesNotHaveStopWithTheirAddress`, `AFadedOutObjectSkipsItsCaseButNotItsRoof` y
`TheCheckedStepKnowsTheReferenceMapsAndTheLogin`), `GameSceneDrawTest`
(`TheReferenceMapsDrawTheTerrainTheGrassTheGroundMarksAndTheObjects`,
`TheStepAfterADrawStepsTheSeenObjects`, `TheStepStopsAtTheSeenObjectsOfTheOtherMaps` y
`TheFirstStepAfterTheWaitReadsTheTrapezoidOfTheSelection`) y
`GoldenWorldModels.DeviasLoadsTheDonkeyAndTheFlagBeforeItsObjects`.

### Los objetos del Dungeon y de Lost Tower

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común (el sector, `+0x16c`, el alpha, la animación y el `rand()` de `0x602ef4`) es
lo de [Los objetos del juego](#los-objetos-del-juego).

**El paso** (`0x604ab0`, después del alpha y la animación). En el 1 (por la tabla `0x605bc0`):

- `0x16`, `0x17` y `0x18`: el byte `model+0x88` de su tipo en 1 (`0x605003`) y la V (`+0x70`) en
  (`ftol(T)` % 1.000) · −0,001f (`0x6ba1a4`): el resto firmado sin negar el ftol, al revés que el agua del 0.
- `0x27`, `0x28` y `0x33`: `+0x58` en −2.
- `0x29` y `0x2a`: fuegos de clase 0 de `0x52f360` a (0; −30; 240) (`0x605043`) y (0; 0; 190) (`0x60505e`).
- `0x34`: con `rand()` % 3 en 0, `0x518cf0(0xda, posición, ángulos, luz +0xf4, 0, 0, −1, …)` (`0x604fdb`), la
  piedra que cae; siempre después, `+0x58` en −2.

En el 4 (`0x6053d5`, por el tipo − 3 en la tabla `0x605c6c` → `0x605c50`):

- `3` y `4`: la U (`+0x6c`) en (−`ftol(T)`, negado en 32 bits) % 1.000 · 0,001f (`0x6b89c8`), como el agua del 0.
- `0x13` y `0x14`: `+0x64` (el blendMesh) en 4 y la misma U (`0x60542f`).
- `0x12` y `0x17`: el blendMesh en 1 (`0x605461`).
- `0x18`: `+0x58` en −2 y, con `rand()` & 63 en 0, `0x518cf0(0x5de, …, luz +0xf4, 0, 0, −1, …)` (`0x6054a9`),
  el chorro.
- `0x19`: `+0x58` en −2 (`0x6059dd`).
- `0x26` y `0x27`: la patada (`0x52f550`). Con la acción del héroe (`+0x111` de `[0x743a170]`) entre `0xd` y
  `0x21` y la x de la velocidad (`+0xcc`) por debajo de 0,1f (NaN también: `fcomp` sin orden pone C0), mide
  dx y dy al héroe; dy se guarda en float y la distancia es √(dy · dy en float + dx · dx). A menos de 50, la
  velocidad queda en (dx · −0,4f; dy · −0,4f; 0), el giro (`+0x28`, `+0x2c`) en (dy · −4; dx · −4) y suena el
  `0x7c`. Siempre después, la velocidad y el giro por 0,6f, sumados a la posición y a los ángulos; la x de la
  posición suma el producto sin redondear de la x87.

**Los efectos** (`0x518cf0` y su paso, `0x5208a0`). Los que crean los objetos del 1 y del 4 no tienen dueño
(`+0x108` en 0) y llevan `+0x8a` en −1:

- `0xda` (`DungeonStone`, `0x51b80a`): seis `rand()`: la vida (& 15) + 24, la escala ((& 7) + 6) · 0,1f, la
  caída (`+0xe4`) en −(& 3) (−0 con 0) y el corrimiento ((& 63) − 32; −((& 31) + 50); (& 127) + 200), girado por
  los ángulos y sumado a la posición. El paso (`0x52444d`): la z suma la caída, que pierde 1; si el suelo
  (`0x5f93c0`) queda por encima (ordenado), se apoya, la caída rebota a −0,4f, pierde 4 de vida y 2 en la y de
  la velocidad.
- `0x5de` con el subtipo 0 (`0x51e7d8`, el chorro, sin modelo: solo el pase del suelo): vida 40 y el registro
  propio abierto y cerrado (`+0xf04` a `+0x8c`, que solo lee la rama del dueño héroe); con la cuenta en 0, el
  cierre gasta los 3.848 `rand()` de `0x40b4d0` (Q-C257). El paso (`0x52ad21`): seis llamas `5de/0` a ±25 en x e
  y (sus dos sorteos antes de cada creación) con luz blanca; con `rand()` & 7 en 0, la piedrita
  `0xc8 + (rand() & 1)` con su luz; y `0x5f9780(x, y, (L; L · 0,4f; 0), 3)`, con L la luz local del primer
  `rand()` del paso, ((& 3) + 7) · 0,1f, que con menos de 5 de vida baja (5 − vida) · 0,2f hasta 0. La rama del
  dueño héroe (`0x52ae2a`) no corre.
- `0xc8` y `0xc9` (`Stone01` y `Stone02` de `Skill\`, que carga el lazo `0x616e20` de `0xbe` a `0x150`) con el
  subtipo 0 (`0x51b72b`): el empuje ((& 255) + 64) · 0,1f en y, la vida (& 15) + 32, la escala ((& 3) + 8) ·
  0,1f, el yaw `rand()` % 360, la velocidad (el empuje girado por los ángulos con ese yaw) y la caída (& 15) + 8.
  El paso (`0x5239c8`): la velocidad sin girar se suma a la posición y queda por 0,9f; la z suma la caída, que
  pierde 3; en el suelo se apoya, la caída rebota a −0,5, pierde 4 de vida y se inclina escala · 128 (en el aire,
  escala · 32); con `rand()` % 10 en 0, un fuego `5d9` de subtipo `rand()` % 3 + 1 con su luz. Después,
  `0x51fc80` vuelve a sumar la velocidad, girada (Q-C307).
- La llama `5de/0` (`0x54143c`): vida 20, la velocidad (0; 0; ((& 127) + 128) · 0,15f) y la escala ((& 63) +
  64) por el argumento por 0,01f. Su paso es el común (`0x545d03`), y se dibuja con el billboard común.

**El dibujo.** En el 4, `0x6002e0` (el objeto posado y sin −2) saca un `rand()` con el 3, el 4, el `0x13`, el
`0x14` y el `0x17`; con los cuatro primeros, antes del cuerpo, un `RenderBody` (`0x4ef0c0`) con `Chrome01`
(`0x5c0`) forzado, con el color del modelo en (1; 0,2f; 0,1f) y `model+0x88` en 2 (el `0x13` y el `0x14`) o 1
(el 3 y el 4), y el alpha, el blendMesh, su luz, la U, la V y la malla oculta del objeto; después vuelve el
color y deja `model+0x88` en `0xff`. Después, el cuerpo común (`0x600b35`). El 1 no tiene casos en `0x6002e0`.
En `0x602ed0`, el 1 va a la cadena de `0x6044ed`, que no tiene nada para él; el 4 (`0x603799`):

- `0x13` y `0x14`: dos sprites, de giro r y −r (r = `ftol(T · 0,1f)` % 360), en cada uno de los huesos 15 y 19
  (escala 0,3) y 21 (1,5) del buffer global de la pose: `0x61e` con (L; L · 0,2f; 0) el `0x13` y `0x5d5` con
  (L · 0,4f; L · 0,8f; L) el `0x14`.
- `0x28`: dos `0x5d5` de 2,5 sobre el objeto, 260 más arriba (`0x6b88f0`), con (L; L; L) y el mismo giro.
- Después cae al caso del 6 (`0x6039cb`): el 9 deja un sprite `0x5ac` de escala L · 5 y luz (L · 0,6f;
  L · 0,3f; L · 0,1f) en el hueso 1 (`0x68b184c`) con la entrada de `0x4ec110` en la pila sin escribir, como el
  `0x38` del 0: en NaN (Q-C256).

**En MuRet.** El paso: `World::StepDungeonObject`, `World::StepLostTowerObject` y `World::KickObject`, desde
`World::StepWorldObjectChecked` (`GameScene::StepTheObjects` le presta la tabla de efectos, la cuenta del
registro propio y la acción del héroe). Los efectos: `World::ModelEffects::CreateFallingStone`,
`CreateFlameJet` y `CreateStoneChip`, y sus pasos en `World::StepGameModelEffect` (con la tabla de efectos y la
luz dinámica prestadas); la llama, en `World/SelectionParticleKernels.h`; los modelos `0xc8` y `0xc9`, en la
cola de `Scenes::GameModels` (sus slots no son los del original, que pasa antes por los modelos de las
habilidades). El dibujo: el pase cromado en `Scenes::DrawGameWorldObjects` (`model+0x88` no vuelve a `0xff`,
que es la entrada ausente de `ObjectModelStepState`) y los sprites en `Scenes::StepLostTowerObjectFrame` y
`Scenes::StepArenaObjectFrame`. Un destino prestado que falta corta con su dirección después de lo que ya
escribió. Con la Data del repo, el 1 y el 4 llegan a un cuadro completo y caminan (`GameT16SceneMatrix`,
`GameT16MapWalk`; el Dungeon no tiene zona segura). Tests: `DungeonObjectsTest`, `ModelEffectsTest`
(`TheFallingStoneDrawsSixTimesAndStartsAboveItsDrip`, `TheFlameJetLivesFortyAndClosesTheOwnRecord` y
`TheStoneChipsArePushedAlongTheirDrawnYaw`), `GameModelEffectsTest` (`TheFallingStoneFallsAndBouncesOnTheGround`,
`TheStoneChipsDragTiltBounceAndSometimesBurn`, `TheFlameJetSpitsSixFlamesASometimesChipAndItsLight` y
`TheFlameJetFadesItsLightAndCutsWithoutItsTables`),
`SelectionParticlesTest.TheLostTowerFloorFlameRisesStraightWithTwoDraws` y `WorldObjectsDrawingTest`
(`TheLostTowerChromeGoesBeforeTheBody` y `TheLostTowerFrameLeavesItsSprites`).

### Los objetos de Tarkan

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). T es el reloj del
mundo (`0x5767b88`, en ms) y `ftol` corta hacia cero en 32 bits.

**La carga** (`0x616ec0`, el caso del mapa 8 en `0x6177f9`): antes de los `Bug` (`0xb2`), `0x63b870` con
`GL_LINEAR` y el sexto argumento en 1 (avisa si falta) sube `Object9\sand01.jpg` a `0x5c2` y `sand02.jpg` a `0x5c3`,
repitiendo (`0x2901`), e `Impack03.jpg` a `0x716`, sin repetir (`0x2900`). Las dos arenas son del HUD
(`0x5a7520`, desde `0x5a7320`, que en el mapa 8 corre dos `0x622000` sobre la pantalla: CL8); `Impack03`, de los
sprites del `0x3f` y el `0x40`. Al final de la carga de los modelos del mundo (`0x61a145..0x61a18f`), el byte
`model+0x88` de los modelos 11, 12, 13, 73, 75 y 79 queda en 0: su malla 0 va sin luz, en el color del modelo, y
con la U y la V del objeto (`0x4ecc30`, abajo).

**El paso** (`0x604ab0`, la tabla `0x605cc0` por el byte de `0x605cfc` con el tipo − 2). Antes de la animación
(`0x604c70..0x604c8e`), el 8 la avanza con la velocidad (`+0xd8`) por 4 (`0x6b8a44`) en una copia; `+0xd8` no
cambia. Con L = sin(fase · 0,002f) · 0,35f + 0,65f, el seno y la suma en la x87:

- `2` (`0x605683`): el blendMesh (`+0x64`) en 0 y la U (`+0x6c`) en (−`ftol(T)`, negado en 32 bits) % 1.000 ·
  0,001f (`0x6b89c8`).
- `4` (`0x6056b5`): el blendMesh en 0, su luz (`+0x68`) en L (fase T) guardada en float, la V (`+0x70`) en
  (−`ftol(T)`) % 10.000 · 0,0001f (`0x6b8a94`) y `0x5f9780(x, y, (L; L; L), 3)` (`0x605718`).
- `7` (`0x605727`): la fase es ángulo z · 100 (`0x6b86b4`) + T; el blendMesh en 0, su luz en L y la luz
  (L; L · 0,6f; L · 0,2f) de radio 3 (`0x60577d`).
- `0xb`, `0xd`, `0x49`, `0x4b` y `0x4f`: la V en (−`ftol(T)`) % 10.000 · 0,0002f (`0x6b8a64`); el `0x48`, igual
  y con el blendMesh en 0.
- `0xc` (`0x6057b7`): la U y la V en (−`ftol(T)`) % 50.000 · 0,00005f (`0x6b8a28`), de dos `ftol`.
- `0x3d`, `0x41` y `0x42` (`0x605831`, `0x605855`): el blendMesh en 1, la V en (−`ftol(T)`) % 1.000 · 0,001f y la
  luz (L; L · 0,6f; L · 0,2f) de radio 2 por el mismo `0x605718`, sin tocar `+0x68`.
- `0x3f` y `0x40`: `+0x58` en −2 (`0x6059dd`).
- `0x52` (`0x605970`): el blendMesh en 0 (EDX, en 0 desde `0x604d50`) y la luz del objeto (`+0xf4`) en (1; 1; 1).

**El dibujo.** En `0x6002e0`, el `0x51` (`0x600c72..0x600cdc`, `Object82`, una malla con `DDD.jpg`) no va por
`RenderBody`: entre un `glPushMatrix` (`0x4ecc10`) y un `glPopMatrix` (`0x4ecc20`), dos `0x4ecc30` de su malla 0:
la común (2, con el alpha, el blendMesh, su luz, la U, la V y sin textura forzada) y la del cromo (`0x44`, con
`Chrome01` en `0x5c0`): las UV (0,5 · nz + t; 0,5 · ny + 2t), aditiva, en el color del modelo (`+0x48`) sin la
luz de cada normal; con el alpha por debajo de 0,99, ese color por el alpha. En `0x602ed0` (la tabla `0x604554`
por `0x604580`, el 8 en `0x603a3e`; los tipos por `0x6045f8` y `0x604618`), con s = sin(fase · 0,002f) · 0,3f +
0,7f y los sprites en el buffer global de la pose (`0x4ec110` con el desplazamiento en cero, `0x602fce`):

- `0` (`0x603a5a`): un `0x5c6` (`Spark03`) en el hueso 6 (`0x68b193c`), de escala s guardada · 5 y luz
  (s · 0,5882353f; s; s), con la fase T.
- `0x3f` (`0x603b0c`): un `0x716` (`Impack03`) en el hueso 2 (`0x68b187c`), con la fase ángulo z · 5 + T, de
  escala s guardada · 1,5 y la luz del 0; el `0x40` (`0x603b87`), igual pero con s guardada antes:
  (s; s · 0,32f; s · 0,32f).
- `0x3c` (`0x603acc`): con `+0x58` distinto de −2, veinte `0x53fe70(0x5f2, posición, ángulos, luz +0xf4, 6,
  escala +0xc, 0)`; después `+0x58` en −2, y no vuelve a humear mientras no se recargue el mapa.
- `0x46` (`0x603c06`): `+0x58` en −2 y, con `rand()` % 5 en 0, un `5f2/7`.
- `0x4c` (`0x603c4c`): `+0x58` en −2 y, con `ftol(T)` % 5.000 mayor que 4.500 (con signo), un `5f2/4`.
- `0x53` (`0x603c9d`): `+0x58` en −2; con b = `ftol(ángulo z)` · 10 (en 32 bits) y `ftol(T)` % 10.000 mayor que
  b + 3.500 y menor que b + 4.000 (con signo), un `5f2/8`; con `rand()` % 3 en 0, además un `5f2/4` corrido
  ((`rand()` & 127) − 64) en x y después en y, sumados en la x87 y guardados, con la z del objeto y media escala
  (`0x6b882c`), y la piedrita `0x518cf0(200 + (rand() & 1), posición, ángulos, luz, 0, 0, −1, 0, 0, 0, 0)`.

Los humos van al pool compartido de las partículas sin dueño, y uno lleno no es error; sus creaciones, pasos y
dibujos son los del humo del mapa 8 (`World::Particles`). `0x606560`, `0x6063d0`, `0x6061c0` y `0x5ff960` no
tienen nada para el 8.

**En MuRet.** La carga: `World::TarkanObjectTextures` (en la lista de `Scenes::TerrainTiles`) y
`World::SeedTarkanModelState` sobre `ObjectModelStepState` (`GameScene::LoadTheObjectModels`). El paso:
`World::StepTarkanObject` y `World::TarkanAnimationSpeed`, desde `World::StepWorldObjectChecked`. El dibujo:
`ObjectDrawKind::TarkanChrome` y `Scenes::DrawTarkanChrome`, y `Scenes::StepTarkanObjectFrame` (con la tabla de
efectos que presta `GameScene`). Un destino prestado que falta, o un reloj o un ángulo sin `ftol` acreditado,
corta con su dirección después de lo que ya escribió. Con la Data del repo, el 8 llega a un cuadro completo y
camina, también en la zona segura (`GameT16SceneMatrix`, `GameT16MapWalk` y `GameT16SafeZoneWalk`, en (70, 70) y
(206, 56)). Tests: `TarkanObjectsTest`, `TarkanObjectFrameTest` y
`WorldObjectsDrawingTest.TheTarkanChromeGoesAfterItsFirstMesh`.

### Los objetos de Icarus

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). En Icarus (el mapa 10)
los objetos casi no hacen nada en su paso: lo que se ve sale de un preludio antes de los objetos, de un paso
aparte por cada objeto visto y de su caso en `0x602ed0`.

**La carga** (`0x616ec0`, el caso del mapa 10 en `0x61788c`): `0x63b870` sube `Effect\clouds.jpg` otra vez a
`0x622` (`GL_LINEAR`, `GL_CLAMP`, lo mismo que la carga global del login); `0x60da30` carga el modelo `0xbb`
(`Object11\cloud`, sin número) y `0x60d5f0` sus texturas con `GL_REPEAT` y `GL_NEAREST`, como los demás
especiales; y `0x6178da..0x6178f2` sube `Effect\cloudLight.jpg` a `0x623` (`GL_LINEAR`, `GL_CLAMP`, sexto 1), el
sprite de `0x6063d0`.

**El trapecio** (`0x6046b2..0x6046c3`): en el 10, `0x6046a0` le suma −10 al margen de cada objeto (`+0xdc`) en la
x87 y lo guarda en float antes de `0x5fbaa0`; en los demás mapas suma 0.

**El paso** (`0x604ab0`): la tabla `0x605ab4` por `0x605ae8` no tiene casos para el 10. Lo demás va en
`0x606560`, en este orden:

1. `0x6061c0(&selector)`, con el selector en 0. En el 10, `0x605d70`:
   - `rand()` % 50; si no da 0, nada más.
   - La luz dinámica (`0x5f9780`, radio 2, `0x605e0a`) en (x del héroe + `rand()` % 300 − 150, sumados en la
     x87 y guardados; y del héroe). Un `rand()` que no se usa (`0x605dbb`, Q-C309). L = ((`rand()` & 3) + 4) ·
     0,05f y el color (L · 0,3f; L · 0,3f; L · 0,081f), cada canal guardado.
   - `0x518cf0(0xbb, héroe, 0, color, 0, 0, −1, 0, 0, 0, 0)`: la nube sobre el héroe (abajo).
   - Con `0x7d365a0` (la cuenta del pase anterior) distinta de 0, el selector es `rand()` % cuenta.
   - `rand()` % 5; si da 0, `AngleMatrix(0, 0, −45)` y un lado de cuatro (`rand()` & 3). El corrimiento del lado
     gira con `0x5fcbd0` en el lugar (la entrada y la salida son el mismo vector: la y gira con la x nueva y la z
     con las dos, Q-C310) y se suma al héroe (el lado 1 lo resta); el alcance gira con el yaw del lado, también
     en el lugar. Los lados, (corrimiento; yaw; alcance): 0, ((−400; −1.000); 240; (−200; −1.000)); 1,
     ((−300; −400), restado; 210; (−500; −1.000)); 2, ((−200; −400); 235; (−1.000; −1.500)); 3, ((−200; 400);
     200; (−600; −1.200)). Las dos puntas son centro + alcance y centro − alcance, las dos 300 más abajo, y
     salen dos `0x530860(0x615, centro + alcance, centro − alcance, 0, 0, 0, ancho, −1, 0, 0, −1)`, cada una con
     su ancho `rand()` % 10 + 40.
   - Después, en `0x6061c0`, con `rand()` % 10 en 0, `0x518cf0(0x5ac, …)` en (x del héroe + `rand()` % 5.000 −
     2.500; lo mismo con la y; z del héroe − 1.000), sin giro y con luz 1.
2. `0x7d365a0` en 0.
3. Sector por sector, cada objeto visto: `0x604ab0`, `0x6063d0(objeto, &selector, &0x7d365a0)` y `0x5ff960`.

`0x6063d0`, en el 10: la cuenta sube 1; con el selector en 0, nada más. Si no, con `rand()` % 10 en 0 y el tipo del
0 al 5, un sprite `0x5491e0(0x623, objeto, 0,5, luz, héroe, 0, 0)` con la luz (`rand()` % 10) · 0,02f por canal y
dos `0x530860(0x614, objeto, objeto, ángulos, 6, objeto, ancho, −1, 0, 0, −1)` de ancho `rand()` % 20 + 10; sin
eso, la cuenta vuelve a bajar. La cuenta del pase siguiente queda en la cantidad de objetos vistos (con el
selector en 0) o en la de los que chisporrotearon (Q-C311).

**El dibujo** (`0x602ed0`, el 10 en `0x603eef`, la tabla `0x60466c`):

- Del 0 al 5 (`0x603f03..0x6040db`): con `+0x58` distinto de −2, veinte (del 0 al 2) o diez (del 3 al 5)
  `0x53fe70(0x622, posición, ángulos, (0,1f; 0,1f; 0,1f), tipo, escala +0xc, objeto)`; después `+0x58` en −2.
- El 10 (`0x6040e9..0x604143`): `0x4ec110` con el hueso 3 del buffer global de la pose (`0x68b18ac`) y el
  desplazamiento en cero, y `0x53fe70(0x5ac, punto, ángulos, 1, 0, 1, 0)`.
- Del 6 al 9, nada.

**Las partículas.** Las nubes `622` del 0 al 5 comparten el alta del `622/1` de la selección (`0x5435cf`, siete
`rand()`, Q-C306) y su paso (`0x5476f0`): mientras el objeto se ve viven; en el giro, la tabla `0x54848c` hace
girar a favor el 1 y el 4 (0,02f, `0x6b8b90`), en contra el 2 y el 5 (−0,02f, `0x6ba354`) y no toca el 0 ni el 3.
El `5ac/0` nace como el `5ac/6` (`0x540151..0x54018f`); en el paso (`0x544c19..0x544dd6`, k = 0,5f) camina
(`rand()` % 2.001 − 1.000) · 0,0001f en x y en y, más el viento, por k (la x se guarda en float en cada paso, la y
no), sube k · 5 más `0x7609bc0` · k (que nadie escribe: 0, Q-C312), la escala baja 0,05f, la luz la copia y con la
escala en 0,1f o menos se apaga. El `5ac/1` (`0x54007f`) sólo vive 50, sin `rand()`, y en el paso
(`0x5449e1..0x544a11`) la luz va por 0,9f y la escala por 0,95f.

**Los efectos con modelo.** La nube `0xbb` (`0x51b968`): vida 2, el blendMesh en 0, escala 10, 200 más en y y 190
menos en z, sin luz por vértice; en el paso sólo tiene la cola común. Sin luz por vértice, `0x5ffce0` le da el
color terreno · 0,1f + luz (la x sin redondear antes de la suma; la y y la z redondeadas, `0x5ffda5..0x5ffde7`).
El `0x5ac` (`0x51e3ec`): la luz en 0,3f, s = `rand()` % 5 + 9 y la velocidad sin girar (sen 30° · cos 70°;
cos 30° · cos 70°; sen 70°) · s · 0,5 en la x87, con los double `0x6ba1c8` y `0x6ba1d0`; escala 3 y vida 400. Su
paso (`0x52a737..0x52a7af`) suma la velocidad, le baja 0,01f a la z, suelta un `5ac/1` con su luz y su escala y,
con la z de la velocidad bajo −2 (o NaN), lo mata; la cola común (`0x51fc80`) vuelve a sumar la velocidad (los
ángulos son 0), y con la vida que queda impar (`0x52cdb3..0x52cdfb`) da otro paso entero, con su `rand()` común
(Q-C313). No se dibuja (`0x52daa1`): se ve por sus estelas.

**Las uniones.** El `615/0` (`0x5319ea..0x531a30`) toma la textura `0x614`, el ancho, 50 puntos, vida 20, la
velocidad `rand()` % 20 + 80 y la z de la luz en 50, y guarda la partida en `+0x24`. En el paso
(`0x53727d..0x537408`), con vida de más de 15, rehace el rayo: un destino por paso (`rand()` % 200 + x − 100;
`rand()` % 100 + y − 50; z, del `+0x58`), vuelve a `+0x24` y, en cada uno de los 50 tramos, gira hacia el destino
con `0x4e9810` de a `rand()` % 80 + 60, sacude (`rand()` % 1.400 − 700) / ancho la x y la z de los ángulos (la z
sin redondear), suma la cola con esos ángulos y avanza con el giro en el lugar de `0x5fcbd0`. Con 15 o menos, la
z de la luz baja 10,12f. La cola común, con la matriz del principio del paso, sólo con vida de más de 10. El
`614/6` (`0x531563..0x53168c`): velocidad `rand()` % 10 + 15, vida `rand()` % 20 + 6, el gris (`rand()` % 10) ·
0,06666667f + 0,1f, el ancho y 50 puntos, sin cola común; el objeto queda como dueño. En el paso
(`0x53683e..0x536e8a`) sólo avanza hasta que la vida llega a 4; desde ahí rehace el rayo desde `+0x24` hacia el
objeto (leído en vivo): en cada tramo, el destino es su posición más `rand()` % 200 + 2.050 en x y en y y 10.000
más abajo, gira de a `rand()` % 100 + 50 y sacude `rand()` % 100 + 20 la x y la z, sin dividir. En el dibujo
(`0x53d01d..0x53d043`), la segunda cara del `614` y del `615` corre sus dos u en dos veces el corrimiento del
reloj.

**En MuRet.** La carga: `World::IcarusObjectTextures` (en la lista de `Scenes::TerrainTiles`) y el `0xbb` de
`WorldSpecialModels`. El margen: `ExtraObjectMargin` en `WorldObjectsDrawing`. El preludio y el paso por objeto:
`Scenes::StepIcarusObjectPrelude` y `Scenes::StepIcarusSeenObject` (`Scenes/IcarusObjectStep.h`), desde
`GameScene::StepTheObjects` con el `AfterObjectStep` de `StepGameWorldObjects`. El dibujo:
`Scenes::StepIcarusObjectFrame`. Las partículas, en `World::SelectionParticleKernels` y
`World::KalimaParticleKernels`; los efectos, en `ModelEffects::CreateIcarusEffect` y `StepGameModelEffect`; las
uniones, en `CreateFaunaJoint` y `StepJoints` (`Scenes/FaunaJointCreation`). Un destino prestado que falta corta
con su dirección después de lo que ya escribió. Con la Data del repo, el 10 llega a un cuadro completo y camina
(`GameT16SceneMatrix` y `GameT16MapWalk`, en (20, 29): el bloque libre con más objetos cerca, 76 a menos de
1.500); Icarus no tiene zona segura. Tests: `IcarusObjectsTest`, `KalimaParticlesTest.TheIcarusFlare*`,
`SelectionParticlesTest.TheIcarusCloudsShareTheMistAndTurnBySubtype`,
`ModelEffectsTest.TheIcarusCloudAndFlareAreCreatedWithTheirOwnFields`,
`GameModelEffectsTest.TheIcarusFlareStepsTwiceAndLeavesTwoTrails`,
`GameJointDrawingTest.BoltsShiftTheirSecondFaceByTwiceTheScroll` y
`OriginalRotationTest.TheInPlaceRotateReadsEachStoredRow`.

### Los objetos de Aida

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). Aida (el mapa 33) usa
los casos del 54 de la selección: el paso `0x4250f0` y el dibujo `0x425260` de
[El mundo de la selección](#el-mundo-de-la-selección); lo que esa sección no cubre son los tres tipos que el 54 no
tiene. `World34` trae doce `0x29`, tres `0x41`, ocho `0x42` y cinco `0x46`.

**La carga** (`0x616ec0`, `0x618566..0x6185e6`): los especiales `0xae` (`Object1\Butterfly`), `0x141`
(`Object34\tree_eff`) y `0xb2` (`Object9\Bug`); `Effect\clouds.jpg` a `0x622`; y los sonidos `0x246..0x248`, que
son de la CL10. Ni `0x6061c0`, ni `0x6063d0`, ni `0x6002e0`, ni los `model+0x88` de después de la carga tienen
algo para el 33.

**El `0x29` y el `0x41`/`0x42`** (`0x4255f3` y `0x42588d`, en `0x425260` desde `0x602ed0`): entre `0x4ecc10` y
`0x4ecc20` (`glPushMatrix` y `glPopMatrix`; el argumento 1,0 no se lee), el `model+0x88` en 0 (la malla 0 sin luz
por vértice, con el color del modelo y la U y la V corridas) y `0x4ecc30(0, banderas, +0x174, +0x64, +0x68, U,
+0x70, −1)`, la malla 0 sola. La U es (−ftol(`0x5767b88`) % 100.000) · paso: `neg`, `cdq` e `idiv`, con el resto
del signo del dividendo, por la constante en la x87 y guardada en float (el pseudocódigo la muestra como el
ftol de `+0x70`, que es la V). Después `model+0x88` vuelve a `0xff`, que es lo que esos tipos tienen en Aida.

| Tipo | Banderas | Color del modelo (`+0x48`) | Paso de la U |
|---|---|---|---|
| `0x29` | `0x42` (con textura y aditivo) | 0,52 (`0x3f051eb8`), que queda en el modelo | 0,00001f (`0x6b8a2c`) |
| `0x41`, `0x42` | 2 (el pase base) | el que le dejó `0x5ffce0` en el cuerpo | 0,00005f (`0x6b8a28`) |

El paso le deja al `0x29` medio alpha (`+0x174` en 0,5), que va al cuerpo y al `0x4ecc30`.

**El `0x46` y el efecto `0xb3`** (`0x42593f..0x4259ae`): `timeGetTime()` & `0x3ff` (Q-C304) y `rand()` % 5; con
1 y el reloj debajo de 30, `0x518cf0(0xb3, posición, ángulos, (0,1f; 0,1f; 0,1f), 0, objeto, −1, 0,
ftol(+0xc), 0, 0)`. La creación (`0x51900b`) pisa con 0 el byte `+0x88` que traía la escala del objeto, pone
`+0x68` y `+0xd8` en 1, vida 1.000 y `+0xe4` y `+0xec` en 1, y saca seis `rand()`: la z, la y y la x ± 100 (`rand()`
% 200 − 100), el yaw más `rand()` % 360, la y de la velocidad (`rand()` & 7) · 0,1f − 5 (la x y la z con `esi`, que
el prefijo dejó en 0) y la escala 0,9 más (`rand()` % 5) · 0,025f; la partida (`+0x17c`) en la posición ya
corrida y la luz (0,01f; 0,01f; 0,03f). El modelo `0xb3` es el `Object8\Fish02` de Atlans, que Aida no carga
(Q-C314).

El paso (`0x520b77..0x520d0c`), después del `rand()` común de `0x5208c5`:

1. Con `rand()` % 40 en 1, `+0xd8` por −1 y `+0xe4` = (`rand()` % 3 + 5) · `+0xd8`.
2. Con otro `rand()` % 40 en 1, `+0xd8` por −1 otra vez y `+0xec` = (`rand()` % 3 + 1) · `+0xd8`. `+0xd8` es
   la velocidad de la animación de la cola común (Q-C315).
3. La distancia a la partida en la x87 (dz², más dy², más dx², y `fsqrt`, sin redondear); si no es menor que 500
   (`test ah, 1`: un NaN cuenta como menor) y el estado `+0x88` es 0, vida 50 y estado 1.
4. El yaw (`+0x24`) suma `fsin`(ftol(`0x5767b88`) % 10.000 · 0,0001f) · `+0xe4`, y el pitch (`+0x1c`) el mismo
   seno · `+0xec` · 1/3f (`0x6ba2a0`): cada producto en la x87, sin guardar el seno.
5. La luz, `+0x68` · (0,1f; 0,1f; 0,25f), y `0x53fe70(0x5ac, posición, ángulos, luz, 9, escala, 0)`.
6. La cola común (`0x52cb98`): el `0xb3` está en `[0xad, 0x151)` y avanza la animación con `+0xd8`; como está
   fuera de la tabla `0x52d1b8`, `0x51fc80` le suma la velocidad girada por los ángulos nuevos, y pierde una vida.

El dibujo del efecto es el genérico de `0x52d620` (un `RenderBody` con el modelo `0xb3`).

**La partícula `5ac/9`.** Nace (`0x53ffb9`) con vida ftol(escala · 40), sin `rand()`: con la escala de 0,9f a
1,0f, de 35 a 40 (0,9f · 40 es 35,999999). En el paso (`0x544b8e..0x544c04`), después de la vida común, la escala
baja 0,011f (`0x6ba3d8`) y se guarda con `fst`, pero la comparación con 0 lee la resta sin redondear. Con el byte
vivo ya en 0, o con esa resta que no queda por encima de 0 (o NaN), la vida va a 0 sin apagar el byte: lo apaga
la resta del paso siguiente. Desde 75 de vida (`cmp eax, 0x4b`, con signo) la luz va por 1,05f (`0x6ba3d4`);
debajo, por 0,952381f (`0x6ba264`). Se dibuja como las demás `5ac` (el billboard común).

**En MuRet.** El dibujo de la malla 0: `Scenes::DrawAidaMeshPass` (`CharacterDrawing`), desde
`DrawGameWorldObjects` en el 33 y el 54 después del cuerpo de cada objeto, con `DrawItemMeshPass` (el
`0x4ecc30` portado) y la pose del cuerpo. No saca `rand()` ni cambia estado, así que va en cada dibujo de MuRet y
no sólo en los frames del original, como el cuerpo (D-C4); el `rand()` común de `0x602ef4` queda donde estaba. El
`model+0x88` de MuRet (`ObjectModelStepState`) no lo toca: nada lo prende para esos tipos en Aida.
`Scenes::StepAidaObjectFrame` crea el `0xb3` con `World::ModelEffects::CreateAidaWisp`; su paso es el caso del
`0xb3` de `World::StepGameModelEffect`, con el seno PC53 de `OriginalRetainedMath` (con el signo del factor); la
`5ac/9` está en `World::KalimaParticleKernels`. El juego le presta al 33 el reloj del dibujo en ms como
`timeGetTime()` (Q-C304) y la escena 5. Con la Data del repo, el 33 llega a un cuadro completo y camina
(`GameT16SceneMatrix` y `GameT16MapWalk`, en (104, 165): suelo libre fuera de la zona segura, con `0x29` y `0x46`
vistos). Tests: `WorldObjectsDrawingTest.TheAidaBodiesRepeatTheirFirstMeshWithTheClockScroll`,
`AidaObjectServicesTest.TheWispLeavesOnlyWhenTheDrawAndTheClockAllowIt`,
`AidaObjectServicesTest.TheRedrawnBodiesAndTheOtherTypesDoNothingHere`,
`ModelEffectsTest.TheAidaWispIsCreatedWithSixDrawsAroundItsObject`, `GameModelEffectsTest.TheAidaWisp*` y
`KalimaParticlesTest.TheAidaWispTrail*`.

### Los objetos de Land of Trials

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). Land of Trials es el
mapa 31 (`World32`, 5.638 objetos). Sus tres casos los abre `0x43ff10` (el mapa en 31) en las tres cadenas: el paso
`0x43ffc0` (en `0x605a62`, después de `0x438a90` y `0x42c850`), el frame `0x4401e0` (en `0x602ed0`, después de
`0x4059d0`, `0x438c10` y `0x42c9f0`) y el dibujo `0x4403b0` (en `0x6002e0`, después de `0x438fe0` y `0x42cc10`).

**La carga** (`0x61822d`): el especial `0xae` (`Object1\Butterfly`); `Effect\clouds.jpg` a `0x622`, las texturas
del jefe (`Monster\bossmap1_R`, `0x63a..0x63c`) y los sonidos `0x1ef..0x1f2`, que son de la CL9 y la CL10. La
creación (`0x43ff70`) le da al `0x1b` y al `0x36` la fase de `+0x84`: (`rand()` % 1.000) · 0,01f. Los demás la
tienen en 0 (`0x606660` pone en cero los `0x1d4` bytes del objeto).

**El paso** (`0x43ffc0`), con un `switch` por tipo (la tabla `0x440180` con los índices de `0x44019c`):

| Tipo | Qué hace | `+0x58` en −2 |
|---|---|---|
| 1, `0x2c`, `0x2d` | con `timeGetTime()` & `0x3ff` debajo de 10 (Q-C304), la mariposa `0xae` /0, /1 o /2 con luz (1, 1, 1) y el objeto de dueño (`0x518cf0`) | sí |
| 3, `0x35` | nada: emiten en el frame | sí |
| `0x1b` | la z más sin(reloj · 0,0024f + `+0x84`) · 0,3f (`0x6b8bbc`, `0x6b89dc`), cada paso sobre la anterior | no |
| `0x2a` | ((`rand()` & 3) + 3) · 0,1f en la x87, por (0,9f; 0,2f; 0,1f), a la luz dinámica `0x7b4ccc4` con radio 3 (`0x5f9780`) | sí |

Después, con cualquier tipo (`0x440151`): si `timeGetTime()` menos `0x723c4c` pasa de 300.000 (`jbe`, sin signo),
una segunda llamada a `timeGetTime()` se guarda en `0x723c4c` y suena el ambiente `0x1ef` (`0x41ed10`). Devuelve 1
siempre que el mapa sea el 31.

**El frame** (`0x4401e0`), después del `rand()` común de `0x602ef4`, que no usa:

- El 3, con `rand()` % 3 en 0: `0x53fe70(0x6f2, posición, ángulos, (1, 1, 1), 3, escala, 0)`; el subtipo es el
  divisor que quedó en ECX.
- El `0x35`, con `rand()` % 3 en 0: `0x53fe70(0x5f2, …, 22, escala, 0)`.
- El `0x31`: el punto del hueso 3 del buffer global de la pose (`0x68b18ac`) con (0, −10, 0) (`0x4ec110`), y ahí
  `0x5491e0(0x5ac, punto, escala, (1, 0, 0), objeto, 0, 0)` con la escala ((sin(reloj · 0,002f) · 0,35f + 0,65f) ·
  1,2f + 0,3f) en la x87 hasta el `fstp`; otro `0x5ac` de 0,5 con la misma luz; y `0x5491e0(0x5fd, punto, 1,2,
  (0,3; 0,3; 0,3), 0, rand() % 360, 0)`.

**La salpicadura `6f2/3`** (también `6f3/3`). Nace (`0x544319`) con vida 60, la velocidad z en −1 y cuatro `rand()`:
la escala (`rand()` % 10 + 15) · escala · 0,02f, el giro `rand()` % 360, la x más `rand()` % 40 − 20 y la z más
`rand()` % 25 − 10. En el paso (`0x54815d`), después de la vida y el movimiento comunes: la escala más 0,005f, la
velocidad z menos 0,05f (`0x6b8a48`) y la luz por 0,97f (`0x6ba33c`); los otros subtipos del `0x6f2` van por
0,909f (`0x6ba26c`) y la z menos 2,5. Se dibuja con el billboard común.

**El humo `5f2/22`.** Nace (`0x5423e0`) con vida 60 y tres `rand()`: la escala ((`rand()` & 63) + 64) · escala ·
0,005f, el giro y la velocidad vertical (`+0x50`) ((`rand()` & 31) + 60) · 0,1f. En el paso (`0x546706`): la luz
de la vida, vida · 0,02f por 0,9f en el rojo y por 0,5 en el verde y el azul (el producto queda en la x87); la
velocidad vertical menos 0,1f, guardada con `fst` y usada sin redondear: la x más `fsin`(reloj) por ella (`fmul
st1`, un factor de 53 bits) por 0,05f, y la z más ella; la escala más 0,04f. Al dibujarse no está en las listas
que restan ni es el /6: el billboard común (`0x548a64`).

**El dibujo** (`0x4403b0`). El color del modelo (`+0x48..+0x50`) es el que le dejó `0x5ffce0` al posarlo:

| Tipo | Color | Cuerpo |
|---|---|---|
| `0x1b`, `0x36` | por sin(reloj · 0,0012f + `+0x84`) · 0,5f + 0,9f, cada canal guardado de su producto; después lo restaura | su `RenderBody(2, …)` y devuelve 1: no hay otro |
| `0x34` | por el mismo pulso con 0,0009f (`0x6b8bc8`), sin restaurarlo | su `RenderBody(2, …)`, y `0x442e10` devuelve 0: `0x6002e0` sigue y lo dibuja otra vez con ese color (Q-C317) |
| `0xa` | (0,56; 0,8; 0,81), sin restaurarlo | su `RenderBody(2, …)` y devuelve 1 |

Lo que no se restaura no pasa al objeto siguiente: `0x5ffdf0` vuelve a llamar a `0x5ffce0` con cada uno. Las
funciones que siguen en la cadena (`0x42f170`, `0x425d90` con `0x428df0` y `0x4306a0`) sólo tienen casos de
monstruos o de otros mapas.

**En MuRet.** El paso: `World::StepLandOfTrialsObject` (`WorldObjectStepServices`), desde `StepWorldObjectChecked`
en el 31; el juego le presta el reloj en ms como `timeGetTime()` (Q-C304) y la hora del ambiente en
`WorldRuntime::trialsAmbientTime`. El frame: `Scenes::StepLandOfTrialsObjectFrame`; el dibujo:
`Scenes::LandOfTrialsBody`, que `DrawGameWorldObjects` aplica al color del cuerpo en cada dibujo de MuRet (no saca
`rand()` ni cambia estado, D-C4) y, en el `0x34`, con un cuerpo más antes. Las dos partículas son la familia
`World::TrialsParticleKernels`, con el seno por un double de `Render::Models::Detail::SineDoubleProductPC53`. Con
la Data del repo, el 31 llega a un cuadro completo y camina (`GameT16SceneMatrix` y `GameT16MapWalk`, en (149, 104):
suelo libre con el 3, el `0x34` y el `0x35` vistos). Tests: `LandOfTrialsObjectsTest`,
`LandOfTrialsObjectServicesTest`, `TrialsParticlesTest` y
`WorldObjectsDrawingTest.TheLandOfTrialsBodiesPulseAndTheBrazierBaseIsDrawnTwice`.

### Los objetos de la Arena

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). La Arena es el mapa 6
(`World7`, 1.489 objetos, entre ellos el −515 de Q-C28). Su carga (`0x616ec0`) sube el especial `0xb1`
(`Object7\Bug`, de la fauna) con sus texturas.

**La cámara.** `0x635ae0` mira el nombre del héroe (`+0x1d9`) con `0x576960`: con "webzen" (la búsqueda de
"webzen2" no cambia nada, Q-C152) la cámara de espectador de [La cámara del juego](#la-cámara-del-juego), que
salta los dos accesos a la celda del héroe y sus `rand()` (`0x635b3d..0x635b9e`). El dibujo (`0x63726f..0x6372f1`)
vuelve a mirar el nombre y centra el trapecio en (6.400; 15.400) con la z del héroe, y `0x5fb570` lo mira dos
veces más: para las medidas propias (3.995 y 425) y para girarlo −yaw. Con cualquier otro nombre, todo es lo de
los demás mapas.

**El paso** (`0x604ab0`, el caso 6 en `0x6054fd`, sin tabla):

| Tipo | Qué hace |
|---|---|
| `0x15` | el blendMesh (`+0x64`) en 3 y la V (`+0x70`) en (−ftol(reloj) % 1.000) · 0,001f, como la U del 3 y el 4 del mapa 4 |
| `0x26` | `+0x58` en −2 |

**El frame** (`0x602ed0`, `0x6039cb`): el 9 deja un sprite `0x5ac` en el hueso 1 con la entrada de la pila sin
escribir, en NaN; es el caso al que cae Lost Tower ([Los objetos del Dungeon y de Lost
Tower](#los-objetos-del-dungeon-y-de-lost-tower), Q-C256). **El dibujo** (`0x6002e0`) no tiene casos del 6: todos
van por el `RenderBody` común.

**En MuRet.** El nombre: `Scenes::ActorNameContains`, el mismo `0x576960` del GM de `0x413fe0`. La cámara:
`GameScene::CameraOfTheFrame` le pasa el resultado a `World::StepGameCamera` y no lee la celda del héroe en esa
rama; `GameScene::Render` usa el punto fijo, y `PolygonOfTheFrame` prende las dos búsquedas de `0x5fb570`. El
paso: `World::StepArenaObject` (`DungeonObjectStep`), desde `StepWorldObjectChecked` en el 6; el frame:
`Scenes::StepArenaObjectFrame`. Con la Data del repo, el 6 llega a un cuadro completo y camina
(`GameT16SceneMatrix` y `GameT16MapWalk`, en (62, 47): suelo libre con once `0x15` y cuatro 9 a menos de 1.500;
`GameT16SafeZoneWalk`, en (70, 139)). Tests: `DungeonObjectsTest.TheArenaScrollsTheVOfItsWaterAndHidesItsMarkers` y
`GameT16Journey.AWebzenNameWatchesTheArenaFromTheSpectatorCamera`.

### Los objetos de Atlans

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). Atlans es el mapa 7
(`World8`, 5.205 objetos: 845 `0x16`, 99 `0x20`, 23 `0x22`, 85 `0x28`). Su carga (`0x616ec0`) empieza, como en
todos los mapas, por `Object8\drop01.jpg` en `0x607` (`0x616eeb..0x616f03`, `GL_NEAREST` y `GL_CLAMP`, requerida);
en su caso sube los peces `0xb3..0xbb` de la fauna y las 32 cáusticas `Object8\wt00..wt31.jpg` en `0x41..0x60`
(`0x617741..0x6177ee`, `GL_LINEAR` y `GL_REPEAT`, sin aviso).

**El paso** (`0x604ab0`, la tabla `0x605c94` por el tipo − `0x16`, índices en `0x605cac`), con el reloj del mundo T
y la x87 en 53 bits:

| Tipo | Qué hace |
|---|---|
| `0x16` | `+0x58` en −2; la fase (`+0x84`) más 0,1f con `fst`: si la suma sin redondear pasa de 10, `+0x84` vuelve a 0 (el `EDX` en 0 de `0x604d50`); con la fase guardada por encima de 5, una burbuja `0x607/0` en `+0x10` con `+0x1c`, la luz `+0xf4`, escala 1 y sin dueño (`0x6055c8`) |
| `0x17` | el blendMesh (`+0x64`) en 0 y la luz (`+0x68`) en sin(T · 0,002f) · 0,3f + 0,5 |
| `0x20`, `0x22` | el blendMesh en 1 y la luz en (sin(T · 0,004f) + 1) · 0,5 |
| `0x26` | el blendMesh en 0 |
| `0x28` | el blendMesh en 0, la velocidad de la animación (`+0xd8`) en 0,05f (antes del `fsin`) y la luz en sin(T · 0,004f) · 0,3f + 0,5 |

Como la comparación con 10 lee la suma sin redondear, 9,9f + 0,1f queda debajo de 10 y se guarda como 10,0f: esa
fase no vuelve a 0 y sopla; la del paso siguiente sí vuelve.

**El frame y el dibujo.** `0x602ed0` manda el 7 a la cadena de `0x6044ed`, cuyas siete funciones miran otros mapas
(18 a 23, 24 a 29 y 36, 30, 31, 35, 33 y 54, 34): nada. `0x6002e0` mira el 7 solo en los ayudantes (la sombra del
caballo oscuro y otros dos, con alfa 0,2 en vez de 0,7): los objetos del mundo van por el `RenderBody` común.

**El agua** (`0x5fa830`, `0x5fa973..0x5fa9b1`). Después del pase de abajo, con alguna esquina en alfa por encima
de 0, el mapa 7 mira la capa 2 (`0x7aacc8c`) de la primera esquina: con 5, en vez del pase de alfa de la capa 2,
`0x5fa5c0(0x1e + DAT_07d2d360, x, y, 0, 1)` (16 en vez de 64 por el ancho y el alto de la textura, sin corrimiento
ni olas) y `0x5fa4b0`: la mezcla aditiva (`0x620a90`), el slot del tile + `0x23` (`0x41` + el cuadro), un
`GL_TRIANGLE_FAN` con `glColor3f` del alfa de cada esquina, sin la luz, y vuelve. `0x7d2d35e`, que lo saltearía,
sólo se lee y queda en el 0 de la .bss. El pase del pasto no corre en el 7 (`0x5fc33e`).

**Los personajes** (`0x505de0`, `0x506d0a..0x506d7c`). En la escena 5, con `ftol(T) % 10000` debajo de 1000 (un
segundo de cada diez), el `0x20a` saca una burbuja `0x607/0` de la cabeza: `0x4ec110` con el hueso `+0x54` del
modelo (20 en Player.bmd) de los huesos propios (`+0x120`), (0, 20, −10), la escala y la posición que `0x505ec6`
copió del personaje, y `0x53fe70(0x607, punto, +0x1c, (1, 1, 1), 0, 1, 0)`. Los huesos propios son los del último
dibujo; antes del primero, el `operator new` sin iniciar de `0x511290` (Q-C318). En Atlans el personaje de pie
nada (la acción de vuelo de `0x4f2250`, que mira la celda del `TerrainWall` vivo).

**En MuRet.** El paso: `World::StepAtlansObject` (`AtlansObjectStep`), desde `StepWorldObjectChecked` en el 7; el
frame y el dibujo, lo común. El agua: `World::DrawTerrainCell` con `TerrainView::waterFrame` (el
`ClothWorld::waterTextureFrame` del frame) y `GameScene::DrawTheWorldPass` sin el pasto en el 7; las texturas,
`World::DropTexture` y `World::CausticTextures`. La burbuja: `CharacterStep` con `ActorState::bodyPose`, que el
dibujo del juego escribe en cada frame del original (`HangTheItems`) y `ConstructActor` vacía; vacía, la matriz en
cero. Con la Data del repo, el 7 llega a un cuadro completo y camina (`GameT16SceneMatrix`; `GameT16MapWalk`, en
(226, 103): suelo libre con cinco `0x16`, siete `0x20` y cuatro `0x22` a menos de 1.500 y agua en la capa 2 cerca;
`GameT16SafeZoneWalk`, en (13, 17)). Tests: `AtlansObjectsTest`, `TerrainCellsTest.AtlansDrawsItsCausticWaterInsteadOfTheAlphaLayer`,
`GameSceneDrawTest.AtlansDrawsItsCausticWaterAndNoGrass` y `CharacterStepTest.AtlansBlowsABubbleFromTheHeadOneSecondInTen`.

### Los objetos de Devil Square y de Blood Castle

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). Devil Square es el
mapa 9 (`World10`, 564 objetos: 200 del 2, 191 del 3; el 32 carga el 9, `0x61a560`) y los Blood Castle son los
mapas 11 a 17, todos con `World12` (210 objetos: once `0xb`, cuatro `0xd`, nueve `0x25`, dos del 9 y dos del 10,
tres `0x1c` y uno `0x1d`).

**El objeto de evento** (`0x447d70(mapa)`, después de `0x61a560`, desde `0x4c3940` y `0x4c6e10`). Con el
argumento entre 11 y 17, suelta el anterior (`0x447eb0`, el destructor virtual con 1) y pide `operator
new(0x134)`: `+0x0c` en 0, el byte `+0x10` en 0, `+0x14..+0x20` en −1, `+0x24` y `+0x28` en 0, los 0x42 dwords de
`+0x2c` en 0 y la vtable `0x6b8c30`. Si no, mira **el mundo en curso** (`0x6c9dd0`, no el argumento) entre 18 y 23:
lo mismo por `0x404d60`, con la vtable `0x6b8794` y `0x6c634c` (el área de Chaos Castle, `.data` `0xff`) otra vez
en `0xff`. Si no, con el argumento en 9, lo mismo con la vtable `0x6b8c1c`. En los demás mapas no hace nada: el
objeto del evento anterior sigue vivo en `0x723c60` (Q-C320). `+0x04` y `+0x08` no los escribe nadie. Sus métodos
los llaman el HUD (`0x5a7320` y `0x5ab6d0`, con `0x40bab0`) y los paquetes (`0x4e2b10` con `0x92` y `0x9b`,
`0x4da730`, `0x4da750`, `0x4dac70`, `0x4e7360`, `0x4c2480`, `0x4d07c0`); ninguno corre en el paso ni en el dibujo
del mundo.

**El preludio de Devil Square** (`0x604ab0`, `0x604ac0..0x604b9b`), una vez por pase y antes de los objetos:
con `ftol(T) % 4.000` (con signo) debajo de 1.000 saca `rand() % 100`; con 0, la luz dinámica de radio 12
(`0x5f9780`) con L = (`rand() % 12` + 4) · 0,1f y el color (L · 0,2f; L · 0,3f; L · 0,5) en el héroe corrido
`rand() % 1.200` − 600, **la y antes que la x**, con z 0. Fuera de la ventana no saca `rand()`. Después, con cada
objeto del pase, `0x41ed10(1, 0, 1)`: el ambiente 1 en bucle. El caso 9 del `switch` (`0x6054f6`) no tiene tipos.

**El frame de Devil Square** (`0x602ed0`, `0x603df9..0x603ee0`): el 2 saca dos `rand() % 4`; con 0, una gota
`0x68/0` (`World10\rain03.tga`) en el hueso 23 y después en el 31 con (−15, 0, 0), sus ángulos, luz 1 y escala 1;
y siempre una más en el 23. **El dibujo** (`0x6002e0`) no tiene casos del 9.

**Los personajes de Devil Square** (`0x505de0`, `0x506d7f..0x506e54`): en cualquier escena, el `0x20a` saca dos
`rand() & 3`; con 0, una `0x68/0` de `0x4ec110` con el hueso 26 (`+0x4e0` de los huesos propios) y después con el
35 (`+0x690`), (−15, 0, 0), la escala y la posición que `0x505ec6` copió, sus ángulos y luz 1. Los huesos propios
son los del último dibujo, como la burbuja de Atlans (Q-C318).

**El paso de los Blood Castle** (`0x605991`, sin tabla): el 9 y el 10 con `+0x8a` distinto de 4 ponen `+0x58` en
−2 (la puerta y su marco cerrados no se dibujan); el 4 lo pone el paquete que abre el castillo (CL9.5). **El
preludio** (`0x6061c0`, `0x6062e6..0x6063c3`), una vez por pase, antes del early-return de los objetos vistos: un
punto en (`rand() % 900` − 300; `rand() % 900` − 300; `rand() % 50` + 250) desde el héroe, la x primero, y con
`rand() & 3` en 0 una órbita `0x60f/3` ahí (`0x53fe70`, ángulos en 0, luz 1, escala 0,19f, `Effect\Flare.jpg`).

**El frame de los Blood Castle** (`0x602ed0`, `0x604171..0x6043e9`), con el reloj del mundo T y la x87 en 53 bits:

| Tipo | Qué hace |
|---|---|
| `0xb` | la luz L = sin((`+0x24` · 20 + T) · 0,001f) · 0,5 en (L; L · 0,5; 0) y siete sprites `0x5ac` de escala 0,5 en los huesos 1, 2, 4, 6, 9, 10 y 11 con (0, 0, 2) |
| `0xd` | un sprite `0x60f` en el hueso 3 con L = sin(T · 0,001f) · 0,3f + 0,7f, escala L + 0,5 y luz (L; L; L) |
| `0x25` | la antorcha: `rand() % 2`; con 0, `ftol(+0x84 + 2) % 4` y `+0x84` más 1: con 0, la llama `6ac/0` y la chispa `6ab/0`. Después otro `rand() % 2`; con 0, `ftol(+0x84) % 4` y `+0x84` más 1: con 0, el humo `622/6`, la chispa `6ab/1` y la órbita `60f/4` con luz (1; 0,8f; 0,8f) y escala 0,19f |

Las partículas de la antorcha (`0x53fe70`, `0x5446e0`, `0x548550`):

| Partícula | Alta | Paso | Dibujo |
|---|---|---|---|
| `6ab/0`, `6ab/1` (`Effect\fi01.jpg`, `0x61d55e`) | `0x5436fe`: vida `rand() % 5` + 20, giro 0; escala (`rand() % 10`) · 0,02f + 0,5 (`/0`) o · 0,1f + 1 (`/1`); velocidad (`rand() % 10` + 5; `rand() % 10` − 5; `rand() % 10` + 5) por (0,4f; 0,4f; 0,2f) (`/0`) o (0,2f; 0,2f; 0,1f) (`/1`) | `0x5477e4`: la posición más la velocidad, la x y la y por 0,95f; la luz en vida · 0,1f, la z +0,3f y la escala +0,07f (`/0`) o +0,09f | aditiva, el default `0x54889a` |
| `6ac/0` (`Effect\fi02.tga`, `0x61d57a`) | `0x543a5d`: vida `rand() % 5` + 25, escala 0,5, giro `rand() % 360` y la velocidad de adelante; después el caso del `6ec` (`0x543ae3`): vida 24, otra velocidad x (`rand() % 10` − 5) · 0,4f y z, y el inicio en la posición (`0x543e7d`) | `0x54793f`: lo mismo con la z +0,6f, la posición corrida (`rand() & 3`) − 2 en x e y y por 0,8f en z, la escala +0,05f, la luz en vida · 0,04f y el giro + (`rand() & 1`) + 1 | `0x620c10` (el modo 6: `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`), `0x5489eb`; el `/2` no está leído |
| `622/6` (`Effect\clouds.jpg`) | `0x54327b`: vida `rand() % 5` + 25, escala 0,2f, giro `rand() % 360` y la velocidad de la `6ac` | `0x54748a`: el de la `6ac` con la luz en vida · 0,02f | el `/6` de la tabla `0x548df8` va a `0x548c32` y, sin ser el 8 ni el 9, al default `0x54889a` |
| `60f/4` | la órbita de `60f/3` con vida 40 | la de `60f/3` con la subida en 5, la escala −0,004f y el apagado desde vida 30 | `0x548bcf` |

**El dibujo de los Blood Castle** (`0x6002e0`, `0x600cf2..0x600db0`): el `0x1c` y el `0x1d`, después de su
`RenderBody`, la sombra (`0x4eff40(blendMesh, 2, −1, −1)`) de las mallas con triángulos salvo la 2 y las de la
textura del blendMesh, con `0x620a00(1)` y `glColor4f(0, 0, 0, 1)`, aplastada sobre la altura del terreno bajo el
objeto (`0x5f93c0`, `0x600d80`); después `+0x58` vuelve a −1.

**En MuRet.** El objeto de evento: `Scenes::ArmEventMapObject` sobre `WorldRuntime::eventObject` y
`WorldRuntime::chaosCastleArea`, desde `WorldEntry` y `WorldTransitions`; los métodos son de la CL8 y la CL9.5.
Devil Square: `World::StepDevilSquarePrelude` (`EventMapObjectStep`), `Scenes::StepDevilSquareObjectFrame`
(`EventMapObjectFrame`) y la lluvia de los personajes en `CharacterStep`. Los Blood Castle:
`World::StepBloodCastleObject` desde `StepWorldObjectChecked`, `World::StepBloodCastlePrelude` desde
`GameScene::StepTheObjects`, `Scenes::StepBloodCastleObjectFrame`, `Scenes::DrawBloodCastleShadow`
(`CharacterDrawing`) con `GameObjectPass::terrainHeight`, las familias de `World::BloodCastleParticleKernels` y el
modo 6 en `IParticleDrawingServices::Blended` y `ModelBatchParticleBackend`. Las texturas, `EffectTextureSet`. Con
la Data del repo, el 9 (y el 32) y los siete Blood Castle llegan a un cuadro completo y caminan (`GameT16SceneMatrix`;
`GameT16MapWalk`, en (122, 174) con 38 del 2 cerca y en (12, 90) con cuatro `0xd` y cuatro `0x25`;
`GameT16SafeZoneWalk`, en (12, 3) de los Blood Castle; Devil Square no tiene zona segura). Tests:
`MapLoadEventTest`, `EventMapObjectsTest`, `EventMapObjectFrameTest`, `BloodCastleParticlesTest` y
`CharacterStepTest.DevilSquareRainsFromTwoBonesOfEachCharacter`.

### Los objetos de Chaos Castle

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). Los seis Chaos Castle
son los mapas 18 a 23, todos con `World19` (194 objetos: 22 del 3, nueve del 6, 14 del 7, seis del 8, ocho del
`0xc` y los demás repartidos del 0 al `0x23`). El castillo, de 23 a 44 en x y de 75 a 108 en y, es todo zona segura
en el `.att`; afuera, la franja de x 1 a 12 con y 70 a 111 se camina y no es segura. La entrada por `1C` corta en
la UI (`0x4906a0`, CL8); la de `F3 04` entra. El objeto de evento (`0x404d60`) y el área (`0x6c634c`, `0xff` en la
`.data`) están en [Los objetos de Devil Square y de Blood Castle](#los-objetos-de-devil-square-y-de-blood-castle).

**Las celdas** (`0x405e00(x, y)`, en el dibujo del terreno, por cada celda dibujada que no está escondida): `rand()
% 8`; con 0 y el área en 0, 3 o 6, el primer rectángulo del área que tiene la celda (`0x6b86d4`, `0x6b8714` y
`0x6b8754`, cuatro cada uno, con los bordes) suelta la piedra `0x5f6/0` en (x · 100 + `rand() % 30` − 15; y · 100 +
`rand() % 30` − 15; z del héroe), sin dueño, escala 1,5, y con `rand() % 5` en 0 el temblor de la cámara
(`0x7d325c8`) en (`rand() % 3` − 3) · 0,1f. Sin paquetes el área queda en `0xff`: sólo sale el `rand() % 8`.

**El preludio** (`0x405490`, primero en `0x6061c0`), con la cuenta de los 3 vistos del pase anterior (`0x7d365a0`):
con `rand() % 10` en 0 y la cuenta distinta de 0, el selector es `rand() % cuenta`. Si no, con el héroe fuera de la
zona segura y otro `rand() % 10` en 0, **el meteoro** `0x6ee/9` (`0x530860`) en (x + `rand() % 800` − 400; y +
`rand() % 800` − 400; z − 150) del héroe, con sus ángulos, sin dueño y escala `rand() % 10` + 50. **Cada visto**
(`0x4055b0`, después de `0x604ab0`): el 3 suma 1 a la cuenta; con la trampa armada (`0x6c634d`, 1 en la `.data`,
que sólo rearma el paquete de `0x404e20`) prende `+0x8a`, deja `+0x60` en 10 y la desarma; si no, con el selector
distinto de 0 lo baja, y al llegar a 0 prende ese.

**El frame** (`0x4059d0`, desde `0x602ed0`):

| Tipo | Qué hace |
|---|---|
| 0 a 3 con `+0x8a` y sin esconder | **la trampa** (`0x405cc5..0x405d82`): el punto del hueso 1 de la pose global; con `+0x60` en 10, el joint `0x615/2` de ese punto a ese punto, con los ángulos del objeto y escala `rand() % 10` + 60, y el sonido `0x9c` + (`rand() & 1`). Después, `+0x60` por debajo de 5 apaga `+0x8a`; si no, baja uno. Sigue con el área |
| 0 a 5 y `0xd` a `0x11` | con el área entre 2 y `0xfe`, se esconde (`+0x58` en −2) |
| 6 a `0xc` | **las nubes**, una vez (después queda escondido): diez `622/(tipo − 6)` del 6 al 8, cinco del 9 al `0xb` y siete `622/7` el `0xc`, con el objeto de dueño, su escala y la luz (0,05f; 0,05f; 0,1f), o 0,3f el `0xc` |
| `0x12` a `0x15` | se ve sólo con el área en 7 u 8 |
| `0x18` a `0x1d` | se ve con el área en 4 o 5 y se esconde desde la 8 |
| `0x1e` a `0x23` | se ve con el área en 1 o 2 y se esconde desde la 5 |

**Los joints** (`0x535840`): el rayo de la trampa, `0x615/2`, se crea (`0x5319ea`, `0x531b0c..0x531bd2` y `0x534cb6`) con la textura `0x614`, el ancho
en la escala, 50 puntos, vida 20, x e y más `rand() % 10` − 5 y z + 800, y el destino en el segundo punto. Su paso
(`0x53770a`) baja en zigzag hasta el destino, con `rand() % 20` − 10 en x e y y z − 16 en cada punto; al final se
apaga (vida por debajo de 4, la luz por 0,8333f), deja dos sprites `0x5fd` en el destino y la partícula `6ed/0`, y la cola corre con vida por encima de 10. El meteoro, `0x6ee/9` (`Effect\JointSpirit02.jpg`, cargado en `0x61d5db`), va con la mezcla que resta (3) y sólo la segunda cara,
con velocidad 10 y ángulos (−90, 0, 0); su paso (`0x538ea4..0x538f37`) corre x e y con `rand() % 10` − 5, sube la velocidad y
se apaga desde vida 10. La cola común (`0x53c393`) usa la matriz del principio del paso.

**Los personajes.** En Chaos Castle un jugador (`+0x88` en 1) no lleva sus partes sino el Angel (`0x1f7`,
`Player\Angel`, que carga `0x618970`): `0x60cf70` con su contorno de estos mapas y `0x60b3d0` con el nivel 0, que
le pone la luz del personaje y llama a `0x609640` con 2, o 6 el héroe, que además lleva el cromo de la malla 0
(`0x1040` a (0,75; 0,55; 0,5) y `0x46` a 1). Su malla de piel va con la piel 0 (Q-C322). El contorno de estos
mapas (`0x60d09f` y `0x60d113`) vale para cualquier `+0x88`: (0,1; 0,01; 0) a 0,1 / escala + 1 y (0,7; 0,07; 0) a
0,04 / escala + 1,02 − 0,02 (Q-C323). No hay marca del gremio (`0x50d4fb`), capa, alas, mascota ni armas en la
espalda. El alpha de los objetos baja 0,15f con `+0x1a1` del héroe (`0x5ffdf0`), que sólo escriben los paquetes.
Al caminar, `0x4fbe10` guarda la celda del héroe y, si se queda 10 s en una celda con el bit 8 del atributo, manda
`C1 06 AF 01` (CL9.5, corta).

**En MuRet.** `World::StepChaosCastleCell` desde el dibujo del terreno, `World::StepChaosCastlePrelude` y
`World::StepChaosCastleSeenObject` (`World/ChaosCastle`) desde `GameScene::StepTheObjects`,
`Scenes::StepChaosCastleObjectFrame` (`EventMapObjectFrame`), los joints `0x615/2` y `0x6ee/9` en
`FaunaJointCreation`, `World::ChaosCastlePoseAlpha` y `Scenes::DrawChaosCastleBody` (`CharacterDrawing`, desde
`GameCharacters`). Con la Data del repo, los seis entran por `F3 04` dentro del castillo y caminan; la trampa armada
se dispara en el primer 3 visto (`GameT16ChaosCastleWalk`). Tests: `ChaosCastleTest` y, del cuerpo,
`GameCharacterRenderTest.InChaosCastleAPlayerWearsTheAngelAndOnlyTheHeroAddsItsChrome` y
`TheChaosCastleOutlineScalesWithTheObjectAndHasItsOwnColors`.

### Los objetos de Castle Siege

Leído en la CL7 en el ASM de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el port y la Data del
repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). Castle Siege es el mapa 30, con `World31`
(`EncTerrain31.obj`: 3.724 objetos, entre ellos 39 del 0, 37 del `0x34`, 52 del `0x35`, 10 del `0x36`, 42 del
`0x2a`, 24 del `0x29`, 13 del `0xc`, 26 del `0x11`, cuatro `0x13`, cuatro `0x33`, uno `0x42`, del `0x3d` al `0x40`
21 puertas y uno 79). Todo depende de **un byte**, `0x7239ec`, que lee `0x4294b0` y que solo escribe `0x4294c0`
desde los paquetes del asedio (CL9.5): arranca en 0 y, sin esos paquetes, queda en 0 (Q-C330). MuRet porta el mapa
con el asedio apagado y corta, con la dirección, cada rama del asedio prendido.

**La carga** (`0x42a760`, desde `0x61a560` antes del pasto): `0x7239f0`, la hora de la música, en 0
(`0x42a795`); el `B9 02` (C1 con Xor32, sin C3) con el socket del GS abierto (`0x42a981`); `Monster78` en el modelo
`0x19f` (`0x612d90(0x4d)`, `0x42aa6b`: siete velocidades y la acción 6 sin repetir); y cinco lanceros `0x19f` con
`0x606660` en (6.500; 7.100; 9.100; 11.800; 12.300) × 11.300, z 270, ángulos 0 y escala 1, **antes** de los objetos
del `.obj`. Con `0x7239ec` en 0, `0x61a6e1` y `0x61a8a8` eligen el `.att` y la luz de siempre (`EncTerrain31.att` y
`TerrainLight.jpg`); prendido, el `.att` 312 y `TerrainLight2`. Las texturas: `Object31\Flag.tga` en `0x69c`
(`0x61e4b3`, `GL_LINEAR` y `GL_CLAMP`) y `Skill\TowerGatePlane.bmd` (`0x616880`), que `0x616d8e` y `0x616e20` piden
dos veces (Q-C333).

**La creación** (`0x42c6b0`): lo de [Creación común de los objetos](#creación-común-de-los-objetos), y el 79 pide
el efecto `0x135` con el objeto de dueño (`0x518cf0`, `0x51d73b`: escala 0,9, vida 100, la luz de las mallas en 0,3f,
sin `rand()`). Al final, `0x42b480` con el byte del asedio: el 4, el `0xb`, el `0x25`, el `0x29`, del `0x2b` al
`0x30`, del `0x43` al `0x45`, del `0x47` al `0x4c` y el `0x58` quedan con `+0x58` en −1 si el asedio está prendido
y en −2 si no; el `0x10`, del `0x26` al `0x28`, el `0x32`, el `0x3a`, el `0x3b`, el `0x4e` y del `0x50` al `0x57`, al
revés. `+0x4` queda en el 0 de `0x606660`.

**El preludio** (`0x42b540`, el tercer eslabón de `0x6061c0`, una vez por paso y antes de mirar si hay objetos
vistos): con `timeGetTime()` en T, si (T − `0x7239f0`) · 0,001 pasa de 1.800 o `0x7239f0` es 0, guarda T en float y
elige la música (sin asedio, `castle.mp3`, `0x42b5c3`); después, siempre, el sonido `0x1e2`. Sin asedio termina ahí,
sin `rand()`; con él siguen los sonidos `0x1e3` a `0x1e7` y las piedras sobre el héroe (`0x42b5f8`).

**El paso** (`0x42c850`, el segundo eslabón de `0x605a62`), con cada objeto visto. Sin asedio: el 0, una luz
dinámica de radio 3 con L = ((`rand()` & 3) + 3) · 0,1f y el color (L; L · 0,6f; L · 0,2f), y `+0x58` en −2; el 4,
`+0x58` en −1 y la U en T · 0,0001f; el `0x2a`, el `0x34`, el `0x35`, el `0x36` y el `0x42`, `+0x58` en −2; el
`0x19f`, en −1. Con el asedio, el 0 queda en −2 sin luz, el `0x35` hace la luz del 0, el `0x19f` queda en −2 y el
`0x42` lee `0x7239e4` (`0x42c977`). Como `0x606560` solo mira lo que vio el último dibujo, los que quedan en −2 se
dibujan **una vez**, la primera que se ven con el −1 de la creación (Q-C335). **El paso de los vistos** (`0x42c500`,
desde `0x6063d0`): el `0x51` y el `0x53` con el blendMesh 1, su luz en 1 y la V en T · 0,0002f o T · −0,0004f; las
puertas (`0x3d` a `0x40`) con el asedio, `0x42c596`; después, siempre, `0x42b480`.

**El frame** (`0x42c9f0`, desde `0x602ed0` después del `rand()` común, con cada objeto visto, también los que
están en −2). Sin asedio: el 0, con `rand() % 3` en 0, la chispa `6ec/0`; el `0x29`, la luz del blendMesh (`+0x68`)
en sin(T · 0,002f) · 0,3f + 0,7f; el `0x34`, el `6f4/6` sin `rand()`; el `0x36`, la salpicadura `0x6f2` +
`rand() % 2` con el subtipo 0, escala 1 y luz 1. Con el asedio: el `0x2a`, con `rand() % 3` en 0, el humo `5f2/0x15`;
el `0x35`, lo del 0 sin asedio; el 0, el `0x34` y el `0x36`, nada.

**El dibujo** (`0x42cc10`, la cadena de `0x6002e0`): con un caso, `0x6002e0` no hace su `RenderBody`.

| Tipo | Qué dibuja |
|---|---|
| `0xc`, la bandera | el color del modelo en 1 y dos `0x4ecc30` con las banderas 2: la malla 0 con su textura y la 1 con `Flag.tga` (`0x69c`) |
| `0x11` | el color en 0,45 (`0x3ee66666`), model+`0x88` en 0 y `RenderBody(2)` con la V en (`ftol(T)` % 10.000) · 0,0002f |
| `0x33` | model+`0x88` en 0 y `RenderBody(0x42)` con la U en (−`ftol(T)` % 10.000) · 0,0002f (el resto con el signo del dividendo) |
| `0x42` | el color en (0,1; 0,4; 0,6) y `RenderBody(0x44)` con la luz en sin(T · 0,001f) · 0,2f + 0,3f |
| `0x13` | sin asedio, `RenderBody(2)` y dos pases con `Chrome01` y el blendMesh en 0: `0x44` en 0,3 y `0x48` en (0,2; 0,2; 0,5); con el asedio, `0x4ecc30` sobre la malla 3 (`0x42ce09`) |
| `0x3d` a `0x40` | con `+0x4` en 0, `RenderBody(2)`; si no, `0x4ee770(0, 0x12f, 0, 0, 0)` (`0x42cfcb`) |
| `0x32`, `0x53` a `0x55` | `RenderBody(2)` y, sin asedio, la sombra negra de `0x4eff40(+0x64, +0x58, −1, −1)` |

Los demás tipos van a `0x42e3d0`, que solo tiene casos de monstruos. **La sombra del 30** (`0x4eff40`,
`0x4eff8d..0x4eff9f`) se abre con 2.500 en lugar de 2.000, también la de los personajes y la del caballo
(Q-C337). El efecto `0x135` (`0x52973e`) vuelve a vida 100 en cada paso y, con dueño, su z es (sen(T · 0,001f) + 1)
· 200 sobre la de la partida; se dibuja dos veces, la segunda con la z en la partida + 400 menos esa altura,
y la deja así (`0x52d86a..0x52d89f`).

**Lo demás del 30.** La niebla: con el héroe en x de 16.100 a 19.000 y en y de 18.900 a 21.700 (inclusivos),
`0x63741d` llama a `0x42b0a0`, que prende la niebla lineal negra de 2.000 a 2.700; afuera, `glDisable(GL_FOG)`
(Q-C334). Los personajes: `0x412140` arma, con `0x6c649a` en `0xff` (sin gremio, Q-C331), el panel de la clase 0 en
`0x5763530`, que dibuja la interfaz (CL8); con otro puesto, los paneles 1 y 2 (`0x412193`). Después de cada `0x20a`,
`0x42afe0` llama a `0x42ddb0(personaje, 380)` solo con el asedio (`0x42b00b`). Las capas del 6 al 13 de lo que cuelga
(`0x4142f0`) solo se dibujan con el asedio (`0x414330`).

**En MuRet.** La carga: `Scenes::LoadMap` (`PrepareTheCastleSiege`) con `MapLoadContext::requestCastleSiegeState`,
que `WorldEntry`, `WorldTransitionSupport` y `SessionEventApplier` atan a `App::ServerSession::RequestCastleSiegeState`
(`Protocol::WriteCastleSiegeState`); `WorldModels::LoadTheSpearman`; `GameTailTextureSet` y `GameModels`
(`TowerGatePlane`). El paso: `World::StepCastleSiegePrelude`, `World::StepCastleSiegeObject` y
`World::StepCastleSiegeSeenObject` (`CastleSiegeObjectStep`), con `World::ApplyCastleSiegeObjectState`. El frame:
`Scenes::StepCastleSiegeObjectFrame` (`CastleSiegeObjectServices`). El dibujo: `Scenes::DrawCastleSiegeBody`
(`CharacterDrawing`) desde `DrawGameWorldObjects`, y `Render::Models::ShadowSpreadOf`. El efecto: `StepGatePlane` y
`ModelEffects::CreateGatePlane`. La niebla: `World::InTheCastleSiegeFog`, que adentro corta en `0x637481` (el
renderer no tiene niebla). El panel: `WorldRuntime::siegePanelKind` y `heroGuildPost`. Cortan, además de las ramas
del asedio prendido, las partículas que MuRet no crea: la chispa `6ec/0` (`0x42cb52`), el `6f4/6` del `0x34`
(`0x42caf4`) y el humo `5f2/0x15` (`0x42caaa`) ([08](08-pendientes.md) P-C53). Con la Data del repo, el 30 llega a
un cuadro completo y camina (`GameT16SceneMatrix`; `GameT16MapWalk`, de (92, 98) a (95, 101), con banderas `0xc` y
lanceros a la vista, lejos de las antorchas, de los braseros y de la niebla). Tests: `CastleSiegeObjectsTest`, `CastleSiegeObjectServicesTest`,
`CastleSiegeDrawingTest`, `MapLoadTest.CastleSiege*`, `WorldProtocolTest.CastleSiegeStateWritesTwoWithXor32AndWithoutC3`
y `GameT16Journey.CastleSiegeAsksForItsStateBeforeConfirmingTheTeleport`.

### Los objetos de Crywolf

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). Crywolf es el mapa 34
(`World35`, 6.644 objetos). Sus tres casos los abre `0x4303c0` (el mapa en `0x22`) en las tres cadenas: el paso
`0x4303e0` (el último de `0x605a62`, después de `0x438a90`, `0x42c850`, `0x43ffc0`, `0x42ee40` y `0x4250f0`, que en
el 34 no hacen nada), el frame `0x430510` (en `0x602ed0`, por `0x6044ed`) y el dibujo `0x4306a0` (el último de
`0x6002e0`; su default sigue a `0x4339d0`, que solo despacha `0x171..0x1b4`, monstruos de la CL9.1).

**El estado del evento.** `0x723c04` (un byte de la .bss, en 0 al arrancar) lo escribe solo el paquete de
`0x42fe80` (CL9.5), que al cambiarlo vuelve a cargar la luz y el `.att`; lo vuelve a 0 `0x42fdc0` (`0x42fe32`),
desde la vuelta a la selección y desde el paquete con el segundo byte en 6. El cliente lo pide con el `BD 00` al
entrar al 34 ([04](04-protocolo.md#el-clima-y-crywolf-0f-y-bd)), después de la carga. Sin paquetes queda en 0, y
MuRet porta ese estado (Q-C340). Lo leen la carga y el dibujo:

- **La carga** (`0x61a6d0`, `0x42fe50` después del `.map`): con 0, `EncTerrain35.att` y `TerrainLight` como los
  demás mapas; con 1 y 2, `EncTerrain351.att`/`EncTerrain352.att` y `TerrainLight1`/`TerrainLight2`; con 3 o más,
  el `.att` con la ruta del `.map` y la luz de Q-C181. MuRet carga el 0 y corta los otros en `0x61a6f4`.
- **El dibujo**: los sorteos del `0x38` solo con 0; el humo del `0x54` y del `0x4a`, con 1; el `0x51`, con 2.

`0x723c05` y `0x723c08`, que también vuelven a 0 en `0x42fdc0`, solo los escriben paquetes: eligen la música
(`0x430250`, CL10), lo que dibuja `0x430180` (CL8), ramas de los personajes (`0x431470`, desde `0x505de0`) y el
color del `0x51` con el estado en 2. Con 0, ninguna de esas ramas cambia el mundo.

**El paso** (`0x4303e0`), con un `switch` por tipo de `0x29` a `0x54` (la tabla `0x4304c8` con los índices de
`0x4304dc`). Devuelve 1 siempre que el mapa sea el 34:

| Tipo | Qué hace |
|---|---|
| `0x29` | (0,2f; 0,7f; 0,5f) a la luz dinámica `0x7b4ccc4` con radio 2 (`0x5f9780`, `0x43043e`) |
| `0x39`, `0x47` | ((`rand()` & 3) + 3) · 0,1f en la x87 (`fst` del rojo), por 0,6f (`0x6b8a20`) y 0,2f (`0x6b8854`), con radio 3 (`0x430497`) |
| `0x51` | el alpha (`+0x174`) en 0,2f (`0x4304a6`) |
| `0x52`, `0x53`, `0x54` | `+0x58` en −2 (`0x4304b7`) |

**El frame** (`0x430510`), después del `rand()` común de `0x602ef4`, que no usa. Cada objeto del 34 suma 1 a
`0x723c0c` y, pasados 70.000 (`0x11170`), un `rand()` la vuelve a 0 y deja **el clima** (`0x5763358`) en
`rand()` % 3. El clima lo leen las hojas (`0x5304f6` y el `0x66` del 34, que solo se dibuja con el clima en 1,
`0x5307b2`) y el `0x49` del dibujo. Después:

- El `0x29`: el giro del brillo (`0x723c10`) más 1 (`0x6b87d4`); `fst` lo guarda y `fcomp` compara con 360
  (`0x6b869c`) la suma que quedó en la x87, y en 360 o más vuelve a 0 (un NaN no). Dos sprites `0x60f` de 1,3
  (`0x3fa66666`) en blanco con el objeto de dueño (`0x5491e0`): el primero sin giro y el segundo con el del brillo,
  en (x, y) del objeto y la z en `fild` de la cuenta de `0x6046a0` (`0x5763538`, o 40 menos ella pasados 20) más
  350 (`0x6b89ac`) más la z del objeto, en la x87 hasta el `fstp`. La cuenta da la vuelta de 1 a 41 en cada
  dibujo de los objetos (`0x6046be..0x6046ce`), en el login, la selección y el juego (Q-C341): los sprites suben y
  bajan de 349 a 370 sobre el objeto. El giro y la cuenta del clima suben con cada objeto, no con cada frame (Q-C348).
- El `0x54`: un `rand()` y, en 0 módulo 3 con el estado en 1, `0x53fe70(0x5f2, posición, ángulos, (1, 1, 1), 21,
  escala, 0)`.

**El dibujo** (`0x4306a0`), con un frame del original para los sorteos, que van antes del `rand()` común:

| Tipo | Cuerpo | Además |
|---|---|---|
| `0x24` | el color del modelo en 0,4 (`0x3ecccccd`) y su `RenderBody(2, …)` | — |
| `0x29` | su `RenderBody(2, …)` | la luz (0,2; 0,7; 0) va a un local que nadie lee (Q-C343) |
| `0x38` | su `RenderBody(2, …)` | con el estado en 0, un `rand()` para la luz ((`rand()` % 10.000) · 0,0001f, `0x6b8a94`, en los tres canales) y, con otro en 0 módulo 3, `0x518cf0(0x61d, posición, ángulos, luz, 3, objeto, 4, 0, 0, 0, 0)` |
| `0x39` | ninguno | con `rand()` % 3 en 0, `0x53fe70(0x6ec, …, (1, 1, 1), 5, escala, 0)` y el humo `5f2/21` igual |
| `0x47` | ninguno | lo mismo, sin el humo |
| `0x48` | ninguno | con `rand()` % 10 en 0, otro `rand()` para la luz ((`rand()` & 3) + 3) · 0,1f por (1; 0,6; 0,2) y el humo `5f2/33` 50 más arriba (`0x4308ca`) |
| `0x49` | ninguno | con el clima en 0, la arena del HUD (`0x576353c`) en 1 (`0x430950`); la gasta el HUD (`0x5a7520` → `0x435930`, CL8) |
| `0x4a` | ninguno | con `rand()` % 3 en 0 y el estado en 1, el humo `5f2/21` |
| `0x4d`, `0x4e` | ninguno | con `rand()` % 6 en 0, `0x53fe70(0x622, …, luz, 9, escala, objeto)` con (0,02; 0,03; 0,02) o (0,03; 0,06; 0,05) (Q-C345) |
| `0x51` | ninguno | con el estado en 2, dos `0x4ecc30` de la malla 0 con el color de `0x723c08` y el seno del reloj |
| `0xfa` | el color del modelo en (0; 0; 0,9) y su `RenderBody(2, …)` | — |

Los demás siguen a `0x4339d0`, que devuelve 0 fuera de `0x171..0x1b4`, y `0x6002e0` dibuja su cuerpo. La malla 1 de
`Object36.bmd` (el `0x23`) nombra el hueso −8888 (Q-C347).

**Las partículas.** Todas sin dueño salvo la `622/9`; las texturas las sube `0x61ccb0` en todos los mapas:
`Effect\fantaF.jpg` a `0x6ec` (`0x61d5a0`) y `Effect\coll.jpg` a `0x698` (`0x61d893`), con `GL_LINEAR` y `GL_CLAMP`.

| Partícula | Nace | Paso | Dibujo |
|---|---|---|---|
| `5f2/21` | `0x542370`: vida 80, escala ((`rand()` & 63) + 64) · escala · 0,005f (`0x6b89d8`), giro `rand()` % 360 y subida (`+0x50`) ((`rand()` & 31) + 60) · 0,1f | el de la `5f2/2` (`0x5466c3`, la tabla de `0x548310`) | resta, como la `5f2/2` |
| `622/9` | `0x54330a`: los siete `rand()` de la `622/8` en su orden, pero la escala (`rand()` % 20 + 180) · 0,01f por la del argumento (`+0x10`) y `+0x60` en (`rand()` % 10) · 0,01f, sin sumarle la escala | el de la `622/8` (`0x54768f` → `0x5476a8`, con el default del giro) | la luz por `+0x38`, como la `622/8`, sin el giro del reloj |
| `698/0` | `0x543152`: vida 32 y la escala (`rand()` % 170 + 1) · 0,01f, que pisa la del argumento | `0x54719f`: el cuadro (32 − vida) / 4 en `+0x48` (`cdq`, `and 3` y `sar`) | `0x54899a`: la columna de 8 cuadros de `coll.jpg`, la v en (`+0x48` % 8) · 0,125f, el alto por el double 0,125 y el giro de `+0x20` |
| `6ec/5` | `0x543b82`: la de la `6ed` (vida 24 y los dos `rand()` de la velocidad) y, con la /5, vida 20 (`0x543bd5`) | el de la `6ed` (`0x547a40`) | el de la `6ed` |
| `60f/10` | `0x542ed0`: la de la `60f/3` (vida 60 y cuatro `rand()`) y, sola entre las órbitas (`0x543000`), la partida corrida por sin y cos de (vida + Vx) · 0,1f por 40 | `0x547286`: calcula el argumento y lo tira (`fstp st(0)`); x e y vuelven a la partida, la z sube, la escala −0,002f y la luz por 0,909f con la vida en 20 o menos | el de la `60f/3` |

**El calco `0x61d/3`** (sin modelo). Nace (`0x51ee96`) con el prefijo común de `0x518cf0`, vida 20 y escala 0,5
(con el subtipo 0, vida 15), sin `rand()`. En el paso (`0x52b91f..0x52b954`), con la vida entre 6 y 9 antes del
paso común (`edi` en 5), suelta `0x53fe70(0x60f, posición, ángulos, luz, 10, 0,19f, efecto)`: cuatro órbitas por
calco. El /0 crea un `0x61d/1` en cada paso y, en la misma ventana, órbitas `60f/0`, y el /2 y el /7 órbitas
`60f/0`: no salen de los objetos de los mapas portados. El pase del suelo (`0x52dc20`) dibuja solo el /1, y el
dibujo de los efectos con modelo salta el `0x61d` (`0x52da84`): el /3 no se ve, solo sus órbitas.

**Las burbujas del agua** (`0x5fa695..0x5fa731`). En el 34, cada vez que `0x5fa830` pide a `0x5fa5c0` las UV del
agua (la capa de abajo, con la capa 1 en 5 y alguna esquina de alfa debajo de 1; y la de alfa cuando además la capa
2 es 5 y alguna esquina pasa de 0), un `rand()` y, con 0 módulo 50, la burbuja `0x53fe70(0x698, (x · 100 + rand()
% 100 + 1; y · 100 + rand() % 100 + 1; z del héroe + 10), ángulos del héroe, (0,3; 0,4; 0,2), 0, 1, 0)`, con el
héroe de `0x743a170`. Va antes de dibujar la celda (después del bit `0x08`), y el dibujo no saca `rand()`; la x de
la burbuja pisa el argumento de `0x5fa5c0` (`[ebp+0xc]`), pero el resto de la función lo vuelve a escribir antes de
leerlo (`0x5fa7b7`).

**En MuRet.** El estado y los globales: `WorldRuntime::crywolf` (`Scenes::CrywolfGlobals`) y la cuenta de
`0x6046a0` en `WorldRuntime::objectDrawCounter`, que `DrawGameWorldObjects` (juego y selección) y el dibujo del login
cuentan con cada frame del original. El paso: `World::StepCrywolfObject` (`WorldObjectStepServices`), desde
`StepWorldObjectChecked`; el frame: `Scenes::StepCrywolfObjectFrame`; el dibujo: `Scenes::CrywolfObjectBody`
(`CrywolfObjectServices`), que `DrawGameWorldObjects` llama antes del cuerpo. El calco:
`World::ModelEffects::CreateFadingDecal` y su paso en `StepGameModelEffect`. Las burbujas:
`World::StepCrywolfWaterCell` (`CrywolfWater`), que `GameScene` le pasa a `World::DrawTerrain` como el gancho de la
celda. Corta, con la dirección: la carga con el estado en 1 o más (`0x61a6f4`), el `0x48` con su `rand()` % 10 en 0
(`0x4308ca`), el `0x51` con el estado en 2 (`0x430a40`) y los casos de monstruos de `0x4339d0`. Con la Data del
repo, el 34 llega a un cuadro completo y camina (`GameT16SceneMatrix`; `GameT16MapWalk`, en (128, 30): suelo libre
de `EncTerrain35.att` en la zona segura del fuerte, con tres `0x29`, cuatro `0x38`, el `0x49`, el `0x4a` y los
`0x52` vistos). Tests: `CrywolfObjectsTest`, `CrywolfParticlesTest`, `CrywolfDecalTest`, `CrywolfWaterTest`,
`CrywolfObjectServicesTest`, `MapLoadTest.CrywolfTakesItsPlainAttributesOnlyWithTheStartingState` y
`TextureSetsTest.TheCrywolfSparkAndBubbleTexturesUploadAtTheirOriginalSlots`.

### Las marcas del suelo

`0x549130`, desde `0x5fc365` (*estático*, salvo donde se dice). Son 100 lugares de `0x78` bytes en
`0x760cab0..0x760f990`, en la .bss: (`0x760f990` − `0x760cab0`) / `0x78`. En todo el binario la nombran
solo tres funciones, con un puntero a `+0`, `+0x10` o `+0x28` del primer lugar y su final en el
`0x760f990` correspondiente: `0x548fe0`, que la llena; `0x549090`, que la envejece; y `0x549130`, que la
dibuja. La tabla de antes (`0x7609bd0`) termina en `0x760cab0` (`0x548e50`, `0x548f25`, `0x548fa0`) y la
de después empieza en `0x760f990` (`0x5491e5`, `0x549449`, `0x549510`). La carga del mapa (`0x6070a0`)
apaga esas dos (`0x607115` y `0x607181`) y no esta (Q-C228).

| Campo | Qué es | Quién lo escribe |
| --- | --- | --- |
| `+0` (byte) | prendida | `0x548fe0` (1, y 0 al lugar que apaga); `0x549090` (0) |
| `+4` | el tipo, que es también el slot de la textura | `0x548fe0` |
| `+0x10` | el lado del calco, en celdas | `0x548fe0`; `0x549090` (el `0x5e3` crece, el 7 achica) |
| `+0x14..+0x1c` | la posición; la z se copia y no la lee nadie | `0x548fe0` |
| `+0x28` | el giro, en grados | `0x548fe0` |
| `+0x2c..+0x34` | el color | `0x548fe0`; `0x549090` (0,1; 0; 0) |
| `+0x38` | el alfa | `0x548fe0` (1); `0x549090` |
| `+0x3c` | la vida, en pasos del juego | `0x548fe0` (con los tipos que se borran); `0x549090` (−1) |

Los otros bytes de cada lugar no los nombra ninguna de las tres.

**`0x548fe0(tipo, &posición, giro, &color, lado)`.** Recorre la tabla buscando el primer lugar apagado.
En el camino guarda el de menos vida, con signo y el primero si empatan (`cmp`, `jle`; `0x548ff6..0x548fff`),
partiendo de `0x270f`. En el primero apagado (`0x549012..0x549059`):

- lo prende y copia el tipo, la posición, el giro, el color y el lado;
- pone el alfa en 1;
- con el tipo `0x5e3`, `0x5e4` o `0x606` saca un `rand()` y pone la vida en `rand() % 32 + 50`
  (`0x54906e..0x549082`, el `%` de C con signo). Con otro tipo no saca nada y deja la vida que tenía el lugar.

Sin lugar apagado apaga el de menos vida y no pone la nueva (`0x54900c`), sin `rand()`. Si además ninguna
vida baja de 9.999, `edx` es todavía el tipo, y el `mov byte [edx], 0` escribe un 0 en la dirección que vale
el tipo (Q-C231).

**Quién la llama.** Dos lugares, los dos con el tipo `0x5e3`, que es la sangre: el slot que sube
`0x63bd00` con `Effect\blood01.tga` desde `0x61d7aa`.

- **`0x505de0` con el modelo `0x182`** (`0x507414..0x507486`), en el caso del tipo del paso de cada
  personaje visto ([El paso de los personajes](#el-paso-de-los-personajes)). Sigue solo con
  `rand() % 5` en 0. Entonces saca `rand() % 21 − 10` para la x y otro para la y, cada uno por la constante
  de `0x6ba1a8` (`0x406a0ea1`, 3,657…) y sumado a `+0x10` y `+0x14` del personaje. Llama con el giro `+0x1c`, el
  color `+0xf4` y el lado 0,65 (`0x3f266666`); la z del local no se escribe en ese camino. Son 1 `rand()`, o
  3 más el de la vida.
- **`0x52e810(objeto)`** (`0x52e96c`), desde `0x4fcb10` (`0x4fcb9c`, con la acción `+0x111` en `0xc3` o
  `0xc4`) y `0x4fcbb0` (`0x4fcc3c`, con un modelo de `0x152` a `0x1c9` en la acción 6). A esos dos los llama
  `0x4fcd00`, desde el paso del cuerpo (`0x4fd5f0`). Los dos lo llaman solo con el byte `+0x38a` del
  personaje (su primer argumento) en 0, y lo ponen en 1 antes. `0x52e810` sale si el modelo
  (`0x5769ad8 + tipo · 0xe0`) tiene −1 en `+0x54`, el hueso. Con el `0x161` lanza diez `0x518cf0` con
  `0xca` y sale; con el `0x159`, el `0x160` y el `0x164` sale. Si no, dos veces:
  - `rand() % 100 − 50` para la x y otro para la y del corrimiento (z = 0);
  - `0x4ec110` lleva el corrimiento al hueso, sin `rand()`;
  - `rand() % 4 + 8`, por 0,1, da el lado (0,8 a 1,1);
  - `rand() % 360` da el giro, y el color es `+0xf4`.

  Con lugar, son 5 `rand()` por vuelta. Que sea la sangre de un personaje que muere es *inferido*: el tipo
  y las acciones `0xc3`, `0xc4` y 6.

**`0x549090`**, desde el paso del juego (`0x63712d`). Para cada marca prendida, en el orden de la tabla:

- baja la vida, un `dec` de 32 bits;
- con el tipo 7 (`0x5490c3..0x5490da`): resta 0,05 al lado (`0x6b8a48`), lo guarda y lo compara
  **desde el registro**, en 53 bits, con 0,1 (`0x6b87b8`); debajo, o con NaN, la apaga. La vida no la mira;
- con el `0x5e3` suma 0,004 al lado (`0x6b89e8`);
- con el `0x5e3`, el `0x5e4` y el `0x606`: el color pasa a (0,1; 0; 0). Con la vida en 0 o menos la
  apaga, y con menos de 50 el alfa es vida · 0,02 (`0x6b8b90`, de un `fild`);
- con cualquier otro tipo solo baja la vida: la marca no se apaga sola.

No saca `rand()`. El paso de los personajes (`0x63710f`) va antes que `0x63712d`: una marca nacida en un
paso ya tiene el color (0,1; 0; 0) en su primer dibujo, y el color con que la crearon no se ve nunca.

**`0x549130`**, sin argumentos.

1. Pone la mezcla aditiva (`0x620a90`) aunque no haya ninguna marca.
2. Para cada marca prendida, en el orden de la tabla, mira los componentes del slot de su tipo: el byte
   `+0x28` del lugar de `0x7d3a198`, en `0x549152`.
   - Con 3, la mezcla aditiva (`0x620a90`).
   - Con otro valor, la normal (`0x620a00(1)`, `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` con alpha test).
3. Llama a `0x5fb2f0(tipo, x, y, lado, lado, &color, −giro, alfa, 5)`. El giro va cambiado de signo
   (`fchs`, `0x54917f`) y el 5 es `0x40a00000`.

`blood01` es una TGA de 64 × 64 a 32 bits (*verificado* en el `.ozt` del `Data\`). Que el slot quede
con 4 componentes y vaya con la mezcla normal es *inferido*, como las demás TGA de 32 bits.

A `0x549130` lo llama solo `0x5fc365`, dentro de `0x5fc260` con 0. A `0x5fc260(0)` lo llaman:

- el juego (`0x6374c6`);
- la selección (`0x634844`, con `edi` en 0 desde `0x6346e2`);
- el login (`0x635201`).

Con 1, el picking, `0x5fc260` sale antes de las marcas (`0x5fc2e8`).

**El calco, `0x5fb2f0(slot, x, y, ancho, alto, &color, giro, alfa, lift)`.** Tiene quince llamadas:
`0x549198` y catorce de efectos, ninguna portada. Entre las de efectos están las de `0x52dc20`
(`0x6374d3`).

1. **El color y el slot.** Con el alfa en 1, `glColor3fv`; si no, `glColor4f`. Las dos dejan el mismo
   color (`0x5fb2f9..0x5fb32d`). Arma la matriz de `0x5fc8f0((0, 0, giro))` y liga el slot con
   `0x620780` (`0x5fb351`), sin tocar la mezcla ni la profundidad.
2. **El centro.** (x, y) · 0,01 (`0x6b86c0`), guardado en float, y su celda c con `ftol`
   (`0x5fb35a..0x5fb383`).
3. **Las cuentas del calco.**
   - El lado es el mayor de ancho y alto; con NaN, el alto (`0x5fb388..0x5fb39d`).
   - El comienzo de las UV es la celda menos el centro, más medio lado. La u parte de la celda guardada
     en float, y la v del entero en el registro (`0x5fb3a0..0x5fb3c4`).
   - 1 / lado, guardado (`0x5fb3c9..0x5fb3d4`).
   - n = `ftol(lado) + 1`, con un `inc` de 32 bits.
   - El estiramiento es ancho / alto (`0x5fb3dc..0x5fb3fa`).
4. **Las celdas.** Con j e i de −n a n en float (de a 1, `0x6b87d4`), saca las UV de la celda en el calco:
   de ((i + comienzo u) · (1 / lado), (j + comienzo v) · (1 / lado)) a las de la celda siguiente, con el
   1 sumado antes de multiplicar (`0x5fb42b..0x5fb4a0`). A cada UV le resta 0,5, la gira con `0x5fcbd0`, multiplica la u por el
   estiramiento y le suma 0,5 (`0x5fb4a3..0x5fb4fe`). Después llama a `0x5faec0(cx + i, cy + j, 1, 1, uvs,
   0, alfa, lift)` (`0x5fb500..0x5fb529`). Son (2n + 1)² celdas, con la del centro en el medio. Con
   n < −n no dibuja nada, y eso pasa solo cuando `ftol(lado)` es negativo o da la vuelta.

`0x5faec0` dibuja una celda:

1. Hace el `ftol` de x y de y. Con alguno negativo o de 255 en adelante, con signo, no dibuja nada
   (`0x5faedb..0x5faefa`).
2. Arma las esquinas (x, y), (x + 1, y), (x + 1, y + 1) y (x, y + 1) por 100. Cada una tiene su UV y
   z = lift + la altura de `0x7accca4` de la misma esquina.
3. Las manda en un `GL_TRIANGLE_FAN` (`0x5fb05e..0x5fb0d9`). Con el sexto argumento en 0 no hay color ni
   luz por vértice.

Deja las esquinas en `0x78acbc4..0x78acbd0` y los vértices en `0x79ecc30..0x79ecc5c`, que el terreno pisa
antes de leerlos. No mira el atributo de la celda: dibuja sobre las que el terreno saltea por el bit
`0x08`, y con el `0x40` usa la altura del `.OZB` y no la fija (Q-C232).

**Entre mapas y escenas.** La tabla dura toda la sesión: ni la carga del mapa (Q-C228) ni la vuelta a la
selección la tocan. La selección corre `0x5fc260(0)` y, con eso, `0x549130`. Su paso no corre `0x549090`,
que solo llama el del juego. Las marcas que quedan prendidas al volver se dibujan en la selección con el
lado, el color y el alfa del último paso del juego, quietas (Q-C233).

**MuRet** (CL7, hito 1):

- **La tabla.** `WorldRuntime::groundMarks` (`World::GroundMarkTable`): 100 `GroundMark` apagadas al
  arrancar, como la .bss, que `LoadMap` no toca.
- **`0x548fe0`.** `World::AddGroundMark`, con el `rand()` de `CrtRandom`. Si la tabla está llena y
  ninguna vida baja de 9.999, corta con error en `0x54900c`. No lo llama nadie: los dos creadores están en
  ramas que MuRet no corre. El modelo `0x182` corta antes en `0x505e0b`, y `0x52e810`, desde `0x4fd5f0`,
  es frontera (P-C44, P-C49 b).
- **`0x549090`.** `World::StepGroundMarks`, desde `GameScene::StepWorld` después de la espera, en
  `0x63712d`.
- **`0x549130` y `0x5fb2f0`.** `World::DrawGroundMarks` y `World::DrawTerrainDecal`, en `0x5fc365`, entre
  el estado de `0x5fc35b..0x5fc360` y el depth test de `0x5fc36a`. El juego de MuRet no sube `blood01` ni
  sabe los componentes de su slot: con la tabla vacía no dibuja nada, y con una marca prendida corta en
  `0x549152` (P-C49 b). La selección y el login no dibujan las marcas (P-C49 c).

Tests: `GroundMarksTest` y `TerrainDecalTest`;
`GameSceneDrawTest.TheReferenceMapsDrawTheTerrainTheGrassTheGroundMarksAndTheObjects`,
`ALiveGroundMarkStopsWhereMuRetDoesNotKnowItsTexture` y `TheStepAgesTheGroundMarksAfterTheCharacters`,
en Lorencia y Devias; y `MapLoadTest.TheGroundMarksSurviveTheMapLoad`.

### La cámara del juego

La cámara del juego es un solo cálculo, `0x635ae0`, que corre **una vez por frame dibujado**, dentro
del dibujo de la escena (`0x6371a0` → `0x637241`), después de los pasos de ese frame: la llaman
`0x6371a0`, `0x6350b0` y `0x634680`, y ningún paso (*estático*). Lee el héroe que dejó el último
paso del frame. Los números de Lorencia salen bit a bit del código real emulado con el héroe de la
traza, y son los de la traza (*emulado* y *verificado*); la cuenta de abajo, reimplementada en
doubles con `f32()` donde el binario guarda en float, da lo mismo que el código real en 22.000
frames al azar (cinco semillas: el héroe, el atributo, el temblor, el reloj, el mapa, D y el zoom).

**MuRet, CL7.3 A (2026-11-05).** `World::GameCamera` y `StepGameCamera` reproducen esta cuenta,
incluidas las cámaras de las escenas 2 y 4, el desvío de los efectos y la rama especial del mapa 6.
Se comparan todos los campos de la cámara y del estado bit a bit con 3.000 frames del código real
emulado (60 casos, cinco semillas; [`vectores`](../../tests/vectors/game/README.md)). El giro inverso
se contrasta además con 403 salidas de `0x5fcc30` real, incluidas tres cancelaciones que observan
el orden de sumas, sin usar la fórmula C++ como expectativa. Seis casos analíticos, dos por
componente, detectan las dos asociaciones alternativas: los tres casos históricos por sí solos
no detectaban todas. El pase y las
329 celdas recortadas de Lorencia usan ahora la cámara calculada desde el héroe de la traza, sin
copiar su posición de la expectativa. `GameCameraOf` conserva la D usada antes del suavizado, el
zoom y el far, y reconstruye el dibujo con el héroe del último paso lógico sin otro avance. La
integración en las escenas es la CL7.3 B; los ocho `rand()` protegidos por frame, la CL7.P. El
recorrido `CCameraMove` sigue fuera de este valor de cámara.

**Contraste ASM y pseudocódigo (CL7.3 A).** Para `0x5fcc30`, el orden de las dos sumas
lo fija el ASM, no la expresión reordenada del pseudocódigo de Ghidra. Cada componente conserva
sus productos y sumas en x87 hasta el único guardado float: x en `0x5fcc50`, y en `0x5fcc67`,
z en `0x5fcc7f`. MuRet usa double y convierte a float en esos mismos límites; el arnés fija
PC53 (`0x027f`), que la función no configura por sí misma. El destino original es un tercer
puntero; MuRet devuelve un valor y no reproduce un destino superpuesto con las entradas.

Para `0x635ae0`, se cotejaron las 462 instrucciones del export (1.913 bytes), sus constantes y
sus tablas con el PE del SHA indicado en el research. La lectura del `.c` se contrastó con ese
ASM. En el alcance de la CL7.3 A no aparecieron discrepancias de lógica ni de constantes:

| ASM | Correspondencia en MuRet |
| --- | --- |
| `0x635b3d..0x635c4f` | la rama especial del mapa 6 usa D vieja y omite el far normal, el reset del nivel, el `0x40` y el zoom |
| `0x635d8f..0x635e59` | el rectángulo inclusivo precede al mapa; far 2.500 solo con mapa 30 y escena 5; nivel leído como word con signo, antes de volverlo a 0 |
| `0x635e63..0x635efc` | la posición usa D previa; el objetivo de efectos sustituye x/y, y el camino sin recorrido vuelve el nivel a 0 |
| `0x635f02..0x635feb` | z base 255 en mapa 30 en todas las escenas; `0x40` deja 1.201; las escenas fijas pisan posición/yaw antes del zoom |
| `0x635fed..0x6360fe` | zoom ±10, hasta 210, con giro fijo −45; también se aplica un zoom negativo distinto de cero |
| `0x636104..0x63614f` | balanceo del mapa 5, con las tasas float exactas y sin actualizar D ni su meta |
| `0x636150..0x63624d` | D/meta 3.200 en la rama especial del mapa 6, 1.100 en mapa 30 en cualquier escena, o suavizado de un tercio hacia 1.000 tras el reset |
| retornos de `0x635ae0` | `AL = 0`; el pseudocódigo no implica que todo EAX valga 0. MuRet devuelve una cámara por su propia interfaz |

El recorrido `CCameraMove`, la vista superior muerta y los accesos protegidos no se incluyeron en
esta equivalencia; sus exclusiones y la sub-ola que los cubre están arriba. Los valores iniciales
se toman del contrato de inicialización; este contraste es de la función de cámara.

El objetivo de efectos se decide en el callee `0x44dc60`, cuyos 130 bytes se contrastaron con
el PE. Compara X, Y y Z con `+0` mediante `FCOMP`, y prueba solo C3 (`TEST AH,40`), sin C2.
Una componente ordenada distinta de cero elige el vector global, incluso si solo Z es distinta
de cero; los ceros negativos conservan el héroe. Con invalid-operation enmascarada, un NaN
también deja C3=1: por sí solo no elige el vector global. El predicado portable usa comparaciones
ordenadas y los tests cubren esos casos en `StepGameCamera` y `GameCameraOf`. Esta equivalencia
es del valor seleccionado; no acredita las excepciones sin enmascarar ni los payloads de NaN
que atraviesan `FLD/FSTP`. La función original no configura su CW.

**Límite de la comparación x87 (revisión cruzada, CL7.3 A).** El balanceo del mapa 5 usa
`std::sin` en MuRet. Con los relojes float `0x4f0d7740`, `0x4f2f0344` y `0x4f5ac415`, Unicorn y
MuRet coinciden en Y (`0x393da1cd`, `0x33b96bb5`, `0x33e7c6a2`), pero una reproducción local de
`FLD/FMUL/FSIN/FMUL/FADD/FSTP` con PC53 en x87 da un ULP más en los tres. Es evidencia del
cálculo aislado en el host de revisión, no una captura del cliente original en Windows. Por eso
los vectores prueban equivalencia al oráculo emulado en esas entradas; no garantizan la misma
aproximación trascendental en todo hardware. P-C29 a queda abierto para capturar también estos
relojes y el resultado de `FSIN` en el original, antes de cambiar su aproximación portable.

La fuente oficial de [Unicorn 2.1.4, `fpu_helper.c`](https://github.com/unicorn-engine/unicorn/blob/8028ec436f2d9376525352dd38ed9ed6b9f6be10/qemu/target/i386/fpu_helper.c#L966)
explica el límite del oráculo: `helper_fsin` y `helper_fcos` convierten el operando x80 a double,
llaman a `sin`/`cos` de C y convierten el resultado a x80. El pin de versión no fija el hash del
artefacto instalado ni su libm. No se regeneraron los fixtures durante esta revisión; su
concordancia sigue siendo una regresión histórica, junto al contraste ASM y a las observaciones
nativas descritas abajo.

**Observación nativa en XP (2026-11-05).** Se verificó el SHA c32bbd5f del archivo del cliente
abierto y las firmas de cámara/inversa en su proceso. Tres posiciones estacionarias del héroe,
(14.150; 12.350; 165), (14.050; 12.350; 165) y (14.550; 12.350; 165), coinciden bit a bit en las
nueve salidas de cámara con `StepGameCamera` enlazada desde MuRet. D/meta son 1.000, zoom/nivel
0, yaw −45, altura fija 1.200, far 2.000, near 20, fov 35, temblor/desvío 0. El estado se carga
como entrada estacionaria: sus siete checks adicionales no predicen un historial. Las primeras
dos lecturas no traían atributo; la tercera lee el de la celda visual, que no acredita la lógica
cifrada. Después del cierre del servidor se obtuvo la **CW del hilo real de main: `0x027f`**
(PC53), mediante `GetThreadContext`, con EIP `0x7c90e4f4`, fuera de cámara/SendMove. La
suspensión previa era 0 y `ResumeThread` devolvió 1: el hilo quedó reanudado. Esto corrobora la
configuración en ese instante, no la precisión dentro de esas funciones ni de todos los frames.
La rama FSIN dentro de main sigue sin capturarse.

La primera serie continua, 2.250 muestras durante 89,968 s, quedó estacionaria: no registra la
vuelta que se había pedido antes de iniciar el lector. Una segunda serie de 89,969 s contiene
cuatro clics normales enviados después de confirmar muestras activas: 63 posiciones observadas
en 10 celdas visuales, de (14.550; 12.350; 165) a (15.050; 12.050; 165), con acciones `01` y
`0d`, y el héroe detenido al final. Son lecturas secuenciales con agenda nominal de 40 ms,
no pasos ni frames consecutivos. Sus 165 imágenes tienen tiempos propios y tampoco forman una
traza de llamadas GL. El lector declara sus fronteras y exige código 0, 2.250 bloques completos,
footer y ausencia de errores. Las herramientas están en
[`scripts/game/capture/`](scripts/game/capture/README.md); evidencia, imágenes y video sin pérdida
quedan fuera de Git.

El tamaño interno fue 1.024×768 y el cliente Win32 1.906×1.032. La imagen quedó abajo a la
izquierda después del resize, como señaló Emanuel. Para los clics de la segunda serie se ajustó
la ventana temporalmente a 1.024×768 y luego se restauró su tamaño. Esto distingue área visible,
ventana y resolución interna: **no se consultó GL_VIEWPORT**. No confundir ese anclaje del
original con un defecto de MuRet, que admite pantallas anchas (D-C11).

El probe x87 propio también se ejecutó en XP: los tres relojes largos dan un ULP más que
Unicorn/MuRet tanto en PC53 como PC64, restaurando su CW. Esa es evidencia nativa del cálculo
**aislado**, no de la función ni de la CW del proceso original; P-C29 a sigue abierto.

**La cuenta**, en la escena 5 fuera del mapa 6 (*estático*, *emulado*). H es el héroe (`0x743a170`):

1. **El fov** en 35 (`0x635aeb`) y los ángulos X e Y en 0. La vista de arriba (`0x7d36899`, sin el
   giro en X) pondría la cámara en la x y la y del héroe, con la z y el far en 3.200 absolutos, no
   sobre el héroe (`0x635b12..0x635b32`), pero nadie escribe la bandera: está muerta.
2. **El atributo** de la celda `[H + 0x420] · 256 + [H + 0x41c]` (la del servidor, por la tabla de
   protección; `0x5f8290`): el byte de abajo del WORD de `0x7d0d348`.
3. **El far** (`0x6d5d44`): **3.000** si 16.100 ≤ H.x ≤ 19.000 y 18.900 ≤ H.y ≤ 21.700
   (`0x635d8f..0x635dd0`, inclusivos y **sin mirar el mapa**, Q-C149); si no, 2.500 en el mapa 30;
   si no, por el nivel `0x7d3a0ec` con el valor del frame anterior (Q-C155): con 0, 20.400 en la
   escena 2, 11.519,9 en la 4 y **2.000** en las demás; con 1 a 4, 2.500, 2.600, 2.950 y 3.200;
   fuera de 0 a 4 (el `ja` de `0x635e0a`) queda como estaba.
4. **El corrimiento**: `out = 0x5fcc30((0, −D, 0), 0x5fc8f0((0, 0, yaw)))`, con D en `0x7d36ba4`,
   la de antes del suavizado (`0x635e63..0x635e96`). `0x5fc8f0` es la `AngleMatrix` que ya porta
   MuRet; `0x5fcc30` es un `VectorIRotate` con su orden de sumas, x' = (m8·z + m4·y) + x·m0, y' =
   (m1·x + m9·z) + m5·y y z' = (m2·x + m10·z) + m6·y, cada una guardada en float
   (`0x5fcc3c..0x5fcc7f`).
5. **El objetivo**: `0x44dc60` da `0x724588..0x724590` si alguno no es 0, y si no el héroe (`H +
   0x10..0x18`). Ese desvío lo escriben en el paso los efectos 299 y 300 del héroe (`0x5208a0`, con
   la z topeada en 800), y `0x52d220` lo vuelve a 0 al principio de cada paso de los efectos.
6. **El recorrido** `CCameraMove` (el objeto de `0x6e57a8`, que arma `0x403210` entre los
   inicializadores estáticos): con su estado `+0x154` en 2 y `+0x6a` y `+0x158` en 0 (`0x4033d0`),
   escribe **H.z = 300** (`0x635ec0`, Q-C153), el objetivo pasa a ser el suyo (`+4..+0xc`) y el
   nivel, su `+0x150` (`0x4034b0`); si no, el nivel vuelve a 0 (`0x635eda`). El estado lo pone solo
   el `BD 00` del servidor ([04](04-protocolo.md#el-clima-y-crywolf-0f-y-bd)).
7. **La posición**: x e y, f32(out + objetivo) (`0x635ee3..0x635efc`); z, f32((D − 150) + z0)
   (`0x635f39..0x635f50`, el 150 en `0x6b87ac`), con z0 en 255 en el mapa 30 y si no **H.z**, la
   del héroe y no la del objetivo. Con el bit `0x40` en la celda, `0x6c9d5c` y z0 pasan a 1.201
   (`0x635f14..0x635f2f`, Q-C150).
8. Las escenas 2 y 4 pisan la posición, Y y el yaw con su cámara fija ([Cámara](#cámara)).
9. **El pitch**: f32(temblor + k), con k en −96,5, −84,5 o **−48,5** (`0x6ba7b8`;
   `0x635fd3..0x635fe5`). El temblor (`0x7d325c8`) lo escriben los efectos de las habilidades
   (`0x405630`, `0x43c150`, `0x4453e0`, `0x5208a0`) y el paso de la escena 5 lo achica × 0,2: se
   achica por paso y se suma por frame (*emulado*: +0,7 da −47,8).
10. **El zoom** (`0x7d3689c`, `0x635fed..0x6360fe`), por frame: con el bit `0x80` de la celda, +10
    mientras sea ≤ 200 (hasta 210); sin él, −10 mientras sea > 0. Distinto de 0, la cámara suma
    `0x5fcc30((0, zoom, 0), 0x5fc8f0((0, 0, −45)))`, (−0,7071 · zoom; +0,7071 · zoom; 0): hacia el
    héroe, con el −45 fijo y no el yaw. El pitch no cambia, así que la vista se corre hacia adelante
    (*emulado*: 21 frames hasta 210, de a (−7,07; +7,07), y la vuelta a 0 justa).
11. **El final** (`0x636104..0x636258`): en el mapa 5, el balanceo `X += 2 · sin(T · 0,0005)` e `Y
    += 2,5 · sin(T · 0,0008)` (T en `0x5767b88`), sin suavizar D; en el 6 con "webzen", D y su meta
    en 3.200; en el 30, en 1.100; en los demás, la meta (`0x6d5ee0`) es 1.000, 1.100, 1.200, 1.300 o
    1.400 según el nivel ya vuelto a 0, y D = f32((meta − D) · 0,33333334 + D), un tercio por frame
    (Q-C151). Siempre devuelve `al = 0`: en `0x6371a0`, la rama del objetivo en `H + 0x17c` está
    muerta.

**Lo que queda de una vez a la otra** (*estático*). D arranca en 1.000: lo pone `0x635ad0`, un
inicializador estático (su puntero está en la tabla de `_initterm`, en `0x6c61bc`). El yaw lo
escribe **una sola vez** el primer paso de la escena 5 (`0x636838`), y en el juego nada más lo toca:
las otras escrituras son las de `0x635ae0` (las escenas 2 y 4, y el mapa 6), la entrada al login
(`0x634c19`), la vista previa de crear (`0x6338ca`) y las rutas (`0x633b45`). El zoom, D, el yaw y
la altura del `0x40` son globales y pasan de una escena a otra (P-C27 b).

**Los números de Lorencia** (*emulado* y *verificado*): con el héroe en (14.350; 12.050; 165), la
celda en `0x01`, el mapa 0, la escena 5, D = 1.000 y el yaw −45, el código real da justo las
llamadas de la traza.

| Valor | Sale de | En la traza |
| --- | --- | --- |
| Y 0°, X −48,5°, Z −45° | el balanceo (solo en el mapa 5), `0x6ba7b8` con el temblor en 0, el −45 del primer paso | los `glRotatef` de las líneas 68 a 70 |
| x 15.057,1064453125 (`466b446d`) | 14.350 + f32(−1.000 · sin(−45°)) | `glTranslatef(-15057.106445, …)`, línea 71 |
| y 11.342,8935546875 (`46313b93`) | 12.050 + f32(−1.000 · cos(−45°)) | `-11342.893555` |
| z 1.015 (`447dc000`) | (1.000 − 150) + 165 | `-1015.000000` |
| fov 35, near 20 | `0x635aeb` y la `.data` de `0x6d5d40`, que nadie escribe | el `glMultMatrixd` del pase `0,77,1024,691` (línea 64) |
| far 2.800 | 2.000 (`0x635e35`) por el 1,4 de `0x6b8c00` (`0x620e2d`) | −1,014388 y −40,28777 |
| viewport | `0x620db0(0, 0, 640, 432)`: 432 · 768 / 480 | `glViewport(0, 77, 1024, 691)`, línea 63 |
| niebla | `0x7d368a0 = 0` (`0x63723a`) | `glDisable(GL_FOG)`, línea 79 |

**El mouse no mueve la cámara** (*estático*). Los botones derecho y del medio y la rueda dejan sus
banderas en el WndProc ([02](02-arranque-y-ciclo-de-vida.md#el-wndproc-0x4af4e0)), y ninguna llega a
la cámara. Las escrituras de `0x7d36868..0x7d3688c`, `0x7d3689c`, `0x7d36ba4` y `0x7d3a0ec` son
estas y no hay otras: en la escena 5, `0x635ae0` y `0x636838`; en las otras, las rutas del login y
de la selección (`0x633920`), la vista previa (`0x6338a0`) y la entrada al login
(`0x634b80..0x634c19`); y la vuelta a 0 del nivel al salir del juego (`0x4c250e`, en `0x4c2480`).
Con Ctrl y la rueda, `0x632dd0` cambiaría el nivel de 0 a 4, pero no la llama nadie: no hay un
`call`, un puntero ni otro `inc` o `dec` de `0x7d3a0ec`.

**El zoom y el `0x40` no se prenden en Lorencia, pero el `0x40` sí en el mapa 30** (*verificado* en
los 29 `EncTerrain*.att` del `Data\` y en el volcado). El cargador del `.att` rechaza el archivo si
una celda trae `0x80` o más (`0x5f85fd`), y ningún `.att` la trae: el `0x80`, el del zoom, solo lo
podría poner el paquete `0x46` (`0x4e2b10` → `0x4dab90` → `0x5f8750` o `0x5f8710`), con los bits que
mande el servidor; los usos fijos de `0x5f8750` ponen `0x08` o `0x04` (*estático*; P-C29 g). El
`0x40` está solo en los cuatro `.att` de `World31` (`EncTerrain31`, `EncTerrain312` y los dos de
prueba): 835 celdas, de x 160 a 190 e y 190 a 220, sobre el castillo y el rectángulo del far
(Q-C149). Ahí, con el héroe encima, z0 pasa de 255 a 1.201 y `0x6c9d5c` queda en 1.201 (Q-C150). El
volcado vivo de Lorencia (`lor_att.bin`) no tiene `0x80` ni `0x40`, con el byte de arriba siempre en
0 (la celda (143, 120) vale `0x01`). Antes este documento, con el barrido de `World1` a `World12`,
decía que ningún `.att` tenía el `0x40`.

**El frustum del juego** (*estático*; el trapecio, *verificado* en Lorencia por
`GoldenTerrainTrace`). Lo de [Terreno](#terreno) vale igual. Lo propio de la escena 5:

- El rango (`0x5fbb00`) lo arma la modelview de la **franja**, que es la misma cámara (la lee
  `0x620370`), con D = far · 0,9 (1.800 en Lorencia, 2.700 en el rectángulo) y k = w · 0,0015625.
- El trapecio (`0x5fb570`), con el nivel 0: lo lejos K = 2.400 (`0x6ba6c8`), lo cerca N = 456, el
  objetivo T = 1.128 y los medios anchos 1.190 · k y 540 · k, centrado en el objetivo de
  `0x44dc60`, girado 45° fijos (−yaw solo en el mapa 55 y con "webzen") y por 0,01. El nivel del
  trapecio (`0x7b4ccb8`) toma el de la cámara solo si `ftol(D) ≤ ftol(meta)`
  (`0x5fb70e..0x5fb738`). En el login K es 244.800 y f 0,159375; en la selección, 13.823,88 y 0,009;
  en el juego, 2.400 y 1/640.

### El trapecio completo

CL7.5E agrega el núcleo puro de `0x5fb570`, con estado efectivo explícito independiente del
solicitado signed16 de la cámara. Evidencia estática del mismo PE SHA256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`: función
`[0x5fb570,0x5fba77)`, SHA256 `a28528d42a4cbd695d9050a6878a964a3ada556eb6fbc1131d882f1713f4ea10`,
326 instrucciones/1.287 bytes cotejados sin huecos ni discrepancias. Esto acredita selección,
constantes y stores; no ejecución nueva ni pureza universal de sus callees.

Prioridad: mapa6 y resultado de nombre para perfil; después mapa30/escena5; resto nivel normal.
Los especiales retienen exactamente el efectivo sin convertir D/meta ni consumir solicitado.
El normal trunca primero D y después meta a int64 y compara **sus bajos32 como signed32**;
adopta el solicitado con signo cuando meta≥D, o retiene. La geometría también se calcula en
escenas distintas de2/4/5; allí sólo el consumidor `0x5fba90` deja pasar todo. El wrapper legacy
conserva su sentinel `Other` de arrays+0; no hay estado oculto en él.

Con W int32 exacto, g=`bits32(0x3acccccd)`, a=R32(W×g), q=R32(W×g+`bits32(0x3dcccccd)`):

| Perfil | F / T / N | FW / NW, ambos con store R32 |
| --- | --- | --- |
| mapa6 especial | 8500 / 3995 / 425 | a×3000 / a×540 |
| mapa30/5 grande | 5100 / 2397 / 969 | a×2250 / a×540 |
| mapa30/5 pequeño | 3300 / 1551 / 627 | a×1300 / a×580 |
| nivel0 escena2 | 244800 / R32(F×t) / R32(F×n) | b×1190 / b×540 |
| nivel0 escena4 | 13823,880859375 / R32(F×t) / R32(F×n) | b×1190 / b×540 |
| nivel0 resto | 2400 / R32(F×t) / R32(F×n) | b×1190 / b×540 |
| nivel1 | 2700 / 1269 / 513 | q×1200 / q×540 |
| nivel2 | 3000 / 1410 / 570 | q×1300 / q×540 |
| nivel3 | 3300 / 1551 / 627 | q×1500 / q×580 |
| nivel4 | 3400 / 1598 / 646 | q×1600 / q×660 |

Aquí t=`bits32(0x3ef0a3d7)`, n=`bits32(0x3e428f5c)` y b=R32(W×k), con k de escena2
`0x3e233333`, escena4 `0x3c137469` o g. **q no guarda W×g antes de sumar0,1f**. No fusionar
pequeño30 y nivel3: sus anchos difieren. Los negativos y todo el ancho de W cuentan.

La región grande30 son las franjas X `[16100,17100)` o `(18300,19000]`, con Y inclusiva
`[18900,21700]`, usando XY del objeto y no la traslación del argumento. Quiet NaN o infinito
en una coordenada selectora eligen pequeño en el modelo portable; la continuidad nativa ante
NaN requiere invalid enmascarada. Z no selecciona. El yaw se elige independientemente: mapa6
con el **segundo** resultado completo de nombre, o mapa55, usan −cameraYaw; resto45. Los dos
resultados de nombre no se fusionan por presunta estabilidad ni representan búsquedas individuales
de «webzen»/«webzen2».

Esquinas locales: `(-FW,R32(F-T),0)`, `(FW,R32(F-T),0)`, `(NW,R32(N-T),0)`,
`(-NW,R32(N-T),0)`. Rotación mediante `OriginalRotation`, suma de traslación XYZ con store f32
y después escala XY por `bits32(0x3c23d70a)` con otro store. La proyección de53bits existente
no acredita libm=x87 para todo float ni ambiente FP arbitrario. Traslación/geometría deben
permanecer finitas representables y el yaw usado dentro del dominio del helper existente.

La frontera portable de errores normal (`InvalidCameraDistance`, luego `InvalidEffectiveLevel`)
está en [ADR0009](../../../docs/adr/0009-errores-del-trapecio.md): no imita locales sin inicializar
ni promete un retorno universal del FISTP excepcional. Snapshot estable y sin alias; defaultsDTO
sin significado probado de inicio/reset. Los tests unitarios observan perfiles, bordes, stores,
estado, bajos32 y errores; las regresiones históricas observan login55, selección54 y trazas de
celdas.

**Los globales y su dueño** (T11b del hito 1; *estático*). El trapecio (las Y en `0x78ecbf0` y las X en
`0x78ecc00`) y el nivel efectivo (`0x7b4ccb8`) son uno para todas las escenas. Solo los escribe `0x5fb570`
(`0x5fba55` y `0x5fba68`, el trapecio; `0x5fb731`, el nivel); a `0x5fb570` lo llama solo `0x5fbb00`
(`0x5fbeef`), y a `0x5fbb00`, los dibujos del login (`0x6351b7`), de la selección (`0x63475c`) y del juego
(`0x637366`). Solo los lee `0x5fba90`, que mira la escena del momento (`0x6d5dc8`) y no la del que los
escribió: en las escenas 2, 4 y 5, que son las que los escriben, prueba el trapecio. Están en la .bss (la
.data del archivo, desde `0x6c6000`, trae `0x20000` bytes): arrancan en cero, un trapecio sin área en (0, 0)
que deja pasar todo con margen negativo (Q-C217). Así, el paso de los personajes de cada escena lee el del
último dibujo de cualquiera: el primero del login, los ceros o el de la escena de antes; el primero del
juego después de la espera, el de la selección (Q-C216). En MuRet son `Scenes::WorldRuntime::terrainPolygon`
y `terrainPolygonState`. Cada frame del original corre la cámara (`0x635ae0`) y detrás `0x5fbb00`, en par
(`Scenes::StepTheTerrainPolygon`, en la selección y el juego); un dibujo intermedio de MuRet solo calcula con
el nivel que hay, sin escribir (`TerrainPolygonBetweenFrames`). El login escribe el trapecio del nivel 0 sin
la transición: con el nivel pedido siempre en 0, da lo mismo ([08](08-pendientes.md) P-C41 d). El rango
también es compartido ahora por `WorldState::DynamicLighting()`, que lo publica desde el dibujo de las
tres escenas y lo conserva para la copia RGB del paso posterior. Los buffers de olas siguen siendo de
cada escena, y la historia global de los planos de `0x5fbb00` continúa pendiente (P-C46).

- Quién lo usa:

| Qué | Dónde | Prueba | Margen |
| --- | --- | --- | --- |
| los bloques y las celdas del terreno | `0x5fc190`, `0x5fc0e0` | el trapecio, en (x + 2, y + 2) y en el centro | −40 y 0 |
| los sectores y los objetos | `0x6046a0` | el trapecio | −180 y −30 |
| los pájaros y los peces | `0x446b60`, `0x446f00` | el trapecio | −20 |
| **los personajes, en el paso** | `0x508f00` (`0x50918c`), a `+0x16c` | el trapecio **del dibujo anterior**, de cualquier escena, en la posición · 0,01 | −20 |
| los efectos con modelo y los ítems del piso | `0x52d620`, `0x608a00` | una esfera contra los cinco planos de `0x5fbb00` (`0x5fbf00`; los ítems con radio 400) | — |

`0x508f00` además le prende el `0x02` a la celda de cada personaje (`0x509165`,
[Caminar](#caminar)). Los cinco planos salen de la cámara y de las cuatro esquinas lejanas, con la
modelview de GL (`0x5fd000`), que arma el driver, como el rango (P-C24 a).

### La esfera de visibilidad de los efectos

Lectura **estática** del build `c32bbd5f…a26a`, sin ejecución ni emulación nuevas. Los cinco planos
que usa `0x5fbf00` son distintos del trapecio XY. `0x5fbb00` transforma la cámara y las cuatro
esquinas lejanas por la transpuesta de la modelview (`0x5fcc30`), guarda cada componente en f32 y
suma después la posición de cámara, con otro store f32. Con puntos 0=cámara y 1..4=esquinas en
orden, `0x5fbd7b..0x5fbdf6` llama a `0x5fd000` con `(0,1,2)`, `(0,2,3)`, `(0,3,4)`, `(0,4,1)` y
`(3,2,1)`: cuatro lados y el plano lejano, sin near. Los primeros cuatro offsets toman el punto0;
el quinto, el punto1 (0x5fbebb..0x5fbee5), aunque la normal tomó el3 como base. Cada offset es `R32(-((nz*pz + ny*py) + nx*px))`, con el perfil de53bits del
helper existente; no es el orden del consumidor.

`0x5fbf00` recorre los cinco planos en orden. Compara `((nx*x + nz*z) + ny*y) + offset` con
`-radius`, sin stores f32 intermedios. Rechaza si es menor o sin orden (`test ah,1`, C0); la igualdad
pasa. No consulta la escena ni el trapecio, y no agrega un sexto plano. `0x52d63c..0x52d655` lo llama
con radio400 y la posición lógica del efecto: escribe `+0x16c` **antes** de consultar el tipo, el
modelo o su alpha. Un lugar muerto no se consulta ni modifica su marca. La extrapolación entre
pasos de MuRet ocurre después de este recorte.

MuRet comparte los cinco puntos entre `TerrainRangeOf` y `SphereFrustumOf`; `SphereVisible` porta
la comparación y `DrawModelEffects` publica la marca. El constructor de planos reutiliza
`FaceNormal`: su contrato exige triángulos finitos y no degenerados de una cámara válida, como
las cámaras portadas. No reproduce la retención de normales del original ante un triángulo
inválido ni fija la CW x87. `SphereFrustumTest` cubre igualdad y los cinco rechazos, NaN, orden y
precisión de sumas, trasposiciónXYZ, traslación, ancho reducido y cámara del login. Son pruebas de
la reimplementación contra casos derivados del ASM, **no** observaciones nativas del original.

**El ancho del mundo** (`0x5bb8d0`) es 640 sin ventanas, 450 (`0x1c2`) con una y 260 (`0x104`) con
dos, según `0x659290`. Achica el viewport (a `0,77,720,691` con una ventana en 1024 × 768), el
aspecto, el rango y el trapecio (*estático*; las ventanas son de la CL8).

**Las cámaras especiales:**

| Cámara | Qué hace | Clase |
| --- | --- | --- |
| el mapa 6 con "webzen" | `0x576960` busca un **substring**, con `_mbclen`: la tiene cualquier nombre que contenga "webzen", y la rama de "webzen2" (5.700; 16.300; yaw −140) está muerta, porque "webzen2" contiene "webzen" (Q-C152). La que corre: yaw −40, pitch −70, z 1.000, el punto fijo (5.400; 16.000) más el corrimiento, far 5.000 y D = 3.200 desde el frame siguiente; el objetivo del frustum es (6.400; 15.400; H.z) (`0x6371a0`) y el trapecio, propio (3.995 y 425, girado −yaw). Al salir del mapa 6 el yaw queda en −40 hasta volver a entrar al juego, y D baja de 3.200 a 1.000 en unos 20 frames (2.466,7 en el primero) | *estático*; *emulado* |
| el mapa 5 (`World6`) | el balanceo del pitch y de Y con el reloj, sin suavizar D | *estático*; *emulado* |
| el mapa 30 (`World31`, el castillo) | la z base en 255 (1.201 sobre las 835 celdas con `0x40` del castillo), el far en 2.500 y D en 1.100 de golpe; en el rectángulo, far 3.000 y la niebla lineal de `0x42b0a0` (de 2.000 a 2.700); fuera, `glDisable(GL_FOG)`; el trapecio, propio | *estático* |
| el recorrido `CCameraMove` | con el `BD 00`: el objetivo y el nivel son los suyos y H.z = 300 en cada frame; al entrar, D va de 1.100 a 1.166,7 y sigue hacia 1.300, y el far es 2.950 desde el segundo frame | *estático*; *emulado* |
| los efectos 299 y 300 del héroe | la cámara sigue la x y la y del efecto; la z, la del héroe | *estático* |
| las cámaras fijas del login y de la selección | las de [Cámara](#cámara). Como `0x635ae0` es la misma, el zoom que quedó del juego corre la cámara del login: con 210, a (12.636,6; 12.819,4), y baja de a 10 por frame (P-C27 b) | *emulado* |

**El ritmo** (*análisis*, con
[02](02-arranque-y-ciclo-de-vida.md#el-ritmo-lógica-a-25-por-segundo-dibujo-a-2133)): como mucho 25
frames por segundo, y casi siempre un paso por frame, a veces dos. El movimiento corre en el paso
(`0x636290` → `0x508f00` → `0x508ea0` → `0x4fd5f0` → `0x4fcd00` → `0x4fcbb0`, y `0x4f9580`) y la
cámara en el dibujo: el zoom, el suavizado de D y la marca del `0x40` van por frame dibujado, y el
temblor se achica por paso. En MuRet, `Render::MakePass3D`, `World::TerrainCulling` y
`World::StepGameCamera` ya sirven, y `GameScene::Render` los corre hasta el primer pase del terreno
([El dibujo del juego](#el-dibujo-del-juego)); lo que sigue falta ([08](08-pendientes.md) P-C41).

### Creación común de los objetos

`0x606660` calcula el sector con la posición de entrada antes de asignar y enlazar el objeto nuevo;
los defaults y las copias de posición preceden a la escena y al mapa. Conserva por separado la
posición activa (`+0x10`), la segunda copia inicial (`+0x17c`) y las copias del mapa 2 (`+0x28` y
`+0x34`). Los casos de las escenas 2/4 corren primero. El tipo del dispatch sigue siendo el argumento
original, aunque un efecto cambie el tipo guardado en el objeto; el mapa se lee después del efecto 180.

Los mapas 0/1/2/3/7/8/55 tienen ramas propias y sus tipos sin caso terminan allí. En el 1 se registran
59/60 y el 60 se oculta con caja alta. En el 2 se registran 22/25/40/45/55/73/91 y el 91 recibe la misma
caja y ocultamiento; los blends son 0 para 19/92/93, 1 para 54/56 y 3 para 78, y el 100 se oculta. Los
20/65/86/88 copian posición y ángulos: convierten angleZ con `_ftol` a signed64, toman **el dword bajo
con signo** y guardan su resto signed por 360 en angleZ y su copia. El port declara el dominio finito
`[-2^63,2^63)`; fuera de él no inventa el estado de excepción x87. El sector también es el dword bajo
con signo de `_ftol`, entre 0 y 15 (`0x60668d..0x6066ab`), y el NaN entra al dominio: da
`0x8000000000000000`, el sector 0, y el objeto se crea (Q-C186; `OriginalFtolLow` y `SectorOf`).

En el 3 los blends son 1 para el tipo 1, 3 para el 9, 0 para 17/19/37 y 2 para el 18; 8/38 se registran
y el 38 se oculta. En el 7 se registra y oculta el 39; en el 8 se registra el 78. El mapa 55 conserva
bucket y segunda posición inicial aunque reubique 86/90. El mapa 3, tipo 322, llama síncronamente a
`0x518cf0` con tipos 322/323/322/323/324, owner null y escala 0: antes de **cada llamada** relee la
posición viva, suma Y 0/4/8/12/20 y Z 350, prestando ángulos y luz del objeto. Hito 2 conecta
`MapModelEffectServices` a `LoadMap(3)` con el pool durable. Cada 322/323 copia la luz **antes** de
poner la luz del objeto en cero: tres sorteos, `%50` para escala `1,3f+0,01f*n`, `%80` para
`+0xe4=0,1f*n`, `%100` para velocidad `0,01f+0,001f*n`. El 324 usa escala 0,6 y ningún sorteo.
Quedan vida `0xffffff` y blendMesh−2; el resto no escrito del slot se conserva. Pool lleno cuenta
como binding ejecutado y no consume RNG ni muta la luz. Las cinco altas, con lugar, gastan 12 CRT
**antes** de los 65.536 del pasto; `MapLoadTest` contrasta ese orden con Data real. Esto resuelve la
frontera histórica de carga de Noria, sin acreditar todos sus pasos/dibujos.

El efecto 180 de las escenas 2/4 recibe owner objeto y escala 1,6, después de ocultarlo y poner su luz blanca; también corre
antes de leer el mapa. Una cola diferida altera el orden y las relecturas de ese contrato.

Los otros mapas llaman `405950`, `438a80`, `42c6b0`, `43ff70`, `42ee30`, `4250e0`, `4303d0`, en ese
orden. Sólo los primeros dos cortan por **AL exactamente 1**; los retornos restantes no se consultan.
`405950` acepta 18..23 para cualquier tipo y copia la posición a `+0x17c` y oculta 18..21 y 24..35;
`438a80` devuelve AL 0 sin accesos. Los demás conservan dependencias transitivas: por ejemplo,
`42c6b0` siempre llama `42b480` en mapa 30, y los wrappers `43ff70`/`42ee30`/`4250e0`/`4303d0`
consultan `43ff10`/`42ee20`/`425070`/`4303c0`, respectivamente.
Un bytecheck del wrapper no acredita esos callees ni autoriza sustituirlos por no-ops.

El constructor con contexto presta registro durable, mapa int32 mutable y servicios síncronos. Sus
`Unsupported` son límites **provisionales de integración**, con objetos, registro y efectos previos
conservados y sin procesar records posteriores; no son errores del original ni acreditan gameplay
39/39. Los overloads visuales anteriores omiten emisiones y cadena default, incluso el que recibe
registro. Esta unidad de creación no cierra paso, dibujo, carga de mundo ni CL7.

### Lo propio de Lorencia

Lo que el mapa 0 hace distinto en los ganchos de la escena ([Los cambios de
mapa](#los-cambios-de-mapa)), y la luz y el pasto que lo acompañan.

**Al crear** (`0x606660`, el `switch` del mapa 0 en `0x6069a3`, con la tabla de `0x606ed4` y
`0x606ea0`; *estático* y *emulado* con los 109 tipos del `.obj` en la escena 5). En la 5 no corren
los casos de las escenas 2 y 4 (el farol):

| Tipo | Qué cambia |
| --- | --- |
| `0x00`, `0x01` (`Tree01`, `Tree02`) | la caja de (−150, −150, 0) a (150, 150, 500) (`+0x124..+0x138`) y la velocidad 0,4 / escala (`0x6b87b4`) |
| `0x06`, `0x91`, `0x92` (`Tree07`, `Furniture06`, `Furniture07`) | se anotan en la lista de 200 de `0x7d34818` (`0x606630`; *inferido*: los lugares para sentarse, por el `PoseBox` y el cursor de `0x5ac421`) |
| `0x85` (`PoseBox01`) | en la misma lista, con la caja hasta (40, 40, 160) y oculto desde que nace (`+0x58 = −2`) |
| `0x34` (`Bonfire01`) | el blendMesh (`+0x64`) en 1 |
| `0x3b` (`TreasureChest01`) | la velocidad en 0 |
| `0x50` (`Bridge01`) | `+0xdc = −50` (*inferido*: un margen del frustum; abajo, y P-C35) |
| `0x5a`, `0x96` (`StreetLight01`, `Candle01`) | el blendMesh en 1 y la velocidad en 0,3 |
| `0x60`, `0x61` (`Sign01`, `Sign02`) | la velocidad en 0,3 |
| `0x62`, `0x77` (`Carriage01`, `House05`) | el blendMesh en 2 |
| `0x69` (`Waterspout01`) | el blendMesh en 3 |
| `0x75`, `0x7a` (`House03`, `HouseWall02`) | el blendMesh en 4 |
| `0x76` (`House04`) | el blendMesh en 8 |

Los demás tipos van con lo de siempre (la velocidad 0,16).

**Bridge01 sigue *inferido*** (2026-11-09). PR150 lo implementa como margen individual de recorte,
y `WorldObjectsTest.TheBridgeCullingMarginDependsOnItsMapNotItsScene`
(`Client/tests/world/test_world_objects.cpp`) lo fija como un hecho: el −50 en `+0xdc` sólo para el
`0x50` del mapa 0, y `+0xdc` como el margen de cada objeto en el recorte. Ese test fija una
inferencia, no una lectura. Falta la lectura estática: el store que el test atribuye a `0x6069bd` y
el uso de `+0xdc` en el recorte de objetos de `0x6046a0` en la escena 5 (P-C35 en
[08](08-pendientes.md)). Hasta entonces, la fila no se promueve.

El port de creación ofrece una ruta con `WorldObjectRegistrations` **preexistente del caller**:
200 entradas, primer flag cero, store de activo 1 y luego identidad, preservando metadata; acepta
null/duplicados y, llena, no escribe ni impide crear objetos. `0x606630` no calcula poses ni consume
RNG. Los IDs64 son identidad portable no poseedora, con 0 para null: el caller conserva unicidad,
contador sin overflow y vida de entidades; mover/realocar el vector no cambia IDs, y copiarlo es una
instantánea, no nuevas entidades. El tipo viene del int16 firmado del Data reader, no de todo el
dominio int32 nativo; records y estado deben estar estables y disjuntos. Los casos directos map0
terminan sin caer al mapa1 (el pseudocódigo sugería lo contrario); 133 registra antes de cambiar caja
y ocultamiento. Se modelan alive, cajas y segunda posición, sin el blob i386 ni bytes todavía sin
consumidor. Los overloads visuales son proyecciones; la ruta con contexto presta los servicios de emisión
síncrona ([Creación común de los objetos](#creación-común-de-los-objetos)).

**API comprobada del port:** los tres overloads devuelven `WorldObjectCreationResult`. El caller
comprueba `failure` antes de consumir `objects`; también las proyecciones conservan el índice,
el prefijo y el objeto actual parcial cuando la conversión de ángulo no está acreditada. Sus copias
pendientes no son una posición válida calculada. `InvalidAngle` expresa ese límite del port, no un
retorno nativo inventado; −2^63 sigue dentro del dominio. `LoginScene::Enter` propaga el diagnóstico.
La garantía de conservación se refiere a retornos por valor: `reserve` puede lanzar antes de efectos
y el provider de emisión exige retorno normal mediante `noexcept`, sin capturar ni traducir excepciones.
El binding y su servicio permanecen estables y vivos durante el batch; datos prestados se releen entre
emisiones. El factor usa `const reference_wrapper<const float>` con default de almacenamiento estático:
rechaza rvalues, no administra lifetime, y su valor se lee después del RNG, no al construir el contexto.
No se agrega un gate `InvalidScale`: los resultados IEEE de ±0/±infinito de los árboles se conservan,
sin acreditar por eso máscaras/traps o payloads NaN del original.

Esta creación no cierra L2 ni el orden/dueño de L1, paso o renderer.

**En cada paso** (`0x604ab0`, el `switch` de `0x604d68`; *estático* y *emulado*):

- **Los techos de la taberna**, `0x7d` y `0x7e` (`HouseWall05` y `06`), antes del `switch`
  (`0x604bc4`): la meta del alpha va a 0 si la capa 1 de la celda del héroe (`0x77f36d0`, que
  escribe `MoveHero` en `0x59e2ba`) vale 4, y a 1 si no. Con el héroe adentro se apagan (*inferido*:
  la capa 4 es el piso de la taberna; P-C31 e).
- **Los fuegos**, `0x52f360(clase, objeto, dx, dy, dz)`. Por llamada, un salto al azar de ±8 en x,
  y y z (tres `rand()`). Con la clase 0, la luz L = (`rand()` % 6 + 6) · 0,1 · (1; 0,6; 0,4)
  (`0x6b87b8`, `0x6b8a20`, `0x6b87b4`), con un 50 % la partícula `0x5d9` (el fuego) con el subtipo
  `rand()` & 3, y `0x5f9780(x, y, L, 4, 0x7b4ccc4)`; la clase 1 tira la `0x5f2` con el subtipo 0 en
  el 50 %, y la 2, con el subtipo 2. Quién los usa:

  | Tipo | Fuegos | Clase |
  | --- | --- | --- |
  | `0x32` (`FireLight01`) | uno en (0, 0, 200) | 0 |
  | `0x33` (`FireLight02`) | uno en (0, −30, 60) | 0 |
  | `0x34` (`Bonfire01`) | uno en (0, 0, 60), y la luz del blendMesh (`+0x68`) en (`rand()` % 6 + 4) · 0,1 | 0 |
  | `0x37` (`DoungeonGate01`) | dos antorchas, en (±150, −150, 140) | 0 |
  | `0x50` (`Bridge01`) | dos, en (90, ±200, 30) | 0 |
  | `0x82`, `0x83`, `0x84` (`Light01..03`) | uno cada uno | 0, 1 y 2 |

  Los `Light01..03` además pasan a `+0x58 = −2`: son marcadores, como los del 55, y se ven solo en
  su primer frame (Q-C148).
- **Las lámparas `0x5a` y las velas `0x96`**: una luz que parpadea, (`rand()` & 1 + 6) · 0,1 · (1;
  0,8; 0,6) y (`rand()` % 4 + 3) · 0,1 · (1; 0,6; 0,2) (`0x6b8a38`, `0x6b8a20`, `0x6b8854`), y las
  dos con `0x5f9780(…, 3, …)`.
- **`0x75` y `0x7a`**: la luz del blendMesh en (`rand()` % 4 + 4) · 0,1, las ventanas que titilan.
- **`0x76` y `0x77`**: la V del blendMesh (`+0x70`) corre con el reloj, −(`ftol(T)` % 1000) · 0,001
  (`0x6b89c8`).

**Después de dibujar cada objeto** (`0x602ed0`, desde `0x604a4d`, después de `0x602e90`; el caso del
mapa, en `0x602ffb`, después del `rand()` de `0x602ef4`; *estático*):

- `0x38` (`MerchantAnimal01`): dos sprites `0x5ac` en dos huesos del buffer global de la pose
  (`0x68b211c` y `0x68b22cc`, `0x603149..0x6031d6`). En la traza, los dos `0x5ac` están en (13.094;
  12.142; 283), sobre la NPC de la tienda (13.065; 12.151), y no sobre el animal (*verificado* el
  lugar). Los dos `0x4ec110` leen el punto de `[ebp − 0x28]`, que esta rama no escribe, y lo dejan en
  `[ebp − 0x34]`: el sprite sale de lo que quedó en la pila (Q-C256). Con el objeto posado, ahí quedó el
  marco de `0x6002e0` (el manejador `0x6b628b`, el nivel del try en −1 y su ebp guardado): la y es NaN y los
  dos sprites salen en NaN, de escala L · 5 y luz (L · 0,6; L · 0,3; L · 0,1). Con el héroe de la traza en
  (143, 120), MuRet no ve ningún `0x38`: los dos del `.obj` (los índices 195 y 2.236)
  están en (400; 14.700) y (18.300; 13.900), fuera del trapecio (*simulado*). Los dos `0x5ac` de la
  traza no se explican con este caso (P-C53 g).
- `0x69` (`Waterspout01`, `0x603011`, *estático*): la luz del blendMesh (`+0x68`) en 1; la V (`+0x70`)
  con el reloj del mundo, −(`ftol(T)` % 1000) · 0,001f, como el `0x76` del paso; y un `rand() % 2`:
  impar, nada más; par, dos partículas `0x5f2` (`0x53fe70(0x5f2, …, 0, 1, 0)`, `0x6030c7` y `0x60313a`)
  en los huesos 1 y 4 (`0x68b184c` y `0x68b18dc`, con `0x4ec110`), a (`rand()` % 32 − 16, −20,
  `rand()` % 32 − 16) y (…, −80, …) de cada uno. MuRet ya las crea con
  `Scenes::StepGameWaterspoutFrame`, el pool compartido de `WorldState::Particles` y los huesos de la
  pose global prestada por `GameObjectPass.retainedPose`, aun con alpha bajo o huesos dummy:
  `0x4ec110` recibe Translate=0 y lee la matriz exacta anterior sin aplicar el transform actual.
  [GameParticlesTest](../../tests/scenes/test_game_particles.cpp) cubre altas, matrices retenidas,
  RNG, pool lleno y el prefijo si falta el segundo hueso. Un préstamo vacío corta tras los dos
  sorteos de la primera gota; no inventa historia fría. El contraste nativo de sus puntos sigue
  pendiente (P-C55 a).

**Al dibujar** (`0x6002e0`): el único caso del mapa 0 es el `0x69`, con cuatro `RenderMesh(malla, 2,
alpha, 3, luz, …)`, la 3 con la U y la V, en vez de un `RenderBody` (*emulado*). Por dentro
(`0x600a4a..0x600ae6`, *estático*): `0x4ecc10(alpha)`; las mallas 0 a 2 con el blendMesh y la luz del
objeto (`+0x64`, que el alta pone en 3, y `+0x68`) y U = V = 0; la 3 con el blendMesh 3 fijo, la luz y
la U y la V del objeto (`+0x6c`, `+0x70`); y `0x4ecc20`. MuRet lo dibuja así
(`Scenes::DrawWaterspout`): cada malla con el pase base de `DrawObject`, sin el `glColor`, la malla oculta
(`+0x58`) ni los scripts R y H, que mira `RenderBody` y no `0x4ecc30` (`Waterspout01` no tiene ninguno).
La malla 3 (`ston02`) va aditiva, como en la traza ([El dibujo del juego](#el-dibujo-del-juego)); los
demás bloques de la traza no se cotejan (P-C53 h).

**Los pájaros** (`0x446120`, la tabla `0x7d2dca8`; el dibujo, `0x446b60`, con su sombra): en el 0
usan el modelo `0xad` (`Bird01`), solo sin tormenta (`0x723c50 == 0`), nacen al azar y se posan
(*estático*). En la traza hay 5, con sus 5 sombras (*verificado*). El ASM fija el tope en **5**:
`ESI` empieza en `0x7d2de18`, suma `0x1d4` por lugar y sale con `ESI >= 0x7d2e73c`
(`0x44642a/430`, `0x446a57`): índices 0 a 4 de `0x7d2dca8`. En 90 observaciones nativas de la
primera serie se vieron 5 vivos 85 veces y 4 las otras 5; el conteo observado no es la prueba
del límite estático. MuRet ya porta nacimiento, evolución, servicios y dibujo de esas familias
([La fauna y sus recursos](#la-fauna-y-sus-recursos)); los tests del port no sustituyen el contraste
temporal nativo de P-C28 j. La tormenta (`0x723c50`, que el `1C` apaga) tira rayos `0xc2` con el
sonido `0x42` y oscurece la luz del terreno alrededor del héroe.

**Los peces** (`0x447030`, la tabla `0x7d35350`; el dibujo, `0x446f00`, con su sombra `0x4eff40`):
en el 0, hasta 3 `0xb2` (`Fish01`), a ±512 del héroe y solo en celdas con la capa 1 en 5, el agua
(`0x5f82a0`; *estático*). MuRet usa el pool durable y los servicios productivos de
[fauna](#la-fauna-y-sus-recursos), con el sorteo del candidato incluso si esa celda lo rechaza.

**Las hojas** (`0x530490`; el dibujo, `0x5306a0`): **80 en Lorencia** (*estático*, y las 80 con la
textura `0x64`, *verificado* en la traza). La cuenta de partida es `(-(mapa != 9) & −120) + 200`
(`0x530497..0x5304a3`): 200 en el mapa 9 y 80 en los demás, y después cambia en algunos
(*estático*): 40 en el 30 (`0x5304da`); en el 34, 60 o 50 con `0x5763358` en 1 o en 2 (`0x530500`,
`0x53050c`; el clima de Crywolf, que sortea `0x430510`, [Los objetos de Crywolf](#los-objetos-de-crywolf)); y en el 10 y del 18 al 23, 80, con la meta de la
lluvia (`0x75fdc38`) en 100 en cada paso. En el 0 nacen con `0x52fa60`, el tipo 100 (`leaf01`),
alrededor del héroe (*estático*), y avanzan con `0x5301a0` y `0x5303a0`: caen y se posan en el
terreno (`0x5f93c0`). **No corren con el héroe adentro de la taberna** (la capa 1 en 4,
`0x637082..0x6370e5`). La cuenta de la lluvia, `0x75fdc3c`, se acerca de a 1 por paso a `0x75fdc38`,
la meta que escribe el `0F` ([04](04-protocolo.md#el-clima-y-crywolf-0f-y-bd)). La lluvia (`0x66`)
solo nace en Icarus (`0x52fc10`): en Lorencia, el `0F` de lluvia prende el sonido de ambiente y nada
más (*estático*; P-C31 d).

**La música y el ambiente** (`0x6387ed..0x639422` y `0x6386a9`, en `0x637700`; *estático*, de la
CL10): en Lorencia, `main_theme.mp3` con el héroe en la zona segura y `Pub.mp3` con la capa 1 de su
celda en 4; el ambiente 0 afuera de la taberna y el 1 (la lluvia) con `0x75fdc3c` en más de 0.

**La luz dinámica del terreno**, `0x5f9780(x, y, L, r, buffer)` (*estático*). Recorre el cuadrado de
celdas ±r alrededor de (x, y) · 0,01 y suma L · (r − d) / r a cada canal donde da positivo, con el
piso en 0, sobre `0x7b4ccc4`. Ese buffer lo rearma `0x5fbf60` en cada paso desde la luz sombreada,
en la ventana del rango ([La luz del terreno en un punto](#la-luz-del-terreno-en-un-punto)), y lo
leen el terreno (`0x5fa260`, `0x5fa350`) y `0x5f99e0` (los objetos y los personajes). **Es lo que
sube los vértices de [Terreno](#terreno)**: de los 27 vértices distintos de la traza más brillantes
que el OZJ, 23 rodean el `FireLight01` del registro 1933, en (145,5; 112,5), con un factor de 1,1 a
3,1 que baja con la distancia, dentro del cuadrado ±4 (*verificado* por el lugar; la cuenta,
*estático*). Los otros 4, en la fila 136 (x de 141 a 144), no caen en el ±3 del `StreetLight01` del
registro 1744 (143,07; 140,44): sin identificar (P-C28 e).

**El pasto** (*estático*; la traza, *verificado*). Es el segundo `0x5fc190(0)` de `0x5fc260`, con
`0x7d0d31c = 2`, y lo dibuja `0x5fa830`: un `GL_QUADS` por celda con las cuatro esquinas en alfa ≤
0, con `0x7b4ccb4` en 0, fuera de los mapas 11 a 17 y con la textura `0x32` + capa 1 cargada. Mide
de alto el doble que la textura (128); arriba se corre −50 en x y la ola (`0x78acbd8`) en y; la u es
x · 0,25 más el corrimiento de su fila, `0x7b0cca4[y & 0xff]` (`0x5faabf..0x5faad3`): de los 65.536
que llena `0x5f8300`, el pasto lee los 256 primeros (Q-C146). El color sale de `0x7b4ccc4`. En la
traza son 25 quads con `TileGrass01.tga`, de z 165 a 293, con la x de arriba corrida 50 (líneas
7.882 a 8.218). En el login no dibuja nada ([El terreno del login](#el-terreno-del-login)).

Por dentro (`0x5fa9f6..0x5fac7b`, *estático*):

- **Qué celdas.** `0x5fac90` ya salteó las del bit `0x08` y `0x5fa830` llamó a `0x405e00(x, y)`
  (`0x5fa83f`, antes de mirar `0x7d0d31c`; fuera de los mapas 18 a 23 vuelve sin hacer nada, Q-C229).
  Cada esquina en alfa (`0x7ccccc4`) se compara con 0 (`fcomp` y `test ah, 0x41`): sigue solo con las
  cuatro en 0 o menos. Después `0x7b4ccb4`, que nadie escribe (Q-C226), y el mapa (`0x5faa79`).
- **La textura.** El slot es `0x32` + la capa 1 de la esquina (x, y) (`0x5faa8c`), y la celda sigue
  solo si el buffer del slot (`+0x34` de `0x7d3a198`) no es 0 (`0x5faaa3`). `0x61a560` sube
  `World<N>\TileGrass01..03.tga` en `0x32..0x34` con `0x63bd00`, que no avisa si falta; `0x6070a0`
  suelta `0x23..0x67` en cada carga y nadie sube del `0x35` al `0x40`: la capa 1 de 3 a 14 no tiene
  pasto (Q-C230). Del `0x41` en adelante están las `wt` de `0x616ec0` y lo que sube después, que MuRet
  no tiene. `World1` trae solo `TileGrass01.OZT`, `World3` solo `TileGrass02.OZT` y `World55` los dos
  primeros.
- **El quad.** El alto es el doble del de la textura (`+0x24`, `fadd st, st`, guardado en un float).
  `0x5fac90` dejó las cuatro esquinas en `0x79ecc30..0x79ecc5c` (con el bit `0x40`, la altura fija);
  `0x5fab27..0x5fabbc` las rearma: la 0 sube el alto, se corre −50 en x y suma en y la ola de la esquina
  0; la 1 toma la x − 50, la y y la z de la esquina 2 (x + 1, y + 1), con el alto en z y la ola de la
  esquina 1 en y; la 2 queda; la 3 toma la x, la y y la z de la esquina 0. Es un cuadro parado sobre la
  diagonal de la celda, inclinado hacia −x. Las UV: (u, 0), (u + 0,25, 0), (u + 0,25, 1), (u, 1),
  cada suma guardada en un float. El color de cada vértice es la luz de la esquina 0, 1, 2 y 3 en ese
  orden (`glColor3fv`, alfa 1), aunque el 1 está sobre la esquina 2 y el 3 sobre la 0 (Q-C227).

**Lo que porta MuRet** (CL7, hito 1). `World::DrawGrassCell` y `World::DrawGrass` (el recorrido de
`0x5fc190`, el mismo de `DrawTerrain`), con las alturas de los buffers que sube `TerrainTiles` en el
juego y el corrimiento de `WorldMap::grassOffsets`. Sin la textura o con la capa 1 de 3 a 14, la celda
no lleva pasto, como en el original; con la capa 1 en 15 o más corta con error (`0x5faaa3`), y sin las
256 filas del corrimiento (`0x5faad3`). Tests: `TerrainGrassTest` (el quad, la luz, la ola, cada
condición y el recorrido), `WorldTerrainTest.TheGrassComesAfterTheTiles` y, en Lorencia y Devias,
`GameSceneDrawTest.TheReferenceMapsDrawTheTerrainTheGrassTheGroundMarksAndTheObjects`: en Lorencia, con el
héroe y la cámara de la traza, los 25 quads de z 165 a 293. Con la Data del repo, la capa 1 de las
celdas que pasan las condiciones del alfa y el `0x08` va de 0 a 13 en los mapas que carga el juego
(*simulado*, la cuenta): ninguno llega al corte del `0x5faaa3`.

### Caminar

Del clic en el piso al paso entre celdas, y lo que manda y recibe la red. *Estático* salvo lo que se
marca; lo *emulado* corre el código real sobre el `.att` y las alturas de Lorencia, y el cable es el
`muproxy.log` (229 `A3`, 1.122 `D3` del servidor y 614 `D7`). Los formatos, en
[04](04-protocolo.md#caminar-y-pegar-d3-y-d7).

**Dónde corre cada cosa** (*estático*):

- `WinMain` despacha **todos** los paquetes de la cola antes de `0x637700`
  ([03](03-red-y-cifrado.md#sockets)): un `D3` que llega se aplica antes de los pasos del frame.
- En cada paso, primero la UI del juego (`0x623e70`, que consume muchos clics: `0x7d366d4` tiene más
  de 100 escritores) y después `0x636290`, con `0x59adf0` (el `MoveHero` de MuMain: el clic, el
  camino y el paso del héroe) y `0x508f00` (el `MoveCharactersClient`: las celdas ocupadas y el paso
  de los 400 personajes), en ese orden.
- En el dibujo, `0x620480(mouse, &0x7d366c0, 1)` arma el rayo del mouse (`0x6374aa`) y, después del
  terreno y de los personajes, `0x59ec80` hace el picking de los personajes. **El paso usa el rayo
  del último dibujo**: la cámara `0x7d36834`, el punto lejano `0x7d366c0` y los rangos del frustum
  (*inferido* del orden, Q-C165).

**El rayo contra el terreno.** Es el camino del dibujo, en modo picking:

```text
0x5fc260(1)
 ├─ 0x78acbd4 = 0: SelectFlag
 ├─ 0x7d366d0 = 9.999.999f (0x622430): maxT, parámetro del segmento
 ├─ 0x5fc190(1)  blockY → blockX, de a 4, frustum en centro +2 y margen -40
 │   └─ 0x5fc0e0  row → col, las 16 celdas, frustum en centro +0,5 y margen 0
 │       └─ 0x5fac90(xf, yf, xi, yi, 1.0f, 1, 1): siete argumentos
 │           ├─ 0x5fd000  normal de (v0, v1, v2)
 │           ├─ 0x622440  cara (v0, v1, v2); sólo si falla, normal/cara (v0, v2, v3)
 │           └─ si toca: maxT → hit.xyz → SelectFlag=1 → SelectXF/YF
 └─ si SelectFlag: retry de la celda seleccionada, sin reset; convierte Y antes de X
```

- Los vértices son los del dibujo, con la altura fija `0x6c9d5c` en una esquina con `0x40` y el
  índice sin dar la vuelta en la fila (Q-C141).
- `0x622440` es el `CollisionDetectLineToFace` de MuMain: el t del segmento cámara → punto lejano
  va de 0 a `0x7d366d0`, sin límite literal `t<=1`: comprueba bounds inclusivos de dos ejes,
  omitiendo el de **menor** |delta| (empate x, luego y). Rechaza backface/paralelo. La prueba de
  adentro (`0x622170`) proyecta por prioridad x, luego y, si no z; ±0,5 seleccionan ese eje, sin
  buscar el mayor componente. Los bordes, vértices y diagonal son estrictos. Al tocar publica
  maxT y punto; **gana el menor t entre las caras efectivamente intentadas**, con empate a favor
  de la posterior. El barrido no se corta tras el primer hit.
- Después `MoveHero` hace **otro retry incondicional** a `0x5fac90(SelectXF, SelectYF, …)`
  (`0x59de39`), incluso tras miss del pase, sin reset ni consulta de SelectFlag. Sólo el AL de
  ese intento habilita publicar `TargetX = ftol(hit.x · 0,01f) & 0xff` y la Y análoga, en DWORD
  (`0x77e1ba8`, `0x77e1bac`). Convierte X, prepara producto Y, guarda X, convierte/guarda Y.
  La constante tiene bits `0x3c23d70a`; los productos quedan en 53 bits antes del trunc signed64,
  sin spill float32. Si falla, conserva TargetX/Y y DontMove.
- Con `Flag` en 1, `0x5fac90` **no saltea las celdas con `0x08`**, las que no se dibujan: se puede
  caminar clickeando terreno invisible (Q-C163). En Lorencia no hay ninguna (*verificado*: sus
  valores son 0, 1, 2, 4 y 5).
- Si el rayo no toca ninguna celda visible, `SelectXF/YF` quedan con la del clic anterior y se
  prueban igual en el consumidor (Q-C164). El pase deja SelectFlag=0 y maxT reiniciado; conserva
  hit y XY. El retry interno sólo ocurre tras hit. Los fallos de retry no hacen rollback.
- `DontMove` (`0x77f3b5c`) se prende con el atributo ≥ 8 sin `0x20` ni `0x40`
  (`0x59de87..0x59deac`), pero solo lo usa el cursor: no frena el camino (Q-C162).

CL7.4D implementa esta transición pura en `World/TerrainPicking`: estado explícito, errores de
recursos/rango/dominio, dos operaciones (pase y consumidor con target/cursor) y frustum existente.
Los cuerpos citados y sus helpers se cotejaron estáticamente contra PE32 c32bbd5f (1.017
instrucciones/2.970 bytes; consumidor 80/326; `ftol` contratado aparte), sin nuevas ejecuciones ni
emulaciones. El contrato C++ es PC53/nearest, stores float32 y sin FMA; no demuestra la CW del
runtime nativo. Bloques 0..251 generan celdas 0..254 y esquinas 0..255; alturas byte×1,5/3 y
fixedHeight1200/1201 garantizan normales definidas (nz=10000), sin fallback ni stack residual.
La API admite además selección retenida fraccional acotada, con anchos positivos; sus defaults
son de MuRet, sin atribuirlos al startup nativo. Tests analíticos, Data de cuatro familias y cámara
de Lorencia verifican el componente; `GameScene`, reglas del clic y caminar siguen en CL7.4E.

**El clic, `MoveHero` (`0x59adf0`):**

- **No hace nada** (`0x59ae24..0x59afd6`) si `0x4033d0()` da verdadero (el recorrido de la cámara:
  lo para con `0x4f2250`), si hay un personaje bajo el mouse y el héroe tiene `+0x7c & 0x200000`, si
  se está muriendo (`+0x389`) o tiene `+0x78 & 0x1000`, o si el contador `0x77f3b04`, que baja 1 por
  paso, pasa de 30. `0x4033d0` es verdadero con `[obj + 0x158] == 0`, `[obj + 0x6a] == 0` y
  `[obj + 0x154] == 2`, con `obj = [0x6e5904]`; `0x511290` escribe `+0x389 = 0` (`0x511c05`) y
  `+0x78` (`0x511d51`). Esos cuatro salen sin tocar el contador (`0x59ae24..0x59ae87`). El contador
  va en dos accesos (*estático*, T5): el primero (`0x59ae8d..0x59af5a`) le resta 1 si es positivo
  (`test`/`jle`, `0x59af01`) y cierra con `0x412890`; el segundo (`0x59af5f..0x59afd1`) lo lee y
  cierra con `0x40b800`, y `0x59afd6` compara con 30 (`jg` al epílogo, `0x59e2c0`). Cada cierre que
  deja la cuenta en 0 gasta 4 `rand()`. Con más de 30 sale antes de `0x57cfc0` (`0x59e255`).
- **Si el héroe camina** (`+0x378`, `0x59b295..0x59b304`; *verificado* el cuerpo, abajo): con
  `+0x78 & 0x20`, `SetPlayerStop` (`0x4f2250`); si no, `SetPlayerWalk` (`0x4f37c0`) y
  `MovePath(héroe, 1)` (`0x4ea270`). Al llegar, `+0x378 = 0`, `SetPlayerStop`, `0x77f3ba8 =
  ftol(ángulo)`, `0x77f3ba4 = 0` y `0x5650c0(héroe, héroe, 0)`, lo que toque al final del camino
  (pegar, levantar, hablar); si no llegó, borra el `0x200000` de `+0x7c` y llama a
  `MoveCharacterPosition` (`0x508a80`).
- **La cabeza del héroe** mira al mouse (`0x59b1c2..0x59b287`): `+0x34` es el ángulo que da
  `0x4e93f0` del punto (w / 2, 180) de la pantalla al mouse (w, el ancho del mundo de `0x5bb8d0`),
  más el yaw del héroe y 315, módulo 360, acotado de 120 a 240, más 180 y módulo 360; `+0x38` es
  (180 − y del mouse) · 0,05. No corre con `+0x78 & 0x1000`, con la acción `0x3e` ni con el
  auto-ataque y un blanco elegido, salvo en los mapas 6 y 18 a 23 (P-C17 a).
- **Los gates**: después de eso, y antes del clic, `0x599ef0` mira si el héroe pisa uno (`0x59b841`,
  [Los cambios de mapa](#los-cambios-de-mapa)).
- **El clic** (`0x59b846..0x59b928`): con `0x77f3b18` o `0x775857c` prendidos (`0x636290` prende
  el segundo con el mouse en la barra de abajo, y > `0x1af`) saltea todo y `MouseUpdateTime`
  (`0x77f3b10`) no sube (Q-C168). Solo con `MouseUpdateTime ≥ MouseUpdateTimeMax` (`0x6c8dcc`) y
  `0x77f3bac` en 0 mira el mouse: el flanco `0x7d366d4`, que consume; el botón abajo, `0x7d36874`; o
  el auto-ataque (`0x6c8e3c`, con `0x6c8e38 == 1` y un personaje bajo el mouse, fuera de los mapas 6
  y 18 a 23). Un clic dentro de la ventana de `MouseUpdateTimeMax` queda pendiente, porque
  `MoveHero` no lo borra (Q-C166). Con un botón abajo una hora (`(T − 0x77f3b44) · 0,001 ≥ 3.600`)
  suelta los dos (`0x59b8e7..0x59b921`, Q-C167).
- **La acción del héroe** (`0x59babe..0x59bb1f`): sigue si la acción (`+0x111`) no es `0xc2` (el
  golpe), `+0x80` no es 1 ni 2, el alpha es ≥ 0,7 y la acción es < `0x22` o > `0x97`; o si la acción
  está en `0x48..0x4b`, `0x6a..0x7d` o `0x8a..0x8c`. Si no, el clic espera: pegando no se camina. El
  tramo del ítem en el cursor (`0x59bb25..0x59cd28`) lee las manos del registro propio
  (`0x59bbe9..0x59bbf0`: la derecha en `+0xb6c`, la izquierda en `+0xbb4`). Solo con el cursor vacío
  (`[0x7879fbc]` ≤ 0), la derecha vacía (−1) y en la izquierda un arma de los grupos 0 a 2 o del 5
  (`0..0x5ff`, `0xa00..0xbff`) pasa esa arma al cursor (`0x59bdf6`), viste al héroe (`0x512a30`) y
  manda el `C1 .. 24` que la lleva a la derecha (`0x59bf1c`, CL8; MuRet corta en `0x59bd3e`). Con
  cualquier otra cosa en las manos, por ejemplo la espada en la derecha y la izquierda vacía, sigue en
  `0x59cd23`.
- **Qué hay bajo el mouse**, desde `0x59cd28` (con `MouseUpdateTime = 0`; `+0x379` es el
  `MovementType`):

  | Bajo el mouse | Dónde | Qué hace |
  | --- | --- | --- |
  | un personaje atacable (`0x558bb0`), con el héroe fuera de la zona segura | `0x59d3de` | `+0x379 = 3`, el blanco en `0x6c8ebc` y el camino hasta él, o `0x5650c0` si está cerca; un elfo que camina manda antes un `C1 05 DF X Y` (*verificado* uno en el cable) |
  | un objeto para usar (`0x6c8e34`, la tabla `0x7d34820`) | `0x59d8c3` | `+0x379 = 4`, si la celda tiene `0x40` o vale menos de 2 y el héroe está quieto |
  | un NPC (`0x6c8e2c`), con las ventanas `0x10000` y `0x40` cerradas | `0x59da75` | `+0x379 = 2` |
  | un ítem del piso (`0x6c8e28`) | `0x59dc5e` | `+0x379 = 1`; si el camino falla, `0x5650c0` (levantarlo ahí) |
  | nada de eso | `0x59ddef` | el terreno, abajo |

  En la zona segura, un clic sobre un monstruo o un jugador no ataca: cae a las filas de abajo y, si
  no hay otra cosa, al terreno (`0x59cd2e`).
- **El terreno** (`0x59ddef..0x59e248`): con Shift abajo (`GetAsyncKeyState(VK_SHIFT)`, `0x59ddf1`)
  no camina (Q-C169). Si el picking toca, saca el blanco. Si el héroe camina y su celda lógica
  (`+0x41c`, `+0x420`) está a 2 o más de la que se ve (`ftol(posición · 0,01)`), no hace un camino
  nuevo; si camina y la celda lógica ya es el blanco, `MouseUpdateTime = 0`; si no, si
  `PathFinding2(celda lógica, blanco, &+0x3e8, 0)` sale bien, `+0x379 = 0` y `SendMove`. Al final,
  `MouseUpdateTime++` (`0x59e248`), `0x57cfc0`, y la capa 1 de la celda del héroe a `0x77f36d0`
  (`0x59e2ba`), la que miran los techos de la taberna, las hojas y la música.
- **Apretado**, el camino se recalcula cada `MouseUpdateTimeMax + 1` pasos; con el mouse cerca del
  héroe (camino de menos de 3, tope 0), en cada paso, y sale un paquete solo cuando cambia la celda
  del blanco con el héroe a menos de 2 celdas de la suya: en la práctica, **uno por celda**. En el
  cable, `A3` de un paso, uno por celda, unos 3 por segundo (*verificado*).

**El camino**, `PathFinding2` (`0x4eac00`) y el A* (`0x4ead70`). El objeto `PATH` lo arma `0x4eaaa0`
(`new(0x424)`, en `0x5767b74`) y lo inicializa `0x4eaaf0`, desde el cargador del mundo (`0x61bd80`):
256 × 256 celdas; el mapa `0x7d0d348`, un WORD por celda (el `TerrainWall`); `PathX[500]` y
`PathY[500]` (`+0x14`, `+0x208`); `NodeFlag` (1 abierto, 2 cerrado, 4 meta); el mínimo y el máximo
tocados, para limpiar rápido (el centinela es 1.950.000.000); el costo desde el inicio; el padre en
x y en y; y la lista abierta (la vtable `0x6ba068`, la cantidad y la raíz).

`FindPath(sx, sy, tx, ty, bErrorCheck, iWall, bClearGoal, fDistance)` (`0x4ead70`, `thiscall`, todo
con enteros salvo la rama de `fDistance`, que caminar no usa):

- **Sale con falso con `sx == 0` o `sy == 0`** (`0x4eadc1..0x4eadce`, Q-C156; *emulado*).
- **La meta**: con `bClearGoal` pone `Map[meta] = 0`, **escribe el mapa** (solo el asedio,
  `0x435a00`, en el mapa 34 con un blanco de tipo `0x204..0x208`); con `bErrorCheck`, una meta con
  `Map ≥ iWall` sin el `0x20` falla; si no, `NodeFlag[meta] = 4`.
- **El tope de vueltas**: 500 con `bErrorCheck` y 50 sin (`0x4eb0dc`, Q-C157).
- **La lista abierta** es un árbol binario sin balancear de nodos de `0x14` bytes: inserta
  (`0x4eb770`) a la izquierda con la clave menor y a la derecha si no (los iguales, a la derecha), y
  saca el de más a la izquierda (`0x4c0dd0`). **El orden de los empates sale de ahí** (Q-C161).
- **Cada vuelta** saca el mínimo, le recalcula el costo desde los vecinos cerrados (+5 en recta, +7
  en diagonal, 0 al inicio) y lo cierra. Si es la meta, arma el camino hacia atrás (`PathX[499 −
  n]`, con un tope de 500).
- **Los vecinos**, en este orden (`0x6d5d80`): (−1, −1), (0, −1), (1, −1), (−1, 0), (1, 0), (−1, 1),
  (0, 1), (1, 1). Entra el que no está abierto ni cerrado y tiene `(Map & ~0xe0) < iWall`, con la
  prioridad **h + el costo del padre**, no el suyo. h = (|dx − dy| · 15 + 3 + min(dx, dy) · 21) / 4,
  en enteros, salvo con dx = dy = 1: ahí `0x4eb2c2` pone en 0 la dy, no la h, y da (15 + 3) / 4 = 4,
  lo mismo que la vecina en recta, (1, 0). **La celda diagonal a la meta vale lo mismo que la de al
  lado** (`0x4eb2b9..0x4eb2c2`, Q-C159; antes este documento decía que daba 0).
- **Sin `bErrorCheck`** guarda el sacado de menor h y, si se acaban las vueltas, devuelve el camino
  hasta ese: "el más cerca que encontré".

`PathFinding2(sx, sy, tx, ty, &path, dist)` (`0x4eac00`, *estático* y *emulado*):

1. Prueba el exacto, `FindPath(…, 1, 2, bClearGoal, dist)`: 500 vueltas, y bloquean las celdas con
   `(Map & ~0xe0) ≥ 2`, otros personajes (`0x02`) y paredes (`0x04`).
2. Si falla, el "más cerca", `FindPath(…, 0, iWall, …)`, de 50 vueltas, con `iWall = 4` si el
   inicio o la meta están en la zona segura y la meta no está ocupada, y 2 si no: **en la zona
   segura el segundo intento camina a través de los otros personajes** (Q-C160).
3. Pide `NumPath ≥ 2` (con 1 falla: el clic en la propia celda) y copia **las primeras 15** celdas
   (`PathNum = min(NumPath, 15)`, `0x4eacef..0x4ead53`) en `path`: `CurrentPath` en `+0`,
   `CurrentPathFloat` en `+1` (los dos en 0), `PathNum` en `+2`, `PathX[15]` en `+3` y `PathY[15]`
   en `+0x12`. En el héroe, `path` es `+0x3e8`.

Los topes y los bordes (*emulado*, con un mapa vacío y con el `.att` de Lorencia):

| Caso | Resultado |
| --- | --- |
| en recta | el exacto llega hasta 30 celdas (`NumPath` 31) y a las 40 falla por las 500 vueltas |
| en diagonal | llega a 20 y a las 30 falla |
| después de fallar | el "más cerca" da caminos de 10 u 11 celdas |
| un camino de más de 15 | se camina el principio: en Lorencia, de (135, 125) a (120, 110), 14 pasos hasta (123, 112), todavía a 3 celdas |
| el blanco ocupado (`0x02`) | el exacto falla y el "más cerca" termina en la celda **diagonal** al blanco, no en la de al lado: las dos tienen h = 4, y decide el orden de los empates |
| un pasillo cortado por personajes | en la zona segura el segundo intento pasa por encima; fuera, se queda antes del corte |
| un clic sobre una pared | de (130, 130) a la (160, 160), con el atributo 4: camina 10 pasos hacia ahí |

**Contra el cable**: de los 178 `A3` con algún paso (229 en total), 171 tienen en el nibble alto de
`P0` la dirección del último paso, así que el blanco es la última celda (*inferido*: el cable no
trae el blanco), y **169 de esos 171 dan el mismo camino** que el A* real emulado sobre el `.att` de
Lorencia, del inicio a esa celda, sin ningún otro personaje (*verificado*). Los 2 distintos (`[7, 6,
7]` contra `[7, 7, 6]` y `[3, 2]` contra `[2, 3]`) son lo que da una celda ocupada por otro
(`0x02`), que el cable no muestra (*inferido*, P-C30 h). Toda la caminata de la captura es en
Lorencia.

**El `TerrainWall` vivo** (`0x508f00`, *estático*): en cada paso, antes del de los personajes, le
saca el `0x02` a las 65.536 celdas, y después se lo pone a la celda lógica de cada personaje vivo
(`+0` distinto de 0, `+0x389` en 0 y `+0x88` distinto de 8), **el héroe incluido**, y al cerrar las
dos celdas cifradas gasta 8 `rand()` por personaje ([La tabla de
protección](#la-tabla-de-protección)). El camino del héroe (en `MoveHero`, que corre antes) ve las
del paso anterior. El `.att` de Lorencia trae una celda con el 2 en el archivo, la (221, 11), que se
borra en el primer paso (Q-C170; *verificado* el dato).

**Estado del port, CL7.4 A:** `World::TerrainWalls` reproduce la limpieza y el marcado con celdas
ya decodificadas; `GameLogic::PathFinder` reproduce la búsqueda de caminar con distancia cero y
el resultado limitado a 15 celdas. El contraste estático incluye los bytes de `0x4eac00`, `0x4ead70`,
`0x4eb770`, `0x4c0dd0`, `0x508f00` y `0x5f82a0`, además de los vecinos de `0x6d5d80`.
Los casos literales de los tests distinguen los topes inclusivos, el orden de empates y los efectos
de `clearGoal`. No se generaron nuevos vectores emulados ni se repitió la comparación histórica de
los 171 caminos. Faltan la rama por radio, el consumidor de las coordenadas protegidas y su RNG,
la actualización del mapa vivo en la escena y la integración clic → camino → paquete → paso.

**El paquete, `SendMove` (`0x56acd0`).** Sale una vez por cada `PathFinding2` que sale bien en
`MoveHero`, al hacer clic o al recalcular apretado: no por paso ni por celda. Con la ventana
`0x8000` abierta o con `+0x78 & 0x20` no hace nada. Pone `MouseUpdateTimeMax` según `PathNum`: 0 con
menos de 3, 5 con 3 y 3 · `PathNum` + 4 con más (49 con 15). El paquete (`D3`, con el tope de 14
pasos, Q-C158) está en [04](04-protocolo.md#caminar-y-pegar-d3-y-d7). Después pone `+0x378`
(`Movement`) en 1, salvo con `0x7d3a10c == 0x70`; cierra las ventanas `0x20`, `0x40`, `0x40000`,
`0x100`, `8`, `0x2000`, `0x10000`, `0x80000` y `0x1000` (`0x65cae0`: caminar cierra ventanas, CL8);
y con `0x78abb00` manda `C1 03 97` y mueve el cursor (CL8, CL9). Con `PathNum` en 0 no manda, pero
pone `Movement` igual (Q-C177; nadie lo llama así).

**Lo que llega** (*estático*; los formatos, *verificado* en el cable):

- **El `D3` del servidor** (`0x4c6790`) busca al personaje con `0x5110d0(id)`, que devuelve 400 si
  no lo encuentra (Q-C174); si está muriéndose (`+0x389`), sale **antes** de poner
  `+0x388 = pkt[7] >> 4`. Al héroe, si está quieto, le pone
  la celda lógica en (X, Y), sin tocar la posición que se ve (Q-C176); si camina, solo la dirección.
  El servidor contesta cada `A3` con un `D3` del héroe con la celda final del camino que le mandaron
  y la dirección de `P0` (*verificado*: los 212 ecos que se encuentran en el cable). A los otros, el
  blanco a `+0x39a`/`+0x39b`; el tipo `0x186` copia la celda lógica, calcula el ángulo hacia
  **`(X*100,Y*100)`, sin +50**, llama `0x4f37c0` y pone `+0x399 = 1`, sin saltar su XY visible;
  los demás, con `+0x3e5` en 0 (un freno de 60 pasos que pone
  `0x4c9790` y baja `0x505de0`), calculan `PathFinding2(celda, blanco, +0x3e8, 0)` con **su** A*:
  `Movement` es el resultado y, si falla, `SetPlayerStop`.
- **El `D7` del servidor** (`0x4caae0`) **no es movimiento: es el daño**. Dibuja el número
  (`0x548e40`, escala ordinaria 15) y lo guarda en `+0x384` **también en el héroe**; al héroe le
  resta la vida
  (`0x7681eb0 + 0x22`, con piso en 0, y cierra el registro propio con 3.848 `rand()`, Q-C183). **Con
  el bit 15 del id llama siempre a `SetPlayerShock`** (`0x4f53a0`, `0x4cab6b`), también con el
  héroe. Sin el bit, al héroe nunca, y a los otros, salvo los de `+0x376 == 0x113`, **una vez de
  cada dos**: si `rand() & 1` da 0 (`0x4cb3bd..0x4cb3e8`).
- **`SetPlayerShock` (`0x4f53a0`)** no hace nada con el daño en 0 o menos, con `+0x389` (muriendo),
  montado en `0x1c14`, `0x1c15` o `0x1c16`, o en las acciones `0x3e`, `0x3f` o `0x44`. Si no, un
  jugador (`0x20a`) toma la acción `0xc2` y **`Movement = 0`** (`0x4f546c`): deja de caminar a mitad
  de camino, con la celda lógica ya en la siguiente (Q-C178). Con Fenrir (`0x1c37`) llama a
  `0x4e9cd0` en vez de la acción, pone `Movement = 0` igual (`0x4f5455`) y, si `rand() % 3` da 0
  (`0x4f5423`), suena el `0x27b` + `rand() & 1` (`0x4f5436`). Un monstruo toma la acción 5, salvo en
  la 3 o la 4, y sigue. Después, con `+0x114` en 0 **o unordered**, suena un golpe que se elige
  según la clase (`0x4f5488..`; `0x4f5496` consulta C3). Las guardas previas no gastan RNG;
  tampoco los subtipos 209..211, cuyo sonido es `0x7b`, ni el tipo 160 silencioso. Los sorteos de
  las ramas restantes están en el presupuesto de Q-C179, no en cada llamada incondicionalmente.

**Aplicadores del hito 2 (2026-11-10).** `SessionEventApplier` recibe los eventos tipados en
orden desde la sesión y aplica `MovementReplies`/`ActorShock` al mundo durable, antes del paso.
Relectura del `main.exe` mapeado, base VA400000, SHA256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`: sólo ASM estático; los tests
nuevos ejecutan el port, no el original ni las protecciones. `LogicalAccess` representa sus
cierres y conserva el CRT compartido. La evidencia y los RED/GREEN reproducibles están en
`.superpowers/sdd/cl7-resto-traspaso/transitions-research.md`, `transitions-report.md` y
`hero-facing-report.md`. Las cuentas siguientes suponen profundidades iniciales 0;
un acceso exterior abierto suprime su cierre.

- D3 (`0x4c6790..0x4c6b39`): héroe móvil conserva celda/path/blanco y sólo actualiza dirección;
  quieto escribe/cierra X y después Y (8 CRT), conservando XY visible. Otro escribe primero
  blanco 39a/b; con freno 3e5 no cambia Movement/path ni gasta CRT. Sin freno cierra Y/X (8 CRT),
  busca sobre `terrain.attributes->cells` vivas y pone Movement según el resultado. Fallar
  conserva el path anterior y llama Stop. El 186 abre X/Y y cierra Y/X antes de Walk, usando su
  propio esqueleto. Clave desconocida se descarta sin escribir el slot 400.
- D7 (`0x4caae0..0x4cb71b`): con bit 15, Shock precede el número rojo, y después viene vida/cierre
  propio con piso 0. Sin bit 15, el héroe resta/cierra antes de los números y no toma el sorteo de
  Shock; otro excluye word376==0x113 **antes** de `rand()&1`. El creador `0x548e40..0x548eab`
  recorre 100 slots, no llama ni sortea, ni siquiera lleno: Z+140, escala recibida, speed 10,
  timer 0 y rise 1, reteniendo campos ajenos. Amount=0 produce MISS−1/escala 15, sin copias.
- Los flags de daño no son equivalentes: bit 6 solo produce **tres** números. Bit 7 produce
  **cuatro**: Z+140/+150/+160 con escalas 50/55/60 y luces base−0,4/base−0,2/base; después cae al
  normal Z+140/escala 50/base. `0x4cb614→0x4cb6c1→0x4cb6c9→0x4cb6df` acredita el cuarto, corregido
  tras revisión adversarial. No reemplazarlo por una única copia destacada ni por tres números.
- `ActorShock` porta el jugador/Fenrir y los presupuestos por clase; errores explícitos conservan
  el prefijo al requerir la tabla de sonidos de monstruos o las partículas 5f3 del 18f. El efecto 109
  del propio con flags78&0x100 sigue como frontera `0x4cb32f`, después de vida/cierre y antes de
  números/damage. Las reproducciones de audio quedan en CL10.

`MovementRepliesTest`, `WorldTransitionsTest` y `SessionEventApplierTest` prueban orden, guardas,
campos retenidos, número de copias y CRT; no constituyen observación visual ni cierran CL7.

**El giro del héroe quieto, paquete 18/7A.** La rama `0x59b333..0x59b83a` corre sólo si Movement
**ya era cero** en `0x59b29d`: una llegada caminando salta a `0x59b841`, sin orientar ese mismo
paso. Input inhibido corta antes del contador. Si no, incrementa el DWORD `0x77f3ba4` con wrap;
ventana 40000 cerrada y modo 78abb18==1 corta antes del umbral 40 con signo. La consulta va
`0x6594de→0x65972c→0x6570a0`, lectura simple de byte+7c: **cero CRT en esta rama**, incluido envío.
Después inhiben byte 775857c, dying, acciones cb/cc/c5/c7, BL de cabeza, acciones 4d/4f/5a y byte381;
conservan el contador incrementado. BL es falso con acción 3e/flag 1000 o blanco 6c8e3c activo con
selector 6c8e38 válido fuera de mapas 6 y 18..23; el prefijo de cabeza lo entrega al kernel.

Al aceptar, `0x59b407` pone el contador en 0 y calcula
`arrivalYaw=(405-ftol(CreateAngle(mouseX,mouseY,centerX,180)))%360`, con resta DWORD y resto firmado.
centerX es `5bb8d0()/2` truncado, no el ancho físico del framebuffer. El sector es
`ftol((double(yaw)+22.5f)*0.02222222276031971f+1)&7`; no redondea a float cada operación. Escribe
arrivalYaw (`0x77f3ba8`, `0x59b450`) incluso con sector igual. Sector distinto gira el cuerpo antes
de publicar **C1 05 18 dir 7A**, con XOR32 y Delivery::Clear; acción 39 sólo evita girar el cuerpo,
pero manda igualmente. Sector igual no cambia cuerpo ni manda. byte381 se retiene por 511290.

`GameHeroFacing` implementa ese kernel mediante un callback independiente del D3;
`Protocol::Movement::WriteFacing`/`ServerSession::SendFacing` publican los bytes crudos sin
normalizar direction. Sus 12 tests cubren 39→40, guardas, sector/borde 112,5, modo/ventana, orden de
campos antes del envío, wrap y errores con prefijo conservado. Las pruebas wire son de sesión y
protocolo; el llamador conserva cabeza→rama quieta/movimiento→gates→clic→57cfc0. El binding normal
actual usa centerX=320 y acción!=3e; la guarda1000 ya cortó antes. El ancho que cambia por ventanas
y la guarda de blanco6c8e3c carecen de productor UI portado: el parámetro permite representarlos,
pero no acredita esas rutas. Tampoco implementa las interacciones de llegada de CL9.

**El paso entre celdas** (*emulado* con `0x4f37c0`, `0x4ea270` y `0x508a80` reales).

`MovePath(c, giro)` (`0x4ea270`):

- Corre mientras `CurrentPath (+0x3e8) < PathNum (+0x3ea)`. Con `CurrentPathFloat (+0x3e9)` en 0,
  la celda lógica salta a `path[min(cur + 1, n − 1)]`: **va una adelante** de la que se ve (Q-C173).
- Arma 4 puntos, `path[cur − 1 .. cur + 2]` acotados a [0, n − 1], en el centro de cada celda
  ((celda + 0,5) · 100), y el sub-punto de Catmull-Rom según `CurrentPathFloat`. Las constantes
  están en `0x6ba114..0x6ba124` y `0x6b8850`, y la cuenta va en la x87, `(p1·w1 + (p2·w2 − p3·w3)) −
  p0·w0`:

  | `CurrentPathFloat` | Pesos | t |
  | --- | --- | --- |
  | 0 | (−0,0703125; 0,8671875; 0,2265625; −0,0234375) | 0,25 |
  | 1 | (−0,0625; 0,5625; 0,5625; −0,0625) | 0,5 |
  | 2 | (−0,0234375; 0,2265625; 0,8671875; −0,0703125) | 0,75 |
  | 3 | el punto 2 | 1 |

- Con la distancia en el plano al sub-punto ≤ 20 (`0x6b86d0`), `CurrentPathFloat++`; pasado de 3,
  vuelve a 0 y `CurrentPath++`.
- Con `CurrentPath ≥ n − 1` **llegó**: la celda es la última, **la posición pasa al punto** (el
  último paso puede saltar más que la velocidad: 17 o 10,3 en la emulación) y devuelve 1.
- **El giro**: el ángulo al sub-punto con `CreateAngle` (`0x4e93f0`); con |Δ| ≥ 45 (`0x4e96f0`)
  salta, y si no, `TurnAngle(ángulo, nuevo, |Δ| · 0,5)` (`0x4e9560`). En la emulación de un codo:
  87,86 → 86,13 → 84,15, y el salto a 181,99.

`MoveCharacterPosition` (`0x508a80`) suma a la posición `VectorRotate((0, −v, 0),
AngleMatrix(o.Angle))` (`0x5fc8f0`, `0x5fcbd0`): **avanza hacia donde mira**, no hacia el punto.
Después la z es la del terreno (`0x5f93c0`), +90 o +30 con el ítem `0x1c15` fuera de la zona segura,
y `+0x84` suma 0,15.

**La velocidad v** (`0x508940`, en unidades por paso; *emulado* previamente: 12 y 15;
contrato completo contrastado estáticamente con sus 82 instrucciones / 319 bytes):

| Caso | v |
| --- | --- |
| sin política especial (`u16 +2` distinto de `0x20a` o `u8 +0x88` distinto de **1 exacto**) | base `u16 +0x386`, extendida con cero (0..65535) |
| Fenrir (`u16 +0x318 == 0x1c37`) fuera de la zona segura | latch `0x7d3a0d0` cero: escribe 1, luego contador `0x7d3a0d4` = 0 y elige 15; latch no cero: 15 con contador **signed32** < 10, 16 de 10 a 19; desde 20, 17 con `u8 +0x31b` cero y 19 con cualquier no cero |
| `+0x318 == 0x1c16` fuera de la zona segura | 17 (y `Run = 40`) |
| sin alas (`u16 +0x2f8 == 0xffff`) e ítem fuera del rango **signed16 inclusivo** `0x1c14..0x1c15`, o en la zona segura | **12 con `Run (u8 +0x38c)` < 40 y 15 si no**, sin cambiar Run |
| cualquier alas distintas de `0xffff`, o ítem `0x1c14`/`0x1c15`, fuera de la zona segura | escribe `Run = 40` y elige 16 con alas `0x1a17`, 15 en otro caso |
| mapa **signed32** de 18 a 23 inclusive, para todos los tipos | después de lo anterior, escribe `Run = 40` y fuerza 15, conservando los efectos previos |
| al final, flags `u32 +0x78 & 2` | divide solo el retorno por 2; también en mapas 18..23 (7,5) |

La política de ítems requiere ambos discriminadores; los demás tipos/modos también pasan por
mapa y flags. La zona segura es cualquier byte no cero, incluidos `0x80/0xff`. Fenrir **no escribe
Run antes del mapa**, tampoco al elegir 17; un latch no cero conserva todos sus bits, y D4 solo se
reinicia con latch cero. D0/D4 son globales compartidos entre actores, no estado de cada uno.
Esta función no incrementa D4 ni Run, no resetea Run por zona segura y no consulta clase, botas,
guantes ni RNG: esos productores siguen en `SetPlayerWalk`. Run admite todo u8, incluidos 128/255;
los contadores negativos eligen 15. Con memoria estable y distinta, constantes intactas y retorno
normal, base y mitades son exactas en binary32, incluido +0. La unidad portable `MovementSpeed`
proyecta valor y esos tres estados; no reproduce el slot de argumento sobrescrito, status/ABI x87,
fallos o aliases arbitrarios. Sus tests analíticos no acreditan integración ni nueva emulación.

A 25 pasos por segundo, caminando son 300 unidades por segundo, 3 celdas; corriendo, 3,75. En la
emulación, 6 celdas caminando son 49 pasos (1,96 s).

`SetPlayerWalk` (`0x4f37c0`, en cada paso mientras camina):

- **`Run` (`+0x38c`)** vuelve a 0 en la zona segura. Fuera, si es < 40, sube 1 por paso **solo** con
  la clase 3 o 4 (`+0x1d4 & 7`), con botas +5 (`+0x298` distinto de −1 y `+0x29a` ≥ 5) fuera de
  Atlans y de los mapas 24 a 29 y 36, o con guantes +5 (`+0x278`, `+0x27a`) en esos mapas: sin eso
  un DW, un DK o un elfo **nunca corren** (Q-C172; *emulado*: con botas +5 corre desde el paso 40, a
  15 por paso, con la acción `0x16`). Con Fenrir sube el contador `0x7d3a0d4`.
- **Las velocidades de las acciones** de `Player.bmd`, solo con el tipo `0x20a`: las pisa **en el
  modelo compartido**, en cada paso (Q-C171). Las 13 a 20, 73 y 75 van a 0,33 (`0x3ea8f5c3`) y las
  22 a 33 a 0,34 (`0x3eae147b`); con `+0x78 & 2`, la mitad para todos. La 21 queda en 0,35, la de
  `0x60dbe0`.
- **La acción**, con `0x4ea030`, en este orden: Fenrir, `0x4e9f50` o `0x4e9da0`; `0x1c16`, `0x4b`;
  `0x1c17` en la mano izquierda en la zona segura, `0x49`; `0x1c14`/`0x1c15`, `0x20` sin armas y
  `0x21` con; con alas fuera de la zona segura, `0x1e`, o `0x1f` con las armas `0xa1a..`; Atlans y
  parecidos fuera de la zona segura, `0x15` (`Run` < 40) o `0x1d`; sin armas, o en la zona segura
  (en los mapas de menos de 11 o más de 23), `0xd`, `0xe` el elfo y `0x16` corriendo; con armas,
  `0xf` a `0x14` u `0x8b` caminando y `0x17` a `0x1c` u `0x8c` corriendo; los que no son jugadores,
  2.
- **Los pasos suenan** (`0x41ed10`) con `rand() & 63 == 0` en el héroe y `rand() & 15 == 0` en los
  otros: gasta `rand()` (Q-C179).

`SetPlayerStop` (`0x4f2250`; 1.607 instrucciones, 5.481 bytes, cotejados con el PE `c32bbd5f`;
*estático*) pone `Run = 0` (`0x4f227f`) y la acción de parado con `0x4ea030`. La zona segura es
`+0x3e2`, la mano derecha `+0x2b8` y la izquierda `+0x2d8`, y los modelos se comparan como WORD con
signo. En este orden:

1. Un tipo que no es `0x20a` va a su propia rama (`0x4f2d87..0x4f36b5`), que lee sus celdas por la
   tabla de protección y el `TerrainWall`, sin leer en detalle.
2. Fenrir (`0x1c37` en `+0x318`) fuera de la zona segura: `0x7d3a0d0 = 0` y `0x76` con las dos manos
   vacías, `0x77` con las dos, `0x78` solo con la derecha y `0x79` solo con la izquierda
   (`0x4f228c..0x4f22f6`).
3. El caballo oscuro (`0x1c16`) fuera de la zona segura: `0x4a`.
4. En la zona segura, con el cuervo (`0x1c17`) en la izquierda: `0x48`.
5. `0x1c14` o `0x1c15` fuera de la zona segura: `0xb` con las dos manos vacías y `0xc` si no.
6. Si no, **vuela** con alas (`+0x2f8` distinto de −1) fuera de la zona segura o con `+0x376` en
   `0x101`; en la escena 5 y en los mapas 7, 24 a 29 y 36, también con el bit `0x01` del
   `TerrainWall` (`0x7d0d348`, `0x4f23c1`) apagado en su celda (`ftol(y · 0,01)`, `ftol(x · 0,01)` y
   `0x5f82a0`, `0x4f2365..0x4f23dd`).
7. **Sentado** (`+0x7c & 0x200000`), vuele o no: con la acción ya en `0xca` no pone ninguna; si no,
   la `0xca` y un `C1 .. 18` con el XOR32 en línea (sentarse, CL9). La rama está dos veces, volando
   (`0x4f23e3`) y en el piso (`0x4f2801`), hasta `0x4f2bea`.
8. Volando: `0xa` con un arma de la derecha del grupo 4 de abajo, y 9 si no (`0x4f27c6..0x4f27fc`).
9. En el piso (`0x4f2bf6..0x4f2d82`): 1 (2 el elfo, `clase & 7` en 2) con las dos manos vacías o
   en la zona segura de los mapas de menos de 11 o más de 23. Si no, por la derecha: los grupos 0
   a 2 dan 3 sin la marca y, con ella, `0x8a` los modelos `0x227`, `0x229` y `0x231` y 4 los
   demás; `0x813` y `0x814`, 5; los grupos 3 y 5, 6 con la marca y 3 sin ella. Después, una
   izquierda en `0xa12..0xa18`, `0xa23`, `0xa26` o `0xa27` da 7, una derecha en `0xa1a..0xa20`,
   `0xa22` o `0xa24..0xc11` da 8, y lo demás, 1 o 2. **La marca** es el byte `+0x1e` del registro
   del arma en `Item.bmd` (`[0x7758550]`, de a `0x54`; `0x4f2c4c`).

Con la acción puesta, el sufijo (`0x4f36be..0x4f37a5`): con `rand() % 16` distinto de 0 termina. Si
no, con el tipo `0x20a` y el subtipo (`+4`) fuera de 209 a 211: sin Fenrir termina; con Fenrir,
`rand() % 3` distinto de 0 termina y si no suena `0x41ed10(0x279 + (rand() & 1), c, 0)`. Con otro
tipo o esos subtipos lee la tabla de sonidos de los modelos (`0x5769ad8 + tipo · 0xe0 + 0xaa`), sin
leer en detalle. Gasta de uno a tres `rand()`. MuRet (`Scenes::SetActorStop`) porta los pasos 2 a 9
y el sufijo, y corta con un error explícito en la rama de los que no son jugadores, el `TerrainWall`
de los mapas de vuelo en la escena 5 cuando quien llama no lo presta (el paso de los personajes lo presta en
todos los mapas desde el PR de Atlans: en el 7 y en Kalima, fuera de la zona segura, de pie nadan), el
`C1 .. 18` de sentarse y la tabla de sonidos.

`0x4f9580` no toca el movimiento: da la **velocidad de la animación**, la base del `PlaySpeed` de la
acción (después va `0x4ec200`, salvo con `+0x78 & 0x1000`), con multiplicadores por tipo, acción y
frame (1,5 con `+0x3e3` en las acciones 3 y 4; 0,5 en la `0x3f` desde el frame 6 y en la `0x44`; 2
en los tipos `0xa8..0xac`, los `NewFace`; entre otros). A caminar y correr no les toca ninguno: lo
único de este tema es que `0x4f37c0` les cambia el `PlaySpeed`. Lo llama `0x4fcd00`, desde
`0x4fd5f0`.

**Los otros personajes** (`0x508ba0`, desde `0x508ea0`; saltea al héroe): si caminan,
`SetPlayerWalk` y `MovePath(c, 1)`; al llegar, `Movement = 0`, `SetPlayerStop` y **el ángulo en (dir
− 1) · 45**, con la dirección del último `D3`: la 0 da −45 y no 315 (Q-C175). Siempre
`MoveCharacterPosition`. Quietos, con `+0x3e5` en 0, un tipo que no es `0x159` y la celda distinta
del blanco, **reintentan el camino en cada paso**.

**La zona segura** (`+0x3e2`) la escribe `0x505de0` en cada paso (`0x50686e..0x5068b2`), con el bit
`0x01` del atributo de la **celda lógica**, la que va una adelante, y solo con el personaje visto
(`+0x16c`, `0x505ef2`; [El paso de los personajes](#el-paso-de-los-personajes), Q-C206). *Estático*; la
observación nativa en el borde del pueblo está pendiente ([08](08-pendientes.md) P-C44). MuRet:
`Scenes::StepCharacters`, con el héroe, contra el `.att` de Lorencia (el borde del este, (172, 128) y
(173, 128)) y de Devias (el del norte, (220, 76) y (220, 77)) en
`GoldenCharacterStep.TheSafeZoneFollowsTheLogicalCellAhead`.

### Los cambios de mapa

*Estático* salvo lo que se marca. Tres paquetes del servidor llaman a `0x61a560` ([La carga del
mapa](#la-entrada-al-mundo)): el `F3 03` (la entrada), el **`1C`** y el **`F3 04`**. Ninguno cambia
de escena: el juego sigue en la 5, porque las ocho escrituras de `0x6d5dc8` (`0x4c2f1a`, `0x4c363f`,
`0x622f93`, `0x633f05`, `0x634470`, `0x634638`, `0x634dce` y `0x635a91`) no están en esos handlers.
Los formatos, en [04](04-protocolo.md#los-cambios-de-mapa-1c-f3-04-y-f3-12).

**El warp, `1C` del servidor** (`0x4c6e10`, desde `0x4e3f2a`, que antes lo copia a la cola de
`0x77f27b4`). Con el cuarto argumento del dispatcher en 0 no hace nada de esto y manda un C3 con `C1
06 F1 03 00 1C` (*inferido*: el aviso de FoxAnti). Si no:

1. `0x5be150`: con cursorItemCount<=0 sale en `0x5be160`, sin RNG; con ítem sigue pendiente.
2. La celda del héroe (`+0x41c`, `+0x420`, cifradas) en `pkt[5]`, `pkt[6]`; la posición en `(celda
   + 0,5) · 100`; la z, la altura del terreno (`0x5f93c0`), más 30 con el `0x1c15` en `+0x318` y
   `+0x3e2` en 0, o más 90 así en los mapas 8 y 10; y el ángulo, `(pkt[7] − 1) · 45`.
3. Con `pkt[3] == 0` (la habilidad teleport, en el mismo mapa): `0x53d930` (la acción `0x93`, el
   efecto `0x5c6` y el sonido `0x76`), `0x576794c = 0` y al final. No manda nada.
4. Con `pkt[3]` distinto de 0 (un gate o un `/move`): `0x607b40`, `0x510f70` y
   `0x451d10`/`0x451590` (los ítems del piso, los demás personajes y la lista), **también sin
   cambiar de mapa**. Si `pkt[4]` es otro mapa: `0x6c9dd0 = pkt[4]` y `0x61a560`; en el 34, el `C1
   04 BD 00`; el prefijo del Chaos Castle (`0x41ed10(0x9e)`, `0x4906a0`, `0x4914e0`, `0x512a30` y
   `0x443d40`) **al entrar y al salir**: `0x4c7af1..0x4c7b2f` mira viejo 18..23 con cambio **o**
   nuevo 18..23. Al salir también paran `0x99`/`0x9e`. Después `0x447d70`; la z otra
   vez, con las alturas nuevas; y el renglón con el nombre del mapa y el texto 484 en el área de
   mensajes (`0x555190(0x5767988, …, 1)`). El nombre sale de `0x5f0b40`: el texto 30 + mapa del 0 al
   9, el 55 en el 10, el 56 del 11 al 17, el 57 del 18 al 23, el 58 en Kalima, el 669 en el 30, el
   59 en el 31, el 39 en el 32, el 1.850 en el 33, el 1.851 en el 34 y 30 + mapa en los demás
   (*emulado* para los 256, y los textos *verificado* en el `Text.bmd` del laboratorio).
5. Siempre que `pkt[3]` no es 0: **manda `C1 04 F3 12`** (C1 con XOR32, no C3); `0x77f3bb4 =
   GetTickCount()`, `0x77f3bb0 = 0`, **`0x77f3b04 = 30`**, `0x77f3b10 = 0` y `0x6c8dcc = 6`;
   `0x657a90`; `0x420d60` (vuelve a crear el cuervo); el efecto `0x61f` en el héroe y `+0x174 = 0`;
   `0x723c50 = 0` (la tormenta); `0x78abb0c` y `0x78abb10` en 0, `0x6c8e28..0x6c8e38` en −1 (los
   objetivos elegidos, *inferido*) y `0x6c8e4d = 0xff`.
6. Al final: `+0x378 = 0`, `0x4f2250(héroe)` y, con `pkt[3]` distinto de 0, `0x65dc30`, el cartel
   con el nombre del mapa (`Interface\<mapa>.tga` en el slot `0x66a`, si el mapa cambió).

Del lado del servidor (`CGTeleportMagicRecv`, `gObjMoveGate` del GameServer reconstruido;
*estático*), mover pone `RegenOk = 1` y **el `F3 12` lo pasa a 2** (`Seam_4789b0`); un pedido de
gate con `RegenOk` prendido rebota. En la PCAP de la sesión real, los cuatro `/move` dan el `1C` y
el `F3 12` 37 ms después sin cambiar de mapa y de 141 a 299 ms después con carga (*verificado* el
tiempo; que sea la carga, *inferido*).

**Revivir, `F3 04` del servidor** (`0x4c4b80`): empieza como `0x4c3940`, pone la vida, el maná, el
BP, la experiencia y el zen, y rearma al héroe con `0x511290(héroe, 0x20a, x, y, (dir − 1) · 45)`,
conservando `+0x368`, `+0x369`, `+0x370`, `+0x375` y `+0x1f2`. Le pone `+0x3e2 = 1` (la zona segura,
*inferido*) y el efecto `0x61f`, limpia los ítems y los personajes y, si cambió el mapa, `0x61a560`
y la z; el segundo `0x512a30` corre sólo al entrar/salir de Chaos. **No manda el `F3 12` ni toca
`0x77f3b04`**. En el `muproxy.log` hay
dos, cinco segundos después del `17` (la muerte) del héroe, y ningún `F3 12` detrás (*verificado*).

**Port del hito 2, T11–T15 (2026-11-10).** `WorldTransitions` y
`WorldTransitionSupport`, desde `SessionEventApplier`, distinguen los tres caminos sin reutilizar
el F303 para reconstruir F304. Misma evidencia estática y límites de precisión que los
[aplicadores de movimiento](#caminar):

- 1C conserva el prefijo posicional aun si falla después: escribe X/Y, lee X/Y (16 CRT), centra XY,
  calcula primero Z con **terreno/mapa anteriores**, pet 1c15 y safeZone existente, y yaw
  `(dir−1)*45`. No copia target39a/b ni direction 388. Gate distinto de 0 apaga piso,
  `KillOtherActors(playerKey)` y lista 451590 incluso con mismo mapa. La comparación de clave
  es signed16, recorre 400 lugares y deja el 400; suelta cuervos incluso de muertos y dueño.
- Mismo mapa conserva objetos/efectos/cámara/zoom. Otro hace LoadMap y altura nueva antes de
  confirmar. Al entrar **o salir** de Chaos el port conserva carga y sonido 9e y corta en
  `0x4906a0`, antes de equipo/F312: faltan UI 4906a0/4914e0, no se sustituyen por un no-op.
  Eventos 9/11..23 conservan la frontera 447d70; Crywolf/asedio/Kalima, las de LoadMap.
- Camino normal: F312 exactamente una vez después de cargar; luego tick uint32 con wrap desde
  `IClock` prestado, pending=false, contador 30/cierre 4, mouseTime 0/Max 6. Ventanas cerradas consumen
  dos CRT. Abiertas cortan en 657a90 **después** de confirmar y contador, **antes** de cuervo y
  llegada. Se recrea cuervo 420d60 (12 CRT desde profundidades 0), nace 61f sin RNG, alpha 0,
  storm/78abb0c/78abb10 en 0, elegidos−1, byte6c8e4d=ff y Movement 0/Stop. Sin cuervo ni carga,
  un 1C mismo mapa usa 23 CRT; con cuervo 35. Cada cierre depende de su profundidad durable.
- Gate0 ignora pkt.map para cargar: no limpia pools, no confirma ni cambia tick/contador ni cierra
  ventanas. Tras los 16 CRT posicionales, `CombatActionSpeeds` porta `0x4f3f80..0x4f48d7`: abre
  attack/magic propios y cierra 0xf08, factores f32 de 0,004 y 0,002, productos intermedios f32 y
  overlay durable `PlayerActionSpeeds`, sin mutar Skeleton const. Sigue acción 93/frame 5,
  byte80=3/alphaTarget=1, efecto **con modelo** 5c6 vida 10/sin RNG, sonido 76,
  gateRequestBlocked 0 y Stop final (puede reemplazar 93). No confundir ese 5c6 con una partícula.
- F304 apaga publicado, latch izquierdo y gateRequestBlocked, copia stats/cierre 0xf08,
  apaga 400 vivos y reconstruye heroSlot (28 CRT). PK 375/word1f2 vienen del **publicado**;
  guildStatus368/byte369/byte370 vienen del **heroSlot**, que puede ser otro. Conserva esas
  cinco fuentes; no sortea ni publica otro héroe. Clase propia/skin=0 llevan otro cierre 0xf08,
  byte88=1 y safeZone=1 (`0x4c4e3a`). Equipo/Stop/llegada preceden piso/KillOtherActors; cambio
  de mapa carga/altura, y sólo Chaos vuelve a vestir. No llama 447d70, no F312/tick/contador.
  Recrea cuervo y actualiza byte374 por gremio, después cierra ventanas. Fixture normal vacío:
  `3*0xf08+35` CRT, no el presupuesto del F303.

**Transiciones 0↔3 y reentrada.** `MapModelEffectServices` permite cargar Noria con sus cinco
altas síncronas antes del pasto. `ClearWorldTransientPools` representa 6070a0 apagando vivos de
fauna/hojas/partículas/daño/joints/cintas/efectos y conservando payload de slots; los ítems del piso
se apagan por 607b40 separado en 1C/F304/retorno. Los tests con GS falso recorren
F303→gate23(0→3)→D3 en el mismo lote→gate25(3→0), F304 3→0 sin ack/cambio de contador y
juego→selección→juego. Conservan terreno/zoom/cámara cuando corresponde y observan prefijos,
reconstrucciones y CRT compartido. Validan el port; no prueban imagen nativa, todos los mapas ni
el cierre completo de Hito 2/T16. Quedan cursor positivo 5be150, ventanas CL8, UI/eventos,
sonidos/efectos de monstruos y la evidencia visual/temporal pendiente. La lista 451590 aún no
tiene productor portado; la limpieza no inventa registros.

**No hay pantalla de carga entre mapas.** La escena 3 solo va de la selección al juego. La carga
corre adentro del handler del `1C`, en el mismo hilo, y mientras dura no hay frame. Además,
`0x6371a0` sale sin dibujar con `0x77f3b04 > 30` (`0x637219..0x637239`), y entonces el bucle saltea
el `glFlush` y el `SwapBuffers` (`0x6385e1..0x6385f7`): **la pantalla queda en el último frame**
desde que el cliente pide un gate (el contador en 9.999.999) hasta que llega el `1C` (en 30). Cómo
se ve, P-C31 a.

**Lo que queda y lo que no:**

- Queda el héroe (su lugar de `0x743a16c`, que `0x510f70` saltea por la clave `0x57678bc`), su
  equipo y sus ventanas. La cámara no se toca (se recalcula en cada frame desde el héroe) y conserva
  su zoom. La música y los sonidos siguen hasta que el bucle elige los del mapa nuevo.
- Se rearman el cuervo (`0x420d60`) y, en el `F3 04`, el héroe.
- Se van, aun en el mismo mapa (`pkt[3]` distinto de 0), los demás personajes, los ítems del piso,
  la lista de `0x7245c8` y los objetivos elegidos (`0x6c8e28..0x6c8e38`).
- Se van solo si cambia el mapa los objetos del mundo, los efectos (las nueve tablas de [La carga
  del mapa](#la-entrada-al-mundo)) y las texturas y los modelos del mundo y de los monstruos.

**Pisar un gate.** El cliente lo detecta y lo pide; el servidor no mira las celdas al caminar.
`0x599ef0` lo llama `MoveHero` (`0x59b841`) en cada paso, por todos los caminos de su parte de
movimiento, salvo en los estados del héroe que la frenan (`+0x389`, bits de `+0x78` y `+0x7c`:
`0x59ae47..0x59ae87`) o con `0x77f3b04 > 30` (`0x59afd6`); antes le resta 1 al contador si es
positivo (`0x59af01`). Recorre los 256 gates de `Gate.bmd` ([05](05-formatos-de-datos.md#gatebmd))
y toma el que tenga el flag en 1, el mapa actual y la celda del héroe en su rectángulo, con
`0x77f3b04 == 0` (`0x59a03d`) y `+0x399` del héroe en 0. Después:

- el gate 28 (de Devias a Lost Tower), con el nivel bajo: nada, sin aviso (`0x59a08b..0x59a16c`);
- los 45 a 49 y 55 a 56 (Atlans) montado en Uniria o Dinorant (`+0xdac`, o `0x7879f80`): el texto
  261 y nada más;
- los 62 a 65 (Icarus) sin alas (`0x1800..0x1806` en `+0xd64`), Dinorant, caballo, capa o Fenrir:
  el texto 263 (vacío en este `Text.bmd`) y, con el nivel bajo, el 350; con alas pero en Uniria
  (`0x1a02` en `+0xdac`), el 569;
- **con el nivel bajo** (`0x7681eb0 + 0xe` menor que el del gate, por 2/3 en las clases 3 y 4):
  `0x77f3b04 = 50` (`0x59a3a3`) y el texto 350 en el área de mensajes. Como 50 > 30, el juego no
  dibuja ni mueve al héroe por 20 pasos (0,8 s), y después hay otros 30 pasos (1,2 s) sin gates
  (P-C31 b);
- si no, con `0x576794c == 0`, `0x77f3bb0 == 0` y **3 s desde el último `1C`** (`GetTickCount() −
  0x77f3bb4 ≥ 3000`, `0x59a462..0x59a468`): **manda el `1C` con el gate**, `0x77f3b04 = 9.999.999`
  (`0x59ac6c`); si el destino es el mapa 30 o el 31, llama a `0x61c210` y después guarda las macros
  (`0x61c170("Data\\Macro.txt")`, `0x59ad2a..0x59ad4a`). Luego pone `0x77f3bb0 = 1`
  (`0x59ad50`), `0x6c8e28..0x6c8e38` en −1 y `0x77f3ba4 = 0` (`0x59ad50..0x59ad73`).

El servidor (`CGTeleportMagicRecv`), con un gate distinto de 0, mira `IsInGate` (el flag, el mapa,
el rectángulo **con 5 celdas de margen** y el nivel, por 2/3 solo en la clase 3) y llama a
`gObjMoveGate`, que elige una celda al azar del rectángulo destino y manda el `1C`; si no, devuelve
el `1C` con la misma celda y el mismo gate (*estático*, del servidor reconstruido; Q-C182). El cable
no tiene ningún `1C` del cliente: en la PCAP todos los cambios fueron por `/move` (P-C31 a).

**Los ganchos por mapa** no son un sistema: son `if (mapa == N)` adentro de las funciones generales
(*estático*; el barrido de las 384 referencias a `0x6c9dd0`, en 173 funciones). Caen en cinco
puntos, en el orden de la escena 5:

1. **Al cargar** (`0x61a560`): `0x616ec0` (los modelos), `0x42a760` (el 30), el `.att` y la luz
   según el estado (30 y 34) y `0x4377a0` (Kalima).
2. **En el paso** (`0x636290`): `0x5fbf60` (las olas), `0x606560` → `0x604ab0`, `0x6063d0` y
   `0x5ff960` (los objetos; el `switch` de `0x605ae8` atiende del 0 al 8, del 11 al 17 y el 55, y el
   9 y del 18 al 54 van a los módulos), `0x530490` (las hojas), `0x446120`, `0x447030` y `0x59adf0`
   (los gates y `0x77f36d0`).
3. **En el frame** (`0x637700`): el `glClearColor`, la música y el ambiente.
4. **En el dibujo** (`0x6371a0`): la cámara (el far por mapa y el rectángulo), el terreno (salvo en
   el 10 y en Kalima), la niebla del 30, el segundo pase de Kalima, la nieve doble de Devias y la
   bandera de Kalima (`local_8`) para los joints, los sprites y las partículas.
5. **Por objeto dibujado** (`0x6046a0` → `0x602e90`, `0x5ffdf0` y `0x6002e0` → `0x602ed0`): la
   creación (`0x606660`, el `switch` de `0x606e68`: 0, 1, 2, 3, 7, 8 y 55), después del dibujo
   (`0x602ed0`, `0x604580`: 0, 2, 3, 4, 6, 8, 9, 10, 11 a 17 y 55) y el dibujo de los modelos
   (`0x6002e0`: 7, 9, 10 y Kalima; además, en `0x600a42..0x600e2a`, el 0 con el `0x69`, el 4, el 8,
   del 11 al 17, el 3 y el 55, *estático*).

Los mapas de evento tienen además sus módulos, con ganchos en los mismos puntos (CL9): el Chaos
Castle (18 a 23, `0x404e20..0x405e00`), el Blood Castle (11 a 17, `0x40c2d0`), el Castle Siege (30,
`0x42a760..0x42ea00`), el 35 (`0x42ee20`), Crywolf (34, `0x42fe50` y `0x4303c0`), Kalima (24 a 29 y
36, `0x4385a0..0x439680` y `0x43c150`), Land of Trial (31, `0x43ff10`) y Aida (33, `0x425070`). Lo
propio del terreno de algunos mapas (*estático*; MuRet ya tiene lo marcado con ✓):

| Mapa | Qué cambia | Dónde |
| --- | --- | --- |
| 7 | el agua con las `wt`, sin segundo pase del terreno y las sombras con alfa 0,2 | `0x5fa973`, `0x5fc33e`, `0x60b41a` |
| 8 | el agua corre a la mitad (40 s) y la ola por 50 ✓ | `0x5fc26b`, `0x5fbf60` |
| 10 | sin terreno; los objetos con margen −10 en el frustum | `0x6371a0`, `0x6046a6` |
| 11 a 17 | el pasto no se dibuja por celda ✓ | `0x5faa79` |
| 18 a 23 | sin pase del pasto (`0x6c9d58` en 0) ✓; `0x405e00` saca `rand()` con cada celda de los dos pases (Q-C229) | `0x5f893b`, `0x405e0c` |
| 24 a 29 y 36 | sin tiles; un terreno de agua propio de `0x114ad8` bytes en `0x723c48` | `0x4377a0`, `0x41cc70` |
| 30 | el agua al revés ✓ y la luz con otras constantes ✓; sin pasto; la niebla y el far de arriba | `0x5fa739`, `0x5f8d90`, `0x5f8942` |
| 34 | cada pedida de las UV del agua saca `rand()` y suelta a veces la burbuja `698/0` ✓ ([Los objetos de Crywolf](#los-objetos-de-crywolf)) | `0x5fa695` |
| 55 | las alturas por 3 ✓ | `0x5f924d` |

Fuera de Lorencia pero en el mismo lugar del paso: con la capa 1 en 3 o en 10 o más, Devias oculta
los techos `0x51`, `0x52`, `0x60`, `0x62` y `0x63` y no nieva.

### La vuelta a la selección

El `F1 02` con `pkt[4]` en 1 (`0x4c2710` salta a `0x4c2f5b` en `0x4c274f`; *estático*, CL7 hito 1,
T14). No mira la escena ni el estado: lo primero que mira es el cuarto argumento del dispatcher, el
del paquete cifrado (`0x4c2f5b..0x4c2f5e`). La mirada a la escena 5 de `0x4c275c` es solo del 2:
con 2, en la escena 5 limpia el juego (`0x42fdc0`, `0x622b00`, `0x41ee50`, `0x5be150` y `0x61eef0`,
`0x4c2765..0x4c2779`) **antes** de mirar si llegó cifrado, así que también lo limpia en claro, y
recién después elige entre volver al login (`0x4c2ef3`) y el aviso (`0x4c2787..0x4c2ef2`). El 2
llega con la CL9.

- **Sin cifrar** (`0x4c2f64..0x4c35dd`): arma `C1 06 F1 03 00 F1` y lo manda por el C3, como el
  aviso del `F3 03` en claro (`0x4c396a`) y el del `F3 10` (Q-C205), y sale por `0x4c3921`. No
  limpia nada ni cambia la escena (Q-C211).
- **Cifrado** (`0x4c3626..0x4c390f`), en este orden:

| Dónde | Qué hace | MuRet |
| --- | --- | --- |
| `0x4c3626`, `0x4c362b` | `0x622b00` y `0x41ee50`: paran sonidos | sin audio hasta la CL10 |
| `0x4c3630` | `0x5be150`: sale en `0x5be160` sin un ítem en `[0x7879fbc]`, que solo escriben con un valor `0x4d1c89` y `0x4d29cb`, handlers que MuRet no tiene; con uno, saca `rand()` y toca la caja de `0x723c80` | queda en 0 ([Los stats del registro propio](#los-stats-del-registro-propio)): nada |
| `0x4c3635` | `0x61eef0`: `0x6070a0` (los efectos y los objetos del mapa; no toca el terreno ni el pasto), `0x612230` y `0x612d50` (sueltan los modelos `0x152..0x207` y los sonidos `0xa0..0x1c1`), `0x607b40` (los ítems del piso) y `0x510f70(−1)` (`0x61ef04`) | `ReleaseTheMapObjects`, `ClearWorldTransientPools`, piso.alive=false y `KillOtherActors(−1)`, en orden. Payloads y terreno/pasto se conservan; de los 400 sobrevive sólo clave `0xffff`, el slot 400 no se recorre (Q-C212). La descarga nativa de modelos/sonidos no se reproduce |
| `0x4c363a` | `0x42fdc0`: globales de la interfaz a su valor inicial | no los tiene |
| `0x4c363f` | la escena 4 | `WorldRuntime::orderedScene`: el `SceneManager` la toma después del `Poll` y entra en su próximo paso, antes del paso de la escena (`SceneManager::Step`) |
| `0x4c3649..0x4c38e4` | el estado `0x32` y el `F3 00` | la sesión: `WriteCharacterListRequest` |
| `0x4c38eb`, `0x4c38f2` | `0x7d3a13f` y `0x7d3a140` en 0: la primera vez de la selección (`0x633f20` corre otra vez) y del juego (`0x636290`, otro `F3 03`) | cada escena es nueva: `SelectionState::BeginSelection` y `GameScene::Enter` |
| `0x4c38f9` | `0x7d3a101` en 0: el mundo sin cargar | `drawingEnabled` en `false`: el juego no avanza ni dibuja hasta que la selección cargue el 54 |
| `0x4c3900` | el estado en 0: la lógica de la selección (`0x6342c4`) y su dibujo (`0x634695`) esperan la lista, que lo pasa a `0x33` (`0x4c224f`, `0x4c225f`) | `WaitingCharacterList`, con su `ReplyTimeout`; la selección no pide otra lista |
| `0x4c390a` | `0x4c2480`, abajo | |

`0x4c2480` (`0x4c2480..0x4c2702`), lo que MuRet tiene:

| Dónde | Qué hace | MuRet |
| --- | --- | --- |
| `0x4c250e` | el nivel de la cámara (el WORD `0x7d3a0ec`) en 0 | `GameCamera::level` en 0 |
| `0x4c2540` | el mundo en curso (`0x6c9dd0`) en −1 | `WorldRuntime::map` en −1 |
| `0x4c2546` | `0x419900`, con `[0x710148]`: pone en 0 su `+0x21383` y manda `C1 03 31` (`0x419a0e`), en claro y sin XOR32 | la sesión: `WriteWindowClose` |
| `0x4c2575..0x4c264b` | abre el registro propio por la tabla de protección (o lo da de alta con la cuenta en 1, `0x4c25cf..0x4c25ea`), pone en 0 `+0x3c` (byte), `+0x3e`, `+0x40` y `+0x42` (`0x4c2600..0x4c260b`) y lo cierra: si queda en 0, `0x40b4d0` y sus 3.848 `rand()` | `LogicalAccess` con `OwnRecordCloseRandoms` |
| `0x4c269d` | `[0x710148] + 4` en `0xff`: la próxima entrada al mundo vuelve a mandar el `A0` | MuRet no manda el `A0` (Q-C215) |
| `0x4c26da..0x4c26e7` | con `[0x576352c]` (el objeto que crea WinMain en `0x4bb993` y suelta al cerrar, `0x4af319`), `0x657670`, cuyo `0x4499b0`, si la caja de `0x723c80` está vacía (`0x449c10`: los 32 lugares de `0x48` bytes con el WORD en `0xffff`), pone en 0 `0x724580` y `0x724584` y manda otro `C1 03 31` (`0x449ad1`) | la sesión: el segundo `WriteWindowClose` (Q-C214) |

Lo demás que escriben `0x4c2480` y `0x4c2710` son globales de la interfaz, del chat, del gremio y
del juego que MuRet no tiene (`0x57678ed..0x5767960`, `0x6c82e8`, `0x6c8e28..0x6c8e50`,
`0x78abaa8`, `0x77f3b67`, `0x78abb04..0x78abb10`, `0x6c9320..0x6c932c`, `0x77f3b78`, `0x77f3b08`,
`+0x2134c..+0x21385` de `[0x710148]`), y las llamadas `0x5545f0`, `0x555030`, `0x40ba20` (con
`[0x723c60]`), `0x5e3c70`, `0x5ecda0`, `0x403300` y `0x403d60`. Ninguna de esas llamadas, ni
`0x622b00`, `0x41ee50`, `0x61eef0`, `0x42fdc0`, `0x419900` y `0x657670`, llega a `rand()` por
llamadas directas; `0x5be150` sí, y no lo saca sin el ítem. Así, la vuelta saca solo los 3.848 del
cierre del registro (`ReturnToSelectionTest`). Lo que **queda**: el terreno y el pasto del juego
(la selección los usa para construir los personajes de la lista hasta que `0x633f20` cargue el 54),
el héroe publicado (`0x743a170`, que solo escriben WinMain y `0x4c4725`) en su lugar del juego,
aunque esté muerto, y lo que la cámara lee de él (Q-C213).

Los paquetes salen en este orden: el `F3 00` (cifrado, `0x4c3653`), el `C1 03 31` de `0x419900` y el
de `0x4499b0`. Qué hace el GS con el `C1 03 31` no está leído: el nombre de MuRet (`WindowClose`)
sale de lo que hace el cliente al mandarlo (*inferido*).

MuRet (`Scenes::ApplyReturnToSelection`, desde el `SessionEventApplier`, y
`ServerSession::ReturnToSelection`) lo sigue en ese orden. El 1 llega en cualquier escena con el GS,
y la orden pasa por la tabla de transiciones: desde el juego, la selección o el login cambia; desde
la carga o la carga del mundo, es un error en el log (Q-C79). La selección que sigue espera la
lista que pidió la sesión, la construye en el terreno del juego con el mundo en −1, la ilumina y le
pone la cámara al héroe publicado si cae entre los cinco, y al elegir vuelve a pasar por la carga
del mundo, con el mosaico y el contador en 9.999.999, como la primera vez
(`SceneManagerTest.GameSelectionGameHoldsAndReloadsAgain`). La respuesta del `F3 03` que sigue corre
como una segunda ([La entrada al mundo](#la-entrada-al-mundo)), con el mapa en 54 otra vez y el
publicado, muerto, en su lugar del juego hasta `0x4c4725`. Lo que pide la VM, P-C45.

**Ampliación del retorno, hito 2 T15.** La limpieza de los pools vivos ya no depende de que estén
vacíos: 6070a0 apaga fauna, hojas, partículas, daño, joints, cintas y efectos, conservando los demás
campos de cada slot; 607b40 apaga el piso por separado. El terreno y el pasto, el zoom y la cámara
no se reconstruyen, salvo el nivel de cámara puesto en 0 por 4c250e. `ReturnToSelectionTest` y las
regresiones del sink observan esos datos retenidos y el recorrido juego→selección→juego, además
del orden de los tres paquetes. Esto no implementa la UI pendiente ni la descarga nativa de
modelos/sonidos, y no sustituye la observación nativa pendiente de P-C45.

### La tabla de protección

Algunas variables del juego viven cifradas en la tabla de `0x57630c8` y están en claro solo mientras
alguien las tiene abiertas: el registro propio de `0x7681eb4` (`0xf08` bytes), la celda lógica de
cada personaje (`+0x41c`, `+0x420`), el contador `0x77f3b04` y el de pasos `0x7d3a0f0` (Q-C125),
entre otras. Cada entrada guarda la copia cifrada y, en el byte siguiente, cuántos accesos la tienen
abierta. Abrir la descifra cuando la cuenta pasa de 0 a 1, y cerrar la vuelve a cifrar cuando baja a
0: **cada cierre que la deja en 0 gasta `rand()`**, 3.848 el registro propio y 4 una variable de 4
bytes (*estático*; las dos cuentas, *emulado*). Las funciones (*estático*):

| Función | Qué hace |
| --- | --- |
| `0x40ad90` | agrega una entrada para una dirección (la primera vez que se abre) |
| `0x40b240` | busca la entrada de una dirección, con el hash del método `+0xc`; −1 si no está |
| `0x40b2f0` | da la entrada: la copia y la cuenta de accesos |
| `0x40b440` | descifra el registro propio al abrir el primer acceso |
| `0x40b4d0` | lo cifra de nuevo al cerrar el último: **un `rand()` por byte, 3.848** (`call 0x69fd70` en `0x40b52e`, el lazo hasta `0x40b549`), y deja `rand() & 0xff` en cada byte del registro |
| `0x40b570` y `0x40b800` | abren y cierran una variable de 4 bytes en una sola llamada; el cierre trae en línea los 4 `rand()` |
| `0x412810` | descifra una variable de 4 bytes |
| `0x412890` | la cifra de nuevo: 4 `rand()` |

Muchos lugares traen el acceso en línea en vez de llamar (el `D7` del héroe tiene el lazo de
`0x40b4d0` en `0x4caf64..0x4cafb7`), así que contar las llamadas no alcanza. Los que gastan, por
paso o por frame (*estático*; el de la cámara, *emulado*):

| Quién | Cuándo | `rand()` |
| --- | --- | --- |
| `MoveHero`, que llama a `0x57cfc0` al final de casi todos los pasos (`0x59e255`), y este abre y cierra el registro propio (`0x57d051..0x57d10d`) | cada paso del juego con el contador `0x77f3b04` en 30 o menos | 3.848 |
| `MoveHero`, el contador `0x77f3b04` en dos accesos (`0x59ae8d..0x59afd1`) | cada paso del juego que pasa los gates de `0x59ae24..0x59ae87`, también mientras espera el `F3 03` | 8 |
| `0x6371a0`, el contador (`0x6371b7..0x637229`) | cada frame dibujado de la escena 5 con `0x7d3a101` en 1, aunque no presente | 4 |
| `0x6356f0`, el contador (`0x635723..0x635963`) | al entrar a la escena 3 | 4 |
| la selección, el registro propio (`0x40f370` o `0x40b4d0`) | al pasar a la escena 3 | 3.848 |
| `0x4c3940`, la respuesta del `F3 03` | al entrar al mundo: tres cierres en línea del registro propio (`0x4c41c4`, `0x4c46f0` y `0x4c481e`) y el de `0x512a30`, que lo abre en `0x512a48..0x512aba` para todo `0x20a` (contraste estático T7; antes se contaban tres) | 15.392 |
| `0x511290`, al armar un `0x20a` | la celda lógica (seis accesos) y el contador de pasos (uno) | 28 |
| `0x4c3940`, el resto | el contador en 30 (`0x412890`), `0x657a90` con las ventanas cerradas (dos entradas de 1 byte, `0x4c0ec0` y `0x4c1190`) y el lugar del héroe (`rand() % 400`) | 4 + 2 + 1 |
| el `F3 04` (tres cierres), el `F3 10` (uno común, uno por cada lugar del equipo y uno por cada ítem de una grilla con `[0x78a6758]` en menos de 12; [El F3 10, el inventario](#el-f3-10-el-inventario)) y el `D7` al héroe (uno, en línea) | con cada paquete | 3.848 por cierre |
| `0x508f00`, las celdas lógicas al marcar el `TerrainWall` | cada paso, por cada personaje vivo | 8 |
| `0x635ae0`, la celda del héroe para el atributo | cada frame dibujado, también en las escenas 2 y 4 | 8 |
| `0x4c2480`, la vuelta a la selección (el `F1 02` con 1 cifrado) | un cierre en línea del registro propio (`0x4c260f..0x4c264b`; [La vuelta a la selección](#la-vuelta-a-la-selección)) | 3.848 |

El mismo gasto lo tienen los otros accesos a `+0x41c` y `+0x420` (en `MoveHero`, `MovePath` y el
`D3`, 4 por cierre) y el contador de pasos (Q-C125). Ese azar no se usa para nada, pero corre la
serie de la CRT que usan los efectos, la tela y el cuervo: Q-C183 y P-C22 a.

**La fase del contador, contraste estático CL7.P:** en `0x637700`, el paso lógico se repite mientras
el acumulador cumple el umbral firmado de 40 ms. El snapshot del contador `0x7d3a0f0` se toma antes
del cierre de `0x6378e6`; `0x6378f7` divide con signo por 313 y `0x6378f9` compara el resto con **57**.
El incremento viene después de la escena, sobre el valor actual reabierto. Con inicio lógico 0,
un solo incremento por paso y sin otras escrituras, 1.000 pasos refrescan con los snapshots
57, 370, 683 y 996;
no se ha probado ese inicio lógico para el proceso nativo.
El `ADD` del incremento guarda 32 bits; después de envolver a valores interpretados como negativos,
el resto firmado no puede ser 57. No hay periodicidad uniforme durante toda la vida del contador.

El refresco completo llama directamente a `rand()` en `0x63792a`, `0x6379d4`, `0x6379f4` y
`0x637a52`. Esto fija cuatro consumos directos; el presupuesto transitivo sigue condicionado por
los contratos de sus llamadas y de los dos cierres del contador. El análisis de 1.790 instrucciones
exportadas (7.471 bytes) coincide con el PE de hash
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`, sin ejecución ni nueva
emulación. CL7.P permanece abierta; no se reconstruyen las transformaciones de protección.

## Lo que no se sabe

- **Modelos, en el binario** (P-C01, cerrado en la CL6.R): el skinning, la pose, el avance, la
  cabeza, el dibujo de una malla, los brillos por nivel y los slots de las texturas ya están
  (arriba). Falta (P-C14): si la selección gira la cabeza, la velocidad que queda en las acciones
  sin escritura, la acción de parado sin arma y con una de dos manos, el color del pase base
  (`0x609640`, P-C14 e: los de los demás pases están desde la CL6.3) y una mano vacía; .
  `0x53c6a0`, la tabla de `0xa10` bytes, son los joints (CL7.R, [Los efectos del login](#los-efectos-del-login));
  su paso (`0x535840`) no se leyó. Las
  100 capturas de pantalla de la selección (`graphics-01/CHARACTER-ANIMATION-NOTES.md`, VMware
  MKS, privadas) muestran que la pose y el arma cambian con la UI quieta; no están sincronizadas
  con la traza.
- Qué es cada bloque de `GL_QUADS` (efectos) y el resto de los `GL_TRIANGLES` que no son del
  personaje: la búsqueda por UV se hizo solo contra `player\` e `Item\`.
- Efectos: la estela (`0x52e590`) y, desde la CL7.R, las siete tablas del pase 3D del login, con las
  fuentes de esa escena ([Los efectos del login](#los-efectos-del-login)); de cada tabla, los casos de
  otros mapas y de las habilidades no se leyeron.
- El dibujo píxel a píxel de cada widget (dentro de `0x62db60`): las trazas no tienen imagen.
- Para qué usa el juego el caché de `0x466000`, si estas escenas no lo tocan (*inferido*: solo
  con `TextOut` en 1).
- **La carga y el login, en GL** (P-C13): no hay traza ni captura de estas dos escenas; el orden,
  las posiciones y la mezcla de arriba son del binario. Tampoco se leyeron las ventanas de
  `+0x3d0`, `+0x7e8` y `+0x4578`, las que abren los botones de la barra, ni qué contesta el CS
  al `F4 06` del botón de los créditos. Del login de la CL6 (P-C15): el slot de `Logo03` y cómo
  sube la luz del logo; el cuervo, leído en la CL7.R, falta verlo contra el original (P-C23).
- **La escena del juego** (el barrido de Lorencia, [arriba](#la-escena-del-juego)): leída en el
  binario y contrastada con una traza de un frame muy posterior a la entrada. Lo que pide una
  corrida del original está en P-C28 (la entrada y lo vivo de Lorencia), P-C29 (la cámara), P-C30
  (caminar) y P-C31 (los mapas y los gates). Tampoco se leyeron `0x53eaf0`, `0x403590`, `0x557650`
  ni `0x451c10`, del paso, ni el detalle de la rama del héroe de `0x50ba30`.

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
| La carga dibuja sola, con su UI en 800 × 600 y un frame entero por cada uno de los 11 avances | `LoadingScene` con su juego de texturas y la barra, dentro del `SceneManager`, con un avance por paso: MuRet no carga nada en ella (`Text.bmd` lo lee `main.cpp` antes, como WinMain en `0x4b9b9f`) | CL5.4 |
| El login carga todo otra vez cada vez que se entra (mundo 55, texturas, ventanas, CS) | `LoginScene::Enter` también cada vez: las 18 texturas, las 3 de las ventanas (que el original sube una sola vez, con los datos del juego, en `0x61ccb0`; `WindowTextureSet`, CL5.5) y `ServerSession::Start`; el mundo, como dato de la escena (`SceneWorlds.h`), lo dibuja la CL7 | CL5.4, CL5.5 |
| El login dibuja en el frame del render loop, con la cámara, el pase 3D y lo común a las escenas | `LoginScene::Render` en un `SceneCanvas` (el lote, el texto, el fin del 2D y el pase 3D), sin el fundido (Q-C66), y al final la UI (CL5.5): la lista, la barra de abajo, la cuenta y el aviso, cada una con la mezcla normal (la que deja `0x554810` antes de la UI), y el cursor del juego (`UI::DrawGameCursor`, desde el cierre); `Gl::GlFrameTarget` limpia en negro, dibuja el lote y presenta | CL5.4, CL5.5, cierre |
| Siete ventanas de la UI nueva en píxeles, con la imagen de la UI nueva (`0x647650`, la de la carga) en sus fondos; avisos por id con un texto de `Text.bmd` y un botón | `UI::` con la lista, la cuenta, el aviso y la barra de abajo, y sus imágenes con la forma de la carga (UV a medio téxel, la y desde arriba) en píxeles; la lista con 20 botones de servidor (Q-C63); el id del aviso y su texto en una tabla, la de arriba | CL5.5 |
| Los que leen los paquetes abren las ventanas: `0x4c1860` la lista con el `F4 06`, `0x4c1e20` la cuenta con el `F1 00`, `0x4e5ebc` el aviso del `F1 01` | la `ServerSession` no conoce la UI: dice en qué estado está (`ChoosingServer`, `WaitingCredentials`, `Failed`), y la `LoginScene` abre la ventana que va cuando el estado cambia. El OK de un aviso que cierra el juego lo pide la escena (`IScene::WantsToQuit`) y lo hace `Main` (Q-C69) | CL5.5 |
| Ventanas como clases con vtable de 22 métodos | `UI::Window` con una interfaz chica (dibujar, evento) y una clase por ventana; el administrador ordena por prioridad | CL8 |
| Cámara con seis globales, far × 1,4 en el mundo | `Camera` como valor, testeable: matrices de vista y proyección comparadas con las de las trazas | CL7 |
| Terreno celda por celda, dos pasadas por celda | los dos triángulos de cada celda en el `ModelBatch` (`World::DrawTerrainCell`), con las mismas reglas de capa, alfa, agua y luz y el mismo orden (`World::DrawTerrain`), celda por celda contra la traza de Lorencia (`GoldenTerrainTrace`) | CL7.1 |
| Modelos transformados en la CPU, un hueso por vértice, pose de `Player.bmd` | skinning en la CPU primero (se compara vértice a vértice con el original); pasarlo a la GPU queda para después, si hace falta | CL6 |
| El cuerpo avanza por paso de lógica con la velocidad de su acción | `Data::Model` guarda la velocidad de cada acción (la tabla de `0x60dbe0`) y el frame se avanza en el paso de 40 ms; el dibujo interpola entre el frame del paso anterior y el del actual (D-C4), y el modo fiel dibuja el del paso, sin interpolar | CL6 |
| Mascota y alas avanzan por frame dibujado (0,5 y 0,25 keys) | avanzan por tiempo: 0,5 keys cada 46,875 ms es la velocidad del original a 21,33 FPS, igual a cualquier FPS (Q-C20) | CL6 |
| Brillo de items con UV de la normal y el reloj del frame | un shader por modo de brillo con las fórmulas de arriba; T es el reloj del frame en ms | CL6 |
| Cuántos pases de brillo lleva una parte, por su nivel (`0x60b3d0`), con el tope de las opciones | `ItemGlow`: la tabla de arriba, una fila por nivel, y los 14 y 15 sin brillo (Q-C97) | CL6.3 |
| Los slots de las texturas de los modelos: un próximo que fija cada cargador y la búsqueda por nombre desnudo | `ModelTextureSlots` con el mismo próximo por cargador y la misma búsqueda (Q-C90), y un golden local contra `lor_slots.bin`; el login sigue el próximo del mundo (Q-C92) | CL6.1, CL6.3, CL6.4 |
| La selección: la lista en cinco lugares fijos, el picking contra una caja corrida, el doble clic por reloj y la luz 0,4 / 1,4 | `CharacterSelectScene` con las tablas de arriba; un lugar de más de 4 se descarta (Q-C101) | CL6.3 |
| La barra de la selección (crear, el menú, conectar y borrar) y el borrado con dos avisos | `UI::SelectionBar` (con `Button` deshabilitado), `UI::DeleteChecks` (las revisiones de `0x63edf0`), `UI::NoticeWindow` con los tipos 0, 3 y 4 y el campo del código, y la escena, que sigue la lista de la sesión (el que crea entra y el que borra se va) y abre el aviso de cada respuesta. El menú, las opciones, los carteles y el área de mensajes, de la CL8 | CL6.5 |
| La ventana de crear, con los filtros del nombre de `FilterName.bmd` y `Filter.bmd` | `UI::CreateCharacterWindow` (la vista previa, `Scenes/CreatePreview`: `SelectionModels` con sus slots desde `0x6f8` siguiendo `GameModels`, y la cámara, el viewport y la tabla de clases de `0x63e460`), `UI::NameChecks` (los tres chequeos de `0x63d990`) y `Data::Text::LoadFilterWords` en `LoadGameData` | CL6.5 |
| El caballo oscuro: un ayudante que aparece de a poco y sigue la acción del jinete, con luz por vértice, un cromo y su sombra | `Scenes/DarkHorse` (el paso) y `DrawDarkHorse` (el caso `0xbf` de `0x6002e0`), con `Render::Models::NormalLight` (L = (1,3; 0; 2), la normal con la escala del objeto) y `ModelBlend::BlendedShadow`; en el login, entre el personaje y el logo | CL6.4 |
| El caballo del juego: la tabla de 10 ayudantes (`0x7d335d0`), el que se apaga sin su jinete y el que se oculta en la celda segura de su jinete | `Scenes/HelperTable` en `WorldState::Helpers`: crear, apagar, el paso de la escena 5 y su dibujo, con errores donde el original crea efectos o partículas que el juego todavía no tiene | CL7 (hito 1, T12) |
| El mundo del login: el mapa 55 entero, con los objetos después de los personajes y casos por tipo y por mapa (`0x606660`, `0x604ab0`, `0x6002e0`) | en el lote del login: el terreno (`LoadedWorld`, CL7.1) y los objetos del `.obj` (`WorldModels`, `WorldObjects` y `WorldObjectsDrawing`, CL7.2), con los casos del mapa 55 como datos y el orden del original | CL7.1, CL7.2 |
| El dragón: un efecto con modelo (`0x518cf0`) que vuela en ocho, sin azar | el efecto `0xb4` con su paso por paso de lógica, comparado con los pasos emulados del código real | CL7.6 A |
| Los sprites de un frame (`0x5491e0`), con el billboard de `0x621400`: el dibujo del personaje pide los 30 destellos de la tela y los cinco del cetro, y `0x549440(0)` los dibuja con la modelview en la identidad y los apaga | `Render::MakeBillboard` (bit a bit contra `0x621400`), `World::FrameSprites` (la tabla, con el alpha de `0x549290`) y `Scenes::DrawFrameSprites` en un pase de ojo, el último del 3D, que se abre solo con alguno vivo; `DrawnCharacter::sprites` recibe los destellos al dibujar la tela y el cetro (`World::CreateScepterGlow`, bit a bit contra el caso `0x620` de `0x509340`) en ese orden, con el `rand()` de la CRT de `ClothWorld`. El original sortea el cetro en cada frame dibujado, a 21,33 por segundo; MuRet lo sortea una vez por frame del original (46,875 ms) que pasó y, entre medio, repite el último sorteo en el hueso de ahora (`World::ScepterGlowCarry`, como la tela, Q-C124): a 60 FPS el tridente cambia al ritmo del original (`TheGlowIsDrawnOncePerOriginalFrame`, `TheScepterGlowChangesOncePerOriginalFrame`). Con varios frames del original de golpe, los de la tela y los del cetro salen agrupados y no intercalados (P-C22 a) | CL7.6 B1, cierre |
| Las partículas (`0x53fe70`, `0x5446e0` y `0x548550`), con el aliento del caballo desde el buffer global de la pose | `World::Particles` (la tabla de 3.000, el viento y el aliento, bit a bit contra el código real) y `Scenes::DrawParticles` en el pase de ojo, después de los sprites; el aliento lo pide `LoginScene` después del paso del caballo, en el hueso 27 del último modelo dibujado que lo tiene sin ser dummy (`PosedBonePoint`) | CL7.6 B2 |
| Las hojas (`0x530490` / `0x5306a0`), con las 80 brasas del mapa 55 y la altura del terreno de `0x5f93c0` | `World::Leaves` (las 80 brasas: nacen y caen, bit a bit contra el código real) y `World::TerrainHeightAt`; `Scenes::DrawLeaves` en el pase del mundo, después de los efectos con modelo; `LoginScene` las avanza después del caballo, con el héroe y la cámara en (0, 0, 0) hasta el primer dibujo (Q-C137) | CL7.6 B3 |
| `0x602ed0`, después del dibujo de cada objeto visto: el `rand()` de cada uno y el brillo del farol (`0x3c`) en la escena 2 | `Scenes::ObjectFrameEffects` en `DrawWorldObjects`: un `rand()` de la CRT por objeto visto y los dos sprites de `flare01` en los huesos 8 y 9 del último modelo posado (`LastPosedBonePoint`: el dueño del dragón, Q-C140); `World::StreetLightGlowLight`, la luz; el dueño (`0xb4`) en `m_objects`, antes del `.obj` | CL7.6 B4 |
| Los joints (`0x53c6a0`) | un sistema con su tabla, cuando una escena los use (el login no tiene ninguno) | — |
| El cuervo: una mascota del ítem `0x1c17` con su propio paso, al final del paso del dueño, y su dibujo al final del de él | `Scenes::DarkSpirit`: el constructor (`0x4167b0`) y el paso (`0x416a20`, estados 0 y 1), bit a bit contra el código real (`raven.txt`); `DrawDarkSpirit` (mallas 3 y 2 y la sombra de las cinco); la chispa nace en `World::Particles` (`0x5c6`, subtipo 5); `ConstructLoginActor` lo crea en el dueño del pool; `StepCharacters` lo avanza dentro del cuerpo y `LoginScene` lo dibuja al final del dibujo del dueño | CL7.6 C1 y C2 (las chispas) |
| La escena 5 en dos tiempos: el `F3 03` en el primer paso y el mundo con la respuesta, con el mosaico de la escena 3 en pantalla mientras tanto; su paso y su dibujo, con su propio orden del 3D | `GameScene` (hoy manda el `F3 03`, retiene el frame con el contador y dibuja hasta el primer pase del terreno) retiene el último frame mientras espera, carga el mapa del paquete, arma al héroe con la cuenta de `0x511290` y dibuja en el orden de `0x6371a0`; los casos del mapa 0 como datos, como los del 55 (propuesta del barrido, P-C28) | CL7 |
| La cámara del juego: `0x635ae0` por frame dibujado, con el far del rectángulo en todos los mapas y sin el mouse | un valor con su estado (D, la meta, el zoom, el nivel, el yaw y la altura del `0x40`) y `OriginalVectorIRotate` (`0x5fcc30`), bit a bit contra el código real emulado y la traza de Lorencia (propuesta del barrido, P-C29) | CL7.3 |
| Caminar: el A* de `0x4ead70` con su árbol y sus topes, el `D3` de `0x56acd0` y el paso entre celdas con el spline y la celda lógica adelantada | `TerrainWalls` y `PathFinder` implementan la parte pura de distancia cero contrastada con ASM; `Protocol::Movement` implementa el wire. El paso, el RNG del consumidor, el picking y la integración de la escena siguen pendientes; sin nuevas comparaciones emuladas | CL7.4 A/B; C–E pendientes |
| Los cambios de mapa dentro de la escena 5, con los ganchos de cada mapa como `if` sueltos en cinco puntos | una tabla por mapa y una interfaz de ganchos (al cargar, al crear un objeto, en su paso, después de dibujarlo y en el paso de la escena), con Lorencia primero (propuesta del barrido, P-C31) | CL7.5 |

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
  las UV de la traza a menos de 0,02 (CL6); esas tres filas de la tabla, con un T fijo (`ShineTest`,
  CL6.2), y las de `0x200` y `0x800`, que piden los niveles 11 y 12 y el `0x840` de `0x60b3d0`,
  cuando entren con `ItemGlow` (`ShineTest.TheChrome2PassRunsWithAFiveSecondClock` y
  `TheChrome3PassIsTheLightAgainstTheNormal`, CL6.3).
- Los slots: la carga de los modelos de MuRet sobre el `Data\` (`GameModels`, con las 505 llamadas
  de `0x60f7d0` y `0x611850` y las 4 de `0x615700` desde su `0x3b8`) da los nombres de
  `lor_slots.bin` de `0x12e` a `0x370` y de `0x3b8` a `0x3c6` (el caballo), y deja el próximo en
  `0x3c7` (`ModelTextureSlotsGoldenTest.StartupMatchesTheDump`, local, fuera de CI); de `0x371` a
  `0x3b7` y de `0x3c7` a `0x41b`, las otras habilidades, los efectos y el mundo, cuando entren.
- El asignador de slots de `0x466560` no se prueba, porque MuRet no lo reproduce (pregunta 11 del
  plan de la CL5): es interno, en las escenas medidas no corre (`0x466000` no corrió, § Texto), y
  la caché del texto de MuRet es otra, con sus slots después de los 1.845 del original (CL5.3).
- El login completo (CL7): los objetos del mapa 55, cada tipo como lo deja el código real
  (`scripts/login/run_objs.py`: la creación, tres pasos y el dibujo); el dragón, paso a paso contra
  los 1.500 pasos de `dragon_emu.py`; el cuervo, bit a bit contra `raven_reference.py` y
  `emulate_raven.py` en varias semillas; y las brasas, el aliento, el cetro y los destellos, con las
  fórmulas de [Los efectos del login](#los-efectos-del-login) y un `rand()` fijo.
- La escena del juego (CL7), con lo que propone el barrido: la respuesta del `F3 03` del cable, sin
  nombres, da el héroe en (13.350; 12.750) del mapa 0 con −45°, y el frame queda retenido hasta
  ella; los slots `0x69..0x6b` y `0x41c..0x47e` de Lorencia dan los nombres de `lor_slots.bin`; los
  objetos del mapa 0, al crearse, en tres pasos y en el dibujo, contra el código real emulado; la
  cámara de Lorencia da `466b446d`, `46313b93` y `447dc000`, y los vectores de `0x635ae0`, bit a
  bit; el camino, el `D3` y el paso entre celdas contra el código real emulado, y los 171 caminos
  del cable, solo con las coordenadas; y los gates de Lorencia con el `Gate.bmd` real.
