# La action de la referencia del cliente: plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** una action manual que regenere la referencia del cliente sobre una rama y muestre cuánto falta para portar y
para cerrar el port contra el ASM, sin tocar el repo.

**Architecture:** `.github/workflows/referencia.yml` clona el research privado con una deploy key, corre los dos
generadores que ya existen hacia `$RUNNER_TEMP` y llama a un script nuevo que escribe el resumen en Markdown; las tablas
salen como artifact.

**Tech Stack:** GitHub Actions (ubuntu-24.04), Python 3 con la biblioteca estándar, `unittest`.

**Spec:** `docs/superpowers/specs/2026-11-20-accion-referencia-design.md`

## Global Constraints

- Secreto: `RESEARCH_DEPLOY_KEY` (la privada); la pública, deploy key de **solo lectura** en
  `EmaNLJN/mu-server-repack-research-102i`, rama `master`.
- La action no commitea ni abre PRs: el resumen va a `GITHUB_STEP_SUMMARY` y las tablas, a un artifact de 30 días.
- Vivo = fila de `funciones.tsv` con `clase` = `game` y `viva` = `sí` (la misma base que las cotas). Ola: columna `ola`.
- Cerrado contra el ASM = vivo y `estado` = `implemented`, `falso_positivo` ≠ `sí`, `cortes_vivos` vacío,
  `citas_pendiente` = 0, `citas_test` > 0, `pasos_vm` vacío e `ids_08_abiertas` vacío.
- Los scripts usan solo la biblioteca estándar y se corren con `python3 -I`; el texto del resumen, en español.
- `workflow_dispatch` solo se ve con el archivo en `main`: la action también corre en `pull_request` cuando cambian
  `.github/workflows/referencia.yml` o `reference_summary.py`, para probarla antes del merge (decisión del builder).

## Review Focus

- Una `metricas.tsv` commiteada sin alguna de las métricas (una referencia vieja): la diferencia muestra "—", no corta.
- Una ola sin funciones vivas (CL1 tiene 11): la tabla por ola la muestra con 0 y sin dividir por cero.
- `cierra_el_cliente` con texto de varias citas (`a:1; b:2`): se lista entero, una fila por corte de `cortes.tsv`.
- Un PR desde un fork no tiene el secreto: el paso del clon falla con un mensaje claro, no con el error de ssh.
- `huecos.tsv` vacío: la sección dice "ninguno" en vez de una tabla vacía.

---

### Task 1: el resumen (`reference_summary.py`)

**Files:**
- Create: `Client/docs/research/scripts/tools/reference_summary.py`
- Test: `Client/docs/research/scripts/tools/test_reference_summary.py`

**Interfaces:**
- Produces: `python3 -I reference_summary.py --nueva DIR --commiteada DIR` escribe el Markdown a stdout.
  Funciones: `load_tsv(path) -> list[dict]`, `closed_against_asm(row: dict) -> bool`,
  `closure_by_wave(funciones: list[dict]) -> dict[str, dict]` (clave `total` y cada ola: `funciones`, `kb`,
  `cerradas`, `kb_cerrados`, `sin_citar`, `con_cortes`, `cierran`, `sin_test`, `con_vm`, `con_08`),
  `metric_deltas(new: list[dict], old: list[dict]) -> list[tuple[str, str, str, str | None, str]]`
  (ámbito, clave, métrica, valor viejo o None, valor nuevo) y `render(...) -> str`.

- [ ] **Step 1: los tests**, con TSV inventados escritos en un `tempfile.TemporaryDirectory`:
  - `test_closed_needs_every_condition`: una fila que cumple todo da `True`; cambiar de a una `estado` a `partial`,
    `falso_positivo` a `sí`, `cortes_vivos` a `G1`, `citas_pendiente` a `1`, `citas_test` a `0`, `pasos_vm` y
    `ids_08_abiertas` a algo da `False` en cada caso; una fila con `viva` = `no` no cuenta en `closure_by_wave`.
  - `test_closure_by_wave_counts_kb_and_blockers`: tres vivas de CL8 (1.024, 2.048 y 512 bytes; una cerrada, una
    `unknown`, una `partial` con `cierra_el_cliente` no vacío) dan para CL8 `funciones` 3, `kb` 3,5, `cerradas` 1,
    `kb_cerrados` 1,0, `sin_citar` 1, `con_cortes` 1, `cierran` 1; y el `total` suma las olas.
  - `test_deltas_show_new_and_missing_metrics`: una métrica igual no sale; una distinta sale con los dos valores; una
    que la vieja no tiene sale con `None`.
  - `test_render_sections`: el Markdown tiene los títulos `## Cuánto falta para portar`,
    `## Cuánto falta para cerrarlo contra el ASM` y `## El estado de las trazas`, y con `huecos.tsv` sin filas dice
    `Ninguno`.
- [ ] **Step 2:** `python3 -I -m unittest discover -s Client/docs/research/scripts/tools -p 'test_reference_summary.py' -v`
  falla porque el módulo no existe.
