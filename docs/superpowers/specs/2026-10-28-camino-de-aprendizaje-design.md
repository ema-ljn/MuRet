# Camino de aprendizaje de MuRet: C++ moderno, MU y sistemas distribuidos

> **Documento de diseño, 2026-10-28.** Sale de una sesión de brainstorming entre Emanuel y Claude.
> Define **cómo se aprende** en MuRet: qué se estudia antes de cada ola, en qué formato, con qué
> acompañamiento, y qué olas nuevas se suman después de la 8 para sistemas distribuidos e
> ingeniería inversa. No reemplaza al
> [roadmap del DataServer](2026-10-25-dataserver-port-design.md), que sigue decidiendo **qué hace**
> MuRet; lo complementa y lo extiende.
> Autor: Emanuel (ema-ljn), con Claude de tutor.

## El problema

El roadmap y las guías de [`docs/learning/`](../../learning/README.md) se escribieron suponiendo
que Emanuel ya sabía C++ moderno. Al revisarlos contra su punto de partida aparecen cinco huecos:

1. **Las guías asumen C++ que todavía no vio.** Desde la ola 1 los ejemplos usan cosas que nunca se
   explican:

   | Ola | C++ que asume la guía |
   | --- | --- |
   | 1 | `std::span`, `std::byte`, `std::array`, `std::bit_cast`, `reinterpret_cast` |
   | 2 | clases con destructor, movimiento, `= delete`, `noexcept`, `std::optional`, `std::jthread` |
   | 4 | lambdas (`[&]`), `std::function`, `std::unique_lock`, `std::condition_variable` |
   | 5 | `virtual` / `override`, `std::expected` |
   | 7 | `std::variant` / `std::visit`, templates con *traits* (`std::is_same_v`, `std::decay_t`) |

   Así, la regla de oro del proyecto (poder explicar cada línea) se rompe en la primera ola: el
   camino de menor resistencia sería copiar.
2. **Sistemas distribuidos llega tarde y se queda corto.** Está casi todo en la ola 8, después de
   siete olas de C++, y cubre "un servicio en red que aguanta fallas" (timeouts, reintentos,
   idempotencia, health checks). No hay réplicas, consenso, relojes lógicos ni particiones: la
   topología es una estrella (N GameServers → 1 DataServer → 1 SQL Server).
