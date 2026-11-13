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
lineal de 2.000 a 2.700, que arma aparte `0x42b0a0` (MuRet la porta: [Los objetos de Castle
Siege](#los-objetos-de-castle-siege)). **El mouse no mueve la cámara**: ni el botón
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
  la 141 con `glTexCoord`); las otras cinco, *estáticas*. El `0x4000` lo usa la cáscara de cromo del 10 de
  `0x6002e0` ([El caballo oscuro](#el-caballo-oscuro); en MuRet, `ShinePass::Chrome5`).
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

**El texto en caja, el modo 2** (hito 4 de la CL8, 2026-11-12, *estático*, leído en el ASM): `0x554180(x, y, texto,
caja, sombra, color)` le pasa a `0x467060` el modo 1 con la sombra en 0 y el 2 con otra (`setnz` e `inc` en
`0x5541b9..0x5541bc`); `0x5542f0` pasa el que le dan. `0x467060` no lee el color (`[ebp + 0x20]`). Mide el texto con
`GetTextExtentPointA`: uno vacío sin caja sale sin dibujar (`0x467077..0x467082`), y uno vacío con caja mide el `"0"`
de `0x6c7ae4` (`0x4670ba..0x4670d0`). En el modo 2 (`0x46711b..0x467132`), la columna del texto es (caja − cx) / 2,
con la división de C (`cdq`, `sub` y `sar`, que trunca hacia el 0), y la caja queda en cx + 2 · esa columna (`lea`):
la pedida, una menos si caja − cx es impar y positiva, o una más si es impar y negativa. Un texto más ancho que la caja
va en una columna negativa y sale cortado de los dos lados (Q-C710). Las filas son las de la celda, hasta 32
(`0x467184`). `0x46719f..0x4671dc` limpia el DIB en todo el ancho de la caja, y `TextOutA` escribe el texto en
(columna, 0), salvo uno que empieza con `0x0a`. De ahí en adelante es el camino del modo 1: con más de 256 columnas,
el corte contra W en _ftol(W − x · rx) (`0x46721f`), y las cajas de 256 con `0x465a00` desde la columna 256 k y
`0x554000` en _ftol(x · rx + 256 k). Así el fondo (`0x6c8e64`) llena la caja entera, no solo lo que ocupa el texto.
Con una caja negativa el ancho puede dar negativo, y el `rep stos` de `0x4671cf` limpiaría fuera del DIB (Q-C711).
MuRet lo porta en `Render::Text::CenterInBox` (la columna y el ancho, con el `"0"` del texto vacío) y
`Scenes::ITextDrawer::DrawColumns` → `TextCache::DrawColumns` (la caja de esas columnas con el texto desde la columna
−columna, sus cajas de 256 y sus quads con `TextPieces` del ancho de la caja; con menos de 0 columnas, un error con
`0x4671cf`): el título del inventario (Q-C473), el rótulo de un botón de la UI nueva (`0x63c710`) y la línea sola de
un aviso (`0x642f00`). Los dos últimos pasan por `0x5542f0` con el fondo en 0 (`0x63c78a`, `0x642f29`), que no se ve:
de la caja cambia solo dónde va el texto (la caja empieza en la x del botón pasada a 640 × 480, y el texto, en píxeles
adentro) y que uno más ancho que la caja sale cortado. `DrawColumns` sirve también para el modo 1 con un ancho menor
que cx (la caja de ese ancho, con el texto desde la columna cx − ancho). Un barrido de las 279 llamadas a `0x554180`
(por los `push` de antes de cada una) encuentra la sombra en 1 (el modo 2) en 57 más que el título, casi todas en las
otras ventanas (el personaje `0x5ee2e0`, la party `0x5f0be0`, las tiendas `0x5f3030` y `0x5f4720`, el almacén
`0x5f4e30`, la caja del caos `0x5febf0`, el NPC de evento `0x5f58f0` y las del asedio), y en otras 14 la sombra va en
un registro: van por `DrawColumns` a medida que se porten.

### HUD y ventanas

Reescrita con el barrido de la CL8 (2026-11-12; plan:
[`2026-11-12-ola-cl8-hud-y-ventanas.md`](../../../docs/superpowers/plans/2026-11-12-ola-cl8-hud-y-ventanas.md)).
*Estático* salvo donde se dice; *verificado* es contra el frame 1 de la traza de GL de Lorencia. Lo que decía
antes esta sección tenía cinco errores: `0x5a89b0` no son las barras propias (es la vida de la party), no hay
minimapa (el modo 4 de las superposiciones es la ventana /move), `0x4383f0` no son los números de daño (es una
cola de textos 3D; el daño es `0x548eb0`), `0x5bbbc0` no dibuja barras de vida y ALT no muestra el HP de todos
(muestra los nombres de los ítems del piso), y la fábrica `0x48ee60` no arma el inventario ni el personaje (arma
amigos, correo y sus cajas; el id de esas ventanas va en `+0x40`, no en `+0x10`).

**Cuatro mecanismos de ventanas.** (1) Los **paneles por bits** de `[0x576352c]` (`IsOpen` `0x659290`, `CanOpen`
`0x659860`, `Open` `0x6599a0`, `Close` `0x65cae0`, `CloseByMask` `0x657ab0`, las teclas `0x657880`): el
inventario, el personaje, la party, /move, el quest, el gremio, el comercio, el almacén, las tiendas y el resto,
en **modo inmediato** (el paso los atiende en `0x5ee140` y el HUD los dibuja con funciones sueltas). (2) El
**administrador viejo** `*0x5763514` (ventanas retenidas de vtable de 23 entradas, un mapa id → ventana, una
lista en orden z en `+0x84` y una cola de mensajes; la fábrica `0x48ee60`): amigos, correo, sus cajas de mensaje,
la lista de ventanas minimizadas `[0x5763524]` y el registro del chat `[0x576350c]`. (3) La **UI nueva**
`0x7d53728` del login y la selección, que en el juego (modo 5, `0x65f5f0`) queda vacía e inerte. (4) Los
**diálogos del juego** `0x7d3a10c`/`0x7d3a110` (`0x6236f0` los abre o los encola), que `0x623e70` atiende por
paso y `0x62db60` dibuja por frame; el `0x6e` es el menú de Esc, el `0x93` las opciones y el `0x70` la
desconexión.

**El clic es primero de la UI.**

| Global | Qué | Quién escribe | Quién lee |
| --- | --- | --- | --- |
| `0x775857c` (byte) | **el mouse es de la UI** | 0 en `0x636e04` en cada paso; 1 en `0x636e1d` (y ≥ 432), en `0x637063` (aviso abierto) y en los handlers de ventana cuando el mouse cae en su rectángulo (`0x5e3ce8` party, `0x5e457d` C, `0x40a48f` D, `0x424230`, `0x41a9e0`, `0x41b170`, `0x44b540`, `0x44b910`, `0x44dd50`, `0x44f810`, `0x5e6520`, `0x5e74f0`, `0x5ebca0`, las hijas del inventario y la UI vieja: 81 escrituras en 44 funciones) | `0x59adf0` en `0x59b37a` (el giro) y `0x59b853` (el clic); `0x59ec80` en `0x59ef16` (el picking, por frame, con el valor del último paso); `0x57cfc0`, `0x5ac8d0` (teclas F), `0x5ac2d0` (el cursor, por frame) y `0x492ac0` (UI vieja) |
| `0x7d366d4` (byte) | flanco del clic izquierdo | 1 en el `WM_LBUTTONDOWN` con `0x7d36874` en 0 (`0x4b395a`); 0 en el `WM_LBUTTONUP` (`0x4b3ce3`) y en cada consumidor (más de 100) | la UI en el paso; después `MoveHero` (`0x59b88b`), que lo consume aunque no camine |
| `0x7d36874` | botón izquierdo abajo | WndProc; algunos handlers lo bajan | `MoveHero` (apretado), `0x636e5d` |
| `0x7d3682c`, `0x7d36828` | x, y del mouse en 640 × 480 | WndProc | todos |
| `0x7d366d8` | rueda en muescas | WndProc; la consume quien la usa | volumen (`0x625782`) |
| `0x5763588` | Enter (flanco propio) | | OK genérico de los avisos |
| `0x77f3b10`, `0x6c8dcc` | `MouseUpdateTime` y su máximo | la UI pone 0 y 6 al usar un clic (`0x6240c5`, los handlers) | `MoveHero` (`0x59b860`) |

**Regla** (*estático*): un clic sobre una ventana **no camina** por dos caminos que se cubren. El
handler pone `0x775857c` y casi siempre consume `0x7d366d4`, y `MoveHero` salta todo el tramo del
clic con la bandera (`0x59b853 → 0x59e24e`, sin subir `MouseUpdateTime`, Q-C168). Hay excepciones.
`0x5a7cc0` no consume el clic ni pone la bandera (Q-C454). `0x435bd0` consume el clic pero no
pone la bandera.

**El orden.**

Por paso (`0x637700`, 25/s, escena 5):

1. `0x623e70` si `0x5762eb4 == 0`: `IsOpen(1)` (sin `rand()`); si no está abierta la de amigos, **lee el flanco
   de Esc** (`0x553520(0x1b)`, `0x623eb5`): recorrido de cámara → lo corta; `0x78abb39` → nada; sin diálogo →
   `0x657880(0x1b)` (diálogo de los paneles, después el panel de bit más bajo; si no cerró ninguno, abre el
   diálogo `0x6e`, o lo encola; con ítem en el cursor, `0x5be150`; suena `0x19`); con el `0x6e` o el `0x93`
   abiertos, los saca.
2. `0x636290` → bloque `0x636e11..0x63705b`, solo con `0x7d36899 == 0`:
   1. mouse y ≥ `0x1b0` → `0x775857c = 1` (el mouse está sobre la UI);
   2. `0x5ecdc0` (mapa); `0x5762eb8 = 0`; `0x46b7b0` (colas de textos, CL8.2);
   3. lista de ventanas (`vtbl[6](0)`), **primera en reclamar el mouse**;
   4. `0x490490`: valida la modal; **Esc** (`0x4904bc`, la misma tabla de flancos que en el punto 1, así que solo
      lo ve si `0x623e70` no lo leyó: es decir, con la de amigos abierta) cierra la de arriba si su `vtbl[19]`
      lo acepta; F5 (con foco en la ventana del juego) y F6; cierra la de amigos si quedó oculta; Update de
      **arriba hacia abajo** (estados 3 y 5 se saltean), cada una reclama el mouse si nadie lo tiene; vacía la cola
      del administrador; `+0x40 = 0`;
   5. registro del chat (`vtbl[6](0)`), último en reclamar; pone `0x6c7a88 = 0` en su constructor;
   6. clic izquierdo sin dueño o del chat: foco viejo a 0 y, sin campos de texto activos (`0x6c8e68`, `0x77f3b69`,
      `0x77f3b68`, `0x77f3b6a`), `0x5762ec0 = 0`, la lista de ventanas se pliega (`0x4a5e10`) y vuelve el foco y
      el IME a la ventana del juego (`0x467360`);
   7. `0x5ebca0`, `0x435bd0`, `0x5ee140`, `0x59ff10` (teclas y botones del HUD, y su cola `IsOpen(0)`),
      `0x5a7cc0`;
   8. si el diálogo no es `0x6e`: `0x657930` (diálogo de los paneles, gremio 2, gremio, CS);
   9. lugar del registro del chat: con `0x6c7a88 == 1`, (0, 420) si hay paneles anchos abiertos, si no (186, 420);
      con otro valor, `0x45a9d0(−10, 0x65)`;
   10. con un diálogo → `0x775857c = 1`.

Por frame (`0x6371a0`, el 2D en `0x637646..0x6376e6`): `0x5a7320` (el HUD, que llama a `0x5f7440` → `0x6579e0`,
los paneles secundarios con su diálogo arriba), `0x5a7eb0`, **`0x490390`** (ventanas viejas, de abajo hacia arriba,
y al final `gluOrtho2D(0, W, 0, H)`), `0x5f37f0`, **`0x62db60`** (diálogos: el menú de Esc va arriba de todas las
ventanas viejas), `0x5f7bc0` (tooltip), …, `0x65fe00` (vacío) y el cursor.

**Lo que porta el hito 1 de la CL8** (`Scenes/GameUi.cpp`, `Scenes/GameSceneUi.cpp`, `UI/PanelRegistry.cpp`).
`GameScene::Step` guarda los botones del WndProc (`0x7d366d4`, `0x7d36874`, `0x7d36880`) y corre `0x623e70` antes
de `0x636290`, con el mundo cargado o no; `StepWorld` corre el bloque `0x636ded..0x63705b` donde estaba la
frontera de P-C41, después de las olas del agua y antes de los objetos. Del bloque, MuRet tiene: la bandera, la
cola de Esc y F5/F6 de `0x490490` (las dos teclas cortaban hasta el hito 7), las puertas de `0x5ee140` (cada hija corta
con su dirección; con el personaje cerrado, `0x424230` pone `0x723911` en 0), y de `0x59ff10` la salida del
recorrido de cámara, PgUp/PgDn (cortan, hito 3), los latches de arriba y abajo, los seis botones del HUD
(`0x5a03c3..0x5a05e5`: gremio, party, personaje, inventario, amigos y comandos, cada uno con su rectángulo y
abierto → `Close`, si no → `Open`), la barra de skills con su botón (`0x5a066c..0x5a06f9`; la lista corta, CL9.2),
los números y Alt con un número (cortan, CL9.2 y hito 3), el clic derecho en las tres ranuras rápidas
(`0x5a2b47..0x5a32dd`: x `0xd0 + 0x1f·i`, 20 × 20 en y `0x1c4`; corta, hito 2 y CL9.3), la cadena de Q/W/E
(Q-C459; la tecla corta), las letras de los paneles, B y R (la reparación es de la CL8.4), la cola `IsOpen(0)` y,
sin ningún panel abierto, el panel del asedio de la clase 0 (`0x40f6d0`: Tab con el latch lo muestra u oculta, el
mouse en su rectángulo de `0x97 × 0xad` es de la UI, la flecha de la página y el alfa, Q-C460). El picking del
frame (`0x637641`) corre después del pase del mundo con las copias del frame proyectado, y el ancho del mundo
(`0x5bb8d0`) se pide en `0x63733e`: con un panel abierto, el frame ~~corta~~ achica el mundo desde el hito 4
([El inventario](#el-inventario)). La
pasada 2D (`0x637646..0x6376e6`) queda como ganchos en su orden, sin dibujar ni sacar `rand()`, hasta el hito 2.

**Las ventanas viejas** (hito 7 de la CL8, `UI/OldWindows.cpp` y `UI/OldWindowDraw.cpp`; *estático*). El
administrador `*0x5763514` (`0x48e470`, `0xf4` B) guarda las ventanas en un mapa id → ventana (`+0x4c`), su orden de
abajo hacia arriba en una lista (`+0x80`: la última es la de arriba), una cola de mensajes de 12 bytes
(`+0x08..+0x3c`), el foco (`+0x44`), la de amigos (`+0x48`), F6 (`+0xa8` y la lista `+0xac`) y las cajas que no son
modales (`+0xb8`); la modal va en `0x5762ebc`. Cada ventana es una `0x45a210` (el id del contador `0x5762eb0`, que
suma antes de dar; el padre, el estado, las banderas, el rectángulo y su cola) con una vtable de 23 entradas: el
marco (`0x491ca0`, `0x6b9f9c`), la caja de mensaje (`0x6b9cb4`) y la lista de ventanas (`0x6ba074`). MuRet las guarda
como datos (`OldWindowKind`) y despacha con una función por método.

- **La fábrica** `0x48ee60(caso, x, y, dato, aux, banderas)`. El 0 es un marco solo (`0x4920a0`: el título, el
  padre, `0xd5 × 0xaa`); el 5 y el 8 son cajas modales y el 9 y el 10 no; el 5 y el 9 tienen dos botones (Text 1037 y
  1038, comandos 1 y 2) y el 8 y el 10 uno (Text 228, comando 1). La caja (`0x4a38f0`) lleva el título 991 o 228, el
  texto cortado por `0x459c70` en 2 renglones de `0x7d`, `0x96 × 100` sin achicarse, las banderas `0xb` y los botones
  de `0x32 × 0x14` en (`0x12`, `0x28`) y (`0x49`, `0x28`) del área de adentro (x + 5, y + `0x15`), o el único en
  (`0x2d`, `0x28`); el segundo de una caja de un botón queda sin padre en (0, 0) (Q-C570). x en −1 es 0 e y en −1,
  `0x14c`; sin el bit 0 de las banderas corre la cascada (de a 20; sin lugar, (r + 1, r + 1) con r = y % 10, y con
  r = 9 no la arma; Q-C571). Va arriba de la lista y corre su vtable + `0x30` (`0x4a3a50`, que ubica los botones). Cada
  ventana y cada botón toman el id que sigue de `0x5762eb0` (sumado antes de usarlo); el valor de partida, el que
  sigue al de la lista de ventanas, es *inferido* (P-C97). Los casos 1 a 4, 6 y 7 (amigos, su chat y las cartas) son
  de la CL9.4.
- **El paso.** La lista de ventanas (`0x636e43`) y `0x490490`: la modal que ya no está; Esc (`0x4904bc`, Q-C446) le
  pide al administrador que cierre la del final si está a la vista; F5, con el foco del teclado en la ventana del
  juego, va a `0x4a5be0(1)` sobre la lista de ventanas, que sin entradas no hace nada; F6 alterna `+0xa8` y llama a
  `0x4906a0(+0xa8, 1)` si `+0xa8` está en 0 o la lista de F6 tiene alguna (Q-C573); la de amigos oculta se suelta; el
  paso de cada una a la vista, **de arriba hacia abajo**; la cola del administrador (`0x490b70`: 1 sube y da el foco,
  2 minimiza, 3 estira, 4 cierra, 5 manda al fondo), y `+0x40` en 0. El paso de una ventana (`0x45aa80`) atiende su
  cola (la caja contesta el 10 de un botón con `0x4a3b80`: al dueño el 12 con su id y 1 o 0, y al administrador el
  4), corre la vtable + `0x1c` (la caja: Enter si es la del final, y el paso de **los dos** botones), toma el mouse
  (`0x5762eb8`, si no lo tiene otra y nadie lo agarró; con el izquierdo abajo sube la de más arriba de su familia) y
  corre la vtable + `0x20`: el botón (`0x45b200`: el apretón lo marca en `0x5762ec4`, el soltado sobre él le manda el
  10 al padre y suena `0x19`, el mouse fuera con el botón apretado suelta todo) o el marco (`0x492ac0`: los botones
  de la barra con el soltado, las esquinas del tamaño y la barra del arrastre, que agarran el mouse en `0x5762eb4`).
  El clic de un botón de la caja tarda dos pasos: el soltado le manda el 10 y la caja lo atiende en el paso
  siguiente, donde el administrador la suelta y suena otra vez. El soltado es `0x7d366ec`: lo prende `WM_LBUTTONUP`
  con el mouse de ese momento (`0x4b3cea`, en `0x576358c` y `0x5763590`) y lo apagan `WM_LBUTTONDOWN` (`0x4b3947`),
  cualquier mensaje con el mouse en otro lugar (`0x4b344d`) y quien lo usa (P-C96).
- **El dibujo.** `0x490390` (`0x637673`) corre de abajo hacia arriba la vtable + `0x14` (`0x492500`) de las que no
  están ocultas ni minimizadas y pone `+0x40` en 1; `0x637680` corre `0x492500` sobre la lista de ventanas siempre,
  oculta y todo, sin quads (P-C98). `0x492500`: la mezcla normal; la sombra (el 3 de la paleta `0x491960`, con alfa ·
  179, en x, y + 5, w × h − 10 con el borde); `0x459ec0` (`glColor4f(1, 1, 1, 1)` y `glEnable(GL_TEXTURE_2D)`); la
  vtable + `0x50` (la caja, `0x4a3a90`: el texto en blanco en (x + 10, y + `0x1d`) y los botones, `0x45b2e0`, que
  dejan la mezcla opaca para la barra del título y los filos, sin diferencia a la vista; la lista, `0x4a5490`: mide
  el renglón con "0" la primera vez y corre el parpadeo);
  el gris 0,5 salvo la del final con la ventana de amigos abierta (Q-C574); la barra del título (`0x645`), sus filos
  (8 a 13; 10 en lugar de 8 y 9 en gris), el borde (`0x492180`), la raya, el título en negrita (`0xffc8dce6`, o
  `0xff646e73` en gris), los botones de la barra (`0x647`) y la esquina del tamaño (`0x648`).
- **Chaos Castle** (`0x4c7b17..0x4c7b2f`): `0x4906a0(1, 0)` esconde las ventanas sin anotarlas (Q-C572), y `0x4914e0`
  suelta la de amigos; `0x4c7b34` sigue con el objeto de los comandos (CL9.4, P-C99).

En la CL8 nadie arma una ventana vieja (los que llaman a la fábrica son de la CL9.4): el administrador queda vacío y
la lista de ventanas, oculta; del paso quedan Esc (sin nada que cerrar), F5 (sin entradas) y F6 (Q-C573), y del
dibujo, el de la lista oculta. Los tests abren una caja a mano. Lo que no se porta corta con su dirección (P-C95).

**Los paneles.**

El objeto (4 B, vtable `0x6baacc`) lo arma `0x657450` desde WinMain (`0x4bb96a`) con seis objetos: `0x7d536f0`
gremio `0x10` (`0x64f3e0`), `0x7d536f4` gremio 2 `0x40000` (`0x654900`), `0x7d536f8` CS `0x80000` (`0x64aca0`),
`0x7d536fc` lista de CS `0x100000` (`0x661090`), `0x7d53700` CS `0x200000` (`0x6485f0`) y `0x7d53704` el diálogo de
los paneles (`0x65feb0`, 0x1064 B, con Sí/No y Confirmar/Cancelar y un campo de texto).

| id | Qué es | De dónde sale `IsOpen` | Ola |
|---|---|---|---|
| `1` | amigos (UI vieja) | `0x490a00(viejo+0x48) != 0` | CL9.4 (el toggle, CL8.3) |
| `2` | mapa (M) | `0x77f3b08 == 4` | CL8.1 |
| `4` | party (P) | `0x7d5370a` | CL8 hito 6, vacía (los miembros, CL9.4) |
| `8` | quest (T) | `*(0x710148)+0x21383` | CL9.x |
| `0x10` | gremio (G) | `0x6522e0()` | CL9.4 |
| `0x20` | trade | **byte protegido `0x7d53711`: 1 `rand()`** | CL9.4 |
| `0x40` | almacén | `0x7d5370d` | CL9.3 |
| `0x80` | — | siempre cerrado: su rama de `0x659290` da 0 (`0x659429 JNZ 0x6597f3` → `XOR AL,AL`) | — |
| `0x100` | caja del caos | `0x7d5370e` | CL9.3 |
| `0x200` | comandos (D) | `*(0x6e592c+4)` | CL8 hito 6 (sin `0x406050`, CL9.4) |
| `0x400` | mascota | `0x420cc0()`: `0x723911 == 2` (`0x424230` lo vuelve a 0 en cada paso con el personaje cerrado) | CL9.x |
| `0x800` | tienda personal propia (S) | `0x7d5370f` | CL9.4 |
| `0x1000`, `0x4000` | ventanas con inventario de NPC de evento (`0x7d53712` con `0x7d53714` en 0 o 1; el paso las atiende en `0x5e5920`, que manda `90`/`9A`) *inferido* | | CL9.5 |
| `0x2000` | sin identificar | `0x7d53718` | — |
| `0x8000` | NPC con la caja `0x723c80` | `0x449bf0()` | CL9.3 |
| `0x10000` | tienda de NPC (la abre el `30`, `0x4d1e00` en `0x4d288e`) | **byte protegido `0x7d53710`: 1 `rand()`** | CL9.3 |
| `0x20000` | tienda personal ajena | `0x7d5370f` | CL9.4 |
| `0x40000` | gremio 2 (paquetes `54`, `56`) | `0x6570a0()` | CL9.4 |
| `0x80000`, `0x100000`, `0x200000` | Castle Siege | `+4`, `+4`, `+0x14` de su objeto | CL9.5 |
| `0x400000`, `0x800000`, `0x1000000` | ventanas de NPC (`0x44dcf0`, `0x44f770`) | `0x44dc10() == 1/2`, `0x44f0f0()` | CL9.x |
| `0x2000000` | personaje (C) | `0x7d53709` | CL8.4 |
| `0x4000000` | inventario (I, V) | `0x7d53708` | CL8.4 |

- `IsOpen(0)` recorre 1, 2, 4… y para en el primero abierto. Los ayudantes `0x6522e0`, `0x6570a0`, `0x44dc10`,
  `0x44f0f0`, `0x420cc0` y `0x449bf0` no llaman a `rand()` (4 a 11 bytes).
- `CanOpen` `0x659860`: pide cerrados `0x10000` (rand), `0x20` (rand), `0x40`, `0x100`, `0x40000`, `0x2000`,
  `0x800`, `0x20000`; el diálogo en 0 o `0x8a`; `0x78abb00 == 0`; el quest ≠ 1; la lista de CS libre; `0x400`
  necesita el personaje; 1 y `0x200` no se abren en los mapas 18 a 23 (Chaos Castle); `0x800` necesita el
  inventario.
- `Open` `0x6599a0(id, aux)`: sale con 0 si ya está abierto, si `CanOpen` falla o si `0x576793c == 1` (salida
  pedida); si el diálogo no es `0x8a`, `0x6236f0(0)`; la máscara `~id ^ 1` (todo menos el id y los amigos) va a
  `CloseByMask` (salvo con 1 y 2, que no cierran nada); el despacho es la tabla de bytes `0x65cabc` / saltos `0x65caa0` para ids ≤ `0x20` y comparaciones
  arriba; la salida común `0x65ca72` hace sonar `0x19` y `0x1c` y devuelve 1, aunque algo haya fallado.
- `Close` `0x65cae0`: con `0x10000`, `0x80000`, `0x8000`, `0x80`, `0x20`, `0x40`, `0x100` y `0x1000` cierra
  también el inventario; `0x2000000` cierra antes `0x408`; `0x4000000` cierra antes `0x11d9e0` (y no cierra si
  la lista de CS está ocupada); `0x100000` ocupado da el texto 1641. Siempre suena `0x19` y `0x1c`.
- `CloseByMask` `0x657ab0(máscara, flag)`: el orden fijo es `0x2000000`, 4, 8, `0x10`, `0x40000`, `0x80000`,
  `0x100000`, `0x200000`, `0x400000`, `0x800000`, `0x1000000`, `0x20`, 1, 2, `0x40`, `0x100`, `0x200`, `0x400`,
  `0x800`, `0x20000`, `0x1000`, `0x2000`, `0x4000`, `0x8000`, `0x10000`, `0x4000000`, y cada uno se consulta con
  `IsOpen` **solo si está en la máscara**.
- Teclas de los paneles: `0x657880(vk)` le da la tecla primero al diálogo (`0x660410`); con Esc cierra el abierto
  de bit más bajo. La llaman `0x623e70` (Esc, en `0x623f19`), el WndProc `0x4af4e0` (Enter, `0x4b5185`, según el
  diálogo: `0x7d`, `0x95`, `0x8a`, `0x8d`, `0x8b` y `0x78abb00 != 3`) y `0x403390` (después de `0x5be150`).
- Teclas del juego (`0x59ff10`, `0x5a4c25..0x5a5504`): guarda
  `!0x6578e0() && 0x7d3a10c == 0 && !*(0x7d536fc+4)`; G → `0x10`, P → 4, T → 8, C → `0x2000000`, I/V →
  `0x4000000`, F → 1, M → 2, D → `0x200`, por la cola común `0x5a4ea6` (abierto → `Close`, si no → `Open`).

**Los `rand()` de abrir y cerrar.**

### 3.6 Los `rand()` de abrir y cerrar (*estático*)

| Qué | Dónde | `rand()` | Paso/frame |
|---|---|---|---|
| `IsOpen(0x20)` / `IsOpen(0x10000)` | `0x659290` (cierres `0x4c0ec0` / `0x4c1190`) | 1 cada uno | donde se llame |
| `IsOpen` de cualquier otro id | | 0 | |
| la barra de skills de `0x59ff10`, aunque no haya skills | registro propio abierto en `0x5a05e5`, cerrado en `0x5a10e2` | 3.848 | **por paso** (Q-C458) |
| la cadena de Q, W y E sin campo de texto | `0x5a343e..0x5a3587`: `IsOpen(0x20)`, `IsOpen(0x40)`, `IsOpen(0x10000)` y lo que sigue, con cortocircuito | 2 con todo cerrado (sale en la consulta del diálogo si hay uno, después de las dos) | **por paso** (Q-C459) |
| `IsOpen(0)` con nada abierto debajo de `0x20` | `0x5a5511` (cola de `0x59ff10`) | 2 | **por paso**: `0x59ff10` corre en cada paso (`0x636ee6`) y solo sale antes en `0x59ff3f` |
| **el paso del juego quieto, con todo cerrado** | `0x59ff10`: la barra, Q/W/E y la cola | **3.852** | por paso (hito 1 de la CL8, `GameUi.cpp`) |
| R apretada con todo cerrado | `0x5a4f34..0x5a54e6`: el registro propio (cerrado en `0x5a53d8`), `IsOpen(0x10000)`, `IsOpen(0x20)` e `IsOpen(0x10000)` otra vez antes del inventario | 3.851 | por paso, con la tecla |
| B apretada con todo cerrado | `0x5a4ec2..0x5a4f2e`: `IsOpen(0x10000)` | 1 | por paso, con la tecla |
| el picking del frame (`0x59ec80`) con el mouse fuera de la UI y sin Alt | `0x59efda..0x59f46e`: el registro propio con la clase 0 o 2; `0x5e4140` → el ancho del mundo con la party cerrada | 3.848 (DW y Elf) + los del ancho | por frame (`0x637641`) |
| el ancho del mundo `0x5bb8d0` con el inventario abierto | `IsOpen(0x10000)` e `IsOpen(0x20)` | 2 por llamada | 19 llamadores, por paso y por frame |
| el resto del bloque de UI del paso (sin `0x59ff10`), con todo cerrado y `0x6c7a88 != 1` | `0x636e11..0x63705b` | 0 | por paso |
| lo mismo con `0x6c7a88 == 1` y el inventario abierto | `0x636f04..0x636ffd` | 2 | por paso |
| `0x657a90` con todo cerrado | `0x657ab0(−1, 0)` | 2 | evento (MuRet ya lo cuenta en `WorldEntry.cpp`) |
| abrir el inventario (I) con todo cerrado | `CanOpen` 2 + máscara 2 | 4 | evento |
| cerrar el inventario (I) | máscara `0x11d9e0` | 2 | evento |
| Esc con solo el inventario abierto | `0x657880` 2 + máscara 2 | 4 | evento |
| Esc con nada abierto | `0x657880` 2, y abre el `0x6e` | 2 | evento |
| abrir el personaje (C) | 4 + registro propio 3.848 + `0x550d10` (CL8.4) | ≥ 3.852 | evento |
| abrir los amigos (F) | `CanOpen` 2 + registro propio en `0x491080` 3.848 | 3.850 | evento |
| R (reparar, nivel ≥ 50, inventario abierto) | registro propio + `IsOpen(0x10000)`, `IsOpen(0x20)`, `IsOpen(0x10000)` | 3.851 | evento (CL8.4) |
| abrir la tienda personal (`0x800`) o la ajena (`0x20000`) | C3 `F3 10`: relleno (`0x65ae52`/`0x65b521`) + serial (`0x40b3f0`) | +2 | evento (CL9.4) |
| abrir la tienda de NPC (`0x10000`) | cierre de su byte (`0x65be36`) | +1 | evento (CL9.3) |
| cerrar el trade abierto | C3 `3D` (`0x6580b7` + serial) + su byte | +3 | evento (CL9.4) |
| F6 para mostrar, si una de las ocultas ya no existe (la de amigos: `0x490490` la destruye al verla oculta) | `0x4906a0` → `0x491080` | 3.850 | evento |
| dibujo del marco viejo | `IsOpen(1)` | 0 | frame |
| dibujo de CS `0x200000` | `0x64a060` (`0x64a9f0`) | 1+ | **frame**, solo con ese panel (CL9.5) |
| dibujo del retrato de la carta | `0x497c10` (`0x497cce`) | 1+ | frame, solo con esa ventana (CL9.4) |
| botón Seleccionar servidor del Esc del login | `0x647f60` | 2 | evento (escenas 2 y 4) |

El registro propio gasta 3.848 al cerrarse **si su cuenta de aperturas (`+0xf08`) vuelve a 0**; dentro de otra
apertura no gasta. Lo mismo vale para las entradas de 1 byte.

**La pasada 2D del juego** (`0x637641..0x6376eb`). Antes del 2D, `0x6375ea..0x63763c` apaga la niebla del
mapa 30 y `0x637641` corre el picking (`0x59ec80`).


| Dir. | Llamada | Condición | Deja |
|---|---|---|---|
| `0x637646` | `0x6219b0` empieza el 2D | siempre | PROJECTION: perspectiva y encima `gluOrtho2D(0, W, 0, H)` con push; MODELVIEW identidad con push; sin profundidad (`0x620870`). Traza 124236..124245 |
| `0x63764b` | `0x4383f0` textos 3D | siempre | `glColor3f(1, 1, 1)` (traza 124242+); con textos, mezcla normal |
| `0x637650` | `0x5bbbc0` hover y ALT | siempre | **opaca** (`0x620980`) y blanco; `0x77f3b66 = 1` |
| `0x637660` | `0x5a7320(1)` el HUD | `0x7d36899 == 0` (sin vista de arriba); el argumento no se usa | termina con `0x5a9630`, que hace `0x621a90`: **sale del 2D por la pila** (traza 126848..126849) |
| `0x637668` | `0x5a7eb0` cuenta y resultados de evento | siempre | mezcla y color de lo que dibuje |
| `0x637673` | `0x490390(this = [0x5763514])` ventanas con clase | siempre | dibuja con vfunc +0x14 las de estado ≠ 3 y ≠ 5; después **PROJECTION con identidad y `gluOrtho2D` sin push** ni vuelta a MODELVIEW (traza 126850..126852) |
| `0x637680` | `[[0x5763524]]+0x14` = `0x492500` (vtable `0x6ba074`) | siempre | la lista de ventanas, aunque esté oculta: sin quads, pero con la mezcla, el color y la textura de `0x492500` y `0x4a5490` (*estático*; esta tabla decía "en la traza, nada", P-C98) |
| `0x637683` | `0x5f37f0` pedido de duelo | adentro, `0x78abad9` | |
| `0x637696` | `0x62db60` cajas viejas | escena `0x6d5dc8 == 5` | con código 0: mezcla normal |
| `0x63769b` | `0x5f7bc0` tooltips | siempre | |
| `0x6376a0` | `0x554810` avisos | siempre | mezcla normal y blanco |
| `0x6376b6` | `0x5554e0` área de mensajes | `0x6c7a88 == 1` o escena ≠ 5 | |
| `0x6376bb` | `0x5b0550` superposiciones (abajo) | siempre | |
| `0x6376c6` | `0x40eca0(this = [0x6eed3c])` cartel del set | `+0x9305 ≠ 0` | |
| `0x6376cb` | `0x65ee60` → `0x65fe00` UI nueva | siempre | (portado para login/selección) |
| `0x6376d7` | `0x5ac2d0` cursor | siempre | mezcla normal, blanco. Traza 126884 |
| `0x6376dc` | `0x633110` ítem 3D de diálogo | `0x7d3a10c` ∈ {0x94/0x98 con `0x78abb14 == 1`, 0x96, 0xa8, 0x99, 0x94 con `0x78abb14 == 5`} | proyección propia (abajo) |
| `0x6376e1` | `0x5ac2d0` cursor **otra vez** | siempre | Traza 126895, mismo quad |
| `0x6376e6` | `0x621a90`, `0x620fc0` | siempre | traza 126896..126901: 3 pops en MODELVIEW, PROJECTION, 1 pop, MODELVIEW; devuelve 1 |

Las pilas no cierran por modo (`0x621a90` hace dos pops en el modo que esté; `0x490390` deja PROJECTION sin
push): puede haber underflow (*inferido*). A MuRet no le importa: `SpriteBatch` arma sus matrices por pase. Lo que
sí importa es la mezcla cacheada (`0x7d366dc`: solo cambia GL si cambia el modo) y el color que hereda el que
sigue, porque varias funciones dibujan sin poner color.

**Los textos 3D** (`0x4383f0`, en `0x63764b`; CL8 hito 6, *estático*). La cola es un `std::deque` de la `.bss` en
`0x723c18` con entradas de `0x4c` bytes: el texto en 64 (`0x4381f0` copia los 64 que le pasan, con su `'\0'`) y la
posición en tres `float`. La llena solo `0x4381f0`, desde el cuerpo de los personajes (`0x4fd603`: el tipo `0xc5`
con su nombre y `+0x1d6`, CL9.5, que MuRet corta). El dibujo hace `glColor3f(1, 1, 1)` y, mientras la cola tenga
entradas, toma la de adelante: su posición con la z 370 más arriba (`0x6b8b88`, `fadd` y `fstp` a `float`) por
`0x6205b0`; con la x y la y de la pantalla en 0 o más (con signo, `0x438460..0x438469`), la negrita en el DC
(`0x43846b`; el `GetTextExtentPointA` de al lado no se usa), el fondo `0xff000064` (`0x6c8e64`), el texto
`0xffc8e6ff` (`0x6c8e5c`) y `0x623690`, el texto centrado en la x. Después la saca, se haya dibujado o no
(`0x69eff0` suelta cada bloque al pasarlo). No saca `rand()`. MuRet: `Scenes::DrawTheWorldTexts` sobre
`WorldRuntime::worldTexts`, que en el juego de hoy llega siempre vacía; sin la proyección del pase del mundo, una
entrada corta con `0x438455`.

**El hover y Alt** (`0x5bbbc0`, en `0x637650`; CL8 hito 6, *estático*). En Chaos Castle (mapas 18 a 23) sin
"webzen" (`0x6c9ccc`) en el nombre del héroe (`0x576960`) y con un NPC (`0x6c8e2c`) o un personaje (`0x6c8e30`)
elegido, vuelve sin hacer nada (Q-C612). Si no, `0x77f3b66` en 1 (Q-C610), `glColor3f(1, 1, 1)` y `0x620980`, y el
primero que haya de:

| Elegido | Qué hace | MuRet |
|---|---|---|
| NPC | `0x557170(+0x1d9, 0x78abb3c, personaje, 0, −1)`: su nombre solo en un globo (el texto vacío de la `.bss`, el modo 0 y sin color) | `AddChatBubble` |
| personaje, con `0x6c8e38` en −1 | un monstruo (`+0x88` en 2): `0x6c8e64 = 0xff000064`, `0x6c8e5c = 0xffc8e6ff` y `0x623690(0x5bb8d0() / 2, 10, nombre)` (la mitad con `CDQ` y `SAR`, hacia el 0; el `GetTextExtentPointA` de antes no se usa). Los demás, sin tienda personal (`0x451d10` da el objeto `0x7245c8` y `0x451770` busca el personaje en su mapa, que llenan los paquetes de la CL9.4), en un globo como el NPC con `0x78abb40` | portado; la tienda da siempre 0 |
| ítem del piso (`0x6c8e28`) | `0x5ba790(i, +0x44, +0, +0x17, +0x18, 0)` de la entrada de `0x220` bytes en `0x77f46d0`: su nombre | corta con `0x5bbd62` (CL9.3) |

Después Alt: el flanco (`0x553520(0x12)`, `0x5bbd6c`) invierte `0x78abb38`, que nadie más lee ni escribe; con él
prendido o con Alt abajo (`GetAsyncKeyState(0x12)` con el byte alto en `0x80`), cada ítem del piso vivo (`+0x44`) y
a la vista (`+0x1b0`) que no es el elegido va a `0x5ba790` con 1 (Q-C611; MuRet corta con `0x5bbdd2`, CL9.3). No
saca `rand()` (`0x5bb8d0` sin el inventario abierto no consulta el comercio ni la tienda de NPC). MuRet:
`Scenes::DrawTheHover`, con el latch de Alt en `GameUiState::keyLatches[0x12]` y `0x78abb38` en
`GameUiState::altNames`; un dibujo intermedio no escribe los globos y gira el latch sobre copias.

**Las ayudas de F1** (`0x5b0550`, en `0x6376bb`; CL8 hito 6, *estático*). F1 (`0x5ac9ce..0x5aca42`, con el latch
de `0x70` en línea) pasa `0x77f3b08` de 3 o 4 a 0 y si no a `(m + 1) % 3`, pone `+0x9305` de `[0x6eed3c]` en 0 y
pide el `0x19`: también cierra la ayuda del ítem y la ventana /move. `0x5b0550` mira el modo tres veces: con 1, la
primera ayuda; con 2 (otra lectura), la segunda, y vuelve; con 3, la del ítem; con otro, `IsOpen(2)` y
`0x449860`. Las dos de F1 empiezan con `0x620980`, ponen en 0 el renglón que sigue (`0x78abb30`, sin tocar
`0x78abb34`) y llenan los renglones de los tooltips (§ Los tooltips: el texto en `0x78793c8`, el color en
`0x787a560` y la negrita en `0x78a9230`) con `"\n"`, el título
(el texto 120 o el 140, color 1 y negrita), `"\n"`, los textos 121 a 139 o 141 a 156 (color 0, sin negrita) y
`"\n"` (los `'\n'` no tocan su color ni su negrita: quedan los de la ayuda anterior), copiados con `strcpy`
(Q-C613); `0x77f3b66` en 1 y `0x5af090(1, 1, cuántos, 0, 2, 1)`. `0x5afb00` es solo del modo 3.

La caja es la de los tooltips (`0x5af090`, § Los tooltips, "La caja"). MuRet: `Scenes::DrawTheFunctionHelp` llena
`HudState::tooltip` y dibuja con `Scenes::DrawTheTooltipBox`, como los cuerpos del hito 5.

**La ayuda del ítem** (el modo 3, `0x5b0802`). La abre el chat con `"/"` y el nombre de un ítem
(`0x576923`: `+0x9305` en 0 con `0x5ae360`, el modo 3, el ítem en `0x77f3b0c`, el `0x19` y 1, que no manda nada).
`0x620980`; el divisor del ancho (`0x5a` en 640 y 800, `0x67` en 1.024, `0x7b` en 1.280, 0 con otro); las flechas
(`0x807`), los virotes (`0x80f`) y los ítems fuera de las franjas `0..0x9ff`, `0xa00..0xbff`, `0xc00..0xdff`,
`0xe00..0x17ff` y `0x1e00..0x1fff` ponen el modo en 0 y vuelven. Las franjas 1 a 4 dividen `0x870` o `0x708` por
el divisor (Q-C615); la 5, `0x1734` (640 y 1.280) o `0x1450` (800 y 1.024), o nada con otro ancho. El cociente es
la x de la primera columna (0 sin división); la y de los títulos y la de las filas salen del ancho (`0x26` y `0x34`
en 640, `0x21` y `0x2f` en 800, `0x1c` y `0x28` en 1.024, `0x16` y `0x20` en 1.280, 0 con otro).

La tabla por nivel (`0x5afc00(ítem)`, en `0x5b09a8`): con el ítem de `0x6c9334` (−1 en la `.data`; solo la escribe
ella) vuelve sin hacer nada (Q-C750). Si no, lo guarda y llena 14 niveles de 17 dwords en `0x787a15c` con el registro
de Item.bmd de `0x77f3b0c`: el nivel (columna 0), el daño mínimo y el máximo (`+0x26` y `+0x27`, 2 y 3), el poder
(`+0x2f`: en la 4, y de `0x408` a `0x40d`, los cetros, en la 5), la defensa (`+0x29`, 6) y su tasa (`+0x28`, 7), los
requisitos de fuerza (`+0x30`, 8), agilidad (`+0x32`, 9), energía (`+0x34`, 10), mando (`+0x38`, 13) y vitalidad
(`+0x36`, 14) y el nivel pedido (`+0x3a`, 15). Un byte distinto de 0 suma 3 · min(nivel, 9) y, del 10 al 13, 4, 9,
15 y 22 más (la tabla `0x5b04f4`); el poder va a la mitad y, salvo en los cetros, suma 2 · nivel; la defensa de los
escudos (`0xc00..0xdff`) suma solo el nivel. Cada requisito distinto de 0 es `(Item.bmd+0x20 + 3 · nivel) ·
requisito · 3 (4 la energía) / 100 + 20`, en 32 bits con signo. La columna 1 es 1 si el registro propio cumple cada
requisito con su stat más lo de los ítems (`+0x2a..+0x36`, en 16 bits, comparado con signo) y el nivel (`+0xe`), y 0
si no; lo abre y lo cierra en cada nivel (`0x5aff9b..0x5b04c3`): sin otro acceso abierto, 14 cierres de 3.848
`rand()`, 53.872. Las columnas 11, 12 y 16 no las escribe nadie.

La caja de arriba (`0x5b09b0..0x5b0dea`), desde el renglón 0: `"\n"`, el texto 160 con `strcpy` (Q-C613) en azul y
negrita, el nombre del ítem (`"%s"`) en negrita, `"\n"`, `" "`, `"\n"`, 46 espacios (51 con más de 800 de ancho) y,
salvo en los pergaminos, 13 renglones `" "` sin negrita (el color va uno más abajo: el del primero queda como estaba y
el que sigue al último, en 0); `0x77f3b66` en 1. `0x40cf00(ítem)` lee los bytes 2 y 3 del tipo en ItemSetType.bmd:
con el 3 de 0 a 3 y un conjunto en la pieza 1 (`0x40cf30`), un renglón verde (el 4) `"%s %s %s:(%s+%d)"` con su
nombre, el 1089, el 591, el 1090 y ese byte; con el 2, lo mismo con la pieza 2. Después las clases de `0x5af590` (las
del cuerpo del ítem), `"\n"` y `0x5af090(1, 1, cuántos, ancho, 2, 1)`. El ancho sale de la fila del nivel 0, en la x87
con 53 bits: una columna (dos con daño mínimo, salvo en los pergaminos), una más por el máximo (salvo en los
pergaminos), el poder, el del cetro, la fuerza y la agilidad (siempre fuera de los pergaminos), y 1,1f por la defensa,
su tasa, la energía, el nivel pedido, la vitalidad y el mando; eso por divisor · 17 / 90, más 0,5 y `_ftol`, y nunca
menos que el divisor. Después, `0x620a00(1)`.

Las columnas (`0x5b0dfb..0x5b10b6`), de izquierda a derecha desde la x: el título (`0x5afb00(columna, x, y)`: el
texto como formato de `sprintf`, sin argumentos, en el renglón 0, con el color 1 y la negrita que haya quedado,
Q-C751; la caja de ese renglón con el modo 3 y sin marco, y `0x78abb30` en 0) y las filas
(`0x5af9b0(columna, formato, &x, muestra, y, franja)`: un `sprintf` por nivel desde `0x78abb30`, con el color 0 si la
columna 1 de ese nivel es 1 y el 2 si no, y la negrita en 0, por fila; la caja con el modo 3 y sin marco; la x crece lo
que mide la muestra con la fuente que dejó la caja, / rx y `_ftol`, y `0x78abb30` vuelve a donde estaba). Son 14
filas, o una con la franja 5, que solo pasan el nivel y la energía (Q-C752). En orden: el nivel (161, `"+%d"`,
`"000000"`); el nivel pedido (la x + 2 antes y + 10 después; 1931); el mínimo (`"00 "`), el título del daño (162) y
la columna `"~"` (la del nivel, `" 00"`), y el máximo, sin título, salvo en los pergaminos; el poder (163) y el del
cetro (1239), con `"%2d%%"`; la defensa (164), la tasa (165, `"%3d%%"`), la fuerza (166), la agilidad (167, también
en 0 fuera de los pergaminos), la vitalidad (169), la energía (168) y el mando (1900), cada una si la fila del nivel 0
tiene algo; las de números, `"%3d"`. El switch de `0x5afb00` no tiene texto para las columnas 1, 11, 12 y más de 15
(`sprintf` con `NULL`, `0x5afb7a`), pero nadie las pide. Termina con `0x620980`.

MuRet: `Scenes::DrawTheItemHelp` (`Scenes/GameItemHelp.cpp`), con la tabla en `HudState::itemHelp`
(`Scenes::FillTheItemHelpLevels`, `Scenes/ItemHelpLevels.h`), las cajas de `Scenes::DrawTheTooltipBox` y las clases
de `Scenes::WriteTheItemClasses`, la misma del cuerpo del ítem. Corta con su dirección sin Item.bmd (`0x5b098f`), sin
Text.bmd (`0x5b09f0`), con el 160 de 100 bytes o más (`0x5b0a03`, Q-C613), sin ItemSetType.bmd (`0x40cf13`) o
ItemSetOption.bmd (`0x40cf46`) y con un nombre de conjunto sin `'\0'` (`0x40cf80`) (08, P-C146). Los tests son
`test_game_item_help.cpp`, en Lorencia y Devias, en 640 × 480, 800 × 600 y 1.024 × 768.

**El cartel del conjunto** (`0x40eca0`, en `0x6376c6` con `+0x9305` distinto de 0). La abre el chat con `"/"` y el
nombre de un conjunto de `ItemSetOption.bmd` (`0x40ebd0`, si no era un ítem): cada conjunto usado (`+0x3c` distinto
de `0xff`) pone `+0x9305` en 0 antes de comparar, y el suyo deja su lugar más 1 y `0x77f3b08` en 0; el texto sale
igual como chat (Q-C594). El dibujo es el del hito 5 (§ Los tooltips, "El cartel del set": `Scenes::DrawTheSetLabel`).

**La ventana /move** (el modo 4 de `0x77f3b08`, `IsOpen(2)`; CL8 hito 6, *estático*). La abre M (`0x6599a0` con el 2,
que pone el modo sin cerrar nada, `0x659f57`) o el comando del chat; F1, Esc, un destino o el renglón de abajo la
cierran. Recorre la lista de `0x723c68`, los destinos de `movereq.bmd` (05) que `0x448440` cargó una vez. Las dos
funciones arman lo mismo con el ancho de la pantalla (`0x6d5d60`): el ancho `w` de la ventana (190 con 640, 170 con
800, 150 con 1.024, 130 con 1.280; con otro, 0), el renglón `r = (0x7758568 · 640) / W` (sin signo; 12 con 640) y la
altura `n · (r + 2) + 0x3c`.

El paso (`0x5ecdc0`, en `0x636e24` sin nadie que agarró el mouse, salta a `0x449850` → `0x448580`, 2.585 B): con el
mouse en `0 < x ≤ w` y `0 < y ≤ alto`, `0x775857c = 1`. Después, cada destino en su orden, con el renglón de
`(r + 2) + 0x1a` a `+ (r + 2)`: abre el registro propio (`0x40b240`, `+0xf08` más 1), lee su zen (`+0xecc`), su nivel
(`+0xe`, sin signo) y su clase (`+0xb & 7`), el nivel (`+0x44`) y el zen (`+0x48`) del destino, y con la clase 3 o 4
el nivel por `0x6b8c64` (2/3 en float, `FILD`, `FMUL` y `ftol`); lo cierra (`0x40b4d0`: 3.848 `rand()` si lo deja en
0, Q-C183). Pasa con el nivel menor o igual (con signo), el zen menor o igual (sin signo) y el PK del héroe
(`+0x375`) debajo de 6; si además el mouse está en `3 ≤ x < w − 2`, dentro del renglón, y el botón izquierdo abajo
(`0x7d36874`, no el flanco: Q-C616), arma con `std::string` el texto 260, un espacio y el nombre de `+4`, y lo dice
con lo mismo que el chat de Enter, en línea (`0x4488d3..0x448ef8`: el freno `0x5767940`, "webzen", /whisper off y on
o /move muriéndose, y el C1 00); con o sin decirlo, `0x77f3b08`, `0x7d36874` y `0x7d366d4` en 0, y deja de recorrer.
Al final, el renglón de abajo (`3 ≤ x < w − 2`, de `alto − (r + 2) − 6` a `alto − 6`) con el botón abajo hace lo
mismo sin decir nada. Con la ventana abierta, el paso saca 3.848 `rand()` por destino (28 en el del laboratorio).

El dibujo (`0x5b10e0`, al final de `0x5b0550`, → `0x449860` → `0x448fa0`, 1.911 B): `0x620a00(1)`; el fondo negro al
80 % en `(1, 1, w, alto)` y el renglón rojo `(0,6, 0, 0)` en `(3, alto − (r + 2) − 6, w − 5, r + 2)`, con `0x621aa0`;
el blanco y `0x620a00(1)`. El título (933) con la negrita, `0x6c8e64 = 0` y `0xff1accff`, centrado (`0x623690`) en
`(w / 2, 5)`; las columnas (934, 935 y 936) con la normal y `0xffffb27f` en la y 25 y en las x de cada ancho (45, 105
y 155 con 640; 40, 100 y 145 con 800; 35, 90 y 130 con 1.024; 30, 75 y 110 con 1.280). Cada destino, desde la y
`(r + 2) + 0x1b`, abre y cierra el registro propio y compara como el paso; con el mouse en `3 ≤ x < w − 2` y en
`y − 1 ≤ my < y − 1 + r + 2`, si pasa y `0x5762eb4` es 0, el resaltado `(0,8, 0,8, 0,1, 0,6)` en
`(3, y − 1, w − 5, r + 2)` con el blanco y `0x620a00(1)` después. El nombre (`+4`) en blanco si pasa y si no en
`0xff1127a4`; el nivel (con los 2/3) y el zen con `_itoa` de base 10 (con signo), en `0xff1a33ff` el que falta y si
no como el nombre. Al final, el 1002 en blanco en `(w / 2, alto − (r + 2) − 5)` y `0x620980`. MuRet:
`Scenes::StepTheMoveWindow` y `Scenes::DrawTheMoveWindow` (`GameMoveWindow.h`), con `Scenes::SayInTheChat`; el paso de
la letra es `Render::Text::FontStepFor`, que con otro ancho da el de 1.280 y no 0 (Q-C59), así que con 1.600 la
ventana tiene 0 de ancho pero sus renglones no son de 2.

**La party y los comandos** (CL8 hito 6, *estático*). Las dos ventanas del paso que son marco, en `(0x1c2, 0)`. Las
dos usan el fondo de `0x5ee220` (la party tiene su copia): `Interface\Item_Back01.jpg` (`0x104`) de 190 × 256 en
`(x, y)` e `Item_Back02.jpg` (`0x105`) de 190 × 177 en `y + 256` (la suma de la x87 guardada en float, `0x6b8840`),
con du = 0,7421875 y dv = 1 y 0,69140625, escalados y sin alfa (`0x621b60`); y el botón de cerrar: `glColor3f(1, 1,
1)` e `Interface\exit_01.jpg` (`0x118`) de 24 × 24 en `(x + 25, y + 395)` con du = dv = 0,75. Con el mouse en
`izquierda ≤ x < izquierda + 24` (`fild` contra el float, lo mismo en y), la fuente normal, `0x6c8e5c = −1`,
`0x6c8e64 = 0xff000000` y la ayuda (`0x554340`) en `(_ftol(x + 25), _ftol(y + 395) − 13)`, que deja la normal y los
colores −1 y 0. El título de las dos va con `0x554180(x + 0x23, y + 0xc, texto, 120 · W / 640, 1, 3)` (la división
sin signo de `0x5f0cd6..0x5f0cef`): el modo 2 de `0x467060` lo centra en una caja de ese ancho que empieza en
`_ftol(x · rx)` y llena la caja con el fondo; con el texto vacío, mide el `"0"` y dibuja solo el fondo. MuRet:
`Scenes::WindowDrawing::DrawTitle`, con `Render::Text::CenterInBox` e `ITextDrawer::DrawColumns`, como el título del
inventario (Q-C710). El paso de las dos mira el mouse desde la esquina que dejó
el dibujo, que antes del primero es (0, 0) (Q-C650).

La party. `Open(4)` (`0x659a82..0x659c44`), con lo que cerró: `0x78aba48 = 0`, el pedido de la lista (`0x659aa6..
0x659b94`: el `C1 03 42` armado en la pila y mandado con `send` si el socket de `0x6c82a0` (`+0xc`) no es −1, en
claro: no pasa por el XOR32 ni por el cifrado del C3) y `0x7d5370a = 1`; suena como todos (`0x65ca72`). El paso
(`0x5e3c80`, desde `0x5ee18d` con `IsOpen(4)`): el mouse en `[x, x + 0xbe) × [y, y + 0x1b1)` desde
`0x78a7068`/`0x78a706c` pone `0x775857c = 1`; con miembros (`0x78aba48 > 0`, `0x5e3cf0`), cada uno con su nombre y
su botón de echar (`C1 04 43`, CL9.4); el clic (`0x7d366d4`) en `[x + 0x19, + 0x18) × [y + 0x18b, + 0x18)` lo apaga,
pone `0x77f3b10 = 0` y `0x6c8dcc = 6` y llama a `Close(4)` (`0x5e40d4..0x5e411d`). No saca `rand()`. El dibujo
(`0x5f0be0(0x1c2, 0)`, lo primero de `0x5f7440`, 2.746 B): cerrada, el ancho del mundo (`0x5bb8d0`, que con el
inventario abierto saca los dos `rand()` del comercio y de la tienda), `0x6c8e5c = −1`, `0x6c8e64 = 0` y, con
miembros, su lista a la derecha del mundo (`0x5f130a`, CL9.4). Abierta: la esquina en `0x78a7068`/`0x78a706c`, el
blanco, `0x620980`, el fondo y `0x620a00(1)`; el título (190) con `0xffdcdcdc` sobre `0xff141414`, en negrita; la
normal con `0xffe6e6e6` sobre 0 y, sin miembros, los textos 191 a 197 en `x + 0x14` y en `y + 0x32`, `0x41`, `0x50`,
`0x64`, `0x73`, `0x82` y `0x91` (`0x554180` con 0, 0, 0: un texto vacío no se dibuja, pero se mide y deja su tamaño
en `0x77f31b8`/`0x77f31bc`); con miembros, sus nombres, su vida y el botón de echar (`0x5f0df0`, CL9.4); con la
cuenta negativa, nada; y el botón de cerrar con la ayuda 221, desde la esquina que acaba de escribir (`0x5f11c2`).
Queda la mezcla normal.

Los comandos. El objeto `[0x6e592c]`: `+4` abierto, `+8` un personaje (*inferido*), `+0xc`/`+0x10` la esquina que
pone el dibujo, `+0x14..+0x1f` los doce estados y `+0x20` el comando elegido. `Open(0x200)` (`0x65a91c..0x65a934`)
prende `+4`; en Chaos Castle `CanOpen` no deja. Cerrarlo con `CloseByMask` (`0x65897a..0x658999`) o con su botón
(`0x40a58a..0x40a5a1`), entrar a la selección (`0x634271`) y volver al login (`0x634bf0`) dejan `+4 = 0`, `+8 = −1`,
`+0x20 = −1` y los doce estados en 1 (que 1 es "el comando se puede", *inferido*); `0x4c7b34` también, desde una
ventana vieja que corta antes (`0x4906a0`, hito 7). El paso (`0x40a450`, desde `0x5ee140` con `ECX = [0x6e592c]`):
con `+4` en 1, el mouse en `[x, x + 0xbe) × [y, y + 0x1b1)` es de la UI; `0x406050` pone los doce estados en 1 y,
con un comando elegido y un personaje elegido (`0x6c8e30 ≥ 0`), sigue en `0x406097` con lo que el comando hace sobre
él (CL9.4; Q-C653); los botones del 1 al 11 (el 0 no: Q-C651) van en `(_ftol(x + 95 − 50), _ftol(y + 53) + 28 · c)`,
y el clic estrictamente adentro de 108 × 20 (Q-C654) lo gasta, suelta el botón (`0x7d36874 = 0`) y elige el comando
(`+0x20`); el botón de cerrar la cierra sin `Close`: los dos sonidos con `0x41ed10`, `0x77f3b10 = 0`, `0x6c8dcc = 6` y
el objeto como al entrar. No saca `rand()`. El dibujo (`0x40a650`, en `0x5f75fa`): con `+4` en 1, la esquina en
`(0x1c2, 0)`, el fondo con la mezcla que haya, `0x620a00(1)` y el blanco; el título (938) en negrita, −1 sobre
`0xff141414`; los once botones (`Interface\Message_box.jpg`, `0xf0`, de 100 × 25 con du = 0,83203125 en `(x + 45,
y + 53 + 28 · c)`, la y sumada en float), el del elegido con `glColor3f(0,5; 0,5; 0,5)`; el botón de cerrar con la
ayuda 927 hecha por `sprintf` con `"D"` (Q-C652); `0x6c8e64 = 0x00141414` y cada nombre (la tabla de `0x40aca4`: 943,
1124, 944, 945, 946, 1352, 1321, 1322, 947, 948 y 949) centrado (`0x623690`) en `(_ftol(x) + 0x32, _ftol(y) + 8)`,
con la normal y el blanco o, el elegido y el que tiene el mouse encima (estrictamente adentro de 108 × 20), con la
negrita y `(1; 0,8; 0,2)`, o `(0,5; 0,5; 0,5)` con el clic; y su casilla (`0x463090` en `(_ftol(x) − 0x14, _ftol(y) +
8)`: sin mezcla, el marco `0x92 0x86 0x79` de 9 × 9 con cuatro `0x621aa0`, el blanco, la textura, la mezcla normal y,
el elegido, la marca: `Interface\win_check.tga`, `0x64a`, de 5 × 5 en `(x + 2, y + 2)` con uv 0,625). Al final, el
blanco, la fuente grande (`0x576355c`), con un personaje elegido su nombre en un cartel junto al mouse
(`Interface\CursorID_Wnd.jpg`, `0x65b`; `0x40aa8d`, CL9.4), el negro y el aviso del elegido según su estado: con 0,
el 255 del 5, el 1313 del 6, el 937 del 10 y el 257 del 3; con `0xff`, el 507; con `0xfe`, el 1320; en negrita,
`0x6c8e64 = 0`, centrado en `(x + 0x5f, y + 0x44)`. Termina con el blanco y `0x620980`.

MuRet: `Scenes::StepThePartyWindow` y `Scenes::DrawThePartyWindow` (`GamePartyWindow.h`),
`Scenes::StepTheCommandsWindow` y `Scenes::DrawTheCommandsWindow` (`GameCommandsWindow.h`), con lo común en
`Scenes::WindowDrawing` (`GameWindowDrawing.h`); el objeto es `UI::CommandsWindowState` y la limpieza
`UI::ResetTheCommands`; el `C1 03 42` es `Protocol::Party::WritePartyListRequest`, que la UI pide con
`IGameUiRequests::RequestPartyList` (sin él, abrir la party corta con `0x659aa0`). Lo que corta: 08, P-C116 y P-C117.
Las texturas nuevas del HUD: `0x104` a `0x120` (sin `0x106`, que no se sube), `0x64a` y `0x65b`.

**El HUD `0x5a7320`, hijo por hijo.** Empieza con `0x6c8e5c = −1`. La columna "base" es el estado de la traza
(sin ventanas, sin party, sin evento, sin arco); `r` son `rand()` por frame.


| # | Función | Qué | Condición | Base |
|---|---|---|---|---|
| 1 | `0x5a7520` | arenas de Tarkan (mapa 8: dos `0x622000` con `0x5c2`/`0x5c3` en (0, 0, 640, 435), UV por tiempo); Crywolf (34) con `0x576353c` → `0x435930` una vez; si no, `0x42b100` (humo; mira `0x4294b0`); `glColor3f(1,1,1)` | por mapa | blanco (traza 124245+) |
| 2 | `0x5a7640` | guerra de gremios: marcador y marcas | `0x576796c` y `hero+0x1f2 ≠ −1`; `0x576796f` | nada |
| 3 | `0x5a89b0` | barras de vida de la party y 'HP : %d0%' | `0x78aba48 > 0` (lista `0x77f3ce8`, 0x24 B) | nada |
| 4 | `0x5a6fc0` | info de Castle Siege | objeto `0x5767924` | nada (libera con `0x4e05f0`) |
| 5 | `0x42a4d0` | barras 50×2 de la cola `0x7239ac` | cola no vacía | nada |
| 6 | `0x42a660(hero)` | íconos de estado: `+0x78` bits `0x200..0x10000` → íconos `0x4b`, `0x35`, `0x40`, `0x43`, `0x45`, `0x46`, `0xcb`, `0xcc` en (180 + 25i, 1, 20, 24) | por bit | nada |
| 7 | `0x5ab810` | flechas (`+0xb6c == 0x80f`, Text 351) o virotes (`+0xbb4 == 0x807`, Text 352), con las del inventario (`0x557d80`, con su acceso anidado) y en x = ancho − party − (`0x5a`, `0x4b`, `0x41`, `0x3a` o 10 por resolución); IsOpen(4) solo con party | **abre y cierra el registro siempre** | 3.848 r |
| 8 | `0x5ab210(…)` | ayudante (`+0x318` ∈ `0x1c12..0x1c16`, `0x1c37`: Text 354/355/1187/1916, o el nombre de `Item.bmd` en los demás) con `0x5a6af0(ancho − 65, y + 12, 50, 2, durabilidad · 50 / 255)` y **un acceso al registro** (`0x5ab26c`), y vida del invocado (`0x5767960`, Text 356) en (ancho − 200, 16) | por equipo y paquete | nada; la traza tiene un ayudante `0x1c12` (el 13/0 de `Item.bmd`): su nombre (124259) y su barra (124268..124304), con 3.848 r más |
| 9 | `0x5aac20` | avisos de durabilidad: IsOpen(`0x20`); si no, 12 ranuras de equipo (`0..0x360` de a `0x48`), '%s (%d/%d)' con la máxima de `0x5b1ba0`, y el aviso en D+0x44 (3: sin la fuerza; 7 a 8: gastado o roto) | siempre | 1 + 12 × 3.848 r |
| 10 | macro | 'Macro Time' y `0x5a6af0(x, 404, 50, 2, …)` | `0x77f3b74 > 0` | nada |
| 11 | `glColor3f(1,1,1)`; `0x40bab0(this = 0x723c60)` + vfunc +4 | cuenta de DS/BC/CC: `0x40bab0` solo dibuja con +8 en 1..14; la vfunc +4 es un `ret` en DS (`0x40bdb0`) y en BC (`0x40c2d0`) y CC (`0x405090`) dibuja solo con +0x10 ≠ 0 y el mapa en 11..17 o 18..23 | objeto activo | nada (MuRet corta solo cuando dibujaría: CL9.5) |
| 12 | `0x5a9050` | globos y nombres sobre las cabezas (`0x77e1be0`) | entradas vivas | nada |
| 13 | `0x451d10`/`0x451c20` | carteles [LOJA] | singleton `0x7245c8` | nada |
| 14 | `0x5a6cc0` | marcador de duelo | `0x78abad8` | nada |
| 15 | `0x46b090(this = [0x5763528])` | caja sin leer | | nada |
| 16 | `0x413ca0` | Castle Siege (mapa 30) | `0x4294b0` | nada |
| 17 | `[[0x576350c]]+0x14` = `0x479080` | ventana del chat (*inferido*) | | sin leer |
| 18 | `0x5f7440` | ventanas (los paneles, abajo) | por máscara | 2 r |
| 19 | `0x65dd70(this = [0x5763534])` | imagen que se desvanece (slot `0x66a`) | `+0x18` | nada |
| 20 | `0x5a9290` | barra de abajo: Menu04 `0xe9` en (0, 387, 108, 45) y espejada en (532, 387); Menu01_new `0xe6` (0, 432, 256, 48), Menu02 `0xe7` (256, 432, 128, 48), Menu03_new `0xe8` (384, 432, 256, 48), opacos | siempre | 3.848 r; traza 124333..124379 |
| 21 | `0x4365c0` | UI de evento; **empieza siempre con `0x620a00(1)`** y sale con `0x6c73d1` en −1 (su `.data` es `0xff`) | `0x723bf9`, `0x6c73d1` | la mezcla normal, que hereda la XP |
| 22 | `0x5aa9a0` | entrada del chat (`0xf8`/`0xf9` en (186, 415)) e indicador F3 (`0xef` en (432, 420)); **empieza siempre con `glColor3f(1,1,1)`** y el `SelectObject` de la fuente normal | `0x6c8e68`/`0x77f3b69`; `0x6c8dd0` | el blanco, que hereda la XP |
| 23 | `0x5abcd0` | XP: nivel `[0x7681eb0]+0xe`; base = (n + 8)(n − 1)²·10 (más un extra arriba de 255); lleno = (xp − base)/(sig − base)·10; la parte fraccionaria es el ancho de la barra sin textura `(0.92, .8, .34)` en (221, 439, ·, 4) y la entera el dígito `(0.91, .81, .6)` en (425, 434); ayuda Text 201 en (223, 424) | siempre | 3.848 r; traza 124393..124407 |
| 24 | `0x5a8cc0` | AG: `+0x38` actual, `+0x3a` máximo; slot `0x101` en (551, 473 − h, 15, h) con h = act·36/máx y `(1, .95, .75)`; dígitos en (571, 467); ayuda Text 214 en (546, 419) | siempre | 3.848 r; traza 124427..124478 |
| 25 | `0x652c90` | BattleSkill | `0x4294b0` | nada |
| 26 | `0x436240` | confirmación de evento | | nada |
| 27 | `0x5ac120` | 3D en la UI (abajo, El 3D de la UI) | siempre | traza 124479..126583 |
| 28 | `0x5f7840` | grillas de ítems de las ventanas abiertas | por máscara | CL8.4 |
| 29 | `0x5a9630` | barra principal (tramos arriba) | siempre | 3 × 3.848 + 2 × 4 r (las celdas del héroe), y 3 × 60 r con una skill a la vista (`0x5a5926` y las dos `0x40ca20`); traza 126584..126848 |
| 30 | `0x5e9b10` | caja de contraseña (`0x78abb20`) | | nada |
| 31 | IsOpen(0) | si no hay ninguna abierta → `0x4125b0` (vfunc +0xc de `[0x5763530]`, el panel del asedio): con la clase 0 a la vista (+0x19, que su constructor `0x412195` deja en 1 y Tab apaga en `0x40f6d0`), `0x40f850` dibuja el minimapa con los personajes (2.645 B, con accesos protegidos y las texturas `0x69d`/`0x69f`): CL9.5 | siempre | 2 r |

**Las cuentas del HUD** (*verificado* contra la traza de Lorencia en 1024 × 768, frame 1, CL8 hito 2). El estado
base (sin ventanas, party, evento, flechas, ayudante ni skill) cierra 19 veces el registro propio (las flechas, los
doce avisos, la barra de abajo, la XP, el AG y los tres de la barra principal), dos veces cada celda del héroe (4 r)
y hace 9 consultas sueltas de `0x20` y `0x10000` (los avisos, las ventanas, el 3D, las grillas y el IsOpen(0) del
final, este de 2): **73.129 r** (19 × 3.848 + 2 × 4 + 9). La traza agrega el ayudante (3.848) y la skill `0x2c`
(180): 77.157. MuRet dibuja los 43 quads del 2D de ese frame (sin los textos) con la textura, la mezcla, el color, las
esquinas y las UV de la traza (`test_game_hud.cpp`); el orbe de la vida da su alto con la vida 1421 de 31131 y
`48 − v` (Q-C444). La vida, el maná y el BP que leen los orbes y el AG los escriben el `26` y el `27` (04, "La vida y
el maná", CL8 2026-11-13): el frame que sigue a uno los muestra (`TheLifeAndTheManaPacketsMoveTheOrbs`).

**El cursor del juego** (`0x5ac2d0`): `0x620a00(1)`, `glColor3f(1,1,1)`, un cuadro de 24 × 24 en (x − 2, y − 2),
como en el login, y **dos veces por frame** (Q-C442). El slot:

`0x620a00(1)`, `glColor3f(1,1,1)`, cuadro de 24 × 24 en (x − 2, y − 2) (como en el login). El elegido (*estático*):

| Orden | Condición | Slot (archivo) | Depende de (ola) |
|---|---|---|---|
| — | solo si `0x78abb20 == 0` y `0x7d3a10c == 0`; si no, el de abajo de todo | | caja abierta (CL8.3) |
| 1 | ítem del piso bajo el mouse (`0x6c8e28`) | 5 CursorGet | picking e ítems del piso (CL9.3) |
| 2 | NPC bajo el mouse (`0x6c8e2c`) | 6 CursorTalk, animado: cuadro `ftol(…) % 6`, cuartos de UV (du = dv = 0,5) | NPC (CL9.1) |
| 3 | objeto usable (`0x6c8e34`, tabla `0x7d34820`) | 8 CursorLeanAgainst si el tipo es el del mapa (0: `0x85`, 1: `0x3c`, 2: `0x5b`, 3: `0x26`), si no 9 CursorSitDown | objetos del mundo (CL7/CL9.1) |
| 4 | personaje (`0x6c8e30`), héroe `+0x3e2 == 0`: atacable (`0x558bb0`) y `0x775857c == 0` | 4 CursorAttack, o `0x68d` CursorAttack2 en el mapa 30; si no y con IsOpen(`0x200`), `0x65a` CursorID | combate (CL9.2) |
| 5 | IsOpen(`0x200`) | `0x65a` CursorID | identificación (CL9.3?) |
| 6 | `0x78abb0c == 1` / `== 2` | 7 CursorRepair / el mismo girado 45° por `sin(T·k)` (`0x621cc0`) | reparar (CL9.3, tienda) |
| 7 | botón izquierdo (`0x7d36874`): `0x77f3b5c == 0` → 3 CursorPush; si no 10 CursorDontMove | 3 / 10 | movimiento (CL6, ya está) |
| 8 | si no | 2 Cursor | |

En la base todo da −1 o 0 y queda el 2 (*verificado*: los dos quads finales son la GL 9 = slot 2). En CL8 entran el
2, el 3, el 10 y el 7/girado; los demás se cortan con su ola y quedan como `if` sobre globals que hoy valen −1.

**El 3D de la UI** (`0x5ac120`, *verificado* contra la traza 124479..126583): proyección de 1° (`0x6203c0(1.0,
W/H, 20, 2000)`), la cámara `0x7d366f0`, profundidad; con `0x78abb00` en 0 o 3, los ítems de las grillas abiertas
(`0x5f7610`), el del cursor (`0x5f7b10`) y las 3 ranuras rápidas (`0x558110(i)` → `0x5e1480(208 + 31i, 454, 20,
20, …)`); si no, `0x5f70a0`. `0x633110` (después del primer cursor) es el ítem 3D de los diálogos de NPC y de la
mezcla, con su propia proyección: CL9.3. El pase y el dibujo de cada ítem, en § El inventario ("El pase de los
ítems").

**Las teclas por paso** (`0x5ac8d0`, `0x63717f`, `GetAsyncKeyState` con un latch por tecla): SPACE levanta el ítem
del piso más cercano (`0x5650c0`, CL9.3); F1 cicla las ayudas (`0x77f3b08`, arriba en "Las ayudas de F1"); F2
alterna `0x6c8dd1` (el chat normal entra en la lista); F3 alterna `0x6c8dd0` (el modo susurro); F4 mueve el
registro del chat (`0x45d8c0`, `0x45a9d0`). **La captura** (PrintScreen, flanco de `0x2c` en `0x637cff`): `0x637700` arma
`Screen(%02d_%02d-%02d_%02d)-%04d.jpg` (mes, día, hora, minuto; contador `0x7d368ac` módulo 10.000) y el Text 459;
después del dibujo de la escena, `0x6202e0` hace `glReadPixels` en RGB y `0x63b150` escribe un JPEG de calidad 100;
el mensaje va al área de mensajes. No saca `rand()`.

### La captura de pantalla

(CL8 hito 6, 2026-11-12; *estático*, ASM de `c32bbd5f`.) Todo pasa en `0x637700`, en cualquier escena, y en
`0x6202e0`. Sus globales solo los leen esas dos: `0x7d368a8` (el pedido), `0x7d368ac` (el contador, de 0 a 9.999),
`0x7d36724` (el nombre) y `0x7d368b0`, que `0x6202e0` pone en 1 (`0x6202ed`) y nadie lee. No saca `rand()`.

- **El pedido.** Detrás del paso de cualquier escena, entre los relojes de los mensajes (`0x637cf3` y `0x637cf8`) y
  los frenos (`0x637d2c` y `0x637d44`), `0x553520(0x2c)` (`0x637cff`) da el flanco de Impr Pant con su latch
  (`0x77f36d4 + 4 · 0x2c`), y con él `0x637d10..0x637d25` *invierte* `0x7d368a8`: un segundo flanco antes del frame
  deshace el pedido (Q-C634). En Windows Impr Pant llega solo con la soltada (Q-C630).
- **Antes del dibujo** (`0x6381bb..0x63835a`, en cada frame, después de la salida por `WM_DESTROY` de `0x6381aa` y
  antes del `glClearColor` de cada mapa). `0x41ef00` (el sonido 3D, CL10); `GetLocalTime` (`0x6381c7`); `sprintf`
  (`0x69fd9b`) de `Screen(%02d_%02d-%02d_%02d)-%04d.jpg` (`0x6d64d0`) con el mes, el día, la hora, el minuto y el
  contador, en `0x7d36724`; `sprintf` con el texto 459 (`0x76debc0`) como formato y ese nombre, en la pila
  (`ebp − 0x14c`); el lugar del grupo elegido (`0x6d5ec4`) recortado entre 0 y 30, y `wsprintfA` de ` [%s / %s]`
  (`0x6d64f8`) con el nombre que `0x4c1860` copió en ese lugar de la lista (`0x7d36bb0 + 0x21e · lugar`) y el del
  héroe (`[0x743a170] + 0x1d9`), pegado detrás con `strcat`; y Shift, el byte alto de `GetAsyncKeyState(0x10)` (`0x638303..0x63832c`). Con el
  pedido y sin Shift, `0x555190(0x7d3a190, mensaje, 1)` (`0x638347..0x63835a`): el mensaje entra al chat antes del dibujo y
  sale en la captura. `0x7d3a190` y `0x7d3a194` son dos textos vacíos de la `.bss` que solo se leen acá.
  `0x6d5ec4` lo escriben el `0x6349a0` del login al entrar, con −1 (`0x634aa7`), y el clic en un servidor, con el
  lugar de su grupo (`0x646394`); nadie más (Q-C632).
- **Después del dibujo** (`0x638586..0x6385da`, antes del `glFlush` y el `SwapBuffers` de `0x6385e1`, que corren
  solo si la escena dibujó). Con el pedido: si `0x7d366bc`, un modo de editor que nadie prende, `0x517570` escribe
  `Data2\MonsterSetBase2.txt` (`0x6d6504`); después `0x6202e0` (`0x6385aa`), también cuando la escena salió con 0 y
  el frame no se presenta (P-C110). Con el pedido y Shift, `0x555190(0x7d3a194, mensaje, 1)` (`0x6385c4..0x6385d7`): el
  mensaje va después de la lectura y no sale. `0x6385da` apaga el pedido.
- **La lectura, `0x6202e0`.** `0x7d368b0 = 1`; `new(ancho · alto · 3)` con la pantalla de `0x6d5d60` y `0x6d5d64`;
  `glReadPixels(0, 0, ancho, alto, GL_RGB, GL_UNSIGNED_BYTE)` (`0x62031f`), sin tocar `GL_PACK_ALIGNMENT` (Q-C631);
  `0x63b150(nombre, ancho, alto, búfer, 100)`, sin mirar lo que devuelve; `free`; y el contador,
  `(contador + 1) % 10000` (`0x620345..0x62035a`).
- **El JPEG, `0x63b150`**, con la libjpeg 6b del binario: `jpeg_std_error` (`0x66c980`), `jpeg_CreateCompress(62,
  0x178)` (`0x66b8f0`), `fopen(nombre, "wb")` (`0x6c8828`), que si falla devuelve 0 sin destruir el compresor
  (Q-C633); `jpeg_stdio_dest` (`0x66c850`), el ancho, el alto, 3 componentes y `JCS_RGB`, `jpeg_set_defaults`
  (`0x66c030`), `jpeg_set_quality(calidad, TRUE)` (`0x66c010`), `jpeg_start_compress(TRUE)` (`0x66bc90`), las filas
  de arriba hacia abajo desde la última del búfer (`búfer + (alto − fila − 1) · ancho · 3`, `0x66bd10`),
  `jpeg_finish_compress` (`0x66ba30`), `fclose` sin mirarlo y `jpeg_destroy_compress` (`0x66b9c0`). Un error de la
  libjpeg va al `error_exit` de `jpeg_std_error`, que sale del juego (Q-C635).

**En MuRet** (`Scenes::Screenshots`). El pedido corre en el gancho de detrás del paso del `SceneManager`, después de
los relojes (con los frenos no comparte nada), y lo de antes y después del dibujo en dos ganchos alrededor del
`Render` de la escena; el de después junta primero lo que tiene el lienzo (el 3D y el 2D, como el `Present`). El
nombre y el mensaje se arman solo con el pedido: nadie más los lee. La lectura (`Render::Gl::GlScreenReader`) es de la
ventana antes del swap; el JPEG (`Data::Textures::WriteJpegFile`) usa la libjpeg-turbo por la misma API y da los
mismos bytes que la 6b (`tools/shot6b.c`, los vectores `screenshot-13x11` y `screenshot-40x24`). El archivo va a la
carpeta de trabajo con el nombre solo, como el `fopen`, y la hora es la local de SDL (`Platform::ReadLocalTime`). Lo
que no hace igual: la lectura con `GL_PACK_ALIGNMENT` en 1 (Q-C631), el lugar del grupo por el servidor elegido de
la sesión (Q-C632) y el frame retenido con el pedido, que corta (P-C110). Los tests: `ScreenshotsTest`,
`JpegWriterTest`, `GlScreenReaderTest` y los ganchos del dibujo en `SceneManagerTest`.

### El chat

**Lo que llega** (CL8 hito 3, 2026-11-12; *estático*). Los paquetes están en
[04](04-protocolo.md#el-chat-que-llega-00-01-02-0c-0d-y-f3-30); acá, lo que hacen sus funciones en la UI.

- **Los globos** (`0x77e1be0`, 100 entradas de `0x294` bytes en la `.bss`). `0x557170(nombre, texto, personaje,
  modo, color)` busca la entrada de ese personaje (`+0x274`) o una libre (nombre y renglón de abajo sin tiempo);
  con las 100 ocupadas, nada. Solo si el personaje está vivo y a la vista. El color es su PK (`+0x375`), o 0 con
  `+0x88` en 4; con gremio (`+0x1f2` como `short` ≥ 0), `0x556dd0` llena el gremio y el puesto con la tabla de los
  gremios (`0x787aa4c`, CL9.4). Con el texto vacío, solo el nombre: 100 pasos en una entrada nueva y 10 en la suya.
  Si no, en su entrada el renglón vivo sube al de arriba, y el texto dura `2 · largo + 0xa0` pasos (modo 0) o 1.000
  (modo 1, que también lo manda al registro del chat con el tipo 3); con 20 bytes o más se parte en dos renglones
  con `0x5549a0`, la cabeza arriba (`+0x16c`) y la cola, que empieza con el espacio, abajo (`+0x6c`).
  `0x557560(nombre, texto, modo)` busca por nombre entre los 400, primero los de `+0x88` en 1 y después los de 2,
  comparando con `strcmp` el nombre de `+0x1d9` (que sigue en `+0x1e3` con los diez bytes llenos). `0x557650`, en el
  paso del juego (`0x6370f9`), baja los tres relojes y pone en 0 los de un dueño muerto o fuera de la vista; con el
  chat abierto, el héroe en zona segura y el derecho sobre un globo ajeno (`+0x278..+0x284`, que escribe el dibujo)
  de un dueño con `+0x376 = 0xff`, llena el destino del susurro (`0x7798cf8`). Nada de esto saca `rand()`.
- **El dibujo de los globos** (`0x5a9050`, el 12 del HUD; leído en el ASM, *estático*). Pone la normal en el DC y
  recorre tres veces las entradas vivas (con el nombre o el renglón de abajo con tiempo). La primera proyecta con
  `0x6205b0` (la caché `0x6203c0` del pase del mundo) el punto (x, y, techo + z + 60) del dueño, con el techo en
  `+0x138` (120 los jugadores, `0x5122d4`, y 150 los demás tipos, `0x5123a9`) y el 60 de `0x6b86cc`; lo mide con
  `0x555840` y lo deja centrado en la x (la mitad del ancho pasado a 640 con `IDIV`) y 0x24 más arriba. `0x555840` mide
  con la normal: el ancho es el mayor del nombre más 3, el gremio, el puesto y los renglones con tiempo (los dos si el
  de arriba tiene; si no, el de abajo si tiene), y el alto, el paso de la letra por las cinco medidas (Q-C520); con el
  título de una tienda personal mide también el título (CL9.4). La segunda pasada, para cada par que se pisa en
  640 × 480, sube el primero arriba del otro si su y está antes de la mitad del otro, y si no lo baja debajo. La tercera
  corre cada uno contra los bordes (Q-C522) y llama a `0x555b00`. `0x555b00`, con `TextOut` distinto de 1: no dibuja
  si el dueño tiene el bit `0x10000000` de `+0x7c`, o si en el asedio (`0x7239ec`) un dueño ajeno con el bit `0x4000`
  de `+0x78` es de otro bando que el héroe (`+0x360`: 9 y 6, 10 y 7, 11 y 8, 13 y 12). Pone la mezcla 1 y el blanco,
  deja el tamaño en `0x77f31b8` y `0x77f31bc`, prende `0x77f3b66` (08, P-C83) y lleva el paso de la letra a 32 como
  mucho (en `0x7758568`) y el ancho a 1.280. El nombre va con el color de `+0x64` (seis colores, y `0xff0000ff` para
  los demás); el color de adelante (`0x6c8e60`) y el fondo son los del globo propio (`0xff00ffc8` y `0x9600643c`) o los
  de `0x513270` y `0x5132a0` con 1 si el `+0x1f2` del dueño es el del héroe (también los dos sin gremio) y con su
  `+0x370` si no (0: `0xffffe6e6` y `0x96321e0a`; 2 u 8: `0xff001eff` y `0x96000000`; otro: `0xff00ffc8` y
  `0x96503214`). Con el mouse encima (el tamaño pasado a 640 sin signo), el campo del chat abierto, el héroe en zona
  segura, otro nombre que el suyo y `0x77f3b94 % 6 < 3`, el texto y el fondo se cambian. Los renglones van de a un
  paso: el gremio y el puesto (enteros con el color de adelante: `0x553e50` toma como columnas de adelante el cx del
  texto de adelante), el nombre y, con el fondo de `+0x65` (0: `0x96321e0a`; 1: `0x9600321e`; otro: `0x96000032`), el
  de arriba y el de abajo si el de arriba tiene tiempo, o solo el de abajo, en `0xffc8dce6` o, con menos de 10 pasos,
  `0x80c8dce6`. Cada renglón es una caja de 256 × 32 (`0x553e50`) del ancho del globo y el alto del paso, que
  `0x554000` corre contra los bordes en píxeles y, con `0x77f3b66`, con la y contra 0 y contra `H − H · 47 / 640`
  (Q-C521). Nada de esto saca `rand()`.
- **Los que susurraron** (`0x77997f8`, diez lugares de 10 bytes más uno; el próximo en `0x77f3b9c`). `0x554d20`
  anota con `strcpy` (Q-C512) un nombre que no esté, si el nivel propio es menor que 10 (Q-C453), y el próximo vuelve
  al 0 después del 9. `0x555030` pone los 110 bytes en 0 y deja el próximo; lo llama la vuelta a la selección
  (`0x4c2671`), que también apaga el sonido de los avisos (`0x77f3b78`, `0x4c266b`) y prende el ataque automático
  (`0x6c8e3c`, `0x4c2533`).
- **La franja de la selección.** El primer `0D` con 1 que llega en la escena 4 queda en `+0x511` de la barra (la
  ventana `+0x7f50`), que `0x63ec00` dibuja en `(x + 7, y + 40)` con la fuente del 1710, blanco sobre 0, antes del
  1710. La barra se arma una vez, así que el texto queda para todas las selecciones siguientes.

**MuRet:** `Scenes/ChatReceive`, `Scenes/ChatBubbles` (la tabla, `0x557170`, `0x557560` y `0x557650` en el paso del
juego), `Scenes/ChatBubbleDraw` (el dibujo, con la proyección del pase del mundo que le pasa `GameScene`; un dibujo
intermedio escribe en una copia de la tabla, y el clic derecho lee la del último frame) y `UI/WhisperSenders`; la franja, en `WorldRuntime` (`chat.selectionNotice`), y el `F3 30` llena las teclas de
las skills del registro propio (`+0x26c`), el ataque automático, el sonido, los carteles prendidos (`0x6c7a8c`), Q, W
y E y los renglones y la transparencia del registro. El cartel de `Slide.bmd`, abajo.

### El cartel de `Slide.bmd`

Leído en la CL8 (hito 3, 2026-11-12; *estático*: no hay traza de GL con un cartel). El objeto `[0x5763528]` (`0x46aea0`,
0x121c bytes) tiene dos colas de `0x46a020` (la normal en `+4`, con la fuente de `0x5763554`, y la gruesa en
`+0x8d8`, con la de `0x5763558`), el período del timer (`+0x11ac`, 10 del constructor), los tramos de nivel
(`+0x11b0`, `+0x11c4` y `+0x11d8`; [05 § `Slide.bmd`](05-formatos-de-datos.md#slidebmd)) y la velocidad de los
carteles del timer (`+0x1218`, 2,5). Cada cola tiene un multimap por el segundo de salida (`+0x84`, con repetidos:
una clave igual entra detrás de las que había), la hora y los segundos que van (`+0x7c` y `+0x80`), dos buffers de
0x400 bytes (`+0xac` y `+0x4ac`, el de turno en `+0xa8`), el corrimiento, el empuje, la velocidad y la máxima
(`+0x8ac..+0x8b8`; 2,5 del constructor), la opacidad (`+0x8bc`), el pedazo de adelante (los bytes en `+0x8c0` y el
ancho en `+0x8c4`), el alto de la letra (`+0x8c8`), el color (`+0x8cc`) y el parpadeo (`+0x8d0`). Cada byte es una
letra (Q-C67). Solo `0x46b680` saca `rand()`.

- **El arranque** (`0x46b010`, en el primer paso del juego, `0x636920`): `0x46a460(0, 0)` en la normal y `(1, 0)`
  en la gruesa (la fuente, el segundo en 10, el parpadeo, `timeGetTime` en `+0x7c`, los buffers vacíos, el
  corrimiento y la velocidad en 0, el empuje en 1, el pedazo y el alto en 0 y la opacidad en 0), `0x45a9d0(0,
  0x1ad)` en las dos, `SetTimer(hWnd, 0x3eb, período · 1000)` y `0x46b0e0`.
- **El timer** (`0x46b0e0`): con la escena 5, `0x6c7a8c` prendido y la opacidad de la normal en 0 o menos, abre y
  cierra el registro propio para leer el nivel (`+0xe`, sin signo; 3.848 `rand()`) y llama a `0x46b680(nivel)`: el
  primer tramo con el nivel menor o igual que su tope (con signo); si su cuenta no es 0, `rand() % cuenta` (`IDIV`)
  y el texto de ese lugar si la lista lo tiene (sin signo); si no, `NULL`. Con texto, `0x46b720(1, 0, texto, 1,
  [+0x1218], 0xffc8dce6)`. El `WM_TIMER` con `0x3eb` (`0x4b1a6b..0x4b1b44`) lee el foco (`0x4c0ec0`, 1 `rand()`), con
  foco llama a `0x46b0e0`, y la cola común de la `WndProc` (`0x4b335a`) saca otro. El binario no importa
  `KillTimer`: el timer sigue al salir del juego, y fuera de la escena 5 solo saca los dos del foco.
- **Encolar** (`0x46b720(repeticiones, salto, texto, tipo, velocidad, color)`): con la escena 5, un texto no vacío,
  hasta 30 repeticiones (con signo) y el tipo de 0 a 5 (sin signo), los tipos 0 a 2 van a la normal con el tipo − 1
  y los 3 a 5 a la gruesa con el tipo − 4. `0x46abf0` agrega `repeticiones` entradas: con el tipo 1 de la cola, todas
  con la clave 0 (salen ya); con los otros, la i-ésima en el segundo de la cola más i · salto. Una con −1 caduca a
  los 60 segundos de su clave (`0x46ae07`). Lo llaman el timer y el `0D` de 10 a 15 ([04](04-protocolo.md)).
- **El paso** (`0x46b7b0`, en el paso de la UI, `0x636e35`): `0x46add0` en la gruesa y después en la normal. Una
  cola que tiene algo pasando no hace nada, ni cuenta sus segundos (`0x46a510`: está quieta sin texto o con menos
  bytes que su pedazo). Si no, `0x46ada0` suma un segundo si pasó más de uno desde `+0x7c`, y la primera entrada del
  multimap, si su clave ya llegó: si no caducó, `0x46a9c0` pone su color, elige su fuente en el DC y agrega
  `640 / cx(" ") · rx` espacios (en la x87, hasta 1.024) y el texto; si no entra en los 0x400 bytes, la entrada
  espera. Después, `0x46abc0` pone la máxima en `rx · velocidad` (o `rx · 2,5` sin una positiva) y la entrada sale.
- **El dibujo** (`0x46b090`, el 15 del HUD): con la gruesa pasando, la normal con `0x46a540(1)` (forzada a
  apagarse); si no, con la gruesa ya apagada, la normal con `0x46a540(0)`; después, con la normal apagada, la gruesa
  con `0x46a540(0)`. `0x46a540` no hace nada con `0x6c7a8c` apagado; baja la opacidad de a 30 hasta 0 si está quieta
  o forzada, y si no la sube de a 30 hasta 205; con 0 o menos sale. Dibuja con mezcla dos líneas negras de 1 de alto
  en `y − 3` y en `y + alto + 2` (con la opacidad hasta 180 menos 25, y desde ahí la opacidad) y una franja en
  `y − 2` de `alto + 4` con la opacidad menos 25 (`0x621aa0`, todo el ancho). Quieta y sin forzar, termina ahí. Si
  no, elige la fuente, saca el pedazo de adelante si el corrimiento lo pasó (`0x46a850`: el pedazo son hasta 4 bytes,
  medidos con `0x46ab30`; Q-C518), toma el alto de "Z" la primera vez y sube la y eso, y dibuja el texto con
  `0x5542f0(x + corrimiento, y, texto, 0, 6, 0)` (el modo 6, Q-C519) con el alfa `(opacidad + 50) · alfa del color /
  255`, a la mitad 5 de cada 10 textos dibujados (`0x5762ee4`) con el parpadeo en 1. Al final, `0x46a910` corre el
  texto: con el mouse sobre la franja (`y − 3`, `alto + 6`) el empuje es −1 y si no 1; la velocidad suma el empuje
  hasta la máxima, sin bajar de 0, y el corrimiento resta la velocidad.

**MuRet:** `Data::Text::LoadSlideTable` y `Scenes::LoadGameData` leen el archivo; `UI::SlideBanner` es el objeto,
en `HudState` (`hud.slides`), con `LoadTheSlides` (`0x46b3b0`), `ResetTheSlideQueues` (`0x46a460` y `0x45a9d0`),
`PickASlide` (`0x46b680`), `QueueTheSlide` (`0x46b720` sin la escena), `StepTheSlides` (`0x46b7b0`, en
`Scenes/GameUi`) y `DrawTheSlides` (`0x46b090`, en `Scenes/GameHud`). `Scenes/Slides` tiene el arranque
(`StartTheSlides`, en el primer paso de `GameScene`), el cuerpo del timer (`QueueATimedSlide`, con los 3.848
`rand()` del registro propio y el de `0x46b680`) y el timer (`RunTheSlideTimer`): `main.cpp` lo corre entre el cierre
del contador de una vuelta de `WinMain` y la siguiente, una vez por vuelta si venció, con los dos `rand()` del foco.
Ese momento es *inferido* (08, P-C80). El `0D` de 10 a 15 encola en `Scenes/ChatReceive`.

**La entrada del chat: escribir, mandar y las macros** (CL8 hito 3, *estático*; `Scenes/GameChat.cpp`,
`UI/ChatInput.cpp`, `UI/ChatCommands.cpp`, `UI/MacroFile.cpp`). Con `0x6c7b8c` en 1 (`.data`), el chat escribe en
dos EDIT de Windows del objeto `[0x57634f8]` (`0x4691e0`): el del mensaje en (`0xc1`, `0x1a6`) de `0xb4 × 0xe` con
`EM_LIMITTEXT 0x32`, y el del susurro en (`0x178`, `0x1a6`) de `0x32 × 0xe` con 10. Sus buffers son `0x7798bf8` y
`0x7798cf8` (de los diez de `0x100` que vacía `0x553570`), con sus largos en `0x7758590` y `0x7758594`; el campo
elegido, `0x77f3b70`; el historial, el deque de `+0x2e4` (`0x469540` guarda, `0x469790` recorre, `0x469a80` deja
diez). La subclase `0x467b10` le pasa Tab y Enter al WndProc y descarta Esc y `0x19..0x1c`. MuRet emula los EDIT como
el campo del login (`UI::TextField` en Latin-1): `EM_SETSEL(0, −1)` elige todo y lo primero que entra lo reemplaza,
sin cursor en el medio ni IME. Los ids de los dos EDIT (1 y 2, los dos primeros objetos que arma WinMain en
`0x4bb173` y `0x4bb184`) son *inferidos*.

- **Enter** (`0x4b50b0..0x4b88e9`): sale sin hacer nada en Chaos Castle fuera de la zona segura, con el diálogo
  `0x7d`, `0x95`, `0x8a`, `0x8d` o `0x8b`, con `0x78abb00` en 3 o si `0x657880(0xd)` toma la tecla. Con el campo
  abierto lee los EDIT a los buffers y lo escrito al historial; vacío (y sin `0x77f3b69`), Enter abre el campo (dos
  campos, los EDIT a la vista con el foco en el del mensaje) o lo cierra. Si no, corre los comandos (`0x56c440`: las
  compuertas, `/move`, `/filter`, `/1`../`0` y la cola de los nombres; cada comando social corta con su dirección,
  CL9.4) y el filtro (`0x622b40` con 1: el texto 570). Con `/` adelante, sin nombre o sin el modo de F3, va al chat:
  los emotes (`0x576eb0`, abajo, salvo montado fuera de la zona segura), el freno (`0x5767940`: con más de
  `0x32` no sale nada y con más de 0 no sale lo mismo que el anterior, `0x57676e4`; cada uno lo deja en `0x46`),
  "webzen" en el nombre del héroe (solo sale lo que empieza con `/` o `!`), `/whisper off` y `on` (textos 264 y 265,
  avisos 267 y 268, en `0x77f3b98`) o, muriéndose, `/move` no sale; después el `C1 00`
  ([04](04-protocolo.md#el-chat-que-se-manda-00-y-02)). Si no, el susurro: `0x554a40(6, nombre)` abre y cierra el
  registro propio (3.848 `rand()`), deja pasar con el nivel 6 o el nombre en la lista de `0x77997f8` y si no avisa
  con el 479; manda el `C1 02` (sin "webzen" en el nombre del héroe) y lo pone en el registro con el tipo 0. Al final
  (`0x4b8841..0x4b88e2`): el área vieja vuelve al renglón `0x72` (no con un comando atendido), el EDIT del mensaje se
  vacía, los EDIT se ocultan, el registro corre 100, el buffer del mensaje queda en ceros y el campo se cierra. El
  chat no saca `rand()`.
- **El paso** (`0x59ff10`): el clic derecho en un renglón del área vieja (`0x59ff45..0x5a009b`) pone el nombre en
  el buffer del susurro (Q-C530), y el derecho sobre un globo (`0x5577a2`, en `0x557650`), en el EDIT y en el
  buffer; PgUp y PgDn corren el registro tantos renglones como muestra (la vtable `+0x38` y
  `+0x34`); arriba y abajo recorren el historial con el foco en un EDIT y el campo par, y si no el nombre del susurro
  de `0x77f31cc` (cinco de `0x100`, desde el 2 de `0x6c8ea8`). Con un campo de texto, el mouse sobre un EDIT lo
  elige (`0x469500`) y es de la UI, el clic le da el foco (`0x468d50` → `0x4682a0(1)`; sus flechas y su barra no,
  P-C86) y el botón de F3 (`0x5a33dc`) cambia `0x6c8dd0`. Las macros (`0x5a116f..0x5a2b47`): con `0x77f3b74` en 0,
  fuera de Chaos Castle y con Alt, cada número abajo (Q-C451) corre `0x56c440(texto, 1)` (Q-C452), los emotes, el
  filtro y lo mismo que el chat de Enter; la que pasa los comandos deja `0x77f3b74` en `0x64` (`0x5a1ea1`).
  `0x637d2c` y `0x637d44` bajan los dos frenos de a uno por paso.
- **El emote** (`0x576eb0`, CL9 hito 1, *estático*): con ESI en el héroe, prueba 95 disparadores con
  `0x576960(lo dicho, texto, 0)` en un orden fijo (`0x576ede..0x577f88`): los textos 270 a 290, 292 a 316, 318 a 347
  y el 783 de `Text.bmd` por tramos, con 15 literales del binario metidos entre ellos (`0x6c90e0..0x6c912c`: `;`, ㅠ.ㅠ, ㅜ.ㅜ,
  `T_T`, ㅡ.ㅡ, ㅡ.,ㅡ, ㅡ,.ㅡ, `-.-`, `-_-`, `^^`, `^.^`, `^_^`, /ㅡ, ㅡ^ y, después del 783, `hustle`; los coreanos en
  CP949), y al final, cada uno solo, el 291, el 317 y el 348. El primero que está decide uno de 22 casos; sin ninguno,
  nada. Cada caso llama a `0x576a60(héroe, héroe, animación, acción)` y después manda su propio `C1 05 18` con la
  acción y la dirección del ángulo del héroe ([04](04-protocolo.md#la-acción-18)). `0x576a60`, con la acción de ahora
  (`+0x111`) de 1 a 12 o `0x8a` (las de parado de `0x4f2250`), pone la animación con `0x4ea030`
  (con las acciones del modelo del tipo del personaje, `+2`: un transformado no usa las de `Player.bmd`; con el elfo,
  `+0x1d4 & 7 == 2`, la siguiente, salvo de `0xb7` a `0xc0`) y manda el mismo `C1 18`: con el héroe parado salen dos
  iguales (Q-C776); caminando (`0x0d..0x21`), pegando o en otra acción, solo el del caso, sin animarse. La dirección
  de los dos sale del héroe aunque `0x576a60` reciba otro personaje (la guerra de gremios, `0x4d8473`, `0x4d8a39` y
  `0x4d8e1d`, hito 8). `0x576960` avanza con `_mbclen`: con la página de códigos coreana (949) no encuentra un literal
  que empiece en el segundo byte de un carácter doble; MuRet compara de a un byte, como con la occidental (1252). Ni
  la búsqueda ni los casos sacan `rand()`. En MuRet: `UI::FindTheEmote`, `Scenes::PlayTheEmote` y
  `Scenes::DoTheEmote`, con los `C1 18` por el `send` del chat.
- **Las teclas de `0x5ac8d0`**: F2, F3 y F4 cambian lo suyo y suenan (F4, con el registro en su ventana, va a x 0 con
  el inventario y una de las seis ventanas anchas, si no a `0xba`); F1 cicla las ayudas ("Las ayudas de F1"); la
  barra (CL9.3) corta, y después Impr Pant (`0x637cff`, la captura, P-C94).
- **El HUD**: el paso 10 dibuja "Macro Time" (`0x6c9310`) centrado en el ancho del mundo en y = `0x188` y su barra
  en y = 404 de 50 × 2; el 22 (`0x5aa9a0`), con el campo abierto o `0x77f3b69`, la caja sin mezcla (`0xf8` y `0xf9`,
  que `0x61ccb0` sube en `0x61cfe6` y `0x61d002`), el texto de los EDIT, los nombres del susurro de los otros lugares
  con el campo impar y, con el modo de F3, el indicador `0xef` (`0x61cee1`). MuRet dibuja el texto de los EDIT sin
  componer su ventana ni el cursor (P-C89).
- **`Data\Macro.txt`**: lo lee WinMain detrás de `Text.bmd` (`0x61ee30` → `0x61c1c0`, `fscanf("%s")`, Q-C449 y
  Q-C531). Lo escriben (`0x61c170`) la salida del juego, el cambio de servidor y el gate a los mapas 30 y 31, que son
  de otras olas salvo las dos primeras: en MuRet lo escriben los botones del menú de Esc del juego (hito 6, [Los
  diálogos del juego](#los-diálogos-del-juego)).

### Los diálogos del juego

CL8, hito 6 (2026-11-12). *Estático*, con el ASM de `0x623e70`, `0x62db60`, `0x632e30`, `0x633300` y `0x61c210`;
sin traza de GL con un diálogo. En MuRet: `Scenes/GameDialogSteps` (el paso), `Scenes/GameDialogDrawing` (el
dibujo), `UI/GameDialogs` (las cajas) y `Protocol/OptionData` (el `F3 30`). Los globales son de la `.bss`: el
abierto `0x7d3a10c` y el que espera `0x7d3a110` (`UI::DialogState`; `0x6236f0` abre, encola o pasa el que espera),
los renglones `0x7d36a98` (7 de `0x26` bytes seguidos), su cuenta `0x7d368e4`, los dos botones `0x7d368b8` (estado,
x, y, ancho y alto) y la página del `0x8c` (`0x7d3a0f4`, `0x7d3a0f8`, `0x7d3a0f9`).

**El despacho** (`0x623f78..0x6263d9`). Después de Esc, `0x623e70` compara el abierto en este orden: `0x6e` el menú
(`0x623f90`), `0x93` las opciones (`0x62556f`), los tramos de otras olas, las cajas con botones (`0x8a`, `0x8c`,
`0x8b`, `0x97` y `0x9b`, `0x62d756`) y, después de todas las comparaciones, el OK genérico (`0x6263df`), al que
llega cualquier id que no está. MuRet lo tiene como tabla (`Scenes::DialogRoutes`): cada ola registra sus ids, y
los de las otras cortan con la dirección de su tramo (P-C90).

**El menú de Esc** (`0x6e`). Cinco botones en la escena 5, x en `[0x104, 0x17c)` e y en `[(6 + 3i) · 10, + 0x16)`.
Cada uno, con el clic (que apaga), según la tabla de `0x62dac4`:

| Botón | Qué hace, en orden |
| --- | --- |
| 0, salir | "> Menu - Exit game." al log; guarda (`0x61c210` y `0x61c170`); con la caja del caos abierta, el aviso 592 de error; si no, la salida |
| 1, otro servidor | el log; con la caja del caos, el aviso; si no, la salida con 2; después guarda |
| 2, otro personaje | el log; guarda; con la caja del caos, el aviso; si no, la salida con 1 |
| 3, opciones | abre el `0x93` (o lo deja esperando) y no pasa por la cola |
| 4, cerrar | va derecho a la cola |

La salida pone `0x576793c` en 1, saca 1 `rand()` y manda el `C3 F1 02` con el tipo (`0x62449a`, `0x6244d0`,
`0x6244f2`; el `C3` saca el del relleno y el de la serial). Guardar es el `F3 30` de `0x61c210` y `Macro.txt`
(`0x61c170`, `UI::WrittenMacros`). La cola (`0x6240a1`): si el abierto no es el `0x93`, pasa el que espera; después
`MouseUpdateTime` en 0, su máximo en 6 y el sonido `0x19`.

**El `F3 30`** (`0x61c210`): `C1 1C F3 30` en claro, con los 24 bytes de su pila. Abre y cierra el registro propio
(3.848 `rand()`); para cada lugar de 0 a `0x71` con tecla (`+0x26c + elegido · 0x100 + lugar` distinto de `0xff`)
escribe el byte de la skill (`+0x6c + lugar`) en `[tecla]`; en `[10]`, 1 o 2 por el ataque automático, 4 u 8 por
el sonido de los avisos y `0x10` sin carteles; en `[11..13]`, el byte bajo de Q, W y E; en `[14]`, los renglones
del registro / 3 en el nibble alto (0 con su lugar en 0) y la transparencia · 10 truncada en el bajo (Q-C551). Sin
elegido o con una tecla de 24 o más (pisaría la pila), MuRet corta (`0x61c2c6`, `0x61c2e8`).

**Las opciones** (`0x93`). Sin ítem en el cursor, filas de `0x78 × 0x16` desde x = `0x104`: volver en y = `0x1e`
(deja el menú esperando, Q-C550), el ataque automático en `0x3c` (`0x6c8e3c`), el sonido de los avisos en `0x5a`
(`0x77f3b78`), el volumen en `0x78`, los carteles en `0x96` (`0x6c7a8c`) y cerrar en `0xe8`. El volumen
(`0x7d3a118`, de 0 a 9) se agarra en la barra de `0x32 × 8` que dejó el dibujo (`0x7d3a120`, `0x7d3a124`) y sigue al
mouse de a 5 píxeles, o cambia con la rueda; con otro valor, `0x41eed0` le pide a DirectSound −10.000 o
`−2000 · log10(10 / v)` (CL10, P-C79). Las cinco casillas del brillo (x desde `0x10e` de a `0x14`, `0x12` de ancho,
y de `0xc8` a `0xda`, con los bordes) ponen `0x6c9dcc` con el clic sin apagarlo. Son los mismos globales que las
opciones de la UI nueva del login ([Las opciones](#las-opciones)).

**Las cajas.** `0x632e30(texto)` parte el texto en renglones de menos de `0x26` (`0x632c90`), deja el primer botón
en `(1, 0x47, 0x8c, 0x46, 0x15)` y el otro en 0, y abre el `0x8a` (o lo deja esperando). `0x633300(renglones,
primero, id)` copia los textos de `Text.bmd` con un `strcpy` por renglón (Q-C554; el `0x9a` los parte uno detrás
del otro), arma los botones `(1, 0x15, 0x5a, 0x46, 0x15)` y `(3, 0x78, 0x5a, 0x46, 0x15)` (y = `0x78` en el `0x9a`) y
abre el id; el `0x99`, el `0xa3`, el `0x96` y el `0xa8` arman su texto con un formato (CL9.3). El paso (`0x62d756`):
cada botón con estado, con el mouse en `[x + 0xd5, x + w + 0xd5] × [y + 0x3c, y + h + 0x3c]`, va a 2 y con el clic
se elige (apaga el clic y el botón abajo); afuera, a 1 (Q-C552). En el `0x8c`, las flechas (`0x62d7eb`,
`[0xf9, 0x108]` y `[0x179, 0x188]` × `[0xca, 0xde]`) cambian la página con el clic sin apagarlo y vuelven a partir el
texto `0x7d3a0f4 + página`. Enter o un botón elegido cierran.

**El OK genérico** (`0x6263df`): lee y apaga Enter (`0x5763588`, que prende el WndProc en `0x4b50ff` y apaga la cola
del frame en `0x63b0b7`); salvo el `0x73`, el clic en `[0x11c, 0x162) × [0x62, 0x77)` también. Con uno de los dos,
el `0x70`, el `0x6f`, el `0x1a` y el `0x1c` cierran el juego (`SendMessageA(WM_DESTROY)`, `0x62828e`); el `0x73` y
el `0x8f` son de otras olas; los demás pasan el que espera.

**El dibujo** (`0x62db60`, en `0x637696` con la escena 5). Siempre deja `0x620980`, el blanco, la fuente normal en el
DC y los colores del texto en (−1, 0), y termina con `0x620a00(1)`. Los textos van con `0x554180` centrados en
`0x140 − (cx · 640 / W) / 2` y dejan su tamaño en `0x77f31b8`.

| Diálogo | Lo que dibuja |
| --- | --- |
| `0x6e` | 5 × `0xf0` en `(260, 60 + 30i, 120, 22)` con las UV `(0, 0, 0,83203125, 1)`, el del mouse otra vez en aditiva con `(0,8; 0,6; 0,4)`; los textos 381, 382, 383, 385 y 384 en y = `0x41` de a `0x1e` |
| `0x93` | 7 filas (la 5 de 44, la 6 en y = 232); 385 en `0x23`; `"%s  On"`/`"%s  Off"` con 386 (`0x41`), 387 (`0x5f`) y 919 (`0x9b`); 389 en `x = 0x140 − (cx + 0x36) / 2` (`0x7d`) y la barra en `(x + 0x144 + cx, 0x7f)`: gris 0,2 de 50 × 8, negro de 48 × 6 y blanco de `(v + 1) · 4,8` × 6; el marco del brillo (gris `(268, 198, 102, 22)`, negro `(269, 199, 100, 20)`), 5 casillas de 18 en y = 200 (`(0,8; 0,6; 0,3)` hasta `0x6c9dcc`, negras después) y las cifras 5, 7, 9, 11 y 13 de 12 × 12 en y = 202 con los colores al revés; 1840 en `0xb9` (Q-C555) y 388 en `0xed` |
| `0x8a`, `0x8c`, `0x9b` / `0x8b`, `0x97` | `0xfc` de 213 × 5 en (213, 60), 4 / 7 filas de `0xfb` de 213 × 40 y otra `0xfc`; cada botón con estado, `0xf0 + estado` en `(x + 213, y + 60, w, h − 1)` con las UV `(0, 0, w / 128, h / 32)`; en el `0x8c`, las flechas (`CursorSitDown` girada 270 y 90 grados, 25 × 16, en (253, 210) y (383, 210)): gris en su tope, 0,7 con el mouse y 0,5 con el clic, que apaga (Q-C553); los renglones del `0x8a` y el `0x8c` desde `((7 − renglones) · 0x12) / 2 + 0x4c`; los del `0x8b`, el `0x97` y el `0x9b`, el objeto del evento (`0x632a2b`) |
| OK genérico | `0xf0` de 213 × 64 en (213, 60) y el OK, `0xf1` (`0xf2` con el mouse en `[0x11c, 0x161] × [0x62, 0x76]`) de 70 × 21 en (284, 98); su texto en y = `0x4b`, o en `0x45` y `0x51` los de dos renglones: el `0x70` el 402, el `0x6f` y el `0x1a` el 405 y el 406, el `0x1c` el 409 y el `0x8f` el 401 |

`0x61ccb0` sube `Message_box` (`0xf0`), `ok`, `ok2`, `cancel`, `cancel2` (`0xf1` a
`0xf4`), `Message_box1` (`0xfb`) y `Message_box2` (`0xfc`); el `bar.jpg` de `0xfd` es de `0x5e9b10`, la caja de la
contraseña del HUD, y MuRet no lo sube.

### El inventario

Leído en el hito 4 de la CL8 (2026-11-12, *estático*, cotejado con el PE `c32bbd5f`). MuRet lo porta en
`Scenes/GameInventoryHud.cpp`, con los tests de `test_game_inventory_hud.cpp` en Lorencia y en Devias.

**La ventana** (`0x5f2340`, desde `0x5f7440`). Empieza con su `IsOpen(0x4000000)`. La x es `0x1c2`, o `0x104` con el
personaje o la party abiertos; la y, 0; las dos quedan en `0x78a5a14` y `0x78a5a10`, que leen las grillas y el paso.
Todos los quads son `0x621b60(slot, x, y, w, h, 0, 0, du, dv, 1, 1, 0)`, en el orden que sigue:

| Pieza | Función | Slot y lugar (desde x, y) | du × dv |
|---|---|---|---|
| fondo | `0x5ee220(x, y, 0)` | `0x104` (0, 0, 190 × 256) y `0x105` (0, 256, 190 × 177); con el tercer argumento, `0x117` (0, 225, 190 × 10), que el inventario no pide | 0,7421875 × 1 / 0,69140625; 0,625 |
| equipo | `0x5e2460`, con el registro propio abierto alrededor (3.848 al cerrarlo) y la mezcla de alfa | 8 `0x113` (15, 46, 40 × 40); 7 `0x112` (115, 46, 60 × 40); 2 `0x107` (75, 46, 40 × 40), salvo la clase 3 (`+0xb & 7`, con otro acceso adentro); 3 `0x108` (75, 89, 40 × 60); 4 `0x109` (75, 152, 40 × 40); 0 `0x10a` (15, 89, 40 × 60); 1 `0x114` (134, 89, 40 × 60); 5 `0x10b` (15, 152); 6 `0x10c` (134, 152); 9 `0x10d` (55, 89, 20 × 20); 10 y 11 `0x10e` (55, 152) y (115, 152) | 0,625 × 0,625, 0,9375 × 0,625 (alas), 0,625 × 0,9375 (40 × 60) |
| grilla | `0x5e3660(x + 15, y + 200, 0x78a9bb0, 8, 8)` | por casilla, 20 × 20 en (c · 20, f · 20): `0x115` vacía (tipo −1) u `0x116` con un tipo | 1 × 1, 0,625 × 0,6666667 |
| zen | `0x5f2441..0x5f27ef` | `0x10f` (50, 367,7f, 113 × 18) | 0,8828125 × 0,5625 |
| cerrar | sin el comercio (`IsOpen(0x20)`, 1 `rand()`) y sin la caja del entrenador (`0x449c00`) | `0x118` (20, 395, 24 × 24); ayuda: texto 225 | 0,75 × 0,75 |
| reparar | sin comercio, tienda de NPC, almacén, caja del caos, lista del asedio, `0x1000`, `0x4000`, entrenador 3 ni `0x78abb00`, y nivel > 49 | `0x11e`, u `0x11f` con `0x78abb0c` en 1 (48, 395); ayuda: texto 233 | 0,75 × 0,75 |
| tienda personal | la misma cadena, con el entrenador en cualquier valor y nivel > 5 | `0x657`, o `0x658` (`btn_pshop2`) con `0x800` o `0x20000` abiertos (76, 395); ayuda: 1127 abierta, 1125 cerrada | 0,75 × 0,75 |
| cartel del set | `0x40e720(x, y)`, método del objeto `[0x6eed3c]` | `0x652` (14, 22 + y en float, 162 × 20) | 0,6328125 × 0,625 |

Cada lugar del equipo y cada casilla toma antes el color de `0x5e22d0(ítem)`, que lee `+0x44`: 0 blanco; 1 (0,8;
0,8; 0,8); 2 (0,6; 0,7; 1); 3 y 99 (1; 0,2; 0,1); 4 (0,5; 1; 0,6); 5 (0,8; 0,7; 0); 6 (0,8; 0,5; 0); 7 (0,8; 0,3;
0,3); 8 (1; 0; 0); otro valor deja el que haya (Q-C471). Las casillas vacías también lo toman.

Los textos: el título (texto 223) con la fuente gruesa, blanco sobre `0xff141414`, por `0x554180` en el modo 2 en
(x + 0x23, y + 0xc) con una caja de `W · 120 / 640` píxeles (centrado en la caja, con la división de C, y el fondo en
toda la caja: [§ Texto](#texto); en 1.600 de ancho la caja mide 300, en dos cajas); el texto
224 en (`_ftol(zx)` − 30, `_ftol(zy)` + 3) y el zen en (`_ftol(zx)` + 10, ·), los dos con la fuente gruesa sobre
`0xff282828`, el primero en `0xff96dcff` y el zen con comas cada tres cifras y su color: `0xff0000ff` desde
10.000.000, `0xff0096ff` desde 1.000.000, `0xff18c900` desde 100.000 y si no `0xff96dcff`. Con el mouse sobre un
botón (en float, `x ≤ m < x + 24`), la fuente normal, blanco sobre negro, y `0x554340` en (`_ftol(x)`, `_ftol(y)` −
13) el de cerrar y − 12 los otros dos. El cartel del set: con la fuente gruesa, "[texto 989]" centrado en
(x + 0x60, `_ftol(arriba + 3)`) en negro sobre `0x64`, y otra vez en (x + 0x5f, `_ftol(arriba + 2)`) con el
blanco de texto y el color de los quads en (1; 0,8; 0,1) si el objeto tiene alguna opción de set (`+0x9348` o
`+0x9349`), o (0,5; 0,5; 0,5).

**Los `rand()` de la ventana**: el registro propio se cierra tres veces sin otros accesos (el equipo, el zen y el
nivel de los botones: 3 × 3.848) y el comercio y la tienda de NPC se consultan cinco veces (cerrar 1, reparar 2,
tienda 2; Q-C470): **11.549** por frame. Con el inventario abierto, `0x5bb8d0` (el ancho del mundo) también consulta
el comercio y la tienda: 2 más en `0x63733e`, 2 en `0x63738a` y 2 en los avisos de durabilidad del HUD (`0x5aac20`),
y 2 por cada munición a la vista (`0x5ab810`).

**El mundo angosto** (`0x6371a0`). `0x5bb8d0` da 640 sin ventanas, 450 con una y 260 con dos. `0x63733e` lo pide para
`0x620db0(0, 0, w, 48)` (solo la modelview) y `0x5fbb00(w · 0,0015625f, &p)`, que achica los cinco planos y el rango
de bloques (`FrustumPointsOf`, el medio ancho por el factor) y pasa w al trapecio (`0x5fb570`). `0x63738a` lo vuelve a
pedir para el pase del mundo, `0x620db0(0, 0, w, 432)`: el viewport de `W · w / 640` píxeles y el aspecto de 0x6203c0
con ese ancho. El HUD y el 2D siguen en 640 × 480.

**Los ítems 3D de las grillas** (`0x5f7610`, desde `0x5ac120`). Con el inventario: `0x5e2fc0`, el equipo con el
registro propio abierto alrededor (3.848), en el orden 8, 7, 2, 3, 4, 0, 1, 5 y, por `0x5e2d00`, 6, 9, 10 y 11, cada
uno con tipo y `+0x3c > 0`; y `0x5e3760(x + 15, y + 200, 0x78a9bb0, 8, 8, 0)`, que recorre las 64 casillas y dibuja
solo la de arriba a la izquierda de cada ítem (`+0x3c` en 1). Después, en este orden, la tienda (`IsOpen(0x10000)`,
1 `rand()`), el comercio (`IsOpen(0x20)`, 1), el almacén, la caja del caos, la lista del asedio en el modo 3 con
`0x6689d0`, las tiendas personales y la caja del entrenador (`0x44ad40(0)`). El ítem del cursor (`0x5f7b10`):
`0x7879f80` con `0x7879fbc > 0`, centrado en el mouse, con el último argumento de `0x5e1480` en 1. Las tres ranuras
rápidas: `0x5e1480(208 + 31i, 454, 20, 20, tipo, +4, 0, 0, 0)`.

**El ítem de la UI** (`0x5e1480(x, y, w, h, tipo, nivel, excelente, set, siempre)` → `0x5e0720`). El modelo es
tipo + `0x212`, salvo: `0x1c0b` por `nivel & ~7` (8 → `0x223e`; `0x10`, `0x70` y `0x78` → `0x223f`; `0x18` y `0x68`
→ `0x2240`; `0x28` → `0x2242`; `0x30` → `0x2243`; `0x40` a `0x60` → `0x2244`); `0x1a0e` con 8 → `0x224a`; `0x1c09`
con 8 → `0x2241`; `0x1a0b` con 8 → `0x224c`; y por `nivel >> 3` con signo: `0x1c15` (1 → `0x2245`, si no `0x1e27`),
`0x1a13` (0, 1 y 2 → `0xc1c`, `0x225` y `0xa24`), `0x1c17` (0 y 1 → `0x1e29` y `0x2246`), `0x1c18` (0 y 1 → `0x1e2a`
y `0x2247`) y `0x1a14` (0 → `0x2249`, 1 a 3 → `0x2248`); con otro nivel, esos cuatro no dibujan nada. `0x5e0720` arma
un objeto local y llama a `0x607b60` (los `rand()` de lo que cuelga del modelo: 1 con `0x220` y `0xc19`, 2 con
`0xc17` y `0xe20`) y a `0x60cf70` → `0x60b3d0`, que con el objeto en blanco llama una vez a `0x609640` (2 con `0xe21`
y `0xe22`) y, sin excelente (`& 0x3f` en 0, o con uno de los modelos `0x1a12` a `0x1a18` o `0x1c30`, que no lo muestran) y
con el set (`& 3`) en 1 o 2,
hace el pase del set (`0x60cc29`): con `sin(0,001 · T) · 0,5 + 0,4 ≤ 0,01` en la x87, `rand() % 100` en el `+0xc0`
del modelo. T es el float del reloj del mundo (`0x5767b88`); el pulso se guarda en un float para el alpha del pase
(`FST`, `0x60cc19`), pero la comparación es la de la x87 sin redondear (`FCOMP`, `0x60cc1c`): un pulso apenas arriba
del `0,01f` cuyo float ya es `0,01f` no saca el `rand()`. MuRet hace las dos cosas con `Render::Models::SetPulseAt` y
`SetPulseIsOff`, en `DrawItemFromMeshes` (el piso, lo que cuelga de los personajes y la UI) y en la cuenta de la UI
sin modelos (CL8, 2026-11-13; `PassColorsTest` y
`UiItemDrawingTest.TheSetRandomWaitsForTheUnroundedPulse`). `0x1a18` llega a sus uniones (`0x60c3b6`, `0x530860`), con el objeto local de `0x5e0720` de dueño: MuRet
corta ahí (P-C70).

**El punto de la caja** (`0x5e1486..0x5e1d69`, *estático*, leído en el ASM). Con el cursor vacío (`0x7879fbc` en 0) o
con `siempre`, el ítem está bajo el mouse si x ≤ mx < x + w e y ≤ my < y + h (el `FILD` de `0x7d3682c` y `0x7d36828`
contra la suma de la x87). El punto es x + w · kx e y + h · ky en la x87, con los factores de la tabla por el tipo:
0,8 y 0,85 (`0x6b8a38`, `0x6b8bac`) de `0` a `0x1ff`; 0,6 y 0,5 con el `0x40d` y 0,8 y 0,7 con los demás de `0x200` a
`0x5ff`; 0,6 y 0,65 de `0x600` a `0x7ff`; el `0x811`, el `0x813` y el `0x814` con los suyos y 0,7 y 0,7 de `0x808`
a `0x9ff`; 0,6 y 0,55 hasta `0xbff`; 0,5 y 0,6 hasta `0xdff` (0,7 con el `0xc0f`, 0,9 con el `0xc10`); 0,5 y 0,8
hasta `0x11ff` (1,05 con el `0x1002`, el `0x1004` y el `0x1006`; 1,1 con el `0x1003` y el `0x1008`); 0,5 y 0,9 hasta
`0x17ff`; y de ahí por tipo y por paso (el `0x1a0e` sin el 8 y el `0x1a0f` suman la altura
entera, 0,6 en x), con 0,5 y 0,7 de `0x1a00` a `0x1bff`, 0,5 y 0,6 fuera de `0x1c00..0x1dff` y 0,5 y 0,95 adentro.
Con un paso que la tabla no tiene (el `0x1a13` fuera de 0 a 2, el `0x1a14` fuera de 0 a 3, el `0x1a0b` fuera de 0 y
1 y el `0x1c15` con el 2 o desde el 4) salta al final sin sumar: la esquina (Q-C481). Cada coordenada va con
`_ftol`, y `0x620480` la desproyecta con la cámara de la UI: el píxel en enteros sin signo (`W · x / 640` y
`H · y / 480`, con `imul` y la división por multiplicación), menos el centro de `0x620d10`, por las escalas de
`0x6203c0` y por 2.000 y −2.000, girado con `0x5fcc30` y más la cámara, que queda en `0x7d36834`.

**La pose** (`0x5e0720`, *estático*). Acerca el punto a la cámara (`0x5fc790` con 0,1f, o 0,07f con `siempre`, en la
x87 con el `fstp`); pone los ángulos de `0x78aadcc..0x78aadd4` por el modelo, con los corrimientos de la posición del
`0x1031`, el `0x1030`, el `0x1230`, el `0x122f`, el `0xa27`, el `0xc1e`, el `0x620` (este con el double 0,01 de
`0x6ba618`; los demás con floats) y del `0x1c32` al `0x1c37`; las armas de `0x212` a `0xe11` se inclinan 15 o 25 en
z por el byte `+0x1e` de su registro de Item.bmd (las dos manos). El `0x1e1e` con un paso que no es 0, 1 ni 2 no los
escribe (Q-C478). Bajo el mouse, la y gira con el reloj (`0x5767b88` · 0,45 de `0x6ba224`). Elige el que posa (el
`0x20a` con las armaduras, de `0x1012` a `0x1a11`; el `0x1e1e` dibuja y posa el `0x223a` con el paso 0 y el `0x223b`
con el 2), la altura de más de la raíz del que dibuja (−160, −100, −50 o −70 por la familia de la armadura, en el
`+0x84` del modelo que posa) y la escala por el modelo con los bits del binario (el `0x225`, el `0xc1c` y el `0xa24`
con el nivel −1 que les pasa el `0x1a13` van con otra y con el nivel en 0; el `0x1e32` al `0x1e34` suben 0,05 y
apagan la x y la z de los ángulos). Después `0x4eb860(&0x68b181c, 0, 0, 0, ángulos, &0x78aadd8, 0, 0)` posa en el
buffer global con la acción del modelo y el frame en 0; el objeto local va con el modelo en `+2`, `0x607b60` (sus
`rand()`), la escala, la posición, `+0xe8` en 0 y `+0x1d4` en 2, y `0x60cf70(objeto, modelo, 0, (1, 1, 1), 1, nivel,
excelente, set, 1, 1, 1, 0, 2)` lo dibuja como un ítem del piso: `0x4ebc80` con el buffer global y `0x60b3d0`, sin
dueño y sin luz por vértice (Q-C480).

**El pase de los ítems** (`0x5ac120`, *estático*). `0x621a90` cierra el 2D; en la PROJECTION, `glPushMatrix`, la
identidad, `0x620d10(0, 0, W, H)` y `0x6203c0(1, W / H, 20, 2.000)` (el aspecto con `FILD` y `FIDIV`); en la
MODELVIEW, `glPushMatrix` y la identidad, que `0x620370` copia en `0x7d366f0`: la cámara de la UI en el origen, sin
giro. `0x620850` y `0x620890` prenden la profundidad y su máscara sin limpiarla (Q-C479). Después de los ítems, la
identidad, `glTranslatef` con −`0x7d36884..0x7d3688c`, otra vez `0x620370`, `0x620480(100, 100, local, 1)` (que
escribe `0x7d36834`), dos `glPopMatrix` y `0x6219b0`, que abre el 2D (P-C73).

**El dibujo en MuRet** (`Scenes/UiItemDrawing.h`). `MakeUiItemPass` arma el pase de la UI (la pantalla entera, la
perspectiva de 1° con 20 y 2.000, la vista en la identidad) y su `GameProjection`; `UnprojectGamePoint` es
`0x620480` con esas escalas; `UiItemAnchor`, `UiItemHovered`, `UiItemCallOf` y `PlaceUiItem` son `0x5e1480` y
`0x5e0720` hasta la pose, y `DrawUiItemModel` posa en el buffer de la pose del frame, saca los `rand()` de
`0x607b60` y dibuja con `DrawItemFromMeshes` (`0x60b3d0`) y los contextos del frame. El primer ítem que dibuja
cierra el lote 2D (`flushSprites`) y empieza el `ModelBatch` con el pase de la UI sin inicializar el material (como
`0x5ac120`, que no pasa por `0x620db0`); el final de `0x5ac120` (`CloseUiItemPass`) dibuja el lote de los modelos: los
ítems quedan encima del 2D de antes y debajo del de después, y un frame sin ítems no cambia. Las cajas: el equipo en
la esquina de la ventana más la tabla de `0x5e2fc0` (el 3 sube 10), la grilla en (x + 15 + 20 · columna, y + 200 + 20
· fila) del tamaño de Item.bmd (`+0x24` y `+0x25`, por 20), el cursor del tamaño de Item.bmd centrado en el mouse y
las ranuras en ((float)(208 + 31i), 454) de 20 × 20. Los ángulos de `0x78aadcc` viven en `HudState` (y en su copia
del frame proyectado). Sin la biblioteca de los modelos (las escenas de las pruebas), cada ítem saca los mismos
`rand()` sin dibujar.

**Las cifras** (`0x5f7840`). Sale si no hay inventario, tienda, almacén, caja del caos ni comercio, salvo con la lista
del asedio en el modo 3 o la caja del entrenador. Con la mezcla de alfa y el color (1; 0,9; 0,7), `0x5e3760(…, 1)`
dibuja las pociones `0x1c00` a `0x1c08` con durabilidad 2 o más (9 × 10, en `x + 13`) y los paquetes
de joyas `0x181e` y `0x181f` con ((`+4 >> 3 & 0xf`) · 5 + 5) · 2 (7 × 8, en `x + 14`). Después, las mismas consultas
que `0x5f7610` para las otras grillas.

**El paso** (`0x5ee1a3`: con el inventario abierto, `0x5e4ce0` y sus seis hermanas). `0x5e4ce0` pone `0x775857c` con
el mouse sobre la ventana (190 × 433) o sobre la de las otras ventanas abiertas, que consulta en orden (`0x10000`,
`0x40`, `0x100` o la lista del asedio en el modo 3 con `0x6689d0`, `0x20`, `0x1000` o `0x4000`, `0x800` o
`0x20000`); abre y cierra el registro propio doce veces, una por lugar, y pone `+0x44` en 1 en los ocupados y en 0 en
los vacíos, y lo mismo en las grillas (el comercio conserva el 99) y en la caja del entrenador (`0x449c60`); deja
`0x78abaac` en 0 y, con el mouse arriba de `_ftol(y + 200f)`, corre el equipo (`0x5c3d20`), si no la grilla
(`0x5c4110(x + 15, y + 200, 0x78a9bb0, 8, 8, 0)`). `0x5c3d20` llama a `0x5bec50(x, y, w, h, lugar)` con los
rectángulos del dibujo: el 8 y el 7, el 2 (sin la clase 3) dentro de un acceso que lee la clase, y los otros nueve;
cada `0x5bec50` sale con `0x5767954 > 0` o con la traba `0x78abb39` antes de abrir el registro, y si no lo abre y lo
cierra (el del 2 queda adentro). Sin ítem en el cursor, con el lugar vacío o el mouse afuera no hace más (`0x5c2bf0`);
con el mouse sobre un lugar ocupado, `0x78abaac` en 1 y, con el clic, levantarlo o repararlo; con un ítem en el cursor y
el mouse adentro, soltarlo (`0x5bee04`). `0x5c4110` sale con la espera, la traba o un ítem en el cursor, y si no busca
la casilla con un tipo bajo el mouse (`0x5c41f4`). Después: `0x78abb10` en 0, el nivel con el registro abierto, y sin
la caja del entrenador, la ventana de un NPC de evento ni las otras (`0x20`, `0x10000`, `0x40`, `0x100`,
`0x78abb00`), los botones del pie en y + `0x18b` a y + `0x1a3`: reparar (x + `0x30` a x + `0x48`, nivel 50 o más,
que además deja `0x78abb10` en 1) invierte `0x78abb0c`; cerrar (x + `0x14` a x + `0x2c`) pone `0x77f3b10` en 0 y
`0x6c8dcc` en 6 y cierra el inventario; la tienda personal (x + `0x4c` a x + 100, nivel 6 o más) y la S
(`0x553520(0x53)`, sin el campo del chat) abren o cierran `0x800`. Por último, las grillas de las otras ventanas, la
caja del entrenador (`0x44ad10`) y el cartel del set (`0x40e6c0`, Q-C472). Las hermanas empiezan con su `IsOpen`:
`0x5e5920` (`0x1000` y `0x4000`), `0x5fe610` (`0x100`), `0x5ea060` (`0x40`), `0x5ecde0` (`0x20`), `0x5ea290`
(`0x800` y `0x20000`) y `0x5ed960` (`0x10000`). **Los `rand()` del paso** sin nada bajo el mouse: 13 cierres del
registro (los doce lugares y el nivel) y 8 consultas (2 de las marcas, 2 de los botones, 2 de las otras grillas y 2
de las hermanas): **50.032**; con el mouse arriba de la grilla, 12 cierres más (46.176).

**El ítem del cursor** (CL8 hito 4, 2026-11-12, *estático*). `0x7879f80` es un ítem entero (0x48 bytes) y
`0x7879fbc` su cuenta; lo acompañan la grilla de origen (`0x78ab9b8`: el inventario u otra), el lugar de origen
(`0x78a6758`: 0 a 11 el equipo, 12 + casilla la grilla), el destino del último `24` (`0x77f3cc8`) y la traba
(`0x78abb39`), que prende el envío del `24` y suelta su respuesta ([04](04-protocolo.md#mover-y-equipar-ítems-24-28-2a-f3-13-y-f3-14)).
`0x5be150` lo vuelve a su lugar: en la grilla, la copia en cada casilla de su huella desde la columna y la fila del
lugar, con `+0x3c` en 1 solo en la de su columna y su fila; en el equipo, antes el ayudante de la mascota (el 8,
`0x443fa0`), el cuervo (el `0x1a05` del 1, `0x420cf0`) y la capa del Wizard's Ring de nivel 3, y la copia con el
registro abierto; después la cuenta en 0, `0x512a30` y el sonido `0x1d`. Esc lo llama al abrir el menú (`0x623f42`).

**Levantar de la grilla** (`0x5c4110` desde `0x5c41f4`, sin ítem en el cursor). La casilla con un tipo bajo el
mouse marca con 2 la huella de su ítem (el ancho y el alto de Item.bmd desde su columna y su fila), deja
`0x78aba27` en 0, el ítem del cartel en `0x78abaa8` con su copia en `0x787a518`, `0x78abb2c` en la casilla + 12 (+
`0x4c` con la tienda personal abierta, sin `rand()`) y la esquina de la ayuda en (izquierda + ancho · 20 / 2 +
columna · 20, arriba + fila · 20). Con Ctrl y el flanco de Q, W o E, el tipo y el nivel (`(+4 >> 3) & 0xf`) van a esa
ranura rápida. Con el clic y sin `0x576794c`: el modo de reparar corta (`0x5c4470`, CL9.3); si no, el clic
consumido, la copia al cursor desde la casilla de arriba a la izquierda, la grilla y el lugar de origen,
`0x5bd750` (la huella en `0xffff` y `+0x3c` en 0), el ítem del cartel en nada, `IsOpen(0x20)` (1 `rand()`) y el
sonido `0x1d`. El clic derecho usa el ítem (`0x5c5895`, CL9.3); con la tienda personal abierta, el clic y el derecho
son de ella (`0x5c5110` y `0x5c5242`, CL9.4).

**El equipo** (`0x5bec50` desde `0x5c2c56`). Con el cursor vacío y el mouse sobre un lugar ocupado, `0x78abaac` en
1, con lo que `0x5d8d50` no corre en ese paso. Sin el clic, `0x5c35ad`: la marca 3 si le falta un requisito, con
`+0x1e`, `+0x20`, `+0x22` y `+0x26` contra la fuerza, la agilidad, la energía y otra vez la fuerza, con sus
agregados (Q-C475), y si no la marca 2; el ítem del cartel, `0x78abb2c` en el lugar y la esquina de la ayuda en
(izquierda + `_ftol(ancho)` / 2, arriba). Con el clic: con `0x576794c`, nada; con `0x78abb0c`, reparar (`0x5c2dd5`,
CL9.3); si no, consume el clic y, salvo en Icarus lo único que vuela (las alas `0x1800` a `0x1806` y la Cape of
Lord en el 7; Guardian Angel, Imp, Horn of Dinorant y Dark Horse en el 8) y en Chaos Castle `0x1a02` a `0x1a05`, pasa
el ítem al cursor (el lugar queda en `0xffff` con su `+0x3c` como estaba), viste al héroe (`0x512a30`, que recalcula
los derivados con `0x550d10`), borra el ítem del cartel, deja `0x78abb2c` en el lugar + 12, pide el sonido `0x1d` y
apaga los ayudantes (el 8) o el cuervo (el `0x1a05` del 1). Con un ítem en el cursor y el mouse en un lugar,
`0x5bee04` prueba si va: el lugar de Item.bmd (`+0x22`; el 1 y el 11 también toman lo del 0 y el 10), la clase (el
Dark Lord con `+0x49`, el elfo y los arcos, el Magic Gladiator con las dos manos), los requisitos contra el registro
(en words que dan la vuelta; con el `0x1a5`, además, el nivel del cuervo `[0x723968]` · 15 + `0xb9` contra el
liderazgo sin agregados, por `0x4221a0`, Q-C775), el mapa (Atlans, Icarus y Chaos Castle con las monturas), Crywolf (las etapas 3 y 4
con `0x200000` en `+0x7c`) y la montura en uso. Si va, la marca 2 y, con el clic, `0x78abaac` en 1, el destino
(`0x77f3cc8`; un arma de una mano o un bastón al 1 con la derecha vacía va al 0), el aviso 1862 de Fenrir y, sin la
traba, el `24`. Si no va, la marca 3 y, con el clic y sin la traba, `0x5be150`.

**El paso del ítem del cursor** (`0x5d8d50`, desde `0x5ee1de` con `0x78abaac` en 0). La caja del caos y la lista
del asedio en el modo 3 cortan al principio; con la traba, nada. La cadena de `0x78abb14` (1 a 6) y `0x78aba60` en 2
son de las ventanas de NPC; con los dos en 0 va a `0x5deac5` con `esi` en 6 (`0x5da592`). Sin ítem (`0x7879fbc` ≤
0), nada. Si no, `0x5ca250(x + 15, y + 200, 0x78a9bb0, 8, 8, 0)`: la casilla bajo el mouse es `_ftol((mouse −
esquina) · 0,05f − tamaño · 0,5f + 0,5f)` en la x87, que trunca hacia el 0 (Q-C476); con una negativa no hace nada.
`0x5c9b40` dice si entra: todas las casillas de su huella dentro de la grilla vacías o, en la casilla de arriba a la
izquierda (fila · 8 + columna, Q-C474), el mismo tipo de los que se apilan (`0x1c00` a `0x1c08`, `0x807` y `0x80f`
con el mismo `+4`, `0x1c1d` y `0x1a20` a `0x1a22`). Las casillas de la huella dentro de la grilla llevan la marca 2
si entra, 4 si es una joya (Bless, Soul, Life o Creation) y 3 si no. Con el clic: si no entra, nada (una joya sobre
otro ítem es de la CL9.3, `0x5d82a8`); con el inventario cerrado, nada más; si entra, el destino y, a otro lugar o
al mismo con algo que se apila, el `24` sin la traba, o si no `0x5be150`. Después, las grillas de las otras ventanas
(`IsOpen(0x20)` con su `rand()`; el comercio `0x5deb1c`, el almacén `0x5deb57`, la tienda personal `0x5dec19` y la
caja del entrenador `0x44aca9` cortan). Sin el clic, sale; si no, lo consume (`0x77f3b10` en 0 y `0x6c8dcc` en 6) y
sale si la grilla lo tomó. La tienda de NPC (`IsOpen(0x10000)`, 1 `rand()`) vende (`0x5decce`, CL9.3). Si no, el
piso (`0x5df449`): lo que no se tira (las mascotas, las joyas, las alas, Dark Horse, Dark Raven, la Cape of Lord,
`+0x1c % 4` en 1 o 2, ocho tipos sueltos, más de nivel 4 debajo de las alas y `+0x1b & 0x3f`) vuelve con el aviso
269 y `0x5be150`; las cajas `0x1a20` a `0x1a25`, con el 1915; los paquetes de Bless y de Soul (`0x402b30` da 2 y 3,
con `JGE`) y el Jewel of Guardian van a `0x5e06b6`: con el inventario abierto y el mouse a su derecha, solo
`0x5be150`, y si no el aviso 269 antes. Tirar lo demás (`0x5df58b`, su envío) es de la CL9.3.

**MoveHero** (`0x59bd3e`). Con el cursor vacío, la mano derecha vacía y un arma de los grupos 0 a 2 o del 5 en la
izquierda, abre el registro propio (3.848 al cerrarlo), pasa el ítem del lugar 1 al cursor (sus 0x48 bytes, con la
cuenta en su `+0x3c`) y deja el lugar en `0xffff` con `+4` en 0; `0x512a30`; la grilla del inventario, el lugar 1 y
el destino 0; y, sin la traba, el `24` del 1 al 0. Sigue con el clic (`0x59cd23`).

**Las flechas del elfo** (`0x561840` → `0x5579e0`, CL8 2026-11-13, *estático*). `0x5bd750` las llama al sacar el
lugar 0 o el 1, después de cerrar su acceso al registro propio. `0x5579e0` abre el registro, lee la clase y, en el
elfo, las dos manos, y lo cierra (3.848 `rand()` si queda sin accesos); con otra clase devuelve −1. Busca las
flechas (`0x80f`) con un arco en la izquierda (`0x800` a `0x806`, `0x811`, `0x814` y `0x815`) y si no los virotes
(`0x807`) con una ballesta en la derecha (`0x808` a `0x80e` y `0x810` a `0x9ff`); sin ninguno de los dos, el tipo es
lo que dejó en `[ebp-0xc]` la búsqueda del registro en la tabla de protección (Q-C772). Recorre la grilla del
inventario de la columna 7 a la 0 y, en cada una, de la fila 7 a la 0, y da la primera casilla de ese tipo con
`+0x3c` mayor que 0. Con −1, `0x561840` avisa con el 251 y el tipo 2 (Q-C477). Si no, abre el registro, mira si es
un elfo con el cursor vacío y lo cierra (en línea, 3.848 si queda sin accesos), y después lee las manos (Q-C773): con
un arco en la izquierda y la derecha vacía, las flechas van a la derecha; con una ballesta de `0x808` a `0x80e` en la
derecha, o de `0x810` a `0x9ff` con la izquierda vacía, a la izquierda (Q-C774). Para eso copia la casilla al cursor,
deja `0x78a6758` en la casilla + 12, la saca con `0x5bd750`, pone el destino en `0x77f3cc8`, llama a `0x449d80`
(que con la traba puesta no hace nada) y avisa con el 250 y el tipo 1. La grilla del cursor (`0x78ab9b8`) no la toca.

**Lo que porta MuRet**: la ventana entera; los ítems 3D, el del cursor y los de las ranuras, dibujados en el pase de
la UI con sus `rand()` (`Scenes/UiItemDrawing.h`, `test_ui_item_drawing.cpp` y `test_game_inventory_hud.cpp`; el
`0x1a18` corta en `0x60c3b6`); las cifras; el mundo angosto en `GameScene` (`0x63733e` y `0x63738a`); las texturas
`0x104` a `0x120` (sin `0x106`), `0x652`, `0x657` y `0x658` en `HudTextureSet`; el paso en `Scenes/GameUi.cpp` (`test_game_inventory_step.cpp`) con
los botones de reparar y cerrar, el equipo y la grilla bajo el mouse y el paso del ítem del cursor; y levantar,
soltar, equipar, el piso, MoveHero y las respuestas en `Scenes/InventoryMoves.cpp` (`test_cursor_item.cpp` y
`test_inventory_reply.cpp`), con Item.bmd, `0x512a30`, el cuervo y los mensajes que pide (`InventoryServices`). Las
otras ventanas cortan con su dirección (CL9.3, CL9.4 y CL9.5), y también la tienda personal (Open de `0x800`, CL9.4), reparar, usar ítems y tirarlos (CL9.3): P-C71.

### Los tooltips

Hito 5 de la CL8. `0x5f7bc0` corre en cada frame del juego desde `0x63769b`, después de las ventanas; `0x40eca0` (el
cartel del set) va aparte, en `0x6376c6`. Los dos llenan los mismos tres arreglos de la .bss y dibujan la caja
`0x5af090`.

**Los renglones.** Treinta de 100 bytes en `0x78793c8` (el texto), sus colores en `0x787a560` y sus negritas en
`0x78a9230` (`Scenes::TooltipLines`). Cada cuerpo limpia los primeros 20 colores y los treinta textos, pero nadie
limpia las negritas: un renglón sin negrita escrita queda con la del cuerpo anterior (Q-C596). Los textos con formato son
textos de Text.bmd pasados como formato a `sprintf` (`0x6a5904`, con la tabla de estados de `0x6bdb14`): un `%` del
texto se interpreta (Q-C590), y uno de más de 100 bytes pisaría el renglón siguiente (Q-C591).
`UI::FormatLikeSprintf` reproduce la máquina del CRT para las marcas que usan los textos (`d i u o x X c s %`,
banderas, ancho, precisión y `h`). Una marca rota escribe solo el carácter que la rompió, una marca al final del
texto no escribe nada, y una marca sin su argumento queda escrita.

**La caja** (`0x5af090(x, y, cuántos, ancho, modo, marco)`, `Scenes::DrawTheTooltipBox`). Mide hasta el primer
renglón vacío, con la fuente de cada uno en el DC (la negrita es `0x5763558`): el más ancho y el alto `cy` del último.
El alto es `(n'\n' · cy · 0,5 + n · cy) / (ry · 0,9090909)`: un renglón que empieza con `\n` cuenta medio. El ancho
es el más ancho / rx (o `2 · ancho / rx` si lo pide quien llama) más 4; la x, `ftol(x − ancho / 2)`, con 0 de mínimo
y `ftol(W / rx − ancho − 1)` si se pasa del borde derecho. Con la mezcla normal y el marco pedido, cuatro bordes
negros de 1 con `0x621aa0` y el fondo negro con alfa 0,8 (`0x3f4ccccd`), y vuelve a la textura. Cada renglón (salvo
`\n` y `" "`, que solo bajan) va en `(x + 1, ftol(y))` con `0x465950(…, (ancho − 2) · rx, 0, modo, 0, 0x280)`, y la
`y` suma 1,1 veces lo que devuelve `0x467060` (`cy / ry`, la mitad con `\n`). El color, del switch de `0x5af368`
(la tabla de `0x5af4f4`): `0x6c8e5c = −1` y un `glColor3f` por índice (0, 5, 7, 8 y 9 blanco; 1 `(0,5, 0,7, 1)`;
2 `(1, 0,2, 0,1)`; 3 `(1, 0,8, 0,1)`; 4 `(0,1, 1, 0,5)`; 6 `(1, 0,1, 1)`; 10 gris 0,4; más de 10, ninguno) y el
fondo `0x6c8e64` (5 `0xff0000a0`, 7 `0xffa00000`, 8 `0xff0066a0`, 9 `0xffc83c3c` con el texto `0xff00ff00`, los
demás 0). Los dos modos de `0x467060` arman una caja de columnas desde `_ftol((x + 1) · rx)` con el fondo en toda
ella: el 2 (`0x46711b..0x467132`) la hace de cx + 2 · ((caja − cx) / 2) columnas y pone el texto en la (caja − cx) / 2,
como el título del inventario (Q-C710); el 3 (`0x467134..0x467144`) la hace de las `(ancho − 2) · rx` justas y pone el
texto en la columna caja − cx, contra el borde derecho (uno más ancho que la caja empieza antes de ella y `TextOutA`
corta la izquierda). MuRet: `Render::Text::CenterInBox` e `ITextDrawer::DrawColumns`, con la x de 640 × 480. Termina en
blanco (`glColor4f(1, 1, 1, 1)`) con `0x620980`. Solo los modos 2 (centrado) y 3 (contra el borde) los usa alguien;
los otros cortan en `0x5af471`. La usan los cuerpos, la ventana y el cartel del set, la ayuda de los íconos de
estado y las dos ayudas de F1 (`0x5b0550`, § HUD y ventanas, "Las ayudas de F1").

**El orden de `0x5f7bc0`** (`Scenes::DrawTheTooltips`):

1. `0x620980`, y `0x78abb0d` (el aviso de durabilidad) en 0.
2. El modo de reparar `0x78abb0c`, si no es 0, vuelve a 1.
3. Con un ítem bajo el mouse (`0x78abaa8`), abre el registro propio y llama a `0x5b8150` (reparar), si el mouse
   está sobre el inventario con el modo de reparar, o a `0x5b2aa0`. Los dos van en la esquina que dejó el paso
   (`0x78a9bac`, `0x78a9ba8`) y con la grilla de `0x78aba27`.
4. Con una skill de la barra bajo el mouse (`0x6c9320` ≠ −1), `0x5b90b0`.
5. Con el inventario abierto, la ventana del set.

La rama de `0x78abb14` (el ítem del cursor en `(0x109, 0x78)`) solo la prenden las ventanas de la CL9.3 y la CL9.4.
Un frame proyectado de MuRet no escribe nada del mundo: ni el modo de reparar, ni `+0x9304`, ni `+0x9305`.

**La ventana del set** (`0x40e850`, un thiscall sobre `[0x6eed3c]`). La prende `0x40e6c0`, el mouse sobre el
cartel del inventario en el paso (Q-C472), con `+0x9304` en 1. Escribe tres `\n` y el título `"%s %s"` con el nombre
del conjunto y el texto 1089, en dorado y negrita, uno por clase de pieza. Con `+0x9307`, el título de la otra clase
va a `+0x9307` renglones del final. Siguen las opciones de la lista de `0x40da20`: cada una con su texto de
`0x40d530` y su valor (`& 0xff`), en azul, o en rojo si la opción es `0xa5` o más sin la marca de la clase. Una
entrada de 16 o más, o más de 30 renglones, son el error. La caja va en `(x + 0x5f, y + 0x28)` del inventario, con
0x78 de ancho, el modo 2 y el fondo `0x64`, y `+0x9304` vuelve a 0.

**El cartel del set** (`0x40eca0`). Con `+0x9305` (el índice + 1 que dejó `0x40ebd0`, desde el chat, al escribir
`/` y el nombre de un conjunto), limpia los renglones y lee el registro de ItemSetOption.bmd. Si el conjunto no se
usa (`+0x3c` = `0xff`), apaga `+0x9305` y sale. Si no, escribe:

- el título `"%s %s %s"` (el nombre, el 1089 y el 159), en dorado;
- trece lugares: los seis pares de `+0x20` con sus valores de `+0x2c`, los dos extras (`+0x32 + j` y `+0x34 + j`)
  y las cinco del conjunto completo (`+0x35 + j` y `+0x3a + j`);
- dos `\n`, y la caja en `(0, 0)`.

Después del sexto par, la segunda opción queda la del sexto (Q-C592). `0x40d530` con una opción de más de `0x23`
no escribe el renglón (Q-C593). Los textos de las opciones (`0x40d530`, `Scenes::WriteTheSetOption`) son:

| opciones | texto |
|---|---|
| 0 a 6 | 950 + opción |
| 7 | 632 |
| 8 a `0x14` | 949 + opción |
| `0x15`, `0x16` y `0x17` | 970, 984 y 983 |
| `0x18` a `0x23` | 947 + opción |

**La ayuda de los íconos de estado** (`0x5a6600(x + 10, 0x1d, ícono)`, desde `0x42a735`, en el HUD). Con el mouse
sobre un ícono de estado, abre el registro propio y escribe los renglones:

- 0 y 2: `\n`;
- 1: el nombre de la entrada del ícono en Skill.bmd (`0x551c70`, con el acceso de la entrada: 60 `rand()` al
  cerrarla en 0), en azul y negrita;
- 3: el texto del caso del ícono (la tabla de `0x5a69fc`: 1466, 1468, 1469, 1470, 1467 y 1471 para `0x40`, `0x43`,
  `0x45`, `0x46`, `0x4b` y `0xcb`), en blanco. El `0xcc` escribe tres, del 3 al 5 (1638, 1639 y 1472).

Termina con un `\n`, cierra el registro y dibuja la caja con el modo 2. No limpia nada (Q-C597).

#### El cuerpo del ítem

`0x5b2aa0(x, y, ítem, grilla)` (`Scenes::DrawTheItemTooltip`, `Scenes/ItemTooltip*.cpp`). Lleva la cuenta de lo
que escribe en dos globales: N (`0x78abb30`), el renglón que sigue, y M (`0x78abb34`), cuántos renglones cuentan
medio alto (`TooltipLines::next` y `halves`). Cada renglón es un `sprintf` en `0x78793c8 + N · 100`, casi siempre
seguido de su color, su negrita en 0 y `N++`; un `\n` sube N y M. Nada mira que N quede debajo de 30: un renglón
más pisa lo que sigue a los textos (el ítem del cursor, `0x7879f80`), y MuRet corta con la dirección del tramo
(`Scenes::TooltipWriter`, Q-C696).

**El principio** (`0x5b2aa0..0x5b2cad`). Con el tipo `0xffff` sale sin tocar nada. Si no, pone N y M en 0, limpia
los 20 colores y los 30 textos y llama a `0x422990`, que con `0x1a04` y `0x1a05` dibuja su propio tooltip (el de la
mascota, con el `A9`) y devuelve 1: el cuerpo sale. El registro de Item.bmd es `[0x7758550] + tipo · 0x54`, sin
mirar el rango. Escribe un `\n` y toma el nivel, `(D+4 >> 3) & 0xf`. El color del nombre:

| ítem | color |
| --- | --- |
| `0x1c0d`, `0x1c0e`, `0x180f`, `0x1c1f`, `0x1c10`, `0x1c16` y los paquetes (`0x402b30` ≥ 2: `0x181e` y `0x181f`) | 3 |
| `0xa0a`, `0x13`, `0x812` y `0x40d` | 6 |
| `0x1c11`, `0x1c12`, `0x1c13`, `0x1a10` y `0x1a11` | 3 |
| `D+0x1c & 3` en 1 o 2 (una pieza de conjunto) | 9 |
| con opciones y alguna excelente (`D+0x1b & 0x3f`) | 4 |
| nivel 7 o más | 3 |
| los demás | 1 con opciones, 0 sin |

Después, `0x1803..0x1806` y `0x1a1e` vuelven a 3 con nivel 7 o más, o a 1 o 0 según las opciones.

**El precio** (`0x5b2cad..0x5b3716`). Salvo `0x1c0c`, `0x1c17..0x1c1a` y `0x1a14` con nivel 1 o 2, pregunta por la
tienda de NPC (`0x659290(0x10000)`, un byte protegido: 1 `rand()`). Abierta, salvo con `0x1c0b` y con `0x1c14` de
nivel 1 a 5, escribe el precio de `0x54d0f0` y `0x5b1200`: es de la CL9.3 y MuRet corta en `0x5b2d23`, aunque
el cálculo de `0x54d0f0` ya está portado (§ El precio de un ítem). Después
pregunta por las tiendas personales (`0x800` y `0x20000`, sin `rand()`), pero el precio solo sale con `0x78abaa4`
en 1, que pone `0x5c4110` con la grilla de la tienda personal (`0x78ab0b8`, CL9.4): en MuRet queda en 0 (Q-C695).

**Los tramos**, en orden:

1. El nombre (`0x5b371b..0x5b45f4`): por tipo y nivel (las tablas de las joyas, los pergaminos, los anillos y los
   ítems de misión), con el nombre del conjunto delante (`0x40cf30`, el nombre de ItemSetOption.bmd y un espacio) y
   el 620 delante con alguna excelente. Va en el color del nombre, en negrita, y sigue un `\n`.
2. Las líneas de cada tipo (`0x5b45f5..0x5b4f92`): el `0x1a13` (el daño, los stats y las opciones fijas de sus tres
   niveles; desde el 3 escribe la dirección del ítem como texto y MuRet corta), las misiones (`0x1c17..0x1c1a`), el
   `0x1c0c`, el `0x1a14` y el `0x1c13`.
3. El daño y los stats (`0x5b4f93..0x5b58e9`): el daño (con el byte `+0x21` de la skill de los pergaminos
   `0x1e00..0x1fff`, por su acceso), la defensa, la resistencia, la tasa de defensa, las dos velocidades de
   Item.bmd, las pociones de asedio y la caja de la suerte.
4. Las notas (`0x5b58ea..0x5b6a3b`): los paquetes de joyas, el texto de cada joya, las alas y los ítems de evento.
5. Los requisitos (`0x5b6a3c..0x5b79bc`): la durabilidad (con la máxima de `0x5b1ba0`), el daño de las flechas, las
   resistencias de `+0x4a..+0x50` y los requisitos de nivel, fuerza, agilidad, vitalidad, energía y comando contra
   el registro propio (más sus sumas): uno que no llega va en rojo, con un renglón más (el 74 con lo que falta).
6. Las clases (`0x5af590`), salvo en `0x1a13` y `0x1a1d`.
7. Lo que suma cada tipo (`0x5b79df..0x5b7c37`): las botas y los cascos desde el nivel 5 (78 y 93), los bastones y
   `0x1f`, `0x17` y `0x15` (79 con `D+0x16`), las varas `0x408..0x40e` (1234), cada uno detrás de un `\n`, en azul
   y negrita; y un `\n` si hay opciones.
8. El stat del conjunto (`0x40dd80` con `0x40dca0`): con `(D+0x1c >> 2) & 3` = k mayor que 0 y Item.bmd`+0x44` de 1
   a 4, el `949 + +0x44` con 5 · k, en azul y sin negrita; los anillos y collares (`0x1a08`, `0x1a09`, `0x1a0c`,
   `0x1a0d`, `0x1a15..0x1a1b`) suman el 1165.
9. `0x551c70` con el octavo byte de opciones (`D+0x32`): el contador de las resistencias quedó en 7 en `[EBP+0x14]`.
   Lee el maná de esa skill, que nadie usa (Q-C691).
10. Las opciones (`0x5b7c62..0x5b7d9f`): cada una con `0x5b2320(tipo, renglón, opción, valor, maná)` en azul, y un
    renglón más con algunas: `0x54` (el 94 con el valor, en azul), `0x31` y `0x3e` (179 y 1201, en el color 5) y la
    `0x50` de `0x1f`, `0x15` y `0x17` (89, en azul). Después, un `\n`.
11. Las cajas (`0x5b7de1..0x5b800d`): 1917, 1918 y 1919 en dorado; la azul `0x1a25` escribe el 1928 con el maná de
    la skill `0x4c` (y sube M: Q-C692), el 1860 o el 1861 con 10 y el 579 si `D+0x1b` es 1 o 2, un `\n`, el 1920 y,
    sin `D+0x1b`, el 1929.
12. Las opciones del conjunto (`0x40eee0`).

**`0x5b2320`** escribe el texto de una opción, sin color ni negrita: con el maná de la skill de la opción
(`0x551c70`, 60 `rand()` si cierra en 0) las `0x12..0x18` (80 a 86), `0x31` (745), `0x36` (920), `0x38` (98), `0x3c`
(1210), `0x3e` (1189), `0x42` (1186) y `0x4c` (1928); con el valor `& 0xff` las `0x50..0x53` (88 a 91), `0x55` (92,
salvo en `0x1a0e..0x1a12`), `0x5e`, `0x60`, `0x61` (630, 632, 633), `0x64..0x68` (740 a 744), `0x8c..0x8f` (985,
986, 988, 987), `0x90` (954), `0x94` (577), `0x96` (959), `0xb0` y `0xb1` (1087 y 1088); y sin argumentos las
`0x54` (87), `0x56..0x5d` (622 a 629), `0x5f` (631), `0x62`, `0x63` (634, 635) y `0x6e` (746). Otra opción deja el
renglón como estaba (Q-C694).

**Las clases** (`0x5af590(registro)`). Junta las clases con su byte de Item.bmd`+0x45..+0x49` distinto de 0 (la
evolución que piden es el byte − 1) y pone en 0 los colores de N + 2 y N + 3. El color de lo que el héroe no cumple
es 2 si su clase usa el ítem y 5 si no. Sin clases, nada. El MG (`+0x1d4 & 7` = 3) escribe un `\n` y el 61 (lo
usa) o el 60 (no) con el texto 23, según su byte `+0x48` y su evolución. Las demás clases, con cinco, nada; si no,
un `\n` y: con una o dos, el 61 con el texto `15 + 5 · byte + clase` de cada una, en blanco si el héroe es de
alguna con la evolución que pide; con tres o cuatro, el 60 con el texto `20 + clase` de cada clase que no lo usa
(Q-C693).

**Las opciones del conjunto** (`0x40eee0(ítem, N)`, un thiscall sobre `[0x6eed3c]`). El conjunto es el byte
`D+0x1c & 3` − 1 del tipo en ItemSetType.bmd; con `& 3` en 0 lee el byte 3 del tipo anterior (Q-C690). Lo guarda
en `+0x9306`; con 0 o `0xff` sale. Con el conjunto sin usar (`+0x3c` en `0xff`) deja `+0x9306` en 0 y sale. Si no,
busca qué parte de la lista de `0x40da20` (`+0x934c`, tres bytes por opción) es de este conjunto: si su nombre es el
de la segunda clase (`+0x9328`), desde `+0x9348 − +0x9307` hasta `+0x9348`; si no, si uno de los dos nombres
empieza con el otro (`+0x9308`), las primeras `+0x9348 − +0x9307`; si no, ninguna. Escribe `\n`, el título
`"%s %s"` (1089 y 159) en dorado y dos `\n`, sin negritas ni medios, y los trece lugares del cartel (los seis
pares, los dos extras y los cinco del conjunto completo, con la segunda opción del sexto par repetida, Q-C592):
cada opción con `0x40d530`, en azul si es la que sigue de la lista y no se pasó de las de este conjunto, o en gris
(10). En el conjunto completo, cada lugar solo sigue en azul si la opción es la que sigue de la lista. Termina con dos
`\n` y devuelve N + 2.

**La caja.** Mide la primera letra del renglón 0 con la fuente que tenga el DC (cy) y calcula
`((N − M) · cy + M · cy / 2) / ry` con `_ftol`. La caja va a esa altura arriba de y; si no entra (queda negativa),
debajo: en `y + Item.bmd+0x25 · 20`. `0x5af090` con N renglones, el ancho propio, el modo 2 y el marco.

#### El cuerpo de reparar

`0x5b8150(x, y, ítem, grilla)` (`Scenes::DrawTheRepairTooltip`), con el modo de reparar y el mouse sobre el
inventario. Sale sin tocar nada con `0x1a00..0x1a05`, `0x807`, `0x80f`, desde `0x1c00`, `0x1807..0x1813`, `0x1a07`,
`0x1a0a`, `0x1a0b`, `0x1a0e..0x1a14`, `0x1a1d` y `0x1a20..0x1a25`. Si no, pone N y M en 0 y limpia los textos, pero
no los colores. El color del nombre es el del cuerpo del ítem sin el de los conjuntos (9), y la vuelta de
`0x1803..0x1806` alcanza a todo tipo desde `0x1a1e`. Escribe:

- un `\n`;
- el 238 con el precio: con la durabilidad debajo de la máxima (`0x5b1ba0`), pone el modo de reparar en 2 y el
  precio es el de `0x54d0f0(ítem, 2)` pasado por `0x5b13e0(precio, durabilidad, máxima, tipo, buffer)` (§ El precio
  de un ítem); si no, `"0"` y el modo en 1. En el color del nombre y en negrita;
- un `\n`, el nombre (`"%s"` o `"%s +%d"`, con el 620 delante si alguna opción es excelente, salvo en
  `0x1803..0x1806` y desde `0x1a1e`), en el color del nombre y en negrita, y otro `\n`;
- el 71 con la durabilidad y la máxima, en blanco, y un `\n`.

Los nombres del `0x180b` (`0x5b8453`, con el acceso de la skill 30 + nivel) y del `0x1a0a` (`0x5b8915`) no se
alcanzan: esos tipos salen al principio. La altura es en enteros sin signo, `((N − M) · cy + M · cy / 2) · 480 / H`
(`0x6d5d64`), y la caja va arriba de y o, si no entra, en `y + Item.bmd+0x25 · 20`, con el modo 2 y el marco.

#### El cuerpo de la skill

`0x5b90b0(x, y, lugar, skill de asedio)` (`Scenes::DrawTheSkillTooltip`, 5.760 bytes), cdecl. `0x5f7bc0` lo llama con
el lugar de la skill del héroe (`0x6c9320`) y 0. El asedio (`0x652c90`, CL9.5) lo llama con el `+0x371` del héroe:
`(0x37, 0x45, 0x59fb80(+0x371), +0x371)`.

Lee la clase (`+0x1d4 & 7`) antes de todo y llama a `0x421c10(x, y, lugar)`, **la ayuda de los comandos del cuervo**.
Con un lugar de `0x78` a `0x7b` y el Dark Lord, sin limpiar nada, escribe:

- en el renglón 0, el nombre del comando c (el texto 1219 + c, de formato) en azul y negrita;
- en el 1, el color 0;
- `\n` en el 1 y en el 2;
- en el 3, la ayuda (1223 + c) (Q-C720).

Dibuja la caja con 4 renglones arriba de y, a `(4 · cy) / 2 / ry` (Q-C721), y devuelve 1: el cuerpo sale sin abrir
nada ni sacar `rand()`.

Si no, abre el registro propio y, con la skill del byte `+0x6c + lugar`:

1. Los daños. `0x5513d0` da la magia. `0x5516e0` (thiscall que no lee el registro) da el de la skill: con s, el
   `+0x21` de su entrada, s y s + s / 2, más el mismo p de los conjuntos que la magia. `0x5b8b80` da el ataque, que se
   suma al de la skill. Los dos primeros abren la entrada de la skill (60 `rand()` al cerrarla en 0); el ataque abre
   el registro propio adentro del otro y no saca nada.
2. `0x551c70(skill, 1, nombre, &maná, &alcance, &stamina)` copia el nombre con `strcpy` a un buffer de 256 en la
   pila. Deja el word `+0x22`, el byte `+0x26` y el word `+0x24` extendidos a 32 bits; el segundo argumento no se
   usa.
3. El nombre es el 1200 (`wsprintfA("%s")`) con la `0x3c`, algo en la mano derecha del héroe (`+0x2b8` ≠ −1) y la
   opción `0x42` entre las `D+0x2a` del lugar 0 del registro (`+0xb97 + i`).
4. Cuenta en EBX, sin limpiar ni tocar `0x78abb30` ni `0x78abb34`: `\n`, el nombre (`"%s"`) en azul y negrita, y
   `\n`.
5. Los stats, con lo de los ítems y en 16 bits:
   - fuerza: `+0x18 + +0x2a`;
   - agilidad: `+0x1a + +0x2c`;
   - vitalidad: `+0x1c + +0x2e`;
   - energía: `+0x1e + +0x30`;
   - comando: `+0x20 + +0x36`.
6. El daño según la clase:
   - el Wizard con la `0x10` escribe la absorción, por `_ftol` (Q-C724):
     - el 578 con `agilidad · 0,02f + energía · 0,005f + 10`;
     - el 880 con `+0x28 · 0,02f`;
     - el 881 con `energía · 0,025f + 60`;
   - el Wizard con otra skill escribe el 170 con la magia, salvo que el cuarto argumento sea `0x43`, `0x44` o `0x45`
     (Q-C722);
   - las clases 1 a 4 dependen de la skill:
     - con la `0x3e`, el 1237 en el color 5;
     - con una de la tabla de `0x5ba73c` (`6`, `0xf`, `0x10`, `0x12`, `0x1a..0x1c`, `0x1e..0x24`, `0x30`, `0x35`,
       `0x3f`, `0x40`, `0x43..0x48`, `0x4b` y `0x4c`), nada;
     - con las demás, el 879 con el daño de la skill más el del ataque;
   - las clases 5 a 7 no escriben nada.
7. Con la `0x4c`, v es el `+0x21` de su entrada (`0x552470(0x4c, &v)`, con el acceso). Escribe el 879 con d y
   d + 30, y d depende de la clase:
   - 1 y 3: `f / 3 + a / 5 + vi / 5 + e / 7 + v`;
   - 0: `f / 5 + a / 5 + vi / 7 + e / 3 + v`;
   - 2: `f / 5 + a / 3 + vi / 7 + e / 5 + v`;
   - 4: el del 0 más `c / 3`;
   - 5 a 7: el daño de la skill más el del ataque.
8. La elfa:
   - con la `0x1a`, el 171 con `e / 5 + 5`;
   - con la `0x1b`, el 172 con `(e >> 3) + 2`;
   - con la `0x1c`, el 173 con `e / 7 + 3`.
9. Los costos:
   - el 174 con el alcance, si no es 0;
   - el 175 con el maná;
   - el 360 con la stamina, si es más de 0.
10. El Dark Knight, en el color 5:
    - el 96 con la `0x2f`;
    - el 99 (el combo) con el `+0x390` del héroe en 1, nivel 220 o más y una skill de `0x13..0x17` o `0x29..0x2b`.
      `+0x390` lo pone en 0 `0x511290` y en 1 las misiones (`0x418e20`, `0x418ee0`).
11. `0x5519f0(skill)` (thiscall, con el acceso) da el byte `+0x30`. Si no es `0xff`, escribe el 1080 + byte, de
    formato y sin argumentos, en azul.
12. El acceso de la entrada, en línea:
    - con `+0x31` en 2, abre adentro la entrada del `+0x32`, escribe el 1480 con su nombre (el `%s` del principio de
      la entrada) y el 1481 con su `+0x21`, los dos en el color 5, y la cierra;
    - con `+0x31` en 1, escribe el 1482 y el 1483 con `+0x33`, en el color 5.

    Después cierra la entrada.
13. La cola:
    - el Dark Lord con la `0x3f` y la party (`0x78aba48`) en 0 o menos: el 1185 en el color 5;
    - con la `0x4c`: el 1926 y el 1927 en el color 5 (Q-C723);
    - un `\n` sin color ni negrita.

Cada renglón escrito, salvo los `\n`, lleva su color (0 si no dice otro) y la negrita en 0. Al final:

1. Cierra el registro propio: 3.848 `rand()` si su cuenta vuelve a 0.
2. Mide cy con `GetTextExtentPointA` sobre el primer carácter del renglón 0.
3. Dibuja la caja arriba de y, a `((N − 3) · cy + (3 · cy) / 2) / ry` por `_ftol`, con N renglones, el modo 2 y el
   marco. No mira si entra arriba.

**Los `rand()`**, con las entradas en 0:

- una skill saca 4.148: cinco accesos de Skill.bmd y el registro;
- la `0x4c` saca 60 más;
- `+0x31` en 2 con otra skill en `+0x32` saca otros 60.

**Lo que porta MuRet**: el orden de `0x5f7bc0`, la ayuda de los íconos de estado (`Scenes/GameHud.cpp`), la caja, la
ventana y el cartel del set, el cuerpo del ítem, el de reparar (con el precio de un ítem gastado, § El precio de un
ítem), el sprintf del CRT (`UI/SprintfFormat`) y `0x40ebd0` en la cola de los comandos del chat
(Q-C594). Los tests son `test_game_tooltips.cpp`, `test_item_tooltips.cpp`, `test_sprintf_format.cpp` y
`test_chat_commands.cpp`. Del cuerpo del ítem cortan con su dirección: `0x1a04` y `0x1a05` (`0x4229d4`, P-C126), el
precio de la tienda de NPC (`0x5b2d23`), el nombre del `0x1c0c` de nivel 2 (la tabla de `0x6c9350`) y del `0x1a0a` (la
tabla de monstruos de `0x7681eb8`), las líneas del `0x1a1d` (`0x5b63b7`) y del `0x1c1c` (`0x437c90`), el `0x1a13`
desde el nivel 3, un nombre de más de 64 bytes y un renglón fuera de los 30 (P-C125). El cuerpo de la skill y la ayuda
de los comandos están en `Scenes/SkillTooltip.cpp`, con `0x5516e0` en `Scenes/OwnDamage` (`test_skill_tooltip.cpp`).
Cortan con su dirección lo que leen fuera de lo que MuRet tiene (P-C135), y lo que depende de otras olas o de la VM
está en P-C136. La ayuda del ítem (el modo 3, que abre el chat) está en § HUD y ventanas, "La ayuda del ítem".

#### El precio de un ítem

`0x54d0f0(ítem, modo)` (`Scenes::ItemPrice`, 2.933 bytes) calcula en 64 bits y devuelve EDX:EAX; los que llaman
leen el dword bajo. Lo llaman con el modo 0 desde la grilla de la tienda de NPC (`0x5b2d2e`), con el 1 desde el
inventario con la tienda abierta (`0x5b2e46`) y con el 2 para reparar (`0x5b838a`). Las cuentas enteras son
`__allmul` (`0x6a1da0`, módulo 2^64) y `__alldiv` (`0x6a0150`, con signo, hacia el 0); las de la x87, en PC53 con
las excepciones enmascaradas (P-C34).

- El `0xffff` vale 0. El tipo se extiende con signo y el registro es `[0x7758550] + tipo · 0x54`, sin mirar el rango
  (`0x54d10e`).
- Con el dword `+0x40` (el precio fijo) distinto de 0, sale con él: por 3 en los modos distintos de 0 y redondeado.
- Si no, el nivel es `(D+4 >> 3) & 0xf` y la base b es el word `+0x20` más 3 por nivel, más 25 con alguna opción de
  `0x56` a `0x63`. Cada tipo tiene su cuenta:
  - `0x807` y `0x80f`: la durabilidad por 100, 1.400 o 2.200 (70, 1.200 o 2.000 las flechas) según el nivel, sobre
    `+0x2d`; 0 sin `+0x2d`. Desde el nivel 3 multiplica por la dirección del ítem (Q-C735).
  - `0x181e` y `0x181f` (`0x402d60`): (5 · nivel + 5) · 18.000.000 y (nivel + 1) · 60.000.000.
  - `0x1a03`: 960.000, más 300.000 por cada opción `0x61`, `0x67` o `0x68`.
  - Los de precio propio (`0x54d2b1..0x54d63a`): once constantes (`0x1c0d` 9.000.000, `0x1c0e` 6.000.000, `0x180f`
    810.000, `0x1c10` 45.000.000, `0x1c16` 36.000.000, `0x1a0f` 33.000.000, `0x1c1c` 600.000, `0x1a1d` 5.000,
    `0x1c14` 900, `0x1c0a` 750 y `0x1c1f` 60.000.000); `0x1a0e`, `0x1a07` y `0x1a0b` en los niveles 0 y 1 (con otro,
    0); el `0x1c09` de nivel 1 (1.000); las tablas por nivel de `0x1a10` y `0x1a11` (del 1 al 7) y de `0x1a12`, por 3,
    y de `0x1c11`, `0x1c12` y `0x1c13` (hasta el nivel 4; desde ahí, el del 4); y los que van por la durabilidad
    (`0x1c1d`, `0x1c15` y `0x1c07`).
  - Con `+0x3c` = v distinto de 0: 10 · v² / 12. De `0x1c00` a `0x1c08`, por 2^nivel (`0x6a1b80`) y por la
    durabilidad, por 3 en los modos distintos de 0, y vuelve en `0x54d6ee` con el dword bajo / 10 · 10, sin tope, sin
    descuento y sin el redondeo del final.
  - `0x1a14` y `0x1a1f`: de los niveles 0 y 1, después de `+0x3c`.
  - Los ayudantes (grupo 13), los pergaminos (grupo 15) y las alas después de `0x1806`: b³ + 100 en 32 bits, por el
    valor más 1 de cada opción `0x55`.
  - Lo demás (las armas, las armaduras y las alas hasta `0x1806`): b suma la tabla de `0x54dc68` del nivel 5 al 13, y
    el cubo (b + 40) · b², en 32 bits, va por 11 más 40.000.000 en las alas, o / 8 + 100. Las armas de una mano
    (grupos 0 a 6 con `+0x1e` en 0) quedan al 80 %. Cada opción sigue la tabla de `0x54dca4`: `0x12..0x18` y `0x38`
    suman el dword bajo de P · 1,5f (Q-C736); `0x50`, `0x51`, `0x53` y `0x55` suman P por 0,6, 1,4, 2,8 o 5,6 (los
    doubles de `0x6ba408..0x6ba420`, un poco por encima) con el valor 4, 8, 12 o 16, y la `0x52` con 5, 10, 15 o 20 (en
    las alas, el valor de la `0x55` va por 4); `0x54` y `0x64..0x68` suman P · 0,25; `0x56..0x63` y `0x6e` lo doblan.
    La rama de la capa `0x1a1e` en `0x54d7b1` no se alcanza (Q-C737).
- Los que salen por `0x54da1d` tienen tope de 3.000.000.000, con signo: uno negativo queda como está. Los modos
  distintos de 0 dividen por 3, salvo `0x1a00` y `0x1a01`. Después, `0x1a20..0x1a25` pisan el precio (la durabilidad
  por 50, 100, 1.000 y 10.000; 30.000 y 50.000) y `0x1a04` y `0x1a05` lo cambian por el nivel de su mascota
  (`0x422930`, los words de `0x723968 + 0x14 · tipo`) por 2.000.000 el caballo y 1.000.000 el cuervo, en 32 bits.
- Solo en el modo 1, salvo `0x807`, `0x80f`, desde `0x1c00`, `0x1807..0x1813`, `0x1a07`, `0x1a0a`, `0x1a0b`,
  `0x1a0e..0x1a14`, `0x1a1d`, `0x1c07`, `0x1c15` y `0x1a20..0x1a25`, resta el dword bajo, sin signo, de
  `_ftol((1 − durabilidad / máxima) · P · 0,6f)`, con la máxima de `0x5b1ba0`; el `0x1a1f` resta 0. Con la máxima en
  0, la FIDIV da NaN (0 / 0, o −infinito por un precio en 0), que resta 0, o un infinito. Una durabilidad arriba de la
  máxima resta 2^32 − x y deja el precio negativo (Q-C736).
- El redondeo: uno negativo sale como su dword bajo; desde 1.000, a 100; desde 100, a 10.

`0x5b13e0(precio, durabilidad, máxima, tipo, buffer)` (`Scenes::RepairPriceOf`) le pone al precio un tope de
400.000.000 (con signo) y cobra 3 · p^(3/4) (dos FSQRT) · (1 − durabilidad / máxima, en float) + 1, por 1,4f con la
durabilidad en 0, o 0 si no falta nada. Le suma eso por el byte bajo de `[0x78abb10]` y por 0,05f, lo pasa por
`_ftol` (EAX) y lo redondea a 100 desde 1.000 y a 10 desde 100. En el buffer escribe el número con los miles
separados por comas (`"%d,%03d"` y sus hermanos); el formato de los mil millones no se alcanza, porque el máximo es
163.341.600. El tipo no lo lee.

**Lo que porta MuRet**: el orden de `0x5f7bc0`, la ayuda de los íconos de estado (`Scenes/GameHud.cpp`), la caja, la
ventana y el cartel del set, el cuerpo del ítem, el de reparar con su precio (`0x54d0f0` y `0x5b13e0` en
`Scenes/ItemPrice.cpp`), el sprintf del CRT (`UI/SprintfFormat`) y `0x40ebd0` en la cola de los comandos del chat
(Q-C594). Los tests son `test_game_tooltips.cpp`, `test_item_tooltips.cpp`, `test_item_price.cpp` (con Item.bmd real
para los de oro), `test_sprintf_format.cpp` y `test_chat_commands.cpp`. Del precio cortan con su dirección las lecturas
que MuRet no tiene, como en P-C140. Del cuerpo del ítem cortan con su dirección: `0x1a04` y `0x1a05` (`0x4229d4`,
P-C126), el precio de la tienda de NPC (`0x5b2d23`, con el cálculo listo), el nombre del `0x1c0c` de nivel 2 (la tabla
de `0x6c9350`) y del `0x1a0a` (la tabla de monstruos de `0x7681eb8`), las líneas del `0x1a1d` (`0x5b63b7`) y del
`0x1c1c` (`0x437c90`), el `0x1a13` desde el nivel 3, un nombre de más de 64 bytes y un renglón fuera de los 30 (P-C125).
El de la skill (`0x5b90b0`) corta con su dirección (P-C101). La ayuda del ítem (el modo 3) está en § HUD y
ventanas, "La ayuda del ítem".

### La ventana del personaje

Hito 5 de la CL8. El panel es `0x2000000` (`[0x7d53709]`), con la C o el botón del HUD.

**Abrirla** (`0x65c94b..0x65ca72`, en `0x6599a0`):

1. Cierra todo menos el inventario y los amigos (`0x657ab0`); si cerró, el personaje en 1.
2. Pone en 0 `[0x6eed3c] + 0x9304..0x9347`: los cuatro bytes de los tooltips y los dos nombres.
3. Con el registro propio abierto alrededor, `0x550cf0` busca un lugar del equipo con la durabilidad (`+0x1a`) en
   0, y con uno llama a `0x550d10`. Un lugar vacío tiene la durabilidad en 0, así que 0x550d10 corre en casi todas
   las aperturas (Q-C595). Sus 33 cierres de `0x40e1b0` quedan dentro del acceso y no sacan nada; la cola saca sus 3
   `rand()`, y el cierre de afuera, 3.848.
4. Los sonidos 0x19 y 0x1c.

Con las consultas de `CanOpen` y de la máscara, la C saca 4 + 3 + 3.848 `rand()`. MuRet lo hace en
`UI::PanelRegistry::Open` con el gancho `characterOpened`, que arma `Scenes::GamePanels` con las tablas que presta el
paso de la UI (`GamePanelTables`). Sin ItemSetType.bmd e ItemSetOption.bmd, `0x550d10` corta en `0x40e1b0`.

**El paso** (`0x5e4510`, desde `0x5ee140`, después de la party). Con el personaje abierto:

1. El mouse en `(x, y)` + `0xbe × 0x1b1` es de la UI (`0x775857c`), con la esquina que deja el dibujo
   (`0x78aba14`, `0x78aba18`).
2. `0x6c9330` es la cantidad de botones: 5 con el Dark Lord, si no 4.
3. Abre el registro propio. Con puntos para repartir (`[0x7681eb0] + 0x68`), el clic en el botón `i`
   (`x + 0x7d`, `y + 0x73 + 0x3c · i`, 0x18 × 0x18) apaga el flanco, manda `C1 05 F3 06 i` con `send()` sin la
   cola (el XOR32 desde el byte 3, como lo arma el PacketWriter de la pila) y suena 0x19. Con el socket en −1 no manda
   nada. Cierra el registro, con 3.848 `rand()`.
4. El botón de cerrar (`x + 0x19`, `y + 0x18b`, 0x18 × 0x18) apaga el flanco, pone `0x77f3b10` en 0 con su tope
   `0x6c8dcc` en 6 y cierra el panel.

**La mascota** (`0x424130` → `0x424230`, al final de `0x5ee140`). Con el personaje cerrado, `0x723911` vuelve a 0.
Abierto, la primera vez manda `C1 06 A9 01 00 08` (el caballo oscuro en la casilla 8, Q-C16) con `send()` y deja
`0x723911` en 1: una vez por apertura, en el paso que sigue al que la abrió. El clic en el botón de la mascota
(`0x217..0x22f`, `0x18b..0x1a3`) apaga los dos flancos y abre o cierra el panel `0x400` (Open corta: CL9.2). Con
`0x723911` en 2 (la respuesta del A9, que MuRet no lee) la ventana de la mascota es de la CL9.2.

**El dibujo** (`0x5ee2e0(0x1c2, 0)`, 10.336 bytes, desde `0x5f7440`; leído entero en el ASM de `c32bbd5f`, con el
decompilado de guía). Escribe la esquina (`0x78aba14`, `0x78aba18`) y dibuja, en este orden:

1. Los fondos `0x104` (190 × 256, du 0,7421875) y `0x105` (190 × 177 en y + 256, dv 0,69140625), opacos y en
   blanco. Después, la mezcla alfa y la negrita.
2. Acceso 1: el nombre del registro (`sprintf("%s")` de sus 11 bytes) y la clase, `(%s)` con el texto
   20 + (`+0xb` >> 3) · 5 + (`+0xb` & 7). El nombre va con el color de `0x555a70` por el `+0x375` del héroe (de
   0 a 5, de `0xfff0ff96` a `0xff1e50ff`; si no, `0xff0000ff`) sobre `0xff141414`, centrado en 120 en
   (x + 0x23, y + 0xc).
3. El servidor (`0x5ee629..0x5ee69c`): el texto de `0x447c70` (el 460 sin PvP, el 461 con PvP, como el título de la
   cuenta) con el nombre del grupo (`0x7d36bb0 + 0x21e · lugar`) y el número de `[0x6d5ec4]` y `[0x6d5ecc]`. La
   clase y el servidor se turnan en (x + 0x16, y + 0x16), centrados en 150, con a = (float)(sin(T · 0,001f) + 1) en
   la x87: la clase con el alfa 2 − a y el servidor con a. El alfa pasa de 1 y GL lo acota (Q-C677).
4. Los fondos `0xf5` del nivel y de los stats: uno más que los botones de `[0x6c9330]`, de 60 en 60 desde
   (x + 13, y + 56), de 75 × 21.
5. Acceso 2: el nivel (el texto 200), en `0xffe6e6e6` sin fondo, centrado en 82 en (x + 0xe, y + 0x3c). Acceso 3:
   la experiencia (201), en blanco sobre `0x80000000`, desde la izquierda en 145 en (x + 0x18, y + 0x50). Acceso 4:
   el renglón de los puntos agregados y quitados, con `wsprintfA` y el formato `'%s %d/%d | %s %d/%d'` de la
   `.rdata` (Q-C672) sobre los textos 1412 y 1903: antes del nivel 10, todo en 0; si no, `+0x44` y `+0x46` con signo
   y −`+0x48` y −`+0x4a`. En negrita y naranja, en 130 en y + 0x5f.
6. Acceso 5: la fuerza (202). El color de un stat es (−(agregado ≠ 0) & `0x97d685`) − `0x984021`: naranja
   (`0xffff9664`) con algo agregado, celeste (`0xff67bfdf`) sin nada; el valor, la suma de 16 bits del stat y lo
   agregado. Centrada en 70 en (x + 0xe, y + 0x78). El acceso 6 solo toma el puntero al lugar 0. Después,
   `0x5b8b80` (el ataque, con su acceso), y el acceso 7 (`0x5ef450..0x5ef55f`): el bonus de la tasa es `+0x4e`;
   con el bit `0x400` de `+0x78` del héroe le suma la agilidad, y con tres o más en la party lo pasa por
   `_ftol((party − 2) · 0,01f · agilidad + bonus)`. El bit `0x2000000` suma nivel / 3 + 45 al mínimo y al máximo.
   El 203 (mínimo, máximo y tasa) con el bonus en más de 0, si no el 204; en naranja si `0x5b8b80` lo marca, en
   blanco si no, desde la izquierda en 130 en y + 0x8c.
7. El set (`0x5ef62a..0x5efc5f`): los lugares 2 a 6, un acceso cada uno, hasta el primero vacío o con la
   durabilidad en 0 (la clase 3 puede no tener casco). Si pasan los cinco, el acceso 9 compara el tipo % 512 de los
   lugares 3 a 6 con el del casco, o en la clase 3 con el de la armadura, que tiene que ser `0x100f`, `0x1014`,
   `0x1017` o `0x1020`.
8. Acceso 10: la agilidad (205), en y + 0xb4. El acceso 11 queda abierto hasta el final del tramo: la defensa,
   `+0x62` (más nivel / 5 + 50 con el bit `0x2000000`), con el 206 o el 207 con set (y la defensa / 10) y el 208 o
   el 209 sin set, según la tasa `+0x60` sea distinta de 0 (sin signo), en y + 200; y la velocidad (64): con la
   clase **del registro** (`+0xb & 7`, Q-C674) en 0 la de la magia, si no la del ataque, en y + 0xd7.
9. Acceso 12, abierto hasta las letras: la vitalidad (210) en y + 0xf0, la vida (211) en y + 0x104, la energía (212) en
   y + 300 y el maná (213) en y + 0x140. Las clases 0 y 3 dibujan la magia (`0x5f04b1..0x5f0633`): `0x5513d0` con el
   acceso 13 adentro del 12 (no saca nada) y el de la entrada de su skill en Skill.bmd (60 `rand()`); 10 más al mínimo y
   al máximo con el bit `0x02` de `+0x3c`; con un báculo en la derecha (de `0xa00` a `0xbff`, `0x1f`, `0x17` o `0x15`),
   el 215 con el agregado (f − r · f) · máximo (el de antes de los 10), f el byte `+0x16` del ítem por 0,01f y r el
   factor de `0x54ee00` sobre el byte `+0x2e` de Item.bmd (Q-C671); si no, el 216; en y + 0x14f. El daño de la skill
   (582): el Dark Knight E / 10 + 200 y el Dark Lord E / 20 + 200, en y + 0x14f; la clase 3, 200 en y + 0x15e. Con
   puntos para repartir, el 217 en negrita, negro sobre `0xffc86e1e`, centrado en 80 en (x + 0x5f, y + 0x32), y los
   botones `0x120` (`0x121` con el botón abajo y el mouse encima) en (x + 125, y + 115 + 60 · i). Las letras (1907),
   centradas en 84 en (x + 0x5b, y + 0x41): `+0x44` · 100 / `+0x46` con signo y `+0x48` · 100 / 1 (Q-C670), cada una a
   70, 60, 50 o 40, y 100 hasta 10. La primera es un `idiv` por `+0x46`, que con 0 es una excepción en `0x5f08ac`
   (Q-C675).
10. El Dark Lord, con el acceso 14: el mando (1170), `+0x20` más `+0x36` con su color, centrado en 70 en y + 0x168.

Después, `0x5f7440` (`0x5f74a3..0x5f759a`) dibuja el botón de cerrar `0x118` (24 × 24 en (x + 25, y + 395)) y,
con el mouse encima, el texto 222 con `0x554340` en (x + 25, y + 395 − 13).

Los cierres que dejan el registro sin accesos son de 12 a 18 por frame (Q-C673): los accesos 1 a 7, el de
`0x5b8b80`, uno por cada lugar del set que mira (de 1 a 5), el 9 si llegan los cinco, el 10, el 11, el 12 y el 14
del Dark Lord. Cada uno saca 3.848 `rand()`, y la entrada de la skill de las clases 0 y 3, 60.

**Los textos con ancho** (`0x554180` → `0x467060`). El ancho es W · k / 640, sin signo. En el modo 1, la caja tiene
ese ancho desde x, con el fondo entero, y un texto más ancho se ve desde la columna cx − ancho (el final). En el modo
2, el corrimiento es (caja − cx) / 2 con la división de C, la caja tiene cx + 2 · eso columnas y el texto empieza
corrido; uno más ancho que la caja se ve por el centro (Q-C676). Un texto vacío mide como `"0"`
(`0x4670ba..0x4670d0`). MuRet lo hace con `TextCache::DrawColumns` (la primera columna, que puede ser negativa, y
las columnas de la caja).

**Lo que corta.** Sin el servidor elegido, en `0x5ee629` (P-C120); con `+0x46` en 0 y `+0x44` en más de 10, en
`0x5f08ac` (P-C121); con el nombre del registro sin su 0 en sus 11 bytes, en `0x5ee47b`, y con la skill del héroe
fuera de los 64 bytes de `+0x6c` que tiene MuRet, en `0x5f04cd` (P-C122). Sin Text.bmd, Item.bmd (la magia con
báculo, `0x5f05a2`) o Skill.bmd (`0x5513d0`), en la dirección que los lee. Un dibujo intermedio de MuRet hace la
magia sobre una copia de la lista de los sets, para no dejar sus marcas.

**Lo que porta MuRet**: abrir, el paso, los botones, el F3 06 (`Protocol/Character/LevelUpPoint`) y el A9 de la
mascota, en `Scenes/GameUi.cpp` y `Scenes/GamePanels.cpp`; el dibujo y el botón de cerrar, en
`Scenes/GameCharacterHud.cpp` (desde `DrawTheWindows` de `Scenes/GameHud.cpp`), con `0x5b8b80` y `0x5513d0` en
`Scenes/OwnDamage.cpp`, `0x40d8c0` en `Scenes/ItemSetOptionList.cpp` y los textos con ancho en
`Render/Text/TextCache.cpp`. Los tests son `test_game_character_window.cpp` (en Lorencia y en Devias),
`test_panel_registry.cpp`, `test_level_up_point.cpp`, `test_game_character_hud.cpp` (el Dark Knight en Lorencia y en
Devias, el Dark Lord, el Wizard y la clase 3, con los `rand()` contados y los sprites y los textos grabados),
`test_own_damage.cpp`, `test_item_set_option_list.cpp` y `test_text_cache.cpp`. Lo que queda para la VM, en P-C123.

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
  | `0x4b` | 1 (y su polvo, abajo) | 0,34 |
  | `0x4e` a `0x52` | 2 | 0,34 |
  | `0x54` | 5 | 1 |
  | `0x55` | 6 | 1 |
  | otra (la `0x4a` del login) | 0 (y `+0x8e` en 0) | 0,3 |

  El polvo del galope (`0x44416b..0x4444ef`, *estático*, ASM c32bbd5f), con la luz (1, 1, 1), los ángulos
  del caballo y el reloj del mundo:
  - **En el mapa 10** (Icarus, `0x44419f..0x4443d1`), sin mirar Kalima ni `+0x78` del jinete:
    `(rand() & 63) − 32` para la z del desplazamiento (120; 0; z), antes de mirar el frame (Q-C404); el
    hueso del buffer global de la pose (`0x68b181c`) por el frame del caballo (`+0x114`), en ventanas que
    se pisan y donde gana la primera: (1; 1,2) el 19, (1,1; 1,4) el 25, (1,3; 1,6) el 32 y (1,5; 1,8] el
    38 (`0x44429a` mira C0 y C3: la última incluye su tope); fuera de todas, nada más. Con el hueso, el
    punto de `0x4ec110(hueso, desplazamiento, salida, 0)` (sin la escala ni el origen del modelo, Q-C401);
    la onda `0x518cf0(0x6eb, …, 1)` corrida `(rand() & 15) − 8` en x y en y, que `0x51f4ad` crea con vida
    20 y escala `((rand() % 10) + 10) · 0,1f`, y que `0x52c5ca` hace crecer `(rand() % 5) · 0,1f` y temblar
    `(rand() & 7) − 4` en x y en y, con la luz `vida · 0,05f` (`40 − vida` pasados los 20) y la escala
    negativa en 0; lo dibuja solo el pase del suelo (`0x52dc20`, la fila `0x6eb` de abajo). Después,
    un `rand()` de la guarda de entrada (`0x444325`, `rand() % 5 + 5`, siempre positivo) y las `0x5f2`/0
    de a una, corridas `rand() % 50 − 25` en x y en y y `(rand() & 15) − 8 − 10` en z, mientras su cuenta
    no llega a un `rand() % 5 + 5` que se vuelve a sacar en cada vuelta (Q-C400): de 5 a 9 o más.
  - **En los demás**, `rand() % 2`; con 0, fuera de Kalima (24 a 29 y 36, `0x4443f6..0x444408`) y sin
    `+0x78 & 0x4000` del jinete, tres `rand()` para el lugar (`(rand() & 63) − 32` en x y en y, y
    `(rand() & 31) − 16` en z, alrededor del caballo), y en el 2 la `0x5f2`/0 (`0x4444a6`) y en los otros
    la `0x5f3`/0 (`0x4444dd`; el 10 de `0x4444bd` no llega). La `0x5f3` (`Effect\smoke02.tga`, que
    `0x61d768` sube con `0x63bd00` en `0x5f3`, LINEAR y CLAMP) nace en `0x54271f` con vida 32,
    `(rand() & 15) − 8` en x y en y, la escala `((rand() & 31) + 32) · escala · 0,01f`, la velocidad (0; 3;
    0) girada por los ángulos del pedido y el giro x en `rand() % 360`. Su paso (`0x546b66`), después del
    movimiento común (que ya le sumó la velocidad girada por esos ángulos), le da la luz `vida · 0,03125`,
    la escala más 0,08f, la posición más la velocidad otra vez, sin girar (Q-C402), el freno 0,9f y la z en
    el terreno (`0x5f93c0`) más el alto de su textura (`+0x24` de su lugar en `0x7d3a198`) por la escala
    por 0,5 (salvo la /5, que nadie crea acá). Su dibujo (`0x548a51`) pide `0x620c10` y va al billboard
    común, con el giro de `+0x44` (en 0) y no el de la creación.

  Después, con la acción distinta de 1 y sin `+0x78 & 0x4000` del jinete, `rand() % 3`, y con 0 el
  aliento (`0x5f2`, o `0x607` en el mapa 7, que nace con la luz en 1; antes este documento lo daba como polvo, [Los efectos del login](#los-efectos-del-login)), que `0x53fe70` pone en el arreglo de `0x743a198` (de `0x78` bytes; lo mueve `0x5446e0` desde
  `0x634c60` y lo dibuja `0x548550` desde `0x6350b0`), desde (50; −4; 0) en el hueso 27 (`0x4ec110` con
  `0x68b1d2c`, sin el origen), con la luz 3 de `+0xf4`. En el paso, ese buffer tiene la pose del
  último modelo que posó el dibujo anterior, que no tiene por qué ser el caballo (Q-C401). Con la 3 (el
  jinete en `0x53`, el terremoto del Dark Lord), `0x52e470` con el héroe publicado (`0x743a170`) de dueño: del
  punto (60; 0; 0) del hueso 9 del buffer global (`0x68b19cc`) al del mismo hueso, en blanco y con el tipo, el
  largo y la key en 0; y, si la acción sigue en 3, `0x4453e0` (**el terremoto**, abajo). Con `jinete + 8` en 1
  (sólo el héroe puede tenerlo: lo pone `0x421db0` con el A9 de las mascotas, 1 con `0xfe` y 2 con `0xff`,
  Q-C436), doce `0x60f`/0 (`0x44469d..0x44474c`): cuatro vueltas que suben la z de los ángulos 90
  (`0x6b8948`) desde 0, giran (0; 50; 0) con esa z y le suman la posición del caballo, cada eje en float, y
  en cada vuelta tres `0x530860(0x60f, punto, punto, ángulos, 0, caballo, 40, 2, 0, 0, −1)` iguales: nacen
  con la luz del caballo (`+0xf4`, 3) y, con el ancho en 40, el paso no vuelve a leer al caballo (Q-C435);
  tres `rand()` cada uno, ninguno con el pool lleno. En la celda segura el paso no llega acá y el 1 queda
  para el primer paso afuera (Q-C437). Después `+8` vuelve a 0 (`0x444758`, también el 2) y el byte vivo
  pasa a ser el del jinete (`0x44475c..0x444764`). Y avanza con `0x4ec200` a `+0xd8`, con la acción en
  `+0xa0` del modelo.
- **El terremoto** (`0x4453e0(caballo, modelo)`, con la acción 3). Sube `+0x8e` (un byte; lo borra sólo la
  rama de las otras acciones del jinete, la que da la 0, en `0x44452e`) y, con `0x7d3a0f0` múltiplo de 10
  (el resto con signo de `idiv`), la onda `0x6eb`/0 en la posición y los ángulos del caballo con su luz:
  vida 30 y escala 20, sin `rand()` (`0x51f4ad`); su paso le resta 1 a la escala (`0x52c74b`) y se dibuja en
  el piso como la del galope (`0x52e0d2`). Con el frame (`+0x114`) en [8; 9,5] (`fcomp` con `test ah,1` y
  `0x41`: un NaN cae afuera):
  - con la cuenta impar, seis piedras: el giro arranca en `rand() % 360` y suma 60 (`0x6b86cc`) antes de
    cada una; (0; (cuenta / 2) · 150; 0) (`0x6b87ac`) girado con esa z más la posición del caballo, cada
    eje en float, y `0x518cf0(0x118 + (rand() & 1), punto, ángulos y luz del caballo, 0, 0, −1, 0, 0, 0,
    0)`, con el `rand()` del tipo después del giro y antes del alta;
  - siempre, el temblor de la cámara: `((rand() % 3) − 3) · 0,7f` (`0x6b8a5c`) en `0x7d325c8`.

  Fuera de la ventana, con la cuenta en `0x13`, `0x518cf0(0xfa, posición, ángulos, luz, 0, caballo, −1, 0,
  2, 0, 0)` (el terremoto con el caballo de dueño y el estado 2) y la cuenta pasa a `0xfd`, así que no
  vuelve a llegar a `0x13` hasta que otra acción la borre.
  - **El `0xfa`** nace (`0x51a66d`) con el dueño, `+0x28..+0x30` con los ángulos, la partida en la posición,
    vida 20, el subtipo `rand() % 100`, la z de los ángulos 330 más, `+0x28` 80 más, `+0x30` 180 más y `+0xe4` en
    50; después abre el registro propio, le sube `+0xf04` y lo cierra (Q-C426). No se dibuja nunca (`0x52d400`
    sale con el estado distinto de 0) y su paso no anima ni mueve (`0x52cbb0`). En la vida 11, con el estado
    distinto de 3, la partida pasa a (−25; −80; 0) girado por los ángulos del dueño más la posición; en Kalima,
    con el estado 0 o 2, revuelve el agua (`0x437860`), y si no sube 100 (afuera, 25); la z, la altura del
    terreno más esa subida; la partícula `0x5ed`/0 (`Effect\Explotion01.jpg`, un atlas de 4 × 4 que cuenta sus
    cuadros y prende la luz dinámica, `0x542a60`, `0x5459a4` y `0x54864b`) y su sonido `0x40`; la z menos 27; con
    un dueño que no es `0x1b1`, las grietas `0xfd`, `0xfb` y `0xfc` de tamaño 150 con el propio `0xfa` de dueño; y
    cinco rayos de largo `rand() % 150 + 100`, a `k · 72 + subtipo` grados, con un par `0xfe`/`0xff` de tamaño
    `rand() % 50 + 40` (salvo en una celda con el `0x4`, el `0x8` o el `0x10`, Q-C424) a la altura del suelo más 3
    (100 en Kalima). En la vida 10, cuatro anillos de cinco pares `0x101`/`0x102` de tamaño 100 (el sentido del
    cuarto sale de un `rand()`, Q-C429; la celda los frena sólo con los tres bits), el sonido `0x97` y la vida en
    0. En las demás, se hamaca alrededor de la partida. El estado 0 (la habilidad del Dark Lord) corta en
    `0x521e13` y `0x5224ec` (CL9.2, P-C58).
  - **Las grietas** `0xfb..0x102` (`Skill\EarthQuake01..08`, `0x6165bc..0x616688`) nacen
    (`0x51a6e4..0x51a85c`) con la escala `tamaño · 0,01f` y la vida de su tipo y subtipo (Q-C423); en su paso, la
    luz de las mallas, se hunden 0,5f, la `0xfb` hace temblar la cámara, la `0xfc` y la `0xff` sueltan piedritas
    `0xc8`/`0xc9`, y prenden la luz dinámica roja con un dueño que no es `0x1b1`, azul con él y blanca sin dueño
    (Q-C427); la `0x102` saca un `rand()` que nadie usa (Q-C428). Se dibujan con el genérico; con el subtipo 1,
    `0xfc`, `0xff` y `0x102` ponen `+0x74` en `0x80` y su dibujo corta en `0x6004aa` (no lo alcanza el caballo).
  - **Las piedras** `0x118` y `0x119` (`Skill\groundStone` y `groundStone2`, que `0x615700` carga en
    `0x61641d` y `0x616442`: 2 mallas, `DhorSS.jpg` y `DhorSS_R.jpg`, 3 huesos y una acción de 5 keys).
    Su alta (`0x51cc6b` y `0x51cd1b`), después del prefijo común, lee el atributo de su celda con
    `0x5f8290(ftol(x · 0,01f), ftol(y · 0,01f))`, que es `y · 256 + x` sin la máscara de `0x5f82a0`
    (Q-C431): con el bit `0x04`, el `0x08` o el `0x10` deja el lugar escrito con el byte vivo en 0 y sin
    `rand()`. Si no, vida 40, la escala `rand() % 30 · 0,01f + 1,2f` (`0x118`) o `+ 1` (`0x119`), +0xd8 en
    0,3f y el yaw `rand() % 360`; `0x4ea030(efecto, 0)` no hace nada (Q-C432). Su paso (`0x5275ca`): con
    la vida entre 33 y 36, el polvo `0x5f2`/11 en (x + 60; y − 60; z + 50) con la luz (1; 0,8f; 0,6f) y
    escala 2, y después la piedrita `0xc8 + (rand() & 1)` con el subtipo 10; con la vida bajo 8, el alpha
    por 0,7692308f y la luz de las mallas por 0,5f; siempre la luz dinámica (0,79f; 0,72f; 0,49f) de
    radio 2 (`0x5f9780`). Se dibujan con el genérico (`0x602e90`). Sus texturas se piden dos veces
    (Q-C434) y `0x615700` escribe un byte de su acción 0 que ya estaba en 0 (Q-C430).
  - **El polvo `0x5f2`/11** nace (`0x541d81`) con vida 50, siete `rand()` (x e y `(rand() & 63) − 32`, z
    `(rand() & 63) + 32`, la inclinación `rand() % 90 − 45`, el yaw y el giro `rand() % 360` y la caída
    `−((rand() & 7) + 40)`) y la luz de la creación guardada; su paso (`0x546111`) le da la luz `vida ·
    0,02f` por esa luz, la velocidad por 0,4f, la escala más 0,05f y la z menos 1; se dibuja con el
    billboard común.
  - **La piedrita `0xc8`/`0xc9` con el subtipo 10** nace por `0x51b432` con cinco `rand()`: el empuje
    `((rand() & 255) + 64) · 0,2` (el double `0x6ba1e8`) en y, la vida `(rand() & 15) + 32`, la escala
    `((rand() & 3) + 15) · 0,05f`, el yaw `rand() % 360` y la caída `(rand() & 15) + 28`; su paso es el del
    subtipo 0 sin el `rand() % 10` del fuego (`0x523ad5`).
- **Su dibujo** (`0x6350b0`: `0x510d00`, los personajes; `0x4453b0`, los ayudantes; `0x6046a0`, los
  objetos; en el juego, desde `0x637508`, después de los peces y antes de las hojas).
  `0x4453b0` llama a `0x445260(lugar, 0)` con los 10. `0x445260` prende `+0x16c` con el frustum
  (`0x5fba90`, en su lugar por 0,01, −20) y, visto y con el jinete `0x20a` (o el tipo `0x1c12`), le
  pone la escala 1,6 en la escena 2, 1 en la 4 y, en las demás, 1 salvo a los de Fenrir (`0x14a` a
  `0x14c`), y llama a `0x602e90(caballo, 0, 0, 0)` (10 con `+0x78 & 0x4000` del jinete: **la cáscara de
  cromo**, abajo). Después saca `rand() % 30 + 70` (`0x445336`),
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
- **La cáscara de cromo** (el 10 de `0x6002e0`, `0x60033c..0x600435`), con `+0x78 & 0x4000` del jinete en el
  ayudante (`0x445310..0x445328`) o del dueño en el cuervo (`0x420e11` → `0x4182f0(10)`). `0x5ffdf0` no lee el
  cuarto argumento: la pose, el alpha de Chaos Castle, el `0x1000` y la luz del terreno son los de siempre.
  `0x6002e0` mira el 10 en `0x600332`, antes de cualquier caso por tipo:
  1. recorre los 400 lugares del pool (`0x600343..0x600399`): si uno es el `+0x108` del objeto, no es el héroe,
     el asedio está prendido (`0x4294b0`), tiene `+0x78 & 0x4000` y su `+0x370` no es 1, sale sin dibujar
     (en el cuervo `+0x108` queda en 0, así que nunca se esconde, Q-C440);
  2. pone el color del modelo en blanco (`0x60039d`);
  3. malla por malla, sin `RenderBody`, `0x4ecc30(malla, 0x4040, 0,5f, blendMesh, luz, U · 8, V · 2, 0x620)`,
     y vuelve: ni sombra ni nada del tipo (ni el cromo del cuello del caballo, ni los efectos y los `rand()`
     del Fenrir, ni las mallas 3 y 2 del cuervo).

  El `0x4000` es el pase de brillo de la tabla de arriba, con `Effect\Chrome02` forzado y aditivo: no usa la
  luz por vértice ni el corrimiento U/V, así que blendMesh, luz, U · 8 y V · 2 no se miran, y el alpha va
  fijo en 0,5, no el `+0x174` (Q-C438). Con ese 0,5 cada malla multiplica en el lugar el color del modelo
  (Q-C95): la k-ésima que dibuja va en 0,5^(k+1) y el modelo compartido queda así hasta el próximo `0x5ffce0`
  (Q-C439). Una malla sin triángulos, con la textura `0x12d` o con N en el script sale de `0x4ecc30` antes de
  tocar el color (`0x4ecc5b`). Lo ponen en `+0x78` los handlers que llaman a `0x4e6380` (el `0x12`, el `0x13`, el
  `0x1f`, el `0x45`, el `0x07` con el byte 3 en 1 y el `0x68`) y lo sacan `0x4e6910` y `0x4fb9e0` (al morir);
  todos de la CL9: en MuRet nadie lo escribe todavía. MuRet: `Scenes::DrawChromeShell` (`CharacterDrawing`),
  `Render::Models::ShinePass::Chrome5`, `DrawTheShell` y `HiddenInTheSiege` en `HelperTable.cpp`, y la rama de
  `DrawGameRaven`.
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
  `WorldState::Helpers`: `CreateHelper` (`0x443fa0` → `0x443d70`, el `0xbf` y las [mascotas](#las-mascotas)),
  `TurnOffHelpersOf` (`0x443d40`, desde el `F3 10`; `0x510f70` en `KillOtherActors`),
  `StepHelpers` (`0x445230` en la escena 5, en `GameScene::StepWorld`, en `0x6370f4`, y en la 4, en
  `0x6342f4`) y `DrawHelpers` (`0x4453b0` en la escena 5 y en la 4, `0x63486a`). El paso saca el polvo y el aliento (CL7.6) a las partículas del juego
  (`WorldState::Particles`, con el reloj del mundo de `HelperStepContext::worldClock`) y la onda de Icarus
  a los efectos con modelo (`ModelEffects::CreateGallopShockWave`, con su paso en `StepGameModelEffect`);
  el buffer global de la pose es `WorldState::PoseCarry`, que publican los dibujos del frame anterior
  (el del caballo, `DrawHelpers` por `onPose`, que `GameScene` manda a `PublishFaunaPose`). La `0x5f3`
  es de `World::Particles` (el alta, el paso con el alto de su textura por `ParticleTextureHeightsOf` y
  el dibujo), y `Effect\ShockWave.jpg` y `Effect\smoke02.tga` suben con `EffectTextureSet`. Un hueso del
  buffer global que ningún dibujo escribió todavía es la matriz en cero de la `.bss` (200 huesos, hasta
  `0x68b3d9c`), y el punto sale del origen (`0x4442b4` y `0x44459d`, Q-C401). La acción 3 va a `CreateRibbon`
  (la cinta) y a `ShakeTheEarth` (`0x4453e0`, con `HelperStepContext::logicSteps` y `cameraShake`; las piedras
  con `World::ModelEffects::CreateGroundStone` y el atributo de la celda de `UnmaskedCellAttribute`, la onda con
  `CreateStompShockWave`); el `+8` del jinete, a `RaiseTheOrbits` (`CreateFaunaJoint` con
  `JointOwnerKind::Helper`, y `HelperState::light`); el dibujo con `+0x78 & 0x4000`, a `DrawTheShell`. Corta con
  error, después de los `rand()` que el original saca antes, una partícula que no se puede crear; los mapas
  `0x12` a `0x17` bajan el alpha en el dibujo. En el
  juego lo crea `0x5bc740` con el `0x1a04` en el lugar 8 (desde la auditoría del cierre de la CL7, `0x4225d0` ya no
  corta, ver [el F3 10](#el-f3-10-el-inventario)); los demás que lo crean no están portados. Desde que los ítems del
  piso no cortan siempre (`0x6374fe`), `DrawHelpers` corre en el juego. La selección crea los suyos con `0x5139f0` en la misma tabla; el del login sigue en `LoginScene` (P-C47).

### Las mascotas

Leído en la CL7 (2026-11-12, *estático* sobre c32bbd5f; el camino de `0x6002e0` con el `0xc6`, el `0x110` y el
`0x1c12`, **emulado** con unicorn sobre el `main.exe`, en los mapas −1 a 56). La tabla, la creación común, el
alpha de `0x4e9980` y el avance de `0x445006` son los del [caballo oscuro](#el-caballo-oscuro); acá va lo de cada
tipo.

- **Quién las crea en el juego.** `0x5bc740` con el lugar 8 (`0x5bc8b3..0x5bca46`, desde el `F3 10`): el Guardian
  Angel (`0x1a00`) con el `0x1c12`, el Horn of Uniria (`0x1a02`) con el `0xc6`, el de Dinorant (`0x1a03`) con el
  `0x110` y el Horn of Doom (`0x1a25`) con un Fenrir por el `+0x1b` del ítem, que copia antes a `+0x31b` del héroe:
  1 el `0x14a`, 2 el `0x14c` y otro el `0x14b`. Salvo con el `0x1c12`, con `+0x3e2` del héroe en 0 sigue
  `0x518cf0(0x61e, héroe + 0x10, héroe + 0x1c, héroe + 0xf4, 1, héroe, −1, 0, 0, 0, 0)`, aunque `0x443fa0` no haya
  creado nada; el paso de ese efecto (`0x518900`) crea tres joints `0x619/1` (*El calco de la mascota*, abajo).
- **`0x443d70`** con los Fenrir, el Uniria y el Dinorant: la escala en 0,9. Con el `0x1c12`: 0,7, el blendMesh
  (`+0x64`) en 1 (`0x443f1a`) y el lugar corrido desde el del jinete `rand() % 512 − 256` en x y en y y
  `rand() % 128 + 128` en z (`0x443f21..0x443f8c`, tres `rand()`). La velocidad del vuelo (`+0xcc..+0xd4`) no la
  escribe: un lugar que se vuelve a usar se queda con la de antes (Q-C420).
- **El paso** (`0x444010`, después del alpha y antes del avance):
  - el Uniria y el Dinorant (`0x4447c4..0x444ac4`): en la celda segura del jinete (como el caballo), el alpha en 0
    y al avance. Si no, el alpha por `+0x80` del jinete (`0x44480b..0x44484a`: con 1 o 2 baja 0,1f, restado en la
    x87, guardado en float y comparado sin redondear, con piso 0; con otro, 1), su lugar (el Dinorant a −10 en los
    mapas 8 y 10 y a −30 en los demás, salvo el −1) y sus ángulos, y la acción por la del jinete
    (`0x4448b1..0x4449b7`, con `0x4ea030`): `0x0d` a `0x21`, `0x46` y `0x47` → 2 (3 el Dinorant en el 8 y el 10), el
    galope; `0x40` y `0x41` → 6 (7 en el 8 y el 10, sin mirar el tipo); `0x22` a `0x37` → 3 (el Dinorant, 4, o 5
    en el 8 y el 10); otra → 0 (1 el Dinorant en el 8 y el 10). Con el galope, `rand() % 2`, y con 0, fuera del 10
    y sin `+0x78 & 0x4000` del jinete, el polvo del caballo (`0x444a19..0x444aa8`). La velocidad en 0,34 y el byte
    vivo del jinete (`0x444ac0`);
  - los Fenrir (`0x444af0..0x444f25`): la celda segura igual; el alpha por `+0x80`, la cabeza, el lugar y los
    ángulos del jinete, y la acción y la velocidad por la suya (`0x444bbc..0x444c4b`): `0x56` a `0x61` → 3, `0x62` a
    `0x65` → 4, `0x66` a `0x69` → 5, `0x76` a `0x79` y `0xc3` → 0, a 0,4 y sin polvo; `0x7a` a `0x7d` → 1, a 1;
    `0x6a` a `0x75` → 2, a 0,6; otra deja las dos. Con la carrera o el trote, el polvo: en el 10, sin `rand()`
    antes, desde el hueso 22 del buffer global con el frame en (1; 1,2) o desde el 44 en (4,8; 5], con (120; 0; 32),
    40 más en x y, en la segunda ventana, 700 más en z (Q-C421), la onda `0x6eb/1` y las `5f2` del caballo en Icarus
    (`0x444d68..0x444e16`); en los demás, `rand() % 3`, y con 0, fuera de Kalima y sin `+0x78 & 0x4000`, el polvo
    del caballo (`0x444e6f..0x444f1d`);
  - el Guardian Angel (`0x444f2a..0x444fff`): sin `+0x78 & 0x4000` del jinete, cuatro `5c5/1` con la luz 0,4,
    corridas `(rand() & 15) − 8` del ayudante en cada eje. Después del avance (`0x445052..0x445221`) vuela: gira
    20 grados hacia donde estaba el jinete al empezar el paso (`0x444080..0x44408b`) si está a 150 o más (al
    cuadrado, en float), se mueve con su velocidad girada por sus ángulos y `(rand() & 15) − 8` en z, con
    `rand() % 32` en 0 toma otra velocidad (de lejos `((rand() & 63) + 128) · −0,1f` en y; de cerca
    `((rand() & 63) + 16) · −0,1f` y el rumbo `rand() % 360`; la x en 0 y `((rand() & 63) − 32) · 0,1f` en z), y
    sube 1,5 su velocidad en z si está a menos de 100 sobre el jinete o la baja 1,5 si está a más de 200.
- **El dibujo** (`0x445260`), con el `0x1c12` sin un jugador de jinete: la escala de `0x4452db..0x445309` (1,6 en
  la 2 y 1 en la 4; en la 5, los Fenrir se quedan con el 0,9 y los demás van a 1). `0x6002e0` lleva el Dinorant a
  `0x600b35` y el Uniria y el Guardian Angel, por la cadena de los mapas (`0x438fe0`, `0x42cc10`, `0x4403b0`,
  `0x42f170`, …), a `0x602360`: los tres, `RenderBody(2, +0x174, +0x64, +0x68, +0x6c, +0x70, +0x58, −1)` con el
  color que dejó `0x5ffce0` (la luz del terreno más el 3 de `+0xf4`, con la luz por vértice), sin sombra, sin
  `rand()` y sin tocar el color ni `+0x88` del modelo (emulado). `+0x74`, que nadie escribe, no toma el camino del
  `0x80` (`0x6004aa`). Los Fenrir van a `0x60277b` (*estático*): dos pases en blanco de `0x4ecc30` (las mallas 0 y
  1) y, con la acción 4, un tercero (`0x46`, la textura `0x68f`), dos `rand()` y la partícula `0x5c6` desde el
  hueso 14; la sombra (fuera del 10 y de Kalima), seis sprites `0x5ac` en los huesos 11 y 13 con la luz de un seno
  del reloj del mundo y, en cada frame, dos `0x518cf0(0x14d, …)` (`0x602b30` y `0x602b50`) con el color de cada
  uno; con otras acciones, más `0x14d` y el `0x14e` desde sus huesos. Después de `0x602e90`, el `rand() % 30 + 70`
  de `0x445336`, por 0,01f, y con el `0x1c12` el sprite `0x5ac` en su lugar (`0x445390`: escala 1, rotación 0,
  aditivo) con esa cuenta por (0,5; 0,8; 0,6), cada canal guardado en float: con 80, (`0x3ecccccc`; `0x3f23d70a`;
  `0x3ef5c290`).
- **Los efectos de los Fenrir** (*estático*, ASM c32bbd5f; portados el 2026-11-12). El rayo `0x14d`
  (`Effect\lightning_type01.bmd`, `0x615ff5`, la textura `0x68f` `eff_lighting`) nace en `0x51db7a`: el prefijo común con
  el Fenrir de dueño, vida 100, la escala `(rand() % 100) · 0,002f + 0,3f`, el alpha 0,7f, los tres ángulos
  `rand() % 360` y el lugar por `rand() % 30`: el punto de un hueso del buffer global (`0x4ec110(…, 0)`, que no mira
  el modelo del dueño) con 1 y 2 el 10, 3 el 14, 4 y 5 el 2, 6 y 7 el 50 o el 51 por un `rand()` par o impar, 8 el
  53; con 3, 6, 7 y 8, 0,2f menos de escala; con 9 y 10 se apaga (alpha 0); con los demás, 0,1f más y la posición
  corrida `rand() % 240 − 120` en x, `rand() % 10 − 5` en y y 110 en z. Siempre, el sprite `0x5ac` de escala 2 en su
  lugar con la luz menos 0,3f (`0x51ddab`). Su paso (`0x5295a4`) prende el alpha de a 0,3f con el subtipo 0 y lo
  apaga con el 1, y gira 0,15f cada eje. El calco `0x14e` (`eff_lightinga01..05`, `0x690..0x694`, sin modelo) nace en
  `0x51ddfc` con vida 200, el cuadro (`+0x1c4`) en 0, la hora de `timeGetTime()` (`+0x1c0`) y la z en 0; su paso
  (`0x52965a`) gira 0,1f en x, baja el alpha 0,05f (la luz por el subtipo del color) y cambia de cuadro cada 200 ms
  de `timeGetTime()`, hasta el quinto; lo dibuja `0x52d8f2` en el piso. La chispa `5c6/15` (Spark03, `0x540b42`) saca
  un `rand()` y sólo con el resto de 3 en 0 toma vida 35, `+0x38` en 1 y la caída `((rand() % 5) + 5) · −0,5f`;
  si no, queda con la vida 2 del prefijo y el `+0x38` que tenía el lugar (Q-C422). Su paso (`0x5453d3`) baja la luz
  por 0,952381f y `+0x38` 0,001f, sin tocar la escala.
- **El calco de la mascota** (`0x61e/1`, *estático*, ASM c32bbd5f; portado el 2026-11-12). `0x518cf0` con el `0x61e`
  va a `0x51eebb`: vida 20 y, con el subtipo 1, la salida de `0x51db73`, sin `rand()`. Su paso (`0x5208a0`, el caso
  del `0x61e` y el `0x61f` en `0x52b9a6`) llama a `0x518900` con los subtipos de 1 en adelante salvo el 4, el 6 y del
  7 al 10: después del `rand()` de la luz (`0x5208c5`), tres `0x530860(0x619, inicio, posición, ángulos, subtipo,
  dueño, 5, −1, 0, 0, −1)` (el 5 pasaría como `0xb`), cada uno con sus ángulos `(rand() % 90, 0, rand() % 360)` y el
  inicio donde esos ángulos llevan `(0, −200, 0)` restado de la posición, con 120 más de z (`0x51897a..0x5189b4`,
  cada resta guardada en float). El `0x61e` no es un BMD: la cola común (`0x52cb98`) no avanza animación y descuenta
  la vida. El joint `619/1` tiene sólo lo común del `0x619` (`0x5323d3..0x5323f8`): velocidad 0, vida 12, el ancho y 2
  puntos. Su paso (`0x538a52..0x538d28`), después del avance común con la velocidad de antes: la velocidad sube 4,
  gira de a 10 (`0x4e9810`) hacia el dueño (`+0x54`, leído en vivo) 120 más arriba, la luz es `(12 − vida) · 0,1f`
  por (0,4f; 0,6f; 1), con el producto sin redondear, y con la vida en 1 el sprite `0x5fd` en ese destino con el giro
  `rand() % 360` y la escala `((rand() & 7) + 8) · 0,2f`; la cola común con la matriz del principio del paso. El
  pase del suelo dibuja el calco (`Effect\Magic_Ground2`, `0x61e`) con el color del subtipo 1 y el lado de la
  llegada, y `0x53c6a0` dibuja los joints con `JointEnergy01` (`0x619`) sin casos para el subtipo 1.
- **La carga** (`0x615700`): `Skill\Rider01.bmd` (`0xc6`, `0x615eca`), `Rider02.bmd` (`0x110`, `0x615f30`; el
  `.smd` de `0x615f17` no corre con `[0x6c9e0c]` en 1), `fenril_black`, `fenril_red` y `fenril_blue` (`0x14a` a
  `0x14c`, `0x615f5c`, `0x615f90` y `0x615fc1`) y `HellGate.bmd` (`0xc5`, `0x61605c`). Las texturas de los Fenrir son
  las primeras `0x60d5f0` de `0x615700` (`0x615f77`, `0x615fab` y `0x615fdc`), desde donde las dejó `0x611850`:
  `fenril02.jpg` en `0x371` (de los tres) y `fenril_black`, `_red` y `_blue` de `0x372` a `0x374`. Después del
  cuervo (`0x6169c2`) piden HellGate (`0x6169e0`, `0x3cb` y `0x3cc`) y el Dinorant (`0x6169fb`, `RDgon.jpg` y
  `RDgonW.tga` en `0x3cd` y `0x3ce`). El Uniria pide las suyas solo en el lazo `0x616e20` (de `0xbe` a `0x150` con
  `Skill\`), y encuentra por el nombre `unicon.jpg` y `unicon01.tga` en `0x2cb` y `0x2cc`, las de `Helper03` en
  `Item\`. *Sin verificar* contra `lor_slots.bin`: `ModelTextureSlotsGoldenTest.StartupMatchesTheDump` compara
  `0x371..0x374` y `0x3cb..0x3ce` en el laboratorio. `Rider01` tiene 2 mallas, 24 huesos y 4 acciones; `Rider02`, 2,
  73 y 8; los Fenrir, 2, 58 y 6. El Guardian Angel es `Player\Helper01.bmd`, de `0x60ff50`.
- **En MuRet.** `Scenes::CreateHelper` construye los siete tipos; `StepHelpers` (con `Scenes/PetHelperSteps` y
  `Scenes/HelperStepParts`) los mueve en la escena 5 y en la 4, con el esqueleto de cada tipo de
  `HelperStepContext::models`; `DrawHelpers` dibuja el Uniria, el Dinorant y el Guardian Angel con `DrawObject` y el
  brillo en la tabla de sprites del frame (`HelperDrawContext::lighting` y `sprites`), en el juego (`GameSceneTail`)
  y en la selección (`0x63486a`). `GameModels` carga los cinco modelos de `Skill\` con sus texturas. El `F3 10` crea
  el Guardian Angel con el `rand()` compartido. Los Fenrir se dibujan con `DrawFenrirBody` y sus efectos van a
  `World::ModelEffects::CreateLightning` y `CreateLightningDecal`, con `HelperDrawContext::effects`, `particles`,
  `systemTime` y `globalPose` (la copia proyectada del buffer global en los dibujos intermedios); el calco de las
  demás mascotas fuera de la zona segura va a `World::ModelEffects::CreatePetSummon`, su paso a
  `Scenes::StepPetSummon` (desde `GameScene::StepTheModelEffects`, como el `5ac/3`) y sus joints a
  `CreateFaunaJoint` y `StepJoints`. Cortan con error lo del caballo (una partícula que no se puede crear, un hueso
  más allá de los 200 del buffer global); con `+0x78 & 0x4000` del jinete, la cáscara de cromo. Pruebas: `HelperTableTest` (`TheUniriaFollowsItsRiderAndRaisesTheDustOfItsGallop`,
  `TheFenrirTakesItsGaitFromTheRider`, `InIcarusTheFenrirRaisesAWaveAndPuffsFromTwoBones`,
  `TheGuardianAngelSparklesAndFliesWithItsVelocity`, `TheUniriaAndTheDinorantDrawWithTheLightOfTheTerrain`,
  `TheDrawScaleDependsOnTheSceneAndOnTheFenrir`, `TheGuardianAngelDrawsItsBlendMeshAndLeavesItsGlow`,
  `ThePetDrawStopsWhereMuRetHasNotArrived`, `TheFenrirDrawsItsSparkItsFlaresAndTwoBolts`,
  `TheFenrirLeavesItsDecalsInTheWindowsOfItsStrikes` y otras), `GoldenGameModels.ThePetMountsLoadWithTheirTextures`,
  `CharacterSelectSceneTest.TheSelectionDrawsTheUniriaOfItsCharacter`,
  `CharacterDrawingTest.TheFenrirGoesWhiteChargesItsSecondMeshAndShadowsEveryMesh`, los del rayo, el calco y la chispa
  en `ModelEffectsTest`, `GameModelEffectsTest` y `WeaponSparkParticlesTest`,
  `GameCharacterEffectsTest.ThePetSummonDropsThreeEnergyJointsAroundItsOwnerEachStep`, los `TheSummonEnergy…` de
  `FullSetJointsTest`, `InventoryReplyTest.AGuardianAngelOfSlot8IsCreatedWithTheSharedRandom` e
  `InventoryReplyTest.HornOfDoomCopiesItsFlags1bAndSummonsOutsideTheSafeZone`.

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
corre un frame del bloque cada 46,875 ms de reloj, como la mascota (Q-C20). Como el bloque corre
después de los pasos del frame, en el original la tela siempre se simula con el cuerpo que se dibuja.
En los dibujos de MuRet entre dos frames del original, cada tela va llevada con su hueso del cuerpo de
su última simulación al de ahora (`Scenes::ClothCarry`, #307): si no, con el personaje que camina, la
capa se queda atrás y salta en el frame siguiente. Los frames del original y el modo fiel no cambian.
En el juego, los frames del original se cuentan por los pasos de la escena
(`Scenes::OriginalFramesOfTheSteps`), para que cada uno vea 1 o 2 pasos como en el original, también a 60 FPS.
Un dibujo es como mucho un frame del original y sólo si trae pasos nuevos: el original nunca corre dos en un
dibujo (`0x637791..0x6381a5`), y por debajo de 21,33 FPS cada dibujo es un frame con más pasos.

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
  [estados 2, 5 y 6 con un objetivo o dueño.flags & 0x4000: las órdenes A7, CL9]
  zona segura (dueño + 0x3e2 == 1, justo 1): si el estado no es 3 ni 4: estado 4, velocidad 3 (Q-C414)
  fuera de ella: si el estado es 3 o 4: estado 0          (cada cambio pone el contador +0x60 en 0)
  frame += f(0,4); al pasar la última key, la acción por el estado (tabla 0x417f0c):
    estado 0 → acción 1 (planea), 1 → 0 (aletea), 2 → 3, 3 → 2, 4 → 1, 5 → 3
  dx = f(dueño.x − x); dy = f(dueño.y − y)
  si (dueño.flags & 0x4000) == 0:                               la chispa
    hueso = rand() % 66; p = la traslación de ese hueso en la pose del último dibujo
    0x53fe70(0x5c6, p, ángulos, (0,3; 0,4; 0,7), 5, 0,8, 0)      (1 rand() adentro)
  d2 = dy² + dx² (double); d2f = f(d2)
  si d2 ≥ 22.500: yaw = f(TurnAngle(yaw, f(CreateAngle(x, y, dueño.x, dueño.y)), f(rand() % 15 + 5)))
  v = VectorRotate(dirección, AngleMatrix(ángulos)); posición += v (cada una con f)
  H = dueño.z + 350 en el mapa 0x37 o con dueño + 0x318 == 0x1a04 (250 en los demás, Q-C131, Q-C413)
  div = 28; si z < H: pitch = f(pitch − 2) (≥ −15), div = 10
            si no, si z > H + 100: pitch = f(pitch + 2) (≤ 15), div = 20
  si rand() % div == 0:                                         el sorteo
    cerca (d2f < 22.500): base = f((rand() & 7 + 32) · −0,1); yaw = f(rand() % 60 + yaw)
    lejos:                base = (rand() & 63 + 128) · −0,1
    dirección = (0, f((base + dirección.y) · 0,5), f((rand() & 63 − 32) · 0,1))
  dirección.y < −12 → estado 0; si no → estado 1    (cada cambio pone el contador +0x60 en 0)
  [lo de arriba desde dx, solo en los estados 0 y 1; en el 3 y el 4, sin rand():]
  estado 3 (0x4178a6): posición = 0x4ec110(modelo del dueño, hueso 37 de dueño + 0x120, (−10, 0, 10), 1)
             ángulos = (dueño.pitch, dueño.roll, f(dueño.yaw − 120))
  estado 4 (0x4178fc): m = AngleMatrix(ángulos); si velocidad ≠ 0: posición += VectorRotate((0, −vel, 0), m)
             h = 0x4ec110(modelo del dueño, hueso 42 de dueño + 0x120, (0, 0, 0), 1)
             d = 0x4e9810(posición, ángulos, h, vel)   (gira yaw y pitch hacia h; d en la x87)
             vel = f(vel + 1); si d < 50: estado 3
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
- **En la zona segura** (*estático* y *emulado*: `make_raven_safe_zone_vectors.py` corre el paso real con el
  dueño entrando y saliendo de la zona). Pasa a bajar (el estado 4) con la velocidad en 3 y, en cada paso,
  avanza su velocidad hacia adelante, gira hacia el hueso 42 del dueño con `0x4e9810` y acelera 1: de lejos
  no llega (la velocidad crece más que el giro) hasta que el contador de lejanía lo trae junto al dueño, y
  a menos de 50 se posa (el 3), en el hueso 37 con (−10, 0, 10), con los ángulos del dueño y el rumbo 120
  grados menos; la velocidad queda como estaba. Al salir de la zona vuelve al 0 y a volar. `0x4ec110` con
  Translate en 1 suma la escala (`+0x68`) y la posición (`+0x6c`) del **modelo compartido** del tipo del
  dueño (`0x5769ad8 + 0x20a · 0xe0`), no las del dueño: las del último `0x20a` que las copió, en su paso
  (`0x505ec6`) o al posar su cuerpo (`0x6001b0`), y el paso del cuervo corre antes del `0x505ec6` de su
  dueño (Q-C412). `0x4e9810`: el rumbo con `CreateAngle` de (x, y) a (x, y) del objetivo y `TurnAngle`;
  dx y dy en la pila, dz y los cuadrados de dx y dy en float, el largo en el plano en float, el cabeceo
  con 360 menos `CreateAngle(z, plano, z del objetivo, 0)` y `TurnAngle`, y devuelve
  √(dz² + dx² + dy²) sin guardar en float.
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
  y MuRet la suma al final. Desde la auditoría del cierre de la CL7 (2026-11-12), **la zona segura**: los
  estados 3 y 4 bit a bit contra el código real (`tests/vectors/world55/raven_safe_zone.txt`, de
  `make_raven_safe_zone_vectors.py`: 4 casos y 610 pasos, con los huesos propios del dueño girados y la escala
  y la posición del modelo compartido), `Render::Models::MoveToward` (`0x4e9810`) y
  `WorldState::PlayerModel`, el `+0x68`/`+0x6c` de `Player.bmd`, que escriben `0x505ec6` en el paso y
  `0x6001b0` en el dibujo del juego, del login y de la selección (Q-C412). Los estados 2, 5 y 6 (las órdenes
  `A7`) cortan en `0x416add`.
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
- **La cola de `0x50ba30`** (`0x50db57..0x510ac2`, *estático*, cierre de la CL7, 2026-11-12). Después del
  cuerpo, las partes y la tela, el personaje del login corre lo mismo que en el juego: el `rand() % 30 + 70`
  de `0x50db57`, lo que cuelga (`0x509340`, con el cetro) y los efectos de las manos, del conjunto y de las
  banderas (`0x50ea20..0x510a7a`). Esa cola no mira la escena salvo `0x510ef0` (la marca del guild, sólo en
  la 5); la luz de lo que cuelga (`0x50db94`) es `[ebp−0x44]` más `+0xf4`, y fuera de la escena 4
  `[ebp−0x44]` es la luz del terreno en su lugar (`0x5f99e0`, `0x50cc55`), la misma del cuerpo; con
  `+0x375` desde 6, (1; 0,1; 0,1). El del login no es el héroe publicado (es el lugar 2), así que no corre
  la rama del color de `0x50cca9`. MuRet (`LoginScene::DrawTheTail` → `Scenes::DrawCharacterTail`):
  una vez por frame del original que cuenta el reloj (con varios, sólo el último dibuja) y, entre medio,
  sobre copias del `rand()`, de los ítems y del estado de los modelos, repitiendo los sprites de lo que
  cuelga del último frame del original (`GoldenLoginWorld.TheLoginCharacterRunsTheTailOfTheGame`,
  `TheScepterGlowChangesOncePerOriginalFrame`). Cada entrada arranca el ritmo de la tela y de la cola,
  como el de los objetos: entre una entrada y la otra el login no se dibuja (`EnteringAgainStartsTheScepterClock`).
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
desde el cierre; desde el cierre de la CL7, con el `rand() % 30` de `0x50db57` y el resto de la cola de
`0x50ba30`, `Scenes::DrawCharacterTail`), el aliento, el viento de las partículas, las brasas, el cuervo, el gesto, la cabeza, 4
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
  `0x96000000`), **el envío de la cuenta** el 472 y el 473 (`0x6427d9` y `0x6427ea`, al final de `0x641bf0`,
  después del `F1 01`; antes este documento decía "el cancelar") y la respuesta al `F4 03` el 481, conecte o no
  (`0x4e33d0`, con el texto como formato de la dirección y el puerto, Q-C464). Los cuatro van sin nombre: el de
  `0x6e57a4` y el de `0x5767974` son cadenas vacías (`.data` en cero, nadie las escribe). `0x5554b0` suma un
  renglón vacío cada 150 pasos desde el último, así que suben y se van. MuRet la tiene desde la CL8 (hito 3):
  `Scenes/GameMessages` y `LoginScene::AddTheMessage`.

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

**El administrador, entero** (CL8, hito 8; *estático*). `0x637700` llama a `0x65fc30` **una vez por
frame**, después del mouse (`0x640940`) y antes de los pasos de ese frame (`0x63778c`), con los
milisegundos del frame (el `double` de `0x459c00`, hasta 200). `0x65fc30` vuelve sin hacer nada en el
modo 0 o sin ventanas; si no, pone `+0x8de8` en 0 y:

1. Con el izquierdo apretado (`+0x1c`), la primera desde la cabeza con el mouse adentro (`vtable + 0xc`
   con 0) pasa a la cabeza con el foco (`0x65f610`); si no hay ninguna, la cabeza pierde el foco
   (`vtable + 0x14` con 0). Con el izquierdo soltado (`+0x1e`), `+0x8de9` en 0, que nadie del login ni de
   la selección lee.
2. Copia la lista en un arreglo nuevo, en su orden (de la cabeza a la cola), con los botones de todas sin
   el mouse (`0x669ed0` con 0), y le da el mouse a los de la primera que lo tiene adentro.
3. Atiende cada una del arreglo con `0x669f40`: **la copia**, así que una ventana que otra abre en este
   frame se atiende en el mismo frame si estaba más abajo en la lista (la barra de la selección abre la
   de crear y la de crear corre su lógica en el mismo frame), y una que se cierra se saltea
   (`0x669f40` mira `+0xc`).
4. `0x65f720` pega la que se arrastra, y `+0x8de8` (la UI tiene el mouse) queda en 1 si alguna se
   arrastra (`+0x10` de 1 a 4) o si, desde la cabeza, alguna tiene el mouse adentro.

`0x669f40` (la base de todas): cerrada, nada; al soltar el izquierdo, `+0x10` en 0; sin arrastre
(`+0x10` en 0), sus botones (`0x63c490`); `vtable + 0x28`; y **solo con el foco** (`+0xd`), el
arrastre (apretado en el título, `vtable + 0xc` con 2: los 26 píxeles de arriba, `0x669df0`), que suma
lo que se movió el mouse y, si no quedó pegada (`+0xe`), la mueve (`vtable + 8`), `vtable + 0x2c` y la
lógica, `vtable + 0x30`. Ninguna ventana del login ni de la selección acepta el 2 (la de Esc,
`0x647d10`, y las demás con su `vtable + 0xc`), así que ahí nada se arrastra. `0x65f720` mira solo la
cabeza arrastrándose: a menos de 10 píxeles de una esquina de la pantalla la pone ahí, y si no, de un
borde (`vtable + 8`); después, con las otras abiertas desde la segunda, una esquina cerca de una esquina
distinta de la otra pega las dos esquinas y termina (la tabla de `0x65fb94`), y si no, un borde: con las
dos superpuestas a lo ancho, arriba o abajo de la otra, y si no, a lo alto, a su derecha o su izquierda.
`0x65fbb0` pega solo si algo de la ventana queda adentro de la pantalla (`IntersectRect`) y pone `+0xe`.
El modo 5 (`0x65f5f0`, desde `0x636290`) vacía la lista (`0x65f2b0`): en el juego, el administrador no
tiene ventanas. `0x65f300` (el login) y `0x65f4b0` (la selección) también empiezan vaciándola y vuelven a
armar cada ventana. MuRet: `UI::NewUi` (`NewUiTest`), con las ventanas de cada escena; lo corre al
empezar cada paso (Q-C490).

### La ventana de Esc

`+0x3d0` (vtable `0x6baa0c`; CL8, hito 8; *estático*). La arma `0x6478c0`, la ubica `0x647c00`, la abre
`0x647cd0`, la atiende `0x647d40` y la dibuja `0x647ed0`. Es de **toda la pantalla**
(`0x669c70(W, H, −1, 0)`, el negro con alfa `0x80` de atrás, como el aviso): `0x647c00` mueve el marco y
los botones, no el rectángulo. El marco (`0x66a1c0`, con la tabla de cinco imágenes) es de 213 de ancho
y `64 + líneas · h + 43` de alto, al centro, con h el alto del buffer de `op1_back3` (`0x66a4e0`):

| Slot | Archivo | Qué |
| --- | --- | --- |
| `0x72a` | `Interface\op1_stone.jpg` | la piedra de adentro, repetida, a 3 de cada borde |
| `0x72b` | `Interface\op1_back1.tga` | el borde de arriba, de 64 |
| `0x72c` | `Interface\op1_back2.tga` | el de abajo, de 43 |
| `0x72d`, `0x72e` | `Interface\op1_back3.jpg`, `op1_back4.jpg` | los lados, de 5, repetidos entre los dos bordes |
| `0x72f` | `Interface\op1_b_all.tga` | los cuatro botones de 108 × 30 (`0x63c330`) |

Los botones, a `(213 − 108) / 2` del borde izquierdo: salir del juego (381) a 33 de arriba, elegir otro
servidor (382) 34 más abajo, cerrar (388) a 52 del borde de abajo y las opciones (385) 34 más arriba.
`0x6478c0` mira el modo del administrador: en el login (2) el de otro servidor queda deshabilitado, con −1
de cuadro deshabilitado (`0x63c710` no lo dibuja), y el marco tiene 6 líneas; en la selección (4), 10.
En 640 × 480 con líneas de 8, el login lo pone de 229 a 259 en y y las opciones de 231 a 261: el botón
que no se ve está casi entero debajo del de las opciones (Q-C492). Arranca sin cuenta (`+0x408`, un
`WORD`, en −1, y `+0x410` en 0). `0x647cd0(1)` la abre sin tocar la cuenta.

`0x647d40` corre con el foco. **Con la cuenta** (`+0x408` distinto de −1) suma el tiempo del frame a
`+0x410` y, cuando pasa de 1.000 (el `double` de `0x6baa48`, estricto), baja la cuenta: en 0, el log
"> Menu - Exit game." (`0x41fa40`) y `PostMessageA(WM_CLOSE)`; si no, `+0x410` en 0 y `0x647ef0`, que
arma el texto 380 con `sprintf` y lo que falta (`"% d"`) y lo manda a `0x555190` como mensaje de
sistema (tipo 1) sin nombre. Con la cuenta no mira ni los botones ni Esc (Q-C493). **Sin la cuenta**:
salir la pone en 5 y llama a `0x647ef0`; otro servidor deja el log "> Menu - Join another server.",
manda el pedido de salida con 2 (`0x647f60`) y cierra esta (`0x65f6c0`, `0x647e38`) y la barra de la
selección (`+0x7f50`, `0x647e48`); las opciones la cierran y abren la de `+0x7e8` (`0x647e75`); cerrar, o
Esc con `GetAsyncKeyState(VK_ESCAPE) == 0x8001` (y `[input + 0x31]` en 0), el sonido `0x19` y la cierra.
`0x647ed0` dibuja el fondo (`0x66a050`), el marco (`0x66a960`: la piedra, arriba, abajo y los dos lados)
y los botones con su rótulo, en los colores `0xffe2e2e2`, `0xffe2e2e2`, `0xffffffff` y 0 de sus cuadros.

`0x647f60` arma `C1 05 F1 02` y el tipo con el XOR32 (la clave de 32 bytes de `0x648025`) y lo manda
como `C3`: un `rand()` para el relleno (`0x648241`) y el serial de `0x5767913` por la tabla de
protección, que se vuelve a cifrar al cerrarse con otro: **dos `rand()`** de la CRT (Q-C497). El GS
contesta con el `F1 02` del mismo tipo, y el 2 vuelve a la lista de servidores (`0x4c2710`, Q-C79).

La abren el botón del menú de la barra del login (`0x6411b0`), el de la selección (`0x63eb90`, `+0x31a`)
y Esc en el paso de cada escena. En el login (`0x634d38`), Esc se lee después del paso del mundo, con el
aviso, la cuenta, esta, las opciones y los créditos cerradas y la barra y la lista abiertas. En la
selección, Enter (`0x634341`) se lee antes, y Esc (`0x63447c`) solo si no se apretó Enter, aunque no
entre: con el aviso, la de crear, esta y las opciones cerradas, el sonido y `0x65f690` (`0x6344ce`). Como
el administrador corre antes del paso, el Esc que cierra esta ventana ya no lo ve el paso: borra el bit
bajo (Q-C45) y no la vuelve a abrir. MuRet: `UI::EscapeWindow` (`EscapeWindowTest`), en la `LoginScene` y
la `CharacterSelectScene` (`LoginSceneSessionTest.TheBarStepsWithTheList`, `TheBarStepsWithTheLogin` y
`CharacterSelectSceneTest.TheEscapeWindowAsksToJoinAnotherServer`); las opciones y los créditos, abajo.

### Las opciones

`+0x7e8` (vtable `0x6ba988`; CL8, hito 8; *estático*): la fila "sin identificar" de la tabla de § Las
ventanas de la UI nueva. La arma `0x644120`, que llaman `0x65f300` (el login) y `0x65f4b0` (la selección)
después de la de Esc, cada vez que arman la UI; la ubica `0x644520` (`vtable + 8`), la abre `0x644640`
(`vtable + 0x10`), la atiende `0x644730` (`vtable + 0x30`) y la dibuja `0x644860` (`vtable + 0x34`). Como la
de Esc, es de toda la pantalla (`0x669c70(W, H, −1, 0)`, el negro con alfa `0x80`) y `0x644520` mueve lo de
adentro. El marco es el de `0x66a1c0` con 30 líneas y otro borde de arriba, de `30 · h + 65 + 43` de alto
(h, el del buffer de `op1_back3`), al centro. Las texturas nuevas las sube `0x61ccb0` con las de la de Esc:

| Slot | Archivo | Qué |
| --- | --- | --- |
| `0x730` | `Interface\op2_back1.tga` | el borde de arriba, de 213 × 65 (en lugar de `op1_back1`) |
| `0x731` | `Interface\op2_ch.tga` | las casillas, de 16 × 16 con dos cuadros: `0x63c330(16, 16, 0x731, 2, 0, 0, −1, 1, 1, 1, −1)`, el 0 sin marcar (apretada o no) y el 1 marcada |
| `0x732` | `Interface\op2_volume3.tga` | la perilla de los deslizadores, de 13 × 13 |
| `0x733` | `Interface\op2_volume2.jpg` | el relleno, de 4 × 7, estirado (`0x647370`) |
| `0x734` | `Interface\op2_volume1.tga` | la barra, de 98 × 13 |

Las tres casillas van a 52 del borde izquierdo y de arriba, una cada 41 (16 + 25): el ataque automático
(386, `0x6c8e3c`), el sonido de los avisos del filtro (387, `0x77f3b78`) y los carteles de `Slide.bmd` (919,
`0x6c7a8c`). Los dos deslizadores van a 52 del borde: el del volumen (389, `0x7d3a118`, de 0 a 9) 48 abajo
de la última casilla más su alto, y el del brillo de los ítems (1840, `0x6c9dcc`, de 0 a 4) 61 más abajo (48
más el alto de la perilla). El botón de cerrar (388, `op1_b_all` de 108 × 30 con los colores de la de Esc),
a `(213 − 108) / 2` del borde y a 301 de arriba. `0x6446e0` marca las casillas y pone los deslizadores con
los globales al armarlas; después nada los vuelve a leer de los globales. El volumen arranca del registro
(`VolumeLevel`, `0x4bad33..0x4bad55`; fuera de 0 a 9, 5) y el brillo en 4 (la `.data`).

`0x644730` corre con el foco, en este orden:

1. los deslizadores (`0x646a50` de cada uno), con el mouse de `0x640850`;
2. una casilla con clic escribe su global (0 o 1) y termina;
3. el botón de cerrar la cierra (`0x65f6c0`), sin sonido;
4. el volumen moviéndose (`+9` del deslizador distinto de 0): si su valor es otro, lo escribe en
   `0x7d3a118` y llama a `0x632d80` (el volumen del DirectSound de `0x41eed0`); con el mismo, termina igual;
5. el brillo moviéndose: compara el byte bajo del valor con `0x6c9dcc`, lo escribe si es otro, y termina;
6. Esc (`0x8001`, con `[input + 0x31]` en 0): el sonido `0x19` y la cierra.

Una casilla que cambia y un deslizador apretado terminan el frame antes de Esc (Q-C502). `0x644860` dibuja
el fondo, el marco (la tabla de `0x66a960` con `op2_back1` arriba), el título (385) centrado en el ancho del
marco a 10 de arriba, con la fuente de `0x5763560` en blanco, los rótulos de las casillas 24 a la derecha y
4 abajo de cada una, los de los deslizadores 18 arriba y su valor 85 a la derecha (el del global, no el del
deslizador, y el brillo como `2 · d + 5`, Q-C499), los deslizadores (`0x646e90`) y, al final, las casillas y
el botón (`0x66a090`).

**El deslizador** (vtable `0x6ba9f8`; *estático*). `0x646640` lo arma con la perilla (un botón de un cuadro
en `+0x28`), la barra con su relleno (`0x640220`, `+0x108`) y el rectángulo del relleno adentro de la barra
(izquierda, arriba, derecha y abajo: (3, 3, 95, 10)). Las opciones lo arman horizontal (`+8` en 0; el
vertical no lo arma ninguna ventana del login ni de la selección), con el máximo en `+0x1c` (1 o más,
`0x6468e0`), el valor en `+0x20` (`0x646f80` lo acota de 0 al máximo) y el recorrido de la perilla en
`+0x24`: el ancho de la barra menos el de la perilla, 85. `0x646830` pone la perilla en
`x + recorrido / máximo · valor` (`fild`, `fidiv`, `fimul`, `fiadd` y `_ftol`: con el máximo 9, en 0, 9, 18,
28, 37, 47, 56, 66, 75 y 85); `0x646900` baja uno y la pone igual; `0x646990` sube uno y, en el máximo, la
pone en `x + recorrido` sin la cuenta. `0x640580` le da al relleno `(derecha − izquierda) · valor / máximo`
de ancho, en enteros sin signo, y su alto. `0x646a50`, su lógica:

1. la perilla con el mouse (`0x63c490`): no está en la lista de la ventana (`0x66a070`), así que su `+0xa9`
   queda en el 1 de `0x63c330` y ve el mouse aunque la ventana no lo tenga;
2. al soltar (`+0x1e`), la perilla vuelve al lugar de su valor y `+9` en 0;
3. con el mouse a la izquierda de la perilla (de x hasta ella, del alto de la perilla) o a la derecha (de
   ella al final de la barra), `+0x110` suma el tiempo del frame; al apretar (`+0x1c`), un paso hacia ese
   lado, `+9` con 1 o 2 y `+0x110` en 0; con `+0x110` en 500 ms o más (`0x6baa00`) y ese lado en `+9`,
   cada 50 (`0x6b89d0`, el tiempo en `+0x118`) otro paso;
4. si se apretó y la perilla está debajo del mouse (`0x63c470`, ya movida por el paso: Q-C503), la agarra:
   `+9 |= 0x10`, la x del mouse en `+0x120` y la de la barra donde se agarró en `+0x124`;
5. agarrada, la perilla sigue al mouse de `+0x124` a `+0x124 + recorrido` (antes, al principio; después, al
   final) y el valor es el más cerca: `_ftol(((perilla − x) + paso · 0,5) / paso)`, con
   `paso = recorrido / máximo` en la x87.

MuRet: `UI::Slider` (`SliderTest`) y `UI::OptionsWindow` (`OptionsWindowTest`), con los globales de
`WorldRuntime` (`Scenes/OptionsSettings.h`: `ui.autoAttack`, `chat.noticeSound`, `chat.slidesOn`,
`ui.volume` y `ui.glowDetail`), en las dos escenas
(`LoginSceneSessionTest.TheOptionsOpenFromTheEscapeWindow` y
`CharacterSelectSceneTest.TheOptionsOpenFromTheEscapeWindow`). Sin el volumen del DirectSound ni el sonido
(P-C79).

### Los créditos

`+0x4578` (vtable `0x6ba848`; CL8, hito 8; *estático*). La arma `0x63f020`, solo en el login (`0x65f300`, la
última: queda en la cabeza de la lista, cerrada), la ubica `0x63f3a0`, la abre `0x63f5a0`, la atiende
`0x63f630` y la dibuja `0x63f6b0`. `0x63f020` arma:

- el fondo de toda la pantalla, `0x669c70(W, H, −1, 0)` con el alfa en `0xff` (`0x63f13b`): **negro opaco**;
- el adorno (`deco.tga`, `0x12`, 189 × 103, en píxeles) en `+0x190` y el logo (`cr_mu_lo.tga`, `0x1a`,
  290 × 41) en `+0x238`;
- tres barras negras sin textura de 800 × 42, opacas (`0x647050` con el alfa en `0xff`);
- el botón de cerrar con `message_close_b_all.tga` (`0x726`), de 54 × 30 con tres cuadros (el 2 apretado y
  el 1 con el mouse encima);
- los 16 nombres de las ilustraciones, `Interface\im1_1.jpg` a `im8_2.jpg`, en `+0x5cc`;
- su fuente: `CreateFontA` de alto 14, 18 o 24 si W es 800, 1024 o 1280 justo, y 10 con cualquier otro
  (Q-C506), de peso 700, con la cara del texto 0 y el charset que lee `0x63f2f2`;
- la tabla de `Data\Local\credit.bmd` (`0x63ff30`, [05](05-formatos-de-datos.md#creditbmd)).

`0x63f3a0` guarda W / 800 y H / 600 en float (`fild`, `fmul` por los floats de `0x6ba884` y `0x6ba880`,
`fstp`) y ubica en ese espacio de 800 × 600 las dos mitades de la ilustración (400 × 400 en (0, 126) y
(400, 126)), el logo en (241, 549) y las barras en (0, 42 · i); y en píxeles, el adorno en
`(W − 189, _ftol(H · 0,8783333f) − 103)` (el float de `0x6ba888`: con 600, el producto da 526,99999… y el
adorno queda en 423, uno más arriba que con 0,878333… exacto, Q-C500) y el botón a (122, 63) del adorno. `0x63f5a0(flag)` prende o apaga las imágenes y abre o cierra el botón; con 1, `0x63fbf0` los
empieza; con 0, el estado de la ilustración (`+0x5b8`) en 0. `0x63fbf0`: la ilustración en el estado 1 con
el par 0 (`0x63fc50`, que sube `im{n+1}_1.jpg` e `im{n+1}_2.jpg` en `0x6f8` y `0x6f9` con `0x63b870`, con
filtro y sin repetir, y las deja visibles con alfa 0), los tres renglones en el estado 1, `+0x39d0` en 0,
el índice (`+0x39a0`) en 0, la cuenta de los nombres en 0 y el primer grupo (`0x63ffe0`). La abre el botón
de la barra (`0x641203`): `C1 04 F4 06` en claro, `0x65f690` y la música (`0x4acf60` con `main_theme.mp3` y
`0x4acfd0` con `Mutheme.mp3`).

`0x63ffe0`, el grupo siguiente, mira la clase del registro de `+0x39a0`: con 0, el sonido `0x19` y
`0x63f9a0`, y sigue; con 1, el título (`+0x39a8`); con 2, el rol (`+0x39ac`); y hasta cuatro 3 seguidos, los
nombres (`+0x39b0..`, la cuenta en `+0x39a4`). Un título sin rol deja el rol de antes (Q-C504), y con más de
cuatro nombres los que siguen van en el grupo de después, con el mismo título y el mismo rol.

`0x63f630`, con el foco: el botón (`+0x582`) llama a `0x63f9a0` sin sonido; si no, Esc (`0x8001`, con
`[input + 0x31]` en 0), el sonido `0x19` y `0x63f9a0`. Y sigue sin volver: `0x6400b0(i, dt)` con cada
renglón y `0x63fe20(dt)`.

- `0x6400b0` (las barras de `+0x340`, `+0x3e8` y `+0x490`; los estados en `+0x39c0..+0x39c8`). Estado 1, la
  barra se va: el alfa menos `_ftol(dt · 0,85)` (`0x6ba8a8`), en 16 bits con signo; en 0 o menos, 0 y el
  estado 2. Estado 2: solo el de los nombres suma dt en `+0x39d0` y, pasado 2.300 (`0x6ba8b0`), lo vuelve a
  0 y pasa los nombres al estado 3, el rol también si lo que sigue no es un nombre, y el título si tampoco es
  un rol. Estado 3, la barra vuelve: el alfa menos `_ftol(dt · −0,85)`; en 255 o más, 255, el estado 1 y,
  con el de los nombres, `0x63ffe0`.
- `0x63fe20` (la ilustración, `+0x5b8`, con el alfa en `+0xa0` y `+0x148`). Estado 1: el alfa menos
  `_ftol(dt · −0,1275)`; en 255 o más, el 2. Estado 2: `+0x5c0` suma dt y, pasado 22.000 (`0x6ba898`), el 3.
  Estado 3: el alfa menos `_ftol(dt · 0,1275)`; en 0 o menos, el par siguiente (`+0x5c8`, después del 8 el
  primero), el estado 1 y `0x63fc50`.

`0x63f9a0`, el cierre: `0x65f6c0` (que llama a `0x63f5a0(0)`), `C1 04 F4 06` en claro por el socket de
`0x6c82a0` (el mismo `send` que `0x641203`) y la música al revés que al abrirlos: `0x4acf60` con
`Mutheme.mp3` y `0x4acfd0` con `main_theme.mp3`. Así que `Mutheme.mp3` es la de los créditos y
`main_theme.mp3` vuelve con el cierre (*inferido* de los nombres y del orden: `0x4acf60` deja una y
`0x4acfd0` pone la otra). Un frame puede cerrarlos dos veces, con el botón o Esc y con el final (Q-C501).

`0x63f6b0` dibuja el fondo, las dos mitades, el adorno y el logo (`0x647650`), los textos con la fuente de
los créditos (`0x5542f0` con el modo 2, centrados en su ancho): el título a 20 y el rol a 46 en `0xffe2e2e2`,
en todo el ancho, y los nombres a 72 en `0xffc1eeff`: uno en todo el ancho, dos en el segundo y el tercer
cuarto (160 y 320 de 640, de W / 4), tres en tercios (0, 213 y 426, de W / 3) y cuatro en cuartos; después
las tres barras, que tapan los renglones, y el botón (`0x66a090`).

Con los créditos abiertos (`+0x4584`), el paso del login (`0x634cf0`) saltea todo el mundo después del
gesto (`0x5fbf60` a `0x633920`), el dibujo (`0x6351f0`) saltea el terreno y los modelos (`0x5fc260` a
`0x5306a0`, con `0x549510`) y sigue con `0x549440(0)` y las partículas (`0x548550(0)`), la de Esc no se
abre (`0x634d77`) y una lista que llega no abre ni la lista ni la barra (`0x4c1af8`).

MuRet: `Data::Text::LoadCredits` (`CreditsTest`, `GoldenCredits`), `UI::CreditsWindow` (`CreditsWindowTest`),
`FontKind::Credits` y `CreditIllustrationSets` (el login suelta el par anterior antes de subir el nuevo), en
la `LoginScene` (`LoginSceneSessionTest.TheCreditsCoverTheLogin` y `TheBarStepsWithTheList`), con el
`F4 06` de `ServerSession::SendCreditsNotice`. Sin la música ni el sonido (P-C79), y lo que contesta el CS,
sin leer (P-C78).

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
- abre el campo del chat (`0x6c8e68 = 1`), vacía los campos de texto (`0x553570`) y el área de mensajes (120
  líneas, `0x555190`) y, si alguno de los dos números de `pkt[5]` de la lista no es 0, escribe una línea con
  `sprintf` y el texto 1240 **como formato** (`0x63420a`, Q-C464), sin nombre (`0x7d3a17c` está en cero) y del
  tipo 1. Los números son los dos nibbles de `pkt[5]` (`0x4c20a0`): el bajo a `0x5767958`, que va primero, y el
  alto a `0x576795c`. En esta edición el 1240 es "A Conta foi enviada para o Char geral %d, Magic Gladiator
  %d". MuRet lo hace en `CharacterSelectScene::PrepareTheCharacters` (CL8, hito 3); el vaciado de los campos va
  con la entrada del chat.

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

**Lo que cuelga y los efectos de la lista** (`0x50db57..0x510ac2`, *estático*, cierre de la CL7). `0x50ba30`
corre con cada personaje de la lista la misma cola que en el juego: no mira la escena salvo `0x510ef0` (la
marca del guild, sólo en la 5) y la luz de lo que cuelga (`0x50db94`), que es `[ebp−0x44]` más `+0xf4`: el 0,4
fijo de la escena 4 (`0x50cc32`) o, en el héroe, el 0,5 que deja la rama de su color (`0x50d231`, Q-C417); con
`+0x375` desde 6, (1; 0,1; 0,1). Así corren el `rand() % 30 + 70` de `0x50db57`, lo que cuelga con sus sprites,
las partículas y los joints de las manos, el conjunto completo con lo que devolvió `0x505820`, y el cuervo de un
Dark Lord que lo lleva en la mano izquierda (`0x510abd` → `0x420df0`, con el trapecio de la selección). El héroe
abre y cierra además el registro propio para su color: 3.848 `rand()` por frame con la cuenta en 0, aunque la
escena después tire ese color (Q-C417). MuRet (`CharacterSelectScene::DrawTheTail` → `Scenes::DrawCharacterTail`,
con las tablas del frame de `SelectionWorld`: las del mundo en un frame del original y sus copias entre frames;
`CharacterSelectSceneTest.TheListRunsTheTailOfTheGameWithTheLightOfSceneFour`,
`TheHeroOfTheListClosesTheOwnRecordForItsColor` y `TheRavenOfALordInTheListIsDrawnAfterHim`).
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
  mapa ([Caminar](#caminar), `SetPlayerStop`). Sentado manda un `C1 05 18` con `0x82`, con el XOR32 en
  línea ([04](04-protocolo.md#la-acción-18)).
- **La luz del héroe en la escena 5** (`0x50ba30`, desde `0x50cc55`) es la del terreno en su lugar,
  **del buffer dinámico** `0x7b4ccc4` (`0x5f99e0`), más `+0xf4..+0xfc`, que `0x511290` deja en 0. En
  la selección es 0,4 fijo, y en el login, 0,3 más el terreno. `0x50ba30` tiene además una rama
  propia del héroe (`0x50cca9..0x50d211`): abre el registro propio en línea (`[0x7681eb4]` por
  `0x57630c8`), lee los bits del color y lo cierra, con 3.848 `rand()` (`0x50d1de`) si la cuenta queda en
  0. Corre en todas las escenas; en la 4, `0x50d219` pisa después el color con 0,5 más `+0xf4` y
  `[ebp−0x44]` con 0,5 (`0x50d231`), la luz de lo que cuelga (Q-C417).

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
  también se prueba directo; el cuervo de la fila 11 es `Scenes::RecreateTransitionRaven` (`0x420d60`, la misma de los traslados: `WorldEntryTest.ARavenIsCreatedAt420d60AfterTheWait`). Lo que MuRet no modela no se escribe: `0x7d36874`, `0x77f3b67`, `0x553780`,
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
`Player.bmd`. En el juego, `GameScene::PrepareFrame`, en un frame del original que llega a `0x510d00`, lo deja
sin valor (`nullopt`) hasta que un jugador llega a `0x50c91e` y publica el suyo (`ScanTheActor`), y `0x550990`
corta con un error si lo lee antes de otro `0x505820` (P-C40). Un dibujo intermedio del modo desacoplado
(`OriginalFramesOfTheSteps::NextDrawIsAnOriginalFrame` en falso) no es un frame del original: no lo borra ni lo
publica, y lo que leen los pasos que siguen, como la C (`0x65ca1e` → `0x550d10`), es lo del último frame
(`GameSceneDrawTest.AnIntermediateDrawKeepsTheScanOfTheLastFrame`). El bloque del dibujo que decide lo que devuelve corre en la
cola de los tres (`DrawGameCharacterEffects`): desde el cierre de la CL7, el login y la selección con el
`FullSetScan` que devuelve `ScanTheDrawnCharacter` (el del último frame del original; entre frames, uno sobre
copias que no publica).

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
viste al héroe publicado. Sin valor (un frame del original del juego en el que ningún jugador llegó a
`0x50c91e`), `RecalculateOwnStats` corta en `0x550990` (`0x5512ac`), con los cinco helpers de antes hechos; con un
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
ayudantes del héroe publicado (`Scenes::TurnOffHelpersOf`). El cuervo del lugar 1 va a
`Scenes::ReplaceTheRaven` (`0x420cf0`): suelta `ActorState::darkSpirit` y, fuera de los mapas 18 a 23, lo
crea de nuevo con `CreateDarkSpirit` en el centro de la celda del héroe, con los 12 `rand()` de sus cierres
(`InventoryReplyTest.TheDarkRavenOfSlot1ReplacesTheRavenAt420cf0`). Las mascotas del lugar 8 van a `Scenes::CreateHelper` con el
`rand()` compartido ([Las mascotas](#las-mascotas)), y el `0xbf` también desde que `0x4225d0` no corta. Fuera
de la zona segura, las que no son el `0x1c12` dejan el calco `0x61e/1` en el héroe (`0x5bca46`,
`World::ModelEffects::CreatePetSummon`).
Cortan con error, nombrando la dirección y después de lo que el original hace antes (el registro se
cierra igual): el anillo de nivel 3 (`0x414340` y `+0x360`), la lectura del nivel con el registro cerrado (Q-C202), una casilla
fuera de su grilla (Q-C200) y `Item.bmd` sin cargar.

**`0x4225d0`, la defensa del Dark Horse** (*estático*, ASM c32bbd5f; desde la auditoría del cierre de la CL7,
2026-11-12). `0x54bf40` la llama en `0x54cc5c`, después de las opciones de magnitud y antes de las tardías de
las armaduras, con el ítem que inicializa; sale si el tipo no es `0x1a04` (`0x4225dc`). Busca la opción `0x96`
(el tag en `D+0x2b+i`) desde la primera; si la encuentra en la primera, sigue como si no estuviera
(`0x422600`, Q-C415). Abre el registro propio (`0x40b240` y, sin acceso, `0x40ad90` con un objeto nuevo de
`0xf09` bytes; con acceso, `+0xf08` + 1 y `0x40b440`; en el camino de agregar, la búsqueda del hash en línea,
`0x42260e..0x4226dd`), escribe el valor en `D+0x33+i` (`i`, la que encontró o la cantidad `D+0x2a`) y lo cierra
(`+0xf08` − 1 y, en 0, `0x40b4d0`, que vuelve a cifrar con 3.848 `rand()`). El valor, en un byte:
`word(registro + 0x1a) / 20` (la agilidad; `imul 0x66666667` y `sar 3`, con signo sobre el word sin signo)
+ 2 · el byte de abajo de `[0x72397c]` (el nivel del caballo, que escribe `0x421db0` con el `A9` de las
mascotas: tipo 1, lugar 8; sin portar, 0) + 5. Después del cierre pone el tag `0x96` en `D+0x2b+i` y, si la
agregó, suma 1 a `D+0x2a`. En la grilla, `0x54bf40` corre con el registro cerrado: son 3.848 `rand()` más. En
MuRet, `Scenes::AddTheDarkHorseOption` (`InventoryReplyTest.TheDarkHorseInTheGridAddsItsDefenseOption`,
`TheWornDarkHorseGetsItsOptionWithTheRecordAlreadyOpen` y `TheDarkHorseOptionSkipsAnEarlierOneInTheFirstPlace`),
y `[0x72397c]` es `WorldRuntime::darkHorseLevel`.

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

El paso de la escena 5 es `0x636290` entero (3.844 bytes), después de `0x623e70`, los diálogos del juego (el menú de Esc y las opciones; [HUD y ventanas](#hud-y-ventanas))
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
El cuerpo de los personajes no pasa por el global: `0x511290` le pone `+0x11c` en 1 (`0x511bf7`) a
cada uno que arma, también al del login (`0x6349a0`), a los de la lista (`0x4c20a0`, `0x4c2290`) y
a la vista previa de crear (`0x63e409`), y `0x5ffdf0`/`0x6001b0` lo posan en `+0x120`
(`0x5fff13`, `0x600259`); al global sólo va lo que cuelga. En MuRet es `ActorState::bodyPose`
(Q-C410).

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
`0x44402a` solo la aplica en la escena 5. Cada ayudante vivo llega al alpha
de `0x4e9980` (`0x444053`) aunque su dueño esté muerto, y después al cuerpo de su
tipo y al avance con el frame + 2 de la escena 4 ([Las mascotas](#las-mascotas)), con
los atributos de la carga del 54. Un tipo que el paso no tiene corta en `0x444083`,
con el alpha que subió; los posteriores al corte no cambian. Con la tabla vacía el
tramo no hace nada. Después de los personajes, `0x63486a` los dibuja (`0x4453b0`),
con la escala 1 de la escena 4, la luz del terreno 54 y los sprites del frame.

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
El cuervo conserva su pose para la chispa del paso siguiente. La zona segura del cuervo (los estados 3 y 4)
está portada desde la auditoría del cierre de la CL7 ([el cuervo](#los-efectos-del-login)); los estados 2, 5
y 6, de las órdenes `A7`, cortan en `0x416add` con los `rand()` anteriores.

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
9. **La habilidad** (`0x4fdb9a..0x50539e`; el detalle, en [El paso del ataque](#el-paso-del-ataque)):
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
| `0x4f97b0` y `0x4f5b70` | 11 y 36 sitios; sin habilidad, 1 (`+0x38e`) y, del frame 5, 3.848 el primero, y 1 (`0x4f5b97`) y 2 lecturas de `+0x38e` el segundo | con `+0x381` distinto de 0; las lecturas, con `+0x3a4` en `[0, 400)` ([El paso del ataque](#el-paso-del-ataque)) |
| las lecturas de `+0x38e` (`0x517630` y en línea) y su escritura (`0x40b0a0`) | 1 cada una | con la cuenta en 0 |
| `0x40f370`, después del sonido | 3.848 | con blanco, salvo los casos de `0x5051f1` y `+0x376` de `0x44` a `0x4b` |
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
| `0x4fb9e0` | 1.058 | muriéndose | `Scenes::StepActorDying`; corta en `0x4fba6d`, `0x4fbba8` y `0x4fbcdd` ([Pegar al llegar](#pegar-al-llegar), "Morirse") |
| `0x4e9980` | 191 | el alpha | `World::StepObjectAlpha` |
| `0x4fcd00` | 473 | la animación | `StepTheAnimation`, con el `0x20a`; corta en `0x4fcd0c` y `0x4fcda2` |
| `0x4f5660` | 1.185 | las partículas del que se muere | `Scenes::SetActorDie`, desde el paso 15 |
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
| `[0x5767948]` | la skill del último tiro (`WorldRuntime::lastShotSkill`) | la escribe `0x53d9a0` (`0x53deb6`); la lee la habilidad `0x31` |
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
  - muriéndose, donde corta `0x4fb9e0` (el `0x192`, las chispas de Chaos Castle y las partículas de la skill
    `0x28`);
  - después de pegar, donde corta el paso del ataque ([El paso del ataque](#el-paso-del-ataque), CL9 hito 3, fase
    2b);
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
| `0x5f9780` | 904 | la luz al buffer dinámico | `World::AddDynamicLight` sobre `WorldState::DynamicLighting` (`0x506b50`) |
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
- la luz de cada mano (`0x506aec..0x506b50`, desde la auditoría del cierre de la CL7, 2026-11-12): con `+0x375`
  debajo de 6, la derecha y después la izquierda, `0x21e` con (0,8; 0,64; 0,4) y radio 3 y `0x225`, `0xa24`, `0xc1c`
  y `0x61f` con (0,64; 0,4; 0,24) y radio 2, por `World::AddDynamicLight` al buffer que `DynamicWorldLight` rehace
  en cada paso (`CharacterStepTest.TheHandLightsAddToTheDynamicBufferBelowPkSix` y
  `BothHandsAddTheirLightsRightFirst`);
- el `rand() & 7` y el caso del `0x20a`, con los pasos solo del héroe;
- corta con error, después de los `rand()` de antes:
  - con otro modelo (`0x505e0b`);
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
| `0x63741d` | en el mapa 30, con el héroe en el rectángulo de `0x42b0a0`, la niebla; si no, `glDisable(GL_FOG)` | la niebla negra lineal de 2.000 a 2.700 para todo el pase, también el del ojo (`0x6213e0`) | `Render::ModelFog` en cada tramo del lote (`ModelBatch::SetFog`), que `GlModelRenderer` aplica ([Los objetos de Castle Siege](#los-objetos-de-castle-siege), Q-C334, Q-C391) |
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
| `0x6375ea..0x63763c` | en el mapa 30, con el héroe **todavía** en el rectángulo, `0x42b0f0`: `glDisable(GL_FOG)` | apaga la niebla antes del 2D; si el héroe salió, queda prendida hasta el `0x620db0` siguiente (Q-C390) | `GameScene::DrawTheWorldPass`, con la segunda lectura |
| `0x637641` | `0x59ec80` | qué personaje está bajo el mouse | [Caminar](#caminar) |
| `0x637646..0x6376e6` | `0x6219b0`, `0x4383f0`, `0x5bbbc0`, `0x5a7320(1)`, `0x5a7eb0`, `0x490390`, `0x5f37f0`, `0x62db60`, `0x5f7bc0`, `0x554810`, `0x5554e0`, `0x5b0550`, `0x40eca0`, `0x65ee60`/`0x65fe00`, `0x5ac2d0` × 2, `0x633110`, `0x621a90`, `0x620fc0` y uno por vtable | el 2D: el HUD, las ventanas, el texto y el cursor ([HUD y ventanas](#hud-y-ventanas)) | CL8 |

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
pose; `0xf4`, después. Los dos, y el `0x10f`, publican su pose sin trasladar, con Translate y padre en 0
(`0x52d33f`, `0x52d4f1` y `0x52d7d2`): los vértices los lleva al mundo `0x4ebc80` (Translate en 1, el
undécimo argumento de `0x60cf70`) y los sprites de `0x60b3d0`, `0x4ec110` (Q-C411). Las cintas permiten count negativo sin desplazar historia y luego
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
  hueso 19, con luz 1 y sin `rand()`. No se ve nunca en la 1.02 (Q-C416): `0x517270` escribe `+0x1d5`
  con su tercer argumento (`[ebp+0x10]`, `0x5172a0`), que es 0 en la lista (`0x4c21d9`) y en el login
  (`0x634a3e`, `EBX` en 0 desde `0x6349e1`), y el byte `[0x7437225]` en la respuesta de crear (`0x4c2341`),
  que nadie escribe (sólo `0x7437224`, la clase);
  los demás escriben 1 (`0x44114b`) o 0 (`0x4c45e1`, `0x4c4dc6`, `0x4c8a10`, `0x4e0c55`). MuRet lo porta
  igual, y no llega.
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
  (0,2; 0,7; 0,9) y el contador en 0 (Q-C353). `ModelEffects::CreateFullSetFlare` lo crea como `0x518cf0` →
  `0x51e489` (vida 30, `+0xd8` en 0, el personaje de dueño en `ModelEffect::ownerContext`), sólo en un frame
  del original; sin la tabla de efectos corta en `0x50fdc3`. Su paso (`0x52a9ad`, `Scenes::StepFullSetFlare`,
  desde `GameScene::StepTheModelEffects`): el `rand()` de la luz de todo efecto vivo (`0x5208c5`), `+0xd8` uno
  más y, con la vida múltiplo de 15 y dueño, el giro z 180 más, la posición a 50 del personaje (leído en
  vivo) con el seno de la suma sin redondear y el coseno del float guardado (Q-C380), 20 más arriba, y ahí
  el joint `619/9`. Después la cola común, sin animación ni velocidad. Vive 30 pasos y suelta dos `619/9`
  (Q-C387); `0x52da84` no lo dibuja.
- **El conjunto completo** (`0x50fddb`, lo que devolvió `0x505820`): el color de `0x608e00` con las botas
  (`+0x298`) y el alpha por 0,5, con un `fstp` por componente; sin `0x4000`, flare01 (`0x5ac`) de 1,3 en
  los huesos b, b − 6 y b − 7 de cada mano. Con el nivel de `[0x743a178]` por encima de 9: las botas de
  `0x182f` a `0x1833` cambian la luz propia por la suya y sacan, por nivel, `rand() & 3`, `rand() % 3`,
  `rand() & 1` o nada (13) para tres partículas `6f1/3` en (0, ∓18 o ∓20, 50) y (0, 0, 70) del hueso 0,
  con la luz de la bota (`0x510158`, `0x51008d` y `0x51020c`). Después, con `rand() % 20` en 0 y la luz en
  1: el 10, la órbita `60f/0` de 0,19f; el 11, con `rand() & 7` la órbita y si no el joint `60f/0` de ancho
  10; el 12, con los pasos múltiplos de 6 dos joints `60f/0x12` de ancho 20 (skill 0 y 1) y si no, de 3, la
  órbita; el 13, los dos `60f/0x12` con 6 y, aparte, con 4 la órbita y el joint `610/7` (Q-C381). Esos
  pasos son `0x7d3a0f0`, que el original lee sin abrir el acceso, con la basura cifrada que dejó el último
  cierre (Q-C125); MuRet usa los pasos de lógica, como en el grito del cuervo (P-C22 a). El 14 y
  el 15, nada (Q-C351). Entre frames sólo corren los `rand()` que eligen; lo que nace en las partículas y
  los joints, sólo en un frame del original. Sin el pool de partículas o sin los joints, corta con la
  dirección de la llamada (`0x510339`, `0x51037c`, `0x5103be`, `0x510435`, `0x51048b` y `0x5104a8`).
- **Las partículas del conjunto.** La `6f1/3` (`0x543f49..0x544153`) nace como la neblina `6f1/0` (giro,
  escala, caída, la luz de la escena y un corrimiento) y encima la textura cra_04 (`0x5ae`), la luz del
  argumento, vida 12, otro giro, vz 2, la escala `(rand() % 3 + 3) · 0,18f` y otro corrimiento: once
  `rand()` (Q-C385). Su paso (`0x547fe8..0x5480ed`) es el de la neblina más la escala −0,038f, vz la suma
  sin redondear menos 0,02f y la luz por 0,98039216f. La órbita `60f/0` (`0x542f74`, `0x5471bd`) nace y
  gira como la `60f/3`, con el lugar del personaje en el pool de dueño (`ParticleOwner`); después de girar,
  con la acción del dueño (`+0x111`) en [`0xd`, `0x21`] o [`0x38`, `0x3c`] pasa a la `60f/1`, que se queda
  donde cayó y se apaga (`0x54735c`, Q-C382). `GameScene` le pasa la acción con
  `ParticleActorActionsAmong`; sin ella, `MissingOwner`.
- **Los joints del conjunto** (`0x530860` y `0x535840`, con el personaje de dueño): el `60f/0` y el
  `60f/0x12` (`0x5341fc`) giran a la velocidad alrededor del destino con `(vida + vy) · 0,1f` (la `0x12`,
  con la vida doble, al revés con el skill 1, y crece) y, con el ancho debajo de 40 y la acción del dueño
  en los mismos rangos, pasan al `60f/1`, sin cola, que no se mueve y se apaga (Q-C383); el `610/7`
  (`0x531e2c`) nace a 40 del segundo punto con `fcos` y `fsin` de `rand() % 360` en radianes (Q-C384) y se
  mece desde ahí con `sen(vida · 0,4f) · vx` mientras sube; el `619/9` (`0x53254f`, `0x5387a9`) gira a vx
  del personaje, leído en vivo, corre la cola lo que el personaje se movió y muere cuando vx baja de 0
  (Q-C386). El `60f/1` con cola corta en `0x53c393`.
- **El efecto de `+0x369`** (`0x510a7a`, `0x510ef0`): en la escena 5, con `+0x88` en 1 y fuera de los
  mapas 11 a 23 y 30, con `+0x369` en 1 el efecto `0x140` cada 5000 ms de `+0x36c` (Q-C352). `+0x369` es de
  los paquetes (`0x4d9260`): corta en `0x510f4d`.
- **El cuervo** (`0x510abd`, `0x420df0` → `0x4182f0`): sin pasar el trapecio, MissTheView. Si lo pasa,
  `0x5ffdf0` (el alpha de Chaos Castle con `+0x1a1` del héroe y, por encima de 0,01, la pose con la luz
  del buffer dinámico) y `0x6002e0`: las mallas 3 y 2 y la sombra sobre la altura de `0x5f93c0`, que no
  va en los mapas 10, 24 a 29 y 36 y va con alpha 0,2 en el 7 (`0x6009a9..0x6009e5`). Con `+0x78 & 0x4000`
  del dueño, `0x4182f0` recibe 10 y `0x6002e0` lo dibuja como la cáscara de cromo de [El caballo
  oscuro](#el-caballo-oscuro) (sin sombra ni alturas, Q-C440); `0x418920` corta en los estados 5 y 6
  (`0x418957`, las órdenes del A7). Se dibuja como lo dejó el paso, igual que el personaje, y en un frame
  del original guarda la pose para la chispa.

`GameCharacterEffectsTest` y `GameRavenTest` cubren cada rama con sus sprites, sus `rand()` (CRT con la
semilla 1), las partículas y los joints de cada nivel del 10 al 13, el `5ac/3` y sus cortes;
`FullSetParticlesTest` y `FullSetJointsTest`, el nacimiento y el paso de cada partícula y cada joint del
conjunto; `GameCharacterRenderTest` y `UnarmedGameCharacterTest`, que la cola no corta sin las ramas.

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
| `6ac/0` (`Effect\fi02.tga`, `0x61d57a`) | `0x543a5d`: vida `rand() % 5` + 25, escala 0,5, giro `rand() % 360` y la velocidad de adelante; después el caso del `6ec` (`0x543ae3`): vida 24, otra velocidad x (`rand() % 10` − 5) · 0,4f y z, y el inicio en la posición (`0x543e7d`) | `0x54793f`: lo mismo con la z +0,6f, la posición corrida (`rand() & 3`) − 2 en x e y y por 0,8f en z, la escala +0,05f, la luz en vida · 0,04f y el giro + (`rand() & 1`) + 1 | `0x5489eb` → `0x548a51`: `0x620c10` (el modo 6: `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`) y el billboard común |
| `6ac/2` (no es de la antorcha: la crea el paso del joint `0x632/6`, `0x53c042`, en `0x535840`) | `0x543a5d`, como la `/0`; la escala 3 (`0x40400000`) del pedido la pisa el 0,5 (Q-C393) | `0x54793f`, como la `/0`, salvo la luz: vida · 0,08f − 1 (`0x5479fb..0x547a0f`, Q-C392) | `0x5489f1..0x548a4c`: `glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD)`, `0x620c10`, el billboard común y `GL_MODULATE` de vuelta: el color es téxel + luz, recortado, y el alfa, el del téxel |
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
modo 6 en `IParticleDrawingServices::Blended` y `ModelBatchParticleBackend`. La `6ac/2`: `GL_ADD` con
`IParticleDrawingServices::TextureEnv` y `Render::TextureEnvMode`, que cada tramo del lote lleva aparte del material
(`ModelBatch::SetTextureEnv`) y `GlModelRenderer` aplica en el fragment shader; nadie la crea todavía, porque el joint
`0x632/6` no está portado ([08](08-pendientes.md) P-C53). Las texturas, `EffectTextureSet`. Con
la Data del repo, el 9 (y el 32) y los siete Blood Castle llegan a un cuadro completo y caminan (`GameT16SceneMatrix`;
`GameT16MapWalk`, en (122, 174) con 38 del 2 cerca y en (12, 90) con cuatro `0xd` y cuatro `0x25`;
`GameT16SafeZoneWalk`, en (12, 3) de los Blood Castle; Devil Square no tiene zona segura). Tests:
`MapLoadEventTest`, `EventMapObjectsTest`, `EventMapObjectFrameTest`, `BloodCastleParticlesTest` (la `6ac/2` creada,
con su paso y su dibujo, en `TheBrightFlameIsCreatedSteppedAndDrawnWithAdd`),
`ParticleDrawingTest.TheBrightFlameDrawsItsBillboardBetweenAddAndModulate`,
`GlModelRendererTest.TheAddTextureEnvSumsTheTexelAndTheColor` (solo en el CI) y
`CharacterStepTest.DevilSquareRainsFromTwoBonesOfEachCharacter`.

### Los objetos de Chaos Castle

Leído en la CL7 en el ASM y el pseudocódigo de `c32bbd5f` (*estático*; las cuentas de MuRet, *simulado*: con el
port y la Data del repo). Lo común es lo de [Los objetos del juego](#los-objetos-del-juego). Los seis Chaos Castle
son los mapas 18 a 23, todos con `World19` (194 objetos: 22 del 3, nueve del 6, 14 del 7, seis del 8, ocho del
`0xc` y los demás repartidos del 0 al `0x23`). El castillo, de 23 a 44 en x y de 75 a 108 en y, es todo zona segura
en el `.att`; afuera, la franja de x 1 a 12 con y 70 a 111 se camina y no es segura. La entrada por `1C` corta en
el objeto de los comandos (`0x4c7b34`, CL9.4), después de esconder las ventanas viejas (hito 7 de la CL8); la de
`F3 04` entra. El objeto de evento (`0x404d60`) y el área (`0x6c634c`, `0xff` en la
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
el `0x35`, lo del 0 sin asedio; el 0, el `0x34` y el `0x36`, nada. Todas van a `0x53fe70` con la posición y los
ángulos del objeto (`+0x10` y `+0x1c`), la luz (1, 1, 1), su escala (`+0xc`) y sin dueño, menos la salpicadura, que
va con escala 1.

**Las partículas del 30.** Las texturas son de las que `0x61ccb0` sube en todos los mapas: `Effect\fantaF.jpg` en
`0x6ec`, `Effect\WATERFALL5.jpg` en `0x6f4` y `Effect\smoke01.jpg` en `0x5f2`. Ninguna mira la escena ni el dueño.

| Partícula | Nace | Paso | Dibujo |
|---|---|---|---|
| `6ec/0` | `0x543ae3` → `0x543b82`: la de la `6ed/0` de Kalima (vida 24 y los dos `rand()` de la velocidad) | `0x547a40`: el de la `6ed/0`; el subtipo 0 salta las ramas del dueño (de la 1 a la 4) | el default de `0x548550` (`0x54889a`): el billboard común con el giro de `+0x44` |
| `6f4/6` | `0x543f04..0x543f47`: vida 30, el giro `rand()` % 360, la escala del argumento más 0,6f (`0x6b8a20`; la `6f4/0` tiene 1,6f), la caída −12 − `rand()` % 5 (la `6f4/0`, −7 − `rand()` % 5) y la luz en 0,2f | `0x547fa4`: no es la /0 ni la /5, que restan 0,005f a la escala y suman 0,1f a la caída: salta a `0x547fc1`, que solo cambia la luz, por 1,1f con la vida en más de 20 y por 0,8333f con menos de 8 (Q-C370) | el default, como la `6f4/0` |
| `5f2/0x15` | la `5f2/21` de Crywolf (`0x542370`) | el de la `5f2/2` (`0x5466c3`) | resta, como la `5f2/2` |

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
(Q-C334). `0x42b0a0` es `glEnable(GL_FOG)`, `glFogfv(GL_FOG_COLOR)` con los tres ceros de `0x63746b..0x63747a`,
`glFogf(GL_FOG_MODE, GL_LINEAR)` (`0x46180400`, 9.729,0), `GL_FOG_START` en 2.000 (`0x44fa0000`) y `GL_FOG_END` en
2.700 (`0x4528c000`). GL la aplica a cada fragmento después de la textura: f = (2.700 − d) / 700, recortado a
[0, 1], y el color f · C (el negro pesa 1 − f); el alfa no cambia. **Qué recibe niebla:** todo lo que se dibuja
entre `0x637481` y `0x63763c`: el terreno, las marcas del suelo, los objetos, los efectos del suelo, los pájaros, los
personajes, los ítems del piso, los peces, los helpers, las hojas, los joints, los efectos, las cintas y, después
del `0x6213e0` (que no toca la niebla), los sprites, las partículas y los números de daño en el ojo. La segunda capa
de `0x637577..0x6375e5`, con su `0x620db0` que la apagaría (`0x620fa5`), es de Kalima (`0x437850`) y en el 30 no
corre. Ninguna mezcla de `0x620980` a `0x620c90` la toca. **Qué no:** el 2D, que empieza en `0x6219b0` después de
que `0x42b0f0` (`0x6375ea..0x63763c`) la apaga, con una segunda lectura del rectángulo (Q-C390). **La distancia:**
con `GL_FOG_COORDINATE_SOURCE` en `GL_FRAGMENT_DEPTH` (nadie lo cambia), la del ojo, que la especificación deja
aproximar con |z| del ojo; `0x42b0a0` no pide `GL_FOG_HINT` (Q-C391). Los personajes: `0x412140` arma, con
`0x6c649a` en `0xff` (sin gremio, Q-C331), el panel de la clase 0 en `0x5763530`, que dibuja la interfaz (CL8); con
otro puesto, los paneles 1 y 2 (`0x412193`). Después de cada `0x20a`, `0x42afe0` llama a `0x42ddb0(personaje, 380)`
solo con el asedio (`0x42b00b`). Las capas del 6 al 13 de lo que cuelga (`0x4142f0`) solo se dibujan con el asedio
(`0x414330`).

**En MuRet.** La carga: `Scenes::LoadMap` (`PrepareTheCastleSiege`) con `MapLoadContext::requestCastleSiegeState`,
que `WorldEntry`, `WorldTransitionSupport` y `SessionEventApplier` atan a `App::ServerSession::RequestCastleSiegeState`
(`Protocol::WriteCastleSiegeState`); `WorldModels::LoadTheSpearman`; `GameTailTextureSet` y `GameModels`
(`TowerGatePlane`). El paso: `World::StepCastleSiegePrelude`, `World::StepCastleSiegeObject` y
`World::StepCastleSiegeSeenObject` (`CastleSiegeObjectStep`), con `World::ApplyCastleSiegeObjectState`. El frame:
`Scenes::StepCastleSiegeObjectFrame` (`CastleSiegeObjectServices`). El dibujo: `Scenes::DrawCastleSiegeBody`
(`CharacterDrawing`) desde `DrawGameWorldObjects`, y `Render::Models::ShadowSpreadOf`. El efecto: `StepGatePlane` y
`ModelEffects::CreateGatePlane`. La niebla: `World::InTheCastleSiegeFog` y `Render::ModelFog`, que
`GameScene::DrawTheWorldPass` pone en el lote con `ModelBatch::SetFog` y saca después de la cola; cada
`ModelDrawCommand` lleva la suya, `ModelBatch::Begin` con el material la apaga (`0x620db0`) y `GlModelRenderer` la
aplica en el fragment shader con la w del clip como |z| del ojo (Q-C391). El panel: `WorldRuntime::siegePanelKind`
y `heroGuildPost`. Las partículas: la `6ec/0` y la `6f4/6` en `World::CreateKalimaParticle` y
`World::StepKalimaParticle` (`KalimaParticleKernels`, con `World::BrazierFallSubtype`), y el humo `5f2/0x15` con el
de Crywolf (`World::CrywolfSmokeSubtype`); el frame las suelta con `Particles::CreateChecked`. Corta lo del asedio
prendido ([08](08-pendientes.md) P-C53); su `0x2a` ya suelta el humo. Con la Data del repo, el 30 llega a un cuadro
completo y camina (`GameT16SceneMatrix`; `GameT16MapWalk`, de (65, 130) a (68, 133), fuera de la niebla, que queda
apagada, con dos antorchas 0 y trece braseros `0x34` a la vista y sus chispas y caídas en el último cuadro,
*simulado*; y en (165, 191), adentro del rectángulo, un cuadro completo con la niebla en todos los tramos de los dos
pases). Tests: `CastleSiegeObjectsTest`, `CastleSiegeObjectServicesTest`, `CastleSiegeParticlesTest`,
`CastleSiegeDrawingTest`, `MapLoadTest.CastleSiege*`, `WorldProtocolTest.CastleSiegeStateWritesTwoWithXor32AndWithoutC3`,
`GameT16Journey.CastleSiegeAsksForItsStateBeforeConfirmingTheTeleport` y
`GameT16Journey.CastleSiegeFogsTheWholeWorldPassInsideItsRectangle`; la niebla en el lote, `ModelBatchTest`, y en GL,
`GlModelRendererTest.TheLinearFogMixesByTheEyeDistance` y `TheFogHasItsColorAndTurnsOff` (solo en el CI).

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
| `0x48` | ninguno | con `rand()` % 10 en 0, otro `rand()` para la luz L = ((`rand()` & 3) + 3) · 0,1f en la x87 (`fst` del rojo; el verde por 0,6f y el azul por 0,2f desde L sin redondear) y `0x53fe70(0x5f2, (x, y, z + 50), ángulos, luz, 33, escala, 0)` (`0x4308ca..0x430932`) |
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
| `5f2/33` | `0x541a8d..0x541b03`: la escala del argumento más 0,1f (`0x6b87b8`), la vida `rand()` % 150 (puede ser 0), la inclinación (`+0x20`) `rand()` % 90 − 45, el giro (`+0x28`) `rand()` % 360 y la velocidad (0; −(`rand()` & 7); 0), que mueve el prefijo (`+0x4c`) | `0x545ffc..0x54604c`: la luz (V · 0,4f; V · 0,4f; V) con V = vida · 0,125f en la x87; la velocidad por 0,001f (`0x6b89c8`); la escala más 0,02f (`fst`) y la z más esa suma sin redondear (Q-C371) | sin restar (no está en `0x548707..0x548742`): el billboard común con su giro (`0x548a64`) |
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
celda. El humo `5f2/33` del `0x48`: `World::CrywolfRisingSmokeSubtype` en `Particles`. Corta, con la dirección: la
carga con el estado en 1 o más (`0x61a6f4`), el `0x51` con el estado en 2 (`0x430a40`) y los casos de monstruos de
`0x4339d0`. Con la Data del repo, el 34 llega a un cuadro completo y camina (`GameT16SceneMatrix`; `GameT16MapWalk`,
en (128, 30): suelo libre de `EncTerrain35.att` en la zona segura del fuerte, con tres `0x29`, cuatro `0x38`, el
`0x49`, el `0x4a` y los `0x52` vistos). `EncTerrain35.obj` no trae ningún `0x48` (*simulado*): su humo no sale en la
caminata y lo cubren `CrywolfObjectServicesTest` y `CrywolfParticlesTest`. Tests: `CrywolfObjectsTest`,
`CrywolfParticlesTest`, `CrywolfDecalTest`, `CrywolfWaterTest`,
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
- En cada paso, primero los diálogos del juego (`0x623e70`) y después `0x636290`, que antes de mover al
  héroe corre el bloque de UI `0x636ded..0x63705b`, donde las ventanas consumen sus clics (`0x7d366d4`
  tiene más de 100 escritores; [HUD y ventanas](#hud-y-ventanas)), con `0x59adf0` (el `MoveHero` de MuMain: el clic, el
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
- **El derecho sobre el elegido** (`0x59afdf..0x59b185`, entre el contador y la cabeza; *estático*, CL9 hito 1). Con
  la ventana de una carta (`+0xf0` de `[0x5763514]`, si `0x490a00` la encuentra) y el flanco del derecho
  (`0x7d36880`), el nombre del elegido (`+0x1d9` del lugar `0x6c8e30`, sin mirar el −1) va al campo de esa ventana
  (`+0xb4`, sus métodos `+0x4c` y `+0x30(1)`). Después, con el chat abierto (`0x6c8e68`), el héroe en la zona segura
  (`+0x3e2`), un elegido, el derecho y `+0x376` del elegido en `0xff` (solo lo escribe `0x5140c0`, el alta de los
  monstruos): el derecho se gasta (`0x59b092`); en Chaos Castle (18 a 23), nada más. Si no, `0x4694c0(0, 0, 1, lugar)`
  con el lugar de ahora del historial de los susurros (`0x77f31cc + 0x6c8ea8 · 0x100`), **antes** de las copias
  (Q-C804); el nombre al buffer del susurro (`0x7798cf8`), del buffer al lugar, el buffer en 0 (0x100 bytes) y del
  lugar al buffer; su largo en `0x7758594` y el sonido `0x19`. Sin `rand()`. MuRet: `NameTheChosen`
  (`GameMovement.cpp`); la carta es del hito 10 y corta en `0x59b003` (P-C162).
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
  y 18 a 23). Un clic dentro de la ventana de `MouseUpdateTimeMax` queda pendiente mientras el
  botón sigue abajo, porque `MoveHero` no lo borra (Q-C166); el `WM_LBUTTONUP` lo apaga
  (`0x4b3ce3`). Con un botón abajo una hora (`(T − 0x77f3b44) · 0,001 ≥ 3.600`)
  suelta los dos (`0x59b8e7..0x59b921`, Q-C167).
- **La acción del héroe** (`0x59babe..0x59bb1f`): sigue si la acción (`+0x111`) no es `0xc2` (el
  golpe), `+0x80` no es 1 ni 2, el alpha es ≥ 0,7 y la acción es < `0x22` o > `0x97`; o si la acción
  está en `0x48..0x4b`, `0x6a..0x7d` o `0x8a..0x8c`. Si no, el clic espera: pegando no se camina. El
  tramo del ítem en el cursor (`0x59bb25..0x59cd28`) lee las manos del registro propio
  (`0x59bbe9..0x59bbf0`: la derecha en `+0xb6c`, la izquierda en `+0xbb4`). Solo con el cursor vacío
  (`[0x7879fbc]` ≤ 0), la derecha vacía (−1) y en la izquierda un arma de los grupos 0 a 2 o del 5
  (`0..0x5ff`, `0xa00..0xbff`) pasa esa arma al cursor (`0x59bdf6`), viste al héroe (`0x512a30`) y
  manda el `24` que la lleva a la derecha (`0x59bf1c`, CL8; MuRet corta en `0x59bd3e`), en `C3`: el
  relleno en `0x59ca54` y el serial en `0x59caac`, 2 `rand()` más los cierres del registro propio del tramo. Con
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
  no hay otra cosa, al terreno (`0x59cd2e`). `0x558bb0` da 0 sin elegido o con un ítem en el cursor
  (`[0x7879fbc]` > 0). En MuRet la primera fila es `AttackTheChosen` (`GameMovement.cpp`, CL9 hito 3, fase 2a;
  [Pegar al llegar](#pegar-al-llegar), "El clic de ataque") y las otras tres no pasan: sus lugares siguen en −1
  hasta la hito 2 y la CL9.3 (P-C162).
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
con enteros salvo la rama de `fDistance`, que caminar no usa: solo el elfo que se acerca, `0x566271`):

- **Sale con falso con `sx == 0` o `sy == 0`** (`0x4eadc1..0x4eadce`, Q-C156; *emulado*).
- **La meta**: con `bClearGoal` pone `Map[meta] = 0`, **escribe el mapa** (solo el asedio,
  `0x435a00`, en el mapa 34 con un blanco de tipo `0x204..0x208`); con `bErrorCheck`, una meta con
  `Map ≥ iWall` sin el `0x20` falla; si no, `NodeFlag[meta] = 4`.
- **La meta por distancia** (`0x4eae7c..0x4eb054`, CL9 hito 3, fase 2a; `FCOMP` con C3 en `0x4eadd4`: una
  `fDistance` que no es 0, o NaN, va por acá y no lee `bClearGoal`): r = `_ftol(fDistance)`; en cada fila
  dy de −r a r, las celdas con |dx| ≤ r − |dy| (el rombo) y, de las de afuera hasta |dx| = r, las que tienen
  `!(√(dx² + dy²) ≥ fDistance)`. Cada una por su índice lineal (`(ty + dy) · 256 + tx ± dx`): cerca del borde
  sigue por la fila de al lado, y fuera de los 65.536 no marca. Con `bErrorCheck` marca solo las que tienen
  `Map < iWall`. Con 1,8 (r = 1) es el cuadrado de 3 × 3; con 2,0 (r = 2), el rombo sin (±2, ±1). MuRet:
  `GameLogic::PathFinder::Find` con `distance` (`MarkTheRadius`).
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
  las ramas restantes están en el presupuesto de Q-C179, no en cada llamada incondicionalmente. Los
  demás monstruos (`0x4f553d..0x4f55ac`, CL9 hito 3, fase 2c): la WORD 2 o la 3 de la tabla de su
  modelo, como al pegar ([Los sonidos del modelo](#los-sonidos-del-modelo); con la WORD 2 en −1,
  nada y sin `rand()`).

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
`Protocol::Movement::WriteAction(dir, 0x7a)`/`ServerSession::SendFacing` publican los bytes crudos sin
normalizar direction. Sus 12 tests cubren 39→40, guardas, sector/borde 112,5, modo/ventana, orden de
campos antes del envío, wrap y errores con prefijo conservado. Las pruebas wire son de sesión y
protocolo; el llamador conserva cabeza→rama quieta/movimiento→gates→clic→57cfc0. Desde el hito 1 de la
CL8, `MoveHero` arma centerX con `PanelRegistry::WorldWidth() / 2` (`0x59b188`), lee `0x775857c` de la UI
del paso (`GameUiState::mouseOnUi`) en `0x59b37a` y `0x59b853`, y la ventana `0x40000` del registro
real. La guarda de blanco6c8e3c carece de productor UI portado. Tampoco implementa las interacciones de
llegada de CL9.

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
  otros: gasta `rand()` (Q-C179). Elegido, un jugador sale salvo con los subtipos 209 a 211, que piden
  el `0x7b`; esos y los que no son jugadores piden después la WORD 0 o la 1 de la tabla del modelo
  (`0x4f3edc..0x4f3f6c`, con `rand() % 2` y sin él con la WORD 0 en −1; [Los sonidos del
  modelo](#los-sonidos-del-modelo)).

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
   la `0xca` y el `C1 05 18` con la dirección de su propio ángulo (`+0x24`, no el del héroe) y `0x82`, con el XOR32
   en línea ([04](04-protocolo.md#la-acción-18)), y sigue al sufijo. La rama está dos veces, volando (`0x4f23e3`,
   el paquete en `0x4f2416`) y en el piso (`0x4f2801`, en `0x4f2834`), hasta `0x4f2bea`.
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
todos los mapas desde el PR de Atlans: en el 7 y en Kalima, fuera de la zona segura, de pie nadan), sentarse
cuando quien llama no presta quien mande el `C1 18` (desde la CL9 lo prestan el paso del héroe y el `18`) y la tabla
de sonidos.

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

### Pegar al llegar

CL9 hito 3 (2026-11-13), *estático* salvo lo que se marca; los paquetes, en
[04](04-protocolo.md#pegar-el-daño-y-la-muerte-d7-15-16-17-y-9c). MuRet: `Scenes/HeroAttack.cpp`,
`Scenes/ActorCombat.cpp` y `Scenes/CombatReplies.cpp`.

- **Si el elegido se puede atacar** (`0x558bb0`, el cursor 4 de la tabla de arriba y el clic de
  `0x59d3de`): no con un ítem en el cursor (`[0x7879fbc] > 0`, con signo) ni sin elegido
  (`0x6c8e30` en −1); en Chaos Castle (18 a 23), cualquiera que no sea el héroe. Un monstruo (`+0x88`
  en 2) sí, salvo con `0x576796c` y sin `0x576796d` (la guerra de gremios). Un jugador (`+0x88` en
  1): con el asedio (`0x4294b0`), los puestos de `+0x360` enfrentados (6 a 11 contra 12 y 13) o las
  marcas de `+0x78` deciden por el escudo `0x4000` del elegido; si no, `+0x370` en 2 u 8; con
  `0x576796c`, el PK de 6 o más y gremio compara los nombres de `0x787aa55` (hito 8, corta en
  `0x558da4`); en el duelo (`0x78abad8`), solo la clave de `0x78abadc` con el nombre de `0x78abae0`
  (`strcmp` contra `+0x1d9`); con `0x576796c`, `+0x374` en 2 y que no sea el héroe; el PK de 6 o más;
  y si no, Ctrl abajo (`GetAsyncKeyState(0x11)`, `0x558e7f`) y que no sea el héroe.
- **El freno de 300 ms** (`0x576791c`, `0x57f2d1..0x57f2f9`, y el mismo en `0x559890`, `0x55b6f0`,
  `0x55cbc0` y `0x55faa0`): salvo la skill `0x28`, si `|GetTickCount() − [0x576791c]|` (resta de 32
  bits con signo, `CDQ`/`XOR`/`SUB`) es 300 o menos, la skill no sale; si sale, guarda un
  `GetTickCount()` nuevo (la segunda llamada). Lo usan las skills de `0x57cfc0` (hito 4).
- **`0x5650c0(héroe, héroe, 0)`**, al final del camino (`0x59b2d2`), salta por `+0x379` (`MOVSX`)
  menos 1 a la tabla `0x56ab20`: 1 levanta el ítem del piso (`0x566755`, hito 5), 2 habla con el NPC
  (`0x5674c8`, hito 2), 3 pega, 4 usa el objeto del mapa (`0x5695b4`) y 5 (`0x56601f`); otro valor
  no hace nada. **El caso 3** (`0x565107..0x56601e`):
  1. El alcance, en celdas: 1,8; 2,2 con la derecha en `0x600..0x7ff`; 6 con las ballestas de la
     derecha (`0x808..0x80e`, `0x810`, `0x812..0x9ff`) o los arcos de la izquierda (`0x800..0x806`,
     `0x811`, `0x814`, `0x815`); 7 con `+0x376` en 9. Las armas son las del registro propio
     (`+0xb6c` y `+0xbb4`, 3848 `rand()` al cerrarlo).
  2. Sale sin blanco (`0x6c8ebc` en −1) y si el héroe ya pega: un jugador con la acción `0x8d` (de
     `0x22` a `0x97` solo esa, Q-C1162), otro modelo con la 3 o la 4.
  3. La celda del blanco, `ftol(posición · 0,01f)`, va a `0x77e1ba8` y `0x77e1bac`; en la zona
     segura (`+0x3e2`) sale ahí.
  4. La distancia del héroe al centro de esa celda, en la x87, contra el alcance · 100: menor, igual
     o unordered está en alcance. Fuera, solo el elfo camina hasta el alcance (`0x56535b..0x566299`: la Y
     y la X de su celda lógica, 4 `rand()` cada una, y `PathFinding2` hasta la celda del blanco con el alcance
     como `fDistance`; con camino, `+0x378 = 1` y nada más, **sin `SendMove`**, Q-C1164); los demás salen.
  5. En alcance: `MouseUpdateTime = MouseUpdateTimeMax`, `0x4f4a10`, `+0x381 = 1`, la posición del
     blanco en `+0x3a8` y el yaw hacia él (`0x4e93f0`); la celda del héroe publicado (`0x743a170`,
     X e Y protegidas, 4 `rand()` cada una) sale en el `D3` de girar; `+0x378 = 0`, las celdas del que
     pega se abren y se cierran (4 y 4), `+0x3a4` toma el blanco y, si el nombre del héroe publicado
     no contiene `webzen` (`0x6c8ff0`, una búsqueda de subcadena), sale el `D7`.
- **`0x4f4a10`, SetPlayerAttack.** Un jugador: primero `0x4f3f80` (las velocidades de `Player.bmd`
  desde el registro propio, 3848 `rand()`); con Fenrir fuera de la zona segura, la acción por las
  armas (`0x56` a `0x61`) y el Dark Lord la pisa con `0x59`; con el caballo oscuro, `0x50`; con
  Uniria o Dinorant, `0x32` a `0x37`; a pie, por las armas, con la marca `+0x1e` de Item.bmd para las
  de la derecha y `rand() & 1` donde el original sortea. Los monstruos: `0x27` y `0x33` crean efectos
  (CL11); `0x28` la acción 1 y el sonido `0x10`; `0x172`, `rand() & 7` y, de 3 en adelante,
  `rand() & 1 + 3`; los demás pasan por los cinco ayudantes, todos y en su orden (abajo, "Los ayudantes de los
  monstruos"), y si nadie atiende, 3 o 4 por `+0x38f % 3`, con `+0x38f + 1`. Con el frame en 0 (`0x4ea030` lo vuelve a 0 al cambiar de acción),
  el sonido: el del arma del jugador, o el de la tabla del modelo (`0x5769ad8 + tipo · 0xe0`, las diez WORD
  de `+0xaa`): con la WORD 2 en −1, ninguno; si no, `rand() % 2` (`0x4f5222`) elige la 2 o la 3, más 5 con el
  subtipo 9 y más `0xd2` (`0x4f51f1..0x4f5262`). Los subtipos `0xd1` a `0xd3` piden antes `rand() & 1 + 0x3c` y
  siguen. Al final, `+0x38f + 1` siempre. El sonido queda en `WorldRuntime::lastSoundId` (CL10), como los demás.
  La tabla la escribe el alta de cada monstruo ([Los sonidos del modelo](#los-sonidos-del-modelo)); de un modelo
  que nadie escribió, el `rand()` sale igual y no se pide ninguno (P-C253).
- **Los ayudantes de los monstruos** (`0x4f5034..0x4f50b2`, CL9 hito 3, fase 2c; MuRet: `MonsterAttack` en
  `ActorCombat.cpp`). Cada uno devuelve AL en 1 si atendió, y con alguno en 1 no hay acción por `+0x38f` ni
  `+0x38f + 1` (`0x437580` en 1 salta directo a `0x4f50b8`):
  1. `0x43a2f0`, en cualquier mapa, por `+0x376` (los bytes de `0x43a644` sobre `+0x376 − 0x91` y los saltos de
     `0x43a628`): `0x91`, `0xaf`, `0xb7`, `0xbf`, `0x105`, `0x10d` leen `+0x38e` y con 1, `0x11`, `0x37`, `0xc8`,
     `0xc9` o `0xca` (la tabla de `0x43a6d0`) toman la 4, si no la 3; `0x93`/`0x94` y sus grupos, `0x11` la 4 y
     otro la 3; `0x95`, `0xb3`, `0xbb`, `0xc3`, `0x109`, `0x111`, 1 la 4, `0x11` la 3 y otro **ninguna** (Q-C1172);
     `0xa0`, `0xa1`, `0xb4`, `0xb5`, `0xbc`, `0xbd`, `0xc4`, `0xc5`, `0x10a`, `0x10b`, `0x112` y `0x113`, `rand() &
     1 + 3`. Cada lectura cierra la entrada antes de la acción (un `rand()` en 0).
  2. `0x42dcb0`: el `0x19e` y el `0x19f`, `+0x110 = 3` y la acción 5.
  3. `0x443750`, en el 31 (`0x43ff10`) con el `0x123`: con `+0x38e` abierto, `'2'` (`0x32`) la 3 y otro la 4, y
     después lo cierra (`0x443946..0x443974`). Atiende siempre.
  4. `0x429270` (los mapas 33 y 54, `0x425070`) con `0x130`, `0x131` y `0x135`, y `0x437580` (el 34, `0x4303c0`)
     con `0x154`, `0x155`, `0x158`, `0x159` y `0x15d` (la tabla de `0x4375cc`): `0x4f92f0`, que abre `+0x42e` (un
     byte protegido que escribe el `0x69`, `0x4cc290`, sin portar), busca su valor sin signo entre las cinco skills
     de `+0x376` en `0x7437670 + +0x376 · 0x18` ([MonsterSkill.bmd](05-formatos-de-datos.md#monsterskillbmd)) y,
     en el lugar 0 a 4, pone la 3, la 4, la 8, la 9 o la `0xb` (la tabla de `0x4f9564`) y atiende; encuentre o no,
     deja `+0x42e` en `0xff` (`0x4f9476`) y cierra la entrada (un `rand()`). `0x511290` no toca `+0x42e`: un lugar
     nuevo lo tiene en 0, que un tipo sin registro encuentra en la `.bss` (Q-C1173).
- **`0x4f5660`, SetPlayerDie.** El héroe pone la vida propia (`+0x22`) en 0 con el registro abierto;
  el jugador toma la `0xc3`, los modelos de efecto (`0x20a` con el subtipo `0xd1..0xd3`, `0x16b` y
  `0x170`) crean partículas (CL11) y los demás la 6. Con el frame en 0, el sonido: en Chaos Castle
  `rand() & 1 + 0x5e`; un jugador `0x6e` el elfo, `0x1c7` el Dark Lord y `0x6c` los demás, y con
  Fenrir `0x278`; un monstruo, la WORD 4 de su tabla (`+0xb2`; −1, ninguno), más 5 con el subtipo 9 y más
  `0xd2`, sin `rand()` (`0x4f5aa8..0x4f5ae8`). Después, el frame en 0,001.
- **El `17` en los castillos** (`0x4d07c0`, en los mapas 11 a 23): con el `TerrainWall` de la celda
  del muerto en 8 y Chaos Castle, cae al vacío (dos `rand()`, el sonido `0x60`); con `0x20`, lo
  empujan del puente hacia el lado de la celda vecina con el 8 (cinco `rand()`). El héroe devuelve
  el objeto del evento a `0x40ba20`. Fuera de los castillos, la skill `0x28` o `0x3b` lo empuja (dos
  `rand()`) y lo gira hacia quien lo mató. El vuelo lo mueve `0x4fb9e0` (abajo, "Morirse").

- **El clic de ataque** (`0x59cd49..0x59d8be`, CL9 hito 3, fase 2a; MuRet: `AttackTheChosen` en
  `Scenes/GameMovement.cpp`). Con el héroe fuera de la zona segura y `0x558bb0` en 1 (arriba):
  1. El elfo (`+0x1d4 & 7` en 2) que camina (`+0x378`) sin interacción (`+0x379` en 0), con una ballesta en la
     derecha (`0x808..0x80e`, `0x810..0x9ff`) o un arco en la izquierda (`0x800..0x806`, `0x811`; acá sin el
     `0x814` ni el `0x815`, Q-C1167), lee su celda lógica, la Y antes que la X, y manda `C1 05 DF X Y`
     ([04](04-protocolo.md#pegar-el-daño-y-la-muerte-d7-15-16-17-y-9c), Q-C1163). Las manos, del registro propio.
  2. `0x59d3de..0x59d3fd`: el blanco (`0x6c8e30`) a `0x6c8ebc`, `0x6c8e38` en 1 y `+0x379 = 3`; la celda del
     blanco, `ftol(posición · 0,01f)`, a `0x77e1ba8` y `0x77e1bac`.
  3. `0x558800(x, y, tx, ty)` desde la celda lógica (la Y y la X, 4 `rand()` cada una): un Bresenham sobre los
     WORD de `0x7d0d348`, por índice lineal desde `0x5f82a0(x, y)`, con el eje largo el de la diferencia mayor
     (con las dos iguales, la Y), que mira de la punta a la punta, las dos incluidas: tapa una celda con el WORD
     en 4 o más y sin `0x20`, `0x40` ni `0x80` en su byte bajo. Si no lo ve, nada más (el final normal,
     `0x59e248`), con el blanco y `+0x379` ya escritos. MuRet: `World::CanSeeTheCell`.
  4. Si lo ve, otra vez la Y y la X y `PathFinding2` hasta la celda del blanco (en `+0x3e8`). Con camino, las
     manos en otro acceso: sin ballesta en la derecha, sin arco en la izquierda (ahora con el `0x814` y el `0x815`)
     y con `+0x376` distinto de 9, `SendMove` (`0x56acd0`) y el final normal.
  5. Si no (sin camino, o con arma de tiro), `0x561c50` (abajo): con flechas, `0x5650c0(héroe, héroe, 1)`, que no
     lee el tercer argumento (Q-C1165), y el final normal; sin flechas, `MoveHero` sale en `0x59e2c0`, sin la suma
     de `MouseUpdateTime`, sin `0x57cfc0` y sin la capa del héroe.
- **`0x561c50`, las flechas al pegar** (`Scenes::CheckTheArrows`). Lee las manos en un acceso al registro propio.
  Con una ballesta en la derecha (`0x808..0x80e`, `0x810`, `0x812..0x9ff`: acá el `0x811` no lo es, Q-C1167)
  mira la izquierda; si no, con un arco en la izquierda (`0x800..0x806`, `0x811`, `0x814`, `0x815`), la derecha.
  En otro acceso, si ahí están los virotes (`0x807`) o las flechas (`0x80f`) con durabilidad (`+0xbce` o `+0xb86`)
  da 1; si no, `0x561840` (que busca otras en el inventario y las equipa, con el registro todavía abierto: sin el
  corte de Q-C773) y 0. Sin arma de tiro, 1.
- **El ataque automático** (`0x6c8e3c`, la opción 386; fuera del mapa 6 y de Chaos Castle). En `0x59b8a7` el
  clic de `MoveHero` cuenta como apretado con `0x6c8e38` en 1 y un elegido (`0x6c8e30` distinto de −1), y en
  `0x59b22f..0x59b261`, con `0x6c8e38` distinto de −1, la cabeza va a 0 y el héroe quieto no gira (`BL` en 0).
  El picking de los jugadores (`0x59ec80`) vuelve a poner `0x6c8e38` en −1 en cada frame: en la práctica solo
  repite el clic sobre un monstruo. `0x57d1d9` (`0x57cfc0`) lo compara con 2, que solo escriben las skills
  (hito 4).
- **Morirse** (`0x4fb9e0(c, o, b)`, desde el cuerpo en `0x4fd722`, con `+0x389` distinto de 0; CL9 hito 3, fase
  2a; MuRet: `Scenes::StepActorDying` en `Scenes/ActorDying.cpp`). Apaga `0x1000`, `0x4000` y `0x20` de `+0x78`.
  `+0x3c8` sube 0,02 por paso (0,05 el `0x172` en el mapa 7; 0,01, `0x6b88d4`, el `0x192` con `+0x60` del objeto
  en 100 o más, con signo, `0x4fba6d..0x4fba7b`: lo escribe su alta, `ActorState::dword60`). Desde 1, el alpha es 1 − (`+0x3c8` − 1) en la x87, y mientras no baje de 0,01
  la Z baja 0,4; debajo, el que no es el héroe se borra (`+0` en 0, `+0x391`, `+0x1a1` y `+0x1bc` en 0, y
  `0x401040` y `0x4fb1a0`, que en MuRet no tienen nada que soltar, P-C161). Después, el vuelo: en Blood Castle (11
  a 17) con `+0x1a1`, `0x4fb2b0` (`(0, +0xd0, 0)` girado por `+0x1a4` desde `+0x1b0`, `+0xd0 += +0xcc`, `+0xe4 +=
  +0xd8`, `+0xd8 −= +0xd4`, el ángulo X menos 5 y la Z en `+0x1b8 + +0xe4`); en Chaos Castle (18 a 23) igual con
  `+0x1a1` (tope 15; sin él, 25) y, del tope − 10 al tope en los pasos impares, tres `rand() % 160` y las chispas
  de `0x51fe10` (CL11: corta en `0x4fbba8`); fuera de los castillos, con la skill `0x28` o `0x3b` en `+0x391`,
  hasta el paso 14 `+0xd0 −= +0xd8` (0 si queda debajo), `+0xd8 += 1` y `0x51fd20` (la posición más `(+0xcc,
  +0xd0, +0xd4)` girado por los ángulos de la cabeza y `+0x30`), y con la `0x28` hasta el paso 30, diez
  partículas `0x5ac` (CL11: corta en `0x4fbcdd`). Por último, en la escena 5 y el mapa 7, cuatro burbujas `0x607`
  (`rand() & 0x7f − 64` en x e y y `rand() & 0xff` en z sobre la posición, `0x53fe70`). Al volver, el cuerpo sube
  `+0x389` (un byte, Q-C1166) y desde 15 llama otra vez a `0x4f5660` (`0x4fd787..0x4fd7e1`; en Blood Castle con
  `+0x1a1`, también antes), y al final apaga `0x8000` y `0x10000` de `+0x78` (`0x505453..0x5054aa`).
- **`+0x381` después de pegar** (`0x4fdb9a..0x4fdbeb`). Lo ponen en 1 el ataque al llegar (`0x5653c1`) y el `18`
  con `0x78` y `0x79` (`0x4cb830`): en cada paso el cuerpo llama a `0x4f97b0` y `0x4f5b70`, lo sube, y desde 15
  corre la habilidad de `+0x38e` (`0x4fdbf1..0x50539e`). Portado en la fase 2b: [El paso del
  ataque](#el-paso-del-ataque).

### El paso del ataque

*Estático*, cotejado con el PE c32bbd5f (CL9 hito 3, fase 2b; `Scenes::StepTheAttack`, `AttackStep.cpp`). Es el
tramo `0x4fdb9a..0x50539e` del cuerpo (`0x4fd5f0`, punto 9 de [El cuerpo de los
personajes](#el-cuerpo-de-los-personajes-0x4fd5f0)). Cada lectura de `+0x38e` (la habilidad, un byte por la
[tabla de protección](#la-tabla-de-protección)) es un acceso: `0x517630` (thiscall sobre `0x57630c8`) o su copia
en línea, con un `rand()` si el cierre deja la cuenta en 0.

**`0x4f97b0(c, o)`, los frames del ataque.**

1. `0x4f9780(o)` (1 con el `0x20a` en la `0x25` o la `0x26`; solo lo leen los casos), `+0x38e` en línea
   (`0x4f97cf..0x4f987a`) y `[0x6c88ac] = 15` (`0x4f987d`).
2. El `switch` de `0x4f9899` (habilidad − `0x28`, tabla `0x4fb10c`): `0x28`, `0x2b`, `0x2c`, `0x2f` a `0x31`,
   `0x34`, `0x35`, `0x37` a `0x39`, `0x3c` a `0x3f`, `0x41` y `0x43` a `0x4a`. Son del hito 4 (la `0x4f9ee2` pone
   `[0x6c88ac]` en 5).
3. Los demás (`0x4fab5d`): desde el frame 1 (`0x6b87d4`, `FCOMP`: NaN no pasa), el `0x20a` en la `0x3e` termina
   (`+0x381 = 15`). Antes del frame 5 (`0x6b87f8`), o con NaN, sale. Sigue el `0x20a` con una acción de `0x22` a
   `0x97` o un tipo de `0x152` a `0x1c9` con la 3 o la 4: abre el registro propio en línea, lee la izquierda
   (`+0xbb4`, `MOVSX`, sin el ítem del cursor) y lo cierra (los 3.848 de `0x40b4d0` en 0). Con `+0x381` en 1 o más,
   el `0x815` y el `0x20a`, veinte `0x53fe70(0x5c6, o+0x10, o+0x1c, o+0xf4, 12, 2,0f, 0)` (`0x4fb0d1`). Después,
   `+0x381 = 15`. El registro es el del héroe aunque pegue otro (Q-C1168).

**`0x4f5b70(c)`, los ayudantes.** Un `rand()` sin condición en `0x4f5b97`, que no lee nadie (Q-C1169). Después,
seis ayudantes, cada uno en sus mapas; el que devuelve 1 hace salir a `0x4f5b70` (`0x4f903c`). Ninguno saca
`rand()` antes de actuar (fase 2c, *estático*):

| Ayudante | Dónde | Quién actúa (su caso) |
| --- | --- | --- |
| `0x43a7a0` (`0x4f5c0f`) | los mapas 24 a 29 y 36 (`0x4f5ba3..0x4f5bda`) | 54 `+0x376` de Kalima, de `0x90` a `0x119` (la tabla de `0x43b8fc`; los demás, AL en 0 en `0x43b8cb`) |
| `0x4436f0` (`0x4f5c22`) | el 31 | `0x123` a `0x125`, `0x127` y `0x12f`: devuelven 1 sin hacer nada más (`0x443721`) |
| `0x42dcf0` (`0x4f5c35`) | el 30 | `0x68` (`0x42dd56`) y `0x120` (`0x42dd23`) |
| `0x429110` (`0x4f5c48`) | el 33 y el 54 | `0x130` (`0x429211`), `0x134` (`0x4291b1`) y `0x135` (`0x42914b`) |
| `0x42fce0` (`0x4f5c5b`) | el 35 (`0x42ee20`) | el tipo `0x1ad` (`0x42fd43`) y el `0x1b2` (`0x42fd04`), con `+0x381` en 14 |
| `0x4324c0` (`0x4f5c6e`) | el 34 | el tipo `0x1b2` y el `0x1ad` con `+0x381` en 14, el `0x1b5` en 15, el `0x17c` y el `0x17e` (la tabla de `0x432b8c`) |

Los dos `switch` por `+0x376` (`0x4f5c7e`: `0x23` a `0x48`, la tabla de `0x4f9074`; `0x4f7598`: `0x49` a `0x12d`,
la de `0x4f90b8`) tienen casos para `0x23`, `0x26`, `0x2a`, `0x2d`, `0x31`, `0x35`, `0x36`, `0x39` a `0x3b`,
`0x3d`, `0x3f`, `0x42`, `0x43`, `0x46` a `0x4b`, `0x4d`, `0x59`, `0x5f`, `0x67`, `0x70`, `0x76`, `0x7c`, `0x82`,
`0x8f`, `0x97` y `0x12d`; los demás, los monstruos de los primeros mapas (`+0x376` debajo de `0x23`) y el `0xffff`
de los jugadores (el de `0x511290`, `0x511d72`), van a la cola (`0x4f80e0`): con `+0x3a4` fuera de `[0, 400)`
sale; si no, lee `+0x38e` (`0x4f811b`): con `0x11`, un `switch` por `+0x376 − 0x25` (`0x4f8147`, la tabla de
`0x4f91cc`: `0x25`, `0x2e`, `0x3d`, `0x42`, `0x45`, `0x49`, `0x4b`, `0x4d`, `0x57`, `0x59`, `0x5d`, `0x5f`, `0x63`,
`0x70`, `0x74`, `0x76`, `0x7a`, `0x7c`, `0x80`, `0x82`, `0x8d`, `0x8f` y `0xa3` a `0xad` impares) del que los
demás salen; si no, otra vez (`0x4f8a2c`): con 3, un `switch` por `+0x376 − 0x22` (`0x4f8a56`, la tabla de
`0x4f9274`: `0x22`, `0x25`, `0x27`, `0x30`, `0x4d`, `0x59`, `0x5f`, `0x70`, `0x76`, `0x7c`, `0x82` y `0x8f`) y los
demás `0x4f8f5b` (el hueso de `+0x2bd` con `0x4ec110`, dos joints `0x614` y la partícula `0x5ca`).

**La cabeza** (`0x4fdb9a..0x4fdbeb`). Con `+0x381` distinto de 0, las dos y `+0x381 += 1` (un byte). Si `+0x381`
queda debajo de `[0x6c88ac]` (`JL`, con signo), salta a `0x50539e`.

**La habilidad** (`0x4fdbf1..0x50539e`):

1. `+0x381 = 0`; `o+0x8a` (WORD) = `0x558eb0(c, [0x6c8e30])`: la clave (`+0x1f4`, `MOVSX`) del elegido o −1. Da
   −1 con un ítem en el cursor (`JG`), sin elegido o si `c` no es el héroe publicado (`[0x743a170]`). En Chaos
   Castle (18 a 23), la clave, también la del héroe. Si no, como `0x558bb0` sin el asedio: la guerra de gremios
   con el PK de 6 o más y un gremio compara los nombres (`0x558f29`, Q-C1170); `+0x370` en 2 u 8; fuera de la
   guerra, el PK de 6 o más o Ctrl (`0x558fd6`) con otro; en ella, `+0x374` en 2 con otro; en el duelo, la clave
   del rival, sin el nombre. No saca `rand()`.
2. `+0x38e` y el `switch` de `0x4fdeeb` (habilidad − 5, índices en `0x505530`): 5, 8, 9, `0xa`, `0xc` a `0xe`,
   `0x1e` a `0x24`, `0x26` a `0x2a`, `0x2c`, `0x30`, `0x31`, `0x35`, `0x37`, `0x38`, `0x3f` a `0x41`, `0x43` a
   `0x48`, `0x4b` y `0x4c`. Los demás, como el 0, siguen en `0x502aff`.
3. **Sin blanco** (`+0x3a4`, `MOVSX`, en −1; `0x502b12..0x5037d5`): `+0x38e` (con `0x18`, una bandera). El `0x20a`
   en las `0x2e` a `0x31`, `0x36`, `0x37`, `0x5e` y `0x61` (el arco y la ballesta a pie, en Uniria o Dinorant y en
   Fenrir), u otro tipo con `+0x88` en 1: dos lecturas más, `0x59fb80(segunda, bandera, primera)` y
   `0x53e2b0(c, o, 0, r)` (`0x50376a`, la flecha). Los tipos `0x168`, `0x175`, `0x17a` y `0x180`:
   `0x53e2b0(c, o, 0, 0, 0, 0)`.
4. **Con blanco** (`0x5037dd..0x50534a`): el lugar de `+0x3a4` en el pool, sin mirar el rango (Q-C1171).
   - Las mismas acciones del `0x20a`: dos lecturas, `0x59fb80(segunda, 0, primera)` y `0x53e2b0(c, o, blanco, r)`
     (`0x504048`); los tipos `0x168`, `0x175` y `0x17a`, `0x53e2b0` (`0x5040a9`).
   - Con `+0x384` del blanco en 1 o más (WORD sin signo, el daño del último `15`): si no es el `0x159`, diez veces
     tres `rand() & 63` (x e y − 32, z + `0x5a`) y `0x53fe70(0x5e4, punto, o+0x1c, (1; 1; 1), 0, 1,0f, 0)`
     (`0x5040fb..0x50419c`); con el `0x18e`, cinco `rand() & 1` y, con 0, `rand() % 30` y `0x518cf0(0x10b)`, y
     el sonido `0x88`; por último `0x42ea00(blanco)`, que solo actúa en el mapa 30 con el `0x19b` y el `0x19c`.
   - Las acciones `0x38` a `0x3c`: `0x52e740` (`0x5042ce`).
   - Los ángulos de `o` con el rumbo de `0x4e93f0` hacia el blanco, `+0x38e` y el `switch` de `0x504372`
     (habilidad − 1, índices en `0x5055c8`): 1, 2, 4, 7, `0xb`, `0x10`, `0x11`, `0x18`, `0x1a` a `0x1c`, `0x2d`,
     `0x2e`, `0x33`, `0x34`, `0x3c`, `0x3d`, `0x49` y `0x4a`. El 3 y los demás van a `0x505069`.
   - `0x505069`: el punto del blanco (z + 120), la bandera de la `0x25` y la `0x26` y la luz; con `+0x380` en 2
     (el `16` del héroe), dos `0x530860(0x618, blanco+0x10, blanco+0x10, o+0x1c, 0 y 1, o, 20,0f, −1, 0, 0, −1)`
     (`0x505169`, `0x5051a4`).
   - `+0x38e` y el `switch` de `0x5051f1` (habilidad − `0x10`, índices en `0x505730`): `0x10`, `0x1a` a `0x1c`,
     `0x1e` a `0x24`, `0x28`, `0x2b`, `0x2f`, `0x30`, `0x35`, `0x3f`, `0x40`, `0x43` a `0x46`, `0x48` y `0x4b`
     no suenan. Los demás, con `+0x376` fuera de `0x44..0x4b`: abre el registro propio (`0x4e79a0`), toma las
     manos (`+0xb6c` y `+0xbb4`, `MOVSX`; el ítem del cursor en el lugar de `[0x78a6758]`, 0 o 1, con
     `[0x7879fbc] > 0` y la grilla `0x78a9bb0`), y con las dos de `0x800` a `0x9ff` pide `0x41ed10(0x4b +
     rand() & 3, o, 0)` (`0x5052fd`) y si no `0x46 +` (`0x505322`); lo cierra (`0x40f370`, `0x50534a`).
5. `0x40ae60`, `+0x38e = 0` y `0x40b0a0` (una escritura: un `rand()` en 0), `+0x382 = 0` y `+0x380 = 0`.

**La sangre `0x5e4`** (CL9 hito 3, fase 2d; *estático*, con la textura `Effect\blood.tga` en el slot `0x5e4`, que
carga `0x61d7c9` con `0x63bd00` lineal y `CLAMP`). Los diez puntos de `0x5040df..0x5041a4` suman a la x, la y y la
z del blanco en la x87 (`fild` y `fadd`) y piden `0x53fe70(0x5e4, punto, o+0x1c, [EBP − 0x30], 0, 1,0f, 0)`:

- **La creación** (`0x53fe70`, la tabla de bytes de `0x54446c` por tipo − `0x5de` y la de saltos de `0x544440`;
  el caso de `0x542b3b`, que comparte con la `0x5e3`): la vida en 12; la escala, `((rand() & 3) + 8) · 0,05f`
  (`0x6b8a48`), que pisa la del pedido; `ángulos.x = rand() % 360`; la matriz de los ángulos del pedido
  (`0x5fc8f0`) gira `(0; −((rand() & 15) + 8); rand() % 6 − 3)` (`0x5fcbd0`) a la velocidad; con la `0x5e4`, la
  luz en (0,1f; 0; 0) (`0x542bdb`, `0x542be9`), que pisa la del pedido. Cuatro `rand()`; no lee el subtipo. Con el
  pool lleno no saca nada, y los tres `rand()` del punto salen igual.
- **El paso** (`0x5446e0`, la tabla de `0x5482d4` y la de `0x5482a8`; el caso de `0x546b04`), después de la vida
  y del movimiento común (la velocidad girada por los ángulos, `0x4e98d0`): la velocidad por 0,95f (`0x6ba30c`),
  el cuadro `+0x48` en `(12 − vida) / 3` y la posición más la velocidad sin girar, la x con el producto sin
  redondear a `float`. Muere en el paso 12, con el cuadro 4. No saca `rand()`.
- **El dibujo** (`0x548550`, la tabla de `0x548c9c` y la de `0x548c78`; la `0x5e4` en `0x5487b9`, la `0x5e3` en el
  billboard común): la mezcla del material, sin giro, la celda del cuadro en un atlas de 2 × 2 (la u, `cuadro % 2 ·
  0,5`; la v, `cuadro / 2 · 0,5`) y el tamaño (0,5; 0,5).

MuRet la crea, la mueve y la dibuja en el pool de partículas del mundo (`World::BloodParticle`, el mismo que usa
la escena y el paso de los personajes; `BloodParticlesTest`, `AttackStepTest.TheTargetWithDamageBleedsTenTimes`).

**Las chispas del `0x815`** (`0x4fb0d1`, *estático*). En `0x4f97b0`, con `+0x381` en 1 o más, el `0x815` en la
izquierda del registro propio y el `0x20a`, veinte `0x53fe70(0x5c6, o+0x10, o+0x1c, o+0xf4, 12, 2,0f, 0)`. La
creación (`0x540a04`, la tabla de `0x5443c0` por subtipo − 1) pone la textura `0x5fc`, `+0x38` en 1,0f, la
posición más (0; −100; 0) girado por los ángulos (`0x5fc8f0`, `0x5fcbd0`) y después `(rand() & 15) − 8` en x e y
y 150 en z; la velocidad `((rand() % 20) · 0,1f − 1; igual; −(rand() % 3) − 3)`, la vida 40 y la escala `2 −
(rand() % 15) · 0,1f`: seis `rand()`. El paso (`0x545397`, la tabla de `0x548204`) saca `rand() & 1` para la luz
(menos 0,6f en rojo y verde, menos 0,8f en azul), resta 0,04f a la escala y 0,001f a `+0x38` (muere en 0 o
menos); el dibujo es el billboard común con la textura `0x5fc`. Con el pool lleno no saca nada
(`AttackStepTest.TheBowInTheOwnLeftHandSparksTwentyTimes`, `AttackParticlesTest`).

**Los rayos del que mató** (`0x505169`, `0x5051a4`, *estático*). Con `+0x380` en 2, dos `0x530860(0x618, blanco+0x10,
blanco+0x10, o+0x1c, 0 y 1, o, 20,0f, −1, 0, 0, −1)`. La creación (`0x532125`, `0x53214b`, y `0x5322df` o
`0x5322f8`): el ancho, velocidad 3, vida 120, 8 puntos y la luz (0,4; 0,3; 0,2) o (0,1; 0,1; 0,5), sin `rand()`;
con dueño, el destino (`+0x58`) queda el del uso anterior. El paso (`0x535840`, el `0x618` por la tabla de
`0x53c4bc`/`0x53c48c` y el subtipo por `0x53c5a0`/`0x53c594`, `0x538313`), después del movimiento común:

- con la vida en 100 o más, la /0 suma 10 al rumbo y 6 a la z; la /1 resta 10 y suma 5;
- con menos, la velocidad + 5 hasta 30, el destino en el dueño (su posición y la z + 120) y `0x4e9810` hacia él con
  la velocidad; a 35 o menos (o NaN) muere, pide `0x53fe70(0x5d5, …, 1, 1,0f, 0)` y el sonido `0x23`; de 35 a 70,
  con el rumbo girado más de 20 y la velocidad en 20 o más, la velocidad − 10;
- siempre: `v = ((rand() & 3) + 8) · 0,03f`, la luz dinámica de radio 2 (v · 0,4f; v; v · 0,8f) (`0x53861c`) y
  `0x53fe70(0x5d5, …, 0, 1,0f, 0)` con la luz del joint (`0x538695`); después, la cola y la vida.

El rayo `0x5d5` (`Effect\lightning2.jpg`): la /0 con la vida del prefijo (2) y la escala 0,15f, la /1 con vida 10 y
escala 0, las dos con la luz del pedido + 0,5 (`0x540bbe`, `0x540bfe`); su paso (`0x545419`) gira con
`(ftol(reloj) % 1000) · 0,001f` y pide la luz dinámica de radio 3 (v · 0,5f; v; v · 0,8f) con v = vida · 0,1f; el
dibujo, el billboard común (`0x54889a`). MuRet los guarda en los joints del juego (`GameDrawTailState::joints`,
`CharacterStepContext::joints`) y los mueve en `StepJoints` con la luz dinámica del mundo y el sonido
(`KillBeamJointsTest`, `AttackStepTest.AKillOfEachClassSendsTheBeamsBackWithoutACut`).

**La skill de la flecha** (`0x59fb80`, *estático*). Las dos ramas de la flecha (`0x50376a`, `0x504048`) le pasan la
segunda lectura de `+0x38e` (los otros dos argumentos quedan en la pila para `0x53e2b0`): abre el registro propio
(`[0x7681eb4]`, `0x40ad90`), busca ese byte entre los 64 de `+0x6c` de `[0x7681eb0]` y lo cierra (`0x40b4d0`, 3.848
`rand()` si queda en 0); devuelve su lugar, o 0. MuRet la porta (`OwnSkillIndex`); el tiro de después
(`0x53e2b0`, `0x50377d` y `0x504060`) está en [La flecha](#la-flecha) (fase 2e).

**Los `rand()` de un ataque sin habilidad**, con las cuentas en 0: antes del frame 5, cuatro por paso (la lectura
de `0x4f97b0`, el de `0x4f5b97` y las dos de la cola de `0x4f5b70`; con `+0x3a4` fuera del pool, dos); en el
paso del frame 5, además, los 3.848 de `0x4f97b0`; en el de la habilidad, con blanco, tres lecturas, el del
sonido, 3.848 y la escritura (3.853), y sin blanco tres. Con el daño del último `15` en el blanco, además setenta:
tres por punto y cuatro por sangre. Con 0,5 keys por paso, el frame 5 llega en el paso 10 y la habilidad corre ahí;
sin llegar, en el 14 (`AttackStepTest.AMeleeAttackOfEachClassRunsItsSteps`, con el paso de las partículas y los
dos `rand()` del giro de la cabeza, `0x5068fd`, descontados: uno en 32 por paso y personaje que no es el héroe).

**Lo que MuRet corta** (con lo de antes y sus `rand()`), y quién llega con un ataque sin habilidad:

| Dónde | Qué | Quién llega |
| --- | --- | --- |
| `0x4f9899`, `0x4f8a39`, `0x4fdeeb`, `0x504372` | los casos de habilidad | nadie: `+0x38e` lo escriben `0x4cc530` y los monstruos (hito 4) |
| los casos de la tabla de arriba (`0x43a7fe`..`0x43b5e4`, `0x42dd23`, `0x42dd56`, `0x4291b1`.., `0x42fd04`, `0x42fd43`, `0x4324fb`.., `0x4f5cb6`..`0x4f804e`, `0x4f814e`..`0x4f890b`, `0x4f8a5d`..`0x4f8e85`) | los ayudantes y los casos de `0x4f5b70` | los monstruos con caso, en sus mapas (hito 2 y CL11); ningún jugador sin transformar |
| `0x4fb0d1` | las veinte `0x5c6`/12 sin el pool de partículas | nadie en el juego (fase 2d) |
| `0x558f29` | los nombres de gremio | el héroe en la guerra de gremios (hito 8) |
| los cortes de [La flecha](#la-flecha) | las skills de tiro, los joints de las flechas y sus partículas sin portar | el elfo con esas armas o skills (fase 2e) |
| `0x50419c` | la partícula `0x5e4` sin el pool de partículas | nadie en el juego: `GameScene` pasa el del mundo (fase 2d) |
| `0x504267` | el efecto `0x10b` | el que pega al `0x18e` con daño, con un `rand() & 1` en 0 de los cinco |
| `0x50428c` | `0x42ea00` | nadie: el mapa 30 corta antes (`0x412140`) |
| `0x5042ce` | `0x52e740` | las acciones de skill `0x38` a `0x3c` (hito 4) |
| `0x505169` | los joints `0x618`/0 y /1 sin el pool de joints | nadie en el juego (fase 2d) |

### La flecha

*Estático*, cotejado con el PE c32bbd5f (CL9 hito 3, fase 2e; `Scenes::FireTheArrow` y `Scenes::StepArrow`,
`ArrowEffects.cpp`). El paso del ataque tira en cuatro lugares: sin blanco, `0x50377d` (con la acción de tiro del
`0x20a` o `+0x88` en 1: la skill es la primera de dos lecturas más de `+0x38e`, el lugar sale de `0x59fb80` con la
segunda y la bandera es 1 si la lectura de `0x502b12` dio `0x18`) y `0x5037d0` (los tipos `0x168`, `0x175`, `0x17a`
y `0x180`, todo en 0); con blanco, `0x504060` (igual, con la bandera en 0) y `0x5040a9` (los tres primeros tipos).
Después sigue el paso del ataque.

**El tiro** (`0x53e2b0(c, o, blanco, lugar, bandera, skill)`). Con la skill `0x34`, `0x33` o `0x2e` va a `0x53e741`
(con `+0xf08` del registro propio) y con la bandera en 1 a `0x53e2e0` (las flechas de `0xa24`/`0xa25` o tres con
`+0x24` corrido): son del hito 4 y MuRet corta en `0x53e2be` y `0x53e2da`. Si no, `0x53d9a0` y, con `0xa25` en
`+0x2b8`, el brillo de la mano (`0x53e630`: el hueso de `+0x2bd` con `0x4ec110`, quince giros `rand() % 360`, el joint
`0x617`/3 con un `rand()` par y la `0x5c5`/0), que corta en `0x53e642`.

**Las armas** (`0x53d9a0`). Las de `+0x2b8` y `+0x2d8` (MOVSX); con el héroe (`[0x743a170]`), el registro propio
abierto y cerrado en línea (3.848 `rand()`) y los tipos de `+0xb6c` y `+0xbb4` más `0x212` (salvo el −1). La skill
`0x34` deja el subtipo en 2 y pide el sonido `0x55`; la skill va a `[0x5767948]`; la `0x33` pide el `0x82` (los dos
con `0x41ed10(id, o, 0)`, CL10: MuRet los manda a `soundRequest`); la bandera en 2 deja el subtipo en `0x63`; la
skill `0x2e` crea solo el `0x13a` y sale (`0x53df2a`, hito 4). La derecha menos `0xa1a` (0..0xb, `0x53e240`) da
`0xdc`, `0xdc`, `0xe0`, `0xde`, `0xdd`, `0xe3`, `0xe2`, nada, `0xf7`, nada, `0x10e` y `0x111`; la izquierda menos
`0xa12` (0..0x15, `0x53e28c`/`0x53e270`), `0xdb`, `0xdb`, `0xdf`, `0xdb`, `0xdb`, `0xdb`, `0xe1`, nada × 10, `0xf8`,
nada, nada, `0x11c` y `0xf9`. Cada una es `0x518cf0(tipo, o+0x10, o+0x1c, o+0xf4, subtipo, o, o+0x8a, lugar, bandera,
0, 0)`: el lugar va a `+0x89`, la bandera a `+0x88`, la clave a `+0x8a` y el dueño a `+0x108`
(`ModelEffects::Begin`).

**El alta** (los casos de `0x518cf0`). `0x51c43b` (`0xdb`..`0xe1`, `0xe3`, `0xf9`, `0x11c`): `+0x64` en 1 con el
`0xdb`, −2 con el `0xe1` y 0 con el `0xdd`, el `0xde`, el `0xdf` y el `0xe3`; `+0xd8` en 1, vida 30 y la boca
(−10; −60; 135) girada por los ángulos (`0x5fc8f0`, `0x5fcbd0`) y sumada a la posición; el `0xe3`, escala 1,8f, la
y de la velocidad en −50 y `+0xe4` en −10; el `0xf9`, escala 1 y −70 y el joint `0x610`/0xe (`0x51c56c`); los
demás, escala 0,8f y −70. Con el subtipo 2, el efecto `0x105` (`0x51c5a9`, hito 4); el `0xe1`/1, el joint
`0x610`/0xd; el `0xdb` con el subtipo 3 o 4, vida 40 y tres `rand()` (`0x51c601`, hito 4); el `0x11c`, `+0x64` en 1,
la y de los ángulos en `rand() % 360` y el joint `0x610`/0xc (`0x51c713`). La salida `0x51a1b6` abre el registro
propio, copia su `+0xf04` a `+0x8c` y lo cierra (3.848 `rand()`). El `0xe2` (`0x51a41e`): igual hasta la boca,
`+0x64` en 0, escala 1, −30, `+0xe4` en −10, vida 40, con el subtipo 2 `+0x100` en 0 y `+0x88` en 1, y la misma
salida. El `0xf7` (`0x51a24a`), el `0xf8` (`0x51a405`), el `0x10e` (`0x51a173`) y el `0x111` (`0x51bf35`; con un
subtipo que no es 0 ni 2 sale con el prefijo) piden joints antes de sacar `rand()`: MuRet corta ahí (CL11).

**El paso** (`0x5208a0`). El `rand()` de la luz local (`(rand() % 4 + 7) · 0,1f`, menos 0,2f por paso de vida bajo
5), el caso del tipo y la cola `0x52cb98` (la animación, la velocidad girada sumada a la posición y una vida
menos), que no mira el byte vivo: corre también si el choque la apagó.

- `0xdb` (`0x52406c`): la luz dinámica de radio 2 (luz · 0,8f; luz · 0,5; luz · 0,2f) (`0x5240a5`); con el
  subtipo 3 o 4, hito 4 (`0x5240b0`); con el 5, el fuego lleva (luz · 0,1f; luz · 0,6f; luz · 0,3f); el fuego
  `0x53fe70(0x5d9, posición, ángulos, color, 0, 1,0f, 0)` y el choque.
- `0xdc`, `0xdd`, `0xde`, `0xdf` y `0xe0` (`0x5267c7` → `0x526dc2`): solo el choque.
- `0xe1` (`0x52697e`; el subtipo 1, `0x5267dc`, hito 4): dos sprites `0x5d5` (`0x5491e0`, escalas 0,5 y 1, la luz
  `+0xf4`, el giro ±`ftol(reloj)` · 0,1f), cuatro veces tres `rand()` del punto ((& 31) − 16; (& 63) − 32;
  (& 31) − 16) y `0x608 + rand() % 3` (corta en `0x526a8b`, CL11), la y de los ángulos + 30, la luz (luz · 0,6f;
  · 0,8f; · 0,8f) y el choque.
- `0xe3` (`0x526c56`): los dos sprites, cuatro veces el punto (& 15) − 8 y la `0x607`/1 (corta en `0x526d5f`,
  CL11), la `0x5f2`/0, la luz verde y el choque.
- `0xe2` (`0x524237`): dos sprites `0x5ac` con (1; 0,6f; 0,4f) y escalas 1 y 2, cuatro veces el punto y la
  `0x607`/1 (`0x52434e`); después cae sobre el terreno (`0x52435d`, sin portar).
- `0x11c` (`0x526aef`): la y de los ángulos + 60; con vida par, la `0x60f`/5 (`0x526bc2`), con impar, el `0x114`
  (`0x526c05`). Los pasos del `0xf7`, `0xf8`, `0xf9`, `0x10e` y `0x111` no se alcanzan: su alta corta.

**El choque** (`0x520170`). Con el asedio (`0x4294b0`) y el bit `0x100` de la celda, la frena (`0x52018d`, CL9.5);
con `+0x88` distinto de 0 y sin el subtipo `0x63`, `0x517a20` (`0x5201ed`, hito 4). Si no, `0x517850(efecto, 100,
+0x8a, 0)`: el primero de los 400 vivos que no sea el dueño, que se vea (`+0x16c`), que no sea el héroe (salvo con la
clase 2) y que no se esté muriendo, a 100 o menos en el plano (la raíz en la x87, guardada en `float`; NaN choca).
Con uno, abre y cierra el registro propio (3.848 `rand()`) para leer la skill del lugar `+0x89`: con la `0x33` la
apaga (y el `0xe2` y el `0xf8` sueltan `0x51fe10`); con la `0x34` y el subtipo 2, el joint `0x60f`/6 (`0x52083f`,
CL11), y si no la apaga con `0x51fe10`; con otra, la apaga (`0x51fe10` con el `0xe2` y el `0xf8` sin el subtipo 1).
`0x51fe10(punto, 1)`: la z + 80, veinte `0x5c5`/2 con los ángulos (`rand() % 60` + 150; 0; `rand() % 30`) y la
`0x5ed`/0 con la luz 0,7f; MuRet corta en la primera `0x5c5`/2 (`0x51fe9e`, CL11) después de sus dos `rand()`. La
flecha de otro elfo no choca con el héroe (la clase 0).

MuRet dibuja las flechas con el modelo de su tipo cuando lo hay (CL11). Los `rand()` de un tiro del héroe con el
arco corto: las dos lecturas de `+0x38e`, 3.848 de `0x59fb80`, 3.848 de las armas y 3.848 de la salida del alta; por
paso de la flecha, uno de la luz (y los del fuego con el `0xdb`) y, al chocar, 3.848
(`ArrowEffectsTest`, `AttackStepTest.TheElfBowShootsAndStopsAtTheSkill`).

### Los cambios de mapa

*Estático* salvo lo que se marca. Tres paquetes del servidor llaman a `0x61a560` ([La carga del
mapa](#la-entrada-al-mundo)): el `F3 03` (la entrada), el **`1C`** y el **`F3 04`**. Ninguno cambia
de escena: el juego sigue en la 5, porque las ocho escrituras de `0x6d5dc8` (`0x4c2f1a`, `0x4c363f`,
`0x622f93`, `0x633f05`, `0x634470`, `0x634638`, `0x634dce` y `0x635a91`) no están en esos handlers.
Los formatos, en [04](04-protocolo.md#los-cambios-de-mapa-1c-f3-04-y-f3-12).

**El warp, `1C` del servidor** (`0x4c6e10`, desde `0x4e3f2a`, que antes lo copia a la cola de
`0x77f27b4`, la de los `12`: [Los otros jugadores en el viewport](#los-otros-jugadores-en-el-viewport)). Con el cuarto argumento del dispatcher en 0 no hace nada de esto y manda un C3 con `C1
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
   04 BD 00`; el prefijo del Chaos Castle (`0x41ed10(0x9e)`, `0x4906a0(1, 0)`, `0x4914e0`, el objeto de los
   comandos `0x6e592c` en `0x4c7b34`, `0x512a30` y `0x443d40`) **al entrar y al salir**: `0x4c7af1..0x4c7b2f` mira viejo 18..23 con cambio **o**
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
  confirmar. Al entrar **o salir** de Chaos el port conserva carga y sonido 9e, esconde las
  ventanas viejas (`0x4906a0(1, 0)`) y suelta la de amigos (`0x4914e0`), y corta en `0x4c7b34` (el
  objeto de los comandos, CL9.4), antes de equipo/F312.
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

### Los otros jugadores en el viewport

*Estático* (CL9 hito 1, 2026-11-13); el cable con jugadores reales queda para la VM (08, P-C160). Los formatos, en
[04](04-protocolo.md#los-otros-jugadores-12-14-45-25-y-df). Cada celda protegida (`+0x41c`, `+0x420`) que se escribe o
se lee abre y cierra su entrada de [la tabla de protección](#la-tabla-de-protección): 4 `rand()` por acceso que la
deja cerrada.

**El alta, `0x512870(clave, tipo, X, Y, ángulo)`**. Busca en los 400 primeros (uno menos que el pool: el 400 es el
"ninguno", que existe) un vivo con la clave, con el `MOVSX` de `+0x1f4` contra el int: lo construye otra vez con
`0x511290` ([Los personajes de la lista](#los-personajes-de-la-lista); 28 `rand()` desde cero) sin tocar la clave y
pone `+0x78` y `+0x7c` en 0. Si no hay, el primero sin el byte vivo: `0x401040` (sus nodos), `0x421d60` (el cuervo),
`0x4fb1a0` (sus telas) y `0x414340` (la capa), `0x511290`, la clave (el `WORD` de abajo) y las dos banderas en 0. Con
los 400 vivos devuelve el 400 sin tocar nada. Devuelve el puntero. **La baja, `0x510ff0(clave)`**: el primero vivo
con la clave queda muerto, con `0x401040`, sus ayudantes apagados (`0x511031..0x51104b`, como `0x443d40`), el cuervo,
las telas y la capa. Nadie más llama a la pareja en la ruta del viewport; el alta de los monstruos (`0x5141e0`) es de
la hito 2. Las búsquedas `0x5132d0`, `0x5178f0` y `0x517920` no tienen llamadores ni punteros en el binario.

**La cola de `0x77f27b4`**. Antes de `0x4c8860`, el dispatcher copia el paquete entero, con su largo, a una cola de
diez ranuras de `0x100` bytes (los largos en `0x77e1bb0` y la cuenta, un char con signo, en `0x77f3ba0`;
`0x4e372e..0x4e37b8`). La misma secuencia corre antes del `0x0B` (`0x4e3594`, su caso en el dispatcher), del `0x1C`
(`0x4e3e98`, antes de `0x4c6e10`), del `0x24` (`0x4e38ca`, antes de `0x4d1400`) y del `0x2A` (`0x4e3ace`, antes de
`0x4d3cc0`), sin mirar el flag de cifrado ni si el paquete se lee. Con la cuenta en más de 9 la deja en 9 y corre las
ranuras 1..9 a 0..8; un paquete de más de `0x100` no entra, pero el corrimiento ya se hizo. El estado 1 del `12` la
escribe otra vez (paso 5, Q-C801). Nadie la lee: `0x557830` (un volcado) y `0x4c12a0` no tienen llamadores. MuRet: la
sesión hace la copia (`ISessionEventSink::CopyToPacketRing`) y la cola es `WorldRuntime::viewportPackets`
(`Scenes::StoreViewportPacket`); el `0x0B` es del hito 11 (P-C162).

**El `12`, `0x4c8860(paquete, largo)`**, por cada registro:

1. Copia los diez bytes del nombre con un 0 detrás; si tienen `"webzen"` (`0x576960`, distinguiendo mayúsculas), el
   registro no hace nada (`0x4c88dc`).
2. Con el estado 1 (`+4 & 0xf`), si hay un vivo con la clave (`0x5110d0`), guarda el byte de abajo de su `+0x1f2`,
   `+0x368`, `+0x369`, `+0x370`, `+0x360`, `+0x78` y `+0x372`.
3. `0x512870(clave sin el bit 15, 0x20a, X, Y, 0)`. Con el estado 1 y lo guardado, lo devuelve: `+0x1f2` con el
   `MOVZX` del byte, sin el de arriba (Q-C800).
4. La clase, `((b & 0x10) | (b >> 4)) >> 1`, en `+0x1d4`; la piel (`+0x1d5`) en 0, `+0x88` en 1 y el PK (`+0x28 &
   0xf`) en `+0x375`.
5. Por el estado (`0x4c9288`): el 1, `0x53d930` (la acción `0x93` en el frame 5, `+0x80` en 3, el alpha de destino en
   1, el efecto `0x5c6` y el sonido `0x76`, con `0x4f3f80` antes: el registro propio abierto y cerrado) y el paquete
   otra vez a la cola (`0x4c8a43..0x4c8ad4`, Q-C801); el 2, la acción `0xc5`, el 3, `0xcb`, y el 4, `0xc9`, con
   `0x4ea030`, y las del elfo (la clase `& 7` en 2) `0xc7`, `0xcc` y `0xca`.
6. Las banderas de `+0x18` con `0x4e6380` (cada una prende su bit en `+0x78` o en `+0x7c`; las que crean efectos
   o joints son de la CL11).
7. La celda protegida en X e Y (8 `rand()`), y `+0x39a`/`+0x39b` igual; el ángulo, `((+0x28 >> 4) − 1) · 45`.
8. Recién llegado: la X y la Y de la tabla (8 `rand()`), la posición en `(celda + 0,5) · 100`, el efecto `0x61f`
   (`0x518cf0`, con el personaje de dueño en `+0x108`) y el alpha en 0. Si no: la Y, la X (8 `rand()`) y `0x4eac00`
   hasta `+0x26`/`+0x27`; con camino, `+0x378` en 1.
9. En Kalima (24 a 29 y 36), el joint `0x610`/8 de `0x530860`.
10. El equipo: `0x5139f0(0x5110d0(clave), +5, 0, 0)` ([Los personajes de la lista](#los-personajes-de-la-lista)).
11. El nombre en `+0x1d9`, `+0x1e3` en 0 y, con el estado 1 y lo guardado, `+0x360` y `+0x78` otra vez.
12. `0x42ab40` (las banderas de Castle Siege, solo en el 30) y `0x412140` (el panel del asedio).

**El `14`, `0x4ca780`**, por cada clave: con la información de Castle Siege (`0x5767924`, que crea el `B2 20`), la
clave entera `0x13d` pone sus bytes 4 a 6 en 0 y cualquier otra los `0x1f` a `0x21` (Q-C802); `0x510ff0` con la clave
sin el bit 15, y `0x451630` busca la tienda personal de la clave en el mapa de `0x7245c8`, que solo llena el `3F`:
sin entradas no llega a `0x451430`.

**El `45`, `0x4c92a0`**: el transformado. El nombre y `"webzen"` como en el `12`; después lee la clase del héroe
publicado (`0x4c9328`) y llama a `0x5141e0`, el alta del monstruo o del transformado del tipo de `+4` (la hito 2).
Sobre lo que devuelve (`0x4c9356..0x4c9762`): con `+0x376` en 7, la escala en 0,8; esa clase, el PK, `+0x88` y
`+0x3e3` en 1; `0x4e6380` y `0x42ab40`; la celda protegida, `+0x39a`/`+0x39b` y el ángulo. Recién llegado, la posición
del centro, el alpha en 0, el efecto `0xec` (`0x518cf0`) y el joint `0x5d5`/2 (`0x53fe70`); si no, el camino con la Y
leída antes que la X. Al final, el nombre, `+0x1e3` en 0 y `0x512a30` sobre el héroe publicado.

**El `25`, `0x4c8380`**: el personaje de la clave entera (`0x5110d0`, el 400 si no hay) y el ítem: el tipo de
`0x54b3a0` (`0x1fff` sin ítem), el lugar en el nibble alto de `pkt[6]` y el nivel en el bajo (`0x513340` con el
nibble entero: de 8 a 15 da 0, `0x51334e`), el excelente `pkt[8] & 0x3f` y el set `pkt[9]`.

| lugar | vacío | con ítem |
| --- | --- | --- |
| 0, la derecha | modelo `0xffff`, el excelente en 0 | modelo (tipo + `0x212`), nivel y excelente |
| 1, la izquierda | lo mismo y `0x421d60` sobre **el héroe publicado** (Q-C803) | lo mismo y `0x420d60` (el cuervo otra vez) |
| 2 a 6, el cuerpo | la parte de la clase (`0x2212` + …) con nivel, excelente y set en 0 | modelo, nivel, excelente y set |
| 7, las alas | modelo `0xffff` | modelo y nivel 0 |
| 8, la mascota | modelo `0xffff` y `0x443d40` | modelo, nivel 0 y el ayudante de la tabla de `0x4c882c` sobre el tipo − `0x1a00`: 0 → `0x1c12`, 2 → `0xc6`, 3 → `0x110`, 4 → `0xbf`, `0x25` (Fenrir) → el excelente en `+0x31b` y `0x14a` con 1, `0x14c` con 2 y `0x14b` con otro, con `0x443fa0` |
| 9 a 15 | nada | nada |

Al final, `0x4053f0` y `0x512930`.

**El `DF`, `0x4c6b40`**: el personaje de la clave entera. Con el tipo `0xf1` (que solo crea `0x5141e0`), el empujón
(`+0xe4`, `+0xd0` y `+0xcc`), el sonido `0x1b` y los joints `0x5c5` de `0x51fe10`; si no, la celda protegida en
`pkt[5]` y `pkt[6]` (8 `rand()`), `+0x39a`/`+0x39b` y `+0x399` en 1.

**Lo que el héroe ya hacía con otros**: el `D3` y el `D7` ([Caminar](#caminar)) buscan al personaje por la clave en
el mismo pool, así que mueven y lastiman a los que da de alta el `12`.

**MuRet** (`Scenes/ActorCreation.h`, `Scenes/ViewportPlayers.h`, `Scenes/ActorCharacterSet.h` y
`Scenes/ActorEffectFlags.h`). `CreateActor` y `RemoveActor` son `0x512870` y `0x510ff0`, para la hito 2 también:
`CreateActor(mundo, clave, tipo, {X, Y, ángulo}, contexto de 0x511290, telas)` devuelve el lugar (el 400 con el pool
lleno). `0x401040` saca los huesos con nombre (`WorldRuntime::namedBones`, desde la hito 2); `0x4fb1a0` no tiene qué
soltar en el mundo de MuRet (P-C161). El efecto `0x61f` va sin el dueño, como el de la entrada del héroe. Cortan con su
dirección, después de lo que el original hace antes: Kalima en `0x4c91dc` (CL11), el recién llegado del `45` en
`0x4c965c` (CL11; el alta de `0x4c934c` es `CreateMonster` desde la hito 2, [Los monstruos y los NPC en el
viewport](#los-monstruos-y-los-npc-en-el-viewport)), los joints del tipo `0xf1` del `DF` en `0x4c6c9d` (CL11; el empujón
está portado desde la hito 2), y las banderas que crean efectos (`0x4e644f`, `0x4e662d`, `0x4e6725`, `0x4e676e`,
`0x4e67d6` y `0x4e6869`, CL11 y CL10; `0x4e68f5`, P-C163). Los sonidos (`0x76`, `0x84`) quedan en el último sonido, sin
audio hasta la CL10 (08, P-C162). Tests: `ViewportPlayersTest`, `ViewportLeaveTest`, `ViewportTransformedTest`,
`ShownEquipmentTest` y `ViewportPacketRingTest`, en Lorencia y en Devias.

### El picking de los personajes del juego

*Estático* (CL9 hito 1, 2026-11-13). `0x637641` llama a `0x59ec80` en cada frame de la escena 5, con el segmento
del mouse del dibujo ([El picking](#el-picking): de la cámara `0x7d36834` al punto lejano `0x7d366c0`). Los lugares
que escribe: `0x6c8e28` (el ítem del piso), `0x6c8e2c` (el NPC), `0x6c8e30` (el elegido), `0x6c8e34` (el objeto) y
`0x6c8e38` (el que se ataca).

1. `0x59ee4b..0x59ef10`: el elegido de antes sigue solo con el ataque automático (`0x6c8e3c`), fuera del mapa 6 y de
   Chaos Castle (18 a 23), entre 0 y 400, sin morirse (`+0x389`) y con `+0x88` en 2 (un monstruo); si no, `0x6c8e30`
   y `0x6c8e38` en −1. Si sigue, con `0x6c8e38` en −1, un botón del mouse (`0x7d36874`, `0x7d366d4`, `0x7d3685c` o
   `0x7d36880`) o el héroe muriéndose, solo `0x6c8e30` en −1.
2. `0x6c8e28`, `0x6c8e2c` y `0x6c8e34` en −1. Con el mouse en la UI (`0x775857c`), `0x59e3b0` (la party) y el final.
3. Con Alt (`GetAsyncKeyState(0x12)`): el ítem del piso (`0x59e2e0`, sin ítem en el cursor); sin él, `0x59e700(4)` en
   `0x6c8e2c`; sin NPC, `0x59e700(0x22)` y `0x59e700(1)` en `0x6c8e30` (el que acierta vuelve sin el final) y los
   objetos (`0x59eb90`).
4. Sin Alt: el orden es `0x22` y 1, al revés si el héroe es Dark Wizard o Elf (`+0x1d4 & 7` en 0 o 2) y la skill de
   su lugar (`+0x425`) en el registro propio, abierto (3.848 `rand()` al cerrarlo), es `0x1a` a `0x1c`. `0x5e4140`
   (la party); sin elegido, el primero, el segundo (si acierta vuelve sin el final), `0x59e700(4)` en `0x6c8e2c`, el
   ítem del piso y los objetos.
5. El final (`0x59f52f`): sin elegido, `0x6c8e38` en −1.

**`0x59e700(máscara)`**: los 400 primeros con `+0x88 & máscara == +0x88`, vivos, a la vista (`+0x16c`) y con el
alpha sobre 0 se tiñen: `+0xe8` en 1 y la luz (`+0xf4`) por el `WORD` de `+0x1d6`, de 0 a 4: (0,2; 0,2; 0,2), (−0,4;
−0,4; −0,4), (0,2; −0,6; −0,6), (1,5; 1,5; 1,5) y (0,3; 0,2; −0,5); con 5 o más la deja; con el PK (`+0x375`) en 6 o
más, (−0,4; −0,4; −0,4). Después, de los mismos, sin el que se muere, sin `+0x7c & 0x10000000`, sin el héroe
publicado (salvo en la escena 4) y con `+0x42c`, gana el de menor distancia² a la cámara (`0x7d36884`; la suma
`(dz² + dy²) + dx²` en el x87 contra un float que arranca en `0x5368d4a5`, ~10¹²) cuya caja (`+0x13c`) toca el
segmento (`0x622900`). En Lorencia la capa 1 en 4 y en Devias en 3 (los techos y los pisos de arriba) cuentan solo si
es la capa de la celda del héroe (`0x77f36d0`), con la celda de la posición truncada (`0x69fd3c`) sobre 100. El
primero gana los empates. Con jugadores en la party (`0x78aba48`), además escribe sus datos; vacía, nada. No saca
`rand()`.

**MuRet**: `Scenes::PickTheCharacter` es `0x59e700` y `Scenes::PickInTheFrame` (`GameUi.h`) es `0x59ec80`, con el
segmento de `GameScene::MouseRay` y, desde la hito 2, la caja de cada tipo (`+0x124..+0x138` de `0x511290` desde la
posición, [El alta de un monstruo](#el-alta-de-un-monstruo)). El dibujo de los personajes del juego toma el elegido
(`0x6c8e30`) y el NPC (`0x6c8e2c`) del frame anterior. El ítem del piso y los objetos son de la CL9.3 (−1) y la party,
de la hito 7 (vacía). `0x6c8e38` solo baja a −1 acá: lo suben el ataque (`0x59d3fd`) y las skills, de los hitos 3 y 4.
Con el elegido de verdad, cada lector de `0x6c8e30` y `0x6c8e38` está portado, corta con su dirección o no se alcanza:
la lista, en 08, P-C162 (d). La vuelta a la selección los deja en −1 (`0x4c2515..0x4c252d`). Lo *inferido* (la cámara,
la caja y la doble precisión), en 08, P-C164. Tests: `CharacterPickingTest` y
`GameUiTest.ThePreviousMonsterStaysOutsideTheMapSix`.

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
| `0x4c2515..0x4c252d` | los cinco del picking (`0x6c8e2c`, `0x6c8e34`, `0x6c8e30`, `0x6c8e28` y `0x6c8e38`) en −1 | `WorldRuntime::selectedTargets` en −1 (CL9 hito 1) |
| `0x4c2540` | el mundo en curso (`0x6c9dd0`) en −1 | `WorldRuntime::map` en −1 |
| `0x4c2546` | `0x419900`, con `[0x710148]`: pone en 0 su `+0x21383` y manda `C1 03 31` (`0x419a0e`), en claro y sin XOR32 | la sesión: `WriteWindowClose` |
| `0x4c2575..0x4c264b` | abre el registro propio por la tabla de protección (o lo da de alta con la cuenta en 1, `0x4c25cf..0x4c25ea`), pone en 0 `+0x3c` (byte), `+0x3e`, `+0x40` y `+0x42` (`0x4c2600..0x4c260b`) y lo cierra: si queda en 0, `0x40b4d0` y sus 3.848 `rand()` | `LogicalAccess` con `OwnRecordCloseRandoms` |
| `0x4c269d` | `[0x710148] + 4` en `0xff`: la próxima entrada al mundo vuelve a mandar el `A0` | MuRet no manda el `A0` (Q-C215) |
| `0x4c26da..0x4c26e7` | con `[0x576352c]` (el objeto que crea WinMain en `0x4bb993` y suelta al cerrar, `0x4af319`), `0x657670`, cuyo `0x4499b0`, si la caja de `0x723c80` está vacía (`0x449c10`: los 32 lugares de `0x48` bytes con el WORD en `0xffff`), pone en 0 `0x724580` y `0x724584` y manda otro `C1 03 31` (`0x449ad1`) | la sesión: el segundo `WriteWindowClose` (Q-C214) |

Lo demás que escriben `0x4c2480` y `0x4c2710` son globales de la interfaz, del chat, del gremio y
del juego que MuRet no tiene (`0x57678ed..0x5767960`, `0x6c82e8`, `0x6c8e3d..0x6c8e50`,
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

### Las misiones

(CL9 hito 2, 2026-11-13, *estático*.) El objeto de las misiones vive en `[0x710148]` (`Scenes::QuestState` en
`WorldRuntime`, salvo la ventana `+0x21383`, que es `UI::DelegatedPanels::quest`): `+4` la clase `& 7` (`0xff` antes
del primer `A0`), `+5` la evolución (`clase >> 3`), `+6..+9` los cuatro bytes del `9F`, `+0xc` los 200 registros de
`0x2a8` de `quest.bmd` (`0x418d10`, el XOR3 de `0x418de0` con la clave de `0x6c64b4`), `+0x2134c` los 50 bytes de
estado (dos bits por misión), `+0x2137e` la misión en curso (un byte, Q-C842), `+0x2137f` la mayor alcanzada,
`+0x21380..+0x21382` la lista, `+0x21384` el diálogo, `+0x21386` el estado (5: una condición no se cumple), `+0x21388`
el zen pedido, `+0x2138c` la pestaña y `+0x2138d` la marca de volver a leer la clase.

Un registro lleva su nombre (`0x20` bytes) y su cabecera, 16 pedidos de `0x14` desde `+0x26` (el `+6` enlaza
condiciones; `+7..+0xb` las marcas de clase; `+0xc..+0x13` los cuatro diálogos) y 16 condiciones de `0x14` desde
`+0x168` (misión pedida, nivel mínimo y máximo, zen y su diálogo), con dos bytes de relleno en `+0x166`. La marca de
la clase se lee como `+7 + clase` desde el pedido, y con una clase alta cae en el relleno (0) o en las condiciones
(`Scenes::ClassFlagOf`).

- **El estado** (`0x4195c0`, `0x419560`): el byte `i / 4` corrido `2 · ((i − i / 4) & 0xf)` bits (Q-C841).
- **El `A0`** (`0x418e20`), **el `A1` y el `A2`** (`0x418ee0`) y **el `A3`** (`0x4ddb80`): 04, "Las misiones y los
  NPC". El `A1` y el `A2` cierran todos los paneles (`0x657a90`, con sus dos `rand()` de los bytes protegidos de
  `0x6594a3` y `0x6597f9`) y abren el diálogo (`0x4198b0(−1)`).
- **Las condiciones** (`0x418fb0`): por cada pedido de la clase y cada condición enlazada, la misión pedida terminada,
  el nivel con el registro propio abierto y cerrado (cada cierre a 0 saca 3.848 `rand()`) y el zen. **Los ítems**
  (`0x4194c0` con `0x558410`). **El diálogo** (`0x418f40`, `0x419600`, `0x419710`): el texto `%d) %s` de cada
  respuesta va a un buffer de `0x48` bytes en la pila (más largo corta), y sin respuestas el texto 609.
- **La ventana** (`0x41ca40` el dibujo, `0x41ba20` y `0x41c600` sus dos vistas, `0x41b5a0` el paso, `0x41b9e0` el
  ítem, `0x419bd0` y `0x41a9e0` los pasos que mandan `A2`, `0x41b170` la pestaña): sin portar; cortan con su
  dirección (`GameHud.cpp`, `GameUi.cpp`).

MuRet: `Protocol::Quest`, `Scenes::Quests`, `Scenes::QuestReplies` y los diálogos de `UI::GameDialogs`.

### El alta de un monstruo

(CL9 hito 2, 2026-11-13, *estático*: las tablas están leídas del listado caso por caso.) `0x5141e0(tipo, x, y,
clave)` prueba primero seis creadores de evento, en este orden, y el primero que devuelve un personaje gana:
`0x439df0` (Kalima y los suyos, 56 tipos entre 144 y 338, en cualquier mapa), `0x42d070` (solo el mapa 30, 14 tipos),
`0x440530` (290 a 303), `0x42f190` (solo el mapa 35), `0x430c90` (solo el 34) y `0x425db0` (304 a 309). Después el
switch por `tipo − 1` sin signo hasta 300 (la tabla de bytes `0x517138` sobre la de saltos `0x516f44`: 124 casos y
el default `0x516e2a`, que carga `Monster01` y crea `0x152`; el 0, el 4 y el 8 además escriben su nombre y su arma).

Cada caso carga sus modelos (`0x612d90(n)`, `Monster{n + 1}.bmd` en `0x152 + n`, o `0x612270(n)` los NPC), crea con
`0x512870(clave, modelo, x, y, ángulo)` y escribe en el personaje: la escala `+0xc`, el sufijo `+4`, el nivel
`+0x1d6`, las partes `+0x218..+0x2da`, el nombre en `+0x1d9` (un `strcpy` sin tope, Q-C847) y otros campos (`+0x58`,
`+0x64`, `+0x68`, `+0x78`, `+0x88`, `+0x92`, `+0x98`, `+0x114`, `+0x118`, `+0x130..+0x138`, `+0x16c`, `+0x170`,
`+0x375`, `+0x386`, `+0x38a`, `+0x394`, `+0x3e4`); algunos visten (`0x512930`), ponen la caja (`0x5133b0`), la acción
(`0x4ea030`), la altura del terreno (`0x5f93c0`) o bajan `+0x18`, marcan mallas del modelo (73 y 75) o un byte de
otro (77), y cuelgan joints (`0x530860`), huesos con nombre (`0x401000`), sonidos o el efecto de llegada. Lo que
depende del mapa: el 34 pone la escala en 1 en Devil Square; la familia del 88 pone `+0x2ba` en 8 del mapa 8 al 12
(Q-C846). Dos casos sacan un `rand()`: `0x51443f` (163 a 173 impares) antes de cargar, para elegir el modelo y el
arma, y `0x514358` (224) después de la escala, para el sufijo `10 + r`. En el mapa 30, cuatro tipos miran el byte
del asedio (`0x4294b0`, Q-C330), y en el 34 el 348 gira a 90 en las columnas 62 y 183.

La cola `0x5140c0`: en los mapas 10 a 17 los tipos `0x54..0x8f` van con el nivel en 0 y la escala más
`(mapa − 9) / 3 · 0,05`; el nombre de la tabla de `0x7681eb8` si el tipo está (P-C174); `+0x376` el tipo, `+8` en 0,
`+0x3a4` el word de `0x57678b0`, y la clase del objeto `+0x88`: 4 para los NPC (201 a 259), 8 para las trampas (100 a
110) y 2 para el resto.

**`0x511290` con otro tipo** (fase 2). La construcción es la del jugador ([Los personajes de la
lista](#los-personajes-de-la-lista); 28 `rand()` desde cero) con lo que cambia por tipo: la clase queda solo con los
NewFace (`0xa8..0xac`); la acción 1 solo con el `0x20a`; la caja, con el piso en (−60, −60, 0) (`0x512270`) y, por la
tabla de bytes de `0x512700` sobre la de saltos de `0x5126e4` con `tipo − 0x154` sin signo hasta `0xb6`, el techo
(`+0x130..+0x138`): (40, 40, 120) para el `0x198..0x19a` y el `0x20a` (`0x5122d4`), (50, 50, 80) para el `0x154`,
`0x158`, `0x15b`, `0x163`, `0x165` y `0x166` (`0x5122f4`), (70, 70, 250) para el `0x15d`, `0x171`, `0x179`, `0x17c` y
`0x17e` (`0x512314`), (100, 100, 150) para el `0x177` (`0x512334`), (−120, −120, 0)..(100, 100, 300) para el `0x18f`
(`0x51233b`), (−90, −50, 0)..(90, 50, 200) para el `0x18e` (`0x51236f`) y (50, 50, 150) para el resto (`0x5123a9`). Los
huesos de las manos (`+0x2bd` y `+0x2dd`), por la tabla de `0x512824` sobre la de `0x5127b8` con `tipo − 0x152` hasta
`0x48`: 25 casos con su par (el `0x17b` y el `0x17c` solo escriben la derecha); los demás van a `0x5126b2`, que prueba
`0x43a250` (el `0x192` y el `0x194..0x197`, con sus pares), después `0x42d7f0` (el `0x19b`: la caja de (−140, −140, 0) a
(140, 140, 300), `+0x38f` en 0 y sin huesos; el `0x1f8`: la caja de (−200, −50, −100) a (200, 50, 100) y sigue) y si
ninguno devuelve 1 cuelga las manos del 33 y del 42. Además, para todos: `+0x58` y `+0x64` en −1, `+0x92` en 1, `+0x93`
en `0xa`, `+0x94` en 1,2, `+0x98` en 0, `+0xe8` en 1, `+0xd8` en 0, `+0x38a` en 0, `+0x394` en 0 y `+0x428` en −0x80
(`0x511be6..0x511d45` y `0x51248d`). `0x512870`, `0x510ff0` y `0x510f70` llaman a `0x401040`, que saca de la lista de
`0x6e5640` los huesos con nombre del personaje; `0x401000` agrega uno y antes saca el del mismo personaje y nombre.

**`0x5133b0`, la caja de los tipos 152 a 158** (`Box`): el techo (`+0x138`) más 70 (`0x6ba1b0`, en el x87), las alas
en `0x1a15`, el cuerpo en `0x102a + 0x200 · i` con nivel 13, la escala en 1 y `+0x111` en 9, sin `0x4ea030`.

MuRet: `Scenes::MonsterKinds` (las tablas en `MonsterKindTable.cpp`, `MonsterCreationOf`, `MonsterStepResolver`,
`ResolveMonsterSteps` y `MonsterTailOf`) y `Scenes::CreateMonster` (`MonsterCreation.h`), que corre los pasos en orden
con cada `rand()` en su lugar entre los del alta. `LoadMonster` mira que esté el `.bmd` (sin él corta, como el cierre
del original, P-C173); ni ese ni `LoadNpc` cargan el modelo, y `Action` corta sin el esqueleto del modelo (08,
P-C176). `MeshFlag` y `ModelByte` quedan en `WorldRuntime::modelPatches` y `BoneName` en `WorldRuntime::namedBones`;
el sonido, en `WorldRuntime::lastSoundId` (CL10). Cortan los joints y la llegada (CL11) y el asedio prendido (CL9.5),
08, P-C175. El nombre se guarda en `+0x1d9..+0x1f1` (el nombre, `+0x1e3` y `ActorState::nameSpill`); lo que pisaría
`+0x1f2` corta (Q-C847). `ConstructActor` pone la caja y los huesos por tipo.

### Los sonidos del modelo

(CL9 hito 3, fase 2c, 2026-11-13, *estático*: los 87 casos leídos del listado.) `0x612d90(n)` carga `Monster{n + 1}.bmd`
en el modelo `0x152 + n` y, después de sus acciones y su escala (`0x612dba..0x612dee`, la carga, que salta si el modelo
ya estaba), entra al switch de `0x613127..0x613130`: `n` sin signo hasta `0x56` sobre la tabla de saltos de `0x615340`.
Cada caso llama a `0x612c80(modelo, w0, …, w9)`, que copia las diez WORD en `+0xaa..+0xbc` del BMD del modelo
(`0x5769ad8 + modelo · 0xe0`); nadie más escribe esas WORD. Las leen `0x4f37c0` (la 0 y la 1, caminar,
`0x4f3efa..0x4f3f6c`), `0x4f4a10` (la 2 y la 3, pegar, [Pegar al llegar](#pegar-al-llegar)), `0x4f53a0` (la 2 y la
3, el golpe, `0x4f553d..0x4f55ac`) y `0x4f5660` (la 4, morir); −1 es sin sonido. Los tres primeros miran la primera
WORD: en −1, nada y sin `rand()`; si no, `rand() % 2` elige esa o la siguiente, más 5 con el subtipo 9 y más `0xd2`.

- 67 llamadas para 75 escrituras: el 0, el `0x1e`, el `0x46` y el `0x47` empujan sus WORD y comparten la llamada de
  `0x61322a`; el 4 lleva en ESI el `0x14` de `0x6134cd` (`0x6134d6`).
- El `0xc` y el `0xd` (los dos yetis) escriben **los dos** modelos, `0x15e` con `0x44, 0x44, 0x46, 0x46, 0x47`
  (`0x613a97`) y `0x15f` con `0x44, 0x45, 0x46, 0x46, 0x47` (`0x613ab5`), el resto en −1: cargar uno deja escrito el
  otro (Q-C1174).
- Sin `0x612c80`: el `0x26`, el `0x38`, el `0x3c`, el `0x3d`, el `0x4a`, el `0x4b`, del `0x4e` al `0x55` y del `0x57`
  en adelante (el default). El `0x56` escribe las diez en −1.

Lo llaman los casos del alta de un monstruo ([El alta de un monstruo](#el-alta-de-un-monstruo)), uno por cada modelo
que cargan. MuRet: `Scenes::WriteMonsterSounds` (`MonsterSounds.h`, las 75 escrituras con su caso), que
`CreateMonster` corre en cada paso `LoadMonster` después de mirar el `.bmd`, sobre `WorldRuntime::modelSounds`;
`ActorCombatContext::modelSounds` vacío cae en esa tabla; los tres sorteos, `Scenes::DrawModelSound`. MuRet no carga el modelo (P-C176): escribe siempre, y con los
mismos valores da lo mismo que el salto del original. Un modelo que nadie escribió queda fuera de la tabla (P-C253).
Los otros dos llamadores, que sí cargan el modelo (fase 2d): la fauna del 0x1f y el 0x21 (`FaunaModels::Prepare`, con
`FaunaModelLoadContext::sounds`) y el lancero `0x4d` del 30 (`0x42aa6b`, en `GameScene` después de
`WorldModels::Load`) la escriben solo si el modelo cargó con mallas, como el salto de `0x612dee`.

### Los monstruos y los NPC en el viewport

*Estático* (CL9 hito 2, fase 2, 2026-11-13); el cable con un GS real queda para la VM (08, P-C172). Los formatos, en
[04](04-protocolo.md#los-monstruos-y-los-npc-que-entran-13-1f-68-y-1a). Cada acceso a una celda protegida saca 4
`rand()`, como en [Los otros jugadores en el viewport](#los-otros-jugadores-en-el-viewport).

**El `13`, `0x4c9c60`**, por cada registro:

1. `0x5141e0(tipo, X, Y, clave)` ([El alta de un monstruo](#el-alta-de-un-monstruo)); un 0 corta el lazo.
2. Los estados de `+4` con `0x4e6380`.
3. El ángulo, `((+0xc >> 4) − 1) · paso`: el paso es 45 (`0x6b8848`) salvo con el modelo `0x1b5` (el 348 de
   Crywolf), que compara los `DWORD` de `+0x420` y `+0x41c` **sin abrir sus entradas** (`0x4c9d01..0x4c9d36`): 0 con
   la Y en `0x5a`, −135 con la X en `0x3e` y 90 con la X en `0xb7` (la X gana). Cerradas, tienen los cuatro
   `rand() & 0xff` del último cierre de `0x511290`, que nunca dan esos valores: el paso es 45 y el 90 del creador en
   las columnas 62 y 183 se pierde (Q-C849).
4. La celda protegida y el blanco en X e Y (8 `rand()`), `+0x39a`/`+0x39b`; `+0x42d` con el bit 7 de `+2` y `+0x93`
   con sus bits 4 a 6.
5. En Castle Siege (30), con `+0x376` en `0x115` (la puerta) y `+0x78` con `0x20000` o en 0, `0x4ea030` y `0x42a110`
   (`0x4c9f0f`).
6. Recién llegado (el bit 15 de la clave), `0x4c9790`; si no, la Y, la X (8 `rand()`) y `0x4eac00` hasta el blanco,
   con `+0x378` en 1 si hay camino.

**`0x4c9790`, el recién llegado**, por `+0x376`: el `0x15` y el `0x2c`, la acción 1 (`0x4ea030`), `+0x112` en 1, el
alpha en 1 y el sonido `0x13` o `0x14e`; el `0x35` y el `0x36`, `+0x3e5` en `0x3c` y el freno de 60 pasos con efectos y
`rand()` de `0x505de0` (`0x4c982b`); el `0x55`, `0x5b`, `0x61`, `0x72`, `0x78` y `0x7e`, el sonido `0x173`; el
`0x154`, `0x155`, `0x158` y `0x159`, lo que lee `[0x6e5904]` (P-C60 g) y los joints `0x615`, `0x614` y `0x5f6`
(`0x4c9883`). El resto (`0x4c9acb`): con el modelo `0x20a`, la acción `0xc1`, `+0x112` en `0xc1` y el alpha en 1; si
no, el alpha en 0.

**El `1F`, `0x4ca0b0`**, por cada registro: los tipos `0x98..0x9e` (los de `0x514390`, el modelo `0xc5`) van por
`0x517400(dueño, clave, tipo, X, Y, recién llegado)`: `0x5141e0`, el nivel (`+0x1d6`) en `tipo − 0x97`,
`wsprintfA(local, +0x1d9, dueño)` (el formato es el nombre del caso, la cadena vacía de `0x6e57a4`), el sufijo en 1
con el nivel 7 y los 24 bytes del local a `+0x1d9` (Q-C848); recién llegado, cuatro `rand()` y los joints `0x615`
(`0x51747c`). Los demás, `0x5141e0`. Después, como el `13`: los estados, el ángulo por 45, las celdas; `+0x88` en 1,
el PK (`+0xc & 0xf`) y, con 6 o más, el nivel en 1. Fuera de `0x98..0x9e`, el nombre: `+0x1e3` en 0, los diez bytes
del dueño, el Text 485 (`0x76e0a38`) y el nombre que tenía (`strcat`, sin tope, Q-C847). Recién llegado,
`0x4c9790` y el efecto `0xec` (`0x518cf0`) con el joint `0x5d5`/2 (`0x53fe70`), y con `0x98..0x9e` además el frame
(`+0x114`) y `+0x118` en 0 y el sonido `0x61` (`0x4ca630..0x4ca642`); si no, el camino.

**El `68`, `0x4e0980`**, por `+0`: con 1 (`0x4e0c1d`), `0x512870(clave, 0x20a, X, Y, 0)`, la clase de `+1` como el
`12`, la piel y el PK en 0, `+0x88` en `0x80`, los estados, las celdas, un `rand()` (`+0x428` en `0x96` y el ángulo
`r % 360`), el equipo de `+7` (`0x5139f0`) y el nombre `"   "` (`0x6c8648`) con `+0x1e3` en 0. Con 2 o 3
(`0x4e09ca`), `0x5141e0` con el tipo `+1 << 8 | +2` (un 0 pasa al siguiente, `0x4e0a06`), los estados, las celdas, el
`rand()` del ángulo y la puerta del asedio como el `13` (`0x4e0bd1`). Con otro, nada.

**El `45` y el `DF` sobre los monstruos** (del hito 1, `0x4c934c` y `0x4c6b80`): el `45` da de alta con `0x5141e0`;
el `DF` con el tipo `0xf1` empuja: `+0xe4` en 20, `+0xd0` en `(pkt[5] − X) · 400` y `+0xcc` en `(Y − pkt[6]) · 400`
con la X y la Y de la celda protegida (8 `rand()`), el sonido `0x1b` y los 20 joints `0x5c5` de `0x51fe10` (dos
`rand()` cada uno).

**El picking** (`0x505de0`, `0x505e15..0x505ebb`): la caja de `+0x13c` es el piso de `+0x124..+0x12c` más la
posición, con los lados `+0x130..+0x138` menos el piso (*inferido* que vale la posición de ahora, como Q-C100).

**MuRet** (`Scenes/MonsterViewport.h`, `Scenes/MonsterCreation.h`): `ApplyViewportMonsters`, `ApplySummons` y
`ApplyViewportObjects`, con `ArriveAsNew` (`0x4c9790`); `ApplyTransformed` da de alta con `CreateMonster` y el `DF`
empuja al `0xf1`. Cortan con su dirección, después de lo que el original hace antes: la puerta del asedio (`0x4c9f0f` y
`0x4e0bd1`, CL9.5), el freno del `0x35` (`0x4c982b`), la llegada del `0x154` (`0x4c9883`, CL11), la acción sin el
esqueleto del modelo (P-C176), el invocado que llega (`0x51747c` y `0x4ca5f6`, CL11), los joints del `DF` (`0x4c6c9d`,
CL11) y el nombre que pisaría `+0x1f2` (Q-C847). `CharacterPicking` usa la caja de cada tipo. Tests:
`MonsterViewportTest` (Lorencia, Devias, Devil Square, Castle Siege y Crywolf),
`ViewportPlayersTest.TheTransformedIsCreatedByItsType`, `ShownEquipmentTest`, `ActorConstructionTest`,
`CharacterPickingTest.TheBoxComesFromTheConstructedType` y `ServerSessionTest.TheMonsterPacketsReachTheSinkInWireOrder`.

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
| Los sprites de un frame (`0x5491e0`), con el billboard de `0x621400`: el dibujo del personaje pide los 30 destellos de la tela y los cinco del cetro, y `0x549440(0)` los dibuja con la modelview en la identidad y los apaga | `Render::MakeBillboard` (bit a bit contra `0x621400`), `World::FrameSprites` (la tabla, con el alpha de `0x549290`) y `Scenes::DrawFrameSprites` en un pase de ojo, el último del 3D, que se abre solo con alguno vivo; `DrawnCharacter::sprites` recibe los destellos al dibujar la tela y el cetro (`World::CreateScepterGlow`, bit a bit contra el caso `0x620` de `0x509340`) en ese orden, con el `rand()` de la CRT de `ClothWorld`. El original sortea el cetro en cada frame dibujado, a 21,33 por segundo; MuRet lo sortea una vez por frame del original (46,875 ms) que pasó. Desde el cierre de la CL7 el login lo sortea en la cola del juego (`LoginScene::DrawTheTail` → `Scenes::DrawCharacterTail`) y, entre medio, repite los sprites de lo que cuelga del último frame del original, como el juego y la selección con toda la tabla (`TheScepterGlowChangesOncePerOriginalFrame`); `World::ScepterGlowCarry` (el último sorteo en el hueso de ahora, como la tela, Q-C124) queda para `DrawCharacter` sin la cola (`TheGlowIsDrawnOncePerOriginalFrame`). Con varios frames del original de golpe, los de la tela y los del cetro salen agrupados y no intercalados (P-C22 a) | CL7.6 B1, cierre |
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
