# Q-C184 / Q-C192: handoff para el agente con VM

Laboratorio didáctico para contrastar la reimplementación con ASM y ejecución observada.
**Objetivo:** medir qué byte lee el original antes de Item.bmd, sus entradas y los efectos en
stats/FPU. No asumir que el byte vale cero ni modificar el original para obtener un resultado.

Leer AGENTS.md, CODING_RULES y reverse-binary-lab. ASM prevalece sobre pseudocódigo.
Actualizar el checkout del **PR #356** sin descartar trabajo ajeno y registrar su SHA.
[qc184_evidence.py](qc184_evidence.py) se ejecuta desde el repo en el host moderno; usa también
recover_cl9_evidence.py para la política de rutas. Sólo los bundles se copian al guest.

## Alcance y estado real

| Modo | Qué registra | Qué no acredita |
| --- | --- | --- |
| Pasivo, con s31mem | Puntero de tabla, bytes table−16/table−39, prefijo de 64 bytes y huellas | Lectura secuencial, no snapshot atómico |
| bonus | CL después de 40dc88, ítem/valor/caller, prefijo, stats y FPU antes/después de 550d10 | Sumas aisladas de cada ADD, conteo de rand() ni un selector universal |
| wear | Cinco argumentos de 54ee00, valor bruto de 54e680, ST0/FPU, EBX y publicación de word62 | En 550b1e todavía faltan modificadores posteriores; no es el final de 550990 |

Se probaron generador/analizadores con fixtures y se prepararon ambos bundles leyendo el PE como datos.
**Los comandos WinDbg requieren su primera ejecución supervisada en la VM.** No se ejecutó, cargó ni
emuló el original/DLL en este host. CAPTURED es consistencia de captura, no MATCH con MuRet: los reportes
mantienen native_fidelity_verified=false y rng_count_observed=false.

## 1. Identificar la sesión

- Localizar VM, cliente, servidor, PID y herramientas reales; no reutilizar los de otra sesión.
- Cliente **PE32 i386**, 3.058.308 bytes, base cargada **0x00400000**, SHA-256
  `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
  Copiar al host el archivo del proceso del guest; una copia del repo no acredita qué se ejecuta.
- Registrar MuRet/fuentes/Data, OS/CPU, relojes, snapshot nuevo, PID/arranque y versiones/hashes de herramientas.
- Usar cuenta de pruebas y nombres nuevos; no borrar personajes existentes ni sus ítems para preparar el caso.
  Credenciales y evidencia cruda permanecen privadas, fuera de Git.

Se necesita WinDbg clásico **x86 compatible con el guest**; en XP usar el disponible del laboratorio
(por ejemplo Debugging Tools 6.12 x86), sin atribuir a este documento una prueba realizada de esa versión.
Python 3.10+ va en el host; no hace falta Python en XP ni ejecutar MuRet/SDL3 allí.

## 2. Preparar en el host

Ejemplo PowerShell; reemplazar las rutas. Las salidas deben ser nuevas, fuera de Git. La ruta guest
es ASCII, sin espacios, de hasta 160 caracteres.

```powershell
$Repo = 'C:\trabajo\MuRet'
$OriginalDelGuest = 'C:\capturas-privadas\main-del-guest.exe'
$Tool = Join-Path $Repo 'Client\docs\research\scripts\game\capture\qc184_evidence.py'
$Bonus = 'C:\capturas-privadas\qc184-bonus-01'
$Wear = 'C:\capturas-privadas\qc184-wear-01'

