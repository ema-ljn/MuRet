# 02. Arranque y ciclo de vida

Fuentes: `analysis/mainexe/functions/paso9-ciclo-vida.md`, `paso7-escenas.md`,
`mainexe-vmxp-c32bbd5f/reports/entry-006a2444.md` y `winmain-004b9b80.md`; para el launcher, su
decompilado en `mu-online-reverse-lab`, `analysis/decompilacion-maxima/Mu-unpacked.exe/`.

## Del launcher al juego

El launcher (`Mu.exe`) abre el juego y le pasa el servidor por el registro, no por la línea de
comandos. El `main.exe` del laboratorio, además, arranca solo. *Estático*, salvo donde se dice otra
cosa:

1. **El launcher.** Después del update, `0x406ae0` escribe en
   `HKLM\SOFTWARE\Webzen\Mu\Connection` un `Key` con la hora (`timeGetTime`, en `0x412210`) y, con
   `0x4127c0`, `ParameterA` y `ParameterB`: el host y el puerto, codificados. Después abre el juego
   con `ShellExecuteA(NULL, "open", "main.exe", "connect", NULL, SW_SHOW)` y se cierra. En un modo
   (el flag `0x44d05c`) los argumentos son `connect /y<host> /z<puerto>`, también codificados; en el
   normal, el host va vacío. Otro camino, `0x40aa60`, abre el juego igual, sin escribir la clave.
   El nombre `main.exe` está armado con un XOR en el código (con un flag de servidor de prueba,
   `maintest.exe`), y si `ShellExecuteA` falla, el launcher muestra uno de los textos 0x83, 0x84 o
   0x85 de su `message.wtf`.
