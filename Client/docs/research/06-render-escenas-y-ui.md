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
  está dada vuelta en Y respecto de la grilla. Coincide (±1 %) en 1.746 de los 1.869 vértices que se
  pueden medir (canal rojo > 8); los otros 123 salen más brillantes, nunca más oscuros (×1,1 a
  ×3,1): luces dinámicas que se suman a la base (fuego, efectos),
  *inferido*, sin identificar quién las suma.
- **El orden importa** para la mezcla y está verificado: bloques de 4 × 4 por (fila de bloque,
  columna de bloque), dentro del bloque las celdas por (y, x), y el pase de alfa de una celda
  inmediatamente después de su pase opaco (los 28 bloques y las 141 celdas con alfa de la traza).

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
  ese objetivo, que no sale del azar, sin leer (P-C14).
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
   y `0x456`; *inferido*, de la simulación de los slots de arriba y de los archivos, P-C15;
   Q-C92). El tercer y cuarto argumento van a `glTexParameteri` en `0x63b870` y `0x63bd00`: el
   tercero a `GL_TEXTURE_MAG_FILTER` y `GL_TEXTURE_MIN_FILTER` (`0x2800`, `0x2801`), el cuarto a
   `GL_TEXTURE_WRAP_S` y `_T` (`0x2802`, `0x2803`). Los fondos y casi todas las de `Interface\` van
   con `GL_NEAREST`; la arena, `FireSnuff` y `cr_mu_lo` con `GL_LINEAR`, y repiten la arena,
   `server_ex03` y `server_ex02`;
3. `0x65f300` arma las ventanas de la UI nueva (abajo);
4. `0x4c1340` conecta al ConnectServer ([03](03-red-y-cifrado.md)). El `connect` bloquea, y del 1 al
   4 corre todo en el primer paso de la escena: hasta que termina, en la pantalla queda la carga con
   la barra llena, y si el connect falla, el primer frame del login ya trae el aviso `0x70` (Q-C85);
5. el objeto `0xa2` en (10.680, 15.550, 1.710), con ángulos (10, 0, 36) y escala 1, que
   `0x606660` pisa (abajo), y el personaje de la escena (`0x517270(2, 4, …)`, con el cuervo del
   `A9`, P-C11) en (10.603, 14.112, 580), girado 188° en z y con escala 1,6; el caballo oscuro en
   que monta, el `0xbf` (`0x443fa0`, abajo; antes este documento lo daba como un efecto); 120 llamadas a `0x555190` con dos
   cadenas de la `.bss` (*inferido*: vacían el área de mensajes); la música (`0x4acfd0`) y una
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
  tercera solo con `+0x2f8` en `0x1c30`; las simula con 5 pasos por frame (`0x455190`) y las dibuja.
- **La cabeza:** `0x505de0`, en cada paso, con un personaje que no es el propio (el 0) y que se ve:
  con `rand() % 32` en 0 elige un objetivo nuevo, de −64 a 63 de giro y de −16 a 15 de inclinación,
  y se acerca un 20 % por paso (`0x6b8854`).

En la escena 2 lleva la ropa fija, el
casco y lo demás de `0x102e`, `0x122e`, `0x142e`, `0x162e` y `0x182e` (el juego 29), la maza
`0x620` en la derecha, el cuervo `0x1c17` en la izquierda, la capa `0x1c30` y el caballo oscuro
`0x1c16`; `0x512930` le deja la cabeza en -1. `0x420d60` le cuelga al cuervo un objeto aparte, de
`0x460` bytes (`0x4167b0`, en `personaje + 0x430`), con su propio paso: su conducta no se leyó
(P-C15). La acción la elige `0x634c60` en cada paso: con `ftol(T) % 2300` entre 0 y 99,
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
   una ventana lo achica), `0x5fbb00(w · (1/640), posición)` (el frustum, *inferido*) y
   `0x620fc0`, que hace `glPopMatrix` de la modelview y de la proyección. En la selección, que
   tiene la misma forma, la traza muestra esa perspectiva sin nada adentro (*verificado*, arriba);
4. `glClearColor(0, 0, 0, 1)` y el pase 3D en `0x620db0(0, 25, 640, 430)`: si la ventana de
   `+0x4578` está cerrada (su bandera de abierta, `+0xc`, es el byte `+0x4584` del administrador),
   el terreno (`0x5fc260(0)`), los personajes (`0x510d00`), `0x4453b0`, los objetos del mundo
   (`0x6046a0`, abajo: el logo), `0x53c6a0(0)` (una tabla de `0xa10` bytes con su mezcla por
   entrada: *inferido*, efectos), `0x52d620(0)`, `0x549510` y `0x5306a0`; después `0x6213e0`,
   `0x549440(0)`, `0x548550(0)` y `glPopMatrix` (CL6.4: antes este documento daba los objetos en
   `0x53c6a0`);
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

  Después, con `rand() % 3` en 0 y la acción distinta de 1, el polvo (`0x5f2`, o `0x607` en el mapa
  7), que `0x53fe70` pone en el arreglo de `0x743a198` (de `0x78` bytes; lo mueve `0x5446e0` desde
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
- **En cada paso** (`0x5fbf60`, desde la lógica del login y de la selección): copia `0x7c0ccc4` a
  `0x7b4ccc4` en las celdas de la ventana que se ve (`0x7d2d364` a `0x6c9d68` + 3 en x, `0x7d2d368`
  a `0x6c9d6c` + 3 en y), donde después se suman las luces dinámicas (`0x5f9780`).
- **En un punto** (`0x5f99e0`): con la escena distinta de 0, 1 y 3 y el terreno cargado
  (`0x7d2d35c`, que pone `0x5f9000` al cargar), la celda es x · 0,01 y y · 0,01 truncados, y entre
  0 y 254 la luz es la bilineal de `0x7b4ccc4` entre las cuatro de alrededor (primero cada columna
  en y). Fuera, (0, 0, 0). Como `ftol` trunca hacia el 0, de −1 a 0 todavía es la celda 0.

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
carga del mundo (`0x633c10`, 02); `+0x8498` (`0x6ba7d4`) la abren los avisos `0x36` y `0x37`.
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

`0x634680` sale con `0x7d3a13f` en 0 o el estado antes de `0x33`. La niebla en 0; los fondos
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
ángulo)` (la z, de `0x5f93c0`) y pone `+0x1f4` en el índice, la luz `+0xf4` en 0,3, la posición,
la clase, la piel, y `+0x78` y `+0x7c` en 0. En la escena 2 viste la ropa fija del login (§ El
login, al entrar); en las otras, casco, armadura, pantalón, guantes y botas en `0x2212`, `0x221a`,
`0x2222`, `0x222a` y `0x2232` más `clase & 7`, y las manos, las alas y la mascota en -1. Después,
`0x4e6910(0x1000000, c)`, `+0x1d8` en 0, `0x512930` y `0x4f2250` (la acción de parado).
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
| la mascota | v = `b[4] & 3`; con 3, `b[9] & 1` da `0x1c15`, `b[11] & 1` `0x1c16` (con el efecto `0xbf`) y `b[11] & 4` `0x1c37` (con `+0x31b` = `b[15] & 3`) | `0x1c12` + v |
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
`glFlush` y `SwapBuffers`), borra el objeto, pasa a la escena 5 y suelta las cuatro texturas.

