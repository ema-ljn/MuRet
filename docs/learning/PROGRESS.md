# Progreso de aprendizaje

Lo leen y actualizan el estilo `Tutor MuRet` y el subagente `muret-reviewer`. El comando `/tutor`
solo lo lee.

## Dónde estamos

- Ola: 0 (Fundaciones)
- Sub-ola: **0.0 resuelta como ejemplo trabajado** (del código al programa), el 2026-11-01. A
  pedido tuyo la resolvió Claude: la misión (`Labs::HeadOffset` en `MuRetLabs`), el jefe (la ODR) y
  el carril R, con las salidas y las respuestas en [`labs/0.0-enlace/README.md`](../../labs/0.0-enlace/README.md).
  Para cerrarla falta lo tuyo: contar con tus palabras las preguntas de la
  [guía](ola-0.0-del-codigo-al-programa.md#preguntas-para-chequearte) y completar su sección final
- La 0.1 está hecha (andamiaje CMake + `Server/DataServer/`); falta compilar en Windows y responder [las preguntas](ola-0.1-cmake.md#te-toca), que se cierran junto con la 0.0
- Próxima: la 0.2 (vcpkg, GoogleTest, CTest, CI), que necesitan también la CL0.2 del cliente
- Documentación lista: el [roadmap](../superpowers/specs/2026-10-25-dataserver-port-design.md) (qué hace MuRet, las olas 0 a 8 y la tabla de las 9 a 14), el [camino de aprendizaje](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md) (cómo se aprende), el [research del original](../../Server/DataServer/docs/research/) (01..10, qué hace el binario byte a byte), los [planes por ola](../superpowers/plans/) y una [guía de estudio por ola](README.md)
- De la 0.3 ya están `docs/CODING_RULES.md`, `AGENTS.md` y `.clang-format`, adaptados de MuMain; quedan los ADR 0001 (tuyo) y 0002

## Conceptos

**Niveles:** 1 **usar** (lo escribiste con tests) · 2 **explicar** (lo contás sin mirar) ·
3 **romper** (predecís la falla y la provocás) · 4 **transferir** (lo aplicás en otro contexto).
Una sub-ola se cierra en el nivel 3; el 4 es ejercicio extra.

**Estado:** un concepto queda **dominado** cuando lo respondés bien en dos sesiones distintas. Si
la explicación tiene huecos, queda **flojo** y entra primero en el repaso del arranque de cada
sesión, junto con los de repaso más viejo.

| Concepto | Ola | Nivel | Estado | Último repaso |
| --- | --- | --- | --- | --- |
| unidad de traducción, archivo objeto y símbolos (`nm`) | 0.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| enlace externo e interno, `inline` y la ODR | 0.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| bibliotecas estáticas y dinámicas, imports del PE | 0.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |

## Preguntas para olas futuras

- Cliente (2026-10-30): nació `Client/` con su [research](../../Client/docs/research/README.md) y su [roadmap](../superpowers/specs/2026-10-30-cliente-design.md) (olas CL0 a CL10, SDL3 + OpenGL 3.3, tick fijo y FPS desacoplados sin perder fidelidad). ¿Cuándo arranca la CL1 respecto de la ola 1 del servidor? (pregunta 3 del roadmap del cliente)
