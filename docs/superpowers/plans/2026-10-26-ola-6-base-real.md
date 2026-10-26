# Plan de la ola 6: Base de datos real

> Plan de ejecución en el formato de superpowers (writing-plans).
> Roadmap: [`2026-10-25-dataserver-port-design.md`](../specs/2026-10-25-dataserver-port-design.md).
> Escrito por adelantado el 2026-10-26; se revisa al arrancar la ola.

**Objetivo:** los mismos repositorios de la ola 5, ahora contra el SQL Server de la VM2003 por
ODBC. Los handlers no cambian: solo se cambia qué implementación se inyecta.

**Arquitectura:** un envoltorio ODBC propio con RAII (los handles se liberan solos) en
`Database/Odbc/`, sentencias preparadas con parámetros, blobs en trozos, y repositorios ODBC que
implementan las interfaces de la ola 5. Los tests de contrato de la ola 5 corren contra las dos
implementaciones.

**Stack:** C++20, ODBC 3.x (`odbc32` en Windows; unixODBC + el driver de Microsoft para Linux),
SQL Server de la VM2003 (puerto 1433), GoogleTest (tests tipados), CTest labels.

**Guía de estudio:** [`docs/learning/ola-6-odbc.md`](../../learning/ola-6-odbc.md): leé su sección antes de cada
sub-ola; la sección final la completás al cerrar la ola.

**Research que fija esta ola:** [06-sql-y-base](../../../Server/DataServer/docs/research/06-sql-y-base.md)
(conexiones, catálogo de SQL, esquema, `CQuery`, inyección) y Q-05, Q-06, Q-07 de
[08](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md).

**Terminada cuando:** los tests de contrato de repositorio pasan contra la base `MuOnline` de la VM,
con una etiqueta de CTest que el CI excluye.

## Preguntas para decidir al empezar

1. **"Verbatim" vs parametrizado.** El roadmap pide compatibilidad con el texto SQL del original
   **y** todo parametrizado (D-11). No se pueden las dos al pie de la letra: `'%s'` pasa a ser `?`.
   ¿Qué se conserva exactamente? Sugerencia: tablas, columnas, nombres de procedimientos y orden
   de argumentos de los `MX_*` al byte; el literal cambia. Queda en el ADR 0004.
2. **Cómo llega el driver a Linux.** El CI corre en Linux sin SQL Server. ¿Se compila ODBC en
   Linux igual (unixODBC por vcpkg) aunque los tests de integración solo corran contra la VM?
3. **Pool.** El original abre 8 conexiones fijas (2 sin usar) más 2 que nunca conectan. Con un solo
   hilo de lógica, ¿cuántas conexiones hacen falta?

---

## Sub-ola 6.1: ODBC crudo con RAII

**Concepto:** una API de C con handles (`SQLHENV`, `SQLHDBC`, `SQLHSTMT`), quién libera qué, y
cómo RAII lo hace imposible de olvidar. Diagnóstico con `SQLGetDiagRec` y SQLSTATE.

**Archivos:**
- Crear: `Server/DataServer/src/Database/Odbc/OdbcHandle.h` (envoltorio dueño de un handle), `OdbcEnvironment.h/.cpp`, `OdbcConnection.h/.cpp`, `OdbcError.h/.cpp`
- Crear: `Server/DataServer/tests/database/OdbcHandleTests.cpp`
- Modificar: `vcpkg.json` (unixODBC en Linux), `Server/DataServer/CMakeLists.txt`

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** tests que no necesitan base: un handle movido queda vacío; el error de conectar a un DSN inexistente trae SQLSTATE y mensaje.
- [ ] **Paso 2 (Emanuel):** `OdbcHandle<Type>` con `SQLFreeHandle` en el destructor; entorno con `SQL_OV_ODBC3`.
- [ ] **Paso 3 (Emanuel):** `OdbcError` arma el error con **todos** los registros de diagnóstico.
- [ ] **Paso 4:** compila y pasa en CI en Windows y Linux.

