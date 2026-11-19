# H3: combate, daño y muerte

> Laboratorio educativo de reconstrucción. Revisión iniciada el 2026-11-15 y verificada desde el 2026-11-16 sobre el PE32 x86
> `main.exe`, SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
> Las direcciones son VAs estáticas, con base `0x400000`. El ASM prevalece sobre el pseudocódigo.

## Alcance y nivel de evidencia

El [PR #342](https://github.com/ema-ljn/MuRet/pull/342) integra el hito 3 con H1/H2/H5 y con las
reparaciones de `main` de los PRs #354/#355. El inventario de H3 contiene trece filas y 11.766 bytes;
ese inventario no incluye todas las ramas de combate insertas en funciones asignadas a otras olas.
Se revisan también esas ramas, sus recursos compartidos y sus efectos sobre el resto del frame.

Este informe distingue lectura estática del original, pruebas de la implementación y observación en
XP. Una prueba C++ que coincide con el contrato ASM no acredita por sí sola una ejecución nativa.
Se obtuvo una captura nueva del combate normal del original; la comparación estricta ejecutando
MuRet contra ese servidor y los casos especiales siguen pendientes.

| Área | Evidencia estática principal | Contratos revisados y regresiones relacionadas |
| --- | --- | --- |
| Elección de ataque | `0x4f4a10` | prioridad del modelo de fauna, modelo sin acciones, contadores y sonido; continuación con pool de modelos lleno |
| Ataque al llegar | `0x5650c0` | modelo transformado, D3 antes de D7, cambios de estado aunque falle el transporte |
| Blanco compartido | writers del WORD `+0x3a4` y sus consumidores | combate, cabeza y helpers leen el mismo `attackTarget` |
| Colocación del modelo | `0x4fd69b..0x4fd6ef`, `0x4ec110` y dibujo de monstruos | origen y escala compartidos, gates de alpha y desplazamientos de modelos `0x18e/0x18f` |
| Expiración del actor | `0x4fd5f0`, `0x511070` | contador `+0x428`, orden de liberación, actor independiente del pool, continuación del cuerpo |
| Cuerpo que muere | `0x4fb9e0`, `0x4fb1a0` | liberación repetida de tela conservando reloj, olvido de huesos al retirar al no héroe |
| Reacción al golpe | `0x4f53a0` | lector canónico de sonido, cinco gates de polvo `5f3/0`, capacidad parcial/llena y sonido final `0x86` |
| Muerte inmediata | `0x4f5660` | vida propia, desactivación previa al efecto especial, frame cero/NaN, prioridad de Chaos Castle |
| Flecha y brillo | `0x53e2b0`, `0x517850` | nacimientos E2, geometría y RNG del brillo A25, impacto sólo en los primeros 400 actores |
| Explosión y dispersión | `0x51fe10`, `0x51fff0` | intentos incluso con pool lleno, RNG del caller y del constructor, sonido sólo si nace la explosión |
| Casos de monstruos | `0x4f5b70`, Kalima `0x43a7a0`, Crywolf `0x4324c0` | orden de helpers, habilidades, pools, sonidos, stores y continuación |
| Helpers de mapa | asedio `0x42dcf0`, Doppelganger `0x429110`, Fortress `0x42fce0` | con modelos llenos, las ramas activas salen con éxito, sin RNG ni cola; con espacio conservan su frontera CL11 |
| XP y muerte por red | `0x4cf200`, `0x4cfce0`, `0x4d07c0`, `0x4d0a30` | formatos de paquete, flag descifrado, overflow, víctima, empujón y caída |
| Números de daño | `0x548e40` | lectura independiente de los 108 bytes: primer hueco de 100, posición Z+140, velocidad 10, timer cero y luz |
| Selección del blanco | `0x558bb0` | prioridades de Chaos Castle, asedio, estado 2/8, duelo y PK; el propio héroe puede ser atacable con PK >= 6 |

## Contratos corregidos

`+0x3a4` es un único WORD nativo. La representación duplicada permitía que el reply escribiera un
blanco mientras la cabeza leía otro. La regresión H3→H2 observa el resultado del consumidor.

El paso `0x4fd5f0` publica la colocación del actor actual antes de movimiento vertical y muerte.
Corre en el segundo bucle de 400 registros para jugadores y monstruos vivos a la entrada; no hereda
las exclusiones del primer bucle. El cuervo puede consumir ese origen anterior al hundimiento,
aunque otro paso publique de nuevo la posición final. Esto corrige la interpretación anterior de
Q-C412/Q-C1178: no era el modelo residual del jugador anterior.

Los puntos de hueso de `0x4f5b70` usan ese modelo compartido: `ESI` se calcula desde el tipo al entrar
y los call sites de `0x4ec110` lo pasan como `this`. Cambiar después `actor.position/scale` no cambia
el origen/escala retenidos. Esto afecta al sangrado, al trail geométrico y a las partículas de cola;
scatter, E2 y el trail aleatorio leen directamente el actor actual y conservan esa procedencia.

La luz local de `0x4f5b70` también persiste entre el caso directo y la cola. Los kinds `0x49/0x4b`
con acción distinta de 3 y paso 9 descartan el punto transformado, pero conservan RGB `(1,0.5,0)`.
El `5CA/0` del fallback de skill 3 consume ese naranja. Considerar inútil toda la rama perdía un
efecto observable cuando los joints estaban llenos y quedaban partículas disponibles.

El contador firmado `+0x428` expira con todo valor `<= 0` salvo el centinela `-128`; un positivo se
decrementa primero. Se escribe el centinela antes de liberar huesos/helpers, cuervo, tela y capa.
La limpieza de un actor independiente también debe desactivarlo aunque no tenga índice en el pool.

En `0x4f5660`, las muertes especiales del jugador transformado D1/D2/D3 y de los modelos `0x170`
y `0x16b` desactivan el actor antes del primer creador. Un pool lleno deja continuar hasta sonido y
gate de frame. ~~Con capacidad disponible se conserva el límite del efecto CL11 en la CALL real:
`0x4f58c2`, `0x4f595d` o `0x4f59ca`.~~ Ya no corta ninguno (CL11): los huesos de `0x4f58c2` y `0x4f595d`
(`World::CreateWizardModelEffect`) y los ocho pares `0xe5/0` y `0xe6/0` de `0x4f59ca` (lote de los tipos altos,
novena vuelta) se crean; con la tabla llena, nada y sin `rand()`. El prefijo no cambia la acción. El cierre del registro propio
del héroe precede al sorteo del sonido de Chaos Castle.

El retorno del transporte D3/D7 no decide el resto del ataque al llegar: el original continúa después
del intento de envío. MuRet conserva errores explícitos de construcción y registra fallos de socket,
sin convertirlos en una interrupción de la secuencia de estado.

Los E2 nacen desde la posición del actor, sin modificar sus ángulos. El caso de tres flechas almacena
en float `yaw`, `float(yaw + 20)` y `float(segundo - 40)`, en ese orden. El brillo A25 sortea el ángulo
**X**, no Z; por cada una de quince iteraciones puede crear el joint y siempre intenta la partícula.
El impacto examina los primeros 400 actores: el registro fantasma 400 queda fuera.

`0x4f53a0` consulta la misma tabla nativa de sonidos que ataque y muerte. El lector de H3 usa ahora
`WorldRuntime::modelSounds`, incluyendo escrituras cruzadas a `0x15e/0x15f`; el fixture de carga
directa enlaza esa tabla como ya hacía `MonsterCreation`. El test que sobrescribe pares de sonidos
prueba la selección del lector; el test previo de creación observa la carga real de ambos registros.

El modelo `0x18f` hace cinco intentos de polvo `5f3/0`, no cinco emisiones garantizadas. Cada intento
lee el gate; si pasa, lee X/Z y llama al constructor, que sólo consume sus cuatro sorteos con un slot
libre. Luego continúa y siempre solicita el sonido `0x86`. El oráculo de integración reutiliza el
constructor para probar el cableado y la intercalación; sus cálculos internos tienen sus propios
tests y esa reutilización no se presenta como una segunda validación independiente del constructor.

## Verificación previa (PR #342)

La compilación completa de todos los targets del PR #342 terminó correctamente. Su corrida del 2026-11-16
aprobó **7.335/7.335 tests**, **cero fallos y cero saltos**, en 236,06 segundos: incluye los **106 GL**
y los **59 casos del manifest de referencias recuperadas**, todos presentes en el XML de CTest.
Se usaron los assets reales, el corpus privado recuperado y una fuente configurada; GL era obligatorio.
No se cuentan fixtures omitidos como verificados ni esas referencias históricas como capturas nuevas.

Antes de esa corrida pasaron 89 tests focales. El primer barrido global aprobó 7.334/7.335 y detectó
una expectativa antigua de `MonsterStep`: atribuía las marcas al actor anterior del mismo tipo.
La lectura de `0x4fd69b..0x4fd6ef` demuestra la publicación previa del actor actual; se corrigieron
nombre, comentario y observador del test, conservando el señuelo del actor anterior. La segunda
corrida completa es la que acredita el resultado final.

Los 37 archivos C++ cambiados desde la integración pasaron clang-format 18. Los archivos nuevos o
modificados en las pasadas de cierre se verificaron con misc-include-cleaner de clang-tidy 18, también
los headers; el último hallazgo requería incluir directamente `GameDrawTail.h` para `JointState`.
Los ciclos RED/GREEN y los límites de los oráculos se registran en el
[plan de cierre](../../../docs/superpowers/plans/2026-11-15-cl9-h3-cierre.md).
El CI y el resultado del merge de la cabeza publicada se consultan en el [PR #342](https://github.com/ema-ljn/MuRet/pull/342).

## Límites que siguen abiertos

- Los constructores de efectos CL11 todavía no portados conservan un error en su primera llamada
  alcanzable con capacidad. Un pool existente lleno debe omitir sólo esa alta y continuar con el
  caller. Ausencia de un recurso no equivale a saturación.
- Skills, mascotas y efectos de H4, integración social de H7/H8 y reglas de eventos de H11 conservan
  sus límites explícitos. Esta entrega no implementa esas olas por alcanzar una de sus llamadas.
- La reproducción de audio pertenece a CL10. Las pruebas de H3 observan los ids y callbacks de sonido
  disponibles; `lastSoundId` conserva sólo el último pedido, no una mezcla sonora nativa completa.
- P-C250(f): los nombres de gremio pertenecen a H8. En guerra, para un blanco PK >= 6 con gremio,
  MuRet descarta como si los nombres fueran iguales; no reproduce todavía la continuación de nombres
  distintos ni el bug de `ECX` de Q-C1170. El detalle no se presenta como fidelidad completa de H3.
- Q-C1050: el caso `0x17e` de Crywolf depende de un offset local sin inicializar; no se inventa una
  geometría a partir del pseudocódigo.
- El paquete GS de muerte `0x17` de cinco bytes no contiene todos los campos que el cliente original
  lee de su buffer retenido. La proyección definida en Q-C1161 no demuestra equivalencia de ese
  estado residual; requiere comparación estricta con servidor.
- La API de números de daño representa `rise` como bool; los callers actuales pasan true. El byte
  nativo admite más valores. La revisión estática no valida por ejecución todo el dominio nativo.

## Observación en VM y herramientas

El cliente original se ejecutó únicamente en XP. Tras varios intentos de movimiento y disponibilidad
de la ventana, se obtuvo una sesión de combate normal en Devias contra Elite Yeti. Hubo desconexiones,
desaparición de la ventana y un cierre que Emanuel identificó como espontáneo en los intentos previos;
no se estableció su causa. La falta de una entrada en el registro de aplicaciones de XP no descarta
un cierre.

La captura de 25 segundos se filtró a la única conexión TCP del puerto de juego, sin guardar el resto
del tráfico. PCAP privado: 15.133 bytes, SHA-256
`ac451532fba776db5a552e2deb4bbd4578864dd5cafb5790d4e098d6f5bd433a`.
`pcap2log.py` y `wirelib.py`, con las referencias C3/C4 recuperadas, produjeron 408 paquetes:

| Sentido | Head | Largo normalizado | Cantidad / observación |
| --- | --- | ---: | --- |
| Cliente → servidor | A3 | 6 | dos giros/movimientos, D3 remapeado por FoxAnti |
| Cliente → servidor | A4 | 7 | dos ataques, acción `0x78`, rumbo 7; D7 remapeado por FoxAnti |
| Servidor → cliente | D7 | 8 | 124 actualizaciones de daño del entorno de combate |
| Servidor → cliente | 17 | 5 | una muerte: confirma el largo corto descrito en Q-C1161 |
| Servidor → cliente | 16, recibido en C3 | 9 | una recompensa sin shock, XP 13.964 y daño 4.630 |

La pantalla posterior al clic mostró el daño **4.630** y **13.964 puntos de experiencia ganados**,
iguales a los campos BE16 del `16`. El primer A4, el 17 y el 16 llevan la misma clave de blanco;
se comprobó la correlación sin publicar esa clave. El 17 llegó antes del 16 sin shock, orden que
reproduce con claves sintéticas `MonsterCombatTest.APlainDeathThenKillExperienceReplyFinishesTheMonster`.
Esa regresión observa los campos del héroe, daño, incremento de XP, chat y retiro del cuerpo.
Se observaron además números «Miss» mientras atacaban los
monstruos. Esto confirma el intercambio normal del original y el formato recibido; no compara
animación por frame, RNG interno, saturación de pools ni la lectura residual tras el `17` corto.
El archivo `main.exe` de la ruta usada en el guest volvió a dar el SHA-256 c32 completo y 3.058.308
bytes después de esta prueba. No se leyó la memoria del proceso nuevo para atestiguar su módulo.

Las pruebas posteriores se prepararon con entrada GUI normal y captura de red acotada. El auxiliar
propio de entrada no abre ni lee memoria de procesos. El servidor está en otro equipo y no se
modificó. No se escribieron memoria ni instrucciones del cliente, ni se desactivaron protecciones.
PCAP, volcados, nombres de personajes y logs que puedan contener información privada no se versionan.

Se usaron exports ASM/pseudocódigo del build identificado, CMake/GoogleTest, clang-format 18 y
clang-tidy 18.1.8, las CLIs oficiales de VMware y Context7 para el contrato de vida de fixtures de
GoogleTest. REA identificó el PE, pero su análisis no terminó dentro del timeout; no se le atribuye
la revisión de las funciones. Su sesión se cerró y se continuó con los exports verificados.

## Estelas de ataque: auditoría del 2026-11-16

El ataque reportado por Emanuel (acción `0x23`, arma visible `0x222`) atravesaba un corte explícito
`4fd236` pese a que el creador, paso y dibujo de las cintas ya existían. Los tests por clase
configuraban el equipo propio, pero conservaban vacías las manos visibles del actor; otro test
llamaba directamente al paso de ataque. Así omitían el productor del cuerpo `4fcee0`.

La nueva regresión `AttackStepTest.AVisibleMeleeWeaponSurvivesTheFullAttackBodyPipeline` usa
mano visible `0x222`, nivel 13 y hueso 33 y pasa por `StepCharacters`: con los tres archivos originales de
`main@015c1746` falla exactamente en `4fd236`; con el port atraviesa cuatro pasos y conserva la cinta.
El fixture tiene todos sus huesos/acciones válidos y observa la saturación nativa en 29 puntos.

### Contratos reconstruidos

| Productor | Contrato que ahora observa la implementación |
| --- | --- |
| `4fcee0` | gate de frame 3/NaN y excepción `0x8d`; mano izquierda `0x25/0x26`; modelo WORD firmado; prioridades por acción, arma y tipo; color; una o diez muestras; continuación de helpers |
| `4eb860`, flags 0,1 | raíz con `OriginalAngleMatrix`; nueve floats de base escalados; posición sin escalar; concatenación mundial; dummy y scratch persistentes |
| `4ec200` y `4f9580` | acción compartida BYTE `+0xa0`, velocidad de esa acción y factores de la acción del actor; publicaciones `+0xa4/+0xa8` incluso al terminar; congelado/sin acciones/una key no publican avance |
| `441260` | `0x1a7`, acciones 3/4: huesos 23/34, keys 23/34; dos llamadas por cada una de diez poses |
| `42fa40` | `0x1b3`, acción 3: hueso 25/key 25; `0x1b1`, acción 4: huesos 80/82, keys 80/84, intercalados |
| `432bd0` | sorteo incondicional; cintas `0x1ab/0x1ae/0x1af/0x1b1/0x1b3/0x1b4`; `0x1b1` y `0x1b3` agregan una segunda tanda a los registros del helper anterior |
| `426d90` | `0x1b8`, acciones 3/4 hasta 5,06 inclusive; `0x1b9`, acciones 3/4 con tres segmentos; `0x1ba`, acción 3 con uno; diez poses, colores/keys y orden originales |

Las diez muestras acumulan `float(frame-speed)` y `float(speed*0.1f)` con un store float por
incremento. No se sustituyen por `first+i*step`. Una tabla de cintas llena no evita posar ni la
continuación; un registro vivo del dueño/key sigue recibiendo muestras sin reiniciar su vida.

El modelo es `*[0x5769ad8] + tipo*0xe0`: el global contiene un puntero al arreglo. Su acción,
escala y origen son compartidos por tipo y pueden pertenecer a otro actor cuando se leen fuera de
la llamada. El avance no escribe `+0xa0`; la pose sí lo limita a 0 si pasa el número de acciones.

Dos fallos adicionales quedaron expuestos por la revisión: el segundo avance del asedio leía la
velocidad de la nueva acción del actor, en vez de la retenida en el modelo (caso discriminante:
frame 0,25 frente a 2,0); y un efecto `0x147` consultaba erróneamente el pool de partículas. Los tests cruzan
ambos pools: `0x145` va antes de posar, `0x147` después de diez muestras. La cinta no elimina sus fronteras.

### Observación nueva en XP y límites

Se copió el archivo del guest y se confirmó el SHA c32 indicado al inicio. Los exports de
`4fcee0`, `4eb860`, `4ec200`, `52e470` y `52e3b0` coinciden en **3.485 bytes, cero diferencias**.
El lector nuevo valida además dos firmas de 16 bytes en el proceso vivo; eso no sustituye el hash
completo del archivo ni verifica todos los bytes del código cargado.

Un clic sostenido normal en Lorencia produjo 500 muestras completas, sin errores de lectura:
414 en acción `0x03`, 10 en `0x24`, 1 en `0x23` y 75 caminando (0f). Hubo 52 muestras con una cinta viva del héroe:
tipo 0, key 0 y RGB `(1;0,6;0,2)` en todas. Con el arma `0x222`, nivel 13, hueso 33 y escala 0,9, las distancias
entre sus extremos fueron de 89,999685 a 90,000466, consistentes con el segmento de 100 escalado.
El cliente siguió en el mapa. No se cuentan estas lecturas como diez llamadas observadas al creador:
el pool se muestrea después de otros pasos, con count entre 2 y 28 y envejecimiento ya aplicado.

Las muestras son secuenciales, **no atómicas**. Que coincidan los guards antes/después no demuestra
que no hubo un paso intermedio; tampoco se interpreta el modelo compartido como el estado exclusivo
del héroe. No hubo breakpoints, suspensión, escritura de memoria ni cambios de protección.
El lector, las fuentes de interacción y el uso están en [capturas](scripts/game/capture/README.md).

En MuRet se llegó al mismo mapa con el caballero equipado. Emanuel realizó el combate con entrada
física y la captura mostró bajas y avisos de 21.000 y 27.000 de experiencia. Después informó que
un mob había matado al personaje: la captura siguiente lo mostró reaparecido en Lorencia, con el
proceso activo y sin `4fd236` ni otro error de escena. No se capturó el frame exacto de la muerte.
Esto acredita continuidad manual en esa secuencia de combate/recompensa/reaparición; no acredita
un D7 capturado ni la equivalencia numérica de cada punto de cinta entre ambos procesos. La
comparación estricta y las ramas especiales no ejercitadas siguen abiertas. Los primeros intentos
de automatización que sólo caminaron no se convierten en PASS de combate.

Los hashes, conteos, fuentes y límites se versionan; logs, imágenes y lecturas crudas permanecen
fuera de Git. El inventario ampliado y los cortes restantes están en la
[pasada funcional](../../../docs/superpowers/plans/2026-11-16-h3-auditoria-funcional-y-estelas.md).

Hash SHA-256 de la serie original (864.819 bytes):
`b6073c722158335f0daa174eb38b39780ef8096b971175edff93cf5ca49f7884`.
El hash identifica el artefacto privado; no lo convierte en una captura atómica ni sustituye sus
precondiciones. La observación manual de MuRet usa el candidato de binario
`274cc5a04045c99af7fab6501f83b6ed00df00d02dbed9e5e0912cff52e06d66`.

### Verificación de esta reparación

La cabeza de esta reparación aprobó **7.365/7.365 tests**, cero fallos y cero saltos, en
**240,06 segundos** de CTest. El XML contiene los **106 GL** y los **59 casos del manifest de
referencias recuperadas**. Se configuraron assets/corpus reales, una fuente y GL obligatorio.
La compilación completa terminó sin PCH y con warnings como errores. No se atribuye este
resultado al CI de Linux/Windows.

Pasaron clang-format 18 sobre los 31 fuentes/headers C/C++ modificados y misc-include-cleaner 18
sobre los 29 de aplicación/tests, incluidos los headers. Los dos auxiliares XP se recompilaron
con MinGW-w64 y warnings como errores; sus PE32 GUI tienen subsistema 5.01 e imports
KERNEL32/MSVCRT/USER32. Los dos fuentes Python modificados pasaron el análisis sintáctico.
La revisión independiente final no encontró nuevos P0/P1/P2 reproducibles; fue estática.

SHA-256 del XML privado de esta corrida:
`06ff2406e258a52f3d68d2f05512e446b2719e4e1c89b273fbf6eb0ed055dbea`.
Estas verificaciones cubren la reparación y sus regresiones; las fronteras funcionales de H3
y el contraste dinámico pendiente siguen descritos arriba y en el plan.
