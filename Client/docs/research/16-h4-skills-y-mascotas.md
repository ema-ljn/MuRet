# H4: contraste de skills, cuervo y mascotas

Pasada del 2026-11-16 para el PR #343. El objetivo es reconstruir el comportamiento del cliente, con el ASM por encima del pseudocódigo. Compilar o encontrar una dirección en C++ no demuestra equivalencia completa.

## Fuentes y método

- Cliente PE32 i386, base `0x400000`, SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
- Research `mu-server-repack-research-102i`, commit `14f0f931f68776dd103927f58c4359dc29ced712`, exports `client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/`. Los exports ASM de algunas funciones grandes tienen huecos: se contrastaron también los bytes del PE mediante desensamblado estático de `.text` (VA `0x401000`).
- Se revisaron callers, callees, puertas anteriores a las capacidades, escrituras persistentes, cierres de accesos y consumo del RNG. El original y sus DLL no se ejecutaron ni emularon en el host. Todavía falta el contraste dinámico en VM.
- La rama integra H3 final `63605ff4` y `main` `015c1746` mediante los casos B y A del traspaso, preservando la historia y las reparaciones de entrada al mundo de #355/#356. También integra la cabeza `52f835dc` del fix de estelas #359 por pedido de Emanuel; al traerla, GitHub aún mostraba ese PR abierto. Se conserva su acción/modelo compartido y la cadena de estelas, junto con las correcciones H4.

## Inventario

El TSV del plan contiene **56 filas y 60.799 bytes** asignados a H4. Además están las ramas por clase de `0x57cfc0`, aproximadamente 90 KB que el TSV no enumeraba. Estos tamaños son inventario de código, no porcentajes de fidelidad.

| Grupo | VAs | Qué se contrasta |
| --- | --- | --- |
| Accesos lógicos, 6 | `40ae60`, `40b3b0`, `40f580`, `412810`, `4e7c40`, `4e7e40` | Profundidad BYTE, accesos anidados y orden del RNG. La protección binaria queda fuera del port. |
| Cuervo y mascotas, 10 | `416510`, `416570`, `416660`, `416670`, `420cd0`, `420e90`, `421b50`, `421b70`, `4249a0`, `424e40` | Órdenes, estados, A8/A9, ventana y barras. `416660` es un RET. |
| HUD, 2 | `421b90`, `424160` | Selector de órdenes y ventana de mascota. |
| Flags, 2 | `439d40`, `4e6910` | Quitar banderas y efectos asociados, sin anticipar capacidades. |
| Respuestas, 8 | `4c5070`, `4cbe00`, `4cbe90`, `4cc290`, `4cc530`, `4ce000`, `4d5b10`, `4dbf50` | F3 11, 07, 1B, 69, 19, 1E, 73 y 48. |
| Acciones y auxiliares, 6 | `4cc110`, `4cc1d0`, `4e9c50`, `4f5290`, `53d8d0`, `53d930` | Manos, acciones, monturas y desvanecido. |
| Bajas, 2 | `518ac0`, `534f90` | Bajas de efectos y joints; pueden abrir un hueco antes del siguiente intento. |
| Costos y demoras, 4 | `5521f0`, `552990`, `552c50`, `552f10` | Campos WORD/DWORD, requisitos, cuentas y resta con wrap de 32 bits. |
| Ataque, alcance y flechas, 9 | `4f48e0`, `5526f0`, `558eb0`, `559510`, `559890`, `55b6f0`, `55cbc0`, `55faa0`, `561c50` | Blanco, alcance, acercamiento, acciones, paquetes y flechas. |
| B7/asedio, 5 | `44dd10`, `44eea0`, `44efc0`, `4e08a0`, `4e08f0` | Dependencia de H13, P-C264 e. No son cinco implementaciones jugables terminadas. |
| Código muerto, 2 | `4deba0`, `4e0950` | El inventario no encuentra callers ni punteros. No se usan para inflar cobertura jugable. |

La revisión incluye las ramas Knight/Gladiator/Lord, Elf, Wizard y la cola común `43..48`. No certifica cada combinación de clase, mapa, arma, montura y estado por disponer de una prueba de cada helper.

## Contratos que corrige esta pasada