## Lo que no se sabe

- **Modelos, en el binario** (P-C01, cerrado en la CL6.R): el skinning, la pose, el avance, la
  cabeza, el dibujo de una malla, los brillos por nivel y los slots de las texturas ya están
  (arriba). Falta (P-C14): si la selección gira la cabeza, la velocidad que queda en las acciones
  sin escritura, la acción de parado sin arma y con una de dos manos, el color de cada pase
  (`0x609640`) y una mano vacía; y el resto del render de modelos (`0x53c6a0`, tabla de 0xA10
  bytes con un `switch` de tipo), leído solo en sus dispatchers. Las
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
  `+0x3d0`, `+0x7e8` y `+0x4578`, las que abren los botones de la barra, ni qué contesta el CS
  al `F4 06` del botón de los créditos. Del login de la CL6 (P-C15): el slot de `Logo03`, cómo
  sube la luz del logo y la conducta del cuervo (`0x4167b0`).

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
| El login dibuja en el frame del render loop, con la cámara, el pase 3D y lo común a las escenas | `LoginScene::Render` en un `SceneCanvas` (el lote, el texto, el fin del 2D y el pase 3D), sin el fundido (Q-C66), y al final la UI (CL5.5): la lista, la barra de abajo, la cuenta y el aviso, cada una con la mezcla normal (Q-C80); `Gl::GlFrameTarget` limpia en negro, dibuja el lote y presenta | CL5.4, CL5.5 |
| Siete ventanas de la UI nueva en píxeles, con la imagen de la UI nueva (`0x647650`, la de la carga) en sus fondos; avisos por id con un texto de `Text.bmd` y un botón | `UI::` con la lista, la cuenta, el aviso y la barra de abajo, y sus imágenes con la forma de la carga (UV a medio téxel, la y desde arriba) en píxeles; la lista con 20 botones de servidor (Q-C63); el id del aviso y su texto en una tabla, la de arriba | CL5.5 |
| Los que leen los paquetes abren las ventanas: `0x4c1860` la lista con el `F4 06`, `0x4c1e20` la cuenta con el `F1 00`, `0x4e5ebc` el aviso del `F1 01` | la `ServerSession` no conoce la UI: dice en qué estado está (`ChoosingServer`, `WaitingCredentials`, `Failed`), y la `LoginScene` abre la ventana que va cuando el estado cambia. El OK de un aviso que cierra el juego lo pide la escena (`IScene::WantsToQuit`) y lo hace `Main` (Q-C69) | CL5.5 |
| Ventanas como clases con vtable de 22 métodos | `UI::Window` con una interfaz chica (dibujar, evento) y una clase por ventana; el administrador ordena por prioridad | CL8 |
| Cámara con seis globales, far × 1,4 en el mundo | `Camera` como valor, testeable: matrices de vista y proyección comparadas con las de las trazas | CL7 |
| Terreno celda por celda, dos pasadas por celda | una malla por bloque de 4 × 4 con las mismas reglas de capa, alfa y luz, y el mismo orden | CL7 |
| Modelos transformados en la CPU, un hueso por vértice, pose de `Player.bmd` | skinning en la CPU primero (se compara vértice a vértice con el original); pasarlo a la GPU queda para después, si hace falta | CL6 |
| El cuerpo avanza por paso de lógica con la velocidad de su acción | `Data::Model` guarda la velocidad de cada acción (la tabla de `0x60dbe0`) y el frame se avanza en el paso de 40 ms; el dibujo interpola entre el frame del paso anterior y el del actual (D-C4), y el modo fiel dibuja el del paso, sin interpolar | CL6 |
| Mascota y alas avanzan por frame dibujado (0,5 y 0,25 keys) | avanzan por tiempo: 0,5 keys cada 46,875 ms es la velocidad del original a 21,33 FPS, igual a cualquier FPS (Q-C20) | CL6 |
| Brillo de items con UV de la normal y el reloj del frame | un shader por modo de brillo con las fórmulas de arriba; T es el reloj del frame en ms | CL6 |
| Cuántos pases de brillo lleva una parte, por su nivel (`0x60b3d0`), con el tope de las opciones | `ItemGlow`: la tabla de arriba, una fila por nivel, y los 14 y 15 sin brillo (Q-C97) | CL6.3 |
| Los slots de las texturas de los modelos: un próximo que fija cada cargador y la búsqueda por nombre desnudo | `ModelTextureSlots` con el mismo próximo por cargador y la misma búsqueda (Q-C90), y un golden local contra `lor_slots.bin`; el login sigue el próximo del mundo (Q-C92) | CL6.1, CL6.3, CL6.4 |
| La selección: la lista en cinco lugares fijos, el picking contra una caja corrida, el doble clic por reloj y la luz 0,4 / 1,4 | `CharacterSelectScene` con las tablas de arriba; un lugar de más de 4 se descarta (Q-C101) | CL6.3 |
| La barra de la selección (crear, el menú, conectar y borrar) y el borrado con dos avisos | `UI::SelectionBar` (con `Button` deshabilitado), `UI::DeleteChecks` (las revisiones de `0x63edf0`), `UI::NoticeWindow` con los tipos 0, 3 y 4 y el campo del código, y la escena, que sigue la lista de la sesión (el que crea entra y el que borra se va) y abre el aviso de cada respuesta. La ventana de crear es de la PR B2; el menú, las opciones, los carteles y el área de mensajes, de la CL8 | CL6.5 |
| El caballo oscuro: un ayudante que aparece de a poco y sigue la acción del jinete, con luz por vértice, un cromo y su sombra | `Scenes/DarkHorse` (el paso) y `DrawDarkHorse` (el caso `0xbf` de `0x6002e0`), con `Render::Models::NormalLight` (L = (1,3; 0; 2), la normal con la escala del objeto) y `ModelBlend::BlendedShadow`; en el login, entre el personaje y el logo | CL6.4 |

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
