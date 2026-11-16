# H3: auditoría funcional contra ASM y cliente original

> Ejecución: `superpowers:executing-plans`, con TDD y revisores de binario por función.

**Objetivo:** completar el comportamiento de H3 alcanzado por ataques normales, inventariar sus
dependencias reales y contrastar resultados con el cliente c32 en XP. No confundir cobertura de
citas con equivalencia funcional ni atribuir a H4 una ruta que su PR conserva sin implementar.
**Base:** main `015c1746`, después de #342 y #358. Rama `codex/h3-estela-arma`.
**Arquitectura:** reutilizar pools, cintas, modelos y pose existentes; portar productores y el estado
compartido que falta. Mantener orden, RNG, saturación y efectos persistentes del ASM.
**Stack:** C++20, GoogleTest/CTest, CMake/vcpkg, SDL3/OpenGL; sin dependencias nuevas.
**Contrato:** research 06, cuerpo/estela/modelo; informe 15 de H3; exports c32bbd5f del repack.

## Restricciones

- Un Ninja y como máximo dos compiladores/tests simultáneos.
- No modificar memoria, protección ni servidor original; raw evidence y credenciales fuera de Git.
- No quitar errores convirtiéndolos en no-op. Resolver por la semántica original o conservar el
  pendiente con sus condiciones exactas y el caso funcional que impide completar.
- Respetar los PR abiertos de H4/H8/H11. No mergearlos ni duplicar sus implementaciones.
- Registrar evidencia estática, regresiones y observación nativa por separado.

## Hallazgo inicial reproducido por Emanuel

