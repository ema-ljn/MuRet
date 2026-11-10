# CL7: traspaso de las tareas que quedan (2026-11-10)

> La actualización CL7b de abajo fija el estado más reciente. La fotografía de la tanda
> `codex/cl7-resto-traspaso`, desde `2110806`, se conserva como antecedente.
> La [continuación previa](2026-11-10-cl7-continuacion-codex.md) documenta una tanda anterior.

El 2026-11-10 Emanuel pidió cerrar lo que estaba en curso y que **Codex siga las tareas que quedan de la CL7**. Este documento distingue la base histórica recibida, el trabajo local en curso y lo que falta, en orden. Los informes de cada hito están en [`2026-11-09-cl7/`](2026-11-09-cl7/).

> **El modo tutor está apagado por ahora** (Emanuel, 2026-11-10; ver [`AGENTS.md`](../../../AGENTS.md)): el agente escribe el código, sin pedir el consentimiento del tutor. Así se hizo la CL7 desde el 2026-11-02.

## Continuación CL7b y publicación consolidada

El [plan CL7b](2026-11-10-cl7b-t16-kalima-p55.md) continúa Windows, T16, Kalima y P-C55.
Por pedido de Emanuel se reemplaza la fragmentación de los PRs #222–#287 por seis bloques:
[#288, bases y recursos](https://github.com/ema-ljn/MuRet/pull/288),
[#289, actores y acceso](https://github.com/ema-ljn/MuRet/pull/289),
[#290, pose y efectos](https://github.com/ema-ljn/MuRet/pull/290),
[#291, fauna y objetos](https://github.com/ema-ljn/MuRet/pull/291),
[#292, movimiento y transiciones](https://github.com/ema-ljn/MuRet/pull/292) y
[#293, ciclo de escenas](https://github.com/ema-ljn/MuRet/pull/293).
La pila parte de `main` y cada bloque usa como base su predecesor real, excepción autorizada.
Los anteriores se cierran como reemplazados sólo después del CI propio de los reemplazos,
preservando referencias y conversaciones. No se mergea automáticamente `main`.

Esos seis PRs recomponen exactamente el tree portátil `a622e314`, que ya tenía Linux/Windows
verdes, y conservan los cinco fixes de portabilidad. La revisión de Astra aprueba los seis
cortes; cada head remoto tiene además su corrida propia. Las ampliaciones funcionales siguientes
son otra captura: no se atribuyen al CI del tree anterior.

El avance funcional nuevo quedó guardado en el commit local `8b6868c`: **4.851 casos registrados,
4.792 pasan, 59 omiten evidencia externa y cero fallos**, incluidos **99 GL** bajo Xvfb obligatorio.
También pasan tres arranques, formato18 y los121 checks de includes del delta. Astra revisó los
kernels y corrigió tres P2 de adapters: materiales durables tras recarga/frame0, pulso global
antes de la espera y sprites nuevos tras invalidar el replay. Windows de esta captura y la
observación nativa siguen pendientes.

| Dominio | Avance comprobado en CL7b | Pendiente |
| --- | --- | --- |
| T16 | 198/198: matriz0..36, terreno/alias, D3 real del GS falso, gates y ciclo real de selección/reentrada. Las filas con fronteras comprueban el corte. | Escenas/caminata completas de todas las familias; las filas negativas no las sustituyen. |
| Kalima | Loader, agua, dos planos, cáusticas32, modelo firmado−1 desde el initializer real, cuerpo/sombra/sprite y prefijo global de objetos. Se conservan los2.266 objetos reales. | Movimiento438ad0, partículas438f81/4389ca y las demás familias alcanzables, efecto9; cuadro poblado y24→0→24 completos. |
| P-C55(f) | Familias/opciones/agua del piso,51IDs de609640, stores antes del error, efectosf4/fa, cintas y Helper07→joints614/14→Thunder5ca/0 con recursos físicos. | Productores y ramas restantes de equipo, armas, alas y CL9–CL11. |
| Q-C180 | MAP/ATT→OBJ→alturas/luz; primer aviso fatal durable, propagación por sesión y modal SDL solicitado con la ventana viva. | Observación del modal y del cierre en Windows/VM. |

**CL7, Kalima y T16 universal siguen abiertas.** También quedan CL.M y el contraste nativo.
La VM permanece al final por pedido de Emanuel; los cuerpos públicos pendientes se pueden
continuar mediante lectura estática sin esperar esa VM.

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

## Estado de integración de la tanda anterior

El [plan de esta tanda](2026-11-10-cl7-resto-traspaso.md) avanza hito 2 y P-C55(c)/(f), con
`WorldState` dueño de la historia durable y errores después del prefijo nativo. La base es
`2110806`. El conjunto se conserva en commits por concepto; no está integrado en `main`.

Ya están publicados [#238, Skill.bmd](https://github.com/ema-ljn/MuRet/pull/238) y
[#239, caminar del actor](https://github.com/ema-ljn/MuRet/pull/239). Se actualizaron por
fast-forward [#230, eventos y envíos](https://github.com/ema-ljn/MuRet/pull/230) y
[#229, recursos durables](https://github.com/ema-ljn/MuRet/pull/229). Sus comprobaciones aisladas
son 180, 61, 219 y 33 pasados respectivamente; #229 omite un golden externo. Los PR #222–#237
anteriores siguen sin merge. La [rama del conjunto](https://github.com/ema-ljn/MuRet/tree/codex/cl7b-integracion-validada)
y el [índice #237](https://github.com/ema-ljn/MuRet/pull/237) registran los bloques que
aún necesitan publicación individual; no deben confundirse con cuatro PR que incluyan toda la tanda.

| Dominio | Alcance implementado y comprobado | Lo que sigue abierto |
| --- | --- | --- |
| Wire/sesión | D3/D7/1C/F3 04 síncronos, historial 1C y envíos Walk/gate/F3 12/BD00/rumbo, con GS falso. [Tests](../../../Client/tests/app/test_server_session.cpp). | Contraste con servidor externo y casos UI de CL8. |
| Actores | Walk/Run, velocidades compartidas sin mutar Skeleton, MovePath/celda adelantada, Stop/yaw, cooldown/reintentos y pasos lógicos. [Tests](../../../Client/tests/scenes/test_actor_motion.cpp). | Acciones que conservan una frontera y audio real de CL10. |
| Héroe/gates | Cabeza→movimiento→gates→clic→cierre→capa, Skill real, paquete18, clic pendiente y espera. [Tests](../../../Client/tests/scenes/test_game_movement.cpp). | Macros30/31, UI y barrido integrado de todos los gates. |
| Aplicadores/transiciones | D3/D7/shock, teleport mismo/distinto mapa y gate0, F3 04, campos retenidos, limpieza y retorno a selección. [Tests](../../../Client/tests/scenes/test_world_transitions.cpp). | LoadMap especiales/eventos y todas las familias en GameScene. |
| GameScene | F303 y lógica entre cuadros completos de0/2/3, cuadros intermedios, espera con lógica activa, viewport/rayo y1C0→3→0. [Tests](../../../Client/tests/scenes/test_game_scene_complete.cpp). | Equipo completo/armas/alas y escenas de extremo a extremo en los otros mapas. |
| Objetos y clima | Fuegos/lámparas/puertas0/2; objetos, UV, partículas y luz de Noria; hojas/lluvia/nieve y texturas físicas64..68. [Recursos meteorológicos](../../../Client/tests/scenes/test_weather_textures.cpp). | Otros productores/mapas, audio real y observación nativa. |
| P-C55(c)/(f) | Aves/peces durables, pasos/dibujos, modelos y poses retenidas; piso, helpers, joints, efectos, cintas, sprites, partículas y daño conectados. Atlas1 y texturas de los joints emitidos tienen recursos reales. [Cola](../../../Client/tests/scenes/test_game_draw_tail.cpp), [recursos](../../../Client/tests/scenes/test_game_tail_textures.cpp). | Modelo firmado−1/backend Kalima, familias/opciones del piso y productores/ramas CL9–CL11. |
| Matriz de movimiento | Kernels0..36: recta/codo/borde/zona segura, Skill real, estado durable y fronteras declaradas. | No acredita LoadMap/escena de esos mapas ni cierra H2 T16.54/55 sólo cubren terreno aplicable. |

`0x6374fe` ya no es el corte universal. Las escenas de referencia presentan el camino completo
portado; una rama ausente sigue devolviendo su dirección, por ejemplo `0x50e298` del jugador
fuera de zona segura. Login y selección ejecutan el paso común de actores; sus límites restantes
son P-C50/P-C54/P-C56.

La revisión Astra6 high corrigió los Important de wire, movimiento, fauna, poses y cola.
La última pasada añadió pruebas de GL real: los comandos sin vértices se ejecutan; un flush y
`6213e0` conservan material; `620db0` reinicia sólo sus ejes acreditados. El backend/loader Kalima
no se reemplazó por un éxito vacío. La verificación final registra 4.451 casos: 4.380 pasados, 71 omitidos por evidencia externa y
cero fallos; 96/96 GL y cuatro arranques correctos. Formato 18 e include-cleaner 18 limpios
en su alcance. Estas comprobaciones no sustituyen el contraste con el original.

**CL7, hito2 y T16 de todos los mapas siguen abiertos.** La VM queda al final por pedido de Emanuel.

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

1. **Completar la publicación individual del conjunto.** Los cuatro conceptos publicados arriba
   tienen comprobaciones aisladas. El resto de commits de integración conserva sus dependencias;
   extraer cada PR sobre main y repetir sus checks antes de declararlo listo.
2. **Ampliar la integración real más allá de las referencias0/2/3.** Conservar sus frames completos,
   probar clic→D3→gate de extremo a extremo y ampliar equipo/armas/alas. Piso, fauna, helpers y
   partículas ya reciben terreno/modelos/reloj y pools durables reales en las referencias.
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
