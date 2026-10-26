# Guía de la ola 6: Base de datos real

> **Guía de estudio.** Leé la sección de cada sub-ola antes de arrancarla; el tutor la explica en
> vivo y la usa para preguntarte. Los ejemplos son **fuera del proyecto** a propósito. Las
> decisiones ("verbatim" vs parametrizado, cómo llega el driver a Linux, cuántas conexiones) son
> las "preguntas para decidir" del
> [plan de la ola 6](../superpowers/plans/2026-10-26-ola-6-base-real.md), y las tomás vos.
> La sección final la completás al cerrar la ola.

## Mapa de la ola

| Sub-ola | Concepto | Al final tenés que poder explicar |
| --- | --- | --- |
| 6.1 | una API de C con handles, y RAII | quién libera qué en ODBC, en qué orden, y cómo se lee un error |
| 6.2 | sentencias preparadas y blobs | por qué un parámetro enlazado no se puede inyectar, y cómo se distinguen NULL, "sin filas" y "falló" |
| 6.3 | conectarse a un SQL Server viejo desde el host | qué partes de la cadena de conexión importan, y por qué los secretos no van en el repo |
| 6.4 | misma interfaz, otra implementación | qué es un test tipado y cómo se aísla un test que usa una base real |

---

## 6.1 Una API de C con handles

### La idea

- ODBC es una API de C: todo es un **handle** que pedís con `SQLAllocHandle` y devolvés con
  `SQLFreeHandle`. Hay una jerarquía:

  ```text
  entorno (SQLHENV)  →  conexión (SQLHDBC)  →  sentencia (SQLHSTMT)
  ```

  Un hijo se libera **antes** que su padre, y una conexión se desconecta (`SQLDisconnect`) antes de
  liberarse.
- Cada función devuelve un `SQLRETURN`:

  | Valor | Qué significa |
  | --- | --- |
  | `SQL_SUCCESS` | bien |
  | `SQL_SUCCESS_WITH_INFO` | bien, **pero** hay mensajes (un truncamiento, un aviso) |
  | `SQL_NO_DATA` | no hay (más) filas |
  | `SQL_ERROR` | falló: el detalle está en los registros de diagnóstico |
  | `SQL_INVALID_HANDLE` | bug tuyo: el handle no vale |

- El detalle de un error se lee con `SQLGetDiagRec`, **en bucle**: puede haber varios registros.
  Cada uno trae un **SQLSTATE** de 5 caracteres; los dos primeros son la clase (`08` conexión, `23`
  integridad, `42` sintaxis o permisos). `08S01` es "se cayó el enlace".
- RAII otra vez, como en la ola 2, con un detalle: en C++ los miembros se destruyen en **orden
  inverso** al que se declararon. Si una clase tiene el entorno y la conexión como miembros, el
  orden de declaración decide el orden de liberación.

### Ejemplo mínimo (fuera del proyecto)

Para una API de C con funciones de "abrir" y "cerrar", `std::unique_ptr` con un *deleter* propio
alcanza a veces:

```cpp
#include <cstdio>
#include <memory>

struct FileCloser {
    void operator()(std::FILE* f) const noexcept { std::fclose(f); }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

struct Copy {
    FilePtr src;     // se declara primero → se destruye último
    FilePtr dst;     // se declara segundo → se destruye primero
};

int main() {
    Copy c{FilePtr(std::fopen("a.txt", "rb")), FilePtr(std::fopen("b.txt", "wb"))};
    // ... al salir, se cierra dst y después src, sin escribir un solo fclose
}
```

Los handles de ODBC necesitan además saber **qué tipo** de handle son para liberarse
(`SQLFreeHandle(SQL_HANDLE_STMT, h)`). Cómo meter ese dato en tu envoltorio es parte del ejercicio.

### Errores típicos

- Tratar `SQL_SUCCESS_WITH_INFO` como error, o ignorar que trae información.
- Leer solo el primer registro de diagnóstico.
- Liberar la conexión antes que sus sentencias.
- Envolver los handles pero dejar que el envoltorio se copie.

### Cómo lo hacía el original

