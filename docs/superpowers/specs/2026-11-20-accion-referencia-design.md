# La action de la referencia del cliente: cuánto falta (diseño corto)

2026-11-20. Pedido de Emanuel: una action de GitHub que se dispare a mano y diga, sobre una rama, cuánto falta para
portar el `main.exe` y cuánto falta para darlo por cerrado contra el ASM/pseudocódigo, con el estado de las trazas,
sin correr nada en su máquina.

## Qué y para qué

- **Objetivo:** saber, en una corrida y sin abrir TSV, el avance del port y lo que queda, por ola y en total, y ver si
  la referencia commiteada en la rama quedó vieja.
- **No es objetivo:** reemplazar la referencia commiteada ni regenerarla en el repo (la action no toca el repo), ni
  medir la fidelidad byte a byte: citar no es portar fiel (`coverage/referencia/LEEME.md`, Límites).
- **Sería un fracaso:** un "% cerrado" que cuente como cerrado lo que solo está citado.

## Decisiones de Emanuel

- **Acceso al research** (privado; MuRet es público): una deploy key de solo lectura en
  `EmaNLJN/mu-server-repack-research-102i`, con la privada como secreto `RESEARCH_DEPLOY_KEY` de MuRet.
- **Salida:** el resumen en la página de la corrida (`GITHUB_STEP_SUMMARY`) y las tablas regeneradas como artifact. No
  commitea ni abre PRs.

## Approaches

1. **Un workflow aparte, `.github/workflows/referencia.yml`** (elegido): `workflow_dispatch` con la rama o el commit;
   corre los dos generadores que ya existen y un script nuevo, testeable, que arma el resumen.
2. Un job más en `ci.yml`: mezcla una medición manual de ~5 minutos con el CI de cada PR. Descartado.
3. El resumen en el YAML con `awk`: frágil y sin tests. Descartado.

## Cómo

- **El workflow:** `workflow_dispatch` con `ref` (por defecto, la rama desde la que se dispara). Checkout de MuRet con
  la historia entera (el `git blame` de la matriz). Clon parcial (`--filter=blob:none`) y disperso del research con la
  deploy key: `client-research/mainexe-vmxp-c32bbd5f/original/`, `decompiled/functions/`, `decompiled/inventory.tsv` y
  `client-research/analysis/mainexe/` (~100 MB). Python de la imagen (los scripts usan solo la biblioteca estándar).
- **Los generadores, sin cambios:** `build_roadmap_matrix.py` y `build_client_reference.py` con `--ref` el commit
  pedido y `--out` en `$RUNNER_TEMP`, no en el repo.
- **El resumen, `Client/docs/research/scripts/tools/reference_summary.py`:** lee `metricas.tsv`, `funciones.tsv`,
  `cortes.tsv` y `huecos.tsv` de la carpeta regenerada y la `metricas.tsv` commiteada en la rama, y escribe Markdown:
  1. **Cuánto falta para portar:** las cotas alta, media y baja, y la baja con test, con los KB que faltan, en total y
     por ola.
  2. **Cuánto falta para cerrarlo contra el ASM:** las funciones vivas del juego (y sus KB) que cumplen todo esto:
     `implemented`, sin falso positivo, sin cortes vivos, sin citas pendientes, con al menos una cita de test, sin
     pasos de VM y sin filas de 08 abiertas. Al lado, lo que frena al resto: sin citar, con cortes (y cuántos cierran
     el cliente), sin test, con VM pendiente y con 08 abiertas, en total y por ola.
  3. **El estado de las trazas:** los cortes vivos por clase, los que cierran el cliente (archivo:línea), los huecos
     (bloques, funciones y KB, con los 10 más grandes) y la diferencia de cada número contra la `metricas.tsv`
     commiteada: si difiere, la referencia del repo está vieja.
- **El artifact:** la carpeta regenerada entera, con 30 días de retención.
- **Tests:** `test_reference_summary.py` con TSV chicos inventados (la definición de cerrado, los totales por ola y la
  diferencia contra la commiteada), con `unittest`, en el paso de Python que ya tiene el CI.
- **Docs:** cómo dispararla y qué muestra, en `coverage/referencia/LEEME.md`.

## Lo que queda a quien la construya

El orden exacto de las tablas del resumen, los nombres de los pasos y cuándo cortar listas largas (más de 20 filas).
