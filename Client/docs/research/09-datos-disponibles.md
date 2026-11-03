# 09. Qué datos hay para reconstruir el cliente

Auditoría del 2026-11-01. Responde una pregunta: **¿alcanza lo que hay para reconstruir el
cliente completo, o falta algún dato?** Alcanza, salvo 91 sonidos y 4 modelos de monstruo que no
están en el `Data\` del laboratorio. El resto de lo que falta es lectura del binario, no datos:
el decompilado dice qué archivo usa cada cosa, y las trazas y la memoria dicen cómo se ve.

Los que faltan **se dan por faltantes** (decisión de Emanuel, 2026-11-01): ninguna de las dos
copias del `Data\` los trae, y no impiden un cliente funcional. El original saltea sin avisar los
modelos y sonidos que no encuentra (*estático*, Q-C23) y MuRet hace lo mismo, con un aviso en el
log: esos sonidos no suenan, el monstruo de tipo 58 no se dibuja y el mapa 5 queda sin objetos.
Los tests golden no los piden.

| Fuente | Qué tiene | Dónde |
| --- | --- | --- |
| `Data\` del cliente | 5.726 archivos: 1.962 modelos, 3.044 texturas, 484 WAV, 26 temas de música, el terreno de 20 mundos | `mu-online-reverse-lab`, `analysis/decompilacion-maxima/assets/original-data/` (copia exacta por SHA-256 de la fuente) |
| Segunda copia del `Data\` | 48 carpetas: las mismas sin `sound\` ni `Music\`. Comparada por nombre y tamaño en `Monster`, `Item`, `Skill`, `World6` y la raíz: no tiene ninguno de los 100 faltantes; `Wing02.bmd` y `Wing03.bmd` son más grandes (de 2009 y 2018), `Macro.txt` cambia y suma 5 texturas de 2009 y 2018 que la del laboratorio no tiene. Es una variante modificada de la misma base | `cliente-lab/Data` en la máquina de Emanuel (no se copió nada) |
| Decompilado de `main.exe` | qué archivo carga cada función y en qué slot o índice | `client-research/mainexe-vmxp-c32bbd5f/decompiled/` |
| GameServer reconstruido | el código fuente del servidor original, en C++ de VC6 | `gameserver/src/reconstructed/win32-vc6/` del repo de research |
| Datos del servidor en uso | 10 archivos de su `data`: `MonsterSetBase.txt`, `gate.txt`, `item` y `skill`, `commonserver.cfg` | `client-research/mu-golden/servidor-en-uso-vm2003/` |
| Memoria del cliente vivo | la tabla de 1.845 slots de textura, terreno, luz, atributos y objetos de Lorencia y Devias | `client-research/mu-golden/memoria-cliente/` |
| Trazas de GL y sesión del cable | 5 frames de la selección y 5 de Lorencia; 3,5 minutos de juego ([04](04-protocolo.md), [06](06-render-escenas-y-ui.md)) | PR #45 del laboratorio |

## El `Data\` del cliente, contra lo que pide `main.exe`

**Verificado.** Un script cruzó tres cosas contra los archivos, sin mayúsculas ni minúsculas y
con `.jpg` → `.OZJ`, `.tga` → `.OZT`, `.bmp` → `.OZB` como hace el cliente:

1. las rutas literales de las strings de `main.exe`;
2. cada llamada a un cargador en el decompilado, con sus argumentos resueltos a la string que
   apuntan (el nombre del modelo se arma como `dir + nombre + NN + ".bmd"` en `0x60da30`);
3. el nombre de textura de cada malla de cada `.bmd`, contra su carpeta y contra las demás.

| Qué | Pedido | Está | Falta |
| --- | ---: | ---: | --- |
| Modelos con ruta fija (`0x60da30`) | 281 | 275 | `giftbox_bp`, `_br` y `_bb` de `Item\`, `Skill\ManyFlag`, `Object6\BossHead` y `Princess` |
| Modelos numerados (84 familias: `Monster`, `HelmMale`, `Sword`, `Tree`…) | 84 | 83 | `Object6\Meteo`; además `Monster50`, `76`, `81` y `89` entre el 1 y el 106 |
| Texturas con ruta fija (`0x63b870` OZJ, `0x63bd00` OZT) | 363 | 363 | ninguna |
| Texturas de las mallas de los `.bmd` | 3.421 | 3.420 | `waterring.JPG` de `Item\waterring.bmd`, un modelo que ningún cargador pide: no hace falta (69 de las que están, en otra carpeta) |
| Terreno (`EncTerrain`, `TerrainHeight`, `TerrainLight`, tiles) | 20 mundos | 19 | `World6` solo trae `.att`, `.map` y `.obj`, y no hay `Object6` |
| Música | 22 | 22 | ninguna |
| Sonidos (`0x41eac0`) | 532 | 441 | 91 (abajo) |

- **Qué pasa si falta algo**, *estático*: una textura que falta corta el juego (`0x63b870` arma
  "File not exist", `MessageBox` y `WM_DESTROY`), también la de un modelo (`0x60d5f0`); un modelo
  que falta no se carga y no avisa (el lector es `0x4f13d0`, y `0x60da30` sigue; `0x4f1ac0`, que
  este documento daba como el lector, **escribe** un `.bmd` en el modo de desarrollo), salvo que
  se llame `Monster`, `Player` o `PlayerTest`, que cierran el juego; un sonido que falta tampoco
  avisa (`0x41eac0` sale si `0x41e6d0` falla). Por eso el cliente corre con estos huecos (Q-C23
  en [07](07-quirks-y-bugs.md)).
- **Los 91 sonidos.** 35 son del personaje (`p*`: pasos, gritos, muerte, subir de nivel,
  pociones, levantar y tirar items), 17 de las skills (`s*`), 3 de NPCs, 27 de los monstruos del
  mundo 31 (`w31\`), 8 del castillo (`BattleCastle\`) y 1 de `w34\`. Los piden sobre todo
  `0x61b350` (53) y `0x616ec0` (33). En el `Data\` del laboratorio no hay ningún `.wav` que empiece
  con `p` ni con `s`, y la segunda copia no trae sonidos: se dan por faltantes.
- **`Monster50` sí hace falta.** El tipo de monstruo 58 (`0x3a`, caso de `0x5141e0`) pide
  `0x612d90(0x31)`, o sea `Monster50.bmd`, y el `MonsterSetBase.txt` del servidor en uso pone 63
  en el mapa 8. El nombre que pasa a `0x60da30` es `Monster`, así que cuando aparece uno, **el
  original se cierra** (`0x612dd6`..`0x612de1`: el `MessageBoxA` y el `WM_DESTROY` de Q-C23;
  *estático*, CL6.R). Antes este documento decía "no se dibujaría". La sesión del cable no pasó
  por ahí. Los otros tres números que faltan no los pide ningún tipo.
- **Cuatro modelos de versión `0x00`.** `Object3\Object25`, `27`, `33` y `90` llevan 0 en el byte
  de la versión: el lector los lee en claro, como los `0x0A`
  ([05](05-formatos-de-datos.md#modelos-bmd)).
- **Las texturas de las mallas se buscan por nombre**, sin la carpeta (`0x60d550`, Q-C90): por eso
  andan modelos cuya textura está en otra carpeta. En la carga de arranque y el mundo del login
  son 31 (**verificado** contra `lor_slots.bin`), y `Helper04` dibuja la de `Skill\`, otro
  archivo con el mismo nombre.
- **`Object6` y `World6`** son el mapa 5, que el cliente conoce pero no trae entero. Si se usa,
  sin leer.
- `Data2\` es la carpeta de desarrollo (modelos `.smd` y dos `.txt` de depuración): los
  cargadores la usan según `DAT_006c9e0c`, y no hace falta para jugar.
- El nombre `Monster\bossmap1_R.jpg.jpg` no es un hueco: el cargador corta el nombre en el primer
  punto y lee `bossmap1_R.OZJ`, que está.

Ningún archivo del cliente se versiona en MuRet: la auditoría corre contra
`MURET_CLIENT_GOLDEN_DIR` (decisión del roadmap).

## Qué función dice qué archivo va dónde

*Estático*, del decompilado. "Qué modelo es cada monstruo" o "qué evento suena con qué `.wav`"
está en estas funciones, con índices literales:

| Qué | Función | Qué fija |
| --- | --- | --- |
| Personaje | `0x60dbe0` | `Player.bmd`, las partes de cada clase y la velocidad de cada acción ([06](06-render-escenas-y-ui.md#cuánto-avanza-la-animación)) |
| Items | `0x60ff50` | los modelos de `Item\` por familia y número |
| NPCs | `0x612270` | sus modelos, partes y sonidos |
| Monstruos | `0x5141e0` y `0x612d90` | el tipo → modelo, escala y nombre (`switch` de 125 casos), y por modelo la velocidad de cada acción y 240 sonidos |
| Skills y efectos | `0x615700` | los modelos de `Skill\` |
| Objetos de cada mapa | `0x616ec0` (desde `0x61a560`) | un `switch` por mapa con sus modelos, texturas y 164 sonidos de ambiente |
| Sonidos del personaje | `0x61b350` (desde `0x61ccb0`) | 135 sonidos con su id |
| Texturas sueltas | `0x63b870`, `0x63bd00` | ruta y slot fijo de la tabla de texturas (227 de `Interface\`, 101 de `Effect\`) |
| Texturas de cada modelo | `0x60d5f0` | la carpeta de donde salen las texturas de sus mallas |
| Música | `0x4acf60`, `0x4acfd0` | una tabla de punteros a `data\music\*.mp3` |

## Las texturas de las trazas ya tienen archivo

**Verificado.** El volcado `lor_slots.bin` de `mu-golden` (la tabla de texturas en `0x7d3a198`,
1.845 slots de 0x38 bytes) guarda en `+0x2c` el nombre de textura de GL de cada slot. El volcado
es del 2026-10-09, con el build `2f16a9c6…`, y las trazas son de otro día con el `c32bbd5f…`, pero
los slots que se cargan al arrancar tienen el mismo nombre de GL en los dos: el texto en la GL 1,
el cursor en la 9 y los pases de brillo en `Shiny01`, `Chrome01` y `Chrome02`, que es lo que
dicen sus UV. Con eso cada textura de GL de las trazas da su slot, y el slot da su archivo por la
tabla de arriba:

- GL 1 = `FontInput` (el slot 0 del texto), 9 = `Cursor`, 18 a 23 = `Menu01_new`, `Menu02`,
  `Menu03_new`, `Menu04`, `Menu_Red` y `Menu_Blue` (la barra del HUD), 74 = `Menu03_new_AG`,
  879 = `Skill`, 912 = `accept_box01`.
- GL 115 = `Effect\Shiny01`, 120 = `Chrome01` y 141 = `Chrome02`: los tres pases de brillo de
  [06](06-render-escenas-y-ui.md), ahora con su textura.
- De las 77 texturas que usa la traza de Lorencia, 39 tienen slot y 19 archivo directo; las otras
  20 tienen slot pero no una carga con ruta fija (probablemente texturas de modelos, `0x60d5f0`,
  *inferido*). Las 38 sin slot (desde la GL 1.217) se cargan
  al entrar al mapa y cambian de número entre sesiones: los tiles de Lorencia (slots `0x23 + i`)
  son la GL `1351 + i` en la traza y la `1946 + i` en el volcado. En la selección, 25 de 72 tienen
  slot y 15 archivo.

Para el HUD (CL8) esto da un oráculo: cada quad de la traza tiene posición, UV y ahora archivo.
Para las texturas que se cargan con el mapa hace falta otra captura con `ImageLog` prendido en
GLIntercept (guarda cada textura y se compara por contenido) o deducir el orden de carga de
`0x616ec0`.

## El GameServer

`gameserver/src/reconstructed/win32-vc6/` es el código fuente del GameServer original
reconstruido en C++ de VC6: **1.587 de 1.594 funciones dan las mismas instrucciones** que el exe,
en 66 unidades (98,97 % de las instrucciones, 65,48 % sin ninguna hipótesis). Cubre el exe; los
parches del repack están catalogados y `MyDll.dll`, la tercera capa del servidor del laboratorio,
no está investigada. Además, `docs/gemelo/handlers-protocolo.md` anota los 58 handlers de lo que
manda el cliente (26 confirmados, 30 inferidos y 2 vacíos).

Con eso **la mitad del servidor del gameplay está escrita**: qué hace con cada paquete y qué
manda, campo por campo (`protocol.cpp`, `user.cpp`, `ObjAttack.cpp`, `ObjUseSkill.cpp`,
`Shop.cpp`, `Party.cpp`, `GuildClass.cpp` y el resto). Cruzado con el cliente (*estático*, por
los `PHeadSet*` y `headcode` de las unidades que hablan con el cliente, sin las del DataServer y
el JoinServer):

- El exe del servidor arma **99 opcodes** hacia el cliente y el cliente tiene handler para 85.
- **56 handlers del cliente no tienen emisor en el exe**: `0x00` y `0xF4` son del ConnectServer;
  `D3`, `D7` y `DF` (movimiento) llegan por el cable igual (`D3` ×229 en la sesión de
  [04](04-protocolo.md)), así que los pone otra capa; y el resto son sistemas que el exe no tiene,
  como el castillo (`0xB1` a `0xBD`), amigos y correo (`0xC0` a `0xC8`) y relaciones de gremio
  (`0xE1`, `0xE5`, `0xE6`). Si `MyDll` los manda o no los manda nadie, sin investigar.
- **14 opcodes del exe no tienen handler en el cliente** (`0x04` a `0x06`, `0x10`, `0x11`,
  `0x15`, `0x1D`, `0x2B`, `0x55`, `0x5A` a `0x5C`, `0xCF` y `0xFF`): ahí está la traducción del
  laboratorio (*inferido*; en el cable no aparecen).

Lo que el servidor no puede decir es la otra mitad: qué hace `main.exe` con cada paquete
(animación, efecto, ventana, sonido), qué clic o tecla lo manda y cuándo. Eso está en sus 141
handlers (91 con la mecánica confirmada en `paso5-protocolo-handlers.md`) y en sus emisores.

Del `data` del servidor hay 10 archivos (`mu-golden`); faltan `Monster.txt` (stats), las tiendas
y los drops, que sirven para los servidores de MuRet, no para el cliente.

## Qué pedir en la próxima captura

Los archivos que faltan ya no se piden (se dan por faltantes, arriba).

1. GLIntercept con `ImageLog` y `TimerLog` prendidos: las texturas de cada mapa por contenido y la
   hora de cada frame.
2. Una sesión con combate, skills, items y ventanas abiertas, con la hora de cada acción, para
   verificar los handlers de CL9 contra el código del servidor.

## Cómo se verificó

Scripts de una página, fuera de los repos: las strings de `main.exe` (`c32bbd5f…`) con extensión;
las llamadas a `0x60da30`, `0x41eac0`, `0x63b870`, `0x63bd00` y `0x60d5f0` en los `.c` del
decompilado, con cada `s_…_ADDR` y `&DAT_ADDR` resuelto a la string de esa dirección del exe; las
mallas de los 1.953 `.bmd` con mallas (el contenedor de [05](05-formatos-de-datos.md)); el campo
`+0x2c` de `lor_slots.bin` contra los `glBindTexture` de las dos trazas; y los `PHeadSet*` del
servidor reconstruido contra la tabla de `paso5-protocolo-handlers.md`. No se imprimió ni se
versionó ningún asset.
