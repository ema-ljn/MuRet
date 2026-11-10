# CL7: traspaso de las tareas que quedan (2026-11-10)

> Actualizado con el WIP de hito 2 + P-C55(c)/(f), desde `2110806`, en
> `codex/cl7-resto-traspaso`. La [continuación previa](2026-11-10-cl7-continuacion-codex.md)
> documenta la tanda anterior; sus totales no verifican esta tanda.

El 2026-11-10 Emanuel pidió cerrar lo que estaba en curso y que **Codex siga las tareas que quedan de la CL7**. Este documento distingue la base histórica recibida, el trabajo local en curso y lo que falta, en orden. Los informes de cada hito están en [`2026-11-09-cl7/`](2026-11-09-cl7/).

> **El modo tutor está apagado por ahora** (Emanuel, 2026-11-10; ver [`AGENTS.md`](../../../AGENTS.md)): el agente escribe el código, sin pedir el consentimiento del tutor. Así se hizo la CL7 desde el 2026-11-02.

## Base histórica recibida

La CL7 (el mundo y todos los mapas) se partió en cinco hitos. Los dos primeros son 1, entrada, héroe y equipo, y 2, caminar, gates, warps y la vuelta. Los otros tres son 3, objetos, luz y pasto; 4, fauna, clima y partículas; y 5, todos los mapas, del 0 al 36 más el 54 y el 55. Al recibir este traspaso, el hito 1 estaba casi cerrado y el 3 empezado; la tabla siguiente conserva esa fotografía histórica, no el estado del WIP actual. El roadmap del cliente está en [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md), y los planes previos de la ola, en [`2026-11-05-ola-cl7-lorencia.md`](2026-11-05-ola-cl7-lorencia.md) y [`2026-11-05-ola-cl7-el-login-completo.md`](2026-11-05-ola-cl7-el-login-completo.md).

Del hito 1 ([informe](2026-11-09-cl7/hito1-entrada-y-heroe.md), tareas T1 a T15):

| Tarea | Estado del informe original | PR |
| --- | --- | --- |
| T1 `InWorld`, T2 `LogicalAccess`, T3 registro propio, T4 pool durable | hecha | #188, #187, #186, #191 |
| T5 la espera, T6 `0x512a30` y `0x4f2250`, T7 la entrada (`0x4c3940`) | hecha | #197, #195, #200 |
| T8 el dibujo y el paso del juego | hecha; dibujo parcial en esa fotografía | #206 y siguientes |
| T9 el `F3 10` | **a medias** en el informe original; ver el alcance actual de T9 abajo | #207 |
| T10 los stats propios (`0x550d10`) | hecha | #198, #202 a #205 |
| T11 la zona segura en cada paso, T11b el trapecio global | hecha | #209, #210 |
| T12 el caballo oscuro | **a medias** en el informe original: paso cableado y dibujo portado; ver la cola actual abajo | #213 |
| T13 el segundo `F3 03`, T14 el `F1 02` con 1 | hecha | #208, #211 |
| T15 el barrido del hito | **abierta** | — |

Además, después del informe:

