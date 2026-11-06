# Observar el cliente original en Windows XP

Herramientas del laboratorio didáctico para contrastar la reimplementación con el cliente vivo.
Build c32bbd5f del [entorno](../../README.md#el-entorno), SHA-256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.

| Herramienta | Qué obtiene | Límite |
| --- | --- | --- |
| `passive_camera.cpp` | Una lectura de cámara/estado y posición del héroe, en bits | Secuencial; no prueba una transición |
| `passive_cl7.cpp` | 2.250 lecturas con agenda de 40 ms, unos 90 s: cámara, héroe, camino, paredes y entidades | No sincroniza pasos ni frames; 25 Hz es el objetivo del lector |
| `compare_lorencia.cpp` | Nueve salidas de cámara contra `StepGameCamera` enlazada desde MuRet | Usa el estado observado como entrada estacionaria; siete checks de ese estado no son predicciones independientes |
| `fsin_probe.c` | Secuencia x87 aislada del balanceo Y en tres relojes largos, PC53 y PC64 | Programa propio: no lee la CW ni ejecuta la función del proceso original |
| `read_fpu.cpp` | CW y EIP del hilo que posee la ventana `MU` | Pausa/reanuda un incremento de suspensión; requiere supervisión |

`passive_camera` y `passive_cl7` buscan la ventana de clase `MU` o reciben un PID decimal. Usan
`PROCESS_QUERY_INFORMATION | PROCESS_VM_READ` y comprueban dos firmas de 16 bytes en
`0x635ae0` y `0x5fcc30` antes de leer una lista cerrada de campos. Son firmas de compatibilidad,
no el hash completo del proceso. Verificar aparte el SHA del archivo. No escriben en el proceso
ni lo suspenden, depuran o inyectan código.

## Compilar y ejecutar

Los tres lectores usan C++20 y dueños RAII para handles y salida. Los cierres normales se
comprueban explícitamente para informar fallos. Con MinGW-w64 de 32 bits, desde esta carpeta:

```bash
for tool in passive_camera passive_cl7 read_fpu; do
  i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -Wextra -Wconversion -Wshadow \
    -Wold-style-cast -Werror -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 \
    -D__USE_MINGW_ANSI_STDIO=0 -fno-exceptions -fno-rtti -static -mwindows \
    "$tool.cpp" -o "$tool.exe" -luser32 || exit 1
done
```

Conservar los headers `CaptureFields.h`, `CaptureHandle.h`, `CaptureOutput.h` y
`ThreadSuspension.h` junto a las fuentes. Las macros seleccionan APIs de XP en el SDK;
no validan por sí solas la compatibilidad del ejecutable. Antes de usar esta migración en XP,
verificar imports, dependencias del runtime y ejecución en el guest.

La compilación de esta auditoría se verificó como PE32 i386, subsistema 4.0, con imports de
KERNEL32, MSVCRT y USER32. Los tres lectores corrieron en XP y terminaron con código 0: la cámara
coincidió en sus nueve salidas, la serie validó 2.250 muestras y FPU retiró su único incremento
de suspensión. Revalidar al cambiar el compilador o runtime; esto no amplía la cobertura de
ramas del cliente original ni cierra la comparación de FSIN.

`fsin_probe.c` conserva C11: se compila con `i686-w64-mingw32-gcc -std=c11`, requiere assembly
x87 y no usa `user32`. Su resultado en un host x86 no acredita la CPU ni la CW del cliente.
Copiar el ejecutable propio al guest y correrlo en una sesión interactiva:

```text
passive_cl7.exe --mu-window --output serie.txt
passive_camera.exe --mu-window --output camara.txt
fsin_probe.exe --output fsin.txt
read_fpu.exe --output hilo.txt
```

Guardar resultados fuera de Git. El lector continuo vacía encabezado y muestras: antes de pedir
una acción, comprobar que ya existe un `end_sample`. No recupera movimientos anteriores.

El comparador usa C++20, `Client/src`, los headers glm de vcpkg y `libClientLib.a` del build.
Las rutas se toman del CMakeCache. Recibe `compare_lorencia camara.txt comparacion.json`.
Acepta una captura individual, no la serie. Con `visualCellAttribute` o `cellAttribute` usa ese
valor; sin él declara la condición supuesta de `0x40/0x80` apagados. Sólo mapa 0/escena 5.
La posición debe estar quieta: el comparador aislado no puede demostrarlo.

`read_fpu` verifica la firma y obtiene el hilo de `MU`;
usa `SuspendThread`, `GetThreadContext` y `ResumeThread`, sin modificar código ni contexto.
Conserva y retira sólo su incremento del contador. Si aparece `recovery_pending`, el supervisor
mantiene vivo el controlador hasta que reanude o confirme que el hilo terminó; no debe matarlo.
El resultado acredita la CW en el EIP y momento observados. Si está fuera de cámara/SendMove,
no acredita la precisión dentro de ellas ni todos los frames. APIs y derechos de XP:
[GetThreadContext](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadcontext),
[ResumeThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-resumethread).

## Qué significa la serie

`camera_series 1` lleva VAs, tamaños, PID y cantidad prevista. Cada bloque `sample N` acaba
en `end_sample`. DWORD/WORD/floats son bits hexadecimales; el camino va byte por byte: actual,
subpunto, cantidad, 15 columnas y 15 filas. Exigir `samplesCompleted=000008ca`, `end`, ningún
`error` y código de salida 0. Una muestra parcial o un error después de `end` invalida la captura.

`ticksBefore/After` acotan el núcleo; `ticksEntitiesAfter`, las entidades. `boundaryStable`
compara mapa, escena, puntero y posición antes y después: no hace atómico el bloque.
`GetTickCount` tiene resolución gruesa: ticks iguales no prueban lectura instantánea. Un retraso
recupera la agenda sin dormir; no garantiza intervalos exactos.

La celda obtenida de xyz es **visual**, diferente de la lógica cifrada adelantada al caminar.
Sólo se leen su WORD de pared y BYTE de capa 1. Del héroe: xyz, ángulos, acción, zona segura,
movimiento/tipo y 33 bytes del camino; nunca nombre ni celdas cifradas. Cada 25 muestras se
leen cinco slots de aves y tres de peces: BYTE vivo y WORD tipo, stride `0x1d4`. El ASM fija
los límites en `0x44642a/430` y `0x447061/064`; el conteo sólo dice cuáles vivían entonces.

`renderSize` lee la resolución interna. Tamaño Win32 y área visible pueden diferir al
redimensionar; estas lecturas no consultan `GL_VIEWPORT`.

`analyze_cl7_series.py SERIE --metadata TIEMPOS --json RESULTADO --report INFORME` valida el
formato completo, cantidades, footer y salida del programa, resume las observaciones y ejecuta
cinco casos negativos de integridad. Atribuye movimiento sólo entre muestras consecutivas con
héroe presente, puntero no nulo e idénticos puntero, mapa y escena. Posiciones distintas tras
una ausencia o cambio de identidad no acreditan continuidad; un héroe ausente no acredita reposo.
La serie, la metadata, el JSON y el informe se leen o escriben explícitamente como UTF-8.

## Probar las herramientas

Desde esta carpeta, con el comparador compilado:

```bash
MURET_COMPARE_LORENCIA=/ruta/al/compare_lorencia \
  python3 -m unittest discover -s . -p 'test_*.py'
```

Incluye 10 tests del analizador y 13 del comparador. Sin `MURET_COMPARE_LORENCIA`, los del
comparador se omiten explícitamente. Los datos sintéticos caracterizan las herramientas;
no son evidencia del cliente original.

`test_capture_readers.py` agrega 13 tests con sustitutos controlados de APIs Win32, reloj y
salida. Requiere Linux con g++/GNU ld y se omite en hosts incompatibles. Comprueba lecturas,
cierres y recuperación de suspensión; no ejecuta ni emula el cliente ni valida el ABI Win32
o la compatibilidad de XP.

## Privacidad y autoridad

Resultados, imágenes y video quedan fuera de Git; las imágenes pueden contener nombres.
Versionar sólo herramientas y resúmenes numéricos del research. Conservar hashes del cliente,
lector y evidencia, código de salida y tiempos. Capturas por segundo no son FPS del juego.

Por instrucción de Emanuel, cambios nuevos se contrastan con ASM y evidencia del original;
sin nuevas emulaciones ni convertir inferencias antiguas en hechos. Los fixtures emulados
anteriores conservan su procedencia como regresiones.