`CQuery` es una clase sin RAII con buffers fijos (100 columnas × 255 bytes). Mira el SQLSTATE solo
para reconectar ante `08S01`
([06 § `CQuery`](../../Server/DataServer/docs/research/06-sql-y-base.md#cquery-la-capa-odbc)).

### Para leer

- Microsoft Learn, "ODBC Programmer's Reference": "Handles", "Allocating the Environment Handle",
  "Diagnostics" y el apéndice de códigos SQLSTATE.
- cppreference: `std::unique_ptr` (deleters), "Destructors" (orden de destrucción de miembros).

### Preguntas para chequearte

1. ¿En qué orden se destruyen los miembros de una clase? ¿Y los elementos de un `std::vector`?
2. Te llega `SQL_SUCCESS_WITH_INFO` al leer una columna. ¿Qué puede haber pasado?
3. ¿Qué te dice un SQLSTATE `42S02`, sin buscarlo? ¿Y después de buscarlo?

---

## 6.2 Sentencias preparadas y blobs

### La idea

- **Inyección SQL:** si armás el texto con `sprintf("... WHERE name = '%s'", nombre)`, el nombre
  pasa a ser **parte del SQL**. Con `O'Brien` la sentencia ya no compila; con `x' OR '1'='1`
  devuelve todas las filas.
- **Sentencia preparada:** el texto lleva `?` donde van los datos (`... WHERE name = ?`). Se
  prepara una vez (`SQLPrepare`) y los valores se **enlazan** aparte (`SQLBindParameter`). El
  servidor nunca interpreta el valor como SQL: no hay nada que inyectar.
- **Blobs largos:** no hace falta tenerlos enteros en un parámetro. Para escribir, el parámetro se
  marca como "dato en ejecución" y se manda en trozos con `SQLParamData` + `SQLPutData`. Para leer,
  `SQLGetData` en bucle: mientras falte, devuelve `SQL_SUCCESS_WITH_INFO` con SQLSTATE `01004`
  (truncado).
- **Tres resultados que no se mezclan:**
  - **NULL:** la columna existe, el valor no. El indicador vale `SQL_NULL_DATA`.
  - **Sin filas:** `SQLFetch` devuelve `SQL_NO_DATA` en la primera llamada.
  - **Falló:** `SQL_ERROR` y diagnósticos.

### Ejemplo mínimo (fuera del proyecto)

El recorrido de una sentencia preparada, sin código de MuRet:

```text
SQLPrepare(stmt, "UPDATE books SET cover = ? WHERE isbn = ?")
SQLBindParameter(1, binario, "dato en ejecución", largo total = 25000)
SQLBindParameter(2, texto, "978-0131103627")
SQLExecute → SQL_NEED_DATA
  SQLParamData → "falta el parámetro 1"
  SQLPutData(10000 bytes)  SQLPutData(10000 bytes)  SQLPutData(5000 bytes)
  SQLParamData → SQL_SUCCESS
```

### Errores típicos

- "Escapar" comillas a mano en vez de parametrizar.
- Un buffer enlazado que deja de existir antes del `SQLExecute` (el driver guarda el puntero, no
  una copia: es la misma regla de vida del buffer de la ola 3).
- Leer un blob de una sola vez con un buffer "suficientemente grande".
- No mirar el indicador y tomar un NULL como cadena vacía o como cero.

### Cómo lo hacía el original

Casi todo el SQL va con `%s` sin escapar (Q-07); solo los blobs van por `?`, en trozos de 10 000
bytes ([06 § inyección](../../Server/DataServer/docs/research/06-sql-y-base.md#inyección-sql),
[catálogo](../../Server/DataServer/docs/research/06-sql-y-base.md#catálogo-de-sql-verbatim)). Los
códigos `-100` (NULL), `-101` (sin filas) y `-103` (falló) se devuelven como si fueran datos.

### Para leer

- Microsoft Learn: `SQLBindParameter`, "Sending Long Data", `SQLGetData` ("Getting Long Data").
- OWASP: "SQL Injection Prevention Cheat Sheet" (la parte de *prepared statements*).

### Preguntas para chequearte

1. ¿Por qué un `?` no se puede usar para el nombre de una tabla?
2. Un blob de 25 000 bytes con trozos de 10 000: ¿cuántas llamadas a `SQLGetData` hacés y qué
   devuelve cada una?
3. ¿Qué devuelve tu código para "el personaje no existe", y qué para "la columna `Quest` es NULL"?

---

## 6.3 Conectarse al SQL Server de la VM

### La idea

- **Cadena de conexión:** se puede conectar por DSN (como el original) o sin DSN, con todo en la
  cadena: driver, servidor y puerto, base, autenticación, cifrado.
- **Autenticación:**
  - *de Windows* (`Trusted_Connection=Yes`): entra con la cuenta del proceso. Desde el host a la VM
    solo anda si esa cuenta existe del otro lado.
  - *de SQL Server* (usuario y clave): requiere que el servidor tenga habilitado el modo mixto.
- **Red:** el SQL Server tiene que escuchar por TCP/IP (en ediciones Express viene apagado), en un
  puerto conocido (1433 si es la instancia por defecto), y el firewall de la VM lo tiene que
  permitir.
- **Drivers y versiones viejas:** los drivers actuales de Microsoft cambiaron valores por defecto
  (el Driver 18 cifra por defecto) y no todos soportan versiones viejas de SQL Server ni de TLS.
  **A verificar en esta sub-ola:** la versión exacta de la VM (`SELECT @@VERSION`), qué driver
  conecta desde Windows y cuál desde Linux, y con qué opciones de cifrado.
- **Secretos:** la cadena de conexión con usuario y clave **nunca** va en el código ni en el repo.
  Va en un archivo de config local (fuera de git) o en una variable de entorno.
- **Reconexión:** ante `08S01`, reintentar con espera creciente (*backoff*) y un tope. Desde la
  ola 4 la red no se congela, pero el hilo de lógica sí espera.

### Ejemplo mínimo (fuera del proyecto)

Las partes de una cadena de conexión sin DSN (valores inventados):

```text
Driver={<nombre del driver>};
Server=<ip>,<puerto>;
Database=<base>;
UID=<usuario>;PWD=<clave>;        ← o Trusted_Connection=Yes
Encrypt=<yes|no>;TrustServerCertificate=<yes|no>;
```

### Errores típicos

- Probar la conexión directamente desde el programa. Primero, con una herramienta (`sqlcmd`, o el
  administrador de orígenes de datos ODBC con "Probar conexión"): si falla ahí, no es tu código.
- Confundir "no llego al puerto" (red, firewall, TCP/IP apagado) con "llego pero me rechaza"
  (login, cifrado). El mensaje del driver y el SQLSTATE los distinguen.
- Subir un archivo de config con la clave.
- Reconectar en un bucle sin espera ni tope.

### Cómo lo hacía el original

El DSN usa `Trusted_Connection=Yes`, así que las credenciales hardcodeadas `Admin`/`BobStudio` se
ignoran; el login `Admin` ni existe (Q-06,
[02 § conexión a la base](../../Server/DataServer/docs/research/02-arranque.md#conexión-a-la-base)).
Ante `08S01` reconecta con el lock global tomado y congela el servidor entero.

### Para leer

- Microsoft Learn: "Connection string keywords" del driver ODBC para SQL Server, "Configure a
  Windows Firewall for Database Engine Access", y la matriz de compatibilidad de versiones del
  driver.
- `man sqlcmd` (o su página en Microsoft Learn), para probar sin tu código.

### Preguntas para chequearte

1. Te da timeout al conectar. ¿Qué tres cosas revisás, y en qué orden?
2. ¿Por qué el original "funciona" con una clave que no existe?
3. ¿Dónde vive la clave en tu máquina, y cómo te asegurás de que nunca llegue a un commit?

---

## 6.4 Misma interfaz, otra implementación

### La idea

- Los repositorios ODBC implementan las **mismas** interfaces de la ola 5. Los tests de contrato
  pasan a ser **tests tipados**: el mismo test corre una vez por implementación.
- Un test contra una base real tiene que estar **aislado**: no depender de lo que dejó otro test.
  Formas comunes: una transacción que se deshace al final del test, datos con nombres únicos, o
  limpiar en el `TearDown`.
- Estos tests no corren en CI (no hay SQL Server ahí): se marcan con una **etiqueta** de CTest y se
  excluyen (`ctest -LE integration`). Los corrés vos contra la VM.
- Elegir la implementación por config en `main` es el único lugar que sabe cuál se usa.

### Ejemplo mínimo (fuera del proyecto)

Un test tipado de GoogleTest: dos contenedores, el mismo contrato.

```cpp
#include <deque>
#include <gtest/gtest.h>
#include <vector>

template <typename T>
class ContainerContract : public ::testing::Test {};

using Impls = ::testing::Types<std::vector<int>, std::deque<int>>;
TYPED_TEST_SUITE(ContainerContract, Impls);

TYPED_TEST(ContainerContract, PushBackKeepsOrder) {
    TypeParam c;
    c.push_back(1);
    c.push_back(2);
    EXPECT_EQ(c.front(), 1);
    EXPECT_EQ(c.back(), 2);
}
```

### Errores típicos

- Tests que pasan solos y fallan juntos (comparten datos en la base).
- Un test tipado que en realidad depende de algo propio de una implementación.
- Dejar la VM con basura de los tests y no saber qué es dato real.

### Cómo lo hacía el original

No tiene tests. El esquema que asume (tablas y columnas) está en
[06 § esquema deducido](../../Server/DataServer/docs/research/06-sql-y-base.md#esquema-deducido),
y es lo que tus repositorios ODBC tienen que respetar.

### Para leer

- GoogleTest Advanced: "Typed Tests" y "Type-Parameterized Tests".
- CMake: `set_tests_properties(... PROPERTIES LABELS ...)` y `ctest -L` / `-LE`.

### Preguntas para chequearte

1. ¿Qué diferencia hay entre `TYPED_TEST` y `TYPED_TEST_P`, y cuál te sirve acá?
2. ¿Cómo hacés que un test que crea la cuenta `test_x` no choque con otro que crea la misma?
3. ¿Qué tiene que cambiar en un handler para pasar de memoria a ODBC? (La respuesta correcta es
   corta.)

---

## Al cerrar la ola (lo completás vos)

- **Qué aprendí:**
- **Qué me costó y cómo lo destrabé:**
- **Cómo quedó la conexión a la VM** (resumen; el detalle va en `ola-6.3-vm-sqlserver.md`):
- **Decisiones que tomé (y por qué):** qué se conserva del SQL original · cómo llega el driver a
  Linux · cuántas conexiones
- **Preguntas que me quedaron para más adelante:**
