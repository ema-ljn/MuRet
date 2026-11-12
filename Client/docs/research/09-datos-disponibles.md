# 09. Qué datos hay para reconstruir el cliente

Auditoría del 2026-11-01. Responde una pregunta: **¿alcanza lo que hay para reconstruir el
cliente completo, o falta algún dato?** Alcanza, salvo 91 sonidos y 4 modelos de monstruo que no
están en el `Data\` del laboratorio. El resto de lo que falta es lectura del binario, no datos:
el decompilado dice qué archivo usa cada cosa, y las trazas y la memoria dicen cómo se ve.

Los que faltan **se dan por faltantes** (decisión de Emanuel, 2026-11-01): ninguna de las dos
copias del `Data\` los trae, y no impiden un cliente funcional. El original saltea sin avisar los
modelos y sonidos que no encuentra, salvo `Monster`, `Player` y `PlayerTest`, que lo cierran
(*estático*, Q-C23), y MuRet hace lo mismo, con un aviso en el log: esos sonidos no suenan y el
mapa 5 queda sin objetos. `Monster50`, que falta, se pide con el nombre `Monster`: el original se
cierra cuando aparece el monstruo de tipo 58 (abajo), y qué hace MuRet lo decide la CL9.
Los tests golden no los piden.

| Fuente | Qué tiene | Dónde |
| --- | --- | --- |
| `Data\` del cliente | 5.726 archivos: 1.962 modelos, 3.044 texturas, 484 WAV, 26 temas de música, el terreno de 20 mundos | `mu-online-reverse-lab`, `analysis/decompilacion-maxima/assets/original-data/` (copia exacta por SHA-256 de la fuente); en MuRet, `Client/assets/Data/` desde el 2026-11-04, con su `Data.sha256` (los 5.726 hashes están en el `assets_manifest.csv` del laboratorio) |
| Segunda copia del `Data\` | 48 carpetas: las mismas sin `sound\` ni `Music\`. Comparada por nombre y tamaño en `Monster`, `Item`, `Skill`, `World6` y la raíz: no tiene ninguno de los 100 faltantes; `Wing02.bmd` y `Wing03.bmd` son más grandes (de 2009 y 2018), `Macro.txt` cambia y suma 5 texturas de 2009 y 2018 que la del laboratorio no tiene. Es una variante modificada de la misma base, que no sirve de referencia ([abajo](#las-dos-copias-del-data)) | `cliente-lab/Data` en la máquina de Emanuel (no se copió nada) |
| Decompilado de `main.exe` | qué archivo carga cada función y en qué slot o índice | `client-research/mainexe-vmxp-c32bbd5f/decompiled/` |
| GameServer reconstruido | el código fuente del servidor original, en C++ de VC6 | `gameserver/src/reconstructed/win32-vc6/` del repo de research |
| Datos del servidor en uso | 10 archivos de su `data`: `MonsterSetBase.txt`, `gate.txt`, `item` y `skill`, `commonserver.cfg` | `client-research/mu-golden/servidor-en-uso-vm2003/` |
| Memoria del cliente vivo | de Lorencia, la tabla de 1.845 slots de textura, el terreno, la luz, los atributos y, en otra lectura, los 2.870 objetos; de Devias, las tablas, el terreno, la luz y los atributos, **sin objetos** ([abajo](#los-volcados-de-memoria-de-cada-mapa)) | `client-research/mu-golden/memoria-cliente/` |
| Trazas de GL y sesión del cable | 5 frames de la selección y 5 de Lorencia; 3,5 minutos de juego ([04](04-protocolo.md), [06](06-render-escenas-y-ui.md)) | PR #45 del laboratorio; las de GL, en la máquina de Emanuel, en `analisis-mu-baires/lab-capture/sessions/graphics-01` (la carpeta de `MURET_CLIENT_GL_TRACE_DIR`) |

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
  andan modelos cuya textura está en otra carpeta. En la carga de arranque son 29 (**verificado**
  contra `lor_slots.bin`) y en el mundo del login 2 más (*inferido*, de la misma simulación), y
  `Helper04` dibuja la de `Skill\`, otro archivo con el mismo nombre.
- **`Object6` y `World6`** son el mapa 5 (*Exile*), que el cliente conoce pero no trae entero: sin
  `TerrainHeight` el original se cierra (Q-C180). Igual que el 35 sin `World36`, queda fuera del alcance de MuRet
  (Emanuel, 2026-11-12, P-C12): es una decisión de Webzen y MuRet hace lo del original.
- `Data2\` es la carpeta de desarrollo (modelos `.smd` y dos `.txt` de depuración): los
  cargadores la usan según `DAT_006c9e0c`, y no hace falta para jugar.
- El nombre `Monster\bossmap1_R.jpg.jpg` no es un hueco: el cargador corta el nombre en el primer
  punto y lee `bossmap1_R.OZJ`, que está.

La auditoría corre contra `MURET_CLIENT_GOLDEN_DIR`. Desde el 2026-11-04 esa carpeta es
`Client/assets/Data/`, en el repo (D-C7 del roadmap).

## Las dos copias del `Data\`

**Verificado el 2026-11-04**, por SHA-256 y corriendo los tests golden y de memoria con cada una.
La de referencia es la primera de la tabla de arriba, la de `Client/assets/Data/`: con ella pasan
todos los tests de datos, también `MemoryTextTable` y `MemoryItemTable`, que comparan con la
memoria del `main.exe` `c32bbd5f…`. Cuando los tests y los planes dicen "el `Data\` del
laboratorio" (`OfTheLab`, D-C9) es esta. La segunda, `cliente-lab/Data`, es una edición posterior
de un servidor privado y falla 17 de esos tests.

Sin mirar mayúsculas (las carpetas cambian: `Player\` en una, `player\` en la otra), 5.004
archivos son iguales, 33 cambian, 689 están solo en la de referencia (los 484 WAV, los 26 temas y
165 de `Local\`, como `Local\Logo\` e `itemtest.bmd`) y 17 solo en la segunda (`Monster\Monster71\`,
con fuentes `.smd`; `Item\Axe10.bmd`, que no pide nadie porque el bucle de las hachas va del 1 al
9, y cinco texturas de sus alas custom). De lo que cambia, lo que lee `main.exe`:

| Archivo | En la segunda copia |
| --- | --- |
| `Local\Text.bmd` | otra traducción: 1.669 textos no vacíos contra 1.645 (Q-C67), y del 1.717 al 1.738 escritos, que en la referencia están vacíos. El registro 714 también ocupa sus 300 bytes sin `\0`, además del 7 (Q-C07) |
| `Local\Item.bmd` | 688.132 bytes contra 688.212, y el checksum coincide; en la referencia no coincide y el original la carga igual (Q-C01) |
| `Local\Skill.bmd`, `Quest.bmd`, `MoveReq.bmd`, `Credit.bmd` | otros bytes |
| `Interface\lo_webzen_logo.OZT` | 53 bytes, un reemplazo vacío (en la referencia, 112.996): por eso la segunda falla `GoldenTextureSet.TheLoadingSetLoads` |
| `Item\Wing02.bmd`, `Wing03.bmd` | alas custom: 19.631 y 49.805 bytes contra 6.507 y 5.371 |
| `Player\` de los sets 16 y 26, `Wing03.bmd` y las texturas `*_14m` | reexportados con otra herramienta; las trazas de GL no los usan y pasan con las dos |
| los fondos de la carga, el login y la selección (`lo_back_01` y `02`, `Login_Back01` y `02`, `character_back01` y `02`) | recodificados |

Lo que cambia para el research:

- **La caja de la descripción de la lista** (P-C13 f, Q-C62) sale vacía con la referencia: los
  textos 1.715 a 1.745 no tienen nada. Una captura con texto en esa caja se sacó con la segunda.
- **La cara del texto** (los textos 0 y 18) está vacía en las dos: `[Video] FontFile` hace falta
  con cualquiera, también en Windows.
- **Las alas** (P-C18 b) van con las de la referencia.
- Las capturas a mano de la CL5.4 (paso 4 del
  [plan de la CL5](../../../docs/superpowers/plans/2026-11-02-ola-cl5-render-2d-y-login.md)) dicen
  "el `Data\` del laboratorio": con cuál de las dos se sacaron, sin verificar.

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

## Los volcados de memoria de cada mapa

**Verificado el 2026-11-09**, contra los archivos y los `SHA256SUMS` de cada carpeta de
`client-research/mu-golden/memoria-cliente/` y el README de `mu-golden`. Este documento decía que había
objetos de Lorencia y de Devias: de Devias no hay.

| Carpeta | Mapa | Qué tiene |
| --- | --- | --- |
| `2026-10-09-lorencia/` | 0 (`World1`), en la celda (136, 130) | el terreno, la luz, los atributos, la tabla de slots de textura (`lor_slots.bin`) y buffers de textura (`lor_*`, `lor2_*`) |
| `2026-10-09-lorencia-obj/` | 0, en la escena 5 y la celda (186, 121) | los objetos: el contador, las 256 cabeceras de bloque, la tabla de `0x606630` y las 256 listas, con 2.870 nodos de `0x1D4` bytes (`obj_*`) |
| `2026-10-09-devias/` | 2 (`World3`) | sólo `s31_*`: `alpha`, `att`, `bmphdr`, `gate`, `guard`, `height`, `item`, `l2l1`, `light`, `skill_live3` y `text`, más tres logs. **Ningún `obj_*`**, y tampoco los slots de textura |

Los objetos se leyeron con `s31mem.exe` (lectura pasiva, con la directiva `list`) y el
`spec-lectura-obj.txt` de esa carpeta. Sus direcciones son fijas de `main.exe` (`0x6C9DD0`, `0x6D5DC8`,
`0x07D36598`, `0x07D325D0` y `0x07D34818`), las mismas que nombra el desensamblado
([06](06-render-escenas-y-ui.md): las 256 listas en `0x7d325d0`, la tabla de `0x7d34818`), y no dependen
del mapa: el mismo spec sirve en Devias. Los tests ya lo decían así (`DeviasCapture` y
`LorenciaObjectsCapture`, en `Client/tests/support/GoldenDir.h`). El volcado de los objetos de Devias es
una captura nativa pendiente (P-C37 en [08](08-pendientes.md); [abajo](#qué-pedir-en-la-próxima-captura)).

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
3. Los objetos de Devias: con el personaje en el mapa 2 y en la escena 5, `s31mem.exe` con el mismo
   `spec-lectura-obj.txt` de `2026-10-09-lorencia-obj/` ([arriba](#los-volcados-de-memoria-de-cada-mapa),
   P-C37 en [08](08-pendientes.md)). Devias es el segundo mapa de referencia de la CL7.

## Cómo se verificó

Scripts de una página, fuera de los repos: las strings de `main.exe` (`c32bbd5f…`) con extensión;
las llamadas a `0x60da30`, `0x41eac0`, `0x63b870`, `0x63bd00` y `0x60d5f0` en los `.c` del
decompilado, con cada `s_…_ADDR` y `&DAT_ADDR` resuelto a la string de esa dirección del exe; las
mallas de los 1.953 `.bmd` con mallas (el contenedor de [05](05-formatos-de-datos.md)); el campo
`+0x2c` de `lor_slots.bin` contra los `glBindTexture` de las dos trazas; y los `PHeadSet*` del
servidor reconstruido contra la tabla de `paso5-protocolo-handlers.md`. No se imprimió ni se
versionó ningún asset.
