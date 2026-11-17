# Interacciones Linux y VMXP para pruebas funcionales

Auxiliares propios recuperados de la validación Q-C184/Q-C192 del 2026-11-16. Permiten repetir
acciones por la UI y conservar evidencia privada. El servidor sigue en otro equipo; estos
programas no lo administran. No constituyen todavía un runner de pruebas ni verifican por sí
solos que una acción haya producido el resultado esperado.

| Archivo | Uso observado | Límite |
| --- | --- | --- |
| `linux_x11_ui.py` | Comandos de ventana, clics, teclas, nombre ASCII y capturas usados con MuRet en Linux | Requiere SDL X11, xdotool y ffmpeg; el adaptador Python reúne los comandos de la sesión |
| `xp-input.c` | `SendInput`, foco, geometría y liberación del botón en XP SP3 x86 | La clase de ventana debe ser `MU`; coordenadas relativas al área cliente |
| `vmio.py` | Copiar archivos, ejecutar auxiliares y recuperar sus salidas mediante VMware Tools | Requiere el wrapper externo `vmguest` con keyring; rutas configuradas por entorno |
| `xp-debug-control.c` | Metadatos de módulos y conexión al CDB ya abierto mediante DbgEng | Auxiliar del runbook de captura, separado de la entrada de teclado/mouse |
| `provenance.json` | Hashes de los sources originales y ejecutables usados en XP | Los sources C se conservaron con cambios de formato únicamente; no incluye binarios |

Los auxiliares C originales se ejecutaron en XP; estos sources se recompilaron en Linux. Los adaptadores
Python se extrajeron de las llamadas usadas en la sesión y eliminan sus rutas privadas. Revalidar
la integración al cambiar host, ventana, resolución, compilador o motor de debugger. Los archivos
`.exe`, SDK/DLL, logs, volcados, capturas y configuración local permanecen fuera de Git.

## Linux: dirigir eventos a la ventana de MuRet

Arrancar el binario propio con `SDL_VIDEODRIVER=x11` y su configuración privada habitual. El login
lo hace la persona. Identificar el PID y la ventana actuales, sin reutilizar los de otra ejecución:

```bash
pgrep -a -x Main
xdotool search --onlyvisible --pid PID_ACTUAL --name '^MuRet$'
python3 linux_x11_ui.py --window ID_ACTUAL state
python3 linux_x11_ui.py --window ID_ACTUAL capture /capturas-privadas/antes.png
```

Sustituir los marcadores por valores observados. El script exige título `MuRet`, comprueba que el
clic esté dentro del cliente y no sobrescribe capturas. Las coordenadas vienen de una captura
reciente de esa misma ventana, en píxeles de cliente, no del escritorio completo.

```bash
python3 linux_x11_ui.py --window ID_ACTUAL click X_OBSERVADA Y_OBSERVADA
python3 linux_x11_ui.py --window ID_ACTUAL key Return
python3 linux_x11_ui.py --window ID_ACTUAL capture /capturas-privadas/despues.png
```

Un clic mantiene el botón 180 ms y lo libera en `finally`; una tecla hace lo mismo. `focus` cambia
el foco de teclado X11 y `click` mueve el puntero visible. Avisar antes de tomar foco. En esta
sesión Wayland/Xwayland, `windowactivate` y los clics XTest genéricos movían un puntero sin llegar
al cursor de SDL. Funcionaron los eventos dirigidos con `--window`; ButtonPress transmite también
la posición. Para el nombre ASCII de prueba funcionaron `windowfocus` y después `type`:

```bash
python3 linux_x11_ui.py --window ID_ACTUAL focus
python3 linux_x11_ui.py --window ID_ACTUAL type NombreDePrueba
```

`type` acepta sólo nombres ASCII alfanuméricos, con el campo de creación previamente observado.
No usarlo para credenciales. Esto no prueba IME ni otras codificaciones. Si SDL no recibe la
acción, revisar foco/estado antes de repetir; `state` sólo informa geometría. El código 0 del
comando acredita entrega al mecanismo X11, no elección de personaje ni entrada al mapa.

## XP: compilar los sources propios

Desde esta carpeta, con MinGW-w64 i686 y un directorio de salida privado existente:

```bash
i686-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -D__USE_MINGW_ANSI_STDIO=0 \
  -static -mwindows -Wl,--major-subsystem-version,5,--minor-subsystem-version,1 \
  xp-input.c -o "$MURET_LAB_OUTPUT/xp-input.exe" -luser32
i686-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -D__USE_MINGW_ANSI_STDIO=0 \
  -static -mwindows -Wl,--major-subsystem-version,5,--minor-subsystem-version,1 \
  xp-debug-control.c -o "$MURET_LAB_OUTPUT/xp-debug-control.exe" -ldbgeng -lole32 -luuid
```

