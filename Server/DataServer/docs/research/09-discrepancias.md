# 09. Discrepancias entre documentos

El research del original tiene varias capas (assembly, decompilación de Ghidra, reconstrucción
`mfc-vc6/`, prosa en `CONTRACT.md` y notas). A veces no coinciden. Este documento junta los
desacuerdos que se verificaron en esta pasada y dice, para cada uno, **cuál es el dato bueno**.

La regla de desempate es la de [01](01-binario-y-metodo.md): manda el assembly
(`disasm/Dataserver.asm`), después la decompilación, después la reconstrucción, y la prosa es la
más débil. La reconstrucción `mfc-vc6/` es un **modelo compilable**, no la verdad: en varios puntos
simplifica o "arregla" a propósito, y eso hay que tenerlo presente al usarla como oráculo.

| # | Dónde | Qué dice una fuente | Qué dice el assembly (dato bueno) | Impacto |
| --- | --- | --- | --- | --- |
| D-1 | objeto `ctx + 0x4068` | la reconstrucción (`ds_net.cpp`, `ds_timer_new`) lo llama "temporizador" de `0x2C` bytes | lo construye `0x4059a0`, el **mismo** constructor de la cola de mensajes, con tope 5000; es una **cola** por conexión que nadie usa ([03](03-hilos-y-concurrencia.md#la-cola)) | cosmético (ninguno la usa), pero cambia qué es la estructura |
| D-2 | cierre desde la UI (`DS_Net_Stop`) | la reconstrucción toma el lock global | `0x404c20` libera los slots **sin** tomar `0x440510` (C-6) | la reconstrucción es más segura que el original; MuRet sigue a la reconstrucción |
| D-3 | log (`0x406270` → `0x4061e0`) | la reconstrucción (`Log.cpp`) usa una `CRITICAL_SECTION` propia | el original **no** toma lock en el camino del logger (C-5) | el original tiene una carrera cosmética que la reconstrucción no |
| D-4 | stats de inicio por clase | una etiqueta vieja marcaba el float `+0x14` como "Mana" | es **MaxLife**; Mana/MaxMana están en `+0x1C`/`+0x20` (corregido en revisión adversarial, [07](07-datos-auxiliares.md)) | afecta los argumentos de `MX_CreateCharacter`; ya está corregido |
| D-5 | getter `0x4010a0` | Ghidra lo nombra "StateMode activo" | es un cuerpo **plegado por el linker** (ICF) que sirve también para leer la cabeza de la lista de gremios; hay que mirar `ecx` antes del `call` ([01](01-binario-y-metodo.md)) | trampa de lectura, no bug del binario |
| D-6 | path del fuente de red | `ds_net.cpp` usa `F:\Backup\MuProject_20031004\DataServer\giocp.cpp`; `ds_parser.cpp` usa `F:\Backup_MuProject_20031004\Dat` (truncado, con guion bajo) | el `.rdata` tiene el string **truncado**; el nombre completo con `\` sale de los `__FILE__` de los logs | solo afecta el texto de los logs; sin impacto funcional |
| D-7 | tamaño útil de paquete | el contrato habla del límite de recepción `0x2000` | el límite **efectivo** es `0x800` (buffer del consumidor); `0x801..0x2000` desborda, `>0x2000` cierra ([04](04-red.md#dominio-efectivo-del-tamaño), Q-02/Q-03) | define el dominio real de entrada |
| D-8 | `0x30` bajo el quirk | la prosa describe el flujo de éxito (crear gremio, result 1) | bajo Q-01 el flujo de éxito **no se alcanza**; el `0x30` real devuelve result 3 ([05](05-protocolo.md), [08](08-quirks-y-bugs.md)) | el flujo "documentado" no es el que corre |

## Cómo tratar esto en MuRet

- La reconstrucción `mfc-vc6/` se usa como **oráculo de paridad** (ola 8), pero cada test de
  paridad tiene que correrse también contra el **binario original** en la VM, porque la
  reconstrucción "arregla" D-2 y D-3 (y quizá otros). Donde el oráculo y el binario difieran, manda
  el binario para definir "compatible", y MuRet elige a conciencia si copia el bug o lo arregla
  (casi siempre lo arregla y lo documenta, [08](08-quirks-y-bugs.md)).
- Las etiquetas de Ghidra (D-5) no son fuente: cada nombre de función se confirma mirando el
  contexto de llamada.
- Este documento se actualiza cuando aparezca una discrepancia nueva. No pretende ser exhaustivo:
  es el registro de las que se cruzaron hasta ahora.

## Lo que NO es una discrepancia

Para no confundir: la reconstrucción marca con comentarios muchos puntos donde **simplifica a
propósito** (por ejemplo, el cuerpo vacío de `0x4035d0`, o modelar punteros de 32 bits como
`uint32_t` para que los offsets valgan en 64 bits). Eso es fidelidad declarada, no desacuerdo. Las
discrepancias de esta tabla son donde dos fuentes **afirman cosas distintas** sobre el mismo hecho.
