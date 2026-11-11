# CL7: continuación del traspaso a Codex

Seguimiento del [traspaso de Claude](2026-11-10-cl7-traspaso.md), desde
`7a76c9d25460a86f308391ab2e71c2475cfd43f1`. Esta tanda avanza varias tareas; **no cierra la CL7**.
Se aplicaron [AGENTS.md](../../../AGENTS.md), las [reglas](../../CODING_RULES.md), la skill local
`cpp-pro`, los contratos de research y Context7. Revisión adversarial: **GPT-6 Astra, esfuerzo high**.

## Estado de las diez tareas

| Tarea | Resultado de esta tanda | Qué queda |
| --- | --- | --- |
| 1. P-C55, dibujo del juego | Esfera/seen persistente, siete casos de suelo y anillos; jugador con sombra/scan/luz/color/tela; gotas y paso de partículas. | Ramas explícitamente cortadas, texturas restantes, pose global retenida, fauna y orden después de los ítems del piso. |
| 2. Paso del login | Lugar 2 construido como jugador, 41 sorteos después del logo, pool canónico, `StepCharacters` sobre terreno 55 y cuervo desde el cuerpo. | Observación nativa de presupuestos/pose y ramas comunes todavía no portadas; P-C56. |
| 3. Paso de selección | Lugares de la lista construidos, apariencia/cuerpo persistentes, paso común, Item.bmd y vista previa NewFace durable. | Paso contextual de helpers, ramas comunes, mundo/objetos/efectos de 54 y observación nativa; P-C50/P-C54. |
| 4. T9, cero pasos | Se elimina el corte incondicional por efecto vivo: seen heredado y `0x61f/0` acreditado. Entrada fría con dos gotas llega a `0x6374fe`, con RNG comprobado. | Frame completo después de F3 03/F3 10, equipo/helpers/etapas posteriores y seen original en XP. T9 sigue parcial. |
| 5. T15, barrido | Regresiones de entrada, cero/primer paso y transiciones; [36 casos de resoluciones](../../../Client/tests/scenes/test_game_scene_viewports.cpp) 640×480, 1024×768 y 1920×1080, con 0/1/3 pasos, retención, proyección y rayo. | Equipo, archivos faltantes, frame completo y contraste original. Los cortes esperados no cierran T15. |
| 6. Hito2 | Inventario contrastado con código e informe; se distinguen kernels puros de callers ausentes. | Las 16 tareas de [caminar/gates](2026-11-09-cl7/hito2-caminar-y-gates.md): estado wire, Walk/MoveHero, clic→D3, aplicadores, gates, 1C, regen y vuelta. Sin implementación nueva en esta tanda. |
| 7. Hitos3/4 | Luz dinámica durable y lectores en tres escenas; especiales por familia, Gate globales, ajustes Dungeon/Kalima y reset de active; fuente/partículas/suelo. H4.11 conecta el paquete de clima `0F` con su target durable; 14 pruebas específicas cubren las tres ramas y los 256 bytes posibles. | Productores de luz/fuego, objetos de otras familias, pools/servicios/dibujos de fauna, consumo visible del clima/hojas, recursos fijos y barridos de integración. |
| 8. Hito5 | Especiales contra Data real de 25 mapas; [MapSweep de 39 nominales](2026-11-09-cl7/map-sweep-2026-11-10.md): 79 casos de carga, archivos/Gate.bmd, identidades, objetos, errores, limpieza y RNG, sin omisiones. | GateSweep de 38, SameMapSweep, handlers1C/F3 04, casos especiales y frames completos. Cargar modelos no acredita caminar ni render completo. |
| 9. Roadmap | CL11 conserva obligaciones de CL7–CL10; [tarea de mapeo](2026-11-10-cobertura-roadmap-cliente.md) con inventario de 5.648 VAs, esquema y controles negativos. | Ejecutar el mapeo y su verificador. La tarea de documentación está definida; las 5.648 funciones todavía no están clasificadas. |
| 10. Original nativo | Pendientes y [runbook](../../../Client/docs/research/scripts/game/capture/RUNBOOK-cl7-vm.md) preservados; sin nuevas emulaciones. | No hay VM XP configurada: CRT, primeros frames, x87, trazas GL y MuError.dmp original quedan abiertos. |

## Contratos y límites

