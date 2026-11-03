# Reglas de código

Estas reglas valen para todo cambio de código en MuRet, lo escriba una persona o un agente. Leelas
antes de commitear.

> Adaptadas de [`docs/CODING_RULES.md` de sven-n/MuMain](https://github.com/sven-n/MuMain/blob/main/docs/CODING_RULES.md).
> Se conservan sus doce reglas, traducidas. Se sacó lo que es propio de un cliente (C#, UI,
> render) y se agregó lo del DataServer: sus capas, el research como contrato y la regla 13 de
> tests.

---

## 1. Funciones cortas y enfocadas

- Una función hace **una cosa**. Si para describirla necesitás un "y", partila.
- Apuntá a funciones que entren en una pantalla (unas 40 líneas). Las que tienen mucha lógica
  (un parser, un handler con varios pasos), más cortas todavía.
- Si el pedazo que extraés no pertenece al archivo actual, **movelo a otro archivo** en la carpeta
  que corresponda.

## 2. Salir temprano

- Los errores y los casos borde se manejan **arriba** de la función.
- Nada de anidar de más: mejor `if (malo) return …;` que envolver todo el cuerpo en
  `if (bueno) { … }`.
- Cada salida temprana tiene una razón clara. Un error no se traga en silencio: se devuelve o se
  loguea.

## 3. Nada hardcodeado

- Números, cadenas, rutas, tamaños, timeouts: si no se explican solos, **llevan nombre**.
- Constantes, `enum class` o valores de configuración. Un número mágico enterrado en la lógica es
  un bug esperando su turno.
- **Lo que depende del lugar donde corre va en la config**: el puerto, la cadena de conexión, las
  rutas a los archivos de datos. Ningún secreto va en el código ni en el repo.
- **Las constantes del wire** (heads, tamaños, offsets) son constantes con nombre en `Protocol/`,
  con un comentario que cita la sección del research que las fija
  (por ejemplo `// 05-protocolo § tabla de heads`).
- `0` y `1` en contextos booleanos o triviales están bien. `0x3BC`, `55960` y `5000` no.

## 4. Código reutilizable

- Antes de escribir lógica nueva, fijate si ya existe algo parecido. No dupliques.
- Si dos o más lugares necesitan lo mismo, sacalo a una función o un helper.
- Parámetros generales cuando no cuesta nada, pero sin abstraer para futuros hipotéticos.

## 5. Capas separadas

El DataServer tiene estas capas, cada una con su carpeta en `Server/DataServer/src/` y su
namespace:

| Capa | Namespace | Hace | No hace |
| --- | --- | --- | --- |
| `Core/` | `DataServer::Core` | config, log, tipos de error | nada de red, SQL ni paquetes |
| `Network/` | `DataServer::Network` | sockets, IOCP/epoll, sesiones, framing | no sabe de SQL ni de qué significa cada head |
| `Protocol/` | `DataServer::Protocol` | bytes ↔ structs de cada head | no toca sockets ni base: es puro |
| `Dispatch/` | `DataServer::Dispatch` | cola, hilo de lógica, tabla head → handler | no tiene lógica de juego |
| `Handlers/` | `DataServer::Handlers` | la lógica de cada head | no toca sockets ni escribe SQL: usa interfaces |
| `Database/` | `DataServer::Database` | interfaces de repositorio y sus implementaciones (memoria, ODBC) | no conoce paquetes |

- **Solo `main.cpp` conoce a todas** y las conecta.
- El código que depende del sistema (`#ifdef _WIN32`) vive en un solo lugar de `Network/`; el resto
  no ve `#ifdef`.
- Agrupá datos y comportamiento relacionados. No desparrames la misma lógica por archivos que no
  tienen nada que ver.
- Nombrá con precisión: si un nombre necesita un comentario para entenderse, elegí otro.

## 6. Una clase por archivo

- Cada clase o struct importante tiene su **propio archivo**, con el nombre de la clase.
- Header e implementación separados (`.h` / `.cpp`). Structs chicos muy pegados a una sola clase
  pueden quedarse en el header de esa clase.

## 7. Estilo consistente

- El formato lo decide [`.clang-format`](../.clang-format) (el de MuMain); corré `clang-format`
  antes de commitear y no lo discutas en la revisión. CI lo verifica en `Client/`, `labs/` y
  `Server/` (`clang-format --dry-run --Werror`, versión 18).
- Seguí el estilo del código que está alrededor. No inventes una convención nueva en un archivo.
- Includes mínimos: solo lo que el archivo usa, y todo lo que usa. Un header compila solo, con sus
  propios includes, y un archivo no cuenta con lo que le llega por el include de otro (Core
  Guidelines SF.10 y SF.11): si usa `std::vector`, incluye `<vector>`. Si un header solo nombra un
  tipo por puntero o referencia, alcanza con declararlo (`class ISocket;`). No hay `stdafx.h` que
  incluir: los precompiled headers los arma CMake y los fuerza solo
  ([`cmake/PrecompiledHeaders.cmake`](../cmake/PrecompiledHeaders.cmake)), con la biblioteca
  estándar. Son para compilar más rápido, no reemplazan ningún include, y el job de Linux del CI
  compila sin ellos para que lo que falte falle.
- Comentarios, mensajes y docs en castellano; identificadores y nombres de tests en inglés
  (`Feed`, `TEST(PacketReaderTest, CutsAC1)`).
- `.editorconfig` fija espacios, LF y UTF-8; `.clang-tidy` (el de MuMain, con las carpetas de
  MuRet) se corre a mano: `run-clang-tidy -p build/linux-x64-debug -quiet 'Client/(src|tests)/'`.

### Nombres (decidido el 2026-11-02, alineado con el código nuevo de MuMain)

Son las costumbres del código nuevo de MuMain (el de los namespaces `<Capa>::<Concepto>`, no el
heredado con `C*`, notación húngara y `g_`). Sus reglas escritas no las nombran: salen de ese
código (`m_socket` en `Network/Reconnect/ReachabilityProbe.h`, `ICamera`, `s_installed`). Valen
para todo el código nuevo y para `Client/` y `labs/cl2.0-flujos/` enteros. `Server/` y
`labs/0.0-enlace/` se alinean cuando se toca cada archivo. Eso es de MuRet, no de MuMain: allá el
código viejo sigue con las costumbres de su módulo hasta una modernización aparte, "not
piecemeal", porque es mucho; acá son dos archivos, y alinearlos al tocarlos no mezcla estilos.

- Tipos, funciones, métodos y namespaces en `PascalCase`: `PacketReader`, `PacketReader::Feed`,
  `Client::Network`.
- Variables, parámetros y locales en `camelCase`: `firstWireByte`, `declaredSize`.
- Miembros de datos privados y protegidos con `m_` y `camelCase`: `m_buffer`, `m_consumed`. Así se
  distingue el estado del objeto de un local al leer un método, y el parámetro de un constructor
  puede tener el nombre natural sin chocar con `-Wshadow` (`cmake/CompilerWarnings.cmake`):
  `ByteReader(std::span<const std::byte> buffer) : m_buffer(buffer)`. Vale también para los
  fixtures de GoogleTest (`m_connections`).
- Los campos públicos de un `struct` sin invariantes van en `camelCase`, sin prefijo:
  `LoadedItemTable::table`, como `Rgb::red` en MuMain.
- Las interfaces (clases con solo funciones virtuales puras) llevan `I` adelante: `IObfuscation`,
  en `IObfuscation.h`. Sus implementaciones no: `IdentityObfuscation`, `FoxAntiObfuscation`.
- Las variables estáticas que no son `const`, de un archivo (en un namespace anónimo) o de una
  función (`static bool s_initialized` adentro de una función, como en MuMain), llevan `s_`. Hoy no
  hay ninguna.
- Constantes y enumeradores en `PascalCase`, con `constexpr` y no `#define`: `MaxPacketSize`,
  `PacketType::C3`.

### Cómo se arma un archivo

- Un header empieza con `#pragma once`, después van los includes y recién después el comentario
  del archivo, pegado a lo primero que declara (el namespace o la clase), como
  `Network/Reconnect/ReachabilityProbe.h` en MuMain:

  ```cpp
  #pragma once

  #include "Network/Packet.h"

  #include <cstddef>

  // Corta un flujo TCP en paquetes C1/C2/C3/C4...
  namespace Client::Network
  {
  class PacketReader
  ```

- Un `.cpp` empieza con su propio header; si tiene un comentario de archivo, va arriba de lo que
  describe, después de los includes.
- Sin línea en blanco después de la llave de `namespace X {` ni antes de `} // namespace X`.

### Tests y CMake (como MuMain)

- Un archivo de test se llama `test_<snake_case>.cpp`, por lo que prueba, y va en la carpeta de su
  módulo: `Client/tests/network/test_packet_reader.cpp`, `test_golden_item_table.cpp`. Seguimos con
  GoogleTest (D-C9), y la suite lleva el nombre de la clase: `TEST(PacketReaderTest, CutsAC1)`.
  Así `ctest -R PacketReader` sigue filtrando como antes.
- Un ejecutable por archivo de test, como `mu_add_test` en MuMain: `muret_add_test` en
  `Client/tests/CMakeLists.txt` lo arma, lo enlaza con `ClientLib`, `ClientTestSupport` (los `.cpp`
  de `tests/support/` que no son tests) y GoogleTest, le pone los warnings y registra cada `TEST` en
  CTest. Cada `TEST` tiene un tope de tiempo (`MURET_TEST_CASE_TIMEOUT`), como el `TIMEOUT` de
  `mu_add_test`. Cada módulo de `tests/` tiene su `CMakeLists.txt`.
- Los fuentes se juntan con un glob y `CONFIGURE_DEPENDS`: `ClientLib` toma todo `src/` con
  `file(GLOB_RECURSE)`, menos `main.cpp` (y, desde la CL3.1, `src/Platform/`, que es de
  `ClientPlatform`, sacado con `list(FILTER ... EXCLUDE REGEX)` como hace MuMain), y cada módulo de
  tests sus `test_*.cpp` con `file(GLOB)`. Un archivo nuevo entra al reconfigurar, sin tocar el
  CMake. Es una diferencia elegida: MuMain lista cada `mu_add_test` a mano. El glob de un módulo
  no entra en subcarpetas, y una carpeta de tests nueva necesita su `CMakeLists.txt` y su
  `add_subdirectory`; si un `test_*.cpp` queda sin ejecutable, el configure se corta y lo nombra.
- Excepción, los labs (`labs/<ola>/`): sus archivos también se llaman `test_<snake_case>.cpp`,
  pero cada lab es **un** ejecutable (`LabCl20Tests`) con su lista a mano, porque el lab es la
  unidad que se aprende y se corre entera.

### Namespaces

1. Siempre con el componente adelante, para no chocar cuando entren otros servidores:
   `Client::<Capa>[::<Concepto>]`, `DataServer::<Capa>[::<Concepto>]`. MuMain no lo usa porque
   tiene un solo componente. La jerarquía va **por dominio**: lo de gremios va en
   `DataServer::Handlers::Guild`, lo arme quien lo arme.
2. Los helpers privados de un archivo van en un namespace anónimo.
3. Nada de `using namespace` en un header.

## 8. Sin fugas ni estado colgado

- Todo recurso tiene un dueño claro y un camino claro de liberación.
- **RAII siempre**: sockets, handles de ODBC, hilos y memoria se liberan en un destructor.
  `std::unique_ptr` (o `std::shared_ptr` cuando la propiedad es compartida de verdad) en vez de
  `new`/`delete` crudos.
- No dejes recursos abiertos en los caminos de salida temprana.
- **La vida de un buffer en vuelo:** un buffer o un `OVERLAPPED` entregado al sistema le pertenece
  hasta la completion; no se libera ni se reusa antes (es C-3 del research).
- **Estado entre hilos:** cada dato tiene un hilo dueño, o una sincronización explícita y
  documentada. El estado del servidor lo toca solo el hilo de lógica.

## 9. Legible antes que ingenioso

- Escribí código obvio de leer, aunque la versión "ingeniosa" sea más corta.
- Las expresiones complejas se parten en variables intermedias con nombre.
- Si un bloque necesita un comentario para explicar **qué** hace, refactorizalo para que no lo
  necesite. Los comentarios explican el **porqué**.

## 10. Pensar antes de agregar

- Cada dependencia, abstracción o patrón nuevo tiene un costo de mantenimiento: justificalo. Las
  dependencias entran por `vcpkg.json` y, si son de peso, con un ADR.
- **No reconstruir la rueda.** Antes de escribir algo que no sea propio del MU, buscá una biblioteca
  en vcpkg y leé su documentación (Context7). Lo propio del MU son sus formatos, sus checksums y sus
  cifrados; si uno de ellos combina primitivas estándar (SHA-1, AES), lo propio es la combinación y
  las primitivas se buscan.
  - La biblioteca entra solo si resuelve **ese** problema y lo hace como se espera. Una que hace
    algo parecido no alcanza (con el código propio es al revés: regla 4).
  - Lo que se espera lo fija el research: la salida del original, salvo lo que MuRet corrige a
    propósito y deja escrito. Lo prueban los tests golden, corridos con la carpeta de datos porque
    CI no la tiene, y vectores chicos que sí corren en CI. Si una diferencia chica se acepta, lo
    decide el roadmap (para JPEG, su pregunta 1).
  - Se escribe a mano igual si el roadmap o el camino ya lo decidieron (D-3, D-5, D-C2; el
    `Expected` es la excepción de L-3 porque C++20 no lo trae), o si son pocas líneas y la
    dependencia cuesta más que ellas. Reabrir una de esas decisiones es un PR a esa decisión. Un
    ejercicio del camino va en `labs/` y no alcanza para mantener una copia casera.
  - Si la salida depende de la versión de la biblioteca, subirla es un PR propio que corre los
    golden.
  - El PR que suma una dependencia, o que escribe a mano algo que no es del MU, dice qué
    bibliotecas se miraron y por qué entraron o no.
- **Una sub-ola = un concepto = un PR chico.** Los diffs chicos se revisan mejor.
- Si dudás de una decisión estructural, preguntá antes de construir encima.

## 11. Documentar el uso, no las tripas

- Lo nuevo o cambiado se documenta al nivel de **uso**: cómo se compila, se corre y se configura
  (`Server/DataServer/README.md`).
  - Fuera de alcance: firmas, diagramas de clases, flujo interno. El código es la fuente de verdad
    de eso.
- La excepción son **las reglas del protocolo y los cálculos no obvios**: qué responde cada head,
  escalas de unidades, el cifrado del nombre. El comportamiento del original está en el
  [research](../Server/DataServer/docs/research/README.md); citá su sección desde el código en vez
  de copiarla.
- **Cuando MuRet hace algo distinto del original** (un quirk arreglado, un cambio de wire), se
  escribe un ADR en `docs/adr/` con el contraste: qué hacía el original, qué hace MuRet y por qué.
- Si no le sirve a quien opera el servidor o a quien busca una regla del protocolo, no lo escribas.

## 12. Rendimiento: con criterio en los caminos calientes

- No micro-optimices los caminos fríos (arranque, carga de config y de tablas): ahí gana la
  claridad.
- **Sí tené cuidado en los caminos calientes.** En un servidor son lo que corre **por cada paquete
  o por cada mensaje**: el parser, el despacho, el envío, el log.
- En un camino caliente, evitá:
  - asignaciones de memoria por paquete cuando hay una alternativa simple (un buffer que se reusa);
  - I/O dentro del bucle del hilo de lógica que no sea la consulta que el handler necesita (por
    ejemplo, loguear cada mensaje a disco en nivel `info`);
  - recorridos O(N²) sobre sesiones cuando hay uno O(N) directo.
- **Medí antes de optimizar** (las sub-olas 2.3 y 4.3 son justamente eso). "Caliente" se juzga por
  frecuencia de llamada, no por lo importante que parece el código.

## 13. Cada comportamiento, un test (propia de MuRet)

- Lo que el research fija (un layout, una respuesta, un quirk) tiene un test que lo demuestra.
- Un quirk que se arregla lleva un test que documenta el comportamiento viejo y el nuevo
  ([08-quirks-y-bugs](../Server/DataServer/docs/research/08-quirks-y-bugs.md)).
- Primero el test en rojo, después el código. No se saltea, desactiva ni pone en cuarentena un
  test para que CI quede verde.
