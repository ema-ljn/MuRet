# CL9 H1/H9: entrega para el agente con acceso a la VM

**Pedido de Emanuel:** fidelidad al ASM y al pseudocódigo; si discrepan, prevalece el ASM.
Primero se contrasta el código, después se observa el original en la VM. Esta entrega permite recuperar
las referencias que faltaban y preparar esa observación. El helper no ejecuta el cliente ni controla la VM.

Leer también el [resultado de H1/H9](../../../../../../docs/superpowers/plans/2026-11-14-cl9-h1-h9-resultado.md),
el [uso de los lectores](README.md) y el [runbook previo de VM](RUNBOOK-cl7-vm.md).
Antes de trabajar, leer `AGENTS.md`, `docs/CODING_RULES.md` y `cpp-pro`; aplicar las skills de ingeniería inversa
y consultar Context7 para las APIs usadas. El modo tutor está apagado. No mergear el PR ni modificar
configuraciones de GitHub.

## 1. Qué se necesita y qué ya está disponible

Hay dos tareas diferentes, ambas necesarias:

| Tarea | Insumos y resultado |
| --- | --- |
| Repetir los 59 tests que se saltearon | Recuperar archivos archivados, verificar sus hashes y activar tres variables. Se puede hacer en el host, sin arrancar el original. |
| Validar H1/H9 en el original actual | Sesión nueva de la VM con el cliente c32, acciones y evidencia correlacionadas. Las referencias históricas no sustituyen esta sesión. |

El [manifest](cl9-vm-evidence.json) enumera los **59 nombres de CTest**, sus 23 fuentes, dependencias y hashes.
Se reparten en **20 memoria/modelos, 25 red y 14 trazas gráficas**. Comparten 39 dumps, 17 archivos de red
(log, dos claves y catorce archivos de siete pares de inventario) y dos trazas descomprimidas. Un caso de Raven hereda el gate
de memoria pero no consume un dump; no necesita una captura nueva. `Data\` completo ya está en
`Client/assets/Data`; el manifest verifica 51 archivos explícitos, no reemplaza el resto de esa carpeta.

Identidades fijadas:

| Fuente | Commit / identidad |
| --- | --- |
| [Research primario](https://github.com/EmaNLJN/mu-server-repack-research-102i) | `14f0f931f68776dd103927f58c4359dc29ced712` |
| [Lab: cable y lector con listas](https://github.com/EmaNLJN/mu-online-reverse-lab) | `9f83b4c7e4e40aaa3b3f0876c9ddbf04a3fa163d` |
| Lab: trazas de [PR45](https://github.com/EmaNLJN/mu-online-reverse-lab/pull/45) | `cb575444f3f0923640083431ceabe1dc24aa2186`; carpeta `reports/gameserver-passive-2026-10-30/graphics-01` |
| Cliente para **CL9 nueva** y trazas gráficas archivadas | SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`, 3.058.308 bytes, PE32 i386, ImageBase preferida `0x00400000` |
| Capturas **históricas de memoria** | SHA-256 del cliente `2f16a9c6bc0905a78ff2be3306d26673a4a2b0fda0956dba492d0a9b2b411de0` |

La igualdad de `.text` documentada entre builds no acredita igualdad del resto del PE ni convierte un dump
2f16 en una observación de c32. El cable queda fijado por sus bytes; su hash no demuestra por sí solo qué EXE
estaba ejecutándose. Los gzip gráficos se verifican contra el manifest de publicación y sus bytes descomprimidos.

Los clones parciales pueden mostrar estos archivos en el árbol Git sin tener sus blobs descargados.
Recuperarlos desde los commits anteriores antes de pedir capturas nuevas. No sustituir una referencia histórica
por otra sesión cambiando nombres, conteos o expectativas para que pase.

## 2. Recuperación reproducible en el host

Requisitos: Python 3.10+, Git autenticado con lectura de ambos repos privados, MuRet con `Client/assets/Data`
completo y su build de tests. No hace falta Python dentro de XP. MuRet y CTest corren en el host moderno; el cliente SDL3/GL3.3
no se debe intentar ejecutar dentro del guest XP. Usar una carpeta privada **fuera de cualquier
checkout Git**. El script sólo copia archivos de Git, verifica hashes, descomprime trazas y genera los siete
pares de inventario con una referencia matemática propia. El EXE se lee como datos; nunca se ejecuta.

Ejemplo PowerShell del host; reemplazar únicamente las rutas por las del equipo:

```powershell
$MuRet = 'C:\trabajo\MuRet'
$Research = 'C:\trabajo\mu-server-repack-research-102i'
$Lab = 'C:\trabajo\mu-online-reverse-lab'
$Evidence = 'C:\capturas-privadas\cl9-h1-h9'
$MainFile = Join-Path $Research 'client-research\mainexe-vmxp-c32bbd5f\original\main.exe'
$Helper = Join-Path $MuRet 'Client\docs\research\scripts\game\capture\recover_cl9_evidence.py'

py -3 $Helper --research $Research --lab $Lab --output $Evidence --main-exe $MainFile --fetch
if ($LASTEXITCODE -ne 0) { throw 'Recuperación incompleta: revisar el informe privado antes de seguir.' }
. (Join-Path $Evidence 'env.ps1')
```

En Linux, la misma CLI funciona con `python3`; cargar después `source /ruta/privada/env.sh`.
`--fetch` permite traer los commits/blobs faltantes; no hace checkout, merge ni cambia las ramas de los clones.
Sin esa opción, los objetos deben estar disponibles localmente: no se permite transporte implícito.
En un clon parcial usar `--fetch` para los blobs faltantes, sin traer el resto del contenido del repo.

La carpeta queda así:

```text
memory/2026-10-09-devias/          # tablas y terreno del snapshot histórico
memory/2026-10-09-lorencia/        # terreno, slots y buffers de texturas
memory/2026-10-09-lorencia-obj/    # listas y estado de objetos
wire/                            # log, claves y catorce sidecars privados
gl/                              # dos gzip y sus trazas descomprimidas
tools/c3c4_reference.py           # referencia propia del cifrado, fijada por hash
tools/s31mem.c                   # lector pasivo con directiva list
env.sh / env.ps1
inventory-report.json           # integridad/disponibilidad, no certificado de fidelidad
```

El helper no sobrescribe una captura distinta. Una salida incompleta conserva lo recuperado y se puede
reanudar. Los códigos son 0 completo, 3 incompleto y 1 error. `--check-only` verifica sin escribir ni generar.
Con `--main-exe`, exige el hash c32 antes de escribir; sin ese archivo no inventa los pares de inventario.
El informe deja `native_verified=false` y `tests_launched=false`: tener archivos no significa haber probado nada.

Para verificar nuevamente y ejecutar los casos del manifest:

```powershell
py -3 $Helper --research $Research --lab $Lab --output $Evidence --main-exe $MainFile --check-only
if ($LASTEXITCODE -ne 0) { throw 'Evidencia incompleta o distinta.' }
$Manifest = Get-Content (Join-Path (Split-Path $Helper) 'cl9-vm-evidence.json') -Raw | ConvertFrom-Json
$TestList = Join-Path $Evidence 'tests59.txt'
$Manifest.tests.name | Set-Content -Encoding ascii $TestList
ctest --test-dir (Join-Path $MuRet 'build\windows-x64') -C Debug --tests-from-file $TestList --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw 'Hay diferencias: conservar el log y diagnosticar sin modificar el oráculo.' }
```

Se necesita CTest 3.29+ para `--tests-from-file`. Para otro build cambiar sólo `--test-dir` y su configuración.
No requieren GPU estos 59 tests de referencias; los 106 etiquetados `gl` son otro grupo.
No imprimir el contenido de `muproxy.log`, claves, payloads de login ni volcados. No subirlos a MuRet.
Compartir sólo herramientas, hashes, conteos y resultados sin datos personales.

El CI corre los unittests sintéticos del helper en Linux y Windows, incluida la prueba de junction NTFS
cuando ese host la admite. Para repetirlos: `python -m unittest discover -s Client/docs/research/scripts/game/capture -p test_recover_cl9_evidence.py -v`. No usan los originales ni necesitan la VM.

## 3. Preparar una sesión nueva c32 en la VM

El agente con VM necesita localizar: hipervisor/runner, VM del cliente, ruta real del EXE del guest,
servidor del laboratorio, cuenta de prueba por un mecanismo local y acceso interactivo al juego.
No asumir nombres de VM, PID, IP, puerto, credenciales, snapshots ni procesos de sesiones anteriores.

1. Identificar la VM y el cliente. Copiar el EXE del **guest real** al host sólo para hashearlo; comparar con c32.
   Verificar además PID, módulo y base cargada `0x00400000` antes de usar las VAs de este documento.
   Una copia del EXE del repo no demuestra qué proceso corre en el guest.
