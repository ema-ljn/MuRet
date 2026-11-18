# CL9.x: cierre de implementación contra el ASM

> Ejecución autorizada por Emanuel el 2026-11-18: completar H12/H13 y, después de ampliar el pedido, todo lo pendiente propio de CL9.x. REQUIRED SUB-SKILL: `superpowers:executing-plans`, en línea, con pruebas por comportamiento y revisión adversarial final.

**Goal:** completar los flujos pendientes del gameplay, sus ventanas, paquetes y transiciones, preservando el contrato observable del cliente original.

**Architecture:** se amplían los lectores, modelos de mundo, ventanas y despachos existentes. Se conservan `GameUiContext`, `GameHudContext`, `GamePanels`, el estado retenido y los escritores de `Protocol/`. Los auxiliares de UI ya portados se reutilizan; un caso pendiente no se convierte en éxito vacío.

**Tech Stack:** C++20, CMake/vcpkg, SDL3, OpenGL 3.3, GoogleTest/CTest; sin nuevas dependencias previstas.

**Spec:** `2026-11-13-ola-cl9-gameplay.md`, `2026-11-14-cl9-traspaso.md`, roadmap `../specs/2026-10-30-cliente-design.md`, research 04/06/07/08 y los TSV de funciones/cortes. Fuente de verdad: exports ASM del original c32bbd5f; el pseudocódigo es una hipótesis que se comprueba contra instrucciones y datos.

## Global Constraints

