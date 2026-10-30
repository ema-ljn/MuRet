# 05. Formatos de datos

Fuentes: `mu-client/docs/component-bmd-tables.md`, `component-terrain.md`,
`component-textures.md`, `component-pipeline.md`, `tier1-*.md`, `coverage-main.md` §0.1 y §5.1,
y `analysis/mainexe/evidence/s31-assets/` (incluido `byte-a-byte.md` y
`divergencias-cliente-servidor.md`). Del repo `mu-online-reverse-lab`:
`analysis/decompilacion-maxima/INDEX.md`, `assets/assets_manifest.csv`, `assets/assets_summary.json`,
`scripts/asset_manifest.py` y `abi/`.

## Qué hay en `Data\`

El inventario completo del cliente (`assets_manifest.csv`, 5.774 archivos; 5.701 identificados, 29
parciales, 44 binarios de soporte, 0 errores de decodificación):

| Formato | Archivos | Transformación |
| --- | ---: | --- |
| OZJ (JPEG) | 2.380 | prefijo de 24 bytes; **7 son TGA con extensión OZJ** (Q-C14) |
| OZT (TGA) | 664 | prefijo de 4 bytes |
| BMD de modelo | 1.962 | v0A en claro o v0C cifrado con K16; todos recorridos sin bytes sobrantes |
| BMD de tabla | 7 + 26 estructuras | XOR3 `FC CF AB` |
| terreno | 23 `EncTerrain` `.map`/`.obj`, 29 `.att`; además 18 `.map`, 20 `.obj` y 24 `.att` que el manifiesto clasifica como `Terrain/*` y no `EncTerrain/*` (cuáles carga el cliente, a confirmar en CL1) | K16 (+ XOR3 en `.att`) |
| OZB | 19 | BMP con prefijo de 4 bytes |
| WAV | 484 | efectos de sonido |
| MP3 | 23 | música por mapa (`Music\Pub.mp3`, `Devias.mp3`, `MuTheme.mp3`…) |
| Ogg/Vorbis | 3 | por `wzAudio` → `vorbisfile` → `ogg` |
| `message.wtf` | 1 | XOR `0xCA` |
| claves C3/C4 | 2 | `Enc1.dat`, `Dec2.dat` |

Es la otra parte bien validada: los cargadores de tablas, terreno y texturas reproducen **byte a
byte** lo que tiene en memoria el cliente original vivo (`mu-golden/memoria-cliente/`, Lorencia y
Devias). Todos trabajan sobre buffers: leer el archivo es tarea del que llama.

## Tablas `.bmd`

| Tabla | Original (carga / escritura) | Formato | En memoria |
| --- | --- | --- | --- |
| XOR3 | `0x5496c0` | XOR con `FC CF AB` (`0x6c8c74`) | — |
| checksum | dentro de los cargadores | semilla `key << 9`, ADD o XOR según la paridad, mezcla cada 4 dwords | — |
| `Gate.bmd` | `0x54a360` / `0x54a2e0` | 256 × 9 bytes | `*[0x07688AB8]`, 0x900 |
| `Item.bmd` | `0x54b9e0` / `0x54b8f0` | 8192 × 0x54 bytes + checksum `0xE2F1` | `*[0x07758550]`, 0xA8000 |
| `Skill.bmd` | `0x54acf0` / `0x54aa20` | 256 × 0x3C bytes + checksum `0x5A18`; luego un contenedor "guardia" con `rand()` | `*[0x076BD1D8]`, 0x3C00 |
| `Text.bmd` | `0x5495e0` / `0x549700` | 2048 × 300 bytes | `g_GlobalText` `0x076BD1DC` |

Si falta un archivo, el original muestra un `MessageBox`, manda `WM_DESTROY` y **sigue cargando**
las demás tablas.

## Terreno de un mundo (`Data\World<N>\`)

Grilla de 256 × 256 celdas, `idx = y * 256 + x`.

| Archivo | Original | Formato |
| --- | --- | --- |
| capa K16 | inline en los tres cargadores | XOR de 16 bytes con `prev0 = 0x5E` y paso `0x3D`, un solo flujo |
| `EncTerrain<N>.map` | `0x5f87b0` | 2 bytes + capa 1 + capa 2 + alfa = 0x30002 bytes |
| `EncTerrain<N>.att` | `0x5f8390` + XOR3 | 4 bytes + 65.536 celdas de 1 o 2 bytes (atributos: zona segura, no caminable…) |
| `EncTerrain<N>.obj` | `0x607370` | 4 bytes + N × 30 bytes (objetos del mapa), agrupados en 256 bloques |
| `TerrainHeight.OZB` | `0x5f9020` | 4 bytes + BMP de 0x10438 bytes; las alturas se leen desde BMP + 1080 |
| `TerrainLight.OZJ` | `0x63b490` | luz base: 256 × 256 × 3 floats |

Verificado contra la memoria del original: `.map`, `.att`, alturas y luz. El `.obj` se cerró en la
tanda 3 contra las 256 listas de objetos de Lorencia (2.870 nodos).

## Texturas

| Formato | Original | Qué es |
| --- | --- | --- |
| tamaño | bucles `0x63bf81` / `0x63bb34` | potencia de 2 desde 1 hasta cubrir ancho y alto, tope 512 |
| OZT | `0x63bd00` | 4 bytes de prefijo + TGA de 32 bpp; lee ancho, alto y bpp, ignora tipo y descriptor; rechaza bpp ≠ 32 y lados > 512 |
| OZJ | `0x63b870` | 24 bytes de prefijo + JPEG baseline |
| JPEG | `0x66b8f0`..`0x67f0c0` | IJG libjpeg 6b enlazada estática |

