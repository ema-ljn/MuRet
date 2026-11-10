# CL7: traspaso de las tareas que quedan (2026-11-10)

El 2026-11-10 Emanuel pidió cerrar lo que estaba en curso y que **Codex siga las tareas que quedan de la CL7**. Este documento es la entrada: qué hay en `main`, con qué reglas se trabajó y qué falta, en orden. Los informes de cada hito están en [`2026-11-09-cl7/`](2026-11-09-cl7/).

> **El modo tutor está apagado por ahora** (Emanuel, 2026-11-10; ver [`AGENTS.md`](../../../AGENTS.md)): el agente escribe el código, sin pedir el consentimiento del tutor. Así se hizo la CL7 desde el 2026-11-02.

## Qué hay en `main`

La CL7 (el mundo y todos los mapas) se partió en cinco hitos. Los dos primeros son 1, entrada, héroe y equipo, y 2, caminar, gates, warps y la vuelta. Los otros tres son 3, objetos, luz y pasto; 4, fauna, clima y partículas; y 5, todos los mapas, del 0 al 36 más el 54 y el 55. El hito 1 está casi cerrado y el 3 empezado. El roadmap del cliente está en [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md), y los planes previos de la ola, en [`2026-11-05-ola-cl7-lorencia.md`](2026-11-05-ola-cl7-lorencia.md) y [`2026-11-05-ola-cl7-el-login-completo.md`](2026-11-05-ola-cl7-el-login-completo.md).

Del hito 1 ([informe](2026-11-09-cl7/hito1-entrada-y-heroe.md), tareas T1 a T15):

| Tarea | Estado | PR |
| --- | --- | --- |
| T1 `InWorld`, T2 `LogicalAccess`, T3 registro propio, T4 pool durable | hecha | #188, #187, #186, #191 |
| T5 la espera, T6 `0x512a30` y `0x4f2250`, T7 la entrada (`0x4c3940`) | hecha | #197, #195, #200 |
| T8 el dibujo y el paso del juego | hecha; el dibujo corta donde dice abajo | #206 y siguientes |
| T9 el `F3 10` | **a medias**: falta el frame de cero pasos, que corta en los efectos del suelo (`0x52dc3b`, P-C55 b) | #207 |
| T10 los stats propios (`0x550d10`) | hecha | #198, #202 a #205 |
| T11 la zona segura en cada paso, T11b el trapecio global | hecha | #209, #210 |
| T12 el caballo oscuro | **a medias**: el paso está cableado (`0x6370f4`); el dibujo (`DrawHelpers`, `0x637508`) está portado pero queda detrás del corte de los ítems del piso (`0x6374fe`) | #213 |
| T13 el segundo `F3 03`, T14 el `F1 02` con 1 | hecha | #208, #211 |
| T15 el barrido del hito | **abierta** | — |

