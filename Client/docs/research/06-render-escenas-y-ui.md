# 06. Render, escenas y UI

Fuentes: `analysis/mainexe/functions/paso7-escenas.md`, `paso10-escenas-completo.md`,
`paso8-texto-y-coordenadas.md`, `paso11-texto-completo.md`, `paso12-hud.md`,
`paso13-ventanas.md`, `paso14-ventanas-interiores.md`, `paso17-mapeo-vtable-panel.md` y
`coverage-ui-sistemas.md`.

Todo este tema es **estático**: se leyó en el assembly y el pseudocódigo, sin compararlo con
capturas de pantalla ni con la memoria del cliente (salvo el asignador de slots de textura).
Describe la arquitectura, no el dibujo exacto de cada píxel.

## La máquina de escenas

El global `DAT_006d5dc8` dice en qué escena está el cliente:

| Valor | Escena | Render |
| --- | --- | --- |
| 2 | login (servidores, cuenta) | `0x6350b0` |
| 4 | selección de personaje | `0x634680` |
| 5 | en el juego | `0x6371a0` |

Hay además un estado de escena más fino en `0x7D3A10C` (por ejemplo `0x73` en el juego, `0x8A`
aviso) que manejan los handlers de red (`paso5-protocolo-handlers.md`).

## Cada frame de una escena

```text
render de la escena
 ├─ cámara       0x635ae0 + 0x44dc60 + 0x620db0 (proyección y modelview)
 ├─ pase 3D      fondo, frustum 0x5fbb00, mundo 0x620480, objetos 0x5fc260 / 0x53c6a0
 │               [selección] preview 3D del personaje 0x5f9780 + luz 0x620980 + nombre 0x4129f0
 ├─ pase 2D      0x6219b0 (ortho) … 0x621a90 (vuelve)
 │    ├─ UI en el juego 0x62db60 (20 KB, 52 llamadas GL)
 │    ├─ texto 0x554180
 │    └─ overlays comunes (chat, avisos)
 └─ SwapBuffers  en el render loop 0x637700
```

### Cámara

Seis floats globales: yaw, pitch y roll (`DAT_07d3686c/68/70`) y posición (`DAT_07d36884/88/8c`).
`0x620db0` arma la perspectiva (FOV, aspect, near y far en `DAT_006d5d48/40/44`), aplica
`glRotatef` ×3 y `glTranslatef`, habilita textura, depth test y culling, `glAlphaFunc(GL_GREATER,
0.25)` y niebla lineal si está activa. La cámara la mueve el mouse (botones derecho y medio,
rueda; ver [02](02-arranque-y-ciclo-de-vida.md)). En el mapa 6 con los nombres "webzen" y
"webzen2" hay una cámara especial.

### Primitivas 2D

- `0x6219b0` / `0x621a90`: entrar y salir del modo 2D (`gluOrtho2D`, viewport).
- `0x621b60(id, x, y, w, h, …, alpha)`: un quad texturizado (`GL_TRIANGLE_STRIP`). Es el átomo de
  toda la UI.
- Resolución de referencia 640 × 480 (`0x620db0` se llama con `(0, 0, 640, 480)`).

### Texto

```text
0x554180(x, y, texto, ancho, sombra, color)
  └─ 0x465950 (virtual) → 0x554000
       ├─ 0x4667a0 colores inline: el byte 0x02 marca un cambio de color y se saca del texto
       ├─ 0x466000 TextOutA sobre un DIBSection (GDI) → glTexImage2D
       │    ├─ 0x466560 slot de textura libre (devuelve 10000 + índice)
       │    └─ caché: árbol rojo-negro por hash del texto
       └─ 0x621b60 dibuja el quad
```

El texto se rasteriza con GDI y se sube como textura. El input de texto (`0x468320`) incluye el
IME coreano. Verificado: el asignador `0x466560` (reconstruido en `mu-client` con una sonda en la
VM).

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

- **El renderer del terreno no está identificado** (es el consumidor del frustum de `0x5fbb00`).
- El render de modelos (`0x53c6a0`, tabla de 0xA10 bytes con un `switch` de tipo) está solo en sus
  dispatchers; skinning y animación, sin analizar.
- Efectos: solo la estela (`0x52e590`).
- El dibujo píxel a píxel de cada widget (dentro de `0x62db60`).

Ver [08](08-pendientes.md).

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| OpenGL 1.x de función fija, matrices globales | `Render::` sobre OpenGL 3.3 core con shaders (decisión D-C3); las matrices son valores, no estado global | CL5 |
| Escenas por un entero global | `Scenes::SceneManager` con una clase por escena (`LoginScene`, `CharacterSelectScene`, `GameScene`) y transiciones explícitas | CL5 |
| Quad texturizado como átomo de la UI, 640 × 480 de referencia | `Render::SpriteBatch` en coordenadas de 640 × 480 escaladas a la ventana real | CL5 |
| Texto por GDI → textura, con caché | rasterizado con una biblioteca portable (FreeType o SDL_ttf), misma caché por texto; se conserva el byte `0x02` de color | CL5 |
| Ventanas como clases con vtable de 22 métodos | `UI::Window` con una interfaz chica (dibujar, evento) y una clase por ventana; el administrador ordena por prioridad | CL8 |
| Cámara con seis globales | `Camera` como valor, testeable: matrices de vista y proyección comparadas con las del original para los mismos ángulos | CL7 |

Tests que fija:

- La máquina de escenas: transiciones válidas (login → selección → juego) y que una inválida se
  rechace (CL5).
- Parseo de colores inline con `0x02` sobre strings literales (CL5).
- Las matrices de la cámara para ángulos y posición dados, contra el cálculo de `0x620db0` (CL7).
- El asignador de slots: los casos de `test_texture_slots` de `mu-client` (CL5).
