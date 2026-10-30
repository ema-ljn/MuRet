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

### El ritmo: 25 por segundo

El render loop tiene su propio reloj (`analysis/mainexe/functions/paso6-ui-map.md` §4):

```text
0x459b60 / 0x459c00          tiempo (timeGetTime)
while (DAT_006d5ee4 > 0x27)  bucle de "alcanzar": consume pasos de 40 ms
   └─ lógica de la escena según DAT_006d5dc8 (y el hash anti-tamper)
dibujo de la escena → glFlush + SwapBuffers
espera activa hasta 0x28 ms   → DAT_006d5ee4 += lo transcurrido
```

- **La lógica avanza en pasos de 40 ms (25 por segundo)** y, si el frame tardó, el `while` corre
  varios pasos seguidos para alcanzar al reloj.
- **El dibujo también queda en 25 FPS**: la espera activa no deja empezar el frame siguiente
  antes de 40 ms. Por eso el cliente "se siente" clavado: no es VSync, es esta espera.
- Es el mismo valor que conserva MuMain para el cliente 5.2
  ([ADR 0001 de MuMain](https://github.com/sven-n/MuMain/blob/main/docs/adr/0001-preserve-25-fps-reference-timing.md)).

*Estático*: leído en el render loop; el orquestador `0x63945d` (7 KB) sigue sin decompilar en
detalle y no hay una medición en la VM. Qué partes de la lógica dependen de ese paso
(animaciones, movimiento, cooldowns) es lo que falta para desacoplar el dibujo sin cambiar el
juego ([08](08-pendientes.md), P-C03).

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
| Lógica en pasos de 40 ms y dibujo esperando a 40 ms | la lógica sigue en pasos de 40 ms con el mismo bucle de alcanzar; el dibujo deja de esperar e interpola entre pasos (D-C4 del roadmap); un modo fiel restaura la espera | CL3 |
| Teclado por `GetAsyncKeyState` | estado del teclado consultado por frame, como el original, detrás de `Input` | CL3 |
| Red por mensajes de ventana | un socket no bloqueante que el bucle consulta en cada frame; mismo modelo de un solo hilo | CL4 |
| Keepalive por `WM_TIMER` cada 20 s | temporizador del bucle, testeable con un reloj falso | CL4 |
| Anticheat (hash, timer 1001, `0x2B11`) | **fuera de alcance** | — |

Tests que fija:

- El bucle principal con un reloj falso: exactamente 25 pasos de lógica por segundo simulado, con
  1, 25 o 240 frames dibujados, y el alcanzar ante un frame de 130 ms (3 pasos seguidos) (CL3).
- El keepalive sale cada 20 s de reloj simulado y no antes (CL4).
- El parser de la línea de comandos acepta `connect /u<ip> /p<puerto>` (CL3).
