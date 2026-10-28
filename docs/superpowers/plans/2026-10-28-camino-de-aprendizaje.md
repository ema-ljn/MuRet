# Camino de aprendizaje: plan de implementación

> **Para agentes:** SUB-SKILL REQUERIDO: usá superpowers:subagent-driven-development (recomendado)
> o superpowers:executing-plans para ejecutar este plan tarea por tarea. Los pasos usan checkbox
> (`- [ ]`) para el seguimiento.

**Objetivo:** dejar el repo listo para el camino de aprendizaje aprobado: roadmap, front-ends,
tutor, `/tutor`, revisor, cuaderno de aprendizaje y puertas de entrada apuntando a la spec nueva.

**Arquitectura:** solo documentación y configuración de Claude Code. Cada tarea toca un grupo de
archivos que cambian juntos, se verifica con `grep` y con el chequeo de links, y termina en un
commit en notación Angular. Al final, un PR a `main` con la descripción completa.

**Stack:** Markdown, Claude Code (output style, skill, subagente), git.

**Spec:** [`docs/superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md`](../specs/2026-10-28-camino-de-aprendizaje-design.md)
(en adelante, "la spec"). Leela entera antes de la primera tarea.

## Restricciones globales

- Todo en español rioplatense, con el tono y el formato de los docs existentes: tablas, links
  relativos, frases cortas.
- Rama `claude/relaxed-einstein-p8fn63`. Commits en notación Angular
  (`tipo(alcance): descripción`, en castellano, en minúscula y sin punto final), con cuerpo que
  explique el porqué y el pie de atribución de la sesión. Ningún identificador de modelo en
  commits, PR ni archivos.
- **No** se crean `labs/`, guías N.0 (`docs/learning/ola-N.0-*.md`) ni código del servidor (spec,
  "Cambios al repo" y L-10).
- Las reglas nuevas del tutor explican desde ciencias de la computación y **sin analogías web**
  (L-2).
- El carril R no incluye romper protecciones (L-9).

**Chequeo de links** (se usa en varias tareas; no imprime nada si todo está bien):

```bash
check_links() { for f in "$@"; do d=$(dirname "$f"); grep -oE '\]\(([^)#]+)' "$f" | sed 's/](//' \
  | grep -v '^http' | while read -r p; do [ -e "$d/$p" ] || echo "FALTA $f -> $p"; done; done; }
```

## Foco de revisión

1. **Referencias viejas a la "ola 9" de front-ends** en cualquier archivo del repo: después del
   cambio no tiene que quedar ninguna (tarea 1, paso 3).
2. **`/tutor` antes de la 0.2**, cuando todavía no hay tests ni directorio de build: tiene que
   configurar si hace falta y decir que no hay tests, sin fallar (tarea 4, paso 2).
3. **`/tutor` en Windows:** los presets se llaman distinto que en Linux (tarea 4, paso 2).
4. **`/tutor` sin cambios pendientes:** no puede inventar una revisión; ofrece una pregunta de
   repaso (tarea 4, paso 2).
5. **Reglas del tutor que se contradicen:** el ritmo nuevo de idea y actividad no puede romper "una
   pregunta por vez" ni "no adelantes temas" (tarea 3, paso 3).

---

### Tarea 1: front-ends, la ola 9 pasa a ola F

**Archivos:**
- Modificar: `docs/superpowers/specs/2026-10-26-frontends-cli-gui-futuro.md` (líneas 143, 307,
  312–317, 328 y el bloque inicial)

**Interfaces:**
- Produce: el nombre "ola F" y las sub-olas `F.1` a `F.6`, que citan las tareas 2 y 6.

- [ ] **Paso 1: renombrar.** "ola 9" → "ola F"; `9.1`…`9.6` → `F.1`…`F.6` (tabla de la sección
  "Cómo encajaría en olas", la línea 143 "a decidir en la 9.1" y el `(9.5)` de la estructura del
  repo). En el bloque inicial, una frase: es la ola F del
  [camino de aprendizaje](../specs/2026-10-28-camino-de-aprendizaje-design.md) (L-12), opcional y sin lugar
  fijo después de la ola 8. Sin cambios de contenido.