## Sub-ola 6.2: sentencias preparadas y blobs

**Concepto:** `SQLPrepare` + `SQLBindParameter` contra `sprintf`; inyección SQL (Q-07); blobs
largos en trozos con `SQLPutData`/`SQLGetData`; distinguir NULL, "sin filas" y "falló" (lo que el
original mezcla con `-100..-103`).

**Archivos:**
- Crear: `Server/DataServer/src/Database/Odbc/OdbcStatement.h/.cpp`, `SqlText.h` (todas las sentencias en un solo lugar)
- Crear: `docs/adr/0004-sql-parametrizado.md`
- Crear: `Server/DataServer/tests/database/OdbcStatementTests.cpp` (etiqueta `integration`)

- [ ] **Paso 1 (Emanuel):** ADR 0004 con la decisión de la pregunta 1.
- [ ] **Paso 2 (Emanuel):** `SqlText.h` con cada sentencia del [catálogo](../../../Server/DataServer/docs/research/06-sql-y-base.md#catálogo-de-sql-verbatim), parametrizada, citando la dirección del original en un comentario.
- [ ] **Paso 3 (Emanuel, `TODO(human)`):** tests: un parámetro con comilla simple no rompe nada; un blob de 25 000 bytes ida y vuelta (más de un trozo de 10 000); NULL, sin filas y error dan tres resultados distintos.
- [ ] **Paso 4 (Emanuel):** `OdbcStatement` con parámetros enlazados y lectura por columna.

## Sub-ola 6.3: conexión al SQL Server de la VM

**Concepto:** cadena de conexión, autenticación de Windows vs SQL (el original cree que usa
`Admin`/`BobStudio` pero el DSN lo ignora, Q-06), red entre el host y la VM, reconexión.

**Archivos:**
- Modificar: `Server/DataServer/src/Core/Config.*` (cadena de conexión, nunca en el código)
- Crear: `Server/DataServer/tests/database/OdbcConnectionIntegrationTests.cpp` (etiqueta `integration`)
- Crear: `docs/learning/ola-6.3-vm-sqlserver.md` (cómo levantar la conexión desde el host)

- [ ] **Paso 1 (Emanuel):** conectar desde el host al SQL Server de la VM (puerto 1433) y anotar los pasos en la nota.
- [ ] **Paso 2 (Emanuel):** el servidor **no arranca** si la base no responde, con un mensaje claro (el original aborta en la primera conexión que falla).
- [ ] **Paso 3 (Emanuel):** reconexión ante `08S01` sin congelar la red (la red no espera al hilo de lógica desde la ola 4).
- [ ] **Paso 4 (Claude):** label `integration` en CTest y exclusión en CI (`ctest -LE integration`).

## Sub-ola 6.4: repositorios ODBC

**Concepto:** la misma interfaz, otra implementación; tests de contrato que corren contra las dos.

**Archivos:**
- Crear: `Server/DataServer/src/Database/Odbc/` (un repositorio por interfaz de la ola 5)
- Modificar: `Server/DataServer/tests/database/RepositoryContractTests.cpp` (tests tipados: Memory y Odbc)
- Modificar: `Server/DataServer/src/main.cpp` (elegir la implementación por config)

- [ ] **Paso 1 (Emanuel):** convertir los tests de contrato en `TYPED_TEST` para correr contra `Memory` y `Odbc`.
- [ ] **Paso 2 (Emanuel):** implementar los repositorios ODBC hasta que pasen contra la VM.
- [ ] **Paso 3:** correr el test de integración de la ola 5 (login, lista, crear, cargar) contra la base real.

---

## Al cerrar la ola 6

- [ ] Completar la sección de cierre de la [guía de la ola 6](../../learning/ola-6-odbc.md).
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Revisar el plan de la ola 7.