py -3 $Tool prepare --main-exe $OriginalDelGuest --output $Bonus --guest-directory 'C:\Capture\qc184-bonus-01' --mode bonus
if ($LASTEXITCODE -ne 0) { throw 'No se preparó bonus; revisar identidad/rutas.' }
py -3 $Tool prepare --main-exe $OriginalDelGuest --output $Wear --guest-directory 'C:\Capture\qc184-wear-01' --mode wear
if ($LASTEXITCODE -ne 0) { throw 'No se preparó wear; revisar identidad/rutas.' }
```

El helper exige tamaño/hash y PE32/ImageBase; extrae huellas desde las secciones del archivo.
No saltea identidad ni sobrescribe carpetas. Copiar cada bundle completo a su **ruta guest exacta**:
start.wds, arm.wds, stop.wds, on-*.wds, passive-spec.txt y manifest.json.
Los .wds son lenguaje de **WinDbg**: no ejecutarlos con CMD/PowerShell ni mediante doble clic.

## 3. Control pasivo antes de depurar

Arrancar el original **normalmente dentro de la VM**, no desde WinDbg. Esperar Data y llegar al
login/lista de servidores antes de autenticarse. Arrancar bajo debugger puede alterar el heap.
Adjuntar tarde y comparar controles no convierte la sesión depurada en una observación pasiva.

Usar s31mem.exe del lab `9f83b4c7e4e40aaa3b3f0876c9ddbf04a3fa163d`, fijado en
[cl9-vm-evidence.json](cl9-vm-evidence.json). Recuperación/compilación y controles XP en
[RUNBOOK-cl9-vm](RUNBOOK-cl9-vm.md#3-preparar-una-sesión-nueva-c32-en-la-vm).
No hace falta recuperar los 59 corpus otra vez si ya se tiene el lector.
Verificar PE32/imports/runtime del lector propio; sólo usa PROCESS_QUERY_INFORMATION | PROCESS_VM_READ.

En CMD del guest, reemplazar PID_VERIFICADO y la ubicación del lector. El directorio de captura no
debe existir antes; s31mem sí sobrescribiría archivos previos:

```text
mkdir C:\Capture\qc184-bonus-01\passive-before
C:\Tools\s31mem.exe PID_VERIFICADO C:\Capture\qc184-bonus-01\passive-spec.txt C:\Capture\qc184-bonus-01\passive-before
```

El spec usa offsets DWORD −16=`0xfffffff0`, −39=`0xffffffd9` y −64=`0xffffffc0`, con suma en 32 bits.
Conservar s31mem.txt y todos los .bin. El analizador comprueba tamaños/direcciones, huellas, puntero
antes/después, páginas legibles y consistencia del prefijo. **Ceros con ilegibles>0 no acreditan un cero.**
Si no se puede obtener el control pasivo, declararlo BLOCKED; la traza instrumentada conserva ese límite.

## 4. Modo bonus: lectura real y stats

1. Abrir un debugger x86 **nuevo** y adjuntarlo tarde al PID anterior; verificar módulo/base.
   Preparar antes de autenticarse evita dejar al GS esperando durante el setup. Registrar cualquier
   desconexión inducida por pausas y repetir con precondiciones claras.
2. `bl` debe estar vacío. Se necesitan cuatro recursos hardware; IDs 0–3 libres no bastan si hay otros ba.
   No sustituirlos por breakpoints software ni alterar datos, resultados o protecciones.
3. En la consola **WinDbg**, ejecutar:

   ```text
   $$><C:\Capture\qc184-bonus-01\start.wds
   ```

   Verificar que abrió un log PID/fecha, las huellas PROFILE_OK y **cuatro puntos habilitados e1** en
   0040dc8c, 00550d10, 005513a3 y 005513b1. PROFILE_OK no certifica instalación. Ante error, conservar
   el log y detener el caso; no continuar sólo porque el script imprimió texto.
4. Aún detenido, tomar otro control pasivo en una carpeta nueva `passive-attached`. Comparar con el
   previo del mismo PID/arranque; registrar cambios sin atribuirlos automáticamente al debugger.
5. Después de verificar los cuatro puntos:

   ```text
   $$><C:\Capture\qc184-bonus-01\arm.wds
   ```

   Debe imprimir ARMED_OK y continuar con g. Los handlers usan gc; no probarlos mediante p/t.
6. Reproducir caballero con arma ordinaria/durabilidad → volver a selección → crear mago en otro
   lugar → entrar. Registrar acciones y resultado visual. Repetir con personaje existente y piezas
   cuyo bono tenga valor 1, 2 y 3 por mecánicas reales del laboratorio. Si faltan piezas, declarar
   BLOCKED_INVENTORY; no editar RAM para fabricar el caso.
7. Con el resultado estable, interrumpir el debugger y ejecutar:

   ```text
   $$><C:\Capture\qc184-bonus-01\stop.wds
   ```

   Comprueba propiedad, elimina sólo sus IDs, marca COMPLETE, cierra log y usa .detach. Confirmar
   cliente activo y sin debugger/suspensión pendiente. No usar bc * ni q sobre el proceso. Si
   STOP_BLOCKED, inspeccionar bl y resolver sólo los puntos propios, documentando el estado final.
   Copiar/verificar evidencia antes de restaurar el snapshot.

En 40dc8c, CL ya contiene el byte; EAX es `0xffffffeb` para IDffff. `[ebp+8]` es el ítem,
`[ebp+c]` apunta al valor y `[ebp+4]` es el retorno: 40e038 corresponde a copias y 550d72 a WORD propios.
BEGIN/END emparejan ESP/hilo/registro/caller; no se capturan nombres, chat ni clave de la cuenta.

**Excepción nativa:** si se detiene en 40dc88 u otra lectura, guardar `.exr -1`, `r`, `rF`, `kb`,
VAs y regiones legibles. Registrar SOURCE_ACCESS_FAULT y selector no observado. No ocultar/continuar
la excepción para producir un PASS; resolver el detach según el debugger y registrar su estado.
INCOMPLETE no descarta esa evidencia de fallo nativo: requiere dictamen manual.

## 5. Modo wear: denominador y FPU

Usar otra sesión/directorio con arranque normal, controles pasivos y debugger nuevo. Repetir los pasos
con `qc184-wear-01`. Puntos: 550adb, 550ae0, 550b00 y 550b1e.
Debe haber **durabilidad residual en lugares 1..7**: mano izquierda, cuerpo o alas. Un arma sólo en
el lugar 0 no alcanza. Equipar realmente escudo/armadura, volver a selección y entrar con otro personaje.
Se exigen BEFORE→FACTOR→REDUCED→SUM del mismo hilo/registro/frame/ranura.

Se capturan cinco DWORD de `[esp]` a `[esp+10]` y `poi(ebp-c)` completo. 54ee00 ajusta la base con
nivel/excellent/ancient: **base=0 no implica denominador=0**. Las alas pasan dos ceros al final.
El denominador del reporte es una predicción ASM; el ST0 observado permanece en el bloque FPU crudo.
Comparar CW/SW y ST0 entre BEFORE/FACTOR y la reducción posterior. EBX puede conservar bits altos;
su DWORD y sus 16 bits bajos se conservan por separado. En 550b1e word62 es todavía parcial.

SW igual antes/después no demuestra ausencia de excepción si el bit ya estaba encendido. No limpiar
CW/SW ni inferir rand() por el tiempo. Esta instrumentación tiene overhead y no mide FPS nativo.

## 6. Analizar y devolver

Copiar logs/directorios al host fuera de Git, sin sobrescribir sesiones. Ejemplo PowerShell:

```powershell
py -3 $Tool analyze-passive --bundle $Bonus --input 'C:\capturas-privadas\raw-bonus-01\passive-before' --output 'C:\capturas-privadas\passive-before.json'
py -3 $Tool analyze-debug --bundle $Bonus --input 'C:\capturas-privadas\raw-bonus-01\native_PID_FECHA.log' --output 'C:\capturas-privadas\bonus-report.json'
py -3 $Tool analyze-debug --bundle $Wear --input 'C:\capturas-privadas\raw-wear-01\native_PID_FECHA.log' --output 'C:\capturas-privadas\wear-report.json'
```

Reemplazar el nombre por el producido por logopen /t y repetir análisis para passive-attached.
Salidas: 0 CAPTURED, 3 INCOMPLETE, 1 error de archivo/perfil. No sobrescribe reportes.
CAPTURED valida consistencia, **no certifica equivalencia con MuRet**. Revisar errors/warnings y log crudo;
el parser no certifica todas las variantes/localizaciones de WinDbg ni todos los errores de instalación.

Devolver al PR/handoff:

- SHAs/fechas de MuRet, original, Data y herramientas; VM/snapshot, OS/CPU, PID/arranque y base.
- Manifest, comandos, hashes/tamaños, acciones y estado final de debugger/VM.
- Tabla por caso: caller/ranura, ID/durabilidad/flags/valor, selector CL y byte de memoria;
  controles antes/después de adjuntar, stats y CW/SW/ST0. Preservar bits altos de EBX.
- Dictamen **MATCH / DIFFERENCE / SOURCE_ACCESS_FAULT / BLOCKED / UNVERIFIED**, separando observación,
  predicción ASM y comparación MuRet. Repetir en varios procesos; no generalizar un byte del heap.
- Para un fixture privado, ligar los bytes a sesión/hash/precondiciones. No relajar guardas, tolerancias
  o expectativas para forzar verde. P-C38(a) requiere una conclusión acotada, no el primer byte observado.

Raw logs, prefijos y volcados permanecen privados. Compartir herramientas, hashes y conclusiones.
Tests: `python -m unittest discover -s Client/docs/research/scripts/game/capture -p 'test_*evidence.py' -v`.
Documentación consultada mediante Context7:
[ba](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/ba--break-on-access-),
[registros/rF](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/r--registers-),
[pseudo-registros](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/pseudo-register-syntax),
[gc](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/gc--go-from-conditional-breakpoint-),
[logopen](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/-logopen--open-log-file-).