- [ ] **Paso 2: verificar el archivo.**
  Run: `grep -nE 'ola 9|\b9\.[1-6]\b' docs/superpowers/specs/2026-10-26-frontends-cli-gui-futuro.md`
  Expected: sin salida.
  Run: `grep -cE '\bF\.[1-6]\b' docs/superpowers/specs/2026-10-26-frontends-cli-gui-futuro.md`
  Expected: 8 o más (seis filas, la línea 143 y la estructura del repo).
- [ ] **Paso 3: verificar el repo entero (foco 1).**
  Run: `grep -rniE 'ola 9 opcional|"ola 9"|ola 9\b.*front' --include=*.md . | grep -v '2026-10-28-camino'`
  Expected: solo la línea 243 del roadmap, que arregla la tarea 2. Si aparece otra, se arregla acá.
- [ ] **Paso 4: commit.** `docs(frontends): renombrar la ola 9 a ola F`, con un cuerpo que diga
  que los números 9 en adelante quedan para distribuidos (L-12).

### Tarea 2: roadmap

**Archivos:**
- Modificar: `docs/superpowers/specs/2026-10-25-dataserver-port-design.md`

**Interfaces:**
- Consume: "ola F" (tarea 1).
- Produce: la tabla de olas completa (0 a 14 y F), que citan AGENTS y el README de learning.

- [ ] **Paso 1: bloque inicial.** Sumar una línea: **cómo se aprende** (las N.0, el formato, el
  tutor, las olas 9 a 14 y el carril R) está en el
  [camino de aprendizaje](../specs/2026-10-28-camino-de-aprendizaje-design.md).
- [ ] **Paso 2: "Cómo trabajamos".** Antes del paso 1 del ritmo: cada ola arranca con su sub-ola
  N.0 de fundamentos y, en los servidores nuevos, con su N.R de research.
- [ ] **Paso 3: "Scope".** El JoinServer y el ConnectServer dejan de estar "fuera": entran en las
  olas 9 y 10; el GameServer sigue afuera.
- [ ] **Paso 4: "Plan por olas".** En la fila de la ola 0, `0.0 del código al programa` va primero.
  Debajo de la tabla, una frase: las olas 1 a 8 arrancan con su N.0 (contenido en la spec,
  sección "Contenido de las N.0"). Después, una tabla nueva "Olas de distribuidos (después de la
  8)" con las mismas columnas (Ola, Tema, Sub-olas, Conceptos, Research), una fila por ola 9 a 14
  y la F, con los temas y conceptos de la spec. En "Research", las carpetas `joinserver/` y
  `connectserver/` del repo de research para 9 y 10.
- [ ] **Paso 5: "Trabajo futuro".** Reemplazar el borrador de "ola 9" por: las olas 9 a 14 y la F
  están en el camino de aprendizaje; los front-ends son la ola F (link a la spec de front-ends).
- [ ] **Paso 6: verificar.**
  Run: `grep -n 'ola 9 opcional' docs/superpowers/specs/2026-10-25-dataserver-port-design.md`
  Expected: sin salida.
  Run: `grep -c '2026-10-28-camino-de-aprendizaje-design.md' docs/superpowers/specs/2026-10-25-dataserver-port-design.md`
  Expected: 2 o más.
  Run: `check_links docs/superpowers/specs/2026-10-25-dataserver-port-design.md`
  Expected: sin salida.
- [ ] **Paso 7: commit.** `docs(roadmap): sumar las N.0 y las olas 9 a 14 del camino de aprendizaje`.

### Tarea 3: reglas del estilo `Tutor MuRet`

**Archivos:**
- Modificar: `.claude/output-styles/tutor-muret.md`

**Interfaces:**
- Consume: el nombre del comando `/tutor` (tarea 4).
- Produce: las reglas 14 a 20 y la sección de contexto, que cita el revisor (tarea 5).