2. Crear un snapshot con nombre nuevo. Registrar hash del cliente, hash/tree de Data, OS/arquitectura,
   herramientas/compilador, versiones, zona horaria y medición de diferencia de relojes host/guest.
3. Compilar lectores propios en el host y copiarlos al guest. Ejecutar originales y lectores del proceso
   **únicamente dentro de la VM**. Operaciones `vmrun`/`vmguest`, de a una por VM.
4. El operador del guest entra al juego. Credenciales no van al chat, scripts, capturas públicas ni commits.
   Para Amigos se necesita nivel 6+. El GS0.97 del repack no acredita el flujo completo de amigos de este cliente:
   si falta soporte, declarar `BLOCKED_SERVER_FRIENDS` y conservar las observaciones UI que sí puedan hacerse.
5. Guardar evidencia en una sesión **nueva**, por ejemplo `native-c32/session-01`, sin tocar `memory/`, `wire/`
   ni `gl/` históricos. Copiar al host y verificar tamaños/hashes **antes** de restaurar el snapshot.

Para `passive_camera`, `passive_cl7` y, sólo si hace falta, `read_fpu`, usar los comandos y límites del
[README de lectores](README.md). No emular el EXE, parchearlo, eludir protecciones ni alterar sus contadores.
El lector FPU suspende/reanuda un incremento: no es necesario para los casos UI. Ante `recovery_pending`,
seguir su protocolo de recuperación; no matar el lector ni restaurar la VM mientras el hilo siga suspendido.

El `s31mem.c` recuperado es el del lab9f83: **sí** soporta `list`. La variante homónima del research primario
no la soporta y no sirve para `obj_nodes`. El helper conserva su SHA. Compilar para x86, por ejemplo:

```text
i686-w64-mingw32-gcc -O2 -o s31mem.exe s31mem.c
```

Verificar PE32/imports/runtime compatible con XP antes de usarlo. No está acreditada aquí una nueva compilación
ni ejecución de ese lector en el guest. Sólo abre con `PROCESS_QUERY_INFORMATION | PROCESS_VM_READ` (`0x410`).
No escribe en el proceso. Puede rellenar con cero páginas ilegibles: **exit0 no basta**, revisar `s31mem.txt`.
Una lectura incompleta, truncada, con páginas ilegibles o snapshot inestable no se convierte en evidencia válida.

Para un snapshot UI c32, crear un directorio nuevo y usar el [spec acotado](spec-cl9-ui.txt):

```text
mkdir C:\captura\cl9-ui-01
s31mem.exe <PID-verificado> C:\captura\spec-cl9-ui.txt C:\captura\cl9-ui-01
```

Son siete DWORD de estado/identidad y cinco BYTE de mouse. Los flags de mouse se contrastaron como BYTE en
`0x4b3444`, `0x4b3947..0x4b3961`, `0x4b3ce3..0x4b3cf1`, `0x4b4099` y `0x4b47b6`.
El spec no lee nombres, chat ni credenciales. Registrar la secuencia y los tiempos: la lectura es secuencial,
no atómica. Evitar que la consola del lector cambie el foco del juego; si lo hace, esa muestra no prueba U1.
Para conocer el foco Win32 hace falta además una lectura de `GetGUIThreadInfo`/`IsWindow` del HWND: el DWORD
del manager o una captura de pantalla no sustituyen ese dato. Si no hay herramienta de foco, declarar U1 pendiente.

## 4. Casos de H1/H9 y observaciones exigidas

Cada caso debe producir entrada, precondición, secuencia, evidencia original, comparación MuRet y dictamen
`MATCH`, `DIFFERENCE`, `BLOCKED` o `UNVERIFIED`. No convertir un resultado parcial en PASS universal.

