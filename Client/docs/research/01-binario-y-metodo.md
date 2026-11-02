# 01. Binario y método

## Identidad del binario

| Dato | Valor | Evidencia |
| --- | --- | --- |
| Archivo | `main.exe` del cliente "Mu Baires 3", 3.058.308 bytes | `mainexe-vmxp-c32bbd5f/reports/build-identity.md` |
| SHA-256 | `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a` | ídem |
| Formato | PE32 x86, GUI, 5 secciones, ImageBase `0x400000`, entry `0x6A2444` | ídem |
| Compilador | MSVC 6.0 (linker 06.00), compilado el 2006-02-21; sin PDB ni símbolos | `analysis/mainexe/functions/triage-fase-a.md` §2 |
| `SizeOfImage` | ~127 MB: `.data` declara `0x7698B64` bytes virtuales, casi todo BSS (tablas grandes del juego) | triage §3 |
| Gráficos | OpenGL 1.x de función fija (52 funciones GL, 2 GLU) | `coverage-ui-sistemas.md` §2 |
| Audio | `wzAudio.dll` (música), DirectSound (efectos), WINMM | ídem |
| Idioma | mensajes en portugués brasileño sin acentos (cargados al arrancar); recursos y nombres en coreano CP949 | `paso3-protocolo.md` §2b y §5 |
| Funciones | 5.648 internas + 355 contratos externos (imports) | `mainexe-vmxp-c32bbd5f/README.md` |

**No está empacado ni virtualizado.** El `.text` RWX es un residuo de un ASPack ya desempacado. La
sección `.Silvana` (347 bytes en el build histórico) no es un packer: es un loader que hace
`LoadLibrary("FoxAnti.dll")` y llama a su export `Init`. Las protecciones fuertes (VMProtect +
ASPack) están en `FoxAnti.dll`, no en `main.exe` (triage §3).

## Cómo se analizó

1. **Decompilación total, dos veces.** Ghidra 12.1.4 en el host y Ghidra 12.1.3 en un contenedor
   Kali, headless, cada uno por su lado: 5.648/5.648 funciones con C y assembly, 0 errores. 5.644
   pseudocódigos idénticos entre motores; las 4 diferencias están revisadas
   (`reports/engine-differences.md`).
2. **Laboratorio por pasos** (`analysis/mainexe/functions/paso*.md`): red y protocolo (pasos 3 y
   5), anticheat (4), escenas y render (7 y 10), ciclo de vida (9), texto (8 y 11), HUD y ventanas
   (12 a 17).
3. **Assembly como fuente de verdad** para offsets, tamaños, signo y convención de llamada.
4. **Validación contra datos reales (N4):** el cable capturado con `muproxy` entre el cliente y el
   servidor del laboratorio (72 conexiones), los `Data\` del cliente copiados de la VM XP y
   volcados de la memoria del cliente vivo (`mu-golden/memoria-cliente/`). Desde 2026-10-30, una
   sesión real de juego en el cable y trazas de OpenGL del cliente vivo (GLIntercept, 5 frames de la
   selección de personaje y 5 de Lorencia) en `mu-online-reverse-lab#45`.
5. **Reimplementación con tests** en C99 (`mu-client/`): 13/13 tests, también con ASan/UBSan.
   Es la referencia ejecutable de este research, pero no reemplaza al assembly.

## Qué tan cubierto está cada subsistema

Según la última medición del laboratorio (`mu-client/docs/coverage-main.md` §0, porcentaje de bytes
analizados del código propio del cliente):

| Subsistema | Tamaño | Analizado | Validado o reconstruido |
| --- | ---: | ---: | --- |
| M1 plataforma (ventana, GL, bucle) | 156 KB | 70 % | estático; las funciones de GL que usa (GLIntercept) y el ritmo del frame (proxy de `SwapBuffers`, en la selección y en Lorencia), medidos |
| M2 formatos de assets | 92 KB | 29 %, más la tanda 3 | tablas, terreno y texturas byte a byte contra la memoria del original |
| M3 red y protocolo | 293 KB | 72 % | recepción, C3/C4, corte, login y keepalive contra el cable ([04](04-protocolo.md)) |
| M4 bucle por frame y escenas | 750 KB | 15 % | estático |
| M5 mundo 3D | 185 KB | 20 % | renderer de terreno identificado y verificado celda por celda contra una traza de GL; cámara y objetos del `.obj` en la misma traza; skinning y pose del personaje reproducidos contra las trazas, su código sin leer ([06](06-render-escenas-y-ui.md)) |
| M6 UI | 365 KB | 65 % | estático; el quad 2D, los viewports y el camino del texto verificados en las trazas de GL |
| M7 audio | 13 KB | 8 % | en `main.exe`, poco; pero `wzAudio.dll` y sus decodificadores están decompilados enteros (`decompilacion-maxima`, ver [05](05-formatos-de-datos.md)) |

El 30 % analizado es mapa del binario, no código reconstruido. Para MuRet esto marca el orden: lo
validado se porta primero, con tests; lo estático se porta con tests de contrato y se marca
pendiente de verificar; lo que falta pide una sub-ola de research (CL*n*.R) antes.

## Las tres fuentes y para qué sirve cada una

| Fuente | Qué es | Cómo la usa MuRet |
| --- | --- | --- |
| `mainexe-vmxp-c32bbd5f/decompiled/` | C y assembly de cada función | carril R: leer el original para entender un comportamiento |
| `analysis/mainexe/` | informes por tema, scripts de referencia en Python, evidencia | la fuente de este research; los scripts generan vectores de prueba |
| `mu-client/` | reimplementación C99 con tests y fixtures | **oráculo**, no destino: sus vectores pasan a ser tests de MuRet; su código no se copia |
| `mu-online-reverse-lab`: `analysis/decompilacion-maxima/` | `wzAudio`, `vorbisfile`, `ogg`, `mumsg`, `wz_zp` y `wzCipher` decompiladas con sus ABI (`abi/`); `assets/original-data/` (el `Data\` entero) y `assets/decoded/`; `assets_manifest.csv` con formato, transformación y metadatos de los 5.774 archivos | contrato de las DLLs que el cliente reemplaza (audio sobre todo; `mumsg`, `wz_zp` y `wzCipher` son del launcher y no se reemplazan, [05](05-formatos-de-datos.md#el-pipeline-de-las-dlls)) y **la carpeta golden**: los tests de datos reales apuntan ahí |

Los datos reales del cliente (`mu-golden/` y ahora `assets/original-data/` de
`mu-online-reverse-lab`) **no se versionan en MuRet**: los tests que los usan reciben la ruta por
una opción de CMake y se saltean si no está. Con el `Data\` completo disponible, los tests golden
pueden recorrer **todos** los archivos de un formato, no solo los 15 de `mu-golden`.

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Un binario de 32 bits, MSVC 6, solo Windows | C++20, 64 bits, Windows y Linux, CMake con presets | CL0 |
| Sin símbolos: los nombres son de Ghidra (`FUN_004e2b10`) | nombres propios; cada clase cita en un comentario la VA del original que reemplaza | todas |
| Dos builds con las mismas instrucciones y distintas constantes | se apunta al build `c32bbd5f…`; las constantes del build van a un solo lugar | CL2 |
| `FoxAnti.dll` cargada por `.Silvana` | no hay DLL: la parte de ofuscación de paquetes es una capa opcional de `Network`, el resto del anticheat queda afuera | CL2 |

Tests que fija: ninguno por sí mismo. Este documento define cómo se cita la evidencia en los tests
de los demás.