- [ ] **Paso 1: "Rol" y "Contexto".** En el rol, sumar MU (incluido su binario) a lo que se
  aprende. En el contexto, sumar la spec del camino de aprendizaje (primera lectura, junto al
  roadmap), las guías N.0 cuando existan, `labs/` cuando exista y el repo de research para los
  servidores nuevos (`joinserver/`, `connectserver/`, `exdb/`).
- [ ] **Paso 2: sección nueva "Cómo se enseña cada N.0"**, después de la regla 13, con estas reglas
  numeradas a continuación:
  - **14. Desde ciencias de la computación:** cada concepto se explica por lo que es formalmente,
    con su referencia de texto. Sin analogías web.
  - **15. Idea y actividad:** nunca más de diez líneas de teoría sin una actividad. El menú de
    actividades es el de la spec (predecí y comprobá, detective de bytes, cazá el bug del original,
    rompelo a propósito, mirá la memoria, mirá el assembly, laboratorio visual, juego), siempre
    con material de MU.
  - **16. Calentamiento, misión y jefe:** toda N.0 los tiene; la misión empieza por el test que
    falla y se escribe en `labs/`; el jefe es un quirk del original o un caso borde con su test.
  - **17. Construir, comparar, usar el estándar** (L-3), con su excepción.
  - **18. Laboratorios visuales:** muestran el fenómeno, no la solución; primero se predice.
  - **19. Puntos de control:** los cinco de la spec (antes de empezar, primer rojo, verde, PR,
    cierre) y el comando `/tutor` a pedido.
  - **20. Carril R:** protocolo y comportamiento; nunca romper protecciones (L-9).
- [ ] **Paso 3: coherencia (foco 5).** En la regla 15, aclarar que cada actividad termina en **una**
  pregunta (regla 3). En "Cierre de sesión", sumar: actualizar el nivel del concepto en
  `PROGRESS.md` (usar, explicar, romper, transferir). Al principio de "Contexto", sumar: cada
  sesión arranca con dos o tres preguntas de repaso de olas **anteriores** (no futuras, regla 13).
- [ ] **Paso 4: verificar.**
  Run: `head -5 .claude/output-styles/tutor-muret.md`
  Expected: el frontmatter intacto (`name: Tutor MuRet`, `keep-coding-instructions: true`).
  Run: `grep -cE 'analogías web|Calentamiento|/tutor|Carril R|repaso' .claude/output-styles/tutor-muret.md`
  Expected: 5 o más.
- [ ] **Paso 5: commit.** `feat(tutor): sumar las reglas del camino de aprendizaje al estilo`.

### Tarea 4: el comando `/tutor`

**Archivos:**
- Crear: `.claude/skills/tutor/SKILL.md`

**Interfaces:**
- Produce: el skill `tutor` (se invoca con `/tutor`, con un argumento opcional: archivo o pregunta).

- [ ] **Paso 1: frontmatter.** `name: tutor`; `description`: revisión de tutor mientras Emanuel
  trabaja; mira los cambios sin commitear, compila, corre los tests y devuelve algo bien, una
  pregunta y un riesgo; se usa cuando escribe `/tutor` o pide que mires lo que está haciendo.
- [ ] **Paso 2: cuerpo, en este orden.**
  1. Leer `docs/learning/PROGRESS.md` para saber la sub-ola, y su guía y su plan.
  2. `git status --short` y `git diff` (más el contenido de los archivos nuevos sin trackear). Con
     argumento, centrarse en ese archivo o esa duda. **Sin cambios (foco 4):** decirlo y ofrecer
     una pregunta de repaso de un concepto flojo de `PROGRESS.md`.
  3. Compilar con el preset del sistema: `linux-x64-debug` en Linux, `windows-x64` +
     `windows-x64-debug` en Windows (foco 3). Si no existe el directorio de build, configurar
     primero (foco 2).
  4. Tests: si hay tests registrados en CTest, `ctest --test-dir <build> --output-on-failure`; si
     no los hay (antes de la 0.2), decirlo y seguir (foco 2).
  5. Respuesta en tres bloques, cortos: **Bien** (algo concreto, `archivo:línea`), **Pregunta**
     (una sola, al problema más importante, pista de nivel 1 salvo que pida más) y **Riesgo**
     (uno). Si es el primer rojo de un test, la pregunta pide predecir por qué falla; si recién
     pasó a verde, pide explicar por qué.
  6. Nunca pegar código corregido ni editar archivos: es una revisión, no un arreglo (reglas 1, 4
     y 7 del estilo). No toca `PROGRESS.md`.
