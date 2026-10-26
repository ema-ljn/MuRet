# DataServer en MuRet: roadmap del port

> **Documento de trabajo, 2026-10-25 (decisiones cerradas el 2026-10-26).** Es el plan completo
> para reescribir el DataServer de MU Online 1.02 en MuRet: el objetivo, cómo trabajamos, las
> decisiones de diseño, la estructura del repo y las 9 olas con sus sub-olas. Queda vivo mientras
> haya olas abiertas.
>
> **Qué hace el original** no está acá: está en el research, un archivo por tema, en
> [`Server/DataServer/docs/research/`](../../../Server/DataServer/docs/research/). Este roadmap
> decide qué hace MuRet y en qué orden; el research es la fuente de verdad del comportamiento.
>
> Escrito a partir de [EmaNLJN/mu-server-repack-research-102i](https://github.com/EmaNLJN/mu-server-repack-research-102i)
> (carpeta `dataserver/`), organizado como [sven-n/MuMain](https://github.com/sven-n/MuMain).
> Autor: Emanuel (ema-ljn), con Claude de tutor.

## Objetivo

Un `DataServer` que el GameServer 1.02 del laboratorio **no pueda distinguir del original**. La
compatibilidad se define contra el comportamiento observable: los bytes en el puerto 55960 (framing
C1/C2, los 22 heads) y el texto SQL hacia la base `MuOnline` (las queries y los procedimientos
`MX_*`/`SP_*`). No se define contra el layout interno ni el PE.

**Qué NO es el objetivo:** copiar la reconstrucción `mfc-vc6/` línea por línea. Esa reconstrucción
es una *referencia de comportamiento* (y un futuro oráculo de paridad), no el destino. MuRet es una
implementación nueva, en C++20 moderno, multiplataforma (Windows y Linux), con capas separadas,
RAII y tests.

Es un **proyecto de aprendizaje**: Emanuel escribe la lógica del servidor etapa por etapa y Claude
hace de tutor (estilo `Tutor MuRet`, reviews con el subagente `muret-reviewer`). Claude puede
escribir el andamiaje (CMake, CI, plantillas de docs) cuando se lo pidan.

## Cómo trabajamos

El plan está en **olas** por tema, y cada ola en **sub-olas**. Una sub-ola = un concepto nuevo = un
PR chico que compila, tiene tests de GoogleTest en verde y se puede mostrar. Vamos de lo rudimentario
a lo escalable: primero bytes, después sockets a mano, después IOCP/epoll como el original, y recién
al final el sistema distribuido.

Cada sub-ola sigue este ritmo (lento a propósito):

1. Claude explica el concepto nuevo con un ejemplo chico fuera del proyecto.
2. Claude pasa una mini-spec: qué archivos, qué interfaz pública, qué test tiene que pasar.
3. Emanuel lo escribe y abre un PR.
4. Claude lo revisa como reviewer: bugs, estilo según `CODING_RULES.md`, con preguntas en vez de
   respuestas.
5. Cuando está verde y entendido, se cierra con una nota en `docs/learning/` y una entrada en
   `PROGRESS.md`, y se pasa a la siguiente.

Cada ola tiene su plan detallado en [`docs/superpowers/plans/`](../plans/) antes de arrancarla.

## Scope

Dentro: el DataServer completo (red, cola, dispatcher, los 22 heads, SQL, datos auxiliares),
multiplataforma, con tests. Fuera: el auto-update (`WZ_UM.DLL`), la UI de MFC (se reemplaza por
consola + log a archivo), y el resto de los servidores de MU (ConnectServer, JoinServer,
GameServer), que entrarán como componentes aparte más adelante.

## Decisiones de diseño

La regla guía: el **contrato** (wire + SQL) se respeta al byte; la **implementación** es moderna y
portable. Cada decisión grande tendrá su ADR en `docs/adr/`, como en MuMain. Los números `D-n` se
citan desde los planes de ola.

| # | Tema | Original (VC6) | MuRet | Por qué / qué se aprende |
| --- | --- | --- | --- | --- |
| D-1 | Lenguaje | C++98 + MFC | C++20, sin MFC | RAII, `std::span`, `std::byte`, `enum class`, smart pointers |
| D-2 | Build | `.dsp` de VC6 | CMake 3.25+ con presets (`windows-x64`, `linux-x64`) y vcpkg, como MuMain | un solo build para Windows y Linux |
| D-3 | Red | IOCP crudo de Win32 | IOCP (Windows) y epoll (Linux) a mano detrás de `Network::IoBackend`, siguiendo el original; Asio solo como comparación opcional (sub-ola 3.4) | `OVERLAPPED`, completion ports, proactor vs reactor, pool de workers |
| D-4 | Concurrencia | N workers + cola + 1 consumidor + lock global | N hilos de I/O + cola acotada + 1 hilo de lógica (mismo modelo, sin lock global) | productor/consumidor, `std::mutex`, `condition_variable`, back-pressure |
| D-5 | Base de datos | ODBC directo (`CQuery`) | interfaces de repositorio con dos implementaciones: en memoria (tests) y ODBC crudo con RAII contra el SQL Server de la VM | inyección de dependencias, testear sin base, prepared statements |
| D-6 | Protocolo | structs empaquetados leídos con punteros | structs `#pragma pack(1)` + `Read`/`Write` sobre `std::span` con validación de tamaño | endianness, layout, no confiar en el tamaño que manda el cliente |
| D-7 | UI | ventana MFC con log | consola + log a archivo | separar presentación de lógica |
| D-8 | Tests | smoke test en host | GoogleTest + CTest desde la ola 0 | TDD, tests de contrato con bytes del original |
| D-9 | Config | `.ini` + rutas fijas | archivo de config (TOML o JSON) + argumentos validados | nada hardcodeado |
| D-10 | Quirks | se replican tal cual | se **corrigen**, pero cada uno queda documentado en el research con un test que muestra el comportamiento viejo y el nuevo | ver [08-quirks-y-bugs](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md) |
| D-11 | SQL | `sprintf('%s')` sin escapar | **todo** parametrizado (`?`); las strings verbatim en un solo archivo de constantes | inyección SQL, prepared statements |

## Diseño de destino

```
            GameServer(s) 1.02
                   │ TCP 55960, C1/C2
┌──────────────────▼──────────────────────────────────────┐
│ Network   IoBackend (IOCP | epoll) · Session · FrameParser│  N hilos de I/O
└──────────────────┬──────────────────────────────────────┘
                   │ paquete completo + id de sesión
┌──────────────────▼──────────────────────────────────────┐
│ Dispatch  MessageQueue (acotada) → Dispatcher (head→fn)  │  1 hilo de lógica
└──────────────────┬──────────────────────────────────────┘
┌──────────────────▼──────────────────────────────────────┐
│ Handlers  Account · Character · Warehouse · Guild · Misc │──► respuesta por Session
└──────────────────┬──────────────────────────────────────┘
                   │ interfaces de repositorio
┌──────────────────▼──────────────────────────────────────┐
│ Database  Memory (tests) | Odbc (SQL Server, VM2003)     │
└─────────────────────────────────────────────────────────┘
```

El paquete entra por Network, pasa por la cola al único hilo de lógica, el handler consulta la base
por una interfaz, y la respuesta vuelve por la Session de esa conexión. Regla de capas (como la
regla 5 de MuMain): **Network** no sabe de SQL, **Handlers** no tocan sockets, **Database** no
conoce paquetes. Solo `main.cpp` conoce a todos. Los namespaces llevan el componente adelante
(`DataServer::Network`) para no chocar cuando entren otros servidores.

El modelo de hilos y los tres invariantes que hay que conservar (orden por conexión, orden total,
exclusión de handlers) están en
[03-hilos-y-concurrencia](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md).

## Estructura del repo

Copiamos la forma de MuMain. El repo crece de a componentes: el DataServer vive en
`Server/DataServer/`; después se suman otros al lado (`Server/ConnectServer/`, …). Lo compartido va
a `Server/Common/` recién cuando aparezca el segundo que lo necesite.

```
MuRet/
  README.md                  qué es el repo, componentes y estado
  AGENTS.md / CLAUDE.md      entrada para humanos y agentes
  CMakeLists.txt             raíz: opciones comunes + add_subdirectory(Server/DataServer)
  CMakePresets.json          windows-x64, linux-x64, debug/release
  vcpkg.json                 dependencias (gtest; el resto cuando haga falta)
  cmake/                     módulos compartidos (warnings, sanitizers)
  docs/
    CODING_RULES.md          adaptado de MuMain
    adr/                     decisiones globales (0001 contrato, 0002 IOCP/epoll a mano, …)
    learning/                PROGRESS.md + notas por etapa
    superpowers/
      specs/                 este roadmap
      plans/                 un plan por ola
  Server/
    Common/                  (más adelante) código compartido
    DataServer/
      CMakeLists.txt         target DataServer + DataServerTests
      README.md              cómo compilar, correr y configurar
      docs/
        research/            QUÉ HACE EL ORIGINAL (01..10, fuente de verdad del comportamiento)
      src/
        main.cpp             arma las piezas y arranca
        Core/                Config, Log, errores        DataServer::Core
        Network/             Server, Session, FrameParser DataServer::Network
        Protocol/            Heads, structs               DataServer::Protocol
        Dispatch/            MessageQueue, Dispatcher     DataServer::Dispatch
        Handlers/            Account, Character, Warehouse, Guild, Misc
        Database/            interfaces + Memory/ + Odbc/ DataServer::Database
      tests/
        protocol/ dispatch/ handlers/ database/ integration/
  tools/
    packet-replay/           reproduce capturas del GS real contra MuRet
```

## Research: qué hace el original

Antes de portar hace falta saber con precisión qué hace el original, incluidos sus errores. Está en
[`Server/DataServer/docs/research/`](../../../Server/DataServer/docs/research/), un archivo por tema,
con direcciones del binario, layouts byte a byte, máquinas de estado, diagramas y niveles de
confianza (Verificado / *inferido* / **Análisis** / *seam*).

| # | Documento | Qué responde |
| --- | --- | --- |
| 01 | [Binario y método](../../../Server/DataServer/docs/research/01-binario-y-metodo.md) | qué binario es, cómo se analizó, cómo verificar un dato |
| 02 | [Arranque](../../../Server/DataServer/docs/research/02-arranque.md) | del `.exe` lanzado hasta que escucha |
| 03 | [Hilos y concurrencia](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md) | hilos, locks, orden de locks, carreras, cotas |
| 04 | [Red](../../../Server/DataServer/docs/research/04-red.md) | framing C1/C2, parser, tabla de conexiones, IOCP, envío |
| 05 | [Protocolo](../../../Server/DataServer/docs/research/05-protocolo.md) | los 22 heads, request y respuesta |
| 06 | [SQL y base](../../../Server/DataServer/docs/research/06-sql-y-base.md) | cada sentencia verbatim, el esquema, `CQuery` |
| 07 | [Datos auxiliares](../../../Server/DataServer/docs/research/07-datos-auxiliares.md) | stats por clase, blobs, items, filtros de nombres, XOR, caché de gremios |
| 08 | [Quirks y bugs](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md) | catálogo numerado de defectos y qué hace MuRet |
| 09 | [Discrepancias](../../../Server/DataServer/docs/research/09-discrepancias.md) | dónde se contradicen los docs y cuál es el dato bueno |
| 10 | [Pendientes](../../../Server/DataServer/docs/research/10-pendientes.md) | huecos de conocimiento y cómo cerrarlos |

Cada test de contrato de MuRet cita la sección del research que fija.

## Plan por olas

Cada sub-ola es un PR chico. "Terminada cuando" es la condición objetiva de cierre.

| Ola | Tema | Sub-olas (un PR cada una) | Conceptos | Research |
| --- | --- | --- | --- | --- |
| 0 | Fundaciones | 0.1 CMake raíz + `Server/DataServer` + presets + este roadmap y el plan de la ola 0 · 0.2 vcpkg + GoogleTest + CTest + CI (Windows y Linux) · 0.3 docs base: `AGENTS.md`, `CODING_RULES.md`, ADR 0001 y 0002 | compilación, linker, targets, vcpkg, CI | 01 |
| 1 | Protocolo sin red | 1.1 helpers de bytes (`std::byte`, `std::span`, endianness) · 1.2 `FrameParser` C1/C2 · 1.3 structs de paquetes con `static_assert(sizeof)` y `Read`/`Write` | tipos, RAII, layout, TDD | 04, 05 |
| 2 | Sockets a mano | 2.1 portabilidad Winsock/BSD · 2.2 servidor TCP bloqueante de un cliente · 2.3 un hilo por conexión y por qué no escala · 2.4 tabla de 100 sesiones (con id + generación) | sockets, TCP como stream, `#ifdef _WIN32`, `std::thread` | 04 |
| 3 | I/O asíncrono como el original | 3.1 IOCP en Windows · 3.2 epoll en Linux tras `IoBackend` · 3.3 cola de envío por sesión + cierre ordenado (contador de pendientes) · 3.4 *opcional:* el mismo server con Asio | proactor vs reactor, OVERLAPPED, los bugs C-2/C-3/C-4 | 03, 04 |
| 4 | Concurrencia | 4.1 `MessageQueue` acotada (`mutex`+`condition_variable`) · 4.2 hilo de lógica + `Dispatcher` head→fn · 4.3 estrés, back-pressure y TSan | data races, productor/consumidor, invariantes I-1/I-2/I-3 | 03 |
| 5 | Handlers en memoria | 5.1 interfaces de repositorio + impl. en memoria · 5.2 heads 0x00, 0x02, 0x40 · 5.3 cuenta y personajes contra memoria | interfaces, inyección de dependencias | 05, 07 |
| 6 | Base real | 6.1 ODBC crudo con RAII · 6.2 prepared statements y blobs · 6.3 conexión al SQL Server de la VM · 6.4 repositorios ODBC | handles de C con RAII, errores, inyección | 06 |
| 7 | Resto de heads | 7.1 cuenta y personajes · 7.2 baúl y varios · 7.3 gremios (Q-01 arreglado) | blobs, transacciones, atomicidad del serial | 05, 06, 07, 08 |
| 8 | Distribuido | 8.1 el GS real contra MuRet · 8.2 `packet-replay` y paridad byte a byte · 8.3 timeouts, reconexión, idempotencia · 8.4 apagado limpio, métricas, health checks | sistemas distribuidos, observabilidad | 02, 08, 10 |

**Planes detallados, uno por ola** (pasos con checkbox, archivos, qué escribe Emanuel y qué es
andamiaje, preguntas a decidir al empezar). Se escribieron por adelantado y cada uno se revisa al
arrancar su ola:
[0 Fundaciones](../plans/2026-10-26-ola-0-fundaciones.md) ·
[1 Protocolo sin red](../plans/2026-10-26-ola-1-protocolo-sin-red.md) ·
[2 Sockets a mano](../plans/2026-10-26-ola-2-sockets-a-mano.md) ·
[3 I/O asíncrono](../plans/2026-10-26-ola-3-io-asincrono.md) ·
[4 Concurrencia](../plans/2026-10-26-ola-4-concurrencia.md) ·
[5 Handlers en memoria](../plans/2026-10-26-ola-5-handlers-en-memoria.md) ·
[6 Base real](../plans/2026-10-26-ola-6-base-real.md) ·
[7 Resto de heads](../plans/2026-10-26-ola-7-resto-de-heads.md) ·
[8 Distribuido](../plans/2026-10-26-ola-8-distribuido.md).

**"Terminada cuando", por ola:** 0 — `cmake --build` y `ctest` pasan en los dos sistemas. 1 —
pasan los casos del contrato del parser (partido, varios por recv, header inválido, `size == 0`,
dominio de tamaño). 2 — un cliente de prueba manda paquetes y el log los muestra; reuso de slot sin
cruce de datos. 3 — 100 clientes simultáneos sin crash ni leaks (ASan) en Windows y Linux. 4 —
estrés con TSan limpio y los invariantes verificados. 5 — integración login + broadcast entre 2
clientes contra memoria. 6 — los tests de repositorio pasan contra la base de la VM. 7 — todos los
heads implementados, gremios incluidos. 8 — un GS 1.02 entra, crea personaje y juega contra MuRet.

**Base de datos durante el desarrollo:** el SQL Server vive en la VM2003, así que hasta la ola 5
todo se prueba contra el repositorio en memoria, sin base. Desde la ola 6 los tests de integración
se conectan por ODBC al SQL Server de la VM (puerto 1433) y se marcan aparte para que el CI no los
corra.

## Cómo el research se vuelve test

Cada afirmación del research se convierte en un test antes de escribir el código que la cumple. Así
el contrato deja de ser prosa y pasa a ser algo que `ctest` verifica.

| Sección del research | Ola | Tipo de test |
| --- | --- | --- |
| Framing C1/C2 ([04](../../../Server/DataServer/docs/research/04-red.md)) | 1.2 | unitario con bytes literales |
| Layout de requests/respuestas ([05](../../../Server/DataServer/docs/research/05-protocolo.md)) | 1.3 | `static_assert(sizeof)` + round-trip |
| Tabla de 100 conexiones, envío, broadcast ([04](../../../Server/DataServer/docs/research/04-red.md)) | 2.4, 3 | integración con clientes de prueba |
| Bugs de red C-2/C-3/C-4 ([03](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md)) | 3.3 | reproduce el escenario y verifica el arreglo |
| Cola de un consumidor, invariantes ([03](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md)) | 4 | estrés con TSan |
| Heads 0x00, 0x02, 0x40 ([05](../../../Server/DataServer/docs/research/05-protocolo.md)) | 5.2 | integración con repo en memoria |
| SQL verbatim ([06](../../../Server/DataServer/docs/research/06-sql-y-base.md)) | 6–7 | comparar strings contra el catálogo |
| Cuenta, personajes, baúl, gremios | 5.3, 7 | repo en memoria + SQL Server de la VM |
| Quirks ([08](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md)) | donde toque el área | test que muestra viejo vs nuevo |
| Paridad con el original | 8.2 | `packet-replay`: mismo request a ambos, comparar bytes |

Cuando un test de paridad difiere, hay tres respuestas: bug de MuRet, quirk corregido a propósito
(tiene que estar en [08](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md)), o un dato
que el research todavía no tenía (se agrega con su evidencia, ver
[10](../../../Server/DataServer/docs/research/10-pendientes.md)).

## Preguntas cerradas (2026-10-26)

- **Red:** IOCP en Windows y epoll en Linux a mano, siguiendo el original; Asio queda como sub-ola
  opcional 3.4.
- **Tests:** GoogleTest con CTest.
- **Forma de trabajo:** por olas y sub-olas; cada sub-ola es un concepto y un PR chico que escribe
  Emanuel y revisa Claude.
- **Superpowers:** el roadmap va a `docs/superpowers/specs/`, un plan por ola en
  `docs/superpowers/plans/`.
- **Base de datos:** el SQL Server de la VM2003; unit tests con repo en memoria, integración contra
  la VM desde la ola 6, fuera de CI.
- **Quirks:** se corrigen y se documentan (D-10).

## Preguntas abiertas

- Formato del archivo de config: TOML o JSON (se decide en la ola 2, con su ADR).
- Librería de log: una propia o una externa (spdlog) (ola 0.2 / 2).
- Si algún quirk del original resulta que el GS real necesita (ninguno encontrado hasta ahora); si
  aparece, se mantiene detrás de un flag de compatibilidad y se documenta.
- Cerrar los *seam* del research antes de las olas 5 y 7 (ver
  [10-pendientes](../../../Server/DataServer/docs/research/10-pendientes.md)).