Además, después del informe:
- el pasto (#212) y las marcas del suelo (#215);
- el paso de los personajes `0x508f00`: `0x508ba0` (#214), el cuerpo `0x4fd5f0` (#217) y `0x505de0` con todos los personajes (#219);
- los objetos del mapa en el dibujo y el paso del juego, en los mapas 0 y 2 (#218);
- el dibujo del juego después de los objetos: el `0x69` de Lorencia, los efectos del suelo, los pájaros y los personajes `0x510d00` (#221);
- el manejador de caídas `MuError.dmp`, de la CL3 (#216).

**Hasta dónde llega hoy.** Un error en el dibujo o en el paso termina la app (`IScene::Failure`). Todo lo que MuRet no porta corta con `Core::Expected` y la dirección. Dónde corta cada mapa lo dicen el párrafo "Hasta dónde llega MuRet" de [06](../../../Client/docs/research/06-render-escenas-y-ui.md#el-dibujo-del-juego) y las filas P-C41, P-C53 y siguientes de [08](../../../Client/docs/research/08-pendientes.md). Al momento del traspaso:
- **Lorencia y Devias:** el dibujo llega a los ítems del piso (`0x6374fe`). Corta antes con un efecto del suelo vivo (`0x52dc3b`; después de la entrada están los dos `0x61f` de llegada), con un personaje vivo y visto (`0x510ea1`; el héroe después de un paso) y, en Lorencia, cuando el `rand() % 2` del `0x69` sale par (`0x6030c7`, la primera gota).
- **Los demás mapas:** cortan en los objetos (`0x6374ce`).

La selección y el login todavía no corren el paso de los personajes (P-C50, P-C54 a).

## Cómo se trabajó

- **Fidelidad máxima, incluido el flujo de `rand()`.** Cada `rand()` del original se cuenta y se saca en el mismo orden. Donde algo no está portado, el código corta con error y la dirección **después** de los `rand()` que el original saca antes. Nunca se saltea en silencio.
- **El research primero.** Antes de portar, se lee estático el ASM, con direcciones, y se escribe en [`Client/docs/research/`](../../../Client/docs/research/README.md): 06 para escenas y render, 07 para las rarezas y 08 para lo pendiente. Las etiquetas de evidencia son las del README:
  - *estático*: leído del ASM;
  - *inferido*: deducido sin verlo;
  - *emulado*: solo con el `main.exe` real corrido en unicorn;
  - *simulado*: un cálculo con el port de MuRet.

  Una inferencia no se escribe como hecho. El binario es el `c32bbd5f…`; dónde está, y su decompilación, lo dice el README del research.
- **Los números.** Las rarezas van en 07 como Q-C y lo pendiente en 08 como P-C, en orden numérico.
  - Q-C usados hasta el 266 y del 276 al 278; P-C, hasta el 55.
  - Libres: 198, 199, 218 a 220, 234, 235, 239, 240, 249 a 255, 258 a 265, 267 a 275 y 279 a 285.
  - Q-C286 a 295 y P-C56: reservados para el paso del login.
  - Lo nuevo, desde Q-C296 y P-C57.
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

1. **El dibujo del juego, desde P-C55.** Con #221 el dibujo de Lorencia y Devias llega a los ítems del piso (`0x6374fe`). Lo que falta está en P-C55; en este orden:
   - (b) primero: el `+0x16c` de los efectos con modelo (`0x52d620`, su esfera `0x5fbf00`) y los casos de `0x52dc20`. Desbloquea T9;
   - (d) `0x50ba30` en el juego, desde `0x50caaa`: la luz, la rama del héroe y el tercer argumento. Con eso el héroe se dibuja después de un paso;
   - (a) las gotas del `0x69`, con las partículas del juego (`0x743a198`, su paso `0x637128` y su dibujo `0x637558`);
   - (c) los pájaros y los peces (`0x446120` y `0x447030` en el paso, y su dibujo);
   - (f) lo que sigue en la tabla de "El dibujo del juego" de 06: los ítems del piso (`0x608a00`, CL9.3), los peces, el caballo (`DrawHelpers`, ya portado), las hojas, los joints, los efectos con modelo, las cintas, los sprites, las partículas y los números de daño.

2. **El paso de los personajes en el login (`0x634d13`).** Está escrito en [`tarea-paso-personajes-login.md`](2026-11-09-cl7/tarea-paso-personajes-login.md). Un agente anterior se frenó porque un control de seguridad automático le bloqueó la respuesta mientras desensamblaba `0x6349a0`. Si te pasa algo así, frená y avisale a Emanuel.
3. **El paso de los personajes en la selección (`0x6342f9`).** Depende del 2:
   - los lugares de la lista armados como `0x20a`;
   - la vista previa `0xa9` (`0x634303`);
   - Item.bmd antes de la primera entrada.

   Ver P-C50 y P-C54 (a).
4. **T9, el frame de cero pasos.** Ver el informe del hito 1, T9, P-C42 y P-C55 (b): después de la entrada corta en `0x52dc3b` por los dos `0x61f` de llegada.
5. **T15, el barrido del hito 1.** Resoluciones 640 × 480, 1024 × 768 y ancha; archivos que faltan; frames 0, 1 y varios.
6. **Hito 2, caminar, gates, warps y la vuelta.** Ver el [informe](2026-11-09-cl7/hito2-caminar-y-gates.md), P-C30 y P-C31.
7. **Hitos 3 y 4, el mundo vivo:**
   - los objetos de los demás mapas y los casos de `0x602ed0` y `0x604ab0` que faltan (P-C53);
   - la luz dinámica;
   - los pájaros y los peces;
   - la lluvia y las hojas;
   - los efectos.

   Ver el [informe](2026-11-09-cl7/hito3-4-mundo-vivo.md); las filas H3.x y H4.x son un PR cada una.
8. **Hito 5, la matriz de todos los mapas.** Ver el [informe](2026-11-09-cl7/hito5-matriz-mapas.md).
9. **El roadmap tiene que cubrir todo el `main.exe` decompilado** (Emanuel, 2026-11-10). Hay unos 821 KB de código de juego, el 36 %, sin ola asignada. Falta un PR de docs que sume:
   - una **ola de efectos** al roadmap del cliente;
   - una **tarea de mapeo** que asigne cada función sin nombre a una ola.

   QA se estima aparte.
10. **Lo que solo cierra la VM.** Las observaciones nativas de cada P-C de 08: el estado de `rand()` en la entrada, el primer frame, la espera, los presupuestos de los pasos y un `MuError.dmp` real. Las corre Emanuel en la VM XP, con el runbook de #196 y sin versionar los volcados.

El PR #185 es un borrador de referencia de la CL7 vieja: no se mergea. Cerrarlo es decisión de Emanuel.