- [ ] **Paso 3: verificar.**
  Run: `head -4 .claude/skills/tutor/SKILL.md`
  Expected: `---`, `name: tutor`, la línea `description:` y `---`.
  Run: `grep -nE 'linux-x64-debug|windows-x64-debug|--output-on-failure|repaso' .claude/skills/tutor/SKILL.md`
  Expected: las cuatro aparecen.
- [ ] **Paso 4: commit.** `feat(tutor): agregar el comando /tutor`.
- [ ] **Paso 5: probarlo en seco.** Con el árbol limpio (después del commit del paso 4), seguir los
  pasos del skill a mano en Linux con el directorio de build borrado: el build configura y compila,
  CTest no tiene tests y el skill tiene que decir "sin cambios" y ofrecer repaso. Anotar el
  resultado para el PR.

### Tarea 5: el revisor `muret-reviewer`

**Archivos:**
- Modificar: `.claude/agents/muret-reviewer.md`

**Interfaces:**
- Consume: las reglas 14 a 20 del estilo (tarea 3).

- [ ] **Paso 1: "Antes de revisar leé".** Sumar la spec del camino de aprendizaje y la guía N.0 de
  la ola, si existe.
- [ ] **Paso 2: qué más revisa**, como puntos nuevos de la lista "Revisá en este orden", sin
  cambiar los cinco que ya están:
  - **`labs/`:** el test de la misión existe y falla sin la implementación; la comparación con el
    estándar está anotada; el test del jefe existe; compila en Windows y Linux; el servidor no
    enlaza `MuRetLabs`.
  - **`tools/`:** misma vara que el código del servidor (CODING_RULES).
  - **Research escrito por Emanuel** (`Server/<Servidor>/docs/research/`): cada afirmación con su
    nivel (Verificado / *inferido* / **Análisis** / *seam*) y su evidencia (dirección o captura);
    contradicciones registradas como en `09-discrepancias`; nada de romper protecciones.
- [ ] **Paso 3: formato de salida.** Al sugerir cambios para `PROGRESS.md`, sugerir también el nivel
  alcanzado (usar, explicar, romper, transferir).
- [ ] **Paso 4: verificar.**
  Run: `grep -cE 'labs/|tools/|Verificado|transferir|camino-de-aprendizaje' .claude/agents/muret-reviewer.md`
  Expected: 5 o más.
  Run: `head -5 .claude/agents/muret-reviewer.md`
  Expected: el frontmatter intacto (`name: muret-reviewer`, `tools: Read, Grep, Glob, Bash`).
- [ ] **Paso 5: commit.** `feat(reviewer): revisar labs, tools y research del alumno`.

### Tarea 6: el cuaderno de aprendizaje

**Archivos:**
- Modificar: `docs/learning/PROGRESS.md`
- Modificar: `docs/learning/README.md`

**Interfaces:**
- Consume: los niveles y el repaso de la spec (sección "Seguimiento").

- [ ] **Paso 1: `PROGRESS.md`.** "Dónde estamos": ola 0; próxima la **0.0** (del código al
  programa), después cerrar la 0.1 (compilar en Windows y sus preguntas) y la 0.2. Reemplazar
  "Conceptos dominados" y "Conceptos flojos" por una tabla `Concepto | Ola | Nivel | Último repaso`
  vacía, con una línea que explique los cuatro niveles y la regla de "dominado" (dos sesiones
  distintas). Mantener "Preguntas para olas futuras".
