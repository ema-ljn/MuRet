# 10. Cobertura del roadmap: cada función del `main.exe` en una ola

Informe de la tarea CL.M del [plan de cobertura](../../../docs/superpowers/plans/2026-11-10-cobertura-roadmap-cliente.md),
2026-11-13. Responde una pregunta: **¿a qué ola del roadmap pertenece cada una de las 5.648 funciones
del decompilado?** Todas tienen respuesta: 3.594 son código de juego con una ola responsable, 1.887
son bibliotecas que MuRet reemplaza, 129 caen en exclusiones que el roadmap ya tenía y 38 son thunks.
No queda ninguna pendiente.

Asignar no es implementar. La matriz dice de quién es cada función y, aparte, qué se sabe de ella y
qué tiene MuRet; el avance que sale de ahí está en [El avance real](#el-avance-real). Falta la revisión
independiente que pide el plan antes de dar CL.M por cerrada ([Límites](#límites-y-revisión-pendiente)).

| Archivo | Qué es |
| --- | --- |
| [`coverage/mainexe-roadmap.tsv`](coverage/mainexe-roadmap.tsv) | la matriz: una fila por VA, las 10 columnas del inventario sin tocar y 9 nuevas |
| [`scripts/tools/build_roadmap_matrix.py`](scripts/tools/build_roadmap_matrix.py) | el generador: lee el research y MuRet y escribe la matriz |
| [`scripts/tools/check_roadmap_coverage.py`](scripts/tools/check_roadmap_coverage.py) | el verificador del plan (`--require-complete` exige 0 pendientes) |
| [`scripts/tools/test_check_roadmap_coverage.py`](scripts/tools/test_check_roadmap_coverage.py) | los controles negativos del verificador |

## Procedencia

| Fuente | Versión |
| --- | --- |
| Inventario del decompilado | `client-research/mainexe-vmxp-c32bbd5f/decompiled/inventory.tsv` del repo de research @ `14f0f931`; blob `0fda5f45`, SHA-256 `42cc4757…194c`, el mismo que fija el plan (@ `91359b2c`) |
| Binario | `main.exe` `c32bbd5f…`, el de `original/` en esa carpeta (el generador comprueba su SHA-256) |
| Grafo de llamadas | `client-research/analysis/mainexe/evidence/coverage/grafo-2f16a9c6/`: llamadas, saltos y tablas del build histórico, válido porque el `.text` es idéntico ([README](README.md#dos-builds-que-no-se-mezclan)) |
| Exports y research | los `.c` y `.asm` de `decompiled/functions/`, `analysis/mainexe/**/*.md` y `registro-niveles.tsv` |
| MuRet | `main` @ `0473805` (la CL8 mergeada): las citas de `Client/src` con la ola de su commit por `git blame`, los cortes, los tests, los docs y el [TSV del barrido de la CL8](../../../docs/superpowers/plans/2026-11-12-ola-cl8-funciones.tsv) |

El generador no ejecuta el PE: todo es lectura estática. Con las mismas entradas da el mismo TSV byte
a byte.

## El resultado

| Clasificación | Funciones | Bytes | Qué es |
| --- | ---: | ---: | --- |
| `game` | 3.594 | 2.329.285 | código del juego, cada una con su ola responsable |
| `library` | 1.887 | 126.893 | funclets de excepciones de C++ (1.163, 13.225 B), CRT estático (470, 70.584 B), libjpeg (198, 39.664 B), STL de MSVC6 (24, 3.066 B) y los inicializadores de globales que genera el compilador (32, 354 B) |
| `excluded` | 129 | 37.963 | [Fuera de alcance](../../../docs/superpowers/specs/2026-10-30-cliente-design.md#fuera-de-alcance): GameGuard (72, 29.306 B), ResourceGuard (37, 4.682 B), el launcher (9, 3.233 B) y el hash anti-tamper (11, 742 B) |
| `thunk` | 38 | 226 | 35 heredan la ola de quien los llama; 3 sin llamadas directas no tienen ola (dos van al sistema y uno a GameGuard) |
| `pending` | 0 | 0 | — |
| **Total** | **5.648** | **2.494.367** | lo mismo que el inventario |

Todas las funciones de juego se llaman `FUN_*`: los nombres que trae el inventario son de funclets,
thunks y CRT. 320 de las 3.594 (81.560 B) son **código muerto**: nadie las llama, las apunta ni
las inicializa, o solo las alcanza otro código muerto. Igual tienen ola, la de su vecindad, para que
quien la porte confirme que siguen muertas.

### Por ola

La ola responsable cuenta los bytes una sola vez. *Vivo* es sin el código muerto; las columnas de
estado se explican en [Los estados](#los-estados).

| Ola | Funciones | KB | % del juego | KB vivos | implementado | parcial | sin cita | sin empezar |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| CL0 | 0 | 0 | 0 | 0 | — | — | — | — |
| CL1 | 13 | 4,7 | 0,2 | 4,2 | 4,1 | 0 | 0 | — |
| CL2 | 51 | 23,3 | 1,0 | 22,5 | 13,4 | 0 | 9,0 | — |
| CL3 | 127 | 83,3 | 3,7 | 80,5 | 12,6 | 60,2 | 7,7 | — |
| CL4 | 27 | 16,4 | 0,7 | 15,2 | 1,8 | 12,1 | 1,3 | — |
| CL5 | 176 | 41,8 | 1,8 | 35,4 | 19,0 | 0 | 16,4 | — |
| CL6 | 181 | 75,8 | 3,3 | 72,2 | 41,1 | 17,4 | 13,7 | — |
| CL7 | 864 | 699,5 | 30,8 | 658,2 | 123,0 | 358,4 | 176,9 | — |
| CL8 | 1.111 | 840,8 | 37,0 | 821,0 | 131,9 | 555,2 | 133,9 | — |
| CL9 | 1.014 | 469,3 | 20,6 | 466,9 | — | — | — | 466,9 |
| CL10 | 20 | 12,1 | 0,5 | 11,3 | — | — | — | 11,3 |
| CL11 | 10 | 7,7 | 0,3 | 7,7 | — | — | — | 7,7 |
| **Total** | **3.594** | **2.274,7** | **100** | **2.195,0** | **347,0** | **1.003,2** | **359,0** | **485,8** |

CL0 no tiene funciones: son las fundaciones de MuRet, sin contraparte en el binario. La CL7 y la CL8
juntan el 68 %: el mundo, sus objetos y efectos, y la interfaz, que en el original son funciones
enormes. La CL9 es el 20,6 %.

**Consumidoras.** Una función compartida tiene una sola ola responsable y la lista de las otras que
la usan (`consumer_waves`). Esa vista cuenta los bytes de nuevo en cada consumidora, así que no suma
el total: la CL9 aparece como consumidora en 781 KB de funciones de otras olas (sobre todo de la CL8:
las ventanas que muestran estado del juego), la CL7 en 306, la CL6 en 283, la CL8 en 282, la CL11 en
265, la CL5 en 169, la CL4 en 100, la CL10 en 98, la CL2 en 82, la CL3 en 69 y la CL1 en 11.

## Los 821 KB y el 36 % del traspaso

El [traspaso de la CL7](../../../docs/superpowers/plans/2026-11-10-cl7-traspaso.md) estimó "unos 821
KB de código de juego, el 36 %, sin ola asignada". La consulta que dio ese número no quedó escrita.
La reconstrucción con la matriz: el código de juego cuya ola **no** sale de algo que ya existía el
2026-11-10 (una cita de `Client/src`, un corte, un doc o plan de MuRet que lo nombre) sino de las
reglas de CL.M o del barrido de la CL8 (escrito el 2026-11-12, después del traspaso). Da **807 KB,
el 35,5 %** del código de juego (2.402 funciones): el número del traspaso se reproduce con un 2 %
de diferencia. Sin el barrido de la CL8 serían 629 KB (27,6 %). Con la matriz, el código de juego
sin ola es 0.

```bash
# la columna evidence dice de dónde salió la ola: base=src, corte, doc, papel, tsv-cl8 o una regla de CL.M
python3 -I - <<'EOF'
import csv
rows = list(csv.reader(open('Client/docs/research/coverage/mainexe-roadmap.tsv', encoding='utf-8'),
                       delimiter='\t', quoting=csv.QUOTE_NONE))
game = [dict(zip(rows[0], r)) for r in rows[1:] if r[10] == 'game']
prior = ('base=src;', 'base=corte;', 'base=doc;', 'base=papel;')
sel = [r for r in game if not any(p in r['evidence'] for p in prior)]
total = sum(int(r['size_bytes']) for r in game)
size = sum(int(r['size_bytes']) for r in sel)
print(len(sel), round(size / 1024), 'KB', round(100 * size / total, 1), '%')
EOF
```

## Cómo se asignó

El generador aplica las reglas en este orden y la primera que alcanza decide. La columna `evidence`
de cada fila dice cuál fue (`base=…`), con el `.asm` de la función y la cita que la sostiene.

1. **Lo que MuRet ya asignó** (`src`, `corte`, `tsv-cl8`, `doc`, `papel`): las funciones que cita
   `Client/src` toman la ola dominante de esas líneas por `git blame` (1.065 funciones, 1.278 KB);
   las que solo aparecen en un corte hacia la CL9 o más, la de ese corte; las del barrido de la CL8,
   la que dice el TSV; las que nombra un doc o plan de MuRet, la que ese doc les da. Cuatro gigantes
   que mezclan olas toman la de su papel (`papel`): `WinMain` (`0x4b9b80`) y `WndProc` (`0x4af4e0`)
   la CL3, el dispatcher de recepción (`0x4e2b10`) la CL4 y el paso por frame de las escenas
   (`0x636290`) la CL3.
2. **Las exclusiones del roadmap** y lo que solo ellas alcanzan ([abajo](#bibliotecas-thunks-y-exclusiones)).
3. **El grafo** (`grafo`): una función sin ola toma la de quien la llama, la apunta o instala la
   vtable que la contiene; un funclet de excepciones cuenta como la función que lo usa. Si la llaman
   varias olas, va a la más baja y las otras quedan como consumidoras. Se repite hasta que no cambia.
4. **El dispatcher** (`opcode`): cada función que llama `0x4e2b10` toma la ola del opcode de su
   caso, según la tabla de `paso5-protocolo-handlers.md` del research: conexión CL4, mundo CL7, chat,
   vida e inventario CL8, el resto de los paquetes del juego CL9 por sub-ola (viewport 9.1, combate
   9.2, ítems 9.3, social 9.4, eventos 9.5). Los tres opcodes de misiones (`A0` a `A2`) van a la CL9
   sin sub-ola: la CL9 todavía no la tiene.
5. **Las ramas de los gigantes** (`rama`, `gigante`): dentro de los cuatro gigantes, una llamada toma
   la ola de la cita de MuRet más cercana (a menos de `0x200` bytes); si no hay ninguna, la del papel
   del gigante.
6. **Los efectos** (`efectos`): lo que solo alcanzan las 13 raíces de efectos del roadmap (`0x518cf0`,
   `0x5208a0`, `0x52d620`, `0x530860`, `0x535840`, `0x53c6a0`, `0x52e590`, `0x5491e0`, `0x549510`,
   `0x549440`, `0x53fe70`, `0x5446e0`, `0x548550`) y MuRet no porta va a la CL11.
7. **Lo que usa y lo que comparte** (`usa`, `vtable`, `datos`, `dominio`): una función que nadie
   alcanza toma la ola de lo que llama, de la vtable que instala o de los globales que comparte con
   pocas funciones; las que usan OpenGL van a la CL5, el IME a la CL8, `wzAudio` y los archivos de
   sonido a la CL10, el reloj a la CL3.
8. **Lo leído a mano** (`lectura`): las 107 que ninguna regla alcanzó ([abajo](#lo-leído-a-mano)).

## Bibliotecas, thunks y exclusiones

**Bibliotecas.** Se reconocen por rango y por forma, con el `.c` de cada una: el CRT estático desde
`0x69eff0` (`criterio-cobertura.md` del research), libjpeg entre `0x66b8f0` y `0x67f0c0`, los
funclets `Unwind@`/`Catch@` que apuntan las tablas de excepciones, el `std::basic_string` y las
listas, deques y vectores de la STL de MSVC6 (refcount en `ptr[-1]`, `_Xlen` en `0x69effb`, `_Xran`
en `0x69f227`) y los inicializadores de globales que llaman a un constructor o registran un destructor
con `atexit` (`0x69f8e4`). MuRet usa la biblioteca estándar de C++, libjpeg-turbo y el manejo de
excepciones de su compilador. El tramo entre `0x67f530` y `0x69eff0` no es biblioteca: es código de
Webzen enlazado al final (el manejador de caídas de la CL3, el SimpleModulus de la CL2, GameGuard).

**Thunks.** Un thunk hereda la ola de quien lo llama; si nadie lo llama directamente, la de su
destino. Su `implementation_state` es `not-applicable`: lo cubre la ola del destino.

**Exclusiones.** Se aplican las que ya estaban en Fuera de alcance, sin agregar ninguna: el launcher
y el anticheat del propio `main.exe`. Ninguna se porta; el launcher queda para más adelante, por si
MuRet llega a tener uno propio o una reinterpretación (Emanuel, 2026-11-13). Cada fila excluida
empieza su `purpose` con el nombre de su grupo y dice el motivo en `next_action`, así que se las
puede filtrar por grupo:

| Grupo | Funciones | Bytes | Qué es y por qué no se porta |
| --- | ---: | ---: | --- |
| GameGuard | 72 | 29.306 | el anticheat nProtect GameGuard, enlazado en el exe ([abajo](#gameguard)); MuRet no corre GameGuard |
| ResourceGuard | 37 | 4.682 | `0x681ec0`: al entrar al mundo arma un objeto, lee la ruta del exe y chequea los recursos, pero el `jmp` de `0x4c4ad5` saltea el aviso y el resultado no se usa (Q-C147 de [07](07-quirks-y-bugs.md)) |
| hash anti-tamper | 11 | 742 | la tabla hash que arma el inicializador `0x4acef0` con tamaños al azar (`0x447f80`..`0x448310`, [02](02-arranque-y-ciclo-de-vida.md)), leída a mano. El `rand()` que gasta la protección se cuenta aparte: Q-C125 de 07 y P-C22 de [08](08-pendientes.md) |
| launcher | 9 | 3.233 | el fin del auto-update (`0x4b96b0` y su copia `0x4b9550`: renombra `mu._xe`, `mumsg._ll`, `wz_zp._ll` y `message._tf`) y `wzPopLaunchInfo` (`0x6922c0`), que lee del registro el servidor que dejó `Mu.exe` ([02](02-arranque-y-ciclo-de-vida.md#del-launcher-al-juego)) |

Una función queda excluida si solo la alcanzan funciones excluidas o código muerto (los métodos de
una clase que se llaman entre sí, también). Una llamada desde código que Ghidra no puso en ninguna
función cuenta como de la función excluida que tiene al lado, o si no la hay, de la que sigue al
hueco.

### GameGuard

Nuevo en este informe. Antes caía en la CL7 por una cadena de inferencias débil (una llamada a
`0x4505d0`).

- `0x450380` arma el texto `"GameGuard Error : %lu"` para los códigos de error de GameGuard
  (`0x6e`..`0xdc`) y no tiene llamadas.
- `0x4505d0` es el callback que recibe los mensajes de GameMon (`0x3e9`..`0x3f8`: error y cierre de
  la comunicación, speedhack, hack detectado, autenticación) y los muestra en un `MessageBoxA` con
  `"Error"`. Lo llaman `0x688590` y `0x6891d0`.
- La librería la arranca un inicializador estático: el código en `0x68653e`, fuera de toda función,
  llama a `0x686550`, que pone un filtro de excepciones temporal, crea un objeto de `0x128c` bytes y
  lo inicializa con `0x6869f0` (registro, `CryptVerifySignatureA`, `CryptDecrypt`, `VirtualProtect`,
  hilos y los `LoadLibrary` con nombres cifrados de `0x688b60`..`0x68bbb0`, que
  [05](05-formatos-de-datos.md#el-pipeline-de-las-dlls) dejó como "probablemente de la protección").

**Por qué no se porta.** Es un anticheat: MuRet no lo corre, como tampoco FoxAnti, y no cambia
nada del juego que se ve ni del protocolo. **Los textos de `Text.bmd`.** Los avisos que muestra
son los registros 790 a 800 de `Text.bmd` (`[error1001]` a `[error1011]`: "No se puede conectar a
nProtect", "Se ha detectado un hackeo de velocidad", "GameGuard ya se está ejecutando"…), y del 801
al 808 hay registros reservados (`Reserve : GameGuard`). Solo los usan estas dos funciones: el
callback `0x4505d0` lee el 792, el 793, el 794 y el 799, y `0x450380`, el 792, el 795 al 798 y el 800
(ninguna otra función del decompilado apunta a los registros 788 a 808). MuRet carga `Text.bmd`
entero (CL1), así que los textos quedan en la tabla aunque nadie los muestre.

Las 72 funciones: `0x450380`, `0x4505d0`, `0x686550`, `0x6867f0`, `0x6869a0`, `0x6869f0`, `0x688260`, `0x688270`, `0x688400`, `0x688590`, `0x688640`, `0x6886e0`, `0x688730`, `0x688980`, `0x688a80`, `0x688b60`, `0x688dd0`, `0x688eb0`, `0x688fa0`, `0x6890b0`, `0x6891d0`, `0x689520`, `0x689b20`, `0x689c00`, `0x689ce0`, `0x689ef0`, `0x68a140`, `0x68a170`, `0x68a250`, `0x68a3b0`, `0x68a4d0`, `0x68a6e0`, `0x68a780`, `0x68a940`, `0x68ac00`, `0x68b690`, `0x68b8b0`, `0x68b960`, `0x68b980`, `0x68b9a0`, `0x68b9d0`, `0x68b9f0`, `0x68bbb0`, `0x68bc00`, `0x68be30`, `0x68c200`, `0x68c210`, `0x68c230`, `0x68c240`, `0x68c3a0`, `0x68c420`, `0x68c4a0`, `0x68c5e0`, `0x68c650`, `0x68c6a0`, `0x68c720`, `0x68cb50`, `0x68cba0`, `0x68cc10`, `0x68d340`, `0x68d640`, `0x68d730`, `0x68dce0`, `0x68dd70`, `0x68de10`, `0x68de80`, `0x68e320`, `0x68e390`, `0x68e510`, `0x6915c0`, `0x691600`, `0x6916a0`.

## Lo leído a mano

Tras las reglas quedaban 107 funciones (12,8 KB), 77 de ellas muertas o sin uso. Se leyeron en su
`.c` y su `.asm` y la decisión está en el diccionario `MANUAL` del generador, con el motivo:

| Qué | Ola |
| --- | --- |
| `MessageBox` centrado con un hook `WH_CBT` (`0x401bb0`..`0x401d30`), un log a archivo (`0x41e1c0`, `0x41e220`), el buffer de DirectInput (`0x41e420`), cerrar un proceso por nombre (`0x4b95e0`), el parseo de la línea de comandos (`0x4acd50`, `0x4ace00`), el destructor del manejador de caídas (`0x680950`..`0x6808d0`) | CL3 |
| un cifrado por bloques de 16 bytes sin uso (`0x68f4b0`, `0x68f540`) y el objeto de la vtable `0x6bd884` (`0x685a40`..`0x685a90`) | CL2 |
| la llamada vacía del `WM_TIMER` 1000 antes del keepalive (`0x450376`) | CL4 |
| los parámetros de un programa ARB por `wglGetProcAddress`, sin uso (`0x66b1c0`..`0x66b350`) | CL5 |
| matemática de vectores (`0x5fc6c0`..`0x5fcc90`) | CL6 |
| la clase de la vtable `0x6b8d38`, los recorridos de los conjuntos del registro del personaje (`0x40cec0`..`0x40db80`), pasos y celdas del terreno (`0x4e9920`, `0x5ae300`, `0x5f9670`, `0x5fd2c0`) | CL7 |
| setters y listas de ventanas, el destructor de la vtable `0x6ba970`, el filtro de palabras (`0x5769e0`), abrir una URL (`0x6200e0`) | CL8 |
| búsquedas en el viewport (`0x5132d0`, `0x517920`, `0x5178f0`) y atributos de ítem por nivel (`0x54e610`, `0x54ebf0`) | CL9 |
| escribir un WAV con `mmio` (`0x41f380`, `0x41f3e0`) | CL10 |
| el hash anti-tamper (11) y los métodos sin uso de la librería de GameGuard (9) | excluidas |

## Los gigantes y la frontera CL7/CL11

Una función grande con ramas de varias olas cuenta sus bytes una vez, en su ola responsable, y
lista las demás como consumidoras. Las de más de 20 KB son 17: 12 de la CL8 (`0x5ca250`, `0x56c440`,
`0x623e70`, `0x5d8d50`, `0x576eb0`, `0x57cfc0`, …), con la CL9 entre sus consumidoras porque cortan
hacia ella; 4 de la CL7 (`0x4fd5f0`, `0x50ba30` y dos raíces de efectos) y `WndProc`. Su parte pendiente cae en
[El avance real](#el-avance-real).

Las 13 raíces de efectos son de la **CL7** (las obligaciones de la CL7.6 que ya porta) y llevan a la
**CL11** como consumidora: once están en `partial` y dos (`0x549510`, `0x549440`) completas. La CL11
es responsable de 10 funciones más (7,7 KB) que solo alcanzan esas raíces y MuRet no porta. Lo que
falta de los efectos dentro de las raíces queda en la parte pendiente de la CL7; la CL11.R lo reparte
caso por caso, como pide el [roadmap](../../../docs/superpowers/specs/2026-10-30-cliente-design.md#la-ola-de-efectos-visuales-cl11).

## Los estados

Son independientes de la ola. `analysis_state` describe lo que se sabe de la función;
`implementation_state`, lo que tiene MuRet; `validation`, el test que la cita.

| Estado | Regla | Funciones de juego |
| --- | --- | ---: |
| `verified` | `registro-niveles.tsv` del research la valida con una observación (traza, VM, cable) | 31 (16 KB) |
| `static` | la cita MuRet o el research, o se leyó a mano | 1.687 (1.669 KB) |
| `inferred` | su ola sale del grafo o de una regla, sin lectura propia | 1.876 (589 KB) |
| `implemented` | la cita `Client/src` sin cortes y no es un gigante | 772 (347 KB) |
| `partial` | la cita `Client/src` pero tiene ramas cortadas o es un gigante | 297 (1.003 KB) |
| `unknown` | es de una ola cerrada (CL1 a CL8) y `Client/src` no la cita | 1.194 (359 KB) |
| `not-started` | es de la CL9, la CL10 o la CL11 y no está citada | 1.011 (486 KB) |
| `not-applicable` | código muerto, biblioteca, thunk o exclusión | 320 de juego |

`unknown` no quiere decir que falte: el código de MuRet porta muchas funciones chicas sin nombrar su
dirección (un helper que se volvió una línea, una tabla que se leyó de otra manera). La acción de esas
filas es comprobarlo en su ola. 713 filas de juego (1.158 KB) citan un test de MuRet en `validation`.

## El avance real

Sobre los 2.195 KB de código de juego vivo:

| Parte | KB |
| --- | ---: |
| `implemented` | 347 |
| lo hecho de las `partial` (la fracción de sus citas que no son cortes) | 866 |
| lo que falta de las `partial`: CL9 92, CL7 27, CL8 17, el resto 2 | 137 |
| `not-started`: CL9 467, CL10 11, CL11 8 | 486 |
| `unknown`: CL7 177, CL8 134, CL5 16, CL6 14, CL2 9, CL3 8, CL4 1 | 359 |

**El avance está entre el 55 % y el 72 %**, según cuánto de lo `unknown` falte de verdad: 55,3 % si
falta todo, 63,4 % si falta la mitad y 71,6 % si MuRet ya lo porta sin citarlo. Lo que seguro falta
es la **CL9, unos 559 KB (el 25 % del código vivo)**, la CL10 (12 KB), la CL11 (8 KB más los casos
de efectos dentro de las raíces de la CL7) y las ramas cortadas de la CL7 y la CL8 (44 KB).

Es una cota por bytes del binario, no un plan de trabajo: no mide QA, la VM ni lo que cuesta cada
byte. La fracción de una `partial` sale de contar citas (cortes contra líneas que la portan), no de
leer qué ramas faltan. El generador la escribe en el resumen (`--summary`, clave `progress`).

## Límites y revisión pendiente

- **El 26 % del código de juego tiene ola `inferred`** (589 KB): la sacó el grafo, sin una lectura
  de la función. Un llamador de otra ola o una tabla que el grafo no ve la puede mover.
- **El grafo no ve todo**: llamadas por punteros armados en ejecución, tablas de funciones fuera de
  `.rdata` y código que Ghidra no puso en ninguna función. Por eso el código muerto tiene ola y su
  acción es confirmarlo.
- **Para revisar en la revisión independiente** que pide el plan: la exclusión de GameGuard (72
  funciones, nueva en este informe), las del hash y ResourceGuard; los gigantes de la CL8 con ramas de
  la CL9; la frontera CL7/CL11 en las raíces de efectos y las funciones de monstruos específicos
  (`0x4271d0` `Monster100`, `0x441400` `Monster81`, `0x434180` `Monster94`, …) que el grafo dejó en
  la CL7 por su llamador, `0x50ba30`, y podrían ser de la CL9 o la CL11.
- Hasta esa revisión, CL.M tiene la matriz completa pero no está cerrada.

## Cómo regenerar y verificar

Con los clones de [scripts/README.md, El entorno](scripts/README.md#el-entorno) (`$RESEARCH`) y
desde la raíz del repo:

```bash
python3 -I Client/docs/research/scripts/tools/build_roadmap_matrix.py --research "$RESEARCH" \
    --repo . --ref origin/main --out Client/docs/research/coverage/mainexe-roadmap.tsv \
    --summary /tmp/resumen-clm.json
INV="$RESEARCH/client-research/mainexe-vmxp-c32bbd5f/decompiled/inventory.tsv"
python3 -I Client/docs/research/scripts/tools/check_roadmap_coverage.py --inventory "$INV" \
    --mapping Client/docs/research/coverage/mainexe-roadmap.tsv --require-complete
MURET_MAINEXE_INVENTORY="$INV" python3 -I Client/docs/research/scripts/tools/test_check_roadmap_coverage.py
```

El generador tarda unos 15 segundos y solo usa la biblioteca estándar. El verificador comprueba que
el inventario sea el fijado (SHA-256 y 5.648 filas), que la matriz tenga cada VA una vez con sus 10
columnas originales sin cambios, que cada fila clasificada tenga ola, propósito y evidencia válidos y
que los tamaños sumen lo mismo que el inventario. Al 2026-11-13 da 0 errores con y sin
`--require-complete`, y sus 10 controles negativos pasan.

Al mergear trabajo nuevo en `main`, regenerar la matriz mueve los estados (`unknown` → `implemented`)
y, con citas nuevas, alguna ola inferida. Las olas que vienen de una cita o de una lectura no cambian.
