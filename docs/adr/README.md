# Decisiones de arquitectura (ADR)

Un ADR registra una decisión global: qué hacía el original, qué hace MuRet y por qué, con el test
que la sostiene ([`CODING_RULES.md` § 11](../CODING_RULES.md#11-documentar-el-uso-no-las-tripas) y
[`AGENTS.md`](../../AGENTS.md)). Sirve para reconocer una diferencia aceptada cuando ya no está
quien la decidió.

Este archivo es el índice. Hoy no hay ningún ADR escrito: la tabla de abajo es el inventario de los
que faltan (AUD-C04 de la [auditoría del 2026-11-03](../audits/2026-11-03-cliente-cpp20.md)). La
evidencia ya existe en el research y en los planes, y acá se enlaza sin copiarla. AUD-C04 se cierra
cuando los ADR exigidos existan: este índice no los reemplaza.

## Cómo se escribe uno

- **Archivo:** `NNNN-tema-corto.md`, con el número siguiente libre. Un concepto por ADR y por PR.
- **Secciones:** Contexto (qué hace el original, con su dirección y su fila: Q-C en el 07 del
  cliente, Q-nn en el 08 del DataServer), Decisión (qué hace MuRet y por qué), Consecuencias (qué se
  gana, qué se pierde, qué test lo fija) y Estado (propuesto, aceptado o reemplazado por otro ADR).
- **Al escribirlo:** sumar su enlace en la tabla, y en esa fila del 07 o del 08.

## Números reservados

| ADR | Tema | Quién | Estado |
| --- | --- | --- | --- |
| 0001 | El contrato wire y SQL del servidor se respeta al byte | Emanuel, ola 0.3 ([guía](../learning/ola-0-fundaciones.md#03-documentación-base) y [plan de la ola 0](../superpowers/plans/2026-10-26-ola-0-fundaciones.md), paso 2) | pendiente |
| 0002 | IOCP y epoll a mano, Asio como comparación opcional | ola 0.3 ([plan de la ola 0](../superpowers/plans/2026-10-26-ola-0-fundaciones.md), paso 3); el plan de la auditoría la deja a Emanuel | pendiente |

Son del servidor y son ejercicios del camino de aprendizaje: no se reasignan ni se dan por
resueltos. Los del cliente empiezan en el 0003.

## Inventario del cliente

Las decisiones que la auditoría encontró documentadas en el research y en los planes, pero sin su
ADR. Todas tienen la fila en el [07 del cliente](../../Client/docs/research/07-quirks-y-bugs.md) y
tests que las fijan.

| Decisión | Research | Tests | Quién | Estado |
| --- | --- | --- | --- | --- |
| El serial de recepción se resincroniza, y el C3 rearmado declara lo que ocupa | Q-C04 y Q-C10 | `PacketSerialsTest.ResyncsWithTheOneThatCame`, `EncryptedPacketTest.TheRebuiltPacketDeclaresWhatItMeasures` | Claude | sin ADR |
| JPEG con libjpeg-turbo y no con la 6b del original (lo decidió Emanuel), con los casos donde difieren | Q-C33, la [pregunta 1 del roadmap](../superpowers/specs/2026-10-30-cliente-design.md#preguntas-abiertas-se-deciden-al-arrancar-la-ola-que-las-necesita), la [CL1.4 del plan de la CL1](../superpowers/plans/2026-11-01-ola-cl1-formatos-sin-ventana.md#sub-ola-cl14-texturas-ozt-y-ozj) y la [regla 10](../CODING_RULES.md#10-pensar-antes-de-agregar) | `JpegImageTest.DecodesLikeTheLibjpeg6b`, `WhatThe6bCannotReadIsInvalidData`, `GoldenJpegImage.DecodesEveryOzjOfTheLabLikeTheLibjpeg6b` (con la carpeta de datos) y los vectores de [`Client/tests/vectors/jpeg`](../../Client/tests/vectors/jpeg) | Claude | sin ADR |
| El ritmo: lógica a 25 pasos por segundo, dibujo desacoplado por defecto con un tope de 1 s por frame, y el modo fiel, sin tope, como opción | Q-C44 y D-C4 del [roadmap](../superpowers/specs/2026-10-30-cliente-design.md) | `ConfigTest.DefaultsWhenAKeyIsMissing`, `GameLoopTest.AFrameLongerThanTheCapRunsOnlyTheCap`, `FixedStepTest.TheCapIsOneSecond`, `FaithfulPacerTest.HasNoCap` | Claude | sin ADR |
| El log: agrega, se vacía al pasar 1 MiB, en claro, y avisa si deja de poder escribirse | Q-C48, AUD-C01 y AUD-C02 | `FileSinkTest.AppendsFromIndependentSinksAfterTheCap`, `EmptiesAFileOverTheCap`, `ReportsAWriteFailureOnce` | Claude | sin ADR |
| La sesión: el keepalive desde el login aceptado y las esperas con tope de 30 s | Q-C53 y Q-C56 | `ServerSessionTest.NoKeepaliveBeforeTheLogin`, `KeepaliveEveryTwentySecondsAndNotBefore`, `EachWaitCountsFromItsStart` | Claude | sin ADR |
| El wrap `GL_CLAMP`, que el perfil core no tiene, como `GL_CLAMP_TO_EDGE` | Q-C73 | `GlTextureTest.RepeatWrapsAndClampDoesNot` | Claude | sin ADR |

"Quién" sigue la decisión del 2026-11-02: Claude escribe hasta que Emanuel diga que participa, y
marca lo que resolvió. Si Emanuel toma uno, lo cambia acá.

## Las otras filas de diseño

Las otras filas de clase *diseño* del 07 son Q-C13, Q-C34, Q-C46, Q-C47, Q-C49, Q-C50, Q-C54,
Q-C55, Q-C62, Q-C69, Q-C70, Q-C71, Q-C72, Q-C76 y Q-C79. Cada una dice qué hace MuRet, y casi todas
son una diferencia elegida con su test en la fila. Las excepciones: Q-C69 no se aparta, reproduce
el original. Q-C13, Q-C34 y Q-C70 no citan un test, y Q-C49 no lo tiene porque CI no puede
arrastrar una ventana. Q-C47 y Q-C70 dejan una parte para la CL5 y la CL9. Queda por decidir cuáles
merecen un ADR propio y cuáles alcanzan con la fila de 07; hasta entonces, la fila es el registro.