- el pasto (#212) y las marcas del suelo (#215);
- el paso de los personajes `0x508f00`: `0x508ba0` (#214), el cuerpo `0x4fd5f0` (#217) y `0x505de0` con todos los personajes (#219);
- los objetos del mapa en el dibujo y el paso del juego, en los mapas 0 y 2 (#218);
- el dibujo del juego después de los objetos: el `0x69` de Lorencia, los efectos del suelo, los pájaros y los personajes `0x510d00` (#221);
- el manejador de caídas `MuError.dmp`, de la CL3 (#216).

## Estado actual de la rama WIP

El [plan de esta tanda](2026-11-10-cl7-resto-traspaso.md) cubre hito 2 y P-C55(c)/(f), con
`WorldState` dueño de la historia durable y errores explícitos después del prefijo nativo.
La base es `2110806`; las implementaciones e integración siguientes están en
`codex/cl7-resto-traspaso` con cambios WIP. **No hay PR nuevos publicados para esta tanda.**
Los PR #222–#237 corresponden a la continuación previa y siguen sin merge; tampoco se presume
que este árbol local esté en `main`. La publicación por concepto sigue pendiente.

| Dominio | Alcance implementado y comprobado | Lo que no acredita |
| --- | --- | --- |
| Wire/sesión | Eventos D3/D7/1C/F3 04 síncronos, historial 1C, envíos Walk/gate/F3 12/BD00/rumbo y rechazos/orden con GS falso; módulos focales verdes. [Informe](../../../.superpowers/sdd/cl7-resto-traspaso/hito2-session-report.md). | Servidor externo ni toda la secuencia de escena/mapas. |
| Actores | Walk/Run/acciones y overlay durable de velocidades compartidas sin mutar Skeleton; MovePath, celda adelantada, llegada/Stop/yaw, otros actores, reintento/cooldown y pasos; módulos focales verdes. [Informe](../../../.superpowers/sdd/cl7-resto-traspaso/actor-motion-report.md). | Creadores/acciones y tipos que conservan frontera; sonido real de CL10 ni captura nativa. |
| Héroe/gates | StepHeroMovement con cabeza, movimiento, gates, clic y cierre propio/Skill/capa; orientación quieta/paquete 18; Skill.bmd obligatorio real. GameGates preserva lecturas/RNG y recorre la tabla real; módulos focales verdes. [Gates](../../../.superpowers/sdd/cl7-resto-traspaso/game-gates-report.md), [rumbo](../../../.superpowers/sdd/cl7-resto-traspaso/hero-facing-report.md), [Skill](../../../.superpowers/sdd/cl7-resto-traspaso/skill-loading-report.md). | UI pendiente, macros de destino 30/31, todos los gates caminando ni toda la integración del input. |
| Aplicadores/transiciones | D3/D7/shock, 1C mismo/distinto mapa y gate 0, F3 04 con campos retenidos, limpieza/reentrada/retorno; pruebas focales verdes con Data/GS falso. [Informe](../../../.superpowers/sdd/cl7-resto-traspaso/transitions-report.md). | UI Chaos/ventanas/cursor, eventos/LoadMap especiales ni todas las familias en GameScene. |
| GameScene | Entrada real y lógica entre frames completos en mapa 0 verdes. Frame con recursos reales, dibujo intermedio, viewport/rayo y regresiones antiguas comprobados en su alcance. [Tests antiguos](../../../.superpowers/sdd/cl7-resto-traspaso/legacy-scene-tests-report.md). | Mapa 2 sin armas y mapa 3 siguen integrándose. Un frame sin productores vivos no prueba todas sus ramas ni equipo completo. |
| Mundo 0/2 y hojas | Fuegos/lámparas/puertas conectados a luz/partículas/sonidos lógicos; binder de hojas/clima con guardas por mapa/capa y creadores reales de lluvia/salpicaduras/nubes. [Objetos](../../../.superpowers/sdd/cl7-resto-traspaso/h3-object-services-report.md), [hojas](../../../.superpowers/sdd/cl7-resto-traspaso/game-leaves-report.md). | Otros mapas/productores, audio real y contraste nativo. |
| P-C55(c)/(f) | Pools durables, recursos/servicios de fauna y orden de cola conectados; piso, hojas, joints, cintas, efectos, sprites, partículas y daño tienen implementaciones acotadas y fronteras por rama. [Fauna](../../../.superpowers/sdd/cl7-resto-traspaso/fauna-implementation-report.md), [cola](../../../.superpowers/sdd/cl7-resto-traspaso/p55-tail-implementation-report.md). | Important de Astra en corrección/verificación; no están cerradas fauna, cola ni revisión final. |
| Matriz de movimiento | Consumidor aislado en 0..36 con terreno contractual explícito, Skill real, callbacks de envío y estado durable; recta/codo/borde/zona segura. [Alcance y fronteras](../../../.superpowers/sdd/cl7-resto-traspaso/movement-map-matrix-report.md). | No es caminar de escena de extremo a extremo en todos esos mapas; 54/55 sólo cubren lo aplicable al terreno. H2 T16 permanece abierto. |

`0x6374fe` ya no es el corte universal del dibujo. Se exige presentación cuando el camino completo
está implementado y se conserva el error en la primera rama realmente ausente: por ejemplo,
el jugador fuera de zona segura aún puede alcanzar `0x50e298`, una marca sin metadata
`0x549152`, y los otros mapas sus fronteras propias. No se omiten etapas para presentar.
Login y selección ya ejecutan el paso de actores; sus helpers, mundo y ramas restantes conservan
los límites de P-C50/P-C54/P-C56.

**La verificación integrada de esta tanda sigue abierta:** build/CTest global, GL, smoke y revisión
final. Los conteos cambian mientras se integra; los informes enlazados dan comandos y alcance de
cada verde focal. Root incorporará los totales finales después de verificarlos. No se reutilizan
como cierre de este WIP los totales globales/GL/smoke de la continuación previa. **CL7, hito 2 y
T16 para todos los mapas no se declaran cerrados.** La VM queda al final por pedido de Emanuel.

## Cómo se trabajó

- **Fidelidad máxima, incluido el flujo de `rand()`.** Cada `rand()` del original se cuenta y se saca en el mismo orden. Donde algo no está portado, el código corta con error y la dirección **después** de los `rand()` que el original saca antes. Nunca se saltea en silencio.
- **El research primero.** Antes de portar, se lee estático el ASM, con direcciones, y se escribe en [`Client/docs/research/`](../../../Client/docs/research/README.md): 06 para escenas y render, 07 para las rarezas y 08 para lo pendiente. Las etiquetas de evidencia son las del README:
  - *estático*: leído del ASM;
  - *inferido*: deducido sin verlo;
  - *emulado*: solo con el `main.exe` real corrido en unicorn;
  - *simulado*: un cálculo con el port de MuRet.

  Una inferencia no se escribe como hecho. El binario es el `c32bbd5f…`; dónde está, y su decompilación, lo dice el README del research.
- **Los números.** Las rarezas van en 07 como Q-C y lo pendiente en 08 como P-C, en orden
  numérico. Antes de reservar otro ID, consultar las filas actuales de ambos archivos: las
  reservas del traspaso histórico ya no son un inventario vigente de números libres.
- **Todos los mapas, siempre.** Nunca solo Lorencia. Los mapas de referencia son Lorencia (0) y Devias (2), y los tests van en los dos donde el mapa importa.
- **Fuera de alcance.** No se portan protecciones ni anticheat: la tabla de protección del registro propio se modela solo como los `rand()` de `Render::Models::LogicalAccess`. No se hacen emulaciones nuevas. No van nombres de personajes ni de cuentas en tests ni logs. Nunca se versionan volcados de memoria, PCAPs, `muproxy.log` ni capturas.
- **Un concepto por PR, chico.**
  - Título `CL7: …` o `CL7 hito N: …`, en castellano, con las secciones "Qué hace", "Research" y "Pruebas".
  - Merge por squash; el repo no acepta merge commits.
  - Un PR apilado se rebasea después del squash con `git rebase --onto origin/main <la cabeza vieja de abajo>`, y las filas de 07 y 08 quedan en orden numérico.
- **El CI son tres checks:** Linux GCC con clang-tidy `misc-include-cleaner`, Windows MSVC `/W4 /WX` y clang-format-18. Trampas que ya rompieron el CI:
  - un campo nuevo en un agregado que los tests arman con inicializadores designados (`WorldRuntime`) necesita `{}` o `= valor`;
  - MSVC pide `static_cast` explícitos (C4242);
  - include-cleaner pide incluir lo que se usa.
- **Desensamblar:** `objdump -D -b binary -m i386 --adjust-vma=0x400000 --start-address=… --stop-address=… main.exe`. La lectura del PE directa falla con ese archivo.

## Qué falta, en orden

1. **Cerrar las correcciones de la revisión Astra de fauna/cola.** Mantener RED→GREEN y el orden
   de escrituras/RNG: pose/sombra retenida, creadores frente a dibujos intermedios, altura fija y
   demás hallazgos de los informes. No dar por cerrada la revisión por un verde anterior al fix.
2. **Terminar la integración real de GameScene.** Conservar el verde de entrada real con lógica
   en 0, completar 2 sin armas y 3, y probar los productores vivos que cada caso alcance. Piso,
   fauna, helpers y partículas deben recibir terreno, modelos, reloj y destinos durables reales;
   los tests parciales no sustituyen ese camino. Después ampliar equipo/armas/alas y demás ramas
   de P-C55(d), conservando sus errores hasta portarlas.
3. **Completar hito 2 y su barrido T16.** Los módulos wire/aplicadores/actores/gates ya existen;
   quedan la validación integrada de clic→D3→movimiento→gate/1C/regen→vuelta, familias, solapes,
   campos retenidos y prefijos de error. La matriz del consumidor 0..36 usa fixtures declarados:
   no acredita por sí sola LoadMap/escena de esos mapas. Mantener explícitos LoadMap 30/34/Kalima,
   archivos ausentes 5/35, normalización 32→9 y eventos; 54/55 no son mapas de caminar del héroe.
4. **Completar las ramas vivas restantes de P-C55(c)/(f).** No confundir pools con productores:
   faltan familias/opciones/brillo/mapas acuáticos del piso, creadores de cintas y joints, ramas
   especiales de joints/efectos, tipos/modelos y demás recursos. Ver los informes de fauna/cola
   y P-C55 para separar lo implementado, lo conectado y lo todavía cortado.
5. **Objetos, luz, clima y mapas restantes de hitos 3/4/5.** Los servicios de 0/2 y el binder de hojas
   son avances de esta tanda; faltan otros casos/familias, entrada de eventos, ciclos de recursos,
   GateSweep/SameMapSweep y frames completos según cada mapa. Los avisos de audio no equivalen
   a reproducción de CL10. Referencias: [hitos 3/4](2026-11-09-cl7/hito3-4-mundo-vivo.md),
   [hito5](2026-11-09-cl7/hito5-matriz-mapas.md), P-C12/P-C31/P-C53/P-C57.
6. **T9/T15 del hito 1 y selección/login restantes.** Hay frames presentados y tests de viewport
   actualizados con recursos reales; siguen abiertos equipo/helpers/archivos faltantes, cobertura
   completa después de F3 03/F3 10 y las ramas de P-C50/P-C54/P-C56. Mantener separados el frame
   frío, el primer paso, catch-up y dibujo intermedio.
7. **Verificación global, GL, smoke y revisión final de la tanda integrada.** Con el entorno común,
   source `/workspace/.tools/muret-env.sh`, ejecutar bajo `/workspace/.tools/muret-build.lock`:

   ```bash
   cmake --build --preset linux-x64-debug
   ctest --preset linux-x64-debug --output-on-failure --no-tests=error -j4
   MURET_CLIENT_REQUIRE_GL=1 ctest --preset linux-x64-debug -L '^gl$' --no-tests=error
   ```

   Repetir los smoke normal/VSync off/faithful y carga→login con Data real según el procedimiento
   de la continuación; registrar comandos, precondiciones, errores y omisiones reales. Revisar
   formato/includes y documentación. No fijar totales finales hasta que Root verifique ese árbol.
8. **Publicar por concepto después de la revisión y los checks correspondientes.** Todavía no hay
   PR nuevos de este WIP. No mergear #222–#237 ni otros PR sin autorización; no modificar configuración
   de GitHub. El PR #185 sigue siendo un borrador histórico cuyo cierre decide Emanuel.
9. **Cobertura del roadmap.** Ejecutar la [tarea de mapeo](2026-11-10-cobertura-roadmap-cliente.md)
   y su verificador. La definición de CL11 y esa tarea no equivalen a clasificar todas las funciones
   sin nombre ni a haber portado los cuerpos pendientes.
10. **VM al final.** Conservar las observaciones nativas de cada P-C de 08: CRT/espera/primeros
    frames, x87, seen/pose, caminar/gates/warps, trazas GL y MuError.dmp original. Las corre Emanuel
    en la VM XP con el [runbook](../../../Client/docs/research/scripts/game/capture/RUNBOOK-cl7-vm.md),
    sin versionar volcados ni capturas. Los tests CPU/GL del port y el ASM estático no las sustituyen.