Verificado: 9 tiles OZJ de Lorencia y 10 OZT, byte a byte contra los buffers de textura del cliente
vivo. `mu-client` escribió un decodificador JPEG propio compatible con libjpeg 6b para lograrlo.

## El pipeline de las DLLs

| DLL | Qué hace | Algoritmo |
| --- | --- | --- |
| `npcipher.dll` | cifra datos de NPC y monstruos | `K = SHA1(clave)`; modo 1 Blowfish, modo 0 AES-192; CFB de 8 bits; IV = `E_K(FF…FF)`; base64 |
| `mumsg.dll` | `message.wtf` | cabecera `CC 01 "WTF File"`, entradas `id:2 len:2 texto`, texto con XOR `0xCA` |
| `wz_zp.dll` | `.WZC` | bzip2 nivel 9 estándar |
| `wzCipher.dll` | `GetCRC32` | CRC-32 estándar, solo si el GUID coincide |
| `npchk.dll` | `CheckCRC` | MD5 contra 14 hashes permitidos |

Verificado: `npcipher` con 4 vectores reales en la VM; el resto con vectores estándar. *Inferido*
(alta): `main.exe` **no** carga estas cinco DLLs en el arranque normal; las usan el launcher y las
herramientas (`coverage-main.md` §3).

## Modelos `.bmd`

Los modelos 3D (personajes, monstruos, objetos) también son `.bmd`. El contenedor está recorrido
en los 1.962 modelos del cliente sin bytes sobrantes (`scripts/asset_manifest.py`, `walk_bmd`):

```text
"BMD" + versión (1 byte)
  v0x0C: tamaño (u32) + payload cifrado con K16 (el mismo de EncTerrain)
  v0x0A: el resto va en claro
nombre[32]
n_mallas, n_huesos, n_acciones        (int16 cada uno)
por malla:   n_vert, n_norm, n_uv, n_tri, textura (int16 x5)
             vértices 16 B · normales 20 B · UV 8 B · triángulos 64 B · nombre de textura[32]
por acción:  n_keys (int16), lock_positions (u8), y si lock: 12 B por key
por hueso:   dummy (u8); si 0: nombre[32] + padre (int16) + 24 B por key de cada acción
```

Verificado el **tamaño** de cada parte (el recorrido cierra exacto en los 1.962 archivos). El
**significado** de los campos (qué son los 24 B por key, cómo se interpola, cómo se aplica el
skinning) está *inferido* de la estructura y sigue sin leerse en el render de `main.exe`
(`coverage-main.md` §7, hito H3; ver [08](08-pendientes.md)).

## Audio

`wzAudio.dll` está decompilada entera (390 funciones) y es un derivado de **CoolPlayer 215**
(`decompilacion-maxima/INDEX.md` §3): 17 exports con ABI revisada (`abi/wzAudio_abi.h`), Ogg con
`vorbisfile.dll` + `ogg.dll` (compatibles con libogg 1.1 y libvorbis 1.0.x) y MP3 propio. La
música es un MP3 por mapa (`paso6-ui-map.md` §6); los efectos son WAV por DirectSound
(`0x41ed10` play, `0x41ee00` stop). *Estático.*

## Cliente contra servidor

Las tablas del servidor en uso del laboratorio están personalizadas (portales, requisitos de
items, skills, 70 celdas de atributos en la plaza de Lorencia). **El cliente se reconstruye fiel a
sus propios datos**, no a los del servidor: si difieren, el servidor decide y el cliente se entera
por el protocolo (`divergencias-cliente-servidor.md`).

## Qué implica para MuRet

| Original | MuRet | Ola |
| --- | --- | --- |
| Cargadores que escriben en globales del juego | `Data::` funciones puras: bytes → tipos de C++ (`ItemTable`, `TerrainMap`…), sin globales, testeables sin ventana | CL1 |
| `MessageBox` + `WM_DESTROY` si falta un archivo | error explícito al que llama (`std::expected`), sin UI en la capa de datos | CL1 |
| libjpeg 6b estática | una biblioteca JPEG del sistema o de vcpkg; los tests golden dicen si alcanza para ser byte a byte (pregunta abierta del roadmap) | CL1 |
| Rarezas de datos (checksum anulado de `Item.bmd`, alturas desde BMP + 1080, registro 7 de `Text.bmd` sin NUL, OZJ que son TGA) | **se reproducen**: son parte de cómo se leen los archivos reales (ver [07](07-quirks-y-bugs.md)) | CL1 |
| Los assets son del juego | MuRet **no** versiona ningún asset; los tests golden reciben la ruta por `MURET_CLIENT_GOLDEN_DIR` y se saltean si no está | CL1 |

Tests que fija:

- Por cada formato, un round-trip con datos sintéticos (siempre corre) (CL1).
- Por cada formato, comparación byte a byte contra `mu-golden` (`cliente-data*`, y la memoria del
  original para lo que tenga volcado), salteada sin la carpeta (CL1).
- Por cada formato, recorrer **todos** los archivos de `assets/original-data/` y comparar con
  `assets/decoded/` y con los conteos de `assets_manifest.csv` (CL1; los modelos en CL6).
- XOR3, K16 y checksum con vectores literales chicos escritos a mano (CL1).