- **F3 11:** `4c52a6..4c52c4` no lee slot cuando count=0/list=2; FE/FF mantienen prioridad. `4c5381..4c5390` lee dos bytes por entrada con stride tres, sin consultar el último padding. La lista vacía todavía recuenta con dos accesos propios; no agrega órdenes DL en la rama list=2.
- **Level-up 48:** `4dbfad..4dc004` intenta quince joints `60f/0`, después el calco `61e/0` y siempre el sonido lógico `98`. Cada alta disponible de joint consume tres RNG; las que no entran no consumen RNG de constructor. `52b984..52bb29` manda el calco /0 a la cola común: un RNG por paso, veinte pasos de vida, sin consultar joints ni dueño. El dibujo /0 ya existe en `GroundEffectDrawing`.
- **Poción automática:** los siete callers de `5af520` usan H5. Se mantienen el orden de consultas warehouse/closed/trade, Text[474], espera diez, dos RNG del envío y sonido. No se consume el ítem ni se repone maná antes de la respuesta del GS. La cola común sigue después del intento aunque el maná siga insuficiente, como Q-C1226.
- **Estado compartido:** H3 y H4 usan un solo `WorldRuntime::lastShotSkill`. `word8a` conserva sus bits WORD y la interpretación signed que usa Kundun. Los callbacks de manos `5fd` y huesos `5fe` coexisten. Se propaga la biblioteca real de modelos tanto desde MoveHero como en los contextos de combate de skills.
- **Selector y teclas:** se recorren 64 slots, se excluyen cero y `43..48` antes de abrir Skill.bmd, y sólo hidden=1 se oculta después del cierre. El ancho usa `min(byte6a-byte6b,20)` sin clamp inferior. Se cambia de fila una sola vez al mostrado 21. El dibujo resta **46** (`5aa2b0`), mientras el clic resta **52** (`5a083c`): la diferencia es nativa. `421b90` dibuja cuatro órdenes si existe cuervo, sin exigir clase. La tecla de una skill normal se lee del banco del personaje elegido antes del cierre propio y se dibuja después; FF no tiene número, cero sí.
- **Demoras:** `5530c2..5530d3` resta como DWORD y después compara signed para truncar negativos. C++ evita overflow signed; una regresión con UBSan reprodujo el desbordamiento previo con `1 - INT_MIN`.

La revisión adversarial de Astra 6 high detectó y volvió a contrastar estos grupos:

| Grupo corregido | Contrato ASM preservado |
| --- | --- |
| Pools y respuestas | HEAD/TARGET, frames y flechas distinguen efectos/joints/partículas, conservan las colas y RNG con pools llenos, y consultan de nuevo después de Kill. |
| Brillo transformado | `53fdb0` usa tipo/escala/pose del dueño y mano del héroe. Pool lleno precede la lectura del dueño. |
| Crywolf | Cuatro wrappers leen Y/X y consultan el altar antes de buscar camino o descartar meta fuera de rango. |
| Estallido0e | `501494` reutiliza el burst H3; con P+E llenos son336 RNG, después índice propio, f6 y cuenta del héroe. |
| Aquagold y barrido | `524405` envía impactos antes de luz/choque; `517a20` recorre0..399, marca8/9 y limita cinco claves. El slot400 de reserva queda fuera. |
| Chispa | `52e740` con P lleno conserva40 selectores y vuelve al golpe. |
| Agua de Kalima | Skill0a con E lleno lee Y/X y llama `437860(x,y,2,-1500)` sólo con owner de ondas; sin él sigue igual por sonido77. |
| Cuerpo y skills | `byte1a0` recorre huesos válidos, preserva seis RNG por joint y decrementa al final;614/7 con hueco sigue CL11. `byte398` gira por el contador anterior×10 y luego baja, sin normalizar. |
| Acciones | 4d/4f/5a reutilizan5ac/0;44/45 reutilizan5ac/6, luego tres619/8 (frontera con hueco);96 reutiliza diez5d9/0 con un selector de hueso cada una. |
| Placement | Los readers anteriores a505ec6 usan el placement compartido publicado antes del movimiento/muerte. El helper2b tampoco sustituye ese origen por actor.position posterior. |

En los constructores de flechas se mantienen los prefijos f7/f8/f9/10e/111/11c, CopyOwnCount y el último hueco de effects con joints lleno. `point40` modela +40..48 y está separado de la interpolación de MuRet. El alta f8 copia sus ángulos y después escribe X/Y=0 en el caller. Los pasos recién alcanzables preservan luces, partículas, impacto y epílogo. Los pasos gráficos105/0 y13a todavía son fronteras CL11 posteriores.

Se observaron fallos antes de las correcciones y regresiones focales posteriores. La revisión estática independiente y las pruebas no sustituyen la comparación dinámica del original. La validación completa y el CI se registran en el PR con su commit exacto.