- [ ] **Paso 2: `README.md`.** En "Qué hay": una fila para las guías N.0 ("se escriben justo antes
  de cada ola") y una frase de que la ola 9 en adelante y la F están en la spec. Sección nueva "Cómo
  es una N.0": el ritmo de idea y actividad, calentamiento, misión y jefe, `labs/` y `/tutor`, en
  cinco líneas como mucho, con link a la spec. En "Dónde está cada cosa": sumar "cómo se aprende"
  con link a la spec.
- [ ] **Paso 3: verificar.**
  Run: `check_links docs/learning/PROGRESS.md docs/learning/README.md`
  Expected: sin salida.
  Run: `grep -n 'Último repaso' docs/learning/PROGRESS.md`
  Expected: una línea.
- [ ] **Paso 4: commit.** `docs(learning): seguimiento por niveles y cómo es una N.0`.

### Tarea 7: puertas de entrada (`AGENTS.md` y `CLAUDE.md`)

**Archivos:**
- Modificar: `AGENTS.md`
- Modificar: `CLAUDE.md`

- [ ] **Paso 1: `AGENTS.md`.**
  - En el primer párrafo, sumar MU a lo que se aprende.
  - En "Dónde está cada cosa", una fila: cómo se aprende (N.0, olas 9 a 14, carril R) → la spec. La
    fila del roadmap dice "olas 0 a 8".
  - En "Ramas y PRs", los commits pasan a notación Angular: `tipo(alcance): descripción` en
    castellano, con el alcance opcional (por ejemplo `docs(learning): …`).
  - En "Fuera de alcance", sumar: romper protecciones del binario original (L-9).
- [ ] **Paso 2: `CLAUDE.md`.** Una línea con la spec del camino de aprendizaje y una con `/tutor`.
- [ ] **Paso 3: verificar.**
  Run: `check_links AGENTS.md CLAUDE.md`
  Expected: sin salida.
  Run: `grep -c 'camino-de-aprendizaje' AGENTS.md CLAUDE.md`
  Expected: 1 o más en cada uno.
- [ ] **Paso 4: commit.** `docs: apuntar AGENTS y CLAUDE al camino de aprendizaje`.

### Tarea 8: verificación final y PR

- [ ] **Paso 1: links de todo lo que cambió.** En la misma shell donde se definió `check_links`:
  Run: `git fetch origin main && check_links $(git diff --name-only origin/main...HEAD -- '*.md')`
  Expected: sin salida.
- [ ] **Paso 2: nada fuera de alcance.**
  Run: `git diff --name-only origin/main...HEAD`
  Expected: solo la spec, este plan y los archivos de las tareas 1 a 7; nada en `labs/`, `Server/`
  ni `docs/learning/ola-*`.
- [ ] **Paso 3: commits.**
  Run: `git log --format=%s origin/main..HEAD`
  Expected: todos con forma `tipo(alcance): descripción` o `tipo: descripción`.
- [ ] **Paso 4: push.** `git push -u origin claude/relaxed-einstein-p8fn63`.
- [ ] **Paso 5: PR a `main`.** Título: `docs(learning): camino de aprendizaje de C++ moderno, MU y
  distribuidos`. El repo no tiene plantilla de PR; el cuerpo lleva estas secciones:
  - **Problema:** los cinco huecos de la spec, con la tabla del C++ que asumen las guías.
  - **Qué decide:** L-1 a L-12 en una línea cada una, con link a la spec.
  - **Qué cambia:** un renglón por archivo, en el orden de las tareas.
  - **Qué no cambia:** sin código del servidor, sin `labs/`, sin guías N.0; el contrato con el
    GameServer 1.02 no se toca.
  - **Cómo revisarlo:** leer primero la spec, después el estilo y `/tutor`; resultado de la prueba
    en seco de la tarea 4.
  - **Qué sigue:** la sub-ola 0.0 (su guía se escribe al arrancarla).
  - El pie de atribución de la sesión.
