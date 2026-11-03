# 02. Arranque y ciclo de vida

Fuentes: `analysis/mainexe/functions/paso9-ciclo-vida.md`, `paso7-escenas.md`,
`mainexe-vmxp-c32bbd5f/reports/entry-006a2444.md` y `winmain-004b9b80.md`; para el launcher, su
decompilado en `mu-online-reverse-lab`, `analysis/decompilacion-maxima/Mu-unpacked.exe/`.

## Del launcher al juego

Un launcher abre el juego. El de Webzen, `Mu.exe`, le deja el servidor en el registro, y
`main.exe` lo toma de ahí: la línea de comandos no la lee. El `main.exe` del laboratorio, además,
arranca solo. *Estático*, salvo donde se dice otra cosa:

1. **El launcher.** `0x406ae0`, lo que hace el botón `0x7d2` (su entrada en el mapa de mensajes de
   MFC, en `0x43ada8`), escribe en `HKLM\SOFTWARE\Webzen\Mu\Connection` un `Key` con
   `timeGetTime()`, los milisegundos desde que arrancó Windows (`0x412210`). Después `0x4127c0`,
   si ese `Key` tiene 5 s o menos, escribe `ParameterA` y `ParameterB`, el host y el puerto
   codificados, y vuelve a escribir `Key`; si no puede, el launcher muestra el texto 0x97 de su
   `message.wtf` y se cierra sin abrir el juego. Si puede, abre el juego con
   `ShellExecuteA(NULL, "open", "main.exe", "connect", NULL, SW_SHOW)` y se cierra. En el modo
   normal, el host va vacío (y el puerto, un `rand()`): la clave dice solo que lo abrió el
   launcher, y el servidor queda el compilado. En otro modo (el flag `0x44d05c`) el host va lleno,
   y también en los argumentos, `connect /y<host> /z<puerto>`, los dos codificados, con el puerto
   siempre en 44405. Otro camino, `0x40aa60`,
   abre el juego sin escribir la clave, con `connect` y el nombre del juego de argumentos. El
   nombre `main.exe` está armado con un XOR en el código (con un flag de servidor de prueba,
   `maintest.exe`). Si `ShellExecuteA` devuelve 0, el launcher muestra el texto 0x83 de su
   `message.wtf`; si devuelve 2 o 3 (no encontró el archivo o la carpeta), el 0x85, y si devuelve
   11 (no es un ejecutable), el 0x84. Con otros errores no muestra nada, y se cierra igual.
