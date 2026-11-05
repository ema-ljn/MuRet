# El cable (CL7)

Los paquetes que el original mandó y recibió, en claro, para comparar con lo que MuRet lee y escribe
(03 § El corte y § C3/C4, 04). Hay dos capturas:

- el `muproxy.log` de `MURET_CLIENT_WIRE_DIR`, con las claves del servidor (`Dec1.dat`, `Enc2.dat`) en la
  misma carpeta;
- el PCAP de la sesión de la PR #45 del laboratorio (`network.pcap`), que `pcap2log.py` pasa al formato
  del `muproxy.log`.

Los C3 y C4 necesitan `MURET_C3C4_REFERENCE` (el `c3c4_reference.py` del repo de research) y
`MURET_MAIN_EXE` (la tabla XOR de `0x6d84fc`). Sin ellos, los C1 y C2 se leen igual y los C3 y C4 se
saltean con un aviso. De dónde sale cada archivo: [`../../README.md`, El entorno](../../README.md#el-entorno).

**Privacidad.** Las dos capturas llevan el login, el chat y los nombres de los personajes. Por defecto
`decode.py` saca solo lo que el plan deja guardar (`wirelib.is_safe`: la respuesta del `F3 03`, el
`F3 10`, `D3`, `D7`, `1C`, `F3 04`, `F3 12`, `0F` y `A3`). Con `--all` sale todo, y esa salida no va a Git
ni a un informe; tampoco el log que da `pcap2log.py`.

| Script | Qué hace | Sub-ola |
| --- | --- | --- |
| `wirelib.py` | el lector: junta los segmentos por sentido, corta los paquetes, saca el `0xAA` por paquete (Q-C18), el XOR32 del cliente y el C3/C4 (Dec1, Dec2), y da cada uno con la cabecera C1/C2 rearmada y como viajó | todas las de abajo |
| `decode.py` | un paquete por línea (conexión, hora, puerto, sentido, frame, serial, en claro y con `--wire` como viajó); `--heads` para elegir opcodes, `--count` para contar sin bytes | CL7.L1 (la respuesta del `F3 03`: celda (133, 127), mapa 0, dirección 0), CL7.L3 (el `F3 10`), CL7.4 B (`D3`, `D7`), CL7.5 C (`1C`, `F3 04`, `F3 12`) |
| `pcap2log.py` | el PCAP al formato del `muproxy.log`, por conexión y sentido, con `ACC` y `GAP` | CL7.5 C (los cuatro `1C` y sus `F3 12`, `C1 04 F3 68` en el cable) |
| `walk_stats.py` | los `A3` del cliente (el `D3` de `main.exe` con el opcode de FoxAnti): los largos, la dirección final y los del blanco al lado del final; y el eco de los `D3` del servidor | CL7.4 B, CL7.4 E |

Desde la raíz del repo, con la carpeta delante (`Client/docs/research/scripts/game/wire/`):

```bash
python -I decode.py --count                        # cuántos de cada opcode, sin bytes
python -I decode.py --heads f303                   # las respuestas del F3 03 (C3, con Dec2)
python -I decode.py --heads 1c,f304,f312 --wire    # el cambio de mapa
tar xzf capture_20261030T192219Z_8920d1.tar.gz     # en una carpeta propia, fuera del repo
python -I pcap2log.py 20261030T192219Z_8920d1/network.pcap > pr45.log
python -I decode.py pr45.log --heads 1c,f312 --wire
python -I walk_stats.py
```

Las conexiones se cuentan con las líneas `ACC` de todos los puertos (la primera es la 1), como en el
barrido C: en el `muproxy.log`, los caminos de Lorencia son de las conexiones 6, 10, 58, 68 y 70.
