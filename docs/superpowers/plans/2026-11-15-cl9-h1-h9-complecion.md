# Compleción propia y contraste nativo de CL9 H1/H9

> **Para el ejecutor:** aplicar `superpowers:executing-plans` y TDD; investigación de binario con una función por analista. El coordinador es el único operador de VM y escritor de los contratos compartidos.

**Objetivo:** completar los pendientes propios identificados de H1/H9, corregir diferencias demostradas y contrastar las rutas alcanzables contra c32 en VMXP, con evidencia y límites explícitos.

**Arquitectura:** mantener C++20/SDL3 y las capas actuales. El control EDIT necesita un modelo portable de texto/cursor/selección/composición, eventos ordenados y dibujo por las métricas existentes. La observación del original sigue en herramientas propias externas al proceso y en el servidor del laboratorio; no se parchea ni se escribe memoria del original.

**Tecnologías:** C++20, GoogleTest/CTest, SDL3, FreeType, REA/Ghidra, exports ASM y pseudocódigo, VMware/VIX, lectores Win32 propios.

**Especificaciones:** `2026-11-13-ola-cl9-gameplay.md`, `2026-11-14-cl9-h1-h9-resultado.md`, `Client/docs/research/scripts/game/capture/RUNBOOK-cl9-vm.md`, research `06-render-escenas-y-ui.md` y `08-pendientes.md`.

## Restricciones globales

