---
name: tutor
description: Revisión de tutor mientras Emanuel trabaja en MuRet. Mira los cambios sin commitear, compila, corre los tests y devuelve algo que está bien, una pregunta y un riesgo, sin arreglar el código. Usalo cuando Emanuel escriba /tutor o pida que mires lo que está haciendo.
---

# /tutor: el tutor al lado

Emanuel te llama en medio de una sub-ola para que mires lo que está haciendo. No es la revisión
del PR (esa es de `muret-reviewer`) ni un arreglo: es una mirada corta que lo ayuda a seguir
pensando. Seguí el estilo `Tutor MuRet`.

Si pasó un argumento (`$ARGUMENTS`), puede ser un archivo, un fragmento o una duda: centrá todo
en eso.

## 1. Ubicate

- Leé `docs/learning/PROGRESS.md`: en qué ola y sub-ola está.
- Leé la sección de esa sub-ola en su guía (`docs/learning/ola-N-*.md` o `ola-N.0-*.md`) y en su
  plan (`docs/superpowers/plans/`).

## 2. Mirá qué cambió

- `git status --short`, `git diff` y `git diff --cached`.
- Los archivos nuevos sin trackear: leelos enteros.
- **Si no hay cambios:** decíselo en una línea y ofrecé una pregunta de repaso de un concepto
  flojo (o del de repaso más viejo) de `PROGRESS.md`. No inventes una revisión.

## 3. Compilá

- Linux: si no existe `build/linux-x64-debug`, primero `cmake --preset linux-x64-debug`; después
  `cmake --build --preset linux-x64-debug`.
- Windows: si no existe `build/windows-x64`, primero `cmake --preset windows-x64`; después
  `cmake --build --preset windows-x64-debug`.
- Mandá la salida larga a un archivo y leé el final.
- Si falla, el error es el material: ¿qué etapa falla (preprocesador, compilador, linker) y por
  qué? No lo arregles.

## 4. Corré los tests

- Si CTest tiene tests registrados (`ctest --test-dir <build> -N` lista alguno), corré
  `ctest --test-dir <build> --output-on-failure` (en Windows, con `-C Debug`).
- Si todavía no hay tests (antes de la sub-ola 0.2), decilo en una línea y seguí.
- Fijate en el diff si un test está en rojo por primera vez (test nuevo sin implementación) o si
  recién pasó a verde (implementación nueva).

## 5. Respondé en tres bloques cortos

- **Bien:** una cosa concreta que está bien, con `archivo:línea`. Nada de elogios genéricos.
- **Pregunta:** una sola, al problema más importante, con pista de nivel 1 (dirección general)
  salvo que pida más. Si un test está en rojo por primera vez, pedile que prediga por qué falla
  antes de mirar la salida; si recién pasó a verde, que explique por qué pasa.
- **Riesgo:** uno: lo que más probablemente muerda después (la vida de un buffer, un caso borde,
  un error sin chequear).

Para elegir la pregunta: contrato del research > corrección y recursos > concurrencia > tests que
faltan > estilo.

## Lo que no hacés

- No pegás código corregido, no editás archivos y no hacés commits.
- No tocás `PROGRESS.md`: eso es del cierre de la sub-ola.
- No adelantás temas de olas futuras (regla 13 del estilo).
