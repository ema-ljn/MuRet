# 10. Cobertura del roadmap: cada función del `main.exe` en una ola

Informe de la tarea CL.M del [plan de cobertura](../../../docs/superpowers/plans/2026-11-10-cobertura-roadmap-cliente.md),
2026-11-13. Responde una pregunta: **¿a qué ola del roadmap pertenece cada una de las 5.648 funciones
del decompilado?** Todas tienen respuesta: 3.583 son código de juego con una ola responsable, 1.891
son bibliotecas que MuRet reemplaza, 136 caen en exclusiones que el roadmap ya tenía y 38 son thunks.
No queda ninguna pendiente.

Asignar no es implementar. La matriz dice de quién es cada función y, aparte, qué se sabe de ella y
qué tiene MuRet; el avance que sale de ahí está en [El avance real](#el-avance-real). La revisión
independiente que pide el plan se hizo el mismo día y sus correcciones ya están en la matriz
([La revisión independiente](#la-revisión-independiente)): CL.M está cerrada.

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
| MuRet | `main` @ `d21f440` (la CL8 y la primera matriz de CL.M mergeadas): las citas de `Client/src` con la ola de su commit por `git blame`, los cortes, los tests, los docs y el [TSV del barrido de la CL8](../../../docs/superpowers/plans/2026-11-12-ola-cl8-funciones.tsv) |

El generador no ejecuta el PE: todo es lectura estática. Con las mismas entradas da el mismo TSV byte
a byte. No lee lo que escribe CL.M (este informe y sus tres scripts): si no, en un commit que ya los
tiene cada función tomaría la ola que le dio la corrida anterior.

## El resultado

| Clasificación | Funciones | Bytes | Qué es |
| --- | ---: | ---: | --- |
| `game` | 3.583 | 2.327.716 | código del juego, cada una con su ola responsable |
| `library` | 1.891 | 127.741 | funclets de excepciones de C++ (1.163, 13.225 B), CRT estático (470, 70.584 B), libjpeg (198, 39.664 B), STL de MSVC6 (28, 3.914 B) y los inicializadores de globales que genera el compilador (32, 354 B) |
| `excluded` | 136 | 38.684 | [Fuera de alcance](../../../docs/superpowers/specs/2026-10-30-cliente-design.md#fuera-de-alcance): GameGuard (72, 29.306 B), ResourceGuard (42, 5.212 B), el launcher (9, 3.233 B), el hash anti-tamper (12, 792 B) y el detector de trampas (1, 141 B) |
| `thunk` | 38 | 226 | 35 heredan la ola de quien los llama; 3 no tienen ola porque solo los llaman bibliotecas o exclusiones (dos van al sistema y uno a GameGuard) |
| `pending` | 0 | 0 | — |
| **Total** | **5.648** | **2.494.367** | lo mismo que el inventario |

Todas las funciones de juego se llaman `FUN_*`: los nombres que trae el inventario son de funclets,
thunks y CRT. 318 de las 3.583 (81.257 B) son **código muerto**: nadie las llama, las apunta ni
las inicializa, o solo las alcanza otro código muerto. Igual tienen ola, la de su vecindad, para que
quien la porte confirme que siguen muertas.

### Por ola

La ola responsable cuenta los bytes una sola vez. *Vivo* es sin el código muerto; las columnas de
estado se explican en [Los estados](#los-estados).

| Ola | Funciones | KB | % del juego | KB vivos | implementado | parcial | sin cita | sin empezar |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| CL0 | 0 | 0 | 0 | 0 | — | — | — | — |
| CL1 | 13 | 4,7 | 0,2 | 4,2 | 4,1 | 0 | 0 | — |
| CL2 | 52 | 19,8 | 0,9 | 18,9 | 9,6 | 0 | 9,3 | — |
| CL3 | 136 | 84,6 | 3,7 | 82,7 | 12,6 | 60,2 | 9,9 | — |
| CL4 | 29 | 20,0 | 0,9 | 18,8 | 5,4 | 12,1 | 1,3 | — |
| CL5 | 171 | 40,6 | 1,8 | 34,3 | 17,6 | 1,4 | 15,2 | — |
| CL6 | 176 | 75,6 | 3,3 | 72,1 | 39,1 | 19,5 | 13,6 | — |
| CL7 | 781 | 610,5 | 26,9 | 576,3 | 103,4 | 366,4 | 106,4 | — |
| CL8 | 1.097 | 783,2 | 34,5 | 756,0 | 122,9 | 515,1 | 118,0 | — |
| CL9 | 1.053 | 553,2 | 24,3 | 550,7 | — | — | — | 504,7 |
| CL10 | 32 | 11,4 | 0,5 | 10,3 | — | — | — | 10,3 |
| CL11 | 43 | 69,6 | 3,1 | 69,6 | — | — | — | 69,6 |
| **Total** | **3.583** | **2.273,2** | **100** | **2.193,8** | **314,7** | **1.020,7** | **273,9** | **584,6** |

CL0 no tiene funciones: son las fundaciones de MuRet, sin contraparte en el binario. La CL7 y la CL8
juntan el 61 %: el mundo, sus objetos y efectos, y la interfaz, que en el original son funciones
enormes. La CL9 es el 24,3 % y la CL11 el 3,1 %. En la CL9, lo que sigue sin empezar (504,7 KB) es
menos que sus KB vivos porque dos gigantes que pasaron a la CL9 ya tienen una parte en MuRet
(`0x57cfc0` y `0x5650c0`, `partial`).

**Consumidoras.** Una función compartida tiene una sola ola responsable y la lista de las otras que
la usan (`consumer_waves`): las olas de quienes la alcanzan (llamadas, punteros y vtables, sin el
código muerto) y, en un gigante, las olas abiertas (CL9 a CL11) de lo que llama, que son las ramas que
le faltan. 1.614 funciones de juego tienen alguna. Esa vista cuenta los bytes de nuevo en cada
consumidora, así que no suma el total: la CL9 aparece como consumidora en 996 KB de funciones de otras
olas (sobre todo de la CL8: las ventanas que muestran estado del juego), la CL10 en 488, la CL8 en
478, la CL7 en 462, la CL11 en 334, la CL6 en 323, la CL3 en 248, la CL4 en 239, la CL5 en 175, la
CL2 en 108 y la CL1 en 12.

## Los 821 KB y el 36 % del traspaso

El [traspaso de la CL7](../../../docs/superpowers/plans/2026-11-10-cl7-traspaso.md) estimó "unos 821
KB de código de juego, el 36 %, sin ola asignada". La consulta que dio ese número no quedó escrita.
La reconstrucción con la matriz: el código de juego cuya ola **no** sale de algo que ya existía el
2026-11-10 (una cita de `Client/src`, un corte, un doc o plan de MuRet que lo nombre) sino de las
reglas de CL.M o del barrido de la CL8 (escrito el 2026-11-12, después del traspaso). Da **801 KB,
el 35,2 %** del código de juego (2.387 funciones): el número del traspaso se reproduce con un 2,5 %
de diferencia. Sin el barrido de la CL8 serían 619 KB (27,2 %). Con la matriz, el código de juego
sin ola es 0. Las 23 filas que reasignó la [revisión independiente](#la-revisión-independiente)
cuentan como previas porque casi todos sus bytes (63 de 69 KB) ya tenían una cita de MuRet; la
primera matriz, sin la revisión, daba 807 KB (35,5 %) y 629 KB.

```bash
# la columna evidence dice de dónde salió la ola: base=src, corte, doc, papel, revisión, tsv-cl8 o una regla de CL.M
python3 -I - <<'EOF'
import csv
rows = list(csv.reader(open('Client/docs/research/coverage/mainexe-roadmap.tsv', encoding='utf-8'),
                       delimiter='\t', quoting=csv.QUOTE_NONE))
game = [dict(zip(rows[0], r)) for r in rows[1:] if r[10] == 'game']
prior = ('base=src;', 'base=corte;', 'base=doc;', 'base=papel;', 'base=revisión;')
sel = [r for r in game if not any(p in r['evidence'] for p in prior)]
total = sum(int(r['size_bytes']) for r in game)
size = sum(int(r['size_bytes']) for r in sel)
print(len(sel), round(size / 1024), 'KB', round(100 * size / total, 1), '%')
EOF
```

## Cómo se asignó

El generador aplica las reglas en este orden y la primera que alcanza decide; los pasos 9 y 10
revisan el resultado al final. La columna `evidence` de cada fila dice cuál fue (`base=…`), con el
`.asm` de la función y la cita que la sostiene.

1. **Lo que MuRet ya asignó** (`papel`, `revisión`, `src`, `corte`, `tsv-cl8`, `doc`): cinco
   funciones que mezclan olas toman la de su papel (`papel`): `WinMain` (`0x4b9b80`), `WndProc`
   (`0x4af4e0`), el paso por frame de las escenas (`0x636290`) y el cierre por `WM_QUIT`
   (`0x4aeda0`) la CL3, y el dispatcher de recepción (`0x4e2b10`) la CL4. Las 23 que reasignó la
   [revisión independiente](#la-revisión-independiente) toman la ola que les dio (`revisión`, 69 KB).
   Las demás funciones que cita `Client/src` toman la ola dominante de esas líneas por `git blame`
   (1.045 funciones, 1.214 KB); las que solo aparecen en un corte hacia la CL9 o más, la de ese
   corte; las del barrido de la CL8, la que dice el TSV; las que nombra un doc o plan de MuRet, la
   que ese doc les da. El informe y los scripts de CL.M no cuentan como cita: medido sobre un commit
   que ya los tiene, le devolverían a cada función la ola de la corrida anterior.
2. **Las exclusiones del roadmap** y lo que solo ellas alcanzan ([abajo](#bibliotecas-thunks-y-exclusiones)).
3. **El grafo** (`grafo`): una función sin ola toma la de quien la llama, la apunta o instala la
   vtable que la contiene; un funclet de excepciones cuenta como la función que lo usa. Si la llaman
   varias olas, va a la más baja y las otras quedan como consumidoras. Se repite hasta que no cambia.
4. **El dispatcher** (`opcode`): cada función que llama `0x4e2b10` toma la ola del opcode de su
   caso, según la tabla de `paso5-protocolo-handlers.md` del research: conexión CL4, mundo CL7, chat,
   vida e inventario CL8, el resto de los paquetes del juego CL9 por sub-ola (viewport 9.1, combate
   9.2, ítems 9.3, social 9.4, eventos 9.5). Los tres opcodes de misiones (`A0` a `A2`) van a la CL9
   sin sub-ola: la CL9 todavía no la tiene.
5. **Las ramas de los gigantes** (`rama`, `gigante`): dentro de los gigantes de papel menos el
   dispatcher (`WinMain`, `WndProc`, `0x636290` y `0x4aeda0`), una llamada toma la ola de la cita de
   MuRet más cercana (a menos de `0x200` bytes); si no hay ninguna, la del papel del gigante.
6. **Los efectos** (`efectos`): lo que solo alcanzan las 13 raíces de efectos del roadmap (`0x518cf0`,
   `0x5208a0`, `0x52d620`, `0x530860`, `0x535840`, `0x53c6a0`, `0x52e590`, `0x5491e0`, `0x549510`,
   `0x549440`, `0x53fe70`, `0x5446e0`, `0x548550`) y MuRet no porta va a la CL11.
7. **Lo que usa y lo que comparte** (`usa`, `vtable`, `datos`, `dominio`): una función que nadie
   alcanza toma la ola de lo que llama, de la vtable que instala o de los globales que comparte con
   pocas funciones; las que usan OpenGL van a la CL5, el IME a la CL8, `wzAudio` y los archivos de
   sonido a la CL10, el reloj a la CL3.
8. **Lo leído a mano** (`lectura`): las que ninguna regla alcanzó ([abajo](#lo-leído-a-mano)).
9. **Las ramas de los tipos** (`tipo`): lo que la CL7 tiene solo porque lo alcanzan los gigantes de
   los personajes (el paso `0x4fd5f0`, el dibujo `0x50ba30` y `0x505de0`, el alta de un monstruo
   `0x5141e0`) o la habilidad (`0x4f97b0`), sin una cita de MuRet: las ramas de cada tipo de
   monstruo y de la habilidad. Si crean efectos o tocan sonidos (llaman a `0x518cf0`, `0x530860`,
   `0x53fe70`, `0x5491e0` o `0x41ed10`), o solo las usan las que lo hacen, van a la CL11; el resto,
   el alta y la lógica de cada tipo, a la CL9. Son 45 funciones (69,5 KB). Es la regla del
   [roadmap](../../../docs/superpowers/specs/2026-10-30-cliente-design.md#la-ola-de-efectos-visuales-cl11)
   ("efectos visuales de combate, skills y eventos, conectados a sus disparadores de CL9") y lo que
   dice 06: los efectos de la habilidad están "sin leer: solo los llama la habilidad"
   ([06](06-render-escenas-y-ui.md):6389) y los módulos de los mapas de evento son de la CL9
   (06:9581). La CL7 queda como consumidora de todas.
10. **Las consumidoras**: al final, cada fila suma como consumidoras las olas de quienes la alcanzan
    (llamadas, punteros y vtables, sin el código muerto) y, si es un gigante, las olas abiertas
    (CL9 a CL11) de lo que llama.

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
y el anticheat del propio `main.exe` (el detector de trampas es anticheat, lo separó la revisión
independiente). Ninguna se porta; el launcher queda para más adelante, por si MuRet llega a tener
uno propio o una reinterpretación (Emanuel, 2026-11-13). Cada fila excluida empieza su `purpose` con
el nombre de su grupo y dice el motivo en `next_action`, así que se las puede filtrar por grupo:

| Grupo | Funciones | Bytes | Qué es y por qué no se porta |
| --- | ---: | ---: | --- |
| GameGuard | 72 | 29.306 | el anticheat nProtect GameGuard, enlazado en el exe ([abajo](#gameguard)); MuRet no corre GameGuard |
| ResourceGuard | 42 | 5.212 | `0x681ec0`: al entrar al mundo arma un objeto (constructor `0x681480`, destructor `0x6814d0`), lee la ruta del exe y chequea los recursos, pero el `jmp` de `0x4c4ad5` saltea el aviso y el resultado no se usa (Q-C147 de [07](07-quirks-y-bugs.md)) |
| hash anti-tamper | 12 | 792 | la tabla hash que arma el inicializador `0x4acef0` con tamaños al azar (`0x447f80`..`0x448310`, [02](02-arranque-y-ciclo-de-vida.md)) y `0x448170`, que fija su multiplicador con un `rand()` desde `WinMain` (`0x4ba65f`, antes del `srand` de `0x4bad9a`), leídas a mano. El `rand()` que gasta la protección se cuenta aparte: Q-C125 de 07 y P-C22 de [08](08-pendientes.md) |
| launcher | 9 | 3.233 | el fin del auto-update (`0x4b96b0` y su copia `0x4b9550`: renombra `mu._xe`, `mumsg._ll`, `wz_zp._ll` y `message._tf`) y `wzPopLaunchInfo` (`0x6922c0`), que lee del registro el servidor que dejó `Mu.exe` ([02](02-arranque-y-ciclo-de-vida.md#del-launcher-al-juego)) |
| detector de trampas | 1 | 141 | `0x4ad070`: la llama `0x4ad100` cuando encuentra por título la ventana de un programa de trampas (Speed Hack, GameHack, ArtMoney); muestra un `MessageBoxA` y cierra el cliente y esa ventana. [04](04-protocolo.md) ya deja la búsqueda fuera |

Una función queda excluida si solo la alcanzan funciones excluidas o código muerto (los métodos de
una clase que se llaman entre sí, también). Una llamada desde código que Ghidra no puso en ninguna
función cuenta como de la función excluida que tiene al lado, o si no la hay, de la que sigue al
hueco.

Dos excepciones, de juego aunque toquen la protección:

- **`0x4ad100`** busca las ventanas de trampas y, en la misma función, manda el primer keepalive
  (`0x4bee60`, CL4.5). Queda en la CL4 por el keepalive; la búsqueda no se porta.
- **Los accesores del registro protegido** (`0x40ad90` a `0x40b3f0`) llaman al método 3 de la vtable
  del hash (`0x448310`, excluido) en cada lectura protegida, pero son de juego: cada uno gasta un
  `rand()` que MuRet reproduce (`Client/src/Render/Models/LogicalAccess.h`). Cada uno tiene la ola
  de su cita o del grafo (el par `0x40b3b0`/`0x40b3f0`, el serial de los C3, la CL2). MuRet no hace
  el alta ni la búsqueda de la entrada (`0x40ad90`, `0x40b240`): sus citas son menciones, no
  implementación.

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
nada del juego que se ve. Toca el protocolo en un solo lugar: el caso `0x3f8` del callback (la
autenticación de GameMon) arma un `C1 08 73` con un `rand()` (`0x4508df`) y el serial de los C3
(`0x450997`), lo cifra (`0x450a15`) y lo manda (`0x450a46`). Sin GameMon ese caso no llega nunca, y
MuRet no corre GameMon, así que su cliente no gasta ese `rand()` ni ese serial. El caso `0x73` del
dispatcher de recepción es otra cosa y sigue en la CL9.2. **Los textos de `Text.bmd`.** Los avisos que muestra
son los registros 790 a 800 de `Text.bmd` (`[error1001]` a `[error1011]`: "No se puede conectar a
nProtect", "Se ha detectado un hackeo de velocidad", "GameGuard ya se está ejecutando"…), y del 801
al 808 hay registros reservados (`Reserve : GameGuard`). Solo los usan estas dos funciones: el
callback `0x4505d0` lee el 792, el 793, el 794 y el 799, y `0x450380`, el 792, el 795 al 798 y el 800
(ninguna otra función del decompilado apunta a los registros 788 a 808). MuRet carga `Text.bmd`
entero (CL1), así que los textos quedan en la tabla aunque nadie los muestre.

Las 72 funciones: `0x450380`, `0x4505d0`, `0x686550`, `0x6867f0`, `0x6869a0`, `0x6869f0`, `0x688260`, `0x688270`, `0x688400`, `0x688590`, `0x688640`, `0x6886e0`, `0x688730`, `0x688980`, `0x688a80`, `0x688b60`, `0x688dd0`, `0x688eb0`, `0x688fa0`, `0x6890b0`, `0x6891d0`, `0x689520`, `0x689b20`, `0x689c00`, `0x689ce0`, `0x689ef0`, `0x68a140`, `0x68a170`, `0x68a250`, `0x68a3b0`, `0x68a4d0`, `0x68a6e0`, `0x68a780`, `0x68a940`, `0x68ac00`, `0x68b690`, `0x68b8b0`, `0x68b960`, `0x68b980`, `0x68b9a0`, `0x68b9d0`, `0x68b9f0`, `0x68bbb0`, `0x68bc00`, `0x68be30`, `0x68c200`, `0x68c210`, `0x68c230`, `0x68c240`, `0x68c3a0`, `0x68c420`, `0x68c4a0`, `0x68c5e0`, `0x68c650`, `0x68c6a0`, `0x68c720`, `0x68cb50`, `0x68cba0`, `0x68cc10`, `0x68d340`, `0x68d640`, `0x68d730`, `0x68dce0`, `0x68dd70`, `0x68de10`, `0x68de80`, `0x68e320`, `0x68e390`, `0x68e510`, `0x6915c0`, `0x691600`, `0x6916a0`.

## Lo leído a mano

Tras las reglas quedaban 107 funciones: 87 de juego (11,4 KB, base `lectura`) y 20 que quedaron
excluidas. Se leyeron en su `.c` y su `.asm` y la decisión está en el diccionario `MANUAL` del
generador, con el motivo. Las filas que llevan esa evidencia son 108 (13,0 KB): una excluida más la
hereda por el cierre de las exclusiones. De las 108, 66 son código de juego muerto y 21 están
excluidas:

| Qué | Ola |
| --- | --- |
| `MessageBox` centrado con un hook `WH_CBT` (`0x401bb0`..`0x401d30`), un log a archivo (`0x41e1c0`, `0x41e220`), el buffer de DirectInput (`0x41e420`), cerrar un proceso por nombre (`0x4b95e0`), el parseo de la línea de comandos (`0x4acd50`, `0x4ace00`), el destructor del manejador de caídas (`0x680950`..`0x6808d0`) | CL3 |
| un cifrado por bloques de 16 bytes sin uso (`0x68f4b0`, `0x68f540`) y el objeto de la vtable `0x6bd884` (`0x685a40`..`0x685a90`) | CL2 |
| la llamada vacía del `WM_TIMER` 1000 antes del keepalive (`0x450376`) | CL4 |
| los parámetros de un programa ARB por `wglGetProcAddress`, sin uso (`0x66b1c0`..`0x66b350`) | CL5 |
| matemática de vectores (`0x5fc6c0`..`0x5fcc90`) | CL6 |
| la clase de la vtable `0x6b8d38`, los recorridos de los conjuntos del registro del personaje (`0x40cec0`..`0x40db80`), pasos y celdas del terreno (`0x4e9920`, `0x5ae300`, `0x5f9670`, `0x5fd2c0`) | CL7 |
| setters y listas de ventanas, el destructor de la vtable `0x6ba970`, el filtro de palabras (`0x5769e0`), abrir una URL (`0x6200e0`) | CL8 |
| búsquedas en el viewport (`0x5132d0`, `0x517920`, `0x5178f0`), atributos de ítem por nivel (`0x54e610`, `0x54ebf0`) y el contador de 1 a 50 que llama a `0x57cfc0` (`0x57cf90`) | CL9 |
| escribir un WAV con `mmio` (`0x41f380`, `0x41f3e0`) | CL10 |
| el hash anti-tamper (11) y los métodos sin uso de la librería de GameGuard (9) | excluidas |

## Los gigantes y la frontera CL7/CL11

Una función grande con ramas de varias olas cuenta sus bytes una vez, en su ola responsable, y
lista las demás como consumidoras. Las de más de 20 KB son 17, todas `partial`:

- **10 de la CL8** (`0x5ca250`, `0x56c440`, `0x623e70`, `0x5d8d50`, `0x576eb0`, `0x5c4110`,
  `0x59ff10`, `0x5b2aa0`, `0x5bec50`, `0x62db60`), con la CL9 entre sus consumidoras porque cortan
  hacia ella, y seis con la CL10 porque tocan sonidos.
- **2 de la CL9**, que la revisión sacó de la CL8: `0x57cfc0`, las acciones del héroe al final de
  `MoveHero` (arma los paquetes de habilidad `0x19`, `0x1E` y `0x26`, CL9.2), y `0x5650c0`, la
  interacción al llegar (levantar un ítem `0x22`, hablar con un NPC `0x30`, la tienda personal; el
  barrido de la CL8 ya la daba a la CL9.3 y la CL9.4). La CL8 queda como consumidora.
- **4 de la CL7**: el paso de los personajes (`0x4fd5f0`), su dibujo (`0x50ba30`) y dos raíces de
  efectos (`0x518cf0`, `0x535840`), con la CL9, la CL10 y la CL11 como consumidoras.
- **`WndProc`** (`0x4af4e0`), de la CL3, con siete olas consumidoras.

Su parte pendiente cae en [El avance real](#el-avance-real).

Las 13 raíces de efectos son de la **CL7** (las obligaciones de la CL7.6 que ya porta) y llevan a la
**CL11** como consumidora: once están en `partial` y dos (`0x549510`, `0x549440`) completas. La CL11
es responsable de **43 funciones (69,6 KB)** que MuRet no porta:

- 32 ramas de los tipos y de la habilidad (59,2 KB, paso 9 de [Cómo se asignó](#cómo-se-asignó)):
  los efectos de cada tipo de monstruo (`0x4271d0` de `Monster100`, `0x441400` de `Monster81`,
  `0x434180` de `Monster94`…) y los de la habilidad (`0x4f5b70`, 13,5 KB, con 69 altas de efectos);
- la habilidad del personaje, `0x4f97b0` (6,3 KB), que la revisión movió desde la CL7: 23 altas de
  efectos, 7 sonidos y ningún paquete;
- 9 que solo alcanzan las raíces (4,0 KB) y un thunk de 39 bytes.

El alta y la lógica de cada tipo que esas ramas no cubren (13 funciones, 10,3 KB) van a la CL9, y el
ataque a varios blancos (`0x517a20`), que estaba en la CL11 por su llamador, también: arma y manda el
`C1 .. 10`. Lo que falta de los efectos dentro de las raíces queda en la parte pendiente de la CL7; la
CL11.R lo reparte caso por caso, como pide el [roadmap](../../../docs/superpowers/specs/2026-10-30-cliente-design.md#la-ola-de-efectos-visuales-cl11).

## Los estados

Son independientes de la ola. `analysis_state` describe lo que se sabe de la función;
`implementation_state`, lo que tiene MuRet; `validation`, el test que la cita.

| Estado | Regla | Funciones de juego |
| --- | --- | ---: |
| `verified` | `registro-niveles.tsv` del research la valida con una observación (traza, VM, cable) | 31 (16 KB) |
| `static` | la cita MuRet o el research, o se leyó a mano o en la revisión | 1.685 (1.670 KB) |
| `inferred` | su ola sale del grafo o de una regla, sin lectura propia | 1.867 (587 KB) |
| `implemented` | la cita `Client/src` sin cortes y no es un gigante | 735 (315 KB) |
| `partial` | la cita `Client/src` pero tiene ramas cortadas, es un gigante o porta solo una parte | 318 (1.021 KB) |
| `unknown` | es de una ola cerrada (CL1 a CL8) y `Client/src` no la cita | 1.124 (274 KB) |
| `not-started` | es de la CL9, la CL10 o la CL11 y no está citada | 1.088 (585 KB) |
| `not-applicable` | código muerto, biblioteca, thunk o exclusión | 318 de juego |

Una cita es un **corte** si su línea dice que esa parte la hace otra ola o que MuRet no la hace
("corta", "es de la CL9", "todavía no", "MuRet no", "no la porta", "gancho"…). Las citas que solo
nombran la función sin portarla (un llamador, un ejemplo, un sonido que MuRet no toca hasta la CL10)
están en `MENTION_ONLY` del generador y no cuentan; las que portan solo una parte, en
`PARTIAL_ONLY`. Las dos listas salen de la [revisión independiente](#la-revisión-independiente).

`unknown` no quiere decir que falte: el código de MuRet porta muchas funciones chicas sin nombrar su
dirección (un helper que se volvió una línea, una tabla que se leyó de otra manera). La acción de esas
filas es comprobarlo en su ola. 712 filas de juego (1.158 KB) citan un test de MuRet en `validation`.

## El avance real

Sobre los 2.194 KB de código de juego vivo:

| Parte | KB |
| --- | ---: |
| `implemented` | 315 |
| lo hecho de las `partial` (la fracción de sus citas que no son cortes) | 871 |
| lo que falta de las `partial`: CL9 102, CL7 29, CL8 16, el resto 2 | 150 |
| `not-started`: CL9 505, CL11 70, CL10 10 | 585 |
| `unknown`: CL8 118, CL7 106, CL5 15, CL6 14, CL3 10, CL2 9, CL4 1 | 274 |

**El avance está entre el 54 % y el 67 %**, según cuánto de lo `unknown` falte de verdad: 54,0 % si
falta todo, 60,3 % si falta la mitad y 66,5 % si MuRet ya lo porta sin citarlo. Lo que seguro falta
es la **CL9, unos 607 KB (el 27,7 % del código vivo)**, la CL11 (70 KB más los casos de efectos
dentro de las raíces de la CL7), la CL10 (11 KB) y las ramas cortadas de la CL7 y la CL8 (45 KB).
La primera matriz daba entre el 55 % y el 72 %: la revisión bajó `implemented` de 347 a 315 KB
(menciones y cortes que no se veían) y pasó a las olas abiertas las ramas de los tipos, la
habilidad, el audio y los dos gigantes de la CL9.

Es una cota por bytes del binario, no un plan de trabajo: no mide QA, la VM ni lo que cuesta cada
byte. La fracción de una `partial` sale de contar citas (cortes contra líneas que la portan), no de
leer qué ramas faltan. El generador la escribe en el resumen (`--summary`, clave `progress`).

## Límites

- **El 26 % del código de juego tiene ola `inferred`** (587 KB): la sacó el grafo, sin una lectura
  de la función. Un llamador de otra ola o una tabla que el grafo no ve la puede mover.
- **El grafo no ve todo**: llamadas por punteros armados en ejecución, tablas de funciones fuera de
  `.rdata` y código que Ghidra no puso en ninguna función. Por eso el código muerto tiene ola y su
  acción es confirmarlo.
- **`implemented` es una cota alta.** Sale de las citas con la dirección `0x…`; una cita que nombra
  la función en una frase sin palabra de corte sigue contando como port. La revisión encontró 10
  casos y los corrigió; puede haber más, chicos.
- **Las consumidoras no son una lista de trabajo**: dicen qué olas tocan la función, no qué le falta.

## La revisión independiente

La pedía el [plan](../../../docs/superpowers/plans/2026-11-10-cobertura-roadmap-cliente.md) para
cerrar CL.M. La hizo el 2026-11-13 un revisor nuevo, sin ver las conclusiones de este informe más
allá del texto publicado: leyó la matriz de `d21f440`, el generador, el `.c` y el `.asm` de cada
función que tocó, el grafo y el PE original con un lector propio. Cubrió las exclusiones (las 129
filas), los 17 gigantes, la frontera CL7/CL11, una muestra al azar ponderada por KB (semilla
20261113: 40 filas de juego y 15 de biblioteca, más los 38 thunks), todas las `implemented` de más
de 1.500 bytes y las que tenían una negación cerca, y cada número del informe.

**Lo que confirmó.** Los números se reproducen exactos desde la matriz. Las bibliotecas y los
thunks están bien (las 15 de biblioteca de la muestra, los 38 thunks). GameGuard, ResourceGuard y el
launcher son lo que dicen y no se pierde nada visible con ellos: los textos 788 a 808 de `Text.bmd`
solo los leen las dos funciones de GameGuard, ninguna fila de juego toca el objeto de GameGuard, el
resultado de ResourceGuard se saltea y el `rand()` del hash se gasta antes del `srand`. De la
muestra de juego, 32 de 40 estaban bien. Los ports que abrió (`0x67f840`, `0x4665d0`, `0x549c60`,
`0x412890`, `0x491840`) son reales y tienen test. La estructura CL0..CL11 no cambia.

**Lo que encontró: 27 hallazgos**, 17 de ola o dueño, 7 de consumidoras o evidencia y 3 de redacción
del informe. Cada uno se verificó contra el `.asm` antes de tocar nada, y la corrección está en el
generador (`REVIEW`, `MENTION_ONLY`, `PARTIAL_ONLY`, `EXCLUDED_ROOTS`, los pasos 9 y 10), no a mano
en la matriz:

| Área | Hallazgo | Qué se hizo |
| --- | --- | --- |
| Exclusiones | `0x4ad070` (cierra el cliente si ve un programa de trampas) estaba en la CL7 | excluida, grupo detector de trampas |
| | `0x4ad100` estaba en la CL7 por `git blame`; su parte de juego es el primer keepalive | CL4; y `0x4bee60`, que arma el keepalive, de la CL7 a la CL4 |
| | el constructor y el destructor de ResourceGuard y tres hojas suyas quedaron en la CL7 | excluidas (37 → 42 filas) |
| | el callback de GameGuard manda un `C1 08 73` con un `rand()` y el serial de los C3 | se escribió en [GameGuard](#gameguard): sin GameMon no pasa |
| | el grupo del hash decía "solo lo alcanzan excluidas", y la vtable del hash la llaman los accesores protegidos de juego | `0x448170` (el multiplicador con `rand()`) al grupo; los accesores siguen de juego porque MuRet reproduce su `rand()`; se escribió la excepción |
| | `0x40b3f0` estaba en la CL9 por una línea de un doc | CL2, con su par `0x40b3b0` |
| Gigantes | `0x57cfc0` y `0x5650c0` eran de la CL8 por una cita de paso | CL9, la CL8 consumidora |
| | a 12 gigantes y a `WinMain` les faltaba la CL10 (tocan sonidos); a `0x4fd5f0` la CL11; a `0x535840` la CL9 | paso 10: los gigantes suman las olas abiertas de lo que llaman |
| Frontera CL7/CL11 | `0x517a20` estaba en la CL11 y arma el ataque a varios blancos | CL9 |
| | `0x520170` y `0x517850` llaman a ese ataque | siguen en la CL11; la CL11 queda como consumidora de `0x517a20` |
| | la habilidad (`0x4f97b0`, `0x4f5b70` y 7 ayudantes, 23 KB) estaba en la CL7 | CL11 (21 KB); un ayudante de lógica (`0x4f2230`) a la CL9, y `0x53e2b0` sigue en la CL7 porque también la llaman `0x561f60` y `0x57cfc0` |
| | los efectos de cada tipo de monstruo (9 funciones, 34 KB) estaban en la CL7 | paso 9 (45 ramas en total): de esas 9, 5 a la CL11 y 3 (el alta y la lógica) a la CL9; `0x440530` sigue en la CL7 por su línea de 06 |
| | `0x612270` estaba en la CL10 por una línea de un doc; carga modelos de NPC | CL7, como su par `0x612d90` |
| Muestra e `implemented` | 11 `implemented` eran menciones o cortes en otra línea | 10 corregidas (`MENTION_ONLY`, `PARTIAL_ONLY` y más palabras de corte) |
| | el audio estaba repartido en la CL5, la CL7 y la CL8 | 7 funciones a la CL10 |
| | `0x498820`, la ventana de las cartas, estaba en la CL2 `implemented` | CL9, sin empezar |
| | 4 subcasos de `F3` tenían ola por la regla débil `usa` | CL9 |
| | el cierre por `WM_QUIT` (`0x4aeda0`) estaba en la CL8 | CL3, por su papel |
| | `0x459a70` y `0x4b8ff0` (el temporizador y la ventana) tenían ola por cercanía | CL3 |
| | 7 funciones de la STL clasificadas como juego | las 4 abiertas, a biblioteca |
| | `0x549ec0` (carga `FilterName.bmd`) estaba en la CL7 | CL6, con su par `0x549c60` |
| | las consumidoras se escribían solo al asignar la ola: 848 filas (631 KB) tenían un llamador de una ola que no figuraba | paso 10: una pasada final las suma |
| Números | "107 funciones (12,8 KB)", "77 muertas" y "3 thunks sin llamadas directas" | corregidos ([Lo leído a mano](#lo-leído-a-mano), thunks) |

**Donde se decidió distinto que el revisor:**

- **`0x446120`** (los pájaros) no es un falso `implemented`: MuRet la porta en
  `Client/src/World/Birds.cpp` y `Client/src/Scenes/FaunaStep.cpp` con citas sin `0x`. Lo viejo es
  el comentario de `GameScene.cpp` que dice que MuRet no corre ese paso.
- **Los monstruos**: el revisor proponía pasar las 9 funciones a la CL11. Se generalizó a una regla
  (paso 9) que separa lo que crea efectos o toca sonidos (CL11) del alta y la lógica de cada tipo
  (CL9), en lugar de mover nueve filas a mano.
- **`F3 40`** (`0x4da070`, un aviso del servidor): el revisor la ponía en la CL8 porque la dibujan
  los avisos de la CL8. Va a la CL9 con los otros subcasos de `F3`: es la respuesta a un paquete del
  juego, y el dibujo de los avisos ya es de la CL8.
- **La STL**: solo pasaron a biblioteca las 4 que el revisor abrió línea por línea. Las 3 probables
  (`0x487be0`, `0x65ea80`, `0x452860`) siguen como juego hasta leerlas.
- **`0x549ec0`**: el revisor la ponía con los cargadores de la CL1; su par `0x549c60` es de la CL6,
  y va con él.
- **Las consumidoras de las funciones comunes** solo suman a quienes las alcanzan. Las olas de lo
  que llaman se suman solo en los gigantes: sumarlas en todas pondría la CL9 en cada función que
  llama a un helper de la CL9.

Lo que movió, contra la matriz de `d21f440` con las mismas fuentes: 71,8 KB de la CL8 a la CL9,
67,2 KB de la CL7 a la CL11, 12,6 KB de la CL7 a la CL9, 3,9 KB de la CL2 a la CL9, 3,8 KB de la
CL11 a la CL9, 3,6 KB de la CL7 a la CL4 y otros menores. De `implemented` pasaron 18,2 KB a
`partial` y 14,1 KB a `not-started`. Con esto CL.M queda cerrada.

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
