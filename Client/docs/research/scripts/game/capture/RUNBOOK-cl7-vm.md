# Runbook: las capturas de la CL7 en la VM XP

Una sesión de la VM con tres capturas del `main.exe` original. Está escrito para que lo corra un agente en
el host de Emanuel, con pausas marcadas **HUMANO** donde hace falta jugar.

| Captura | Qué cierra | Herramienta |
| --- | --- | --- |
| La CW de la x87 con el cliente caminando, en Lorencia y Devias | Avanza P-C34 (la política única de `_ftol`, ADR 0009): da la CW con el hilo en el código del juego. Cerrarlo del todo pide el EIP adentro de `0x69fd3c` | `read_fpu.exe` (de este repo) |
| Los objetos de Devias | P-C37 | `s31mem.exe` (del repo de research) |
| La luz sombreada del terreno (`0x7c0ccc4`) en Lorencia y Devias | P-C15 e, la luz de #190 contra el original | `s31mem.exe` |

Nada de esto prueba MuRet: se observa el original, build `c32bbd5f`
(SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`). El significado de cada salida
está en el [README de esta carpeta](README.md).

## Reglas

- **Solo lectura.** Los dos lectores abren el proceso con `PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`.
  `read_fpu` además suspende un instante el hilo de `MU` y lo reanuda. Ninguno escribe, inyecta ni depura.
- **Snapshot antes, copia al host, y recién después restaurar.** Si se restaura antes de copiar, la
  captura se pierde.
- **`vmrun` de a una operación por vez.** Nunca dos comandos de `vmrun` en paralelo contra la misma VM.
- **Sin datos personales.** El login lo hace Emanuel; usuario, contraseña, cuenta y nombre del personaje
  no pasan por el agente ni quedan en archivos, logs, commits o mensajes. No guardar capturas de pantalla.
  Las credenciales del guest viven en variables de entorno del host: no usar `set -x` ni imprimirlas.
- **Los resultados no van a MuRet.** Los volcados van al repo de research; a MuRet solo llegan resúmenes
  numéricos.
- **`recovery_pending` en `read_fpu`:** no matar `main.exe` ni el lector, y no restaurar el snapshot.
  Parar y avisar a Emanuel (README, «Compilar y ejecutar»).

## Variables

| Variable | Qué es |
| --- | --- |
| `MURET` | el clon de MuRet |
| `RESEARCH` | el clon de `EmaNLJN/mu-server-repack-research-102i` |
| `MU_VMRUN` | el `vmrun` del host |
| `MU_VMX` | el `.vmx` de la VM XP |
| `MU_VM_USER`, `MU_VM_PASS` | la cuenta del guest (las pone Emanuel; no se escriben en ningún lado) |
| `MU_GUEST_MAIN` | la ruta de `main.exe` en el guest, por ejemplo `C:\MU\main.exe` |
| `OUT` | una carpeta del host fuera de Git, por ejemplo `~/capturas/2026-11-10-cl7` |

```bash
vm()  { "$MU_VMRUN" -T ws "$@"; }
vmg() { "$MU_VMRUN" -T ws -gu "$MU_VM_USER" -gp "$MU_VM_PASS" "$@"; }
SNAP="pre-cl7-$(date +%Y%m%d-%H%M)"
mkdir -p "$OUT/bin" "$OUT/fpu" "$OUT/lorencia-luz" "$OUT/devias-obj" "$OUT/devias-luz"
```

Si el host no es VMware Workstation, cambiar `-T ws` por el tipo que corresponda.

## 0. Compilar en el host

```bash
cd "$MURET/Client/docs/research/scripts/game/capture"
i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -Wextra -Wconversion -Wshadow -Wold-style-cast -Werror \
  -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -D__USE_MINGW_ANSI_STDIO=0 -fno-exceptions -fno-rtti \
  -static -mwindows read_fpu.cpp -o "$OUT/bin/read_fpu.exe" -luser32
i686-w64-mingw32-gcc -O2 -o "$OUT/bin/s31mem.exe" "$RESEARCH/client-research/analysis/mainexe/scripts/s31mem.c"
cp "$RESEARCH/client-research/mu-golden/memoria-cliente/2026-10-09-lorencia-obj/spec-lectura-obj.txt" "$OUT/bin/"
printf 'mapa 0x6C9DD0 0x4\nescena 0x6D5DC8 0x4\nluz_sombreada 0x7C0CCC4 0xC0000\n' > "$OUT/bin/spec-luz.txt"
file "$OUT/bin/"*.exe                      # los dos: PE32 executable ... Intel 80386
sha256sum "$OUT/bin/"* > "$OUT/bin/SHA256SUMS"
```

`spec-luz.txt` vuelca el mapa (`0x6c9dd0`) y la escena (`0x6d5dc8`) junto con la luz, así cada volcado dice
de qué mapa es. La luz sombreada son 256 × 256 celdas de tres `float`: `0xC0000` bytes.

**Corte:** si alguna compilación falla, parar. No corregir los fuentes en esta sesión.

## 1. Preparar la VM

```bash
vm snapshot "$MU_VMX" "$SNAP"
vmg createDirectoryInGuest "$MU_VMX" 'C:\captura'
for f in read_fpu.exe s31mem.exe spec-lectura-obj.txt spec-luz.txt; do
  vmg copyFileFromHostToGuest "$MU_VMX" "$OUT/bin/$f" "C:\\captura\\$f"
done
vmg copyFileFromGuestToHost "$MU_VMX" "$MU_GUEST_MAIN" "$OUT/bin/main.exe.guest"
sha256sum "$OUT/bin/main.exe.guest"        # tiene que ser c32bbd5f16ca400c...ba26a
```

**Corte:** si el SHA-256 del `main.exe` del guest no es el de arriba, parar: las direcciones del research
no valen para otro build.

## 2. HUMANO: entrar a Lorencia y caminar

Emanuel abre el cliente, entra con su cuenta y queda en Lorencia (mapa 0). Mientras dura el paso 3, camina
sin parar con el clic apretado y, si puede, girando la cámara. El agente espera la confirmación
«caminando en Lorencia».

## 3. La CW caminando en Lorencia (20 muestras)

```bash
for i in $(seq -w 1 20); do
  vmg runProgramInGuest "$MU_VMX" -interactive 'C:\captura\read_fpu.exe' --output "C:\\captura\\lor-$i.txt" \
    || { echo "read_fpu lor-$i falló"; break; }
  vmg copyFileFromGuestToHost "$MU_VMX" "C:\\captura\\lor-$i.txt" "$OUT/fpu/lor-$i.txt"
  grep -q recovery_pending "$OUT/fpu/lor-$i.txt" && { echo "recovery_pending en lor-$i: PARAR"; break; }
  sleep 1
done
```

Cada archivo bueno empieza con `thread_snapshot 1`, trae `eip=` y `controlWord=`, termina en `end` y no tiene
ninguna línea `error`. Los códigos de salida de `read_fpu`: 0 bien, 1 error, 2 uso, 4 el hilo terminó
durante la recuperación.

**Cortes:**
- `recovery_pending` o código 4: aplicar la regla de arriba y avisar.
- `error field=signature`: el proceso no es el build `c32bbd5f`. Parar.
- `error field=MU_window`: el cliente no está abierto en la sesión interactiva. Volver al paso 2.

## 4. La luz sombreada en Lorencia

**HUMANO:** Emanuel deja al personaje quieto en Lorencia.

```bash
PID=$(vmg listProcessesInGuest "$MU_VMX" | grep -i 'main\.exe' | sed -n 's/^pid=\([0-9]*\),.*/\1/p' | head -1)
echo "pid=$PID"                            # un solo número; si sale vacío o hay dos main.exe, parar
vmg createDirectoryInGuest "$MU_VMX" 'C:\captura\lorencia-luz'
vmg runProgramInGuest "$MU_VMX" -interactive 'C:\captura\s31mem.exe' "$PID" 'C:\captura\spec-luz.txt' \
  'C:\captura\lorencia-luz'
for f in s31mem.txt mapa.bin escena.bin luz_sombreada.bin; do
  vmg copyFileFromGuestToHost "$MU_VMX" "C:\\captura\\lorencia-luz\\$f" "$OUT/lorencia-luz/$f"
done
```

No guardar la salida de `listProcessesInGuest`: trae el usuario del guest. Solo se usa el PID.

**Criterio de éxito:** `s31mem.txt` termina en `fin`, sus tres líneas dicen `ilegibles=0 ok` y no hay
ninguna con `ERROR`; `od -An -tu4 mapa.bin` da `0`, `od -An -tu4 escena.bin` da `5`, y `luz_sombreada.bin`
pesa 786.432 bytes.

## 5. HUMANO: ir a Devias y caminar

Emanuel va a Devias (mapa 2) y camina como en el paso 2. El agente espera «caminando en Devias».

## 6. La CW caminando en Devias (20 muestras)

El mismo lazo del paso 3, con `dev-` en lugar de `lor-`.

## 7. Los objetos y la luz de Devias

**HUMANO:** Emanuel deja al personaje quieto en Devias.

```bash
PID=$(vmg listProcessesInGuest "$MU_VMX" | grep -i 'main\.exe' | sed -n 's/^pid=\([0-9]*\),.*/\1/p' | head -1)
vmg createDirectoryInGuest "$MU_VMX" 'C:\captura\devias-obj'
vmg runProgramInGuest "$MU_VMX" -interactive 'C:\captura\s31mem.exe' "$PID" 'C:\captura\spec-lectura-obj.txt' \
  'C:\captura\devias-obj'
for f in s31mem.txt obj_gworld.bin obj_scene.bin obj_count.bin obj_heads.bin obj_lights.bin obj_nodes.bin; do
  vmg copyFileFromGuestToHost "$MU_VMX" "C:\\captura\\devias-obj\\$f" "$OUT/devias-obj/$f"
done
vmg createDirectoryInGuest "$MU_VMX" 'C:\captura\devias-luz'
vmg runProgramInGuest "$MU_VMX" -interactive 'C:\captura\s31mem.exe' "$PID" 'C:\captura\spec-luz.txt' \
  'C:\captura\devias-luz'
for f in s31mem.txt mapa.bin escena.bin luz_sombreada.bin; do
  vmg copyFileFromGuestToHost "$MU_VMX" "C:\\captura\\devias-luz\\$f" "$OUT/devias-luz/$f"
done
```

**Criterio de éxito de los objetos:** `s31mem.txt` termina en `fin`, sin `ERROR`, con `ilegibles=0` en
cada región y, en la línea de `obj_nodes`, `cortadas=0 nodos_ilegibles=0 ok`. `obj_gworld.bin` da `2`,
`obj_scene.bin` da `5`, y el `nodos=` de `obj_nodes` es igual al número de `obj_count.bin`. Como referencia,
la captura de Lorencia (`2026-10-09-lorencia-obj/log-lectura-obj.txt`) dio 2.870.

**Criterio de éxito de la luz:** el mismo del paso 4, con `mapa.bin` en `2`.

**Corte:** si una región sale con `ilegibles` distinto de 0 o con `ERROR`, repetir esa lectura una vez con
el personaje quieto. Si se repite, parar y reportar la línea.

## 8. Cerrar la VM

Primero comprobar en el host que están los 40 archivos de `fpu/` y las tres carpetas completas. Recién
entonces:

```bash
sha256sum "$OUT"/fpu/* "$OUT"/*/s31mem.txt "$OUT"/*/*.bin > "$OUT/SHA256SUMS"
vm revertToSnapshot "$MU_VMX" "$SNAP"
```

El snapshot queda; Emanuel decide si lo borra.

## 9. Entregar

**La CW.** Los 40 archivos crudos se quedan en `$OUT`. A MuRet va solo esta tabla:

```bash
for f in "$OUT"/fpu/*.txt; do
  eip=$(sed -n 's/^eip=//p' "$f"); cw=$(sed -n 's/^controlWord=//p' "$f")
  if [ $((16#$eip)) -ge $((16#00401000)) ] && [ $((16#$eip)) -lt $((16#006b8000)) ]; then
    zona=main.exe; else zona=fuera; fi
  echo "$(basename "$f" .txt) eip=$eip cw=$cw $zona"
done
```

`main.exe` quiere decir que el EIP cayó en su `.text` (`0x401000..0x6b7fff`; `_ftol` es `0x69fd3c`). Lo que
cae fuera suele ser `ntdll` o un driver, con el hilo esperando, como la muestra vieja (`0x7c90e4f4`).
**Si las 40 caen fuera**, repetir los pasos 3 y 6 con 50 muestras cada uno; si siguen fuera, reportarlo así.

**Los volcados.** En el repo de research, una carpeta por captura en `client-research/mu-golden/memoria-cliente/`,
con la fecha real: `AAAA-MM-DD-devias-obj/`, `AAAA-MM-DD-devias-luz/` y `AAAA-MM-DD-lorencia-luz/`. Cada una
lleva sus `.bin`, el spec usado, `s31mem.txt` renombrado a `log-lectura-obj.txt` o `log-lectura-luz.txt`, y
su `SHA256SUMS`, como `2026-10-09-lorencia-obj/`. Van en un commit propio, en una rama y con PR de ese repo.

**El resumen para MuRet**, en el hilo de la CL7 o en el PR que lo pida: la tabla de la CW, las líneas de
los tres `s31mem.txt` (no tienen datos personales) y el SHA del commit del research. Con eso se clasifica
P-C34, se compara la luz sombreada celda por celda con `World::ShadeTerrainLight` de los mapas 0 y 2, y se
suman los objetos de Devias a los golden.