## Fronteras reales

El precio del tooltip de mascota en tienda NPC (P-C246) todavía depende del formato de dinero `5b1130` y de la tienda. Party (H7) y reglas de guild/duel (H8) requieren los datos de sus hitos. B7 y el asedio son H13; el altar de Crywolf usa la condición compartida de H3 y conserva la frontera de eventos. Audio CL10 todavía es un evento lógico en `lastSoundId`, no reproducción equivalente. Los constructores y pasos CL11 ausentes con capacidad disponible conservan su VA y su pendiente; un pool lleno debe omitir únicamente el alta, no el resto del caller.

El GS reconstruido antiguo no prueba compatibilidad de todas estas respuestas. Su F3 11 usa cabecera de cinco bytes y entradas desde +5; el cliente inspeccionado lee list en +5 y entradas desde +6. Su 07 usa máscara WORD y clave desde +6, mientras el cliente lee máscara DWORD y clave desde +8. No se alteró el cliente para acomodar esas diferencias de versión.

## Evidencia dinámica necesaria para cerrar la fidelidad

El agente de VM debe probar también la continuación conjunta con #359: arma visible0x222/acción23 durante cuatro pasos y acción38 con partículas llenas, con estela y chispa habilitadas. La pose compartida de las estelas no reemplaza bodyPose, que los readers de skills leen del dueño.

El agente de VM debe usar el mismo SHA del original y registrar también el commit exacto de MuRet, mapa, clase, equipo, skill, selección, teclas y estado inicial. Las trazas y los volcados van fuera de Git; se versionan hashes y resultados resumidos. Puede reutilizar el [runbook CL9](scripts/game/capture/RUNBOOK-cl9-vm.md).

1. Lista normal, FE/FF y list=2/count=0: slots antes/después, recuento 6a/6b, índice425, accesos y RNG de `4c5070`.
2. Selector con más de veinte y cuarenta entradas, hidden1/2, teclas 0/7/FF y personaje de slot distinto de0. Capturar geometría y clic por separado para no “corregir” la diferencia nativa de Y.
3. Mago, caballero, elfa, Gladiator y Lord sin maná: orden de 26 y skill, estado de espera y rechazo por ventanas; luego confirmar que el GS, y no el envío, modifica inventario/maná.
4. Anillo de transformación: brillo creado y actualizado con escala del modelo compartido distinta de la escala del actor. Repetir con partículas llenas, verificando que el origen no cambie por un alta que no ocurrió.
5. `48` sobre actor válido/inválido, con 0/1/7/15 huecos de joints y effects lleno/libre. Registrar cada intento y sonido, además del paso y dibujo del calco.
6. Flechas de skill, Aquagold en su última vida, blancos dentro/fuera del radio y subtipo63. Registrar el C1 10 descifrado y su posición respecto del paso, luz y choque.
7. Cuervo: órdenes, owner flags1000/4000, estados5/6, contador14/15, pose ausente/presente y capacidades parciales. Mascotas A9: ventanas, hover, trainer y fórmulas con overflow WORD/DWORD.
8. Demoras y transiciones: retorno a selección, cambio de personaje y vuelta al mundo sin arrastrar acceso abierto, tecla de otro slot o estado del cuervo anterior.

Para cada caso hacen falta observaciones originales y de MuRet, no sólo una captura visual de MuRet. Si una variante exige manipular estado en el depurador por no emitirla el GS del laboratorio, registrar que es un experimento controlado, sus bytes exactos y los gates que se omitieron. No presentar ese caso como recorrido de juego normal ni inventar evidencia para los tests salteados.

## Validación de la composición con #359

Sobre la implementación integrada se ejecutaron nueve suites y **177 pruebas sin fallos**: AttackFrameSkills14, AttackStep17, AttackSkillCases31, ArrowEffects29, CharacterStep66, WeaponTrail9, WeaponTrailPipeline2, MonsterRibbonTrails7 y WorldModelPose2. Incluyen la regresión de arma visible de359 y una nueva acción38 con partículas llenas y estela de lanza. El formato de los1890 archivos C++ versionados pasó con clang-format18; include-cleaner18 revisó el diff del cliente. UBSan de resta signed:10 pruebas sin reporte.

Estos resultados son focales. La suite completa Linux y el CI Linux/Windows se registran sobre el commit publicado en los checks y en la descripción de #343; no se sustituyen por esta cifra ni se presume validación VM. Las herramientas de evidencia ejecutaron49 pruebas Python, con un salto de plataforma.