Ataque a un mob con acción `0x23` y arma `0x222`: `StepTheWeaponTrail` aborta en `0x4fd236`.
`CreateRibbon`, el pool, su paso y su dibujo ya existen. Los tests anteriores esperan el corte;
la prueba de ataque por clase no configura el modelo visible de la mano, aunque configura equipo.
Los siete PR abiertos se inspeccionaron: los tres que cambian `CharacterStep.cpp` (#343/#344/#347)
conservan el mismo error. No hay una corrección publicada de esa ruta en esa tanda.

## Secuencia de trabajo

- [x] Identificar el fallo y comprobar PRs antes de duplicar trabajo.
- [x] Revisar por separado `0x4fcee0`, `0x4eb860` y `0x4ec200` contra ASM.
- [x] Añadir regresión real de cuerpo armado acción23/modelo222 y observar RED en `4fd236`.
- [x] Completar pose mundial `(0,1)`, manteniendo intactas pose normal/attached y dummy persistentes.
- [x] Conservar acción compartida del modelo (+a0) y publicaciones de frame/key (+a4/+a8) donde
  corresponden; probar diferencias respecto de la acción del actor al terminar una animación.
- [x] Portar el productor de cinta de `0x4fcee0`: mano, modos, tipo, color, una/diez muestras,
  velocidad publicada, saturación y continuación de helpers/RNG. Probar los límites de las ramas.
- [x] Auditar todas las dependencias H3, incluidas las omitidas por las 13 filas del TSV; registrar
  cortes genuinos y falsos de efectos, armas, daño y muerte con sus tests/oráculos.
- [x] Corregir discrepancias demostradas y repetir las revisiones del área afectada.
- [x] Revisión independiente final, build/tests completos con corpus/GL, formato e includes.
- [ ] Repetir combate normal, daño, muerte y recursos en MuRet y XP; comparar casos compatibles.
- [x] Preparar la rama y descripción del PR con research, resultado y pendientes estrictos; destino main, sin polling CI.

## Contratos confirmados para la primera integración

- `4fd236`: acción23/arma222, mano0, puntos `(0,-20,0)` y `(0,-120,0)`, tipo0. Diez poses entre
  `frame-speed` y la última suma float de `speed*0.1f`; cinta corta, key0, mismo dueño durable.
- `4fd487` lee el **byte** +a0 del modelo para su velocidad. `4fd69b..4fd6ef` lo publica antes
  de animar, y `505ee4` vuelve a publicarlo después del cuerpo. `PlaceModel(actor)` en la base
  sólo conservaba escala/origen; ahora publica también la acción.
- Pose `4eb860(0,1)`: escala los nueve coeficientes de la raíz con redondeo float, copia posición
  mundial sin escalarla y concatena los huesos. No duplicar escala/origen al transformar extremos.
- `4ec200` no escribe+a0. Con más de una key escribe+a4 y+a8 incluso al terminar; la pose también
  escribe esos metadatos. Congelado/sin acciones/una key conservan lo que no escriben.
- `4fd5c1..4fd5df`: continuar helpers después de las cintas, incluido el rand incondicional de
  `432bd0` aun con jugador. Tabla llena omite el alta y conserva el resto del flujo.

## Foco de revisión

Acción compartida diferente de la del actor; dummy y buffer global retenido; manos y armas especiales;
pool lleno con dueño preexistente; orden de partículas/RNG/retirada cuando otra rama falla. Cada
conclusión de cierre tendrá función/caso, evidencia, test que observa su efecto y límite dinámico.

## Checkpoint de evidencia original

El EXE recién copiado de XP tiene el hash c32 completo indicado por el research. Comparación de
los bytes de cinco exports ASM con ese PE: `4fcee0` (1.806), `4eb860` (1.046), `4ec200` (320),
`52e470` (134) y `52e3b0` (179): **3.485 bytes, cero diferencias**. Esto confirma la identidad de
la referencia de esas funciones; no acredita todas sus interpretaciones ni equivalencia dinámica.

La regresión del cuerpo con acción23/arma222 falló con el error original de Emanuel en `4fd236`
y pasó al emitir diez muestras y continuar RNG/efectos del cuerpo. La pose mundial pasó 19 tests
incluyendo raíz escalada/posición sin escalar, hijos, lock_positions y dummy retenidos.

Las primeras capturas por UI no acreditan un ataque: la secuencia de clic sostenido produjo
movimiento de `(187,129)` a `(180,132)`, con daño entrante/Miss visible. Se conserva ese resultado
como observación, sin contar estelas ni declarar MATCH. Se preparó un lector acotado de acción,
frame, modelo y pool de cintas; el resultado posterior está en research 15 y al final de este plan.

### Inventario adicional a las trece filas TSV

El análisis de H3 debe seguir los callers y dependencias, no sólo el porcentaje de filas citadas:

| Grupo | VAs y comportamientos que deben figurar en la revisión |
| --- | --- |
| Clic y llegada | `59cd49..59d8be`, `59b8a7`, `5650c0`, `558800`, `561c50`; orden de D3/D7 y stores |
| Selección | `558bb0`, `558eb0`, `4eac00`; prioridad de flags/PK/duelo y consumidores del blanco |
| Reply de ataque | `4cb750 → 4f4a10`; estado, modelo y todos los helpers llamados |
| Paso de ataque | `4f97b0`, `4f5b70`, `4fdb9a..50539e`, `517630`, `59fb80` |
| Daño | `4caae0`, `4f53a0`, `548e40`, `548f40`, `548eb0 → 621710`; pool, timer y dibujo |
| XP/muerte por red | `4cf200`, `4cfce0`, `4d0a30`, `4d07c0` |
| Muerte/recursos | `4f5660`, `4fb9e0`, `4fb1a0`, `511070`, `52e810` |
| Cuerpo/pose/estelas | `4fd5f0`, `4fcd00`, `4f9580`, `4ec200`, `4eb860`, `4ec110`, `4fcee0`, `6001b0`, `505de0` |
| Proyectiles/efectos | `53e2b0`, `53d9a0`, `5208a0`, `520170`, `517850`, `51fe10`, `51fff0`, `52e470` |

Fronteras que siguen abiertas y requieren su propio contrato/test, sin convertirlas en no-op:

- Armas normales de tiro 4/16..4/21: joints en `51a24a`, `51a405`, `51a173`, `51bf35`,
  `51c56c`, `51c713`; y pasos especiales de 4/6,4/13,4/14 en `526a8b`, `526d5f`, `52434e`.
- Efectos especiales del reply al pegar `4f4f5b`/`4f4fb9` y de muerte `4f58c2`/`4f595d`/`4f59ca`,
  cuando hay capacidad. Distinguirlos de una llamada que no crea nada por tabla llena.
- Helpers de Kalima/asedio/Doppelganger/Fortress/Crywolf y los datos no corroborados de
  Q-C1050/Q-C1176; no asignar cero a bytes indeterminados para cerrar la prueba.
- Integraciones de H4 en `4f9899`, `4fdeeb`, `504372`, `5042ce` y proyectiles de habilidades;
  H8 en `558da4`/`558f29`; H11 en `4eac41`/`4fbee8`; H13 en `50428c`.

Blind spots observados: el ataque por clase de `test_attack_step` configura el equipo propio pero
no las manos visibles del actor; el de monstruo llama directamente al paso de ataque y no atraviesa
el cuerpo/estela. Sus resultados previos no acreditaban esas rutas. Los tests de cortes deben
reemplazarse con observaciones de productores reales al portar cada frontera.

## Regresiones discriminantes y observación manual

- `AttackStepTest.AVisibleMeleeWeaponSurvivesTheFullAttackBodyPipeline`: RED exacto en `4fd236`
  con los tres archivos de main; GREEN con cuerpo equipado y cuatro pasos completos.
- `CharacterStepTest.ANewFaceInTheGameUsesTheGameAnimationEndGate`: con la condición anterior
  que forzaba selección por tipo, se borraban ambos latches de pisada. Con la escena real se conservan.
- Dibujo de araña y pose de Crywolf: el frame compartido señuelo 6,25 persistía con la escritura
  anterior de sólo key. Ahora se publican frame/key juntos; entre frames originales no se escriben.
- Los pools cruzados de efectos/partículas distinguen los efectos145/147 de las partículas, y los
  casos194/195/197 prueban prioridad y prefijos antes de un constructor aún no portado.

Los últimos 124 tests de CharacterStep/MonsterDrawing/ModelEffects pasaron tras observar los RED
anteriores. La compilación de todos los targets pasó sin PCH y con warnings como errores.

MuRet siguió activo durante el combate físico de Emanuel: imágenes con bajas, avisos de 21.000/27.000
XP y reaparición en Lorencia tras la muerte informada. Sin errores de escena. No se capturó el frame
exacto de muerte ni D7; no es una comparación punto a punto con XP. El cliente original sí produjo
500 lecturas completas, con acciones 23/24 y cinta 0/key 0/RGB(1;0,6;0,2), según research 15.

La revisión independiente final no encontró nuevos P0/P1/P2 reproducibles en el diff; fue estática,
sin ejecutar el original. Los 29 archivos C++/headers de aplicación y tests pasaron
`misc-include-cleaner` 18; se retiraron includes obsoletos y se agregaron los directos necesarios.
Los dos auxiliares XP se recompilaron con MinGW-w64 y warnings como errores, sin red, y los dos
fuentes Python modificados se analizaron sin generar cachés. No se versionan los ejecutables.

## Resultado global

- Build completo final: éxito, sin PCH, warnings como errores, máximo dos compiladores.
- CTest completo final: **7.365/7.365**, cero fallos/saltos, **240,06s**; máximo dos tests.
- XML revisado: **106/106 GL** y **59/59 referencias del manifest** presentes y ejecutadas.
- clang-format 18: 31/31 fuentes y headers C/C++; includes: 29/29 de aplicación/tests.
- La observación manual combate/recompensa/reaparición pasó sin error de escena. Sigue pendiente
  el contraste dinámico estricto de daño, muerte, recursos y ramas especiales con el original.
- El inventario de fronteras anterior continúa abierto: no se declara H3 al 100 % funcional.

Comandos: `cmake --build --preset linux-x64-debug --parallel 2` y
`ctest --preset linux-x64-debug --parallel 2 --output-on-failure --output-junit SALIDA_PRIVADA`,
con corpus/assets, fuente y `MURET_CLIENT_REQUIRE_GL=1`. Logs/XML y capturas fuera de Git.
