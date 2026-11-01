# 02. Arranque y ciclo de vida

Fuentes: `analysis/mainexe/functions/paso9-ciclo-vida.md`, `paso7-escenas.md`,
`mainexe-vmxp-c32bbd5f/reports/entry-006a2444.md` y `winmain-004b9b80.md`.

## Línea de comandos y configuración

- El laboratorio lanza el cliente como `main.exe connect /u172.16.198.1 /p44405`: IP y puerto del
  ConnectServer (`mu-client/CHECKPOINT.md`, sección VMXP).
- La configuración vive en el registro, en `SOFTWARE\Webzen\Mu\Config` (por ejemplo
  `VolumeLevel`). `FoxAnti.Init` lee además `ConnectIp` de **HKCU** y pisa la IP del juego en
  `0x6C76E0` (`mu-client/docs/component-packet.md`). *Estático.*

## De `entry` al bucle de mensajes

```text
entry 0x6a2444 (arranque del CRT)
  └─ WinMain 0x4b9b80 (RET 0x10: stdcall de cuatro argumentos)
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

El render loop `0x637700` hace, en orden: cámara, color de cielo por mapa, la escena que toque
según el global `DAT_006d5dc8` (2 login, 4 selección, 5 en el juego; ver
[06](06-render-escenas-y-ui.md)), objetos, HUD, `SwapBuffers` y música. *Estático.*

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
(`graphics-01/`, commits `16dc0221`, `ddcdd64` y `61012a80`). Un `opengl32.dll` proxy que solo toma
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
- **Un swap tarde no arrastra al siguiente.** La mayoría de los intervalos fuera de los 3 ticks
  vienen de a pares: uno largo (54 a 61 ms) y enseguida uno corto (34 a 39 ms), que suman entre 91
  y 96 ms, dos frames de 46,875 (7 pares en la selección, 2 en Lorencia). El frame que viene
  después vuelve a la grilla: el plazo lo pone el reloj, no el swap anterior, como en la espera
  con `GetTickCount`.
- **El ritmo no cambia con la escena ni con el tiempo.** El CSV de Lorencia tiene la sesión
  entera, 31.331 swaps en 24,5 minutos: cada minuto da 21,2 a 21,3 FPS (el primero 20,8, por el
  arranque), con solo 6 frames de más de 200 ms (cuatro al arrancar y dos en el medio).
- Las cifras de los JSON del laboratorio se reprodujeron todas desde los CSV, con dos matices. Los
  JSON dan 21,34 FPS porque dividen las filas por la duración; con los intervalos son 21,33, lo
  mismo que da su intervalo medio. Y el 95,87 % a ±2 ms de la selección (3.365) cuenta la distancia
  a cualquier múltiplo de la grilla: suma un intervalo de 61,27 ms, a 1,23 ms de los 4 ticks. A
  ±2 ms de 46,875 son 3.364 (95,84 %). En Lorencia las dos cuentas dan 3.514.
- La medición anterior (20,87 FPS con GLIntercept, `FPS-MEASURE-RESULT.json`) incluye la carga y
  el frenado de GLIntercept; `FPS-SELECTION-RESULT.json` quedó inválida por un cartel de
  desconexión.
- Lo que el swap no puede probar: que la espera sea la de `0x637700` y los 7 pasos cada 6 frames.
  Eso sigue siendo estático e *inferido* (arriba).

La animación de los modelos avanza por paso, no por frame: en las trazas de GL el frame de la
animación salta de a 0,263 keys por paso de 40 ms (*inferido* con tres modelos, ver
[06](06-render-escenas-y-ui.md#cuánto-avanza-la-animación)). Lo que falta
([08](08-pendientes.md), P-C03): qué más depende del paso (movimiento, cooldowns) para desacoplar
el dibujo sin cambiar el juego. El orquestador `0x63945d` (7 KB) sigue sin decompilar en detalle.

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
| Registro de Windows para la config | archivo de config + argumentos (`connect /u<ip> /p<puerto>` se sigue aceptando) | CL3 |
| Lógica en pasos de 40 ms; dibujo esperando 40 ms con `GetTickCount`, que en la práctica da 46,875 ms | la lógica sigue en pasos de 40 ms con el mismo bucle de alcanzar; el dibujo deja de esperar e interpola entre pasos (D-C4 del roadmap); el modo fiel restaura la espera con un reloj de 15,625 ms, para dar los mismos 21,33 FPS | CL3 |
| Teclado por `GetAsyncKeyState` | estado del teclado consultado por frame, como el original, detrás de `Input` | CL3 |
| Red por mensajes de ventana | un socket no bloqueante que el bucle consulta en cada frame; mismo modelo de un solo hilo | CL4 |
| Keepalive por `WM_TIMER` cada 20 s | temporizador del bucle, testeable con un reloj falso | CL4 |
| Anticheat (hash, timer 1001, `0x2B11`) | **fuera de alcance** | — |

Tests que fija:

- El bucle principal con un reloj falso: exactamente 25 pasos de lógica por segundo simulado, con
  1, 25 o 240 frames dibujados, y el alcanzar ante un frame de 130 ms (3 pasos seguidos) (CL3).
- El modo fiel con un reloj falso de 15,625 ms: 64 frames cada 3 segundos y 7 pasos de lógica cada
  6 frames (CL3).
- El keepalive sale cada 20 s de reloj simulado y no antes (CL4).
- El parser de la línea de comandos acepta `connect /u<ip> /p<puerto>` (CL3).