Fuente estática: build `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
Direcciones y fórmulas en [06](../../../Client/docs/research/06-render-escenas-y-ui.md), rarezas en
[07](../../../Client/docs/research/07-quirks-y-bugs.md) y pendientes en
[08](../../../Client/docs/research/08-pendientes.md). Se preservan sorteos y efectos anteriores a
cada error; ninguna etapa pendiente se omite para presentar un frame.

`WorldState` conserva efectos, partículas/viento y luz dinámica entre escenas. Crear/apagar un
efecto conserva seen. La copia inicial de luz usa 0..255; después el paso copia el rango publicado
por el dibujo anterior ampliado 3, conservando el exterior. Faltan los productores de luz del mundo.

Los especiales prestan Gate01/02 sin copiar los modelos globales. Queda pendiente la historia física
de slots compartidos tras `WorldModels::Release`: el 69 puede haber sido sobrescrito y la metadata
global todavía ser un snapshot inicial. No se acredita su ciclo de vida completo ni se inventa una
semántica para los seis stores de Tarkan en `modelo+0xc8`. BMD cargados no equivalen a recursos
fijos, sonidos o efectos creados.

## Revisión y comprobación

Se observaron pruebas RED→GREEN de los comportamientos nuevos. La integración revisó las causas
de errores de contexto en UI y expectativas antiguas de cortes/RNG. El paso incluye dos rand del
viento con pool vacío; ambas gotas agregan ocho después del guard; el render ordinario del héroe
visto suma 3.848+1, además de los consumos anteriores.

P-C40 conserva una invalidación preventiva del scan antes de dibujar mientras faltan etapas del
frame original. Los jugadores que alcanzan `0x50c91e` publican su resultado. Esa precaución del port
no se presenta como una escritura nativa ni como el cierre de todas las segundas entradas.

Astra motivó regresiones para el origen del offset del plano lejano, operaciones GL sin geometría,
tela entre frames, descarte de destellos intermedios y prefijos de sombra/partes/scan/luz. También
se corrigieron el prefijo de un catch-up fallido, los Gate opcionales ausentes, el orden escena→viento
y contador, el reloj de reapertura del preview y la vida/reciclado de partículas del cuervo.
Astra cerró todos los hallazgos Important que reportó y no dejó Critical. H4.11 también pasó su
revisión acotada. Esto no transforma las fronteras documentadas de la CL7 en funcionalidad terminada.

Verificación final del árbol integrado, en Linux con GCC 14, warnings como errores y sin PCH:

- Build completo correcto. CTest registró **3.867 casos: 3.808 pasaron, 59 omitidos y 0 fallos**.
  Las omisiones requieren capturas, memoria o trazas externas del original; no son pases acreditados.
- **93/93 pruebas GL** correctas con Mesa/Xvfb y `MURET_CLIENT_REQUIRE_GL=1`, sin omisiones.
- Ejecutable: 60 frames en modo normal, VSync off y faithful; con Data real, carga→login y120 frames.
  Las cuatro ejecuciones terminaron con código0, sin conectarse a un servidor externo.
- **108 archivos C++/headers** pasan clang-format18 y `misc-include-cleaner` de clang-tidy18.
- Los enlaces locales de la documentación modificada y `git diff --check` están comprobados.

Comandos principales, después de preparar las dependencias del README y una pantalla Xvfb:

```bash
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug --output-on-failure --no-tests=error -j4
MURET_CLIENT_REQUIRE_GL=1 ctest --preset linux-x64-debug -L '^gl$' --no-tests=error
```

No se ejecutó Windows/MSVC ni la VM XP original. Los commits por concepto quedan en la rama local
`codex/cl7-continuacion-claude`; esta tanda no crea PRs ni integra cambios en `main`.

## Orden de continuación

1. Completar P-C55 y el orden detrás de `0x6374fe`, coordinado con CL9.3 para ítems del piso.
2. Hito2: eventos/estado wire, Walk/Stop y MoveHero; después clic/paquetes, gates, 1C, regen y vuelta.
3. Hitos3/4 por familias: productores de luz y objetos, pools/servicios de fauna, clima/hojas y dibujos.
4. Ampliar barridos de entrada/mapas hasta cubrir frames completos y caminos reales de red.
5. Ejecutar CL.M y las observaciones nativas cuando esté disponible la VM.
