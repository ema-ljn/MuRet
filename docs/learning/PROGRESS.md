# Progreso de aprendizaje

Lo leen y actualizan el estilo `Tutor MuRet` y el subagente `muret-reviewer`. El comando `/tutor`
solo lo lee.

## Dónde estamos

- Ola: 0 (Fundaciones)
- Sub-ola: **0.0 en curso** (del código al programa), arrancada el 2026-11-01. Ya están la
  [guía](ola-0.0-del-codigo-al-programa.md) y `labs/` con el test de la misión, que todavía no
  compila: te toca la misión (`Labs::HeadOffset`), el jefe y el carril R
- La 0.1 está hecha (andamiaje CMake + `Server/DataServer/`); falta compilar en Windows y responder [las preguntas](ola-0.1-cmake.md#te-toca), que se cierran junto con la 0.0
- Próxima del servidor: la 0.2, ahora más chica: vcpkg, GoogleTest, los test presets y el CI ya
  llegaron con la CL0.2 y la CL0.3 del cliente; le quedan los tests del DataServer
- **Cliente (tu foco): CL0.2 y CL0.3 resueltas como ejemplo trabajado** el 2026-11-01, después de
  que eligieras ir directo al cliente: `ClientLib` con la resolución de rutas de `Data\`,
  `ClientTests` con la carpeta golden, la ventana SDL3 con OpenGL 3.3 core y el CI en Windows y
  Linux. Para cerrarla: las [preguntas de la CL0](../superpowers/plans/2026-10-30-ola-cl0-fundaciones-cliente.md#para-cerrar-la-cl0-emanuel)
  y correr `Main.exe` en tu Windows. Conceptos para explicar: targets y qué se linkea a quién,
  vcpkg en modo manifiesto y su baseline, loader de GL, tests salteables. Próxima: la CL1
  (formatos sin ventana), con su [plan](../superpowers/plans/2026-11-01-ola-cl1-formatos-sin-ventana.md)
  y la [guía de la CL1.0](ola-cl1.0-bytes-y-archivos.md). Los errores van con un `Expected` propio
  que escribís vos (decidido el 2026-11-01)
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

(vacío)

## Preguntas para olas futuras

- Cliente (2026-10-30): nació `Client/` con su [research](../../Client/docs/research/README.md) y su [roadmap](../superpowers/specs/2026-10-30-cliente-design.md) (olas CL0 a CL10, SDL3 + OpenGL 3.3, tick fijo y FPS desacoplados sin perder fidelidad). ~~¿Cuándo arranca la CL1 respecto de la ola 1 del servidor?~~ Decidido el 2026-11-01: el cliente va directo (pregunta 3 del roadmap del cliente)
