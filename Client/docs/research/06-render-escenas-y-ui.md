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
  `ZERO/SRC_COLOR`.
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
  con el jinete del tipo `0x20a` (o el tipo `0x1c12`), busca un lugar libre en los 10 ayudantes de
  `0x7d335d0` (de `0x1d4` bytes) y llama a `0x443d70`. En el login, `0x6349a0` lo crea con el
  personaje de `0x517270` (`0x443fa0(0xbf, +0x880, +0x870)`); en la selección, `0x513430` y
  `0x5139f0` (el armado desde la lista) lo crean con el bit 0 del byte 11 (`+0x318` en `0x1c16`).
  `0x443d70` lo deja vivo con el alpha (`+0x174`) en 0 y su meta (`+0x170`) en 1, la luz por vértice
  (`+0xe8`) prendida, el blendMesh y la malla oculta en −1, la luz del blendMesh (`+0x68`) en 1,
  `+0xf4..+0xfc` en 3, la velocidad (`+0xd8`) en 0,5, los ángulos del jinete y, con el `0xbf`, la
  escala en 1.
- **Su paso** (`0x445230` → `0x444010`, en el login desde `0x634c60`, después de `0x606560` (el logo)
  y antes de `0x508f00` (los personajes): ve la acción que el jinete tiene en ese paso).
  `0x4e9980` lleva el alpha hacia su meta un décimo (`0x6b87b8`) de lo que falta con `+0x16d` en 0, y
  baja `+0x68` al alpha si es mayor: como el alpha sube, `+0x68` queda en el del primer paso, 0,1
  (Q-C112). En una celda de zona segura (el bit 0 del atributo en `0x7d0d348`, la del **jinete** por
  0,01: `0x4440b8` lee `[EDI + 0x14]` con `EDI = [ESI + 0x108]`) el alpha va a 0 y salta al avance,
  con la `+0xd8` de antes, sin copiar nada del jinete: la celda (106, 141) del mundo 56 tiene
  `0x08`, y el mundo 56 no tiene ninguna con el bit 0. Si no, copia del jinete la posición, los
  ángulos y `+0x28..+0x30` (la cabeza), pone el hueso de la cabeza del modelo (`+0x54`) en 7 (en
  `DarkHorse.bmd` es una raíz sin hijos que ningún vértice usa: no se ve) y elige su acción por la
  del jinete (`+0x111`), con `0x4ea030`:

  | La del jinete | La del caballo | `+0xd8` |
  | --- | --- | --- |
  | `0x53` | 3 | 0,34 |
  | `0x4b` | 1 (y su polvo: en el mapa 10, siempre; en los demás, con `rand() % 2`, el `0x5f2` en el 2 y el `0x5f3` en los otros) | 0,34 |
  | `0x4e` a `0x52` | 2 | 0,34 |
  | `0x54` | 5 | 1 |
  | `0x55` | 6 | 1 |
  | otra (la `0x4a` del login) | 0 | 0,3 |

  Después, con `rand() % 3` en 0 y la acción distinta de 1, el aliento (`0x5f2`, o `0x607` en el
  mapa 7; antes este documento lo daba como polvo, [Los efectos del login](#los-efectos-del-login)), que `0x53fe70` pone en el arreglo de `0x743a198` (de `0x78` bytes; lo mueve `0x5446e0` desde
  `0x634c60` y lo dibuja `0x548550` desde `0x6350b0`), desde el hueso 27 (`0x4ec110` con
  `0x68b1d2c`, *inferido*: en el paso, ese buffer tiene la pose del frame anterior); con la 3, otro
  efecto; con `jinete + 8` en 1, doce `0x60f` (`0x530860`), y `+8` vuelve a 0. Y avanza con
  `0x4ec200` a `+0xd8`.
- **Su dibujo** (`0x6350b0`: `0x510d00`, los personajes; `0x4453b0`, los ayudantes; `0x6046a0`, los
  objetos). `0x445260` prende `+0x16c` con el frustum (`0x5fba90`, en su lugar por 0,01, −20) y, con
  el jinete `0x20a`, le pone la escala 1,6 en la escena 2 (1 en la 4) y llama a `0x602e90(caballo, 0,
  0, 0)` (10 con `+0x78 & 0x4000` del jinete). `0x5ffdf0` no lo dibuja con el alpha por debajo de
  0,01; si no, el color de `0x5ffce0` con luz por vértice (la del terreno más 3), que `0x6002e0`
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
  dinámicas (`0x5f9780`, [Lo propio de Lorencia](#lo-propio-de-lorencia)).
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
  (`0x32..0x34`), las hojas (`0x64`, `0x65`) y la lluvia (`0x66..0x68`), que el terreno no usa. Los mapas
  `0x18` a `0x1d` y el `0x24` arman los suyos (`0x4377a0`).
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
  cabeza del paso en el dibujo: el original no la interpola.
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
- `0x633f96`..`0x63417a`, la tabla de hash de `0x57630c8`: no se leyó;
- vacía el área de mensajes (120 líneas, `0x555190`) y, si alguno de los dos números de `pkt[5]`
  de la lista (`0x5767958`, `0x576795c`) no es 0, escribe una línea con `sprintf` y el texto 1240
  **como formato** (`0x63420a`).

### La lógica

Cada paso, `0x6342c0` anima (`0x508ea0`, la vista previa entre otros) y corre las rutas de la
cámara (`0x633920`). Después:

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
sigue bajo un aviso. MuRet la avanza una vez por paso, solo con la ventana arriba y quieta bajo un
aviso (el ruling de la B3, en el plan de la CL6).

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
`Scenes::ApplyWorldEntry` (T7, [abajo](#la-entrada-al-mundo)) hasta la frontera de `0x512a30` y no
dibuja nada en la escena 5.

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
  1`, `+0x371 = 'C'` y `+0x188` con el contador de pasos. El ángulo es (dir − 1) · 45
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
  entrada va a `0x5bc740`, que la pasa al héroe con `0x512a30`. Si el `F3 10` se despacha en la
  misma vuelta de `WinMain` que la respuesta, el héroe sale vestido desde el primer frame (P-C28 a).
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
   `0x220` bytes en `0x77f4714`), y `0x510f70(0x57678bc)`, que mata a todo personaje cuya clave
   `+0x1f4` no sea la del héroe y suelta lo que le cuelga (`0x421d60`, `0x4fb1a0`, `0x414340`).
   Recorre hasta `0x69780`, **400 registros de `0x438`**, uno menos que el pool (el 401 no lo
   mira), y compara la clave **con signo** (`movsx` en `0x510f8a`) contra el DWORD que recibe: una
   clave de `0x8000` o más nunca coincide, así que mata hasta al dueño de esa clave (*estático*, T7).
   Después `0x451d10` → `0x451590` borra de la lista `0x7245c8` lo que no es del héroe, y el objeto
   de `0x6eeda0` queda con `+4 = 1`, `+8 = 0xf3`, `+0xc = 400` y `+0x18 = 0` (sin leer qué es).
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
   crea los dos objetos del 2; la carga de sus dos modelos no está en `LoadMap` (el dibujo es la
   T8). Los dos registros de `EncTerrain3.obj` con la y en NaN (492 y 493, tipo 86) caen en la
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

- **Los mapas.** Con la `Data` del repo cargan el 0, 1, 2, 4, 6 a 11, 18, 31, 33 y 54. Cortan: el
  3 en `0x606660` (la torre `0x142` pide efectos que MuRet no crea), el 5 por el archivo que falta
  en `World6`, del 24 al 29 y el 36 en `0x4377a0`, el 30 en `0x42a760`, el 34 en `0x42fe50` (el
  `.att` según Crywolf, antes del `BD`, que por eso no sale) y el 55 en `0x6188ae`. Sin
  `TerrainHeight.OZB`, el error nombra el archivo, con el pasto ya sacado (Q-C180).
- **Los mapas de evento** (`0x447d70`: 9, 11 a 23) cortan después de la carga: el objeto de evento
  (`0x447eb0`, `new 0x134`) no está portado.
- **La respuesta** corta hoy dentro de `0x512a30`: en `0x550d10`, en su primera llamada, `0x40e1b0`
  ([Los stats del registro propio](#los-stats-del-registro-propio)), o en `0x505820` si el lugar
  sorteado es el del héroe anterior (`0x512ee7`, con `0x743a170` todavía en él). La sesión
  queda en `Failed` con el héroe sin publicar (`0x743a170` sin tocar), el contador sin bajar a 30 y
  los `rand()` gastados hasta ahí: el primer y el segundo cierre del registro, el pasto, el lugar del
  héroe, los 28 de `0x511290` y lo que `0x512a30` sacó antes de cortar. Lo que sigue a `0x512a30`
  (filas 6 a 17) está en `Scenes::SettleWorldEntry` y se prueba directo; el cuervo de la fila 11 corta
  en `0x420d60`. Lo que MuRet no modela no se escribe: `0x7d36874`, `0x77f3b67`, `0x553780`,
  `0x77f3b10`, `0x6c8dcc`, los elegidos de la fila 14, `0x78abb0c`/`0x78abb10`, ResourceGuard
  (Q-C147) y la fila 19 (los mensajes, CL9). Los sonidos se calculan y se descartan (sin audio
  hasta la CL10).
- **La clave del héroe** (`0x57678bc`) es la del `F1 00`, que la escribe en `0x4c1ec0` antes del
  chequeo de versión ([04](04-protocolo.md)).
- **Divergencia previa**: la selección (escena 4) todavía no gasta los 28 de `0x511290` ni los
  65.536 del pasto al cargar el 54.

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
9. Si el personaje es el de `0x743a170` (el héroe publicado; `0x512ee2`), `0x505820` (`0x512ee7`;
   914 bytes), sin leer en detalle: busca los conjuntos, escribe `0x743a178` y `0x743a17c` y, con la
   clase 3, abre otra vez el registro.
10. [`0x550d10`](#los-stats-del-registro-propio) (`0x512ef5`, `thiscall` sobre el registro): los stats.
11. Cierra el registro (en línea, `0x512efa..0x5130cf`).

En la respuesta del `F3 03` el héroe todavía no está en `0x743a170` (lo pone la fila 6), así que esa
primera llamada no corre `0x505820`. MuRet (`Scenes::ApplyActorEquipment`) hace los pasos 1 a 9 y
corta con un error explícito donde falta la lectura: `0x505820`, y adentro de `0x550d10`, en su
primera llamada, `0x40e1b0`. El cierre del registro gasta los 3.848 `rand()` también cuando corta.

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
7. Ocho helpers con `ecx` en el registro (`0x551287..0x5512ba`), sin leer en detalle: `0x54efe0`
   (2.304 bytes), `0x54f9b0` (873), `0x54fd40` (193), `0x54fe10` (2.471), `0x5507e0` (425),
   `0x550990` (653), `0x550c30` (46) y `0x550c60` (63). `0x54fe10` empieza escribiendo `+0x4c` y
   `+0x58`, las velocidades del keepalive ([04](04-protocolo.md), el `0E`; P-C28 i): `+0x1a` más
   `+0x2c`, sobre 50 y 50 en la clase 2 (`+0xb & 7`), 15 y 20 en la 1 y la 3, 10 y 10 en la 4, y 20
   y 10 en las otras (`0x54fe1e..0x54fefb`); después, según `0x7879fbc`, `0x78ab9b8` y `0x78a6758`,
   lee el ítem de `0x7879f80` en lugar de uno del equipo, y en algunos caminos (con el bit `0x04` de
   `[0x7681eb0] + 0x3c`) abre otra vez el registro. `0x550c30` y `0x550c60` escriben `+0x64` y
   `+0x66` con `0x54e6b0` y `0x54e6d0` (los lugares 2 a 7, y el 6 y el 7).
8. La cola (`0x5512bf..0x5513b1`), con WORD que envuelven:
   - `+0xef8 = +0xef0 − +0xee6` y `+0xefa = +0xef2 − +0xee6`;
   - un `rand()` (`0x5512f2`) y `+0xefc = rand() % (+0xee4 − +0xee2 + 1) + +0xee2 − +0x62`: los dos
     WORD sin signo y el `idiv` de 32 bits con signo. Con `+0xee2 = +0xee4 + 1` divide por 0
     (Q-C185); con `+0xee2` mayor, el divisor es negativo y el resto es el `rand()` entero;
   - `+0xefe = +0x4e − +0xeec` y `+0xf00 = +0x60 − +0xeea`, cada uno con tope 100 si pasa de 100 sin
     signo (`jbe`);
   - otro `rand()`: `+0xf02` en 1 si `rand() % 100` es menor que `+0xefe` (`jge`), si no en 0; y
     otro, igual, para `+0xf03` con `+0xf00`.

`0x40e1b0` (1.290 bytes; *estático*, leído solo en su forma) es de la clase de `[0x6eed3c]`, cuyas
tablas carga `0x40c600` de `Data\Local\ItemSetType.bmd` (`0x40c660`) e `ItemSetOption.bmd`
(`0x40c850`): 4 bytes por tipo desde `+0`, que `0x40cfa0` indexa con `D+0x1c & 3`, y registros de
`0x4c` desde `+0x8004`. Recorre dos veces los lugares 0 a 11 salvo el 7 y el 8 (`0x40e1e6..0x40e1f8`
y `0x40e4cf..0x40e4e1`), y abre y cierra el registro en cada uno (en la segunda pasada, a veces con
`0x40f370`) y una vez más en el medio, para leer `+0x18`, `+0x1a`, `+0x1c` y `+0x1e` de
`[0x7681eb0]`: con `0x512a30` afuera, ninguno de esos cierres gasta. Llama además a `0x40cfa0` (100
bytes), `0x40d010` (1.298; lee la clase del héroe publicado, `[0x743a170] + 0x1d4`, y escribe su
`+0x358`) y `0x40de10` (898).

MuRet (`Scenes::RecalculateOwnStats`) corta en el paso 1 con un error, antes de tocar el registro y
sin `rand()`: no carga las tablas de los conjuntos. Los pasos 3, 4, 6 y 8 están portados aparte
(`ClearOwnItemStatWords`, `AddOwnItemStatBonuses` con `ItemStatBonusOf`, `AddCapeOfLordOptions` y
`UpdateOwnDerivedStatsTail`), cada uno con su prueba, para cuando exista lo de antes; los pasos 1,
2, 5 y 7 son la frontera.

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

### El paso del juego

El paso de la escena 5 es `0x636290` entero (3.844 bytes), después de `0x623e70`, la UI del juego
([02](02-arranque-y-ciclo-de-vida.md#qué-corre-en-cada-paso-y-qué-en-cada-frame)). Después del
bloque de la entrada, en orden (*estático*):

| Dónde | Qué llama | Qué hace | En MuRet |
| --- | --- | --- | --- |
| `0x636da3` | el estado `0x3d` pone `0x7d3a101 = 1` (`0x636dac`) | si no, y `0x7d3a101` está en 0, sale (`0x636dbc`) | sí (`GameScene::StepWorld`) |
| `0x636dc2..0x636ddb` | el temblor `0x7d325c8` · 0,2 | [La cámara del juego](#la-cámara-del-juego) | falta |
| `0x636dd4` | `0x652940`, si el héroe tiene `+0x371 == 'C'` (siempre: lo pone `0x511290`) | lo del asedio y del gremio (`0x4294b0`) | no hace falta |
| `0x636de8` | **`0x5fbf60`** | copia la luz sombreada `0x7c0ccc4` al buffer dinámico `0x7b4ccc4` en la ventana del rango y escribe las olas | las olas (`LoadedWorld::Step`); la copia de la luz, no |
| `0x636e11..0x63705b` | con `0x7d36899` en 0: `0x5ecdc0`, `0x46b7b0`, `0x490490`, `0x5ebca0`, `0x435bd0`, `0x5ee140`, `0x59ff10`, `0x5a7cc0`, `0x657930`, `0x659290`, `0x45a9d0` y dos por vtable | la UI, el chat y los paquetes de la interfaz | CL8, CL9 |
| `0x63706a` | **`0x606560` → `0x604ab0`** | los objetos del mundo ([Lo propio de Lorencia](#lo-propio-de-lorencia)) | sí, con los casos del 55 |
| `0x637078` | `0x608880` | los ítems del piso que caen | CL9.3 |
| `0x637082..0x6370e5` | **`0x530490`**, en el mapa 0 si la capa 1 de la celda del héroe (`0x77f36d0`) no es 4 | las 80 hojas | solo las brasas del 55 |
| `0x6370ea` | **`0x446120`** | los pájaros | no |
| `0x6370ef` | **`0x447030`** | los peces | no |
| `0x6370f4` | `0x445230` | el caballo oscuro | sí (`StepDarkHorse`) |
| `0x6370f9..0x637105` | `0x557650`, `0x451d10`, `0x451c10` | contadores y listas, sin leer | — |
| `0x63710a` | **`0x59adf0`**, el `MoveHero` | el héroe con el mouse ([Caminar](#caminar)); baja `0x77f3b04` y lo vuelve a leer (dos accesos, 8 `rand()`) y, con más de 30, sale; si no, escribe en `0x77f36d0` la capa 1 de la celda del héroe (`0x59e2ba`) y, con `0x57cfc0`, abre y cierra el registro propio: 3.848 `rand()` ([La tabla de protección](#la-tabla-de-protección)) | el contador (`GameScene::StepWaitCounter`), sin los gates de antes (P-C28 b); lo demás falta |
| `0x63710f` | **`0x508f00` → `0x508ea0`** | por personaje: el trapecio en (x, y) · 0,01 con −20 a `+0x16c`, `0x508ba0`, `0x4fd5f0` (la animación y el alpha), `0x505de0` (la cabeza y las luces) y `0x42da90`; antes, el `0x02` del `TerrainWall`, con 8 `rand()` por personaje vivo | la animación, la cabeza, `CharacterSeen` |
| `0x637114` | `0x548f40` | los números de daño (`0x7609c0c`) | CL9.2 |
| `0x637119` | `0x53eaf0` | la tabla `0x75fe028` (`0x51fc80`), sin leer | — |
| `0x63711e` | **`0x52d220`** | los efectos con modelo | sí (`StepModelEffects`) |
| `0x637123` | `0x53c670` | los joints | no |
| `0x637128` | **`0x5446e0`** | el viento y las partículas | sí (`StepParticles`) |
| `0x63712d` | `0x549090` | los sprites con vida (`0x760cac0`) | sin mirar |
| `0x637138` | `0x403590` | `0x4033d0` y compañía (el recorrido de la cámara), sin leer | — |
| `0x637144..0x63717a` | `0x5fc0d0` y `0x5fc0b0`, con `0x7d2d35e` | funciones de un `ret` | nada |
| `0x63717f` | `0x5ac8d0` | F1 a F4 (CL3.3) | CL8 |

### El dibujo del juego

`0x6371a0`, en el orden de sus llamadas (*estático*; lo que está en la traza, *verificado*). La
última columna dice qué tiene ya MuRet por el login o la selección:

| Dónde | Qué llama | Qué hace | ¿Como en el login? |
| --- | --- | --- | --- |
| `0x6371a6` | con `0x7d3a101` en 0, devuelve 0 | sin leer el contador | `GameScene::PrepareFrame` |
| `0x6371b7..0x63722e` | el contador por la tabla de protección, con su cierre (`0x40b800`, `0x637229`); **con más de 30, devuelve 0** (`jle` con signo) | no dibuja ni presenta | `GameScene::PrepareFrame` (T5) |
| `0x63723a` | `0x7d368a0 = 0` | sin niebla | sí |
| `0x637241` | **`0x635ae0()`** y, fuera del mapa 6, `0x44dc60(&p)` (`0x4034b0` con el recorrido) | la cámara y el punto del recorte | [La cámara del juego](#la-cámara-del-juego) |
| `0x63734d` | `0x620db0(0, 0, w, 48)` | la franja, que no dibuja | sí (`MakePass3D`) |
| `0x637366` | **`0x5fbb00(w / 640, &p)`** | el rango de bloques y el trapecio | sí (`TerrainRangeOf`, `TerrainPolygonOf(Game)`) |
| `0x637391..0x637404` | `glClearColor` por mapa: en el 0, (0,0390625; 0,078125; 0,0546875) | no se usa (Q-C143) | ver abajo |
| `0x637410` | `0x620db0(0, 0, w, 432)` (480 con `0x7d36899`) | el pase del mundo | sí |
| `0x63741d` | en el mapa 30, su luz | | — |
| `0x6374aa` | `0x620480(mouse x, mouse y, &0x7d366c0, 1)` | el rayo del mouse | [Caminar](#caminar) |
| `0x6374c6` | **`0x5fc260(0)`**, si `0x437850()` es falso y el mapa no es 10 | el terreno: el pase opaco, el de alfa, **el del pasto** y `0x549130` | `LoadedWorld::DrawTerrain`, **sin el pasto** |
| `0x6374ce` | **`0x6046a0` → `0x602e90` → `0x602ed0`, `0x5ffdf0`, `0x6002e0`** | los objetos del `.obj` por sector | `DrawWorldObjects`, con los casos del 55 |
| `0x6374d3` | **`0x52dc20`** | los efectos del suelo (`0x61d`, `0x61e`, `0x61f`…), con `0x5fb2f0` | no |
| `0x6374d8` | **`0x446b60`** | los pájaros, con su sombra | no |
| `0x6374dd` | **`0x510d00` → `0x50ba30`** | los personajes vistos (`+0x16c`), por índice; `0x42afe0` para los jugadores, solo en el mapa 30 | `DrawCharacter`, con la luz del login |
| `0x6374ed` | `0x5fc260(1)` con `0x77f3b18` | el picking del terreno con esa bandera (*inferido*: el editor) | — |
| `0x6374fe` | `0x608a00`, con `0x7d36899` en 0 | los ítems del piso | CL9.3 |
| `0x637503` | **`0x446f00`** | los peces, con su sombra | no |
| `0x637508` | `0x4453b0` | el caballo oscuro | `DrawDarkHorse` |
| `0x63750d` | **`0x5306a0`** | las hojas | `DrawLeaves` |
| `0x637516` | `0x53c6a0(local_8)`; en el mapa 0, con 0 | los joints | no (el login no tiene) |
| `0x63751d` | **`0x52d620(0)`** | los efectos con modelo | `DrawModelEffects` |
| `0x637525` | `0x52e590` | cintas en `GL_TRIANGLE_FAN` (`0x75e987c`, texturas `0x5bb` + n) | no |
| `0x63752a` | **`0x549510`** | los sprites del frame | `DrawFrameSprites` |
| `0x63752f` | `0x6213e0` | la modelview en la identidad | sí |
| `0x63754c` | `0x5306a0` otra vez, solo en el mapa 2 | | — |
| `0x637552` | **`0x549440(0)`** | | sí |
| `0x637558` | **`0x548550(0)`** | las partículas | `DrawParticles` |
| `0x63756a` | `0x548eb0(0)` | los números de daño, sin depth test | CL9.2 |
| `0x637572` | `0x6213f0` | el `glPopMatrix` | sí |
| `0x637577..0x6375e5` | si `0x437850()`, un segundo pase | | no en el mapa 0 |
| `0x637641` | `0x59ec80` | qué personaje está bajo el mouse | [Caminar](#caminar) |
| `0x637646..0x6376e6` | `0x6219b0`, `0x4383f0`, `0x5bbbc0`, `0x5a7320(1)`, `0x5a7eb0`, `0x490390`, `0x5f37f0`, `0x62db60`, `0x5f7bc0`, `0x554810`, `0x5554e0`, `0x5b0550`, `0x40eca0`, `0x65ee60`/`0x65fe00`, `0x5ac2d0` × 2, `0x633110`, `0x621a90`, `0x620fc0` y uno por vtable | el 2D: el HUD, las ventanas, el texto y el cursor | CL8 |

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
celdas. La integración temporal (pares cámara→trapecio por frame original, dueño persistente,
reentrada y faseZ163) queda pendiente; este núcleo no cierra CL7 ni sus callers.

- Quién lo usa:

| Qué | Dónde | Prueba | Margen |
| --- | --- | --- | --- |
| los bloques y las celdas del terreno | `0x5fc190`, `0x5fc0e0` | el trapecio, en (x + 2, y + 2) y en el centro | −40 y 0 |
| los sectores y los objetos | `0x6046a0` | el trapecio | −180 y −30 |
| los pájaros y los peces | `0x446b60`, `0x446f00` | el trapecio | −20 |
| **los personajes, en el paso** | `0x508f00` (`0x50918c`), a `+0x16c` | el trapecio **del dibujo anterior**, en la posición · 0,01 | −20 |
| los efectos con modelo y los ítems del piso | `0x52d620`, `0x608a00` | una esfera contra los cinco planos de `0x5fbb00` (`0x5fbf00`; los ítems con radio 400) | — |

`0x508f00` además le prende el `0x02` a la celda de cada personaje (`0x509165`,
[Caminar](#caminar)). Los cinco planos salen de la cámara y de las cuatro esquinas lejanas, con la
modelview de GL (`0x5fd000`), que arma el driver, como el rango (P-C24 a).

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
temblor se achica por paso. En MuRet, `Render::MakePass3D` y `World::TerrainCulling` (el trapecio de
la escena 5 con el nivel 0) ya sirven; faltan la cuenta de `0x635ae0`, con `0x5fcc30`, y los pases
de `GameScene::Render`.

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
posición viva, suma Y 0/4/8/12/20 y Z 350, prestando ángulos y luz del objeto. El efecto 180 de las
escenas 2/4 recibe owner objeto y escala 1,6, después de ocultarlo y poner su luz blanca; también corre
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

**Antes de dibujar** (`0x602ed0`, `0x602ffb`; además del `rand()` por objeto que ya tiene MuRet,
*estático*):

- `0x38` (`MerchantAnimal01`): dos sprites `0x5ac` en dos huesos del buffer global de la pose
  (`0x68b211c` y `0x68b22cc`, `0x603149..0x6031d6`). En la traza, los dos `0x5ac` están en (13.094;
  12.142; 283), sobre la NPC de la tienda (13.065; 12.151), y no sobre el animal (*verificado* el
  lugar).
- `0x69` (`Waterspout01`): la luz del blendMesh en 1, la V con el reloj y, con un 50 %, dos
  partículas `0x5f2` en los huesos `0x68b184c` y `0x68b18dc`.

**Al dibujar** (`0x6002e0`): el único caso del mapa 0 es el `0x69`, con cuatro `RenderMesh(malla, 2,
alpha, 3, luz, …)`, la 3 con la U y la V, en vez de un `RenderBody` (*emulado*).

**Los pájaros** (`0x446120`, la tabla `0x7d2dca8`; el dibujo, `0x446b60`, con su sombra): en el 0
usan el modelo `0xad` (`Bird01`), solo sin tormenta (`0x723c50 == 0`), nacen al azar y se posan
(*estático*). En la traza hay 5, con sus 5 sombras (*verificado*). El ASM fija el tope en **5**:
`ESI` empieza en `0x7d2de18`, suma `0x1d4` por lugar y sale con `ESI >= 0x7d2e73c`
(`0x44642a/430`, `0x446a57`): índices 0 a 4 de `0x7d2dca8`. En 90 observaciones nativas de la
primera serie se vieron 5 vivos 85 veces y 4 las otras 5; el conteo observado no es la prueba
del límite estático. Queda su evolución temporal, en P-C28 j. La tormenta (`0x723c50`, que el `1C` apaga) tira rayos `0xc2` con el sonido `0x42` y
oscurece la luz del terreno alrededor del héroe.

**Los peces** (`0x447030`, la tabla `0x7d35350`; el dibujo, `0x446f00`, con su sombra `0x4eff40`):
en el 0, hasta 3 `0xb2` (`Fish01`), a ±512 del héroe y solo en celdas con la capa 1 en 5, el agua
(`0x5f82a0`; *estático*).

**Las hojas** (`0x530490`; el dibujo, `0x5306a0`): **80 en Lorencia** (*estático*, y las 80 con la
textura `0x64`, *verificado* en la traza). La cuenta de partida es `(-(mapa != 9) & −120) + 200`
(`0x530497..0x5304a3`): 200 en el mapa 9 y 80 en los demás, y después cambia en algunos
(*estático*): 40 en el 30 (`0x5304da`); en el 34, 60 o 50 con `0x5763358` en 1 o en 2 (`0x530500`,
`0x53050c`; *inferido*, el estado de Crywolf); y en el 10 y del 18 al 23, 80, con la meta de la
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
  tramo del ítem en el cursor (`0x59bb25..0x59cd28`) puede mandar un `C1 .. 24` (CL8, sin leer a
  fondo).
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
  no lo encuentra (Q-C174), y pone siempre `+0x388 = pkt[7] >> 4`. Al héroe, si está quieto, le pone
  la celda lógica en (X, Y), sin tocar la posición que se ve (Q-C176); si camina, solo la dirección.
  El servidor contesta cada `A3` con un `D3` del héroe con la celda final del camino que le mandaron
  y la dirección de `P0` (*verificado*: los 212 ecos que se encuentran en el cable). A los otros, el
  blanco a `+0x39a`/`+0x39b`; el tipo `0x186` salta a la celda sin caminar (el ángulo hacia el
  centro, `0x4f37c0` y `+0x399 = 1`); los demás, con `+0x3e5` en 0 (un freno de 60 pasos que pone
  `0x4c9790` y baja `0x505de0`), calculan `PathFinding2(celda, blanco, +0x3e8, 0)` con **su** A*:
  `Movement` es el resultado y, si falla, `SetPlayerStop`.
- **El `D7` del servidor** (`0x4caae0`) **no es movimiento: es el daño**. Dibuja el número
  (`0x548e40`, de 15 de alto) y, si no es el héroe, lo guarda en `+0x384`; al héroe le resta la vida
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
  la 3 o la 4, y sigue. Después, con `+0x114` en 0, suena un golpe que se elige con otro `rand()`
  según la clase (`0x4f5488..`). Todo esto gasta `rand()` (Q-C179).

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
de los mapas de vuelo en la escena 5, el `C1 .. 18` de sentarse y la tabla de sonidos.

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
`0x01` del atributo de la **celda lógica**, la que va una adelante.

### Los cambios de mapa

*Estático* salvo lo que se marca. Tres paquetes del servidor llaman a `0x61a560` ([La carga del
mapa](#la-entrada-al-mundo)): el `F3 03` (la entrada), el **`1C`** y el **`F3 04`**. Ninguno cambia
de escena: el juego sigue en la 5, porque las ocho escrituras de `0x6d5dc8` (`0x4c2f1a`, `0x4c363f`,
`0x622f93`, `0x633f05`, `0x634470`, `0x634638`, `0x634dce` y `0x635a91`) no están en esos handlers.
Los formatos, en [04](04-protocolo.md#los-cambios-de-mapa-1c-f3-04-y-f3-12).

**El warp, `1C` del servidor** (`0x4c6e10`, desde `0x4e3f2a`, que antes lo copia a la cola de
`0x77f27b4`). Con el cuarto argumento del dispatcher en 0 no hace nada de esto y manda un C3 con `C1
06 F1 03 00 1C` (*inferido*: el aviso de FoxAnti). Si no:

1. `0x5be150` (algo del ítem en el cursor, sin leer).
2. La celda del héroe (`+0x41c`, `+0x420`, cifradas) en `pkt[5]`, `pkt[6]`; la posición en `(celda
   + 0,5) · 100`; la z, la altura del terreno (`0x5f93c0`), más 30 con el `0x1c15` en `+0x318` y
   `+0x3e2` en 0, o más 90 así en los mapas 8 y 10; y el ángulo, `(pkt[7] − 1) · 45`.
3. Con `pkt[3] == 0` (la habilidad teleport, en el mismo mapa): `0x53d930` (la acción `0x93`, el
   efecto `0x5c6` y el sonido `0x76`), `0x576794c = 0` y al final. No manda nada.
4. Con `pkt[3]` distinto de 0 (un gate o un `/move`): `0x607b40`, `0x510f70` y
   `0x451d10`/`0x451590` (los ítems del piso, los demás personajes y la lista), **también sin
   cambiar de mapa**. Si `pkt[4]` es otro mapa: `0x6c9dd0 = pkt[4]` y `0x61a560`; en el 34, el `C1
   04 BD 00`; los sonidos del Chaos Castle (`0x41ed10(0x9e)`, `0x4906a0`, `0x4914e0`, `0x512a30` y
   `0x443d40` al entrar a uno, `0x41ee00(0x99)` y `0x41ee00(0x9e)` al salir); `0x447d70`; la z otra
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
y la z, con dos `0x512a30`. **No manda el `F3 12` ni toca `0x77f3b04`**. En el `muproxy.log` hay
dos, cinco segundos después del `17` (la muerte) del héroe, y ningún `F3 12` detrás (*verificado*).

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
   (`0x6002e0`: 7, 9, 10 y Kalima).

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
| 11 a 17 | el pasto no se dibuja por celda | `0x5faa79` |
| 18 a 23 | sin pase del pasto (`0x6c9d58` en 0) | `0x5f893b` |
| 24 a 29 y 36 | sin tiles; un terreno de agua propio de `0x114ad8` bytes en `0x723c48` | `0x4377a0`, `0x41cc70` |
| 30 | el agua al revés ✓ y la luz con otras constantes ✓; sin pasto; la niebla y el far de arriba | `0x5fa739`, `0x5f8d90`, `0x5f8942` |
| 34 | el agua suelta partículas | `0x5fa695` |
| 55 | las alturas por 3 ✓ | `0x5f924d` |

Fuera de Lorencia pero en el mismo lugar del paso: con la capa 1 en 3 o en 10 o más, Devias oculta
los techos `0x51`, `0x52`, `0x60`, `0x62` y `0x63` y no nieva.

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
| el `F3 04` (tres cierres), el `F3 10` (uno) y el `D7` al héroe (uno, en línea) | con cada paquete | 3.848 por cierre |
| `0x508f00`, las celdas lógicas al marcar el `TerrainWall` | cada paso, por cada personaje vivo | 8 |
| `0x635ae0`, la celda del héroe para el atributo | cada frame dibujado, también en las escenas 2 y 4 | 8 |

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
| El mundo del login: el mapa 55 entero, con los objetos después de los personajes y casos por tipo y por mapa (`0x606660`, `0x604ab0`, `0x6002e0`) | en el lote del login: el terreno (`LoadedWorld`, CL7.1) y los objetos del `.obj` (`WorldModels`, `WorldObjects` y `WorldObjectsDrawing`, CL7.2), con los casos del mapa 55 como datos y el orden del original | CL7.1, CL7.2 |
| El dragón: un efecto con modelo (`0x518cf0`) que vuela en ocho, sin azar | el efecto `0xb4` con su paso por paso de lógica, comparado con los pasos emulados del código real | CL7.6 A |
| Los sprites de un frame (`0x5491e0`), con el billboard de `0x621400`: el dibujo del personaje pide los 30 destellos de la tela y los cinco del cetro, y `0x549440(0)` los dibuja con la modelview en la identidad y los apaga | `Render::MakeBillboard` (bit a bit contra `0x621400`), `World::FrameSprites` (la tabla, con el alpha de `0x549290`) y `Scenes::DrawFrameSprites` en un pase de ojo, el último del 3D, que se abre solo con alguno vivo; `DrawnCharacter::sprites` recibe los destellos al dibujar la tela y el cetro (`World::CreateScepterGlow`, bit a bit contra el caso `0x620` de `0x509340`) en ese orden, con el `rand()` de la CRT de `ClothWorld`. El original sortea el cetro en cada frame dibujado, a 21,33 por segundo; MuRet lo sortea una vez por frame del original (46,875 ms) que pasó y, entre medio, repite el último sorteo en el hueso de ahora (`World::ScepterGlowCarry`, como la tela, Q-C124): a 60 FPS el tridente cambia al ritmo del original (`TheGlowIsDrawnOncePerOriginalFrame`, `TheScepterGlowChangesOncePerOriginalFrame`). Con varios frames del original de golpe, los de la tela y los del cetro salen agrupados y no intercalados (P-C22 a) | CL7.6 B1, cierre |
| Las partículas (`0x53fe70`, `0x5446e0` y `0x548550`), con el aliento del caballo desde el buffer global de la pose | `World::Particles` (la tabla de 3.000, el viento y el aliento, bit a bit contra el código real) y `Scenes::DrawParticles` en el pase de ojo, después de los sprites; el aliento lo pide `LoginScene` después del paso del caballo, en el hueso 27 del último modelo dibujado que lo tiene sin ser dummy (`PosedBonePoint`) | CL7.6 B2 |
| Las hojas (`0x530490` / `0x5306a0`), con las 80 brasas del mapa 55 y la altura del terreno de `0x5f93c0` | `World::Leaves` (las 80 brasas: nacen y caen, bit a bit contra el código real) y `World::TerrainHeightAt`; `Scenes::DrawLeaves` en el pase del mundo, después de los efectos con modelo; `LoginScene` las avanza después del caballo, con el héroe y la cámara en (0, 0, 0) hasta el primer dibujo (Q-C137) | CL7.6 B3 |
| `0x602ed0`, después del dibujo de cada objeto visto: el `rand()` de cada uno y el brillo del farol (`0x3c`) en la escena 2 | `Scenes::ObjectFrameEffects` en `DrawWorldObjects`: un `rand()` de la CRT por objeto visto y los dos sprites de `flare01` en los huesos 8 y 9 del último modelo posado (`LastPosedBonePoint`: el dueño del dragón, Q-C140); `World::StreetLightGlowLight`, la luz; el dueño (`0xb4`) en `m_objects`, antes del `.obj` | CL7.6 B4 |
| Los joints (`0x53c6a0`) | un sistema con su tabla, cuando una escena los use (el login no tiene ninguno) | — |
| El cuervo: una mascota del ítem `0x1c17` con su propio paso, al final del paso del dueño, y su dibujo al final del de él | `Scenes::DarkSpirit`: el constructor (`0x4167b0`) y el paso (`0x416a20`, estados 0 y 1), bit a bit contra el código real (`raven.txt`); `DrawDarkSpirit` (mallas 3 y 2 y la sombra de las cinco); la chispa nace en `World::Particles` (`0x5c6`, subtipo 5); `LoginScene` lo crea al entrar, lo avanza después del personaje y lo dibuja al final de su dibujo | CL7.6 C1 y C2 (las chispas) |
| La escena 5 en dos tiempos: el `F3 03` en el primer paso y el mundo con la respuesta, con el mosaico de la escena 3 en pantalla mientras tanto; su paso y su dibujo, con su propio orden del 3D | `GameScene` (hoy manda el `F3 03`, retiene el frame con el contador y no dibuja) retiene el último frame mientras espera, carga el mapa del paquete, arma al héroe con la cuenta de `0x511290` y dibuja en el orden de `0x6371a0`; los casos del mapa 0 como datos, como los del 55 (propuesta del barrido, P-C28) | CL7 |
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