Recompilados con GCC 16-win32 de la imagen local del laboratorio, sin red. Inspeccionar los PE32
e imports antes de copiarlos a XP; fijar macros/subsistema no garantiza compatibilidad de runtime.
`xp-debug-control` requiere el DbgEng x86 compatible con XP, como el 6.12 del runbook; no versionar
esas DLL. Nunca ejecutar el cliente original ni sus DLL en Linux para probar estos auxiliares.

Los binarios recompilados se copiaron y arrancaron en XP mediante este `vmio.py`: `xp-input state`
devolvió `MU_window_missing` (el juego estaba cerrado) y `xp-debug-control meta 0 -` terminó con
`EXIT=0`, sin adjuntar debugger. Esto comprueba carga/runtime del rebuild, no repite los clics de
la sesión original. En Linux se verificaron `state`, `key Escape` y `capture` del adaptador.

Invocaciones en el guest; cada salida debe tener un nombre nuevo:

```text
xp-input.exe state C:\Capture\sesion\state-01.txt
xp-input.exe focus C:\Capture\sesion\focus-01.txt
xp-input.exe click X_CLIENTE Y_CLIENTE C:\Capture\sesion\click-01.txt
xp-input.exe hold X_CLIENTE Y_CLIENTE 3000 C:\Capture\sesion\hold-01.txt
xp-input.exe key 1b 100 C:\Capture\sesion\escape-01.txt
xp-input.exe up C:\Capture\sesion\release-01.txt
```

`key` recibe VK hexadecimal y duración decimal. Conserva `MapVirtualKeyA` al generar el scan code:
sin él no funcionó la entrada del nombre en el original. `click` espera 250 ms después de mover
el cursor para que el picking atienda el punto (Q-C165), mantiene 100 ms y suelta. Ambos exigen
que `MU` esté al frente. `up` sólo libera el botón izquierdo global: es recuperación explícita.
`hold X Y MILISEGUNDOS SALIDA` hace el mismo clic con duración explícita de hasta 5.000 ms;
suelta al terminar y permite capturar la animación durante la pulsación mediante MKS. Es una
extensión de la auditoría H3 del 2026-11-16, posterior a los hashes históricos de `provenance.json`.
No abre el proceso del juego ni lee/escribe su memoria. Revisar `action_ok`, `foreground_is_game`,
`left_down` y `end`; después comprobar imagen/estado. `action_ok=1` tampoco acredita el efecto.

Se conserva el modo histórico `text NOMBRE_ASCII X_PANTALLA,Y_PANTALLA SALIDA`: busca un único
Edit visible, sin `ES_PASSWORD`, en ese punto y usa `SetWindowTextA`. No encontró el campo del
cliente en esta sesión (`matched_visible_name_edits=0`); para las pruebas usar `key`. A diferencia
del controlador DbgEng, `xp-input` abre la salida con `w`: puede sobrescribirla. El supervisor debe
proporcionar siempre una ruta nueva. No pasar credenciales ni códigos personales por argumentos.

## Linux → XP mediante VMware

Requiere Workstation, VMware Tools activo y `vmguest` instalado en el host. Este último es el
wrapper local del laboratorio que obtiene la contraseña del keyring; no se incluye su clave ni
se reemplaza por `vmrun -gu/-gp`. Configurar fuera del repo:

```bash
export MURET_VM_VMX='/ruta/absoluta/VMXP.vmx'
export VMWARE_GUEST_USER='usuario-del-guest'
export MURET_VM_OUTPUT_DIR='/capturas-privadas/sesion-nueva'
export MURET_VM_GUEST_DIR='C:\Capture\sesion-nueva'
export MURET_VM_CONTROL_EXE='C:\Capture\tools\xp-debug-control.exe'
```

Crear los directorios por el procedimiento habitual y comprobar Tools con
`vmrun -T ws checkToolsState "$MURET_VM_VMX"`. Operaciones secuenciales; el VMX va inmediatamente
después del nombre de operación. Ejemplo Python desde esta carpeta, con el binario ya desplegado:

```python
import vmio

output = vmio.guest_path("state-01.txt")
vmio.guest("runProgramInGuest", vmio.guest_path("xp-input.exe"), "state", output)
local = vmio.pull(output, "state-01.txt")
print(local.read_text())
```

`push(Path(...), destino)` copia al guest; `pull(origen, nombre)` rechaza una salida local existente.
`vbs(nombre, texto)` conserva los pequeños scripts de coordinación ASCII/CRLF con `wscript //B`,
sin mostrar consola; ejecutar sólo el texto revisado y autorizado. No transporta contraseñas.