3. **MU es solo el DataServer.** El [repo de research](https://github.com/EmaNLJN/mu-server-repack-research-102i)
   tiene mucho más: el JoinServer 1.02d decompilado entero con su protocolo observado en vivo, ExDB,
   el ConnectServer a medio investigar y el GameServer. El MU 1.02 del laboratorio ya es un sistema
   distribuido, y el roadmap no lo aprovecha.
4. **El acompañamiento llega al final.** Hay un estilo de tutor y un revisor de PRs, pero nada
   entre "arranco la sub-ola" y "abro el PR", que es donde más se aprende y más se traba uno.
5. **Las guías son mucho texto.** Explican bien, pero se leen de corrido. Para aprender a fondo
   hace falta hacer: predecir, romper, mirar la memoria, encontrar bugs.

## El punto de partida del alumno

- Sabe control de flujo y la sintaxis básica. Viene de la programación web.
- No conoce todavía: C++11 en adelante, clases, punteros, lo propio de C++ ni la biblioteca
  estándar.
- Sistemas distribuidos: base básica.
- Proyecto personal, **sin fecha**. El objetivo es aprender tres cosas: C++ moderno, MU (incluido su
  binario) y sistemas distribuidos.

## Objetivo y criterio de éxito

Aprender las tres cosas **construyendo**, a su ritmo y siempre con piezas de MU: nada de ejercicios
genéricos que no terminen en el servidor o en entenderlo mejor.

Se cumplió cuando Emanuel puede:

- **C++:** escribir sin copiar un programa chico de C++ moderno (clases RAII, contenedores, smart
  pointers, lambdas, templates) y explicar cada línea.
- **Distribuidos:** explicar y construir las piezas clave: modelos de falla, orden de eventos,
  detección de fallas, réplicas y consenso.
- **MU:** verificar contra el binario un dato del research, y documentar uno nuevo con su
  evidencia.

## Decisiones

| # | Tema | Decisión | Por qué |
| --- | --- | --- | --- |
| L-1 | Forma del camino | **Justo a tiempo:** antes de cada ola, una sub-ola **N.0 de fundamentos** con exactamente lo que esa ola necesita | cada concepto se usa enseguida en código de MU. Se descartaron "nivelación primero" (meses sin tocar el servidor, teoría que se olvida) y "dos carriles en paralelo" (distribuidos con C++ flojo; dos contextos a la vez) |
| L-2 | Cómo se explica | desde lo que el concepto **es en ciencias de la computación**, con una referencia de texto. **Sin analogías web** | decisión de Emanuel: entender el concepto, no traducirlo |
| L-3 | Construir vs usar | se construye desde cero, se compara con el estándar y **en el servidor se usa el estándar** | entender `std::span` escribiendo una vista propia, pero no mantener una copia casera. Excepción: lo que el estándar no trae y el servidor necesita (`UniqueSocket`, `FrameParser`, el `Expected` de C++20) |
| L-4 | Dónde vive el código de las N.0 | `labs/` en la raíz, target `MuRetLabs` con sus tests; el servidor **no** lo enlaza | separar el código para aprender del que se entrega |
| L-5 | Formato | interactivo: una idea corta, una actividad; calentamiento, misión y jefe; ejemplos del DataServer | decisión de Emanuel: que no sea todo texto, que sea didáctico y divertido |
| L-6 | Acompañamiento | `/tutor` a pedido + puntos de control fijos en cada sub-ola | acompaña sin cortar la concentración. Se descartaron, por ahora, la pasada automática en cada commit y el "modo pareja" con `/loop` (más costo, más interrupciones); se pueden sumar después |
| L-7 | Distribuidos | **fase A:** los servidores reales de MU 1.02, con el original como oráculo; **fase B:** "MU mejorado" (réplicas, consenso, particiones) sin romper el contrato con el GameServer 1.02 | A da un oráculo; B da lo que A no tiene |
| L-8 | Ingeniería inversa | **carril R completo:** se aprende a leer el binario de a poco en cada N.0 y se hace research real antes de cada servidor nuevo | aprender MU incluye entender el original por dentro |
| L-9 | Alcance del carril R | protocolo y comportamiento, para reimplementar. **Queda afuera** romper protecciones (VM de `MyDll`, Themida, licencias) | no aporta a MuRet |
| L-10 | Cuándo se escriben las guías N.0 | **justo antes de cada ola**, a partir de esta spec y de `PROGRESS.md` | se ajustan a lo que el alumno ya domina; esta spec fija qué tiene que cubrir cada una |
| L-11 | Seguimiento | niveles por concepto (usar, explicar, romper, transferir) y repaso espaciado | "entender a fondo" tiene que poder medirse |
| L-12 | Front-ends | el borrador de "ola 9" de [front-ends](2026-10-26-frontends-cli-gui-futuro.md) pasa a llamarse **ola F** (opcional, sin lugar fijo) | los números 9 en adelante quedan para distribuidos |

## Estructura de una ola

```text
Ola N
 ├─ N.0  Fundamentos: la teoría y el C++ que la ola necesita
 │    1. el concepto formal: qué es en ciencias de la computación, con su referencia
 │    2. lo construís vos desde cero, en labs/, con tests y datos de MU
 │    3. lo comparás con el estándar y anotás las diferencias
 ├─ N.R  (solo en servidores nuevos) research del original con el binario
 ├─ N.1 … N.k  las sub-olas del plan de la ola, usando la versión del estándar
 └─ cierre: explicar, romper, transferir; actualizar PROGRESS.md
```

Una N.0 es una sub-ola como cualquier otra: un concepto, un PR chico, tests en verde. Su código
vive en `labs/` y compila en Windows y en Linux. Cuando un tema es de un solo sistema (IOCP, epoll),
el lab se queda con la parte portable y lo propio de cada sistema se hace como experimento
documentado.

## Formato de una sesión de N.0

**Ritmo:** una idea corta (diez líneas de teoría como mucho), una actividad, charlar lo que salió y
la siguiente idea. Nunca más de diez líneas de teoría sin una actividad en el medio.

**Actividades**, todas con material de MU:

| Actividad | Qué hace el alumno | Ejemplo del DataServer |
| --- | --- | --- |
| Predecí y comprobá | dice qué va a pasar antes de correrlo | llegan `C2 03 BC 06`: ¿cuántos bytes tiene el paquete entero? |
| Detective de bytes | decodifica a mano un volcado hexadecimal | el login `0x00`: clase en `+3`, puerto en `+4`, nombre en `+6` ([05](../../../Server/DataServer/docs/research/05-protocolo.md)) |
| Cazá el bug del original | encuentra el defecto antes de que se lo digan | Q-02, Q-07, Q-10 ([08](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md)), C-1 a C-7 ([03](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md)) |
| Rompelo a propósito | provoca la falla y lee el reporte de ASan, TSan o UBSan | el índice del ring del log sin atomicidad (C-5) |
| Mirá la memoria | debugger o Compiler Explorer | su código compilado al lado del `cmp eax,0x1388` del original |
| Mirá el assembly (carril R) | lee el binario original en Ghidra | ver [carril R](#carril-r-ingeniería-inversa) |
| Laboratorio visual | una página interactiva donde mueve los controles | el parser paso a paso; dos hilos que avanza a mano |
| Juego | diseña algo y el tutor lo rompe con un contraejemplo | los dos generales (8.0) |

**Regla de los laboratorios visuales:** muestran el fenómeno, no la solución. Primero se predice,
después se ve. No traen el algoritmo que el alumno tiene que escribir.

**Tres niveles por N.0:**

- **Calentamiento:** de 5 a 10 minutos, para entrar en tema.
- **Misión:** lo que se construye en `labs/`, empezando por el test que falla.
- **Jefe:** un quirk del original o un caso borde difícil, con un test que lo demuestra.

**Ejemplo, la 1.0:**

| # | Tipo | Qué pasa |
| --- | --- | --- |
| 1 | idea | la memoria es un arreglo de bytes; un `uint16_t` ocupa dos, y su orden depende de la máquina |
| 2 | predecí | `C2 03 BC 06` es la respuesta de `0x06` (cargar personaje): ¿cuánto mide el paquete? |
| 3 | mirá la memoria | guardar `0x03BC` en un `uint16_t` y mirar sus bytes en el debugger: ¿mismo orden que en el wire? |
| 4 | idea | endianness: por qué el tamaño de un C2 viaja en big-endian y x86 es little-endian |
| 5 | misión | `ByteView` y `read_u16_be` en `labs/`, empezando por el test que falla |
| 6 | jefe | Q-02: el parser acepta un C2 de hasta `0x2000` bytes, pero el consumidor lo copia a un buffer en la pila de `0x800`. ¿Qué pasa con uno de `0x900`? Escribir el test que lo demuestra |

## Acompañamiento: el tutor al lado

- **El estilo `Tutor MuRet` está activo por defecto** (`.claude/settings.json`). Toda sesión de
  Claude Code en el repo arranca como tutor y lee `PROGRESS.md`, que hace de memoria entre
  sesiones.
- **`/tutor`, a pedido:** mira los cambios sin commitear, compila, corre los tests y devuelve tres
  cosas: algo concreto que está bien, una pregunta que apunta al problema más importante y un
  riesgo. No arregla el código: pregunta y escala las pistas solo si el alumno se traba (reglas 4 y
  7 del estilo). Se usa desde la terminal o desde la extensión de VS Code, seleccionando el código
  que genera dudas.
- **Puntos de control fijos en cada sub-ola:**

  | Momento | Qué hace el tutor |
  | --- | --- |
  | antes de empezar | explica el concepto y hace una pregunta de diagnóstico |
  | primer test en rojo | pide predecir por qué falla, antes de correrlo |
  | test en verde | pide explicar por qué ahora pasa |
  | PR | revisión con `muret-reviewer`, que también cubre `labs/`, `tools/` y el research |
  | cierre | romperlo a propósito, aplicarlo en otro lado, actualizar `PROGRESS.md` |

- **Límite conocido:** Claude Code no ve lo que se tipea en el editor; sus automatismos reaccionan a
  lo que hace Claude. Por eso el acompañamiento se dispara a pedido o en los puntos fijos.

## Contenido de las N.0, olas 0 a 8

Cada bloque fija qué cubre la guía de la N.0, que se escribe justo antes de la ola (L-10). Las
lecturas son el punto de partida; la guía puede sumar otras.

### 0.0 Del código al programa

Va **ahora**, antes de cerrar la 0.1.

- **Teoría:** unidades de traducción, símbolos, enlace interno y externo, la regla de una
  definición (ODR), archivos objeto, bibliotecas estáticas y dinámicas.
- **C++:** declaración vs definición, headers, `namespace`, `static` e `inline`, `const` y
  `constexpr`, paso por valor y por referencia.
- **Misión:** una biblioteca estática con `header_size(first_byte)`, que devuelve 2 para C1 y 3
  para C2 ([04 § framing](../../../Server/DataServer/docs/research/04-red.md#el-framing-c1c2)).
  Romperla a propósito (símbolo sin definir, definición duplicada) y mirar los símbolos con `nm` o
  `dumpbin`. Acá nace `labs/`.
- **Jefe:** una violación de la ODR que compila y enlaza sin error pero se comporta distinto según
  el orden de enlace.
- **R:** el formato PE (secciones, imports, ImageBase), contra la identidad del binario en
  [01](../../../Server/DataServer/docs/research/01-binario-y-metodo.md).
- **Lectura:** *CS:APP* (Bryant y O'Hallaron), capítulo 7.

### 1.0 Memoria y representación de datos

- **Teoría:** la memoria como un arreglo de bytes con dirección; representación de objetos; enteros
  sin signo y en complemento a dos; endianness; alineación y relleno. Para la 1.2, autómatas
  finitos.
- **C++:** `<cstdint>`, punteros, referencias, arrays de C vs `std::array`, `std::vector`, `sizeof`
  y `alignof`, los casts y por qué `reinterpret_cast` es peligroso, `std::byte`, comportamiento
  indefinido. Cadenas: los nombres de largo fijo del wire (el Id de 10 bytes en `+3`),
  `std::string` y `std::string_view`.
- **Misión:** `ByteView` propia (puntero y largo, con acceso que chequea límites) y `read_u16_be` a
  mano con desplazamientos de bits; leer cabeceras C1/C2 con bytes literales del research.
  Comparar con `std::span` y `std::endian` (`std::byteswap` es C++23).
- **Jefe:** Q-02 (ver el ejemplo de arriba).
- **R:** x86 básico (registros, `mov`, `cmp`, saltos) y primeros pasos en Ghidra: el
  `cmp eax,0x1388` del tope de la cola y el parser en `0x403010`.
- **Lectura:** *CS:APP*, capítulos 2 y 3.9; Sipser, *Introduction to the Theory of Computation*,
  capítulo 1.

### 2.0 Recursos, objetos y el sistema operativo

- **Teoría:** proceso y espacio de direcciones, llamadas al sistema, la frontera entre usuario y
  kernel; un descriptor de archivo es un índice a una tabla del kernel; TCP como máquina de
  estados (*handshake*, `FIN`, `TIME_WAIT`).
- **C++:** clases e invariantes de clase, constructores y destructores, duración automática,
  dinámica y estática, copia vs movimiento, `std::move`, la regla de 0/3/5, `= delete`,
  `noexcept`, templates de clase.
- **Misión:** `UniqueHandle<Traits>` propio. Un test con un `close` falso verifica que cada handle
  se cierra exactamente una vez; otro, con `static_assert`, que copiarlo no compila. Comparar con
  `std::unique_ptr<T, Deleter>`.
- **Jefe:** C-4 en versión simple: en una tabla de 100 slots sin generación, un evento atrasado de
  una conexión ya cerrada (en el original, una completion vieja) actúa sobre la conexión nueva del
  slot reusado. Predecir qué se cruza y escribir el test que lo muestra.
- **R:** convenciones de llamada (`cdecl`, `stdcall`, `thiscall`), la pila, llamadas a Winsock por
  la tabla de imports.
- **Lectura:** *OSTEP* (Arpaci-Dusseau), procesos; Kerrisk, *The Linux Programming Interface*,
  descriptores y sockets; Stroustrup, *A Tour of C++*.

### 3.0 E/S y el kernel

- **Teoría:** E/S bloqueante y no bloqueante; multiplexación por disponibilidad (`poll`, `epoll`)
  vs por finalización (IOCP); costo del cambio de contexto; el problema C10K; qué pasa con un
  buffer prestado al kernel.
- **C++:** punteros a función, lambdas y capturas (y las referencias que quedan colgando),
  `std::function`, `std::chrono`, `std::shared_ptr` y `std::weak_ptr` para buffers que tienen que
  vivir hasta su notificación de finalización.
- **Misión:** un buffer circular de capacidad fija para acumular lo que llega por `recv`. Comparar
  con el buffer lineal de `0x2000` del original
  ([04 § parser](../../../Server/DataServer/docs/research/04-red.md)).
- **Jefe:** C-3: un solo `OVERLAPPED` de envío, reusado, apuntando a la pila. Encontrar por qué se
  corrompe un envío cuando el buffer del socket está lleno.
- **R:** el bucle de los workers de IOCP en `0x402e80`.
- **Lectura:** *OSTEP*, "Event-based Concurrency"; *TLPI*, capítulo 63; Kegel, "The C10K problem".

### 4.0 Concurrencia y el modelo de memoria

- **Teoría:** condición de carrera vs data race; exclusión mutua; las condiciones de Coffman;
  *happens-before*; secuencialmente consistente vs acquire/release; linealizabilidad; monitores.
- **C++:** `std::jthread`, `std::mutex`, `std::scoped_lock`, `std::unique_lock`,
  `std::condition_variable` y los despertares espurios, `std::atomic`, `std::memory_order`.
- **Misión:** reproducir C-5 (el ring de 30 líneas del log con un índice que se incrementa sin
  atomicidad), verlo con TSan y arreglarlo de tres formas: mutex, atomic y confinamiento a un hilo.
  Un spinlock propio con `std::atomic_flag`, comparado con `std::mutex` bajo contención.
- **Jefe:** C-1: la inversión del orden de locks entre gremios y global. Encontrar el deadlock a
  partir de la tabla de locks y escribir el test que lo provoca (con timeout).
- **R:** `CRITICAL_SECTION` en assembly, contra la tabla de locks de
  [03](../../../Server/DataServer/docs/research/03-hilos-y-concurrencia.md#locks).
- **Lectura:** *OSTEP* (locks, variables de condición, bugs de concurrencia); Williams, *C++
  Concurrency in Action*, capítulos 3 a 5; Lamport, "Time, Clocks, and the Ordering of Events in a
  Distributed System" (1978), de donde sale *happens-before*.

### 5.0 Abstracción, polimorfismo y errores

- **Teoría:** tipos abstractos de datos; despacho dinámico (vtable) vs estático (templates);
  inversión de dependencias; tres formas de reportar errores: códigos, excepciones y tipos
  resultado.
- **C++:** herencia, `virtual`, `override`, `final`, destructor virtual, clases abstractas,
  `concept` (C++20), excepciones y garantías ante excepciones, `std::unordered_map`, iteradores y
  algoritmos.
- **Actividad:** predecir cuánto crece `sizeof` al agregar un `virtual` y buscar el puntero a la
  vtable en el debugger.
- **Misión:** una vtable a mano (struct de punteros a función) para un repositorio de cuentas en
  memoria; la misma interfaz con `virtual` y con un `concept`. Un `Expected<T, E>` propio, que
  alimenta la pregunta 1 del [plan de la ola 5](../plans/2026-10-26-ola-5-handlers-en-memoria.md).
- **Jefe:** Q-05: el `-103` de la query de arranque viaja como `ItemCount`. Test que muestra que con
  `Expected` el error ya no se confunde con un número.
- **R:** vtables y RTTI de una clase MFC del original, al lado de la propia.
- **Lectura:** Stroustrup, *A Tour of C++* (clases, templates, concepts).

### 6.0 Bases relacionales y APIs de C

- **Teoría:** modelo relacional, ACID, niveles de aislamiento y sus anomalías; la inyección SQL como
  confusión entre código y datos; sentencias preparadas.
- **C++:** cómo se usa una API de C (handles opacos, códigos de retorno); RAII sobre handles
  anidados (entorno, conexión, sentencia); orden de destrucción de los miembros.
- **Actividad:** dos consolas de `sqlcmd` contra la base de la VM para provocar una lectura no
  repetible.
- **Misión:** RAII sobre una API de C falsa que registra el orden de apertura y cierre; los tests
  fallan si la conexión se cierra antes que la sentencia.
- **Jefe:** Q-07: con el `sprintf('%s')` del original y un nombre de personaje armado a propósito,
  predecir qué SQL se ejecuta; test que muestra que con parámetros no pasa.
- **R:** imports por ordinal y strings de SQL, contra el catálogo de
  [06](../../../Server/DataServer/docs/research/06-sql-y-base.md).
- **Lectura:** Kleppmann, *Designing Data-Intensive Applications* (DDIA), capítulo 7.

### 7.0 Tipos suma, transacciones e identificadores únicos

- **Teoría:** tipos algebraicos (producto y suma); atomicidad; bloqueo en dos fases vs MVCC;
  generación de identificadores únicos con varios clientes.
- **C++:** `std::variant`, `std::visit`, `if constexpr`, type traits, `constexpr`.
- **Actividad:** laboratorio visual con dos transacciones sobre el dinero de la bodega, intercaladas
  paso a paso hasta que aparece la actualización perdida.
- **Misión:** una unión etiquetada propia (`enum` + `union` + destructor a mano) para el resultado de
  un handler (respuesta, error o nada), y después con `std::variant`. Un generador de seriales de
  items por rangos reservados, con un test de varios hilos que verifica que no hay duplicados.
- **Jefe:** encontrar por qué los gremios del original nunca funcionan antes de leer Q-01.
- **R:** el `switch` del dispatcher de heads en `0x40b190` como tabla de saltos; structs
  reconstruidos a partir de offsets.
- **Lectura:** DDIA, capítulo 7; cppreference de `std::variant`.

### 8.0 Fundamentos de sistemas distribuidos

- **Teoría:** modelos de sistema (síncrono, asíncrono, parcialmente síncrono); modelos de falla
  (caída, omisión, bizantino); por qué un timeout no distingue "lento" de "muerto"; entrega como
  mucho una vez vs al menos una vez; los dos generales; el argumento extremo a extremo.
- **C++:** qué se puede hacer dentro de un manejador de señal, `std::atomic` libre de locks,
  `std::chrono` para timeouts y backoff, `<random>` para el jitter.
- **Actividad (juego):** los dos generales: el alumno propone protocolos y el tutor rompe cada uno
  con un caso concreto.
- **Misión:** un proxy TCP de fallas propio (`tools/fault-proxy/`) entre el GameServer y el
  DataServer que demora, corta o duplica frames C1/C2 con el `FrameParser` de la ola 1.
- **Jefe:** el `0x07` no tiene ACK ([05](../../../Server/DataServer/docs/research/05-protocolo.md)).
  Con el proxy, cortar la conexión justo después de un `0x07`: ¿el GameServer puede saber si se
  guardó? ¿Qué cambiaría si se pudiera tocar el protocolo, y por qué no se puede?
- **R:** análisis dinámico en la VM: un head capturado en vivo.
- **Lectura:** DDIA, capítulo 8; los apuntes de Kleppmann del curso de sistemas distribuidos de
  Cambridge; Saltzer, Reed y Clark, "End-to-End Arguments in System Design" (1984).

## Olas nuevas de distribuidos

El MU 1.02 del laboratorio ya es un sistema distribuido. Según el research: el GameServer habla
con el JoinServer (TCP 55970) y con el DataServer (55960), ExDB escucha en 55906 y el JoinServer
avisa al ConnectServer por UDP (55557). Cada ola nueva tiene su N.0 con el formato de arriba.

### Fase A: MU tal cual, con el original como oráculo

**Ola 9 · JoinServer**

- **9.R:** verificar el research del JoinServer
  ([`joinserver/`](https://github.com/EmaNLJN/mu-server-repack-research-102i/tree/main/joinserver))
  contra el binario, cerrar los pendientes de su `docs/06_handoff_pendientes.md` y llevarlo a
  `Server/JoinServer/docs/research/` con la estructura del DataServer.
- **Teoría:** autenticación centralizada; exclusión mutua distribuida (que una cuenta esté adentro
  es un lock que guarda un coordinador: el original usa un `std::map` de sesiones contra el doble
  login); qué pasa con esos locks cuando muere el GameServer que los tiene; *leases* y *fencing
  tokens* (vuelve la idea de la 2.4).
- **C++:** diseño de bibliotecas: acá nace `Server/Common/` con lo que comparten DataServer y
  JoinServer, con una interfaz estable.
- **Jefe:** encontrar el bug documentado del contador de GameServers, que pasa por debajo de cero
  cuando se cierra uno.

**Ola 10 · ConnectServer y UDP**

- **10.R:** completar el research del ConnectServer (hoy solo tiene la clasificación de funciones
  y el desempaque): su protocolo y lo que recibe por UDP.
- **Teoría:** datagramas sin conexión y con pérdida; descubrimiento de servicios; estado que se
  reconstruye solo con *heartbeats*; detectores de falla (detectar rápido vs equivocarse); elegir
  servidor por la carga reportada.
- **C++:** sockets UDP; temporizadores con `std::priority_queue` o una rueda de temporizadores
  propia.
- **Jefe:** un GameServer se muere sin avisar: medir cuánto tiempo lo sigue listando el
  ConnectServer y ajustar el timeout.

**ExDB** queda como ola opcional, sin número, cuando el research confirme su rol.

### Fase B: "MU mejorado", sin romper el contrato con el GameServer 1.02

**Ola 11 · Simulador determinista y relojes lógicos**

- **Teoría:** simulación de eventos discretos; determinismo y semillas; relojes de Lamport y
  relojes vectoriales; orden causal.
- **C++:** generadores con semilla (`std::mt19937`); inyección de reloj y red a través de
  interfaces (lo de la ola 5); corrutinas de C++20 como opción para escribir cada nodo simulado.
- **Misión:** GameServer, JoinServer y DataServer en un solo proceso, con red y reloj simulados que
  se reproducen igual con la misma semilla.
- **Jefe:** reproducir con una semilla un doble login que en la red real pasa una vez cada mil.

**Ola 12 · Failover y leases**

- **Teoría:** replicación activo-pasivo; *leases* para elegir al principal; *split-brain*;
  *fencing* en la base, que rechaza escrituras de un principal viejo; durabilidad (qué garantiza
  `fsync`).
- **C++:** snapshots del estado y su serialización; persistencia en disco.
- **Misión:** un DataServer activo y uno de respaldo. El GameServer 1.02 sigue viendo una sola IP:
  cómo se logra, y qué punto único de falla nuevo aparece, es parte de la ola.
- **Jefe:** con *split-brain*, dos DataServers aceptan el mismo `0x09` (cerrar bodega). ¿Se puede
  duplicar un item? Demostrarlo en el simulador y después impedirlo.

**Ola 13 · Consenso**

- **Teoría:** replicación de máquinas de estado; Raft (elección de líder, replicación del log,
  seguridad); FLP y por qué los timeouts hacen posible el consenso en la práctica.
- **Misión:** Raft desde cero, probado en el simulador de la ola 11. Después, un almacenamiento
  replicado detrás de la interfaz `Database` de la ola 5; por ejemplo, un generador de seriales de
  items que no repite nunca aunque se caiga un nodo.
- **Jefe:** el caso de la figura 8 del paper de Raft (Ongaro y Ousterhout, 2014).

**Ola 14 (opcional) · Particiones y transacciones distribuidas**

- **Teoría:** *consistent hashing*; *two-phase commit* (2PC) y por qué se bloquea; linealizabilidad
  vs consistencia eventual; CAP y PACELC.
- **Misión:** repartir las cuentas entre varios DataServers; mover un item entre personajes que
  están en DataServers distintos con 2PC.
- **Jefe:** el coordinador de 2PC se cae entre *prepare* y *commit*.

**Lectura de toda la fase B:** DDIA, capítulos 5, 8 y 9; los apuntes de Kleppmann; el paper de
Raft.

### Ola F · Front-ends (opcional)

El borrador de [front-ends](2026-10-26-frontends-cli-gui-futuro.md) (sub-olas F.1 a F.6), sin
cambios de contenido. Se puede hacer en cualquier momento después de la ola 8.

## Carril R: ingeniería inversa

Antes de investigar un servidor nuevo hace falta saber leer assembly. Como el C++, se aprende de a
poco: una actividad de "mirá el assembly" en cada N.0, verificando datos del research del
DataServer que el alumno ya conoce.

| Ola | Qué se aprende a leer | Qué se verifica en el original |
| --- | --- | --- |
| 0.0 | formato PE: secciones, imports, ImageBase | la identidad del binario ([01](../../../Server/DataServer/docs/research/01-binario-y-metodo.md)) |
| 1.0 | x86 básico; primeros pasos en Ghidra | el `cmp eax,0x1388` del tope de la cola; el parser en `0x403010` |
| 2.0 | convenciones de llamada, la pila, llamadas por la tabla de imports | las llamadas a Winsock |
| 3.0 | patrones de IOCP | el bucle de los workers en `0x402e80` |
| 4.0 | `CRITICAL_SECTION` en assembly | la tabla de locks de 03 |
| 5.0 | vtables y RTTI | una clase MFC del original |
| 6.0 | imports por ordinal, strings | una sentencia del catálogo de 06 |
| 7.0 | `switch` como tabla de saltos, structs a partir de offsets | el dispatcher en `0x40b190` |
| 8.0 | análisis dinámico en la VM | un head capturado en vivo |
| 9.R | research real | pendientes abiertos del JoinServer |
| 10.R | research real | protocolo y UDP del ConnectServer |

- **Dónde queda:** el research de cada servidor vive en `Server/<Servidor>/docs/research/`, con la
  misma estructura y los mismos niveles de confianza (Verificado / *inferido* / **Análisis** /
  *seam*) que el del DataServer.
- **Herramientas:** Ghidra, la VM2003 y el método del repo de research (`TECNICAS_Y_METODO.md`).
- **Alcance:** protocolo y comportamiento (L-9).
- **Lecturas:** *CS:APP*, capítulo 3 (está en x86-64; el original es de 32 bits y la guía marca
  las diferencias); Eagle y Nance, *The Ghidra Book*.

## Seguimiento

`docs/learning/PROGRESS.md` pasa a ser una tabla:

| Concepto | Ola | Nivel | Último repaso |
| --- | --- | --- | --- |
| ejemplo: endianness | 1.0 | romper | 2026-11-03 |

- **Niveles:** 1 usar (lo escribió con tests), 2 explicar (lo cuenta sin mirar), 3 romper (predice
  y provoca la falla), 4 transferir (lo aplica en otro contexto). Una sub-ola se cierra en el nivel
  3; el 4 es ejercicio extra.
- **Repaso espaciado:** cada sesión arranca con dos o tres preguntas de olas anteriores, empezando
  por los conceptos flojos y los de repaso más viejo.
- **Dominado:** un concepto pasa a dominado cuando el alumno lo responde bien en dos sesiones
  distintas.

## Cambios al repo

Es todo documentación y andamiaje (regla 10 del estilo): lo escribe Claude y lo revisa Emanuel.

| Archivo | Cambio |
| --- | --- |
| `docs/superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md` | esta spec |
| [roadmap](2026-10-25-dataserver-port-design.md) | la tabla de olas suma las N.0, las N.R, las olas 9 a 14 y la F; "Trabajo futuro" apunta acá |
| [front-ends](2026-10-26-frontends-cli-gui-futuro.md) | "ola 9" y "9.x" pasan a "ola F" y "F.x" |
| `.claude/output-styles/tutor-muret.md` | reglas nuevas: teoría desde ciencias de la computación sin analogías web; ritmo de idea y actividad; menú de actividades; calentamiento, misión y jefe; puntos de control; alcance del carril R |
| `.claude/skills/tutor/SKILL.md` | nuevo: el comando `/tutor` |
| `.claude/agents/muret-reviewer.md` | revisa también `labs/`, `tools/` y el research escrito por el alumno |
| `docs/learning/PROGRESS.md` | el formato de seguimiento nuevo |
| `docs/learning/README.md` | cómo se usa una N.0 y dónde está cada cosa |
| `AGENTS.md` y `CLAUDE.md` | apuntan a esta spec |
| `labs/` y el `CMakeLists.txt` raíz | **no** se crean ahora: nacen en la sub-ola 0.0 |

Las guías de cada N.0 (`docs/learning/ola-N.0-*.md`) **no** se escriben ahora (L-10). La primera,
la de la 0.0, se escribe cuando arranque.

## Fuera de alcance

- Romper protecciones del original (VM de `MyDll`, Themida, licencias).
- Escribir todas las guías N.0 por adelantado.
- Automatismos del tutor que intervienen solos (pasada en cada commit, modo pareja con `/loop`).
- Cambiar quién escribe la lógica del servidor: sigue siendo Emanuel.

## Riesgos y preguntas abiertas

- **El roadmap crece mucho:** una N.0 por ola, dos N.R y seis olas nuevas. Sin fecha no es un
  problema de plazo, sino de foco: se revisa al cerrar cada ola si lo que sigue sigue teniendo
  sentido.
- **El laboratorio:** las olas 6 y 8, el carril R desde la 8 y la fase A necesitan la VM2003 con el
  SQL Server y el GameServer andando.
- **Por confirmar en research:** el rol de ExDB y el protocolo entre el GameServer y el
  ConnectServer.
- **Mismo número, dos significados:** la ola 9 era la de front-ends en los documentos viejos. El
  renombre a F se hace en el mismo PR que esta spec, para que no convivan las dos versiones.
