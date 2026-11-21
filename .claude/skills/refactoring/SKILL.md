---
name: refactoring
description: Usala cuando en MuRet haya que refactorizar, limpiar o simplificar código sin cambiar lo que hace (los PR de diseño del refactor del cliente, la deduplicación de AR6, un olor de código que traba un cambio). Es el ciclo de refactoring de Martin Fowler con su catálogo de olores y técnicas, con los cuidados de fidelidad del port.
license: MIT
metadata:
  upstream: https://github.com/cskwork/clean-code-skill/tree/acd750b18894b4bd03a0c929c2198336fce638d2
---

# Refactorizar en MuRet

Es la skill `clean-code` de [cskwork/clean-code-skill](https://github.com/cskwork/clean-code-skill)
(MIT), renombrada `refactoring` para no chocar con la skill `clean-code` de Robert C. Martin: el
método y el catálogo de *Refactoring* de Martin Fowler, ordenados como refactoring.guru. La skill
`clean-code` dice cómo se ve el código limpio; esta, cómo se llega sin cambiar el comportamiento.
El original está en `references/upstream-skill.md`.

## Quién manda

1. Lo que Emanuel pida en la conversación.
2. **La fidelidad al `main.exe` original** y el golden de comportamiento del refactor: si algo
   cambia, es un bug del refactor y no un golden para regenerar.
3. [`docs/CODING_RULES.md`](../../../docs/CODING_RULES.md) y las Global Constraints del
   [plan del refactor](../../../docs/superpowers/plans/2026-11-20-refactor-arquitectura-cliente.md).
4. Esta skill y, al final, `references/`.

## El ciclo

Para cada limpieza, sin saltear el paso 4 ni el 5:

1. **Detectar**: nombrar el problema concreto de lectura o de costo de cambio. El catálogo de
   olores está en [`references/code-smells.md`](references/code-smells.md).
2. **Elegir** la transformación más chica que sirva, de
   [`references/refactoring-techniques.md`](references/refactoring-techniques.md).
3. **Un paso chico** que no cambie el comportamiento.
4. **Verificar** con los tests de lo que se tocó después de cada tanda coherente: los tests del
   módulo y, si toca lógica portada, el golden de comportamiento (`FrameDigest`). Se agranda la
   verificación cuando la superficie lo pide; no se corre de nuevo lo que no cambió.
5. **Verificar que quedó más limpio**: leer el resultado. Si no se lee mejor que antes, se revierte.
   Cumplir una regla a costa de la lectura es una pérdida.

## Las reglas de juego

- **Solo transformaciones que conservan el comportamiento.** Si algo cambia lo que hace el
  programa, es un arreglo o una función nueva: otro commit y otra revisión. En MuRet, además, otro
  carril (el del port).
- **Los tests son la red.** Sin tests alrededor, primero un test de caracterización que fije lo que
  hace hoy, después el refactor.
- **Pasos chicos, siempre en verde**, y **un tema por commit**, con títulos `AR<épica>.<hito>`.
- **Seguir el código de alrededor** y **ser quirúrgico**: se toca lo que la tarea pide; el formato y
  los nombres del resto quedan como estaban.
- **Cohesión antes que número de líneas.** Los tamaños de CODING_RULES detectan olores, no son ley:
  se parte por responsabilidad.

## Los cuidados de fidelidad (propios de MuRet)

Algunas técnicas del catálogo conservan el comportamiento en Java y no en un port de x87:

| Técnica | El cuidado |
| --- | --- |
| *Extract Variable*, *Inline Temp*, *Replace Temp with Query* | En la matemática portada del original, un temporal de `float` redondea distinto que la expresión recalculada en precisión extendida. Se conserva el tipo de cada intermedio (`F32()`, `Ftol`) y la cantidad de veces que se evalúa. |
| *Replace Conditional with Polymorphism*, *Replace Type Code with Subclasses* | Un `switch` que transcribe el del original queda como `switch` marcado o pasa a una tabla con el mismo orden (AR2); no se reparte en clases. |
| *Substitute Algorithm* | Nunca sobre lógica portada, salvo que el golden pruebe que el resultado es idéntico bit a bit y que los `rand()` salen en el mismo orden. |
| *Consolidate Duplicate Conditional Fragments*, *Remove Control Flag* | Mover código de lugar puede cambiar el orden de efectos (sonidos, partículas, `rand()`): solo si el golden no cambia. |
| *Replace Error Code with Exception*, *Replace Exception with Test* | MuRet no tira excepciones: los errores vuelven como valores (`Core::Expected`). |
| *Extract Method*, *Move Method*, *Extract Class* | Son la base de los PR de diseño. La cita al original (`// 0x...`) viaja con el código que cita. |

## Una limpieza grande

El refactor del cliente sigue el flujo de upstream para limpiar un código entero, con los nombres
del plan:

1. **La red primero**: AR0 (el golden de comportamiento, el chequeo de capas, el rastreo).
2. **Por impacto**: primero los olores que traban cambios (*Divergent Change*, *Shotgun Surgery*,
   las clases dios como `WorldRuntime`), los cosméticos al final.
3. **Por unidades**: un grupo de módulos por hito (AR4), cada uno con su mudanza por script y su PR
   de diseño.
4. **Verificar cada unidad** y decir exactamente qué se verificó.
5. **El porqué en el commit**: el qué está en el diff.

## Las referencias

| Archivo | Para |
| --- | --- |
| [`references/code-smells.md`](references/code-smells.md) | nombrar el problema: 23 olores en 5 familias, con la técnica que trata cada uno |
| [`references/refactoring-techniques.md`](references/refactoring-techniques.md) | la técnica: más de 60, con antes y después de las frecuentes |
| [`references/clean-code-principles.md`](references/clean-code-principles.md) | los hábitos que evitan los olores (nombres, funciones, guardas, constantes, DRY, comentarios) |
| [`references/upstream-skill.md`](references/upstream-skill.md) | el `SKILL.md` original, en inglés |

Los ejemplos de `references/` son JavaScript y Python: explican la técnica, no se copian al repo.

## Origen y licencia

`references/` es una copia textual de `references/` y del `SKILL.md` (como
`references/upstream-skill.md`) en el commit `acd750b1` de cskwork/clean-code-skill, bajo la
licencia MIT de [`LICENSE`](LICENSE). Este `SKILL.md` es una adaptación. Para actualizar: traer los
archivos de un commit nuevo, cambiar el commit en el frontmatter y en esta sección, y revisar "Los
cuidados de fidelidad".