- [ ] **Step 3: el script.** Secciones, en este orden:
  1. **Cuánto falta para portar:** de `metricas.tsv`, `avance`/`total` y cada `avance-ola`: cota alta, media, baja,
     baja con test y `falta_cota_baja` (KB), en una tabla con una fila por ola y la del total.
  2. **Cuánto falta para cerrarlo contra el ASM:** `closure_by_wave`: % cerrado por funciones y por KB, y las columnas
     de lo que frena, por ola y total; abajo, una línea que recuerda que cerrado mide citas, tests, cortes, VM y 08,
     no la fidelidad byte a byte.
  3. **El estado de las trazas:** de `metricas.tsv`, los ámbitos `cortes` (por clase) y `huecos`; los cortes de
     `cortes.tsv` con `cierra_el_cliente` distinto de `no` y de vacío (archivo:línea, función, clase); los 10 huecos de
     más bytes de `huecos.tsv`; y la tabla de `metric_deltas` contra la commiteada (sin los ámbitos `serie` y `muret`),
     con "—" para lo que la commiteada no tiene, o "La referencia commiteada está al día" si no hay diferencias.
     Las listas de más de 20 filas se cortan con "y N más".
- [ ] **Step 4:** el comando del Step 2 pasa.
- [ ] **Step 5:** sobre la referencia del repo, `python3 -I reference_summary.py --nueva
  Client/docs/research/coverage/referencia --commiteada Client/docs/research/coverage/referencia` da las cotas de
  `LEEME.md` (91,7 / 89,5 / 87,4 %) y "al día".
- [ ] **Step 6: commit** (`ci: el resumen de la referencia del cliente`).

### Task 2: el workflow, la deploy key y el test en el CI

**Files:**
- Create: `.github/workflows/referencia.yml`
- Modify: `.github/workflows/ci.yml` (junto al paso de `test_*evidence.py`)
- Modify: `Client/docs/research/coverage/referencia/LEEME.md` (sección nueva, "La action")

**Interfaces:**
- Consumes: `reference_summary.py --nueva DIR --commiteada DIR` (Task 1).

- [ ] **Step 1: la deploy key**, con OK de Emanuel ya dado: `ssh-keygen -t ed25519 -N '' -C 'muret-referencia'` en el
  scratchpad; `gh repo deploy-key add <pub> -R EmaNLJN/mu-server-repack-research-102i -t 'MuRet: action de la
  referencia'` (sin `--allow-write`); `gh secret set RESEARCH_DEPLOY_KEY -R ema-ljn/MuRet < <priv>`; borrar el par
  local. Verificar: `gh repo deploy-key list -R …` la muestra de solo lectura y `gh secret list -R ema-ljn/MuRet` el
  secreto.
- [ ] **Step 2: `referencia.yml`.** Disparos: `workflow_dispatch` con `ref` (string, opcional) y `pull_request` con
  `paths` de este archivo y de `reference_summary.py`. Un job, `ubuntu-24.04`, `timeout-minutes: 30`,
  `permissions: contents: read`. Pasos:
  1. `actions/checkout@v7` con `fetch-depth: 0` y `ref: ${{ inputs.ref || github.ref }}`; `git rev-parse HEAD` a una
     variable.
  2. Sin `RESEARCH_DEPLOY_KEY`, corta con "falta el secreto RESEARCH_DEPLOY_KEY (los PRs desde un fork no lo tienen)".
  3. El clon con `GIT_SSH_COMMAND` y la clave en `$RUNNER_TEMP` (permisos 600): `git clone --depth 1
     --filter=blob:none --sparse git@github.com:EmaNLJN/mu-server-repack-research-102i.git`, y `git sparse-checkout set
     --no-cone` con las cuatro rutas del spec.
  4. `build_roadmap_matrix.py --research R --repo . --ref $COMMIT --out $RUNNER_TEMP/matriz.tsv --summary … --rows …`
     y `build_client_reference.py --research R --ref $COMMIT --matriz … --filas … --out $RUNNER_TEMP/referencia`.
  5. `reference_summary.py --nueva $RUNNER_TEMP/referencia --commiteada Client/docs/research/coverage/referencia
     >> $GITHUB_STEP_SUMMARY`.
  6. `actions/upload-artifact@v4` de `$RUNNER_TEMP/referencia` con `retention-days: 30`.
- [ ] **Step 3: el test en el CI:** en `ci.yml`, con `if: matrix.kind == 'build'`, un paso
  `python -m unittest discover -s Client/docs/research/scripts/tools -p 'test_reference_summary.py' -v`.
- [ ] **Step 4: LEEME.md**, "La action": cómo dispararla (Actions → Referencia del cliente → Run workflow, con la rama),
  qué muestra cada sección, qué es "cerrado" y el secreto que usa.
- [ ] **Step 5: verificar:** push de la rama y PR; la corrida de `pull_request` termina en verde, el resumen tiene las
  tres secciones y el artifact las tablas.
- [ ] **Step 6: commit** (`ci: la action de la referencia del cliente`).
