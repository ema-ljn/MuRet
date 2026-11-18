# Cobertura del roadmap del cliente — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Asignar a una ola cada función de juego del `main.exe` decompilado y justificar el resto del inventario sin declarar implementado lo que solo está asignado.

**Architecture:** Una matriz por VA conserva los datos del inventario y agrega clasificación, ola responsable, consumidores y evidencia. Un verificador reconcilia todas las filas y un informe resume tamaños, huecos y estados; el ASM fija las asignaciones dudosas.

**Tech Stack:** Markdown y TSV UTF-8; Python 3 con biblioteca estándar para validar la matriz; exports C/ASM de Ghidra ya existentes.

**Spec:** [Roadmap del cliente, CL11 y CL.M](../specs/2026-10-30-cliente-design.md#la-ola-de-efectos-visuales-cl11), pedido del [traspaso, tarea 9](2026-11-10-cl7-traspaso.md#qué-falta-en-orden).

## Global Constraints

- Fines didácticos: comprender y reimplementar el comportamiento del cliente 1.02; no recuperar el fuente exacto.
- Build `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`, PE32 x86, base `0x400000`; las direcciones de la matriz son VAs estáticas.
- ASM antes que pseudocódigo; no ejecutar el PE ni hacer emulaciones nuevas para este mapeo.
- Se conservan CL0 a CL10, las obligaciones actuales de CL7.6 y las exclusiones de [AGENTS.md](../../../AGENTS.md) y del [roadmap](../specs/2026-10-30-cliente-design.md#fuera-de-alcance); CL11 agrega cobertura de efectos restantes.
- Una función compartida tiene una sola ola responsable y una lista de consumidoras; asignación, análisis, implementación y validación son estados distintos.
- Documentación y mensajes en castellano; no versionar binarios, volcados, PCAPs, nombres de cuentas ni capturas. QA se estima aparte.

## Review Focus

- Funciones `FUN_*` que sean CRT o bibliotecas: clasificarlas con evidencia, sin contarlas como gameplay pendiente ni excluirlas por dirección.
- Funciones con nombre, thunks y llamadas indirectas: ninguna desaparece del denominador; seguir destinos y vtables con evidencia cuando afecten al alcance.
- Una función con ramas de varias olas: contar sus bytes una vez y conservar las obligaciones y consumidores por caso, especialmente CL7.6 frente a CL11.
- Inventarios alterados, VAs duplicadas o tamaños distintos: fallar la reconciliación, aunque los totales agregados coincidan.
- Filas sin evidencia o todavía dudosas: mantenerlas pendientes y bloquear el cierre; una ola genérica o la etiqueta CL11 no resuelve una función desconocida.

---

## Fuente y punto de partida

Fuente inmutable: [inventory.tsv](https://github.com/EmaNLJN/mu-server-repack-research-102i/blob/91359b2ceffa4ce085ea19bc7bf11762fc2034a5/client-research/mainexe-vmxp-c32bbd5f/decompiled/inventory.tsv),
del repo `EmaNLJN/mu-server-repack-research-102i` @ `91359b2ceffa4ce085ea19bc7bf11762fc2034a5`,
carpeta `client-research/mainexe-vmxp-c32bbd5f/decompiled/`. Los exports están en `functions/`,
con el `file_base` del TSV más `.c` o `.asm`. El [README del export](https://github.com/EmaNLJN/mu-server-repack-research-102i/blob/91359b2ceffa4ce085ea19bc7bf11762fc2034a5/client-research/mainexe-vmxp-c32bbd5f/README.md)
explica el método y los 355 contratos externos, que están en otro TSV y no se suman a las 5.648 internas.

| Comprobación al 2026-11-10 | Resultado |
| --- | ---: |
| Funciones internas / VAs únicas / `status=ok` | 5.648 / 5.648 / 5.648 |
| Nombres `FUN_*` / suma de sus `size_bytes` | 4.403 / 2.477.342 bytes |
| Thunks internos | 38; ninguno con nombre que empiece por `FUN_` |
| Suma de `size_bytes` de todas las filas | 2.494.367 bytes |
| Tamaño del TSV fuente | 557.189 bytes |

Git blob fuente: `0fda5f454712bdae8ebcb55ae6aa313a65ea915a`.
SHA-256 del TSV fuente: `42cc47579259c0184e3e921e93ac7817176093a31bb7fb07f2d77734f073194c`.
La copia de trabajo disponible en `/workspace/.tools/muret-evidence/inventory.tsv` pierde el tab
del último campo vacío en la última fila; al restaurar ese tab coincide con el blob fuente.
Esto no altera los conteos. Al ejecutar, usar una copia fiel del TSV y registrar su procedencia,
sin depender de que esa ruta temporal exista en otra sesión.

**Estado (2026-11-13): CL.M terminada.** La matriz está completa: 5.648 filas, 0 pendientes, el
verificador y sus controles en verde. La revisión independiente del paso 4 se hizo el mismo día
(27 hallazgos, verificados contra el ASM) y sus correcciones están en el generador y la matriz. El
resultado, el método y la revisión están en
[10](../../../Client/docs/research/10-cobertura-roadmap.md#la-revisión-independiente).

**Estado al escribir el plan:** este plan deja pendiente la clasificación y asignación fila por fila. El inventario no
contiene etiquetas de juego/biblioteca ni olas; 4.403 nombres `FUN_*` no equivale a 4.403 funciones
de juego sin ola. Los 821 KB y el 36 % del traspaso se reproducirán o corregirán al publicar el
denominador de juego. La suma de tamaños se informa como métrica del inventario, no como bytes
únicos de `.text`, porcentaje implementado ni estimación de QA.

## Task 1: CL.M — reconciliar y asignar el inventario completo

**Files:**
- Crear: `Client/docs/research/10-cobertura-roadmap.md` — procedencia, criterios, conteos por ola y pendientes.
- Crear: `Client/docs/research/coverage/mainexe-roadmap.tsv` — una fila por cada una de las 5.648 VAs internas.
- Crear: `Client/docs/research/scripts/tools/check_roadmap_coverage.py` — validación de integridad y cierre.
- Modificar: `Client/docs/research/README.md` — índice del informe.
- Modificar: `docs/superpowers/specs/2026-10-30-cliente-design.md` — enlaces y estado real de CL.M; ajustar cobertura de olas solo con evidencia.

**Interfaces:**
- Consume: `inventory.tsv` de la fuente fijada arriba, su `file_base`, los exports C/ASM y el research 01..09; no requiere acceso al binario en ejecución.
- Produce: una matriz ordenada por VA, el informe reproducible y el comando `python3 -I Client/docs/research/scripts/tools/check_roadmap_coverage.py --inventory RUTA --mapping Client/docs/research/coverage/mainexe-roadmap.tsv [--require-complete]`.
- El comando devuelve 0 con datos válidos; devuelve un valor distinto de 0 con errores. Sin `--require-complete` permite publicar avances con filas pendientes explícitas; con él exige que no quede ninguna.

- [x] **Step 1: Fijar el inventario y comprobar la reconciliación inicial.**

  Obtener el TSV fuente sin modificarlo, verificar hash, columnas y conteos de la tabla anterior y
  registrar el commit del checkout de MuRet. Crear la matriz copiando todas las columnas originales
  y añadir estas columnas, en este orden:

  `classification`, `primary_wave`, `consumer_waves`, `purpose`, `evidence`, `analysis_state`,
  `implementation_state`, `validation`, `next_action`.

  Valores de `classification`: `game`, `library`, `thunk`, `excluded`, `pending`. Para `game`,
  `primary_wave` será exactamente un valor de `CL0` a `CL11`; `consumer_waves` usa esos mismos
  identificadores separados por `;`. Para `library` o `excluded`, la ola queda vacía y la evidencia
  explica el reemplazo estándar o la exclusión ya decidida. Para un `thunk`, documentar el destino
  y heredar su ola si corresponde al juego; si apunta a una biblioteca o exclusión, justificar la
  ola vacía. No marcar un thunk externo por el solo hecho de que su destino sea un import.

  Empezar con `classification=pending`, `analysis_state=pending`, `implementation_state=unknown`
  y `validation=none`; la matriz inicial aún no es un mapeo terminado. Preservar vacíos los
  campos originales que estén vacíos, sin inventar firmas ni convenciones.

- [x] **Step 2: Implementar el verificador con controles negativos.**

  Validar igualdad del conjunto de VAs y de cada columna original contra el TSV fuente, además de
  su hash. Rechazar VAs repetidas, faltantes o ajenas, tamaños o nombres alterados, olas inválidas,
  ola vacía en `game`, consumidores duplicados y clasificaciones finales sin propósito ni evidencia.
  `analysis_state` admite `pending`, `inferred`, `static` o `verified`; `implementation_state`
  admite `unknown`, `not-started`, `partial`, `implemented` o `not-applicable`. `validation` cita
  el test, traza u observación existente o vale `none`; no se deduce de los otros estados.
  `verified` describe el análisis del contrato, no el cierre de su implementación: exige citar
  en `evidence` la observación que supera la lectura estática. Puede coexistir con
  `implementation_state=not-started` y `validation=none` cuando todavía no hay validación del port.

  Probar con copias temporales de una matriz válida: duplicar una VA; sustituirla por una ajena
  conservando el número de filas; cambiar un tamaño; vaciar la ola de una fila `game`; añadir
  `CL12`; borrar evidencia; y dejar una fila `pending`. Cada caso debe fallar; el último puede
  pasar únicamente sin `--require-complete`. Verificar también un thunk de biblioteca sin ola y
  una función `game` con dos consumidores: deben pasar si están justificados.

- [x] **Step 3: Clasificar cada VA y asignar las funciones de juego.**

  Revisar las 5.648 filas, no solo las 4.403 `FUN_*`. Partir de los callers conocidos y las
  referencias del research; abrir el ASM de cada asignación dudosa, incluidas llamadas indirectas,
  vtables y tablas de dispatch. Una coincidencia textual, la cercanía a otra función o un rango de
  direcciones solo orienta la búsqueda. Cada fila final necesita una ruta o enlace con la función,
  el tramo ASM o la sección del research que sostiene su propósito y su clasificación.

  Asignar por responsabilidad: formatos a CL1; framing/cifrado a CL2; plataforma/bucle a CL3;
  conexión/dispatch a CL4; 2D/login a CL5; modelos/selección a CL6; mundo/movimiento y obligaciones
  vigentes de efectos a CL7; HUD/ventanas a CL8; reglas, paquetes y estado del juego a CL9; audio
  a CL10; efectos visuales restantes a CL11. CL0 queda para fundaciones. Resolver las funciones
  transversales con una única responsable y las otras como consumidoras; describir los casos
  repartidos y sus sub-olas en `evidence` o en el informe, sin sumar dos veces sus bytes.

  Para CL11, seguir al menos las raíces del roadmap y distinguir alta, paso, dibujo y limpieza:
  joints, cintas, efectos con modelo, sprites y partículas. Mantener CL7.6, las partículas del
  mundo, P-C53/P-C54/P-C55 y T9 en CL7; los números de daño (`0x548eb0`) en CL9.2; los ítems del piso
  (`0x608a00`) en CL9.3. El detalle por caso puede involucrar una función compartida con CL11.
  Clasificar CRT/bibliotecas y exclusiones con evidencia; no usar CL11 como destino de lo desconocido.

  Si falta evidencia, conservar `pending` y escribir en `next_action` la lectura concreta que lo
  resuelve. Si aparece comportamiento en alcance que ninguna ola describe, ampliar la ola del
  dominio con su evidencia antes de cerrar el mapeo; no inventar su comportamiento. No agregar ni
  quitar una exclusión existente como atajo para alcanzar el 100 %.

- [x] **Step 4: Publicar el informe y comprobar el cierre.**

  Publicar funciones y suma de `size_bytes` por responsable y clasificación; los consumidores se
  cuentan aparte. Mostrar el denominador de juego y las filas sin ola/pendientes, y reproducir o
  corregir la estimación de 821 KB / 36 % con la consulta que la calcula. El total de registros y
  tamaños debe reconciliar con la tabla de partida. Informar por separado asignación, análisis,
  implementación y validación; toda afirmación de implementación cita archivos y tests del commit
  de MuRet revisado. No estimar QA con los bytes.

  Ejecutar el comando de Interfaces primero sin y después con `--require-complete`. Ambos deben
  devolver 0 y el segundo informar 5.648 filas, 0 pendientes y 0 funciones de juego sin responsable.
  Comprobar enlaces y que las obligaciones de CL7, CL8, CL9 y las exclusiones no hayan cambiado.
  Una revisión independiente debe contrastar especialmente las exclusiones, las funciones grandes
  con varias ramas y el límite CL7/CL11. Actualizar CL.M como terminada solo con esa evidencia.

**Cierre del plan:** un PR de documentación y su verificador; no incluye portar las funciones
asignadas. La ola CL11 tendrá su propio plan de implementación después de CL11.R. Este documento
no da por realizadas las casillas pendientes ni sustituye el QA o las observaciones de la VM.
