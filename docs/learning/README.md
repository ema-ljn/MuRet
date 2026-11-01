# Aprendizaje

Esta carpeta es el cuaderno del proyecto. MuRet es un proyecto de aprendizaje: Emanuel escribe el
código y Claude hace de tutor. Acá queda lo que se estudia, lo que se mide y lo que se aprendió.

## Qué hay

| Archivo | Qué es | Quién lo escribe |
| --- | --- | --- |
| [PROGRESS.md](PROGRESS.md) | estado de cada concepto (dominado, flojo, pendiente) | se actualiza al cerrar cada ola |
| [ola-0.0-del-codigo-al-programa.md](ola-0.0-del-codigo-al-programa.md) | guía de la 0.0: unidades de traducción, símbolos, enlace, ODR; la misión de `labs/` | guía de Claude, cierre tuyo |
| [ola-0-fundaciones.md](ola-0-fundaciones.md) | guía de la ola 0: dependencias, tests, CI, ADR y formato | guía de Claude, cierre tuyo |
| [ola-0.1-cmake.md](ola-0.1-cmake.md) | nota de la sub-ola 0.1 (CMake, targets) | Claude la armó; las respuestas son tuyas |
| [ola-1-protocolo.md](ola-1-protocolo.md) | guía de la ola 1: bytes, framing, serialización | guía de Claude, cierre tuyo |
| [ola-2-sockets.md](ola-2-sockets.md) | guía de la ola 2: sockets, RAII, hilos, sesiones | guía de Claude, cierre tuyo |
| [ola-3-io-asincrono.md](ola-3-io-asincrono.md) | guía de la ola 3: IOCP, epoll, vida de los buffers | guía de Claude, cierre tuyo |
| [ola-4-concurrencia.md](ola-4-concurrencia.md) | guía de la ola 4: cola, hilo de lógica, carreras | guía de Claude, cierre tuyo |
| [ola-5-handlers.md](ola-5-handlers.md) | guía de la ola 5: interfaces, handlers, repositorios | guía de Claude, cierre tuyo |
| [ola-6-odbc.md](ola-6-odbc.md) | guía de la ola 6: ODBC, sentencias preparadas, SQL Server | guía de Claude, cierre tuyo |
| [ola-7-heads.md](ola-7-heads.md) | guía de la ola 7: el resto de los heads y los gremios | guía de Claude, cierre tuyo |
| [ola-8-distribuido.md](ola-8-distribuido.md) | guía de la ola 8: laboratorio, golden tests, fallas | guía de Claude, cierre tuyo |
| `ola-N.0-*.md` | guía de la N.0 de cada ola: fundamentos (teoría, C++, misión y jefe) | Claude, justo antes de cada ola |
| [referencias-emuladores-wow.md](referencias-emuladores-wow.md) | qué usan TrinityCore, AzerothCore y CMaNGOS, qué patrones mirar en cada ola y bibliotecas opcionales (Asio, spdlog, nanodbc) | Claude |

El **cliente** (`Client/`, olas CL0 a CL10) sigue el mismo formato: sus guías se escriben al
arrancar cada ola, a partir del [roadmap del cliente](../superpowers/specs/2026-10-30-cliente-design.md)
y de este `PROGRESS.md`. Las N.0 que ya se vieron en el servidor (bytes, sockets) se repasan, no se
repiten.

Las olas 9 a 14 (sistemas distribuidos) y la F (front-ends) todavía no tienen guía: están definidas
en el [camino de aprendizaje](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md).

Las notas de medición que piden los planes (`ola-2.3-medicion.md`, `ola-4.3-throughput.md`,
`ola-6.3-vm-sqlserver.md`, `ola-8.1-gs-real.md`) todavía no existen: las escribís vos cuando
hagas esa sub-ola, con tus números.

## Cómo se usa una guía

1. **Antes de la sub-ola**, leé su sección: la idea, el ejemplo y los errores típicos.
2. **Durante**, el tutor la explica en vivo y te hace las "preguntas para chequearte". Si alguna no
   te sale, es la señal para volver a leer, no para seguir.
3. **Al cerrar la ola**, completá la sección final de la guía ("Al cerrar la ola"). Es lo que
   después pasa a [PROGRESS.md](PROGRESS.md).

Las guías **no** traen las respuestas a las decisiones de diseño: esas están como "preguntas para
decidir" en cada [plan](../superpowers/plans/) y las tomás vos al empezar la ola. Los ejemplos de
código son siempre **de otro dominio** (archivos `.wav`, un chat, una cola de impresión): muestran
el concepto sin resolver el ejercicio de MuRet.

## Cómo es una N.0

- **Ritmo:** una idea corta y una actividad (predecí, cazá el bug del original, rompelo, mirá la
  memoria o el assembly), y recién ahí la siguiente idea.
- **Tres niveles:** calentamiento (5 a 10 minutos), misión (en `labs/`, empezando por el test que
  falla) y jefe (un quirk del original o un caso borde, con su test).
- **Construís, comparás, usás el estándar:** lo escribís desde cero, lo comparás con el estándar y
  en el servidor usás el estándar.
- **El tutor al lado:** `/tutor` mira tus cambios sin commitear y te devuelve algo que está bien,
  una pregunta y un riesgo. El detalle, en el
  [camino de aprendizaje](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md#formato-de-una-sesión-de-n0).

## Dónde está cada cosa

- **Qué hace MuRet y en qué orden:** el
  [roadmap](../superpowers/specs/2026-10-25-dataserver-port-design.md).
- **Cómo se aprende** (las N.0, el formato, las olas 9 a 14 y el carril R): el
  [camino de aprendizaje](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md).
- **Qué hace el original, byte a byte:** el
  [research](../../Server/DataServer/docs/research/README.md).
- **Qué hay que hacer en cada ola, paso por paso:** los [planes](../superpowers/plans/).
- **Qué se estudia para poder hacerlo:** esta carpeta.