Para una captura visual, la interfaz usada fue `vmcli "$MURET_VM_VMX" MKS captureScreenshot
/capturas-privadas/estado.png`, sin acceder a memoria. No usar estos helpers para cambiar snapshots,
matar procesos o alterar protecciones. Iniciar sesión sigue siendo tarea de la persona.

## Control del debugger, separado de la UI

`xp-debug-control.exe MODO PIPE ARCHIVO_COMANDOS SALIDA` usa exactamente cuatro argumentos.
`meta PID - SALIDA` enumera `main.exe`/`FoxAnti.dll` con Toolhelp32 y registra base/ruta públicas.
`status PIPE - SALIDA` consulta GetExecutionStatus y devuelve 0 sólo con NO_DEBUGGEE.
`exec` envía el archivo de comandos al CDB ya abierto; `interrupt` pide interrupción, verifica BREAK
y luego ejecuta ese archivo, si no es `-`. `vmio.control(...)` envuelve esas operaciones.

La conexión local de XP que funcionó fue `npipe:server=.,pipe=PIPE`; `localhost` falló. La salida
existente se rechaza. El controlador no selecciona un proceso, adjunta ni instala puntos por sí
solo; los comandos sí pueden afectar la ejecución. Usarlo únicamente con los bundles revisados
del [runbook](../RUNBOOK-qc184-vm.md), conservando el procedimiento de retirada/detach. No mezclar
pausas de debugger con mediciones de tiempos de una prueba funcional.

## Reutilizarlo como prueba funcional

Separar preparación, acción y aserción: login privado y estado inicial comprobados; una acción;
captura/log nuevo que demuestre el resultado. Guardar identidad de fuentes/binario/Data, resolución,
VM/arranque y precondiciones. Los nombres de personajes y cualquier borrado se deciden explícitamente:
los scripts no borran personajes automáticamente ni incorporan credenciales o secuencias de login.

Las repeticiones observadas y sus límites están en [Q-C184, §8](../RUNBOOK-qc184-vm.md#8-repetición-en-muret-linux-2026-11-16).
Se pueden automatizar después sus aserciones y transiciones; hoy son herramientas de interacción
supervisada, no una suite desatendida ni una prueba de equivalencia numérica con el original.

## Hover y captura de cierre en Linux

`move` envía un `MotionNotify` lógico a MuRet mediante Xlib. En esta sesión, `xdotool mousemove`
cambiaba el puntero X11 sin actualizar siempre el cursor de SDL; el evento explícito permitió
actualizar el cursor del juego sin un clic que iniciara una caminata. La ABI procede de `XMotionEvent`
en Xlib.h; usa la biblioteca de sistema X11, no una biblioteca del cliente original.

```bash
python3 linux_x11_ui.py --window ID_ACTUAL move X_CLIENTE Y_CLIENTE
python3 linux_x11_ui.py --window ID_ACTUAL capture /capturas-privadas/hover.png
python3 linux_x11_ui.py --window ID_ACTUAL click X_CLIENTE Y_CLIENTE
```

Confirmar el elegido en un dibujo anterior al DOWN y dejar más de 500 ms entre clics ordinarios.
El picking del juego corre después del movimiento: un primer clic sobre una posición recién enviada
puede usar la selección anterior. Sostener un píxel fijo tampoco sigue al mob cuando se mueve la
cámara. Los dobles clics SDL no equivalen a dos DOWN ordinarios. Este auxiliar no acredita un D7;
conservar un caso sin ataque confirmado como no verificado.

Para distinguir una salida normal de una señal externa puede arrancarse el binario **propio** en
un servicio temporal del usuario, con strace limitado a señales. Las variables son rutas privadas;
no habilitar una unidad permanente ni usar este procedimiento sobre el original.

```bash
systemd-run --user --unit=UNIDAD_NUEVA --working-directory="$MURET_LAB_OUTPUT" \
  --setenv=DISPLAY --setenv=XAUTHORITY --setenv=SDL_VIDEODRIVER=x11 \
  --property="StandardOutput=append:$MURET_LAB_OUTPUT/cliente.log" \
  --property=StandardError=inherit \
  strace -ff -e trace=none -e signal=all -o "$MURET_LAB_OUTPUT/signals" \
  "$MURET_BIN" --config "$MURET_PRIVATE_CONFIG"
systemctl --user show UNIDAD_NUEVA -p ActiveState -p ExecMainStatus
```

Esto abre la ventana de MuRet y registra señales, sin registrar lecturas de teclado ni bytes de red;
strace puede alterar el rendimiento, por lo que no sirve para medir tiempos fieles. El login sigue
siendo manual. Al terminar, salir normalmente del juego y retirar la unidad temporal con
`systemctl --user stop UNIDAD_NUEVA`. En esta auditoría dos arranques anteriores salieron con código0
sin error de escena; no se atribuye su causa sin una captura de la señal o solicitud de cierre.