| Caso | Acción y estado | Qué comparar con ASM y MuRet |
| --- | --- | --- |
| H1-E1 | Héroe parado, escribir `^^`; repetir caminando, con elfo y con transformación realmente cargada | `576eb0 → 576a60 → 4ea030`: parado dos acciones 18 iguales antes del chat; caminando una. Para `^^`, acción 0x8e; dirección del héroe. Si la animación no existe en el modelo transformado, se conserva el no-op pero continúan los envíos. Registrar modelo/acciones y recurso realmente cargado. |
| H1-E2 | Provocar por una mecánica real del laboratorio un DF de tipo 0xf1 con actor publicado | `4c6b40 → 51fe10(pos,0)`: empujón, sonido 0x1b, intentos 0x5c5/2, z + 80; sin explosión opcional 0x5ed. Con slots libres se esperan 20 altas. Correlacionar paquete, estado y frame; vídeo por sí solo no cuenta intentos ni RNG. |
| H1-E3 | Aplicar al héroe el bit 0x02000000; repetir sin quitarlo, y observar un actor distinto | `4e68d3..4e6904`: aviso con nombre vacío y Text[1828], tipo 1, en cada aplicación al héroe; OR después. Otro actor sólo recibe el bit. Si el servidor no ofrece una mecánica para ese flag, registrar bloqueo, no alterar memoria para simularla. |
| H9-B1 | Abrir Amigos, mantener un botón, cerrar con F y reabrir sin una liberación intermedia; repetir F6 y caja Agregar | Destructor 0x45adf0 limpia pressed_button sólo por igualdad de ID. Cerrar una ventana debe conservar el botón ajeno que sigue vivo. Guardar snapshots/acciones correlacionados, no sólo el estado final. |
| H9-D1 | Doble clic sobre fila offline y sobre un botón; observar también un tercer clic y mouse UP | WndProc 0x4b47b6 prende double, sin DOWN ordinario; siguiente mensaje 0x4b3444 lo borra o la lista lo consume antes. Un paso lógico sin mensaje no lo apaga ni lo repone. La fila offline permite separar H9 de abrir chat H10. No inferir el orden de mensajes por los píxeles. |
| H9-Q1 | Abrir Agregar, escribir nombre sintético de prueba y Enter; mover/redimensionar con colas pendientes | `45aa50 → 45aa80(1)` drena también Name/Answer/teclado/foco durante Draw; Close/Raise del manager esperan su Step. Comparar orden y único envío; el port no debe repetirlo por presentar más cuadros intermedios. Registrar wire sin publicar sus bytes. |
| H9-U1 | Cerrar el EDIT y continuar escribiendo; volver a abrirlo | Registrar HWND/foco/clase y estado antes/después. `467710` restaura proc y libera GDI, sin DestroyWindow/SetFocus explícitos. Resolver la pregunta con el HWND real; no inventar una limpieza en el port. Selección, IME y editor completo siguen P-C181. |

Los casos de IDs que requieren vuelta de 32 bits y el pool totalmente lleno están cubiertos por contraste ASM
y regresiones del port. No forzar esos estados escribiendo en el original. Una captura pasiva que no los alcanza
no los acredita dinámicamente. Para el RNG del DF, el contrato estático es `8 + 40 + 5*altas` en ese handler;
observar veinte partículas no demuestra esa cuenta, ni permite atribuir los sorteos de otros sistemas.

Para nuevos quads/foco/efectos usar trazas y herramientas del laboratorio compatibles con ese guest.
La configuración histórica de GLIntercept está en la carpeta `graphics-01` de PR45. Registrar versión,
configuración y overhead; no reemplazar DLL ni forzar hooks como parte del helper de recuperación.
Si se instrumenta una sesión mediante el procedimiento ya validado del laboratorio, diferenciarla de una corrida
sin instrumentación. No inferir FPS nativo, algoritmo o fidelidad total sólo de un vídeo.

## 5. Cómo devolver el trabajo

Copiar [cl9-vm-session.example.json](cl9-vm-session.example.json) al directorio privado de la sesión
y completarlo con observaciones, no con valores supuestos. Los null y UNVERIFIED son intencionales.
No publicar el archivo si incorpora datos personales o rutas sensibles.

Conservar privados raw logs, claves, volcados, trazas, imágenes y vídeo. Devolver al PR/handoff:

- Commits de MuRet y fuentes, hashes de cliente/Data/herramientas/archivos, manifest y comandos reproducibles.
- Tabla de los59: ejecutado, pasó/falló, archivo usado y perfil de procedencia; nunca contar un skip como aprobado.
- Tabla de los casos nuevos H1/H9 con estado y límites; diferencias con VA/caller, entrada y observador concretos.
- Estado final de VM, snapshot y lectores; confirmar evidencia copiada y que no quedó un hilo suspendido.

Si aparecen diferencias, conservar el original y su referencia. Corregir el port o el fixture cuando se demuestre
una precondición obsoleta; no ajustar tolerancias, conteos o datos esperados sólo para obtener verde.
Los 59 tests son una regresión histórica acotada. Pasarlos no termina CL9 ni integra H3/H4/H7/H10/H13.