- ASM del original SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`. El PE sólo se lee como datos; la ejecución original se reserva para la VM.
- Preservar orden de lecturas/escrituras, anchos y signedness, wrap, RNG, sonidos, accesos protegidos, capacidad y retorno. Un pool lleno omite el alta, no el resto del caller.
- No inventar respuestas del servidor ni ocultar constructores ausentes. Cada frontera externa mantiene su VA y dueño, y no acredita fidelidad completa.
- Conservar la historia y los cambios ajenos. Sin force push, rebase de ramas públicas ni merges de PRs en main. Actualizar los PRs existentes por hito; H13 sobre H12.
- `clang-format-18`, include-cleaner 18, warnings como errores, tests Linux/Windows y GL obligatorio. Fixtures grandes al heap por el stack de 1 MB de MSVC.
- Las citas y bytes del inventario no son un porcentaje de equivalencia funcional. No se cierran pruebas sin evidencia original.

## Review Focus

- Abrir una ventana desde un paquete y usarla en el siguiente paso/dibujo: estado y callbacks deben llegar completos.
- Selecciones vacías, listas retenidas, índices signed/unsigned y cambios de pestaña no deben fabricar ni perder estado nativo.
- Aceptar/cancelar/cerrar, Esc y cambio de escena deben respetar mensajes, envíos y consumo de RNG en orden.
- Cambio de servidor con y sin movimiento pendiente, actor eliminado y gremio previo: no retener identidades ni cachés ajenos al original.
- Flujos de evento con capacidades parciales/llenas y frames intermedios: no omitir la cola ni duplicar paquetes, sonidos o altas.

## Tareas

### 1. Base e inventario ejecutable

**Files:** tablas `2026-11-13-ola-cl9-{funciones,cortes}.tsv`, research 08 y plan vigente; informe de contraste en `Client/docs/research/` al cerrar.

- [x] Registrar main/base/ramas y clasificar cada hueco alcanzable: lógica CL9, dependencia CL10/CL11, código muerto o evidencia dinámica pendiente.
- [x] Compilar y ejecutar primero `test_castle_siege`, `test_castle_siege_receive`, `test_castle_senior_answers` y `test_session_event_applier` de la cabeza H12 heredada. Expected: baseline conocida; registrar cualquier fallo antes de cambiar código.

### 2. Tiendas, almacén y caos: cierre de hallazgos heredados

**Files:** `Scenes/{TextureSets.h,ChaosMix.cpp,NpcWindowDrawing.cpp,NpcWindows.cpp,GameDialogSteps.cpp}`, tests respectivos, revisión A/B de H6.

**Interfaces:** conservar `GameUiContext`, `GameHudContext`, `NpcWindowState` y el EDIT compartido que H7/H8 ya implementan.

- [x] Reconciliar cada hallazgo A/B con el código actual y sus callers ASM; no repetir pendientes ya resueltos al integrar H7/H8.
- [x] Regresiones RED antes de reparar cargas usadas por mezcla, lectura de tablas y aritmética del coste del almacén. Expected: fallo por comportamiento ausente, no por compilación.
- [x] Cerrar pasos y respuestas restantes con paquetes esperados literalmente y comprobar GREEN de la suite del hito y vecinas.

### 3. Eventos y conexiones de los hitos ya integrados

**Files:** `Scenes/{GameHud.cpp,GuildDrawing.cpp,CameraTour.cpp,PetTooltip.cpp}`, servicios de efectos y tests correspondientes.

**Interfaces:** `WorldRuntime::events`, tablas de gremio H8, `GameHudContext`, pools existentes y `GamePanels`.

- [x] Implementar el marcador y la tela de Battle Soccer desde `0x5a7a01..0x5a7cbd` y `0x50caaa..0x50cb5c`; tests RED con partido activo y nombres/puntaje reales del estado.
- [x] Reconciliar precio de mascotas con H6 y las entradas de eventos de las grillas; RED sobre el recorrido completo que hoy corta.
- [x] Contrastar el recorrido de Crywolf, sus altas y efectos desde `0x4039f0`, pasos y dibujo. Completar las capacidades necesarias por su contrato; registrar por separado cualquier frontera de otras olas aún genuina.

### 4. H12: las ventanas del castillo

**Files:** `UI/CastleSiegeState.{h,cpp}`, nuevos módulos de ventanas en `Scenes/`, `Scenes/{GamePanels,GameUi,GameHud,CastleSiegeReceive,CastleSeniorAnswers}`, `UI/{PanelRegistry,PanelDialog}`, tests de paquetes/ventanas/dibujo.

**Interfaces:** `StepTheCastleWindows(const GameUiContext&)`, `DrawTheCastleWindows(GameHudContext&)`, callbacks de abrir/cerrar en `GamePanels`; escritores existentes de `Protocol::CastleSiege`.

- [x] Registro y ventanas de listas: `0x64aca0`, `0x648a30`, `0x64d350`, `0x661310`, `0x661090`, `0x6485f0`, listas B4/B5; pruebas RED de apertura desde respuesta, clics, selección y cierre con envío/RNG exactos.
- [x] Castle Senior: pasos `0x6691c0`, `0x663b20`, `0x666190`, pestañas, impuestos/plata, dibujo y respuestas `0x6623e0..0x667a40`. Reutilizar `PanelDialogState` y sus callbacks, sin callbacks desligados del mundo/cable.
- [x] Eliminar únicamente los cortes propios resueltos, conectar inventario y listas, y probar paso/dibujo desde GameScene. Expected: ventanas utilizables y packets byte a byte, sin el error heredado `0x6579e0`.

### 5. H13: batalla y cambio de servidor

**Files:** WIP de `origin/claude/cl9-hito-13`, `App/ServerSession`, módulos de estado/recepción/dibujo CastleBattle, servicios del mapa 30 y tests vecinos.

**Interfaces:** preservar el estado canónico H12, caché de gremios H8, actores H1/H2, opciones/macros y sesión. La rama H13 se reconcilia encima de H12, sin perder ninguno.

- [x] Integrar WIP y validar su baseline. Revisar B1/hello con y sin movimiento pendiente y limpieza `0x447ae0`; RED del ciclo de reconexión y de estado residual.
- [x] Completar B9/BA/BB/B2 20, listas/estados retenidos, HUD, interruptores y limpieza al borrar actor desde callers ASM.
- [x] Completar lógica del mapa 30 (`0x4294b0`, `0x42b00b`, `0x42b5f8`, `0x412193`, `0x4140d5`), armas/puertas y reglas de interacción con los globales del asedio.
- [x] Tests GREEN de red, estado, UI, objetos y actores, incluidos RNG/pools y entradas fuera del mapa 30 cuando el original no mira el mapa.

### 6. Barrido de cierre propio y editor

**Files:** TSV de cortes, research 08, UI/escenas y sus tests.

- [x] Revisar cada corte CL9 restante y sus callers: completar el comportamiento alcanzable, o demostrar código muerto/dependencia con VA precisa.
- [x] Reconciliar pendientes de H9 con el EDIT de H10 ya integrado; completar rutas propias demostrables. IME o perfiles nativos no contrastados permanecen explícitos y no se certifican por una fixture local.
- [ ] Actualizar inventario funcional por flujo y tabla de evidencia, sin porcentajes de citas presentados como implementación. Primera entrega escrita; la pasada final sigue detectando callers determinados que deben cerrarse antes de dar el barrido por concluido.

### 7. Verificación, adversarial y PRs

- [ ] Suite completa disponible en Linux con GL obligatorio, formato/include-cleaner y pruebas focales de cada contrato; registrar fallos y saltos por nombre.
- [ ] Revisión adversarial final fresca Astra 6 high contra ASM; cada hallazgo importante se reproduce RED y se corrige con GREEN.
- [ ] Publicar cambios en los PRs por hito y crear H13 apilado sobre H12. Verificar CI Linux/Windows en cada cabeza; no heredar verde de otra base.
- [x] Handoff VM con casos/estado inicial/hash original y SHA MuRet. Declarar el alcance estático comprobado y lo que todavía impide fidelidad completa. Preparado; no ejecutado en VM.

## Resultado de ejecución al 18/11

Implementaciones de tareas1..6 integradas; el barrido final todavía revisa y conecta callers. Baseline123/123; regresiones
RED→GREEN de reset/cierres y suites focales R2 registradas en el
[estado integrado](../../../Client/docs/research/2026-11-18-cl9-estado-integrado.md).
Las condiciones pendientes de tarea7 se resuelven sobre la cabeza final: suite completa,
CI Linux/Windows y revisión fresca. El runbook VM está preparado, pero sus casos no
se declaran ejecutados. Marcar una tarea de implementación no cierra sus fronteras de
fidelidad ni equivale a CL9 completa.