- Prevalece el ASM sobre el pseudocódigo. Diferenciar hipótesis, análisis estático y observación nativa.
- Tutor apagado; implementación autorizada por Emanuel el 2026-11-15.
- Binario c32 SHA256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`, PE32 x86, base preferida `0x00400000`; verificar archivo, PID, módulo y base reales por sesión.
- Original y lectores de su proceso sólo en VMXP. No inyectar, eludir protecciones ni fabricar capturas históricas. Un solo operador y una operación VIX por VM a la vez.
- Raw, claves, login, volcados y capturas permanecen privados fuera de Git. Al repo sólo contratos, herramientas propias, hashes, conteos y resultados sanitizados.
- Mantener alcance y cortes de H3/H4/CL11, H7, H10 y H13 identificados; no declarar su integración por completar una regresión de H1/H9.
- Los tests nuevos deben fallar antes del arreglo. Un skip no cuenta como aprobado.

## Foco de revisión

- Selección y cursor al insertar, borrar y pegar en el límite ANSI de 10 bytes; no truncar una secuencia de entrada inválida a un carácter diferente.
- Orden de texto, teclas, composición y mouse recibidos en el mismo frame y conservación en un frame sin paso.
- Cierre con foco en EDIT: no inventar SetFocus/DestroyWindow; explicar por caller el foco superviviente y el Raise posterior.
- Pegado y selección deben conservar la privacidad de los campos; ningún valor del editor se imprime al log o a GoogleTest.
- Draw intermedio y movimiento de ventanas no deben duplicar envíos; coincidencias gráficas no demuestran RNG ni orden WndProc.

## Tareas

### 1. Contrato EDIT, procedencia y observadores

**Archivos:** research del cliente, `scripts/game/capture/`, resumen sanitario de la sesión.

- [x] Contrastar ctor `4674c0`, init `467fe0`, subclass `467b10`, foco `4682a0`, mouse `468d50`, destructor `467710` y manager `490b70`; una función por propietario y dependencias explícitas.
- [x] Resolver el virtual de límite `vtable 6b9550 + 3c -> 467f30`, semántica XP de `EM_SETSEL(-2,-1)` y ANSI/IME antes de fijar expectativas del buffer.
- [x] Reusar/actualizar lectores propios con estado, selección y caret del EDIT conocido; probar sus entradas sintéticas sin leer nombres, chat ni login.
- [x] Guardar estados, comandos, versiones, hashes y límites; conservar las observaciones previas como sesión separada.

### 2. Modelo portable del EDIT y regresiones

**Archivos previstos:** `Client/src/UI/OldControls.h`, nuevo modelo de edición en `Client/src/UI/`, tests de UI, `FriendWindows.cpp`.

**Interfaz:** conservar `Value/Shown/Type/Backspace/Clear/SetText` de los lectores actuales; añadir movimiento de caret, selección, borrado delantero, pegado y composición sin exponer el contenido en logs. El modelo no depende de SDL ni Win32.

- [x] Escribir y observar fallar tests de inserción en medio, selección reemplazada, límites, Home/End/Shift, Delete/Backspace y composición confirmada/cancelada según el contrato de la tarea 1.
- [x] Implementar el modelo mínimo, manteniendo los campos de login fuera del cambio salvo dependencia real demostrada.
- [ ] Comparar los mismos casos con EDIT c32 en XP y corregir diferencias con evidencia.

### 3. Entrada SDL, integración de escena y dibujo

**Archivos previstos:** `InputEvent/InputState`, `Platform/InputEvents/Window`, `Scenes/GameSceneUi/GameChat/GameUi`, `UI/OldWindowDraw` y sus tests.

- [x] Escribir tests de eventos de composición, edición y modificadores conservados en orden sin pasos y consumidos una vez.
- [x] Conectar cursor/selección/pegado y preedición al EDIT activo; conservar el enrutamiento de Enter/Tab, foco superviviente y bloqueos de otras ventanas.
- [ ] Dibujar caret en su posición real, selección y composición con las métricas de texto; ubicar el área IME mediante SDL3.
- [x] Comprobar escenas con socket falso, Draw intermedio, envío único y ausencia de datos privados en diagnósticos.

### 4. Validaciones nativas H1/H9 y servidor

- [ ] H1-E1: emote `^^` parado/caminando/elfo/transformado, modelo realmente cargado y correlación de wire, acciones y frame.
- [ ] H1-E2: DF f1 por mecánica real, posición, sonido, altas y RNG; pool lleno y wrap de IDs quedan en sus pruebas estáticas sin forzarlos en el original.
- [ ] H1-E3: aviso de bendición repetido y otro actor por una mecánica real del laboratorio.
- [ ] H9-B1: conservar las tres ramas ya observadas y completar botón ajeno/efectos del mouse mantenido.
- [ ] H9-D1/Q1: doble clic y consumo por mensaje; cola Draw y envío único con observadores adecuados.
- [ ] H9-U1 y P-C181: entrada/selección/límite/IME/foco en XP frente al port.
- [ ] P-C184/P-C186: amigos reales, altas/bajas/estado, quads/texto/RNG, con servidor compatible o bloqueo concreto documentado.

### 5. Revisión y cierre verificable

- [x] Actualizar research, pendientes y tabla de diferencias con la evidencia obtenida.
- [x] Build con warnings como errores, suite completa con corpus y GL cuando disponible, formato e includes requeridos.
- [x] Revisión independiente del delta final y de qué observa cada test; resolver hallazgos importantes con regresiones.
- [x] Entregar cambios revisables, resumen sanitizado de VM, estado final y pendientes concretos. No llamar completo a un caso no ejecutado ni a un corte de otro hito.

## Estado de los puntos parciales

- La comparación nativa de la tarea2 cubre edición SBCS, selección y pegado; no un IME genuino.
- La tarea3 conserva preedición en el modelo y dibuja selección/caret; faltan preedición visible y
  geometría candidata. El chat común comparte el encoder, pero aún no integra toda esa edición.
- La tarea4 conserva los casos observados y los no alcanzados. Los flujos de amigos necesitan
  un servicio compatible; los casos propios pendientes de H1/editor siguen separados de ese límite.

## Registro de ejecución al 2026-11-15

El [resultado de la pasada](2026-11-15-cl9-h1-h9-resultado.md) distingue cada evidencia y pendiente.
Los checks de arriba mantienen abiertos los puntos compuestos que sólo se alcanzaron parcialmente.

- Contratos ASM y experimentos del EDIT: research11; codificación NLS/GDI XP: research12.
  Incluyen las funciones previstas y las revisiones adversariales de Enter, SetText y modos IMM.
- Implementados: perfil XP de entrada/render, cursor/selección, pegado y atajos observados,
  eventos ordenados, buffer huérfano, gates del mouse y dibujo de selección/caret a origen de píxel.
  Composición conservada/cancelada en el modelo; preedición visible, geometría candidata, modos IME,
  scroll y editor del chat común siguen parciales. No equivale a completar P-C181.
- Los ocho avisos Linux de las 03:12 provenían de death tests intencionales. La corrección suprime
  reportes sólo dentro de esos hijos; no altera la configuración del escritorio.
- Nueva evidencia nativa H1: el caso `^^` parado envió dos C1/18 iguales y un chat, con eco del servidor.
  No demuestra pose/animación completa ni las otras condiciones de H1.
- Se preservan las tres ramas B1 previas. Se añadieron buffer superviviente, teclado, pegado y
  bitmap visible. Q1 observó una sola solicitud al propio personaje y cierre remoto sin respuesta;
  Emanuel confirmó que el servicio de amigos/chat no conecta. No se repiten esos envíos.
- La compilación y suite del delta final quedan registradas en el resultado al terminar. El CTest
  anterior interrumpido/solapado con build no se cuenta como resultado completo ni se hereda el CI353.
- Los cortes H3/H4/H7/H10/H13 y las pruebas nativas no alcanzadas conservan sus pendientes.
  El servidor está en otra PC y queda fuera de esta reparación. H1/H9 no se declaran completos.