2. **La actualización.** `WinMain` cierra la ventana "MU Auto Update" si sigue abierta, y
   `0x4b96b0` termina el update ([05](05-formatos-de-datos.md#el-pipeline-de-las-dlls)).
3. **El servidor.** `0x6922c0`, que el log del original llama `wzPopLaunchInfo`, lee `Key`; si
   pasaron 26 s o menos (`timeGetTime() - Key <= 26000`), lee y decodifica `ParameterA` y
   `ParameterB`. Borra la clave en los dos casos (`0x691bf0`). Si el host no está vacío, `WinMain`
   lo copia a `0x5763304` y pisa el ConnectServer que trae compilado: el puntero `0x6d5dc0`, que
   apunta a un buffer de 16 bytes en `0x6c76e0` con un host, y el puerto de `0x6d5dc4`, 44405. Con
   esos dos se conecta (`0x4c1340`, que llaman `0x6349a0` y `0x642810`). Sin launcher, o con el
   host vacío, queda el compilado.
4. **Sin launcher.** Si `wzPopLaunchInfo` no encuentra la clave, o es vieja, el código que sigue
   (`0x4b9c17`) escribe en el log que falló y que vuelve a abrir el launcher, hace
   `WinExec("mu.exe")` y sale. En el laboratorio no corre: el salto está anulado con un parche
   (Q-C34), y el juego arranca solo. Así se lo usa en la VM, sin el launcher, y se conecta
   (`mu-client/CHECKPOINT.md`, sección VMXP, y `mu-client/docs/component-packet.md`): eso está
   verificado.
5. **La línea de comandos no se lee.** `GetCommandLineA` se llama en `WinMain` (`0x4b9e24`),
   `0x4ad360`, `0x4b90a0` y `0x4ad4a0`, y las cuatro le pasan lo que devuelve a `0x4ad270`, que se
   queda con el nombre del archivo del juego: lo que hay después de la última `\`, hasta el primer
   espacio, comilla, `/` o `\`. Con ese nombre, `0x4ad360` abre su propio archivo y, si no lo
   encuentra, el juego sale (Q-C35), y `0x4aece0` lee su versión, la que escribe el log
   (`Mu online %s (%s) executed.`) y la que se dibuja en pantalla (`0x6350b0`); si no puede,
   `0x4b90a0` usa la de `config.ini` (`[LOGIN] Version`). Hay un parser de opciones, `0x4b9890`,
   que entiende `/y<host> /z<puerto>` codificados, como los arma el launcher, o `/u<ip> /p<puerto>`,
   pero nadie lo llama (Q-C36). El laboratorio lanza el juego como
   `main.exe connect /u172.16.198.1 /p44405` (`mu-client/CHECKPOINT.md`), y eso no cambia nada: la
   IP la pone la FoxAnti, y 44405 ya es el puerto compilado.
6. **La configuración** vive en el registro, en `HKCU\SOFTWARE\Webzen\Mu\Config` (`0x4b90a0`:
   `ID`, `SoundOnOff`, `MusicOnOff`, `Resolution`, `ColorDepth` y `TextOut`; más adelante,
   `VolumeLevel`). `FoxAnti.Init` lee además `ConnectIp` de **HKCU** y pisa los 16 bytes del host
   compilado, en `0x6c76e0` (`mu-client/docs/component-packet.md`).

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
       │                  no hay, sigue con el compilado (Q-C34)
       ├─ su archivo      0x4ad360: se abre a sí mismo; sale si no está en la carpeta actual (Q-C35)
       ├─ opciones        0x4b90a0: config.ini y el registro SOFTWARE\Webzen\Mu\Config
       ├─ modo de video   EnumDisplaySettingsA x2 + ChangeDisplaySettingsA
       ├─ fuentes         CreateFontA x4 (HUD y texto)
       ├─ config          registro SOFTWARE\Webzen\Mu\Config
       ├─ ventana         0x4b8ff0: RegisterClassA("MU") + CreateWindowExA
       ├─ contexto GL     0x4b8e10: ChoosePixelFormat + wglCreateContext + wglMakeCurrent
       ├─ timers          SetTimer x3
       ├─ ShowWindow + UpdateWindow
       └─ bucle           PeekMessageA en idle, GetMessageA al bloquear
             ├─ sin mensaje  mantenimiento del hash anti-tamper
             ├─ frame        0x63945d (orquestador) → 0x637700 (render loop)
             └─ WM_QUIT      0x456eb0 + 0x4aeda0 + 0x4017a0 y return
```

*Estático*: la cadena se resolvió por referencias de bytes (`call 0x637700` en `0x639501`,
`call 0x63945d` en `0x4be04a`).

## El frame

El orquestador `0x63945d` elige por el global de escena `DAT_006d5dc8`: la 1 va a `0x622e40`, la 3
a `0x6356f0` y la 2 (login), la 4 (selección) y la 5 (juego) al render loop `0x637700`. Antes deja
en `DAT_07d36ba8` un valor que late con el reloj (0,6 + 0,15 por el seno o el coseno de
0,004 · T). Después del frame, la mayor parte de sus 7 KB arma paquetes del grupo `F1` cuando un
contador (`DAT_05763578`) pasa de 31; sin leer en detalle. *Estático.*

El render loop `0x637700` hace, en orden: cámara, color de cielo por mapa, la escena que toque
(ver [06](06-render-escenas-y-ui.md)), objetos, HUD, `SwapBuffers` y música. *Estático.*

### El ritmo: lógica a 25 por segundo, dibujo a 21,33

El render loop tiene dos relojes (`analysis/mainexe/functions/paso6-ui-map.md` §4 y el decompilado
de `0x637700`):

```text
0x459b60 / 0x459c00          delta del frame con QueryPerformanceCounter (timeGetTime si no hay),
                             con tope; lo recibe un objeto global (0x65ee60 lo devuelve,
                             0x65fc30 lo actualiza; qué es, sin leer)
while (DAT_006d5ee4 > 0x27)  bucle de "alcanzar": consume pasos de 40 ms
   └─ lógica de la escena según DAT_006d5dc8 (y el hash anti-tamper)
inicio = GetTickCount()                                   0x63852b
dibujo de la escena → glFlush + SwapBuffers
while (GetTickCount() − inicio < 0x28) {}                 espera activa, 0x6385fd..0x638624
DAT_006d5ee4 += GetTickCount() − inicio
```

- **La lógica avanza en pasos de 40 ms (25 por segundo)** y, si el frame tardó, el `while` corre
  varios pasos seguidos para alcanzar al reloj.
- **La espera mide con `GetTickCount`**, que no avanza de a 1 ms sino de a un tick del reloj de
  Windows: 15,625 ms (64 por segundo) en la VM XP y en un Windows con el tick por defecto. Dos ticks
  son 31,25 ms (menos de 40) y tres son 46,875 ms, así que **la espera termina en el tercer tick**:
  el dibujo queda en 46,875 ms por frame, **21,33 FPS** (64 / 3), no en 25.
- La lógica no se atrasa: el acumulador suma lo que midió `GetTickCount` (46 o 47 ms) y consume 40
  por paso, así que corre **7 pasos cada 6 frames** (1, 1, 1, 1, 1, 2) y sigue en 25 por segundo.
  *Inferido* de la aritmética del bucle: la medición ve los frames, no los pasos.
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
- Lo que el swap no puede probar: que la espera sea la de `0x637700` y los 7 pasos cada 6 frames.
  Eso sigue siendo estático e *inferido* (arriba).

### Qué corre en cada paso y qué en cada frame

*Estático*, del decompilado de `0x637700`. **Por paso de 40 ms**, en orden:

```text
hash anti-tamper del contador de pasos DAT_07d3a0f0   (se rearma con tamaños al azar
                                                     cuando el contador % 313 == 57; no se porta)
0x623e70 (40 KB)        solo en la escena 5 y con DAT_05762eb4 == 0; sin leer
lógica de la escena     2 → 0x634c60, 4 → 0x6342c0, 5 → 0x636290 (ahí avanza el cuerpo de
                        cada modelo, ver 06)
0x456b10(0,005) × 5     una lista de objetos con 5 subpasos de 0,005 y un valor al azar
                        acotado entre −0,2 y 1 (DAT_007245f0; ¿viento?, inferido)
0x5547e0, 0x5554b0      dos temporizadores de 301 y 151 pasos (12 y 6 s) que llaman a
                        0x554610 y 0x555190 (mensajes en pantalla, inferido)
IMPR PANT               alterna el pedido de captura (DAT_07d368a8)
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
[06](06-render-escenas-y-ui.md#cuánto-avanza-la-animación). Lo que falta
([08](08-pendientes.md), P-C03): qué hacen `0x623e70` y la lógica de cada escena (movimiento,
cooldowns, cámara) para desacoplar el dibujo sin cambiar el juego.

Cada frame termina con `glFlush` y `wglSwapBuffers` (2.375 de cada uno en la sesión) y empieza con
`glClear` (2.381). **Verificado** en las estadísticas de GLIntercept.

## El WndProc `0x4af4e0`

| Mensaje | Qué hace |
| --- | --- |
| `WM_DESTROY` (0x2) | apagado: bandera `DAT_05763568 = 1`, cierra el socket (`0x4e8380`), libera, `wzAudioDestroy`, cierra GL (`0x4ad180`), `PostQuitMessage(0)` |
| `WM_ACTIVATE` (0x6) | `DAT_05763580` = foco ganado o perdido |
| `WM_PAINT` (0xF) | `BeginPaint`/`EndPaint` vacío: el dibujo no va por `WM_PAINT` |
| `WM_ERASEBKGND` (0x14) | devuelve 1, no borra |
| `WM_SETCURSOR` (0x20) | `ShowCursor(0)`: el cursor lo dibuja el juego |
| `WM_RBUTTON*`, `WM_MBUTTON*`, `WM_MOUSEWHEEL` | cámara y zoom |
| `WM_SYSCOMMAND` (0x112) | bloquea `SC_KEYMENU` y `SC_MONITORPOWER` |
| `WM_TIMER` (0x113) | timer `0x3E8`: keepalive `0x0E` (ver [03](03-red-y-cifrado.md)) y tick |
| `WM_USER` (0x400) | mensajes propios 1, 2 y 0x20 |
| `0x2B11` | señal del anticheat: cierra el cliente (`paso4-killer.md`) |

**El teclado no usa `WM_KEYDOWN`**: se lee con `GetAsyncKeyState` en cada frame. Los eventos de
red llegan como mensajes de ventana (`WSAAsyncSelect`, `FD_READ`/`FD_WRITE`, ver
[03](03-red-y-cifrado.md)). *Estático.*

### Una discrepancia entre documentos

`paso9-ciclo-vida.md` dice "timer `0x3e8` (1 s)". El informe del keepalive
(`mu-client/docs/component-net-envio.md` §9) dice que `0x3E8` es el **id** del timer y que el
periodo instalado es de 20.000 ms, y el cable lo confirma: 156 keepalives `0x0E`, uno cada 20 s
(`analysis/mainexe/evidence/s31-cifrado/README.md`). **El dato bueno es 20 s.** El timer de 1 s es
otro: el del anticheat, id 1001 (`paso4-killer.md` §1).

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| `WinMain` + `WndProc` + `wgl*` | una capa `Platform` sobre una biblioteca portable de ventana e input (decisión D-C3 del roadmap); `main.cpp` solo arma y arranca | CL3 |
| El servidor: el compilado, o el que deja el launcher en el registro (`wzPopLaunchInfo`); la config en el registro; los argumentos no se leen | archivo de config + argumentos; se acepta `connect /u<ip> /p<puerto>`, lo que entiende el parser sin usar del original (Q-C36) y como ya se lo lanza en el laboratorio | CL3 |
| Sin el servidor del launcher, el original vuelve a abrir `Mu.exe` y sale; en el laboratorio, ese salto está anulado (Q-C34) | arranca solo, como el del laboratorio: no hay launcher | — |
| Se abre a sí mismo por el nombre de la línea de comandos, en la carpeta actual, y si no está, sale sin avisar (Q-C35) | no se abre a sí mismo; la carpeta de datos sale de la config | CL3 |
| Lógica en pasos de 40 ms; dibujo esperando 40 ms con `GetTickCount`, que en la práctica da 46,875 ms | la lógica sigue en pasos de 40 ms con el mismo bucle de alcanzar; el dibujo deja de esperar e interpola entre pasos (D-C4 del roadmap); el modo fiel restaura la espera con un reloj de 15,625 ms, para dar los mismos 21,33 FPS | CL3 |
| Teclado por `GetAsyncKeyState` | estado del teclado consultado por frame, como el original, detrás de `Input` | CL3 |
| Red por mensajes de ventana | un socket no bloqueante que el bucle consulta en cada frame; mismo modelo de un solo hilo | CL4 |
| Keepalive por `WM_TIMER` cada 20 s | temporizador del bucle, testeable con un reloj falso | CL4 |
| Fin del auto-update del launcher (`0x4b96b0`: copia `mu._xe`, `mumsg._ll`, `wz_zp._ll` y `message._tf` sobre sus nombres) | no se porta: es del launcher ([05](05-formatos-de-datos.md#el-pipeline-de-las-dlls)) | — |
| Anticheat (hash, timer 1001, `0x2B11`) | **fuera de alcance** | — |

Tests que fija:

- El bucle principal con un reloj falso: exactamente 25 pasos de lógica por segundo simulado, con
  1, 25 o 240 frames dibujados, y el alcanzar ante un frame de 130 ms (3 pasos seguidos) (CL3).
- El modo fiel con un reloj falso de 15,625 ms: 64 frames cada 3 segundos y 7 pasos de lógica cada
  6 frames (CL3).
- El keepalive sale cada 20 s de reloj simulado y no antes (CL4).
- El parser de la línea de comandos acepta `connect /u<ip> /p<puerto>`, y rechaza un puerto que no
  sea un número de 1 a 65535 (CL3).