2. **La actualización.** `WinMain` cierra la ventana "MU Auto Update" si sigue abierta, y
   `0x4b96b0` termina el update ([05](05-formatos-de-datos.md#el-pipeline-de-las-dlls)).
3. **El servidor.** `0x6922c0`, que el log del original llama `wzPopLaunchInfo`, lee `Key`; si
   pasaron 26 s o menos (`timeGetTime() - Key <= 26000`, sin signo), lee y decodifica `ParameterA`
   y `ParameterB`. Borra la clave (`0x691bf0`) si la encuentra vieja o si no está, y si la lee
   bien; si es nueva y falla al leer `ParameterA` o `ParameterB`, la deja. Si el host no está
   vacío, `WinMain` lo copia a `0x5763304` (Q-C37) y pisa el ConnectServer que trae compilado: el
   puntero `0x6d5dc0`, que apunta a un host en `0x6c76e0` (en `c32bbd5f` un nombre, en `2f16a9c6`
   una IP), y el puerto de `0x6d5dc4`, 44405. Solo `WinMain` escribe ese puntero (`0x4ba047`), y
   con esos dos se conecta al ConnectServer (`0x4c1340`, desde `0x6349a0` y `0x642810`). Sin
   launcher, o con el host vacío, queda el compilado.
4. **Sin launcher.** Si `wzPopLaunchInfo` falla, el código que sigue (`0x4b9c17`) escribe en el
   log que falló y que vuelve a abrir el launcher, hace `WinExec("mu.exe")` y sale. En los dos
   `main.exe` del laboratorio no corre: el salto está anulado, con lo que parece un parche
   (Q-C34), y el juego arranca solo. Así se lo usa en la VM, con `start-mu.bat` y sin launcher, y
   se conecta: está verificado (`handoff-foxanti-2026-10-08-session21.md`, "Operativo de la
   sesión"; `handoff-foxanti-2026-10-08-session24.md`, la conexión `ESTABLISHED`, y
   `analysis/foxanti/functions/session32-scan-caller.md` §1).
5. **La línea de comandos no se lee.** En el código del juego, `GetCommandLineA` se llama en
   `WinMain` (`0x4b9e24`), `0x4ad360`, `0x4b90a0` y `0x4ad4a0`, y los cuatro le pasan lo que
   devuelve a `0x4ad270`, que se queda con el nombre del archivo del juego: lo que hay después de
   la última `\`, hasta el primer espacio, comilla, `/` o `\` (Q-C37). La otra llamada es la del
   CRT (`0x6a24ca`), que arma `lpCmdLine`, `__argc` y `__argv`, y nadie los lee: `WinMain` no usa
   su tercer argumento. Con ese nombre, `0x4ad360` abre su propio archivo y, si no lo encuentra,
   el juego sale (Q-C35), y `0x4aece0` lee su versión: la del log
   (`Mu online %s (%s) executed.`, o `unknown` si no puede) y la que se dibuja en pantalla
   (`0x6350b0`), que si no puede sale de `config.ini` (`[LOGIN] Version`, en la carpeta actual).
   Hay un parser de opciones, `0x4b9890`, que entiende `/y<host> /z<puerto>` codificados, como los
   arma el launcher, o `/u<ip> /p<puerto>`, pero nadie lo llama (Q-C36). El laboratorio lanza el
   juego como `main.exe connect /u172.16.198.1 /p44405` (`mu-client/CHECKPOINT.md`), y eso no
   cambia nada: la IP la pone la FoxAnti, y 44405 ya es el puerto compilado.
6. **La configuración** vive en el registro, en `HKCU\SOFTWARE\Webzen\Mu\Config` (`0x4b90a0`: `ID`,
   `SoundOnOff`, `MusicOnOff`, `Resolution`, `ColorDepth` y `TextOut`; más adelante, `VolumeLevel`;
   qué hacen `ID`, `Resolution` y `TextOut`, en [Resolución, fuentes y
   texto](#resolución-fuentes-y-texto)). `FoxAnti.Init` lee además `ConnectIp` de **HKCU** y escribe
   16 bytes sobre el host compilado, en `0x6c76e0` (`mu-client/docs/component-packet.md`).
7. **Los launchers del laboratorio.** El paquete trae tres
   (`decompilacion-maxima/metadata/analisis_pe.json`): `Mu.exe`, el de Webzen, el único
   decompilado; `Launcher.exe`, de Lords Games, en Visual Basic 6, cuyos textos nombran
   `LGMain.exe` y `lgconfig.ini`, que trae la IP (`c32bbd5f` venía como `LGMain.exe`); y
   `acceso.exe`, de FRANMU, cuyos textos nombran `main.exe` y `game.dll`, un INI con la IP y el
   puerto. Los dos últimos no están en el laboratorio, solo su análisis PE; según Emanuel,
   `acceso.exe` solo pasa los parámetros de conexión (session21). Como el juego no lee sus
   argumentos, solo los puede recibir por la clave de Webzen o por el `ConnectIp` de la FoxAnti;
   cuál usan, no se sabe. *Inferido*: el parche de Q-C34 es lo que les deja abrir el juego sin
   pasar por `Mu.exe`.

## De `entry` al bucle de mensajes

```text
entry 0x6a2444 (arranque del CRT)
  └─ WinMain 0x4b9b80 (RET 0x10: stdcall de cuatro argumentos)
       ├─ errores         0x67f4f0(0x4b9a70): el manejador que escribe MuError.dmp
       ├─ textos          0x61ee30: Data\Local\Text.bmd (0x5495e0) y Data\Macro.txt
       ├─ actualización   cierra "MU Auto Update" y 0x4b96b0 termina la del launcher; sale si
       │                  llegó un mu.exe nuevo (lo abre), si la ventana no se cierra en 6 s o si
       │                  falla una copia (05, el pipeline de las DLLs)
       ├─ servidor        0x6922c0 (wzPopLaunchInfo): el que dejó el launcher en el registro; si
       │                  no hay, el original vuelve a abrir mu.exe y sale, y el del laboratorio
       │                  sigue con el compilado (Q-C34)
       ├─ su archivo      0x4ad360: se abre a sí mismo; sale si no está en la carpeta actual (Q-C35)
       ├─ opciones        0x4b90a0: config.ini y el registro SOFTWARE\Webzen\Mu\Config
       ├─ modo de video   EnumDisplaySettingsA x2 + ChangeDisplaySettingsA
       ├─ ventana         0x4b8ff0: RegisterClassA("MU") + CreateWindowExA
       ├─ contexto GL     0x4b8e10: ChoosePixelFormat + wglCreateContext + wglMakeCurrent
       ├─ ShowWindow + UpdateWindow
       ├─ fuentes         CreateFontA x4 (abajo)
       ├─ volumen         VolumeLevel en el registro SOFTWARE\Webzen\Mu\Config
       ├─ timers          SetTimer x3
       └─ bucle           PeekMessageA en idle, GetMessageA al bloquear
             ├─ sin mensaje  mantenimiento del hash anti-tamper
             ├─ frame        0x63945d (orquestador) → 0x637700 (render loop)
             └─ WM_QUIT      0x456eb0 + 0x4aeda0 + 0x4017a0 y return
```

*Estático*: la cadena se resolvió por referencias de bytes (`call 0x637700` en `0x639501`,
`call 0x63945d` en `0x4be04a`).

### Resolución, fuentes y texto

Leído en la CL5.R; todo *estático*.

- **La resolución** es el valor `Resolution` del registro (`0x4b90a0`): 1 da 800 × 600, 2 1024 ×
  768, 3 1280 × 1024 y 4 1600 × 1200. Un 0, o el valor que falta, pasa a 1 (`0x4b9349`..`0x4b9353`),
  y 640 × 480 sale solo si no abre la clave `Config`: queda el 0 que puso antes (`0x4b92a2`). Con
  más de 4 (`ja`, sin signo, en `0x4b93bb`) no escribe nada y quedan los de la `.data`, 1024 × 768.
  W y H van a `0x6d5d60` y `0x6d5d64`, y las proporciones a `DAT_05762ea8` (W / 640) y
  `DAT_05762eac` (H / 480).
- **Las fuentes:** cuatro `CreateFontA(alto, 0, 0, 0, peso, 0, 0, 0, charset, 0, 0, 3, 0, cara)`
  (`0x4ba737`, `0x4ba777`, `0x4ba7b9` y `0x4ba813`), todas sin suavizado
  (`NONANTIALIASED_QUALITY`, el 3) y con el charset que lee de `0x6c76a0`, un dword de la `.data`
  que vale `0x86` (`GB2312_CHARSET`, Q-C67) y que nadie escribe (lo leen estas cuatro y
  `0x63f2f2`). La cara es un texto de `Text.bmd`, o `NULL` (la que elija el sistema) si empieza
  con `\0`:

  | Global | Peso | Alto | Cara |
  | --- | --- | --- | --- |
  | `DAT_05763554` | 400 | n − 1 | el texto 0 |
  | `DAT_05763558` | 700 | n − 1 | el texto 0 |
  | `DAT_0576355c` | 700 | 2 · (n − 1) | el texto 0 |
  | `DAT_05763560` | 400 | 12 | el texto 18 |

  n es `DAT_07758568`, que se escribe ahí (`0x4ba6af`..`0x4ba6d3`): 12 con W = 640, 13 con 800, 14
  con 1024 y 15 con 1280. La otra escritura, en `0x556333` (dentro de `0x555b00`), la acota a 32, y
  con estos valores no la cambia. Con 1600 no se escribe y, como está en la `.bss`, vale 0: las
  alturas quedan en -1, -1 y -2 (Q-C59).
- **El texto:** `TextOut` (0 si no está) elige cómo se dibuja: con 1, la caché de `0x466000` con
  los colores `0x02`; con otro valor, un `TextOutA` por texto, el camino de las trazas
  ([06](06-render-escenas-y-ui.md#texto)).
- **La cuenta guardada:** `ID` se lee a `DAT_057630a0` con un tope de 11 bytes, y nadie en
  `main.exe` lo escribe (*inferido*: lo deja el launcher). La ventana de la cuenta la pone en su
  campo ([06](06-render-escenas-y-ui.md#la-cuenta-y-la-clave)).

## El frame

El orquestador `0x63945d` elige por el global de escena `DAT_006d5dc8`: la 1 va a `0x622e40`, la 3
a `0x6356f0` y la 2 (login), la 4 (selección) y la 5 (juego) al render loop `0x637700`. Antes deja
en `DAT_07d36ba8` un valor que late con el reloj (0,6 + 0,15 por el seno o el coseno de
0,004 · T). Después del frame, la mayor parte de sus 7 KB arma paquetes del grupo `F1` cuando un
contador (`DAT_05763578`) pasa de 31; sin leer en detalle. *Estático.*

El render loop `0x637700` hace, en orden: cámara, color de cielo por mapa, la escena que toque
(ver [06](06-render-escenas-y-ui.md)), objetos, HUD, `SwapBuffers` y música. *Estático.*

### Las cinco escenas y sus transiciones

Quién escribe `DAT_006d5dc8`, leído en la CL5.R en el desensamblado (*estático*):

| De | A | Dónde | Con qué |
| --- | --- | --- | --- |
| (arranque) | 1 | la `.data` | `DAT_006d5dc8` vale 1 en el archivo |
| 1 | 2 | `0x622e40` | al terminar la carga ([06](06-render-escenas-y-ui.md#la-carga-escena-1)) |
| 2 | 4 | `0x634c60` | el login aceptado: el `F1 01` deja el estado de la conexión, `DAT_057678c0`, en `0x14`; la escena lo ve en su paso, lo pasa a `0x32` y manda el `F3 00` |
| 4 | 3 | `0x6342c0` y `0x633c10` | en la lógica de la selección (que corre solo con el estado en `0x33` o más, la lista de personajes): Enter con un personaje elegido (`DAT_006d5edc`, de 0 a 4), o doble clic sobre un personaje (el de `0x6c8e30`, que pasa a ser el elegido) con la ventana de la selección (`+0x7f50`) abierta; o el botón de esa ventana (`0x633c10`, desde `0x63eb9a`, en su `vtable + 0x30`). Si el personaje tiene el bit `0x1` de `+0x1d8`, abre el aviso `0x84` en vez de entrar. Enter pide además `[input + 0x31]` en 0 y cuatro ventanas cerradas, y suena el `0x19`; el doble clic, no. Al entrar copia el nombre, la clase, la piel y el nivel al personaje propio y suelta lo de la selección (`0x61cc50`; CL6.R, [06](06-render-escenas-y-ui.md#la-selección-de-personaje)) |
| 3 | 5 | `0x6356f0` | al terminar la carga del mundo, que dibuja un solo frame con las cuatro `LSBg` en mosaico y las suelta (CL6.R); el `F3 03` lo manda el primer paso de la 5 (`0x636290`), no la selección ([04](04-protocolo.md#la-selección-de-personaje-f3)) |
| 5 o 4 | 4 o 2 | `0x4c2710` | la respuesta del `F1 02`: con `pkt[4]` en 1 pasa a la 4, y con 2 a la 2, y en ese caso cierra el GS, pone en 0 la bandera del login (`DAT_07d3a13d`, `0x4c2f24`) y corta: al volver se carga todo otra vez, y vuelve a conectar al CS. Las dos, solo si el paquete llegó cifrado: el cuarto argumento del dispatcher (el "cifrado" de [03](03-red-y-cifrado.md), `[ebp+0xc]` en `0x4c2781` y `0x4c2f5e`); sin cifrar no cambia de escena, y lo que hace llama a la tabla de hash de `0x40b240` (no se siguió). Con `pkt[4]` en 0, `SendMessageA(WM_DESTROY)` (`0x4c391b`): el juego se cierra, sin aviso y sin mirar si llegó cifrado (Q-C106). De la escena solo mira si es la 5, para limpiar el juego (`0x4c275c`), y el dispatcher no la mira: también la cambiaría desde la 1, la 2 o la 3 (Q-C79) |

Los estados de la conexión que importan acá son 0 (sin GS), 2 (el `F1 00` aceptado), `0x13` (el
login mandado), `0x14` (aceptado), `0x32` (la lista de personajes pedida) y `0x33` (llegó).
Ninguna otra función escribe la escena: los ocho `mov` a `DAT_006d5dc8` del binario son estos,
todos con un número fijo (`0x4c2f1a`, `0x4c363f`, `0x622f93`, `0x633f05`, `0x634470`, `0x634638`,
`0x634dce` y `0x635a91`). La primera entrada a la selección (`0x633f20`, desde
`0x6342c0`) pone el mapa 54, `World55` (05), sube las texturas y los modelos de la escena
(`0x61ca30`), arma la vista previa de la ventana de crear (el modelo `0xa9`) y pone la UI en el
modo 4 ([06](06-render-escenas-y-ui.md#la-selección-de-personaje)).

### El ritmo: lógica a 25 por segundo, dibujo a 21,33

El render loop tiene dos relojes (`analysis/mainexe/functions/paso6-ui-map.md` §4 y el decompilado
de `0x637700`, releído en la CL3.R):

```text
0x459b60 / 0x459c00          delta del frame con QueryPerformanceCounter (timeGetTime si no hay),
                             con tope de 200 ms; lo recibe la UI (0x65fc30), no el acumulador
while (DAT_006d5ee4 >= 0x28) bucle de "alcanzar" (CMP + JL, 0x637791..0x637798), sin tope
   ├─ lógica de la escena según DAT_006d5dc8 (y el hash anti-tamper)
   └─ DAT_006d5ee4 −= 0x28                                0x638167
inicio = GetTickCount()                                   0x63852b, después de los pasos
dibujo de la escena → glFlush + SwapBuffers
while (GetTickCount() − inicio < 0x28) {}                 espera activa, 0x6385fd..0x638624
DAT_006d5ee4 += GetTickCount() − inicio                   0x638632
```

La espera compara con signo: `CMP ...,0x28` en `0x63860f` y `JGE` en `0x638616`. *Estático*.

- **La lógica avanza en pasos de 40 ms (25 por segundo)** y, si el frame tardó, el `while` corre
  varios pasos seguidos para alcanzar al reloj.
- **El acumulador es un `int` con signo y no tiene tope.** *Estático*: `CMP [0x6d5ee4],0x28` en
  `0x637791` y `JL` en `0x637798`, `SUB EAX,0x28` en `0x638167` y `ADD` en `0x638632`. Un frame
  que pasa 10 s en el dibujo, el swap o la espera corre 250 pasos seguidos (Q-C44).
- **El acumulador arranca en 40 y no se reinicia.** *Estático*: en la `.data` de `c32bbd5f`,
  `0x6d5ee4` vale `0x28` (40), así que el primer frame del juego ya corre 1 paso. Los bytes de la
  dirección aparecen 5 veces en el archivo, y las 5 son operandos de `0x637700`: el `CMP` de
  `0x637791`, los dos `MOV` de `0x638162` y `0x63816a`, alrededor del `SUB`, y los dos de
  `0x63862c` y `0x638638`, alrededor del `ADD`. Nada lo pone en cero al cambiar de escena, así
  que el resto pasa de una escena a la otra: ni por su dirección ni por un puntero guardado
  (ningún dword de la `.data`, la `.rdata` o la `.rsrc` apunta a `0x6d5e00`..`0x6d5ee7`, y el
  archivo no tiene `.reloc`). Lo que queda es una escritura desbordada, que no se puede descartar
  sin ejecutarlo.
- **La medición empieza después de los pasos** (`0x63852b`): suma el dibujo, el swap y la espera,
  y **el tiempo de los pasos no se acumula**. Lo que pasa fuera de la medición se pierde: el
  principio del frame (`0x459b60`, el reloj T de `0x4eb620`, el mouse de `0x640940` y la UI de
  `0x65fc30`), los pasos, el `glClearColor` y el `glClear` de `0x6384f5`..`0x638516`, el bucle de
  mensajes de `WinMain`, la red, una ventana arrastrada. Y se pierde de a ticks enteros: solo
  cuenta si cruza el borde de un tick de `GetTickCount`. *Análisis*: con pasos de 10 ms, un frame
  de 1 paso no pierde nada y uno de 2 pasos pierde un tick. Por eso un paso lento frena el juego en
  vez de espiralar.
- **El otro delta**, `0x459b60` sobre el temporizador de `[0x5763564]`, es el tiempo desde el
  frame anterior (`0x459c00` lo rearma en cada frame), con QPC o `timeGetTime`, y con tope de
  200 ms (`0x6ba668` vale 200,0). Lo recibe el administrador de ventanas de la UI (`0x65ee60`
  devuelve el objeto, `0x7d53728`, y `0x65fc30` lo actualiza), no el acumulador. *Estático*.
- **La espera mide con `GetTickCount`**, que no avanza de a 1 ms sino de a un tick del reloj de
  Windows: 15,625 ms (64 por segundo) en la VM XP y en un Windows con el tick por defecto. Dos ticks
  son 31,25 ms (menos de 40) y tres son 46,875 ms, así que **la espera termina en el tercer tick**:
  el dibujo queda en 46,875 ms por frame, **21,33 FPS** (64 / 3), no en 25.
- La lógica no se atrasa: el acumulador suma lo que midió `GetTickCount` (46 o 47 ms, 46,875 de
  media) y consume 40 por paso. 64 frames suman 3.000 ms, así que corre **75 pasos cada 64
  frames**, con 11 frames de 2 pasos, y sigue en 25 por segundo; 6 frames seguidos dan 7 u 8 (los
  frames 31 a 36 dan 2, 1, 1, 1, 1, 2). *Análisis*: la aritmética del bucle y una simulación del
  de `0x637700` sobre una grilla de 15,625 ms, con el acumulador en 40 al empezar: los 13 primeros
  frames dan 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2, y desde el segundo cada 64 frames seguidos
  suman 75 y cada 6, 7 u 8 (los 6 primeros suman 6). Antes este documento decía "7 pasos cada 6
  frames", que no es exacto.
- Con un tick de 10 ms (otro HAL) la espera terminaría a los 40 ms justos: los 25 FPS son la
  intención del código, no lo que se ve en una máquina común. *Inferido* para otros equipos.
- MuMain conserva 25 FPS para el cliente 5.2
  ([ADR 0001 de MuMain](https://github.com/sven-n/MuMain/blob/main/docs/adr/0001-preserve-25-fps-reference-timing.md)).

**Verificado** en las dos escenas, en
[mu-online-reverse-lab#45](https://github.com/EmaNLJN/mu-online-reverse-lab/pull/45)
(`graphics-01/`, commits `16dc0221`, `ddcdd64`, `61012a80` y `cb575444`). Un `opengl32.dll` proxy que solo toma
`QueryPerformanceCounter` antes y después de cada `wglSwapBuffers` (0,12 a 0,14 ms por llamada)
registró, con la escena quieta y sin carteles:

| | Selección | Lorencia |
| --- | --- | --- |
| Archivo | `swap-times-valid-final.csv` | `swap-times-lorencia-valid-final.csv` |
| Intervalos | 3.510 en 164,56 s | 3.596 en 168,58 s |
| FPS | **21,33** | **21,33** |
| Intervalo medio / mediana | 46,884 / 46,912 ms | 46,879 / 46,898 ms |
| En el tercer tick de 15,625 ms | 99,57 % | 99,89 % |
| A ±1 ms / ±2 ms de 46,875 ms | 79,23 % / 95,84 % | 86,60 % / 97,72 % |
| Mediana del residuo a la grilla | 0,507 ms | 0,379 ms |
| Concentración de fase con 15,6250 ms | 0,953 | 0,963 |

- **Los frames están enganchados a una grilla de 15,625 ms.** Buscando el período que mejor alinea
  los swaps entre 15,60 y 15,65 ms gana 15,6250 en las dos escenas (15 ms o 16 ms dan menos de
  0,06), y la fase no se corre entre los cuatro cuartos de cada serie. Asignando cada swap a su
  tick, todos caen sobre una sola grilla de 15,62500 ms, con una desviación de 0,7 a 0,8 ms. Un
  reloj de QPC no tendría por qué alinear los frames así; uno de ticks de 15,625 ms, sí.
- **La grilla es la del tick, no un período de 46,875 ms.** Con 46,875 ms la concentración baja a
  0,75 en la selección y a 0,89 en Lorencia: los pocos frames de 2, 4 o 5 ticks corren la fase de
  46,875 (la selección gana 2 ticks en total y Lorencia 1), pero no la de 15,625. Después de cada
  uno, la serie sigue enganchada al tick.
- **Un swap tarde no arrastra al siguiente.** La mayoría de los intervalos fuera de los 3 ticks
  vienen de a pares: uno largo (54 a 61 ms) y enseguida uno corto (34 a 39 ms), que suman entre 91
  y 96 ms, dos frames de 46,875 (7 pares en la selección, 2 en Lorencia). El frame que viene
  después vuelve a la grilla: el plazo lo pone el reloj, no el swap anterior, como en la espera
  con `GetTickCount`. El único frame de 5 ticks (83,5 ms, en la selección) viene de un
  `wglSwapBuffers` que tardó 74 ms en volver: un corte de la VM o del driver, no del juego.
- **El ritmo no cambia con la escena ni con el tiempo.** El CSV de Lorencia tiene la sesión
  entera, 31.331 swaps en 24,5 minutos: cada minuto da 21,2 a 21,3 FPS (el primero 20,8, por el
  arranque), con solo 6 frames de más de 200 ms (cuatro al arrancar y dos en el medio).
- **VSync no interviene en esta VM.** Los swaps no se alinean con ninguna frecuencia de pantalla
  común: la concentración de fase da 0,03 o menos con 60, 59,94, 70, 75, 85, 100 y 120 Hz (contra
  0,95 con 15,625 ms), y `wglSwapBuffers` vuelve en 0,10 a 0,11 ms de mediana (p99 0,3 a 0,4 ms), sin esperar
  ningún retrazo vertical. En el binario `c32bbd5f…`, `wglSwapIntervalEXT` y
  `wglGetSwapIntervalEXT` solo aparecen en dos stubs de carga perezosa (`0x69e830` y `0x69e860`:
  piden la función a `wglGetProcAddress` y la guardan en `0x6dbb84` y `0x6dbb88`), y nada en el
  código llama esos punteros. Coincide con `VSYNC-NOTES.md` del laboratorio. Que un driver fuerce
  VSync desde afuera es otra historia, sin medir.
- Las cifras de los JSON del laboratorio se reprodujeron todas desde los CSV. Desde `cb575444` dan
  21,33 FPS (intervalos sobre duración) y separan los dos conteos a ±2 ms de la selección: 3.364 a
  46,875 ms y 3.365 al tick más cercano, porque un intervalo de 61,27 ms es de 4 ticks. En Lorencia
  las dos cuentas dan 3.514.
- La medición anterior (unos 20,9 FPS con GLIntercept, `FPS-MEASURE-RESULT.json`) incluye la carga y
  el frenado de GLIntercept; `FPS-SELECTION-RESULT.json` quedó inválida por un cartel de
  desconexión.
- Lo que el swap no puede probar: que la espera sea la de `0x637700` y los 75 pasos cada 64
  frames. Eso sigue siendo *estático* y *análisis* (arriba).

### Qué corre en cada paso y qué en cada frame

*Estático*, del decompilado de `0x637700`. **Por paso de 40 ms**, en orden:

```text
hash anti-tamper del contador de pasos DAT_07d3a0f0   (se rearma con tamaños al azar
                                                     cuando el contador % 313 == 57; no se porta)
0x623e70 (40 KB)        solo en la escena 5 y con DAT_05762eb4 == 0: la interfaz del juego
                        (teclado, mouse, ventanas, paquetes; ver la tabla de abajo)
lógica de la escena     2 → 0x634c60, 4 → 0x6342c0, 5 → 0x636290 (ahí avanza el cuerpo de
                        cada modelo, ver 06)
0x456b10(0,005) × 5     una lista de objetos con 5 subpasos de 0,005 y un valor al azar
                        acotado entre −0,2 y 1 (DAT_007245f0; ¿viento?, inferido)
0x5547e0, 0x5554b0      dos temporizadores de 301 y 151 pasos (12 y 6 s) que llaman a
                        0x554610 y 0x555190 (mensajes en pantalla, inferido)
IMPR PANT               en todas las escenas: alterna el pedido de captura (DAT_07d368a8)
DAT_05767940, DAT_077f3b74   cuentas regresivas: −1 por paso hasta 0
DAT_07d2d360            ciclo de 32 pasos: (n + 1) mod 32
DAT_07d3a0f0 + 1, DAT_07d3a144 + 1, DAT_006d5ee4 − 40
```

**Por frame:** `0x4eb620` toma `timeGetTime` y lo guarda como `float` en `DAT_05767b88` (el reloj T
del brillo de los items, [06](06-render-escenas-y-ui.md); también cuenta los FPS), el delta de
`0x459b60` (tope de 200 ms), el dibujo de la escena (2 → `0x6350b0`, 4 → `0x634680`,
5 → `0x6371a0`), la animación de mascota y alas dentro de ese dibujo, la captura si se pidió
(`0x6202e0`) y `SwapBuffers`, solo si el dibujo de la escena devolvió verdadero.

La animación del cuerpo avanza por paso con la velocidad de su acción (0,26 y 0,28 keys en las
trazas) y la de mascota y alas por frame dibujado: **verificado** en las trazas de GL, ver
[06](06-render-escenas-y-ui.md#cuánto-avanza-la-animación).

### Qué avanza por paso, por frame y por reloj (P-C03)

La CL3.R leyó cada cosa que avanza en `0x637700`, la lógica de la escena 5 (`0x636290`), el
principio de `0x623e70` y los usos de los contadores. *Estático*, salvo donde se dice otra cosa.
"Por reloj" quiere decir que avanza con `timeGetTime` o `GetTickCount`, sin contar pasos ni
frames:

| Qué | Dónde | Avanza | MuRet |
| --- | --- | --- | --- |
| el acumulador y el contador de pasos | `0x637791`..`0x638638`; `DAT_07d3a144` + 1 por paso | por paso; la medición, por frame desde `0x63852b` | igual (CL3.2), Q-C44 |
| el cuerpo de cada modelo | `0x636290` y la tabla de velocidades de `0x60dbe0` ([06](06-render-escenas-y-ui.md)) | por paso, **verificado** | por paso; el dibujo interpola (CL6) |
| mascota y alas | `0x509340` | por frame dibujado, **verificado** (Q-C20) | por tiempo (CL6) |
| la lista de 5 subpasos de 0,005 | `0x456b10`, 5 veces por paso | por paso | por paso |
| dos temporizadores de 301 y 151 pasos | `0x5547e0`, `0x5554b0` | por paso | por paso |
| `DAT_05767940` | lo ponen en 70 (2,8 s) seis copias en línea del código que manda un mensaje, cada una con el último texto (`0x57676e4`): cuatro en `0x59ff10` (`0x5a129d`, `0x5a193c`, `0x5a1fad`, `0x5a25f1`), una en `0x448580` (`0x44895f`) y una en el WndProc (`0x4b5810`). La versión en función, `0x5ae370` (`0x5ae3e8`), solo la llama `0x59fea0`, y a `0x59fea0` no la llama nadie: no tiene llamadas directas y su dirección no aparece en el archivo. En `0x5ae370`, con más de 50 no manda otro, y con más de 0 no manda el mismo texto que el último | por paso; lo leen la lógica y el WndProc, ningún dibujo | por paso (CL8). Que sea el freno del chat, *inferido* |
| `DAT_077f3b74` | lo ponen en 100 (4 s) dos escrituras en línea de `0x59ff10` al mandar una macro (`0x5a1ea1`, `0x5a2b3d`); `0x59fea0` hace lo mismo (`0x59ff01`), pero no lo llama nadie (arriba). Mientras no sea 0 (`TEST` y `JNE` en `0x5a116f`), `0x59ff10` no lee las teclas de los dígitos que eligen la macro (`0x5a11a4`; la tabla de macros, de 256 bytes cada una, en `0x77f1db0`) | por paso; lo lee el HUD (`0x5a7320`, `0x5ab6d0`), que dibuja una barra de ancho `n · 50 / 100` | por paso; la barra muestra el último paso, sin interpolar (CL8) |
| `DAT_07d2d360`, el ciclo de 32 pasos | elige una de 32 texturas animadas: en el terreno del mundo 7 el slot `0x1e + n` (`0x5fa830`), y en los modelos con la textura `0x41`, el `0x41 + n` (`0x4ecc30`, `0x4ef340`, `0x42e3d0`, `0x438fe0`) | por paso; lo lee el dibujo | por paso y sin interpolar: una textura no se interpola (CL6, CL7) |
| la interfaz del juego | `0x623e70`, solo en la escena 5: lee Esc (`0x553520(0x1b)`, `0x623eb5`) y el mouse de los globales del WndProc, abre y cierra ventanas (`DAT_07d3a10c`), hace sonar (`0x41ed10`, 27 llamadas) y arma paquetes (68 referencias al motor `0x6b85f4` en el desensamblado; el decompilado muestra 69) | por paso | por paso (CL8, CL9) |
| la captura de pantalla | el pedido, por paso y en todas las escenas (`0x553520(0x2c)`, `0x637cff`, después del `switch` de la lógica, `0x637cab`..`0x637cb9`); Shift (`0x638305`) y la captura (`0x6202e0`), por frame | paso y frame | CL8 |
| el reloj T del frame | `0x4eb620` guarda `timeGetTime` en `DAT_05767b88` (Q-C22), una vez por frame y antes de los pasos (`0x63776c`): los dos pasos de un frame ven el mismo T | por frame; lo leen 79 funciones: 22 desde el paso y 51 desde el dibujo (*análisis*, abajo) | cada una en su ola (CL6 a CL9) |
| la UI nueva: foco, arrastre y el delta | `0x65fc30`, con el delta de `0x459b60` (tope de 200 ms) y el mouse de `0x640940`: busca la ventana bajo el cursor, la arrastra (`0x669f40`) y le pasa el delta a cada ventana (métodos `+0x28` y `+0x30` de su vtable) | por frame, con el delta real | por frame y por tiempo real (CL8); qué hace cada ventana con el delta, sin leer |
| las demoras de las habilidades | una tabla de `int` en `[0x7681eb0] + 0x76c`, una por casilla de habilidad del personaje; `0x552c50` y `0x552990` cargan la de una casilla con el campo `+0x28` del registro de `Skill.bmd` de su habilidad (`0x3c` bytes por registro, desde `[0x76bd1d8]`) y no dejan usarla mientras sea mayor que 0 | **por reloj**: en la escena 5, `0x4eb620` le pasa a `0x552f10` (`0x4eb758`) los ms de `timeGetTime` desde el frame anterior, sin tope, que los resta de cada una sin bajar de 0 (alrededor, el hash del bloque, que no se porta) | por reloj real, no por pasos (CL9) |
| un freno de 300 ms entre acciones | `_DAT_0576791c`, con `GetTickCount`, en `0x57cfc0`, `0x559890`, `0x55b6f0`, `0x55cbc0` y `0x55faa0`, todas desde el paso: si `abs(GetTickCount() - último)` es 300 o menos (`CMP ...,0x12c` + `JLE`) no manda, y si no, anota la hora y arma un C1. Cada comprobación se salta, y manda sin mirar el freno, cuando un valor que la acompaña vale `0x28` (un byte en `0x55ac91`, un local en `0x55cdc3`) | **por reloj** | por reloj real (CL9). *Análisis*: en el modo fiel, con ticks de 15,625 ms, se abre a los 312 o 313 ms |
| el keepalive | `WM_TIMER` `0x3E8`, cada 20 s, con foco o sin él (abajo) | **por reloj** (un mensaje) | por reloj, con un `IntervalTimer` en la `ServerSession`, consultada una vez por frame y solo con el login aceptado (CL4.5) |
| la conexión | `0x4e8560`, después de la espera, en la escena 5 y con el byte `0x7d3a13c` distinto de 0 (`0x638640`): con -1 deja `> Connection closed` en el log una vez (`DAT_07d3a148`) y llama `0x6236f0(0x70)`, la ventana `0x70` de la interfaz del juego (Q-C70) | por frame | por frame (CL4) |

El censo del reloj es *análisis*, contado en el desensamblado: 80 funciones tocan `DAT_05767b88`;
`0x4eb620` la escribe y 79 la leen. El decompilado muestra 64 (63 y la que escribe), porque
esconde la lectura cuando a un `FLD [0x5767b88]` le sigue `CALL 0x69fd3c` (`__ftol`): queda un
`__ftol()` suelto, como en `0x634c60`, la lógica de la escena 2. Qué corre en el paso o en el
dibujo sale del grafo de las llamadas directas, armado del desensamblado y del decompilado, que dan
lo mismo: 22 se alcanzan desde el paso (`0x623e70`, la lógica de las escenas, `0x456b10`,
`0x5547e0`, `0x5554b0`), 51 desde el dibujo (`0x6350b0`, `0x634680`, `0x6371a0` y lo que sigue en
`0x637700`; dos, `0x53fe70` y `0x5fc260`, desde los dos) y 8 desde ninguno de los dos (una es
`0x63945d`, el que llama al frame, que lo lee en `0x639482`). Las que se llaman por vtable no están
contadas. Además, 25 funciones de esos árboles llaman directo a `timeGetTime` o `GetTickCount`, 17
desde el paso y 9 desde el dibujo (una, `0x518cf0`, desde los dos). Lo que una lógica por paso lee
del reloj avanza por reloj aunque corra en el paso: con el dibujo desacoplado, MuRet tiene que
darle un reloj, no solo pasos. Lo que falta leer de cada una queda en [08](08-pendientes.md), P-C03.

**Son tres relojes**, *estático*: `GetTickCount` para la espera y el freno de 300 ms;
`timeGetTime` para el reloj T y el delta de las demoras (`0x4eb62c`); y `QueryPerformanceCounter`
para el delta de la UI (`0x459b60`) y el doble clic (`0x459bc0`), con `timeGetTime` si no hay.
`timeBeginPeriod` solo se llama en el constructor de ese temporizador, `0x459a70` (`0x459aa1`), en
el camino en que `QueryPerformanceFrequency` da 0: con QPC, el cliente no sube la resolución del
reloj de Windows. Qué resolución tienen `timeGetTime` y QPC en la VM no está fijado: `timeGetTime`
va con el periodo del sistema, que otro proceso o una DLL puede bajar.

Cada frame termina con `glFlush` y `wglSwapBuffers` (2.375 de cada uno en la sesión) y empieza con
`glClear` (2.381). **Verificado** en las estadísticas de GLIntercept.

## El WndProc `0x4af4e0`

| Mensaje | Qué hace |
| --- | --- |
| `WM_DESTROY` (0x2) | apagado: bandera `DAT_05763568 = 1`, cierra el socket (`0x4e8380`), libera, `wzAudioDestroy`, cierra GL (`0x4ad180`), cierra el handle de su propio archivo (`0x4ad3c0`, Q-C35), `PostQuitMessage(0)` |
| `WM_ACTIVATE` (0x6) | `DAT_05763580` = 0 con `WA_INACTIVE` (`0x4af6e5`) y 1 con cualquier otro (`0x4af86c`): el foco, que decide si corre el frame (abajo) |
| `WM_PAINT` (0xF) | `BeginPaint`/`EndPaint` vacío: el dibujo no va por `WM_PAINT` |
| `WM_ERASEBKGND` (0x14) | devuelve 1, no borra |
| `WM_SETCURSOR` (0x20) | `ShowCursor(0)`: el cursor lo dibuja el juego |
| `WM_MOUSEMOVE` (0x200) | la posición en 640 × 480: `x · 640 / ancho` en `DAT_07d3682c` y `y · 480 / alto` en `DAT_07d36828` (el tamaño, en `0x6d5d60` y `0x6d5d64`) |
| `WM_LBUTTONDOWN`, `WM_LBUTTONUP` (0x201, 0x202) | `DAT_07d366d4` en 1 al apretar, si no estaba ya abajo (`DAT_07d36874`), y en 0 al soltar, que además guarda dónde (`0x576358c`, `0x5763590`); la lógica lo pone en 0 cuando lo usa (`0x623e70`) |
| `WM_RBUTTON*`, `WM_MBUTTON*` (0x204 a 0x208) | banderas propias (`DAT_07d3685c`, `DAT_07d36864`), con contadores del hash alrededor; según `paso9-ciclo-vida.md`, cámara y zoom. Qué hace cada bandera, sin leer |
| `WM_MOUSEWHEEL` (0x20A) | `DAT_07d366d8` = delta / 120, en muescas; lo consume quien lo lee (las listas, `0x479960` y sus vecinas, lo ponen en 0) |
| sin foco | lo que no atiende el primer `switch` (`WM_SYSCOMMAND`, los mensajes de 0x2 a 0x20 de esta tabla, `0x400`, `0x401`, `0x2B11`, `WM_TIMER` y `0x500`..`0x5FF`) pasa por la prueba del foco (`0x4b33fe`..`0x4b3427`) y, sin foco, va a `DefWindowProcA` (`0x4b3439`) sin tocar nada: el segundo `switch` entero, con `WM_CHAR` (0x102), los `WM_IME_*` de 0x10D a 0x10F, el mouse (0x200 a 0x20A) y 0x282. Sin foco se pierden el mouse, el texto del chat y el IME |
| `WM_CLOSE` (0x10) | no tiene caso: cae a `DefWindowProcA` (sin foco en `0x4b3439`, con foco en `0x4b8c0f`), que destruye la ventana y lleva a `WM_DESTROY` |
| `WM_SYSCOMMAND` (0x112) | bloquea `SC_KEYMENU` y `SC_MONITORPOWER` |
| `WM_TIMER` (0x113) | el id menos `0x3e8` por la tabla de `0x4b8c5f`, del 1000 al 1003. El 1000, cada 20 s: `thunk_FUN_00450376`, que no hace nada, y el keepalive `0x0E` si hay conexión al GS (`DAT_05767918`), sin mirar el login (Q-C53; [04](04-protocolo.md)); no mira el foco (`0x4af945`..`0x4b1a43` no lee `DAT_05763580`). El 1001 es del anticheat |
| `WM_USER` (0x400) | los eventos del socket principal, por `lParam & 0xFFFF`: 1 (`FD_READ`) lee, 2 (`FD_WRITE`) drena y `0x20` (`FD_CLOSE`) avisa y cierra ([03](03-red-y-cifrado.md#sockets)) |
| `0x2B11` | señal del anticheat: cierra el cliente (`paso4-killer.md`) |

**El teclado no usa `WM_KEYDOWN`**: se lee con `GetAsyncKeyState`, casi siempre en el paso
(abajo). Los eventos de red llegan como mensajes de ventana (`WSAAsyncSelect`,
`FD_READ`/`FD_WRITE`, ver [03](03-red-y-cifrado.md)). *Estático.*

### El teclado y el mouse: en el paso y en el frame (P-C08)

*Estático*, del decompilado y el desensamblado; qué función corre en el paso o en el frame sale
del grafo de las llamadas directas, sin las que van por vtable (*análisis*). Contadas por las
referencias a la entrada de `GetAsyncKeyState` en la IAT (`0x6b84fc`), 36 funciones la llaman. 34
son funciones de Ghidra: 11 se alcanzan desde el paso y 3 desde el dibujo (una, `0x558bb0`, desde
los dos), dos corren en el frame (`0x637700` y `0x640940`, la del mouse) y una es `0x553520`, el
flanco de abajo; las otras 18 no se alcanzan por llamadas directas (entre ellas, las ventanas y
listas de la UI, que van por vtable). Las dos que faltan, Ghidra no las definió y el decompilado no
las muestra: `0x6418b0`, después del `RET` de `0x641880`, una ventana más que lee Enter y Esc con
`0x8001` (por frame, abajo); y `0x59fe10`, después de `0x59fb80`, una copia de la lectura de Alt y
de los dígitos de `0x59ff10` que no llama nadie (no tiene llamadas directas y su dirección no
aparece en el archivo). Leen la tecla de tres formas:

- **"Está abajo":** `(valor >> 8) == 0x80` o `!= 0`, el bit alto. Es la mayoría.
- **"Se apretó", con el bit bajo:** `valor == 0x8001` (`-0x7fff`): abajo ahora y apretada desde la
  última llamada. Así leen Esc y Enter la lógica de las escenas 2 y 4 (`0x634c60`, `0x6342c0`) y
  seis ventanas de la UI nueva (Enter y Esc `0x63d880`, `0x642e30` y `0x6418b0`; Esc `0x63f630`,
  `0x644730` y `0x647d40`), y así lee `0x640940` los botones del mouse.
- **"Se apretó", con un flanco propio:** `0x553520(vk)` mira el bit alto y lo compara con lo que vio
  la vez anterior, en una tabla de un `int` por tecla (`0x77f36d4`), una sola para todos los que la
  llaman. Tiene 21 llamadas, todas con la tecla fija: Esc, Tab, Enter, Alt, RePág, AvPág, Impr
  Pant, E, F, Q, S, W, F5 y F6 (quién, en la tabla de abajo).

Las teclas de cada escena, desde su lógica por paso: la 2 (login, `0x634c60`) lee Esc; la 4
(selección, `0x6342c0`), Enter y Esc; la 5 (juego, `0x636290` y `0x623e70`), Esc, F5 y F6
(`0x490490`), Shift (`0x59adf0`, `0x652330`, y con los dígitos en `0x420e30`), Ctrl (`0x5588d0`,
`0x558eb0`, `0x5c4110`), Q, W y E con el flanco (`0x5c4110`), S (`0x5e4ce0`), Espacio y F1 a F4
(`0x5ac8d0`: `0x5ac8ea` y `0x5ac9d0`..`0x5acad2`), RePág y AvPág (`0x64d090`), y en `0x59ff10`
RePág, AvPág, las flechas arriba y abajo, Ctrl, Alt (`0x5a1129`, `0x5a1191`), los dígitos, que
con Alt eligen una macro y sin Alt llaman a `0x59f810` (sin leer), y catorce letras: B, C, D, E,
F, G, I, M, P, Q, R, T, V y W (`0x5a35a8`..`0x5a4f36`). Impr Pant no es de una escena: `0x637700`
la lee en cada paso después del `switch` de la lógica (`0x637cab`..`0x637cb9`), en todas
(`0x637cff`). **Por frame** se leen Shift en `0x637700` (`0x638305`, para el nombre de la
captura), Alt en el dibujo (`0x59ec80`, `0x5bbbc0`; Alt se lee también en el paso, en
`0x59ff10`), los botones del mouse, y en las seis ventanas de arriba Esc, y Enter solo en tres
(`0x63d880`, `0x642e30` y `0x6418b0`): antes de los pasos, `0x63778c` llama a `0x65fc30`, que
llama a `0x669f40` por cada ventana (`0x65fd6a`), y esta a su método `+0x30` (`0x66a044`), que en
las seis es la que lee la tecla (la vtable `0x6ba900`, por ejemplo, tiene `0x6418b0` en `+0x30`).

**Qué tecla lee quién.** *Estático*, del desensamblado: cada llamada a `GetAsyncKeyState` (123,
por la IAT `0x6b84fc` o por un registro cargado de ahí) y a `0x553520` (21), con la tecla que le
pasa. Todas la pasan fija, salvo cuatro lazos sobre los dígitos (`0x420e5e`, `0x5a113d`,
`0x5a11a8` y `0x59fe60`) y la de adentro de `0x553520`, que pasa la suya. Cada lector va con su
función y, entre paréntesis, la dirección de la llamada. De dónde se alcanza cada función sale de
las llamadas directas (*análisis*): **P** desde el paso, **D** desde el dibujo, **F** por frame
(`0x637700` y `0x640940`, y las ventanas que llama `0x65fc30` por vtable), **V** sin llamadas
directas desde el paso ni desde el dibujo (van por vtable) y **N** no la llama nadie (`0x59fe10`).
Las **P** se alcanzan desde la lógica de la escena 5 (`0x636290`, `0x623e70`), salvo `0x634c60`
(la 2), `0x6342c0` (la 4) y la de Impr Pant (todas); `0x5588d0` y `0x558eb0`, también desde la 2
y la 4.

| Tecla | VK | Bit alto | `== 0x8001` | `0x553520` |
|---|---|---|---|---|
| botones del mouse | `0x01`, `0x02`, `0x04` | | `0x640940` (`0x6409df`, `0x640aa2`, `0x640b65`) F | |
| Tab | `0x09` | | | `0x40f6d0` (`0x40f6ea`) V |
| Enter | `0x0D` | | `0x6342c0` (`0x634343`) P; `0x63d880` (`0x63d91a`), `0x642e30` (`0x642e4b`), `0x6418b0` (`0x64192d`) F | `0x4a43c0` (`0x4a43ed`), `0x4a4e20` (`0x4a4e34`) V |
| Shift | `0x10` | `0x420e30` (`0x420e4d`), `0x59adf0` (`0x59ddf1`), `0x652330` (`0x6525f9`) P; `0x637700` (`0x638305`) F | | |
| Ctrl | `0x11` | `0x5588d0` (`0x558b86`), `0x558eb0` (`0x558fd6`), `0x59ff10` (`0x5a0905`), `0x5c4110` (`0x5c4ac1`) P; `0x558bb0` (`0x558e7f`) P y D; `0x632dd0` (`0x632dd5`) V | | |
| Alt | `0x12` | `0x59ff10` (`0x5a1129`, `0x5a1191`) P; `0x59ec80` (`0x59ef37`), `0x5bbbc0` (`0x5bbd91`) D; `0x59fe10` (`0x59fe4b`) N | | `0x5bbbc0` (`0x5bbd6c`) D |
| Esc | `0x1B` | | `0x634c60` (`0x634d3a`), `0x6342c0` (`0x634485`) P; `0x63d880` (`0x63d956`), `0x642e30` (`0x642e8d`), `0x6418b0` (`0x641962`), `0x63f630` (`0x63f650`), `0x644730` (`0x644822`), `0x647d40` (`0x647e98`) F | `0x623e70` (`0x623eb5`), `0x490490` (`0x4904bc`) P |
| Espacio | `0x20` | `0x5ac8d0` (`0x5ac8ea`) P | | |
| RePág | `0x21` | `0x59ff10` (`0x5a00a3`) P | | `0x64d090` (`0x64d0d6`, `0x64d127`) P; `0x650350` (`0x650387`) V |
| AvPág | `0x22` | `0x59ff10` (`0x5a00db`) P | | `0x64d090` (`0x64d0f3`, `0x64d144`) P; `0x650350` (`0x6503a4`) V |
| izquierda, derecha | `0x25`, `0x27` | `0x467b10` (`0x467b51`, `0x467b5e`) V; `0x479960` y sus nueve vecinas, que descartan el valor (`0x479999`, `0x47999d`) V | | |
| arriba | `0x26` | `0x59ff10` (`0x5a0126`) P; `0x467b10` (`0x467b37`), `0x479960` y sus nueve vecinas (`0x4799a1`) V | | |
| abajo | `0x28` | `0x59ff10` (`0x5a027a`) P; `0x467b10` (`0x467b44`), `0x479960` y sus nueve vecinas (`0x4799eb`) V | | |
| Impr Pant | `0x2C` | | | `0x637700` (`0x637cff`), en el paso de todas las escenas |
| 0 | `0x30` | `0x59ff10` (`0x5a0fba`, `0x5a115b`, `0x5a1eba`) P; `0x59fe10` (`0x59fe7e`) N | | |
| 1 a 9 | `0x31`..`0x39` | `0x59ff10`, una por una (`0x5a0915`..`0x5a0f05`) y en dos lazos (`0x5a113d`, `0x5a11a8`), y `0x420e30`, del 1 al 4 en un lazo (`0x420e5e`) P; `0x59fe10`, en un lazo (`0x59fe60`) N | | |
| A | `0x41` | `0x5acbe0` (`0x5acf63`) V | | |
| B, C, G, I, M, P, T, V | `0x42`, `0x43`, `0x47`, `0x49`, `0x4D`, `0x50`, `0x54`, `0x56` | `0x59ff10` (`0x5a4ec4`, `0x5a4d54`, `0x5a4ca0`, `0x5a4d96`, `0x5a4e38`, `0x5a4cdc`, `0x5a4d18`, `0x5a4dd8`) P | | |
| D | `0x44` | `0x59ff10` (`0x5a4e71`) P; `0x5acbe0` (`0x5acf81`) V | | |
| E | `0x45` | `0x59ff10` (`0x5a44b3`) P; `0x5acbe0` (`0x5acf45`) V | | `0x5c4110` (`0x5c4af8`) P |
| F | `0x46` | `0x59ff10` (`0x5a4dff`) P; `0x5acbe0` (`0x5acffb`) V | | `0x4a2f70` (`0x4a2feb`) V |
| Q | `0x51` | `0x59ff10` (`0x5a35a8`) P; `0x5acbe0` (`0x5acf27`) V | | `0x5c4110` (`0x5c4ad1`) P |
| R | `0x52` | `0x59ff10` (`0x5a4f36`) P; `0x5acbe0` (`0x5acfdd`) V | | |
| S | `0x53` | `0x5acbe0` (`0x5acfbe`) V | | `0x5e4ce0` (`0x5e5678`) P |
| W | `0x57` | `0x59ff10` (`0x5a3d2c`) P; `0x5acbe0` (`0x5acf9f`) V | | `0x5c4110` (`0x5c4ae3`) P |
| F1 a F4 | `0x70`..`0x73` | `0x5ac8d0` (`0x5ac9d0`, `0x5aca44`, `0x5aca88`, `0x5acad2`) P | | |
| F5 | `0x74` | | | `0x490490` (`0x490548`) P; `0x467b10` (`0x467d3b`) V |
| F6 | `0x75` | | | `0x490490` (`0x490563`) P |

Las nueve vecinas de `0x479960` son `0x47b0f0`, `0x47c870`, `0x47e010`, `0x47f7b0`, `0x480e00`,
`0x4824d0`, `0x483bc0`, `0x4852b0` y `0x486910`. Las diez leen las flechas en el mismo orden y
tiran el valor de la izquierda y la derecha (el decompilado muestra `GetAsyncKeyState(0x25);` y
`GetAsyncKeyState(0x27);` sueltos), así que esas dos lecturas solo borran el bit bajo (Q-C45).
Ninguna otra tecla pasa por `GetAsyncKeyState` ni por `0x553520`: nadie lee F7 a F12
(`0x76`..`0x7B`), las letras H, J, K, L, N, O, U, X, Y y Z, Retroceso, Inicio, Fin, Insert, Supr
ni el teclado numérico (el texto del chat llega por `WM_CHAR`, arriba). De Shift, Ctrl y Alt el
original pasa solo los códigos genéricos (`VK_SHIFT`, `VK_CONTROL` y `VK_MENU`: `0x10`, `0x11` y
`0x12`), nunca los de un lado (`0xA0`..`0xA5`); con los genéricos, según la documentación de
`GetAsyncKeyState`, no se distingue la izquierda de la derecha.

Lo que implica (*análisis*):

- Un frame con 2 pasos no ve dos veces el mismo apretón: el primer paso borra el bit bajo, o deja
  la tabla de `0x553520` en 1.
- Una tecla apretada y soltada entre dos lecturas se pierde: `GetAsyncKeyState` da `0x0001`, que no
  es `0x8001` ni tiene el bit alto. Entre dos pasos de frames seguidos pasan 46 o 47 ms.
- Quién ve un apretón depende del orden y de la forma (Q-C45). Cualquier llamada sobre la tecla,
  de cualquiera de las tres formas, borra el bit bajo: un lector con `0x8001` que venga después no
  lo ve. `0x553520` mira solo el bit alto y su tabla, así que a un lector de `0x553520` solo se lo
  esconde otro de `0x553520`. Y los lectores por frame corren antes que los del paso: si una de las
  ventanas lee Esc, la lógica de las escenas 2 y 4, con `0x8001`, ya no lo ve, y `0x553520(0x1b)`
  en el paso (`0x623e70`, `0x490490`) sí. Con Enter pasa lo mismo entre las tres ventanas que lo
  leen y la escena 4.
- La tabla de `0x553520` vuelve a 0 solo cuando alguien llama a `0x553520` con la tecla arriba: si
  ningún lector corre mientras la tecla está suelta, el apretón siguiente se pierde.

**El mouse son dos sistemas.** El WndProc deja la posición en 640 × 480 y el botón izquierdo en
globales (la tabla de arriba), que leen la lógica y la UI vieja: `DAT_07d366d4` aparece en unas
100 funciones. Y en cada frame, antes de los pasos, `0x640940` actualiza el objeto `0x7d53540`
(que devuelve `0x640850`) para la UI nueva (`0x65fc30`, `0x63c490`): `GetCursorPos` y
`ScreenToClient`, acotada al tamaño del área (`+0x28`, `+0x2c`), y el movimiento desde el frame
anterior; los botones izquierdo, derecho y del medio con `GetAsyncKeyState(1)`, `(2)` y `(4)`,
donde `0x8001` es apretar, `0` es soltar si estaba abajo y `0x8000` (sigue abajo) no cambia nada;
y el doble clic, si el apretón llega antes de `GetDoubleClickTime()` (leído en `0x6408a0`) desde el
anterior, medido con el temporizador de `0x459bc0`. Una bandera (`+0x30`) cambia el izquierdo por
el derecho.

### Sin foco no corre el frame

*Estático*, del desensamblado de `WinMain`. El bucle de mensajes llama a `PeekMessageA`
(`0x4bbc0e`) con las banderas en 0 (`PM_NOREMOVE`), que vuelve enseguida; con un mensaje lo saca
con `GetMessageA` y lo despacha, y sin mensajes hace el mantenimiento del hash (no se porta),
consulta `IsIconic` (`0x4bdaef`), que solo elige una rama del hash, y en `0x4be019` lee el foco,
`DAT_05763580`. El bucle no duerme nunca: ni `WinMain` ni lo que llama en cada vuelta sin foco
(por llamadas directas) llama a `Sleep`, y `WaitMessage` y `MsgWait*` ni están importadas.

- **Con foco** (`JE` en `0x4be041`, no tomado), llama al frame: `0x63945d` en `0x4be04a`, y con él a
  los pasos y el dibujo.
- **Sin foco** salta a `0x4be057`: `SetForegroundWindow` y `SetFocus` sobre la ventana, en cada
  vuelta sin mensajes, y **no llama al frame**: no corren los pasos, ni el dibujo, ni la espera que
  suma al acumulador. Sin espera y sin dormir, el bucle gira sin parar. En `0x4be14c` una prueba
  sobre un contador envuelto en el hash (`DAT_0576357c`) elige entre dos caminos; qué cuenta ese
  contador es del anticheat, fuera de alcance:
  - uno llama `SetTimer(ventana, 0x3e9, 1000)` y `PostMessageA(ventana, WM_CLOSE)`
    (`0x4be1f2`..`0x4be217`); `WM_CLOSE` cae a `DefWindowProcA` (en la tabla del WndProc, arriba),
    que destruye la ventana;
  - el otro, desde `0x4be222`, llama `ShowWindow(ventana, SW_MINIMIZE)` en `0x4be454` y después
    `ShowWindow(ventana, SW_MAXIMIZE)` en `0x4be5ad`: todos sus caminos pasan por los dos.
- Las dos ramas terminan en `0x4be5b3` con `0x4e1d50` y `0x49e510`, que corren en cada vuelta:
  el primero despacha todos los paquetes de la cola de la conexión principal, y el segundo, los de
  cada conexión del chat (CL4.R, [03](03-red-y-cifrado.md#sockets)).

Mientras `DAT_05763580` valga 0, lo que va por paso queda en pausa, porque el acumulador no suma
nada y al volver no alcanza, pero lo que va por reloj no (*análisis*, sobre la tabla de arriba). En
el primer frame con foco, en la escena 5, `0x4eb620` le pasa a las demoras de las habilidades
(`0x552f10`) todo el tiempo sin foco, porque ese delta no tiene tope (`0x4eb681`..`0x4eb696`, y el
`PUSH` de `0x4eb757`): se cumplen todas en ese frame. El reloj T y el freno de 300 ms de
`GetTickCount` también siguen la hora real; solo el delta de la UI tiene tope, de 200 ms. Los
mensajes se siguen despachando, y con ellos la red y el keepalive: el timer `0x3E8` no mira el
foco, *estático* (arriba). Qué ve el jugador sin foco (una pausa, el foco que vuelve o el cierre)
queda para una prueba dinámica, con alt-tab en la VM. MuRet no lo copia (Q-C46).

### Una discrepancia entre documentos

`paso9-ciclo-vida.md` dice "timer `0x3e8` (1 s)". El informe del keepalive
(`mu-client/docs/component-net-envio.md` §9) dice que `0x3E8` es el **id** del timer y que el
periodo instalado es de 20.000 ms, y el cable lo confirma: 156 keepalives `0x0E`, uno cada 20 s
(`analysis/mainexe/evidence/s31-cifrado/README.md`). **El dato bueno es 20 s.** El timer de 1 s es
otro: el del anticheat, id 1001 (`paso4-killer.md` §1).

En el desensamblado (CL4.R): el `SetTimer(ventana, 1000, 20000, NULL)` está en `WinMain`, en
`0x4bad89`, después de poner la primera escena (`0x632d80`) y antes del bucle de mensajes. Es
el único `SetTimer` con el 1000 y el binario no importa `KillTimer`: el timer corre desde el
arranque hasta el cierre, también antes de conectar. Los otros: el 1001 en `0x4b3196`,
`0x4bd8c3` y `0x4be205`, el 1002 (15 s) en `0x48e5f7` y el 1003 en `0x46b071`. Qué manda el
1000 y cuándo, en [04](04-protocolo.md) ("Quién manda el keepalive y cuándo").

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| `WinMain` + `WndProc` + `wgl*` | una capa `Platform` sobre una biblioteca portable de ventana e input (decisión D-C3 del roadmap); `main.cpp` solo arma y arranca | CL3 |
| El servidor: el compilado, o el que deja el launcher en el registro (`wzPopLaunchInfo`); la config en el registro; los argumentos no se leen | archivo de config + argumentos; se acepta `connect /u<ip> /p<puerto>`, lo que entiende el parser sin usar del original (Q-C36) y como ya se lo lanza en el laboratorio | CL3 |
| Sin el servidor del launcher, el original vuelve a abrir `Mu.exe` y sale; en el laboratorio, ese salto está anulado (Q-C34) | arranca solo, como el del laboratorio: no hay launcher | — |
| Se abre a sí mismo por el nombre de la línea de comandos, en la carpeta actual, y si no está, sale sin avisar (Q-C35); copia ese nombre y el host del launcher sin mirar el largo (Q-C37) | no se abre a sí mismo; la carpeta de datos sale de la config, y el host y el puerto se validan | CL3 |
| Lógica en pasos de 40 ms; dibujo esperando 40 ms con `GetTickCount`, que en la práctica da 46,875 ms | la lógica sigue en pasos de 40 ms con el mismo bucle de alcanzar; el dibujo deja de esperar e interpola entre pasos (D-C4 del roadmap); el modo fiel restaura la espera con un reloj de 15,625 ms, para dar los mismos 21,33 FPS | CL3 |
| Teclado por `GetAsyncKeyState`, casi todo leído en el paso; "se apretó" por el bit bajo o por un flanco propio, que pierden un apretón corto (Q-C45); el mouse por mensajes y, para la UI nueva, consultado en cada frame | estado del teclado y del mouse armado con eventos, detrás de `Input`; un apretón lo ve un solo paso | CL3 |
| Sin foco no corre el frame: ni pasos, ni dibujo, ni la espera (Q-C46) | los pasos siguen sin foco y minimizada; minimizada no se dibuja, y tampoco mientras el login conecta, que en el original bloquea el bucle (Q-C85) | CL3, CL5.5 |
| Lo que avanza por reloj: las demoras de las habilidades y el reloj T (`timeGetTime`), el freno de 300 ms (`GetTickCount`) y el delta de la UI (QPC) | por el reloj del cliente, no por pasos, aunque la lógica corra en el paso | CL6 a CL9 |
| Cinco escenas en un entero que escriben ocho lugares, con números fijos | `Scenes::SceneManager` con la tabla de transiciones de arriba: una que no está es un error en el log | CL5 |
| Cinco resoluciones, y la letra de a un punto por ancho, que en 1600 × 1200 queda en -1 y -2 (Q-C59) | las cinco del original o un modo del monitor, con la ventana fija (`App::IsValidResolution`, decidido el 2026-11-03); la letra por escalones de ancho, con el de 1280 desde 1280 en adelante | CL5 |
| Red por mensajes de ventana | un socket no bloqueante que el bucle consulta en cada frame; mismo modelo de un solo hilo | CL4 |
| Keepalive por `WM_TIMER` cada 20 s | temporizador del bucle, testeable con un reloj falso | CL4 |
| Fin del auto-update del launcher (`0x4b96b0`: copia `mu._xe`, `mumsg._ll`, `wz_zp._ll` y `message._tf` sobre sus nombres) | no se porta: es del launcher ([05](05-formatos-de-datos.md#el-pipeline-de-las-dlls)) | — |
| Anticheat (hash, timer 1001, `0x2B11`) | **fuera de alcance** | — |

Tests que fija:

- El bucle principal con un reloj falso: exactamente 25 pasos de lógica por segundo simulado, con
  1, 25 o 240 frames dibujados, y el alcanzar ante un frame de 130 ms (3 pasos seguidos) (CL3).
- El modo fiel con un reloj falso de 15,625 ms: 64 frames cada 3 segundos y, desde el segundo
  frame, 75 pasos de lógica cada 64 frames, 11 de ellos con 2 (7 u 8 cada 6) (CL3).
- El keepalive sale cada 20 s de reloj simulado y no antes (CL4.5,
  `ServerSessionTest.KeepaliveEveryTwentySecondsAndNotBefore`).
- Las transiciones de escena de la tabla, y que una que no está se rechace (CL5.4).
- La letra de cada ancho: 12, 13, 14 y 15 en 640, 800, 1024 y 1280, y 15 en 1600 (CL5.3).
- El parser de la línea de comandos acepta `connect /u<ip> /p<puerto>` como el original, con las
  letras en mayúscula o minúscula, y rechaza un puerto que no sea un número de 1 a 65535; qué hace
  con `/u` sin `/p` se decide en la CL3 (CL3).
- Sin la carpeta de datos, el arranque termina con un error que la nombra (CL3; un archivo que
  falta adentro es Q-C12).
