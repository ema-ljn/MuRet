# Research del cliente original (`main.exe` 1.02)

> **Borrador, 2026-10-30.** Escrito a partir de
> [EmaNLJN/mu-server-repack-research-102i](https://github.com/EmaNLJN/mu-server-repack-research-102i)
> @ `91359b2c` (carpeta `client-research/`): la decompilación estática completa del build
> `c32bbd5f…` (`mainexe-vmxp-c32bbd5f/`), el laboratorio histórico (`analysis/mainexe/`), la capa
> de paquetes de FoxAnti (`analysis/foxanti/`), la reimplementación en C99 con tests
> (`mu-client/`) y los oráculos de datos reales (`mu-golden/`). Se completó con
> [EmaNLJN/mu-online-reverse-lab](https://github.com/EmaNLJN/mu-online-reverse-lab) @ `9f83b4c7`
> (carpeta `analysis/decompilacion-maxima/`): las DLLs de audio y datos decompiladas, el `Data\`
> completo del cliente (5.726 archivos originales y 3.997 decodificados) y su manifiesto de
> formatos; y con su PR #45 (`reports/gameserver-passive-2026-10-30/`): una sesión real de juego
> capturada en el cable y, desde el commit `69be91d2`, trazas de OpenGL (GLIntercept) y tiempos de
> cada frame en la selección y en Lorencia (hasta `61012a80`). Las direcciones son VAs del binario (base `0x400000`).
>
> Este research describe **qué hace el original**, no cómo lo hace MuRet. Las decisiones de MuRet
> están en el [roadmap del cliente](../../../docs/superpowers/specs/2026-10-30-cliente-design.md).
> Cada tema termina con lo que implica para MuRet y qué test lo fija.

## Para qué sirve

El cliente de MU Online 1.02 (`main.exe`) es el programa que usa el jugador: abre una ventana
OpenGL, se conecta al ConnectServer, elige un GameServer, hace login, muestra la selección de
personaje y después el mundo 3D con el HUD. MuRet lo reescribe en C++20 multiplataforma con el
mismo comportamiento observable: los bytes que manda y entiende por la red, y la lectura de los
archivos de `Data\` del cliente original. Antes de escribir código hace falta saber con precisión
qué hace el original, incluidos sus errores.

A diferencia del DataServer, el cliente es enorme (5.648 funciones internas contra 1.572) y está
investigado de forma despareja: la red, el cifrado y los formatos de datos están validados contra
datos reales; el render tiene trazas de OpenGL del original (terreno, cámara, texto y skinning del
personaje verificados), y la animación de modelos quedó leída en el binario y verificada con esas
trazas; el audio, casi nada. Este research lo dice explícitamente en cada tema y [08](08-pendientes.md) junta los huecos.

## Índice

| # | Documento | Qué responde | Olas que lo usan |
| --- | --- | --- | --- |
| 01 | [Binario y método](01-binario-y-metodo.md) | qué build es, cómo se analizó, qué nivel de evidencia tiene cada cosa | todas |
| 02 | [Arranque y ciclo de vida](02-arranque-y-ciclo-de-vida.md) | de `entry` a la ventana, el bucle de mensajes, el frame y el apagado | CL3, CL5 |
| 03 | [Red y cifrado](03-red-y-cifrado.md) | recepción, corte de paquetes, XOR32, C3/C4 con `Enc1`/`Dec2`, seriales, FoxAnti | CL2, CL4 |
| 04 | [Protocolo](04-protocolo.md) | el dispatcher de 3 niveles, los opcodes, el flujo ConnectServer → GameServer → mundo | CL2, CL4, CL6, CL9 |
| 05 | [Formatos de datos](05-formatos-de-datos.md) | tablas `.bmd`, terreno, texturas OZJ/OZT, `message.wtf`, el pipeline de las DLLs | CL1, CL5, CL7 |
| 06 | [Render, escenas y UI](06-render-escenas-y-ui.md) | la máquina de escenas, cámara, primitivas 2D, texto, HUD y ventanas | CL5, CL7, CL8 |
| 07 | [Quirks y bugs](07-quirks-y-bugs.md) | catálogo numerado de rarezas del original y qué hace MuRet con cada una | todas |
| 08 | [Lo que falta saber](08-pendientes.md) | huecos de conocimiento, ruta crítica y cómo cerrarlos | todas |
| 09 | [Datos disponibles](09-datos-disponibles.md) | si alcanza lo que hay: el `Data\` contra lo que pide `main.exe`, el GameServer reconstruido, las texturas de las trazas | todas |

Orden de lectura sugerido para aprender: 01 → 03 → 04 → 05 → 02 → 06, y el resto como
referencia.

## Niveles de confianza

El repo de research marca cada hallazgo con **C** (confirmado), **I** (inferido) o **D**
(desconocido), y mide la validación en niveles N1 a N4 (N4 = contra datos reales: el cable
capturado, los `Data\` del cliente o la memoria del proceso vivo). Acá se traducen a los niveles
del research del DataServer:

| Nivel en MuRet | Equivale en el repo de research | Cómo se marca |
| --- | --- | --- |
| **Verificado** | C con N4 (cable, archivos reales o memoria del cliente vivo) | sin marca, con dirección citada |
| **Estático** | C solo contra el assembly, sin datos reales | *estático* |
| **Inferido** | I | *inferido* |
| **Análisis** | consecuencia que deriva este documento | **Análisis** |
| **Seam** | estructura conocida sin bytes exactos | *seam* |

El nivel *estático* es propio de este research: en el cliente hay mucho código leído con cuidado
en el assembly pero nunca contrastado con una sesión real (por ejemplo el login `F1 01` del build
actual). No es lo mismo que verificado.

## Receta para verificar cualquier dato

1. Tomar la dirección citada (por ejemplo `0x4e1d50`).
2. Buscar `decompiled/functions/` en `mainexe-vmxp-c32bbd5f/` (un `.c` y un `.asm` por función) o
   el informe del laboratorio en `analysis/mainexe/functions/` y `analysis/mainexe/evidence/`.
3. Contrastar con el `.asm`: tamaños, signo de las comparaciones y convención de llamada
   (`thiscall` usa `ecx` como `this`). El pseudocódigo de Ghidra nunca es la última palabra.
4. Para formatos y red, correr el script de referencia que cita el informe (`red_reference.py`,
   `c3c4_reference.py`) y los tests de `mu-client/` contra `mu-golden/`.

MuRet hereda esta receta: cada test de contrato del cliente cita la sección de este research que
fija.

## Dos builds que no se mezclan

| Build | SHA-256 | Para qué |
| --- | --- | --- |
| actual | `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a` | decompilación completa; constantes del login `10209` y `MULORDSVERSAO102` |
| histórico | `2f16a9c6bc0905a78ff2be3306d26673a4a2b0fda0956dba492d0a9b2b411de0` | laboratorio, trazas del cable, oráculos de memoria; login `09908` y `UK4l32T426CaMOy9` |

El `.text` de los dos es idéntico byte a byte; cambian `.rdata`, `.data` y recursos. Por eso el
código vale para los dos, pero **las constantes se toman del build actual**. MuRet apunta al build
actual.

## Glosario mínimo

| Término | Qué es |
| --- | --- |
| CS | ConnectServer: da la lista de GameServers. En el laboratorio, puerto 44405. |
| GS | GameServer: el servidor del mundo. En el laboratorio, GameServer1 en el puerto 55901. |
| opcode | byte de operación del paquete; en C1/C3 está en `pkt[2]`, en C2/C4 en `pkt[3]`. |
| C1 / C2 | paquetes en claro con tamaño de 1 o 2 bytes (el de 2 bytes en big-endian). |
| C3 / C4 | paquetes cifrados por bloques con `Enc1.dat` (envío) y `Dec2.dat` (recepción). |
| XOR32 | la ofuscación de 32 bytes que el motor de envío del cliente aplica a los C1/C2 (a todos en el GS; los `F4` del CS salen en claro). |
| FoxAnti | `FoxAnti.dll`, el anticheat del repack. Además de sus defensas, mete una capa de paquetes que el GS del laboratorio espera: ofusca (`0xAA`, remapeos) y traduce algunos formatos del laboratorio a los de `main.exe`. |
| `.bmd` | tablas binarias de datos del cliente (`Item.bmd`, `Skill.bmd`, `Gate.bmd`, `Text.bmd`) y también modelos 3D. |
| OZJ / OZT | texturas: JPEG o TGA con un prefijo de 24 o 4 bytes. |
| escena | estado global del cliente: 2 login, 4 selección de personaje, 5 en el juego. |
