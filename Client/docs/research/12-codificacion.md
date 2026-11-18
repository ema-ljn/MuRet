# Codificación del cliente original y de su laboratorio XP

Análisis del 2026-11-15, solicitado por Emanuel para conservar el comportamiento de la época.
La antigüedad del cliente no determina una codificación. Este informe corresponde al PE32 x86
`main.exe` de SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`,
base `00400000`; no se generaliza a todos los builds de MU ni a cualquier instalación de Windows.

**Conclusión:** el juego intercambia y conserva cadenas de bytes. La interfaz EDIT usa APIs ANSI;
su significado depende del Windows que las procesa. El CRT, la fuente lógica, la fuente física,
los literales compilados y el wire son contratos distintos. No existe evidencia de un modo UTF-8
global, de una CP949 global ni de que los buffers de juego sean UTF-16.

## Procedencia y niveles de evidencia

Se contrastaron exports ASM/pseudocódigo con el PE, importaciones por mapeo de secciones, activos
del repo y de la VM, lecturas pasivas del original y entradas sintéticas por la UI de XP. El
original y los lectores de su proceso sólo se ejecutaron en VMXP. Los experimentos NLS/GDI son
programas propios ejecutados en ese mismo XP; no se presentan como trazas de llamadas del juego.

Las sesiones privadas son `encoding-state-01`, `encoding-assets-01`, `editor-paste-03`,
`editor-unicode-01`, `encoding-nls-01` y `encoding-nls-02`, dentro de `native-c32/session-02`.
Las capturas, CSV completos, EXE, fuentes y rutas personales permanecen fuera de Git. Cada sesión
que lee el original verificó el archivo de su módulo cargado contra el SHA anterior y base real
`400000`. Los CSV NLS tienen terminador y conteos comprobados.

## Qué hace el binario

| Capa | Evidencia estática | Qué permite afirmar |
| --- | --- | --- |
| EDIT y mensajes | IAT `CreateWindowExA=6b84f8`, `GetWindowTextA=6b8478`, `SetWindowTextA=6b847c`, `SendMessageA=6b8430`, `CallWindowProcA=6b846c` | La interfaz externa es ANSI; la letra A no fija por sí sola una CP concreta |
| Dibujo | IAT `TextOutA=6b807c`, `GetTextExtentPointA=6b806c`, `GetTextExtentPoint32A=6b8068`, `CreateFontA=6b8094` | Las cadenas y longitudes entregadas son bytes; no necesariamente un byte por glifo |
| Composición | `468320` usa `ImmGetCompositionStringA`, `GCS_COMPSTR`, buffer de 255 bytes y `strlen` | La ruta observada de preedición no es una cadena UTF-8 de SDL |
| CRT multibyte | Startup `6ac5c5 → 6a6c67`; `6a6c70` pasa `-3` a `6a687f`; normalizador `6a6a2c` llega a `GetACP` | Inicializa el estado multibyte con la página ANSI del sistema, no con 949 fija |
| Conversores CRT | Imports `MultiByteToWideChar=6b833c`, `WideCharToMultiByte=6b8238`; hay callers con CP_ACP=0 y CP_OEMCP=1 | La presencia de APIs W no convierte el juego entero a Unicode |
| Fuente lógica | `4ba737/4ba777/4ba7b9/4ba813` piden charset `[6c76a0]=134`, GB2312 | Es una petición al font mapper; falta distinguirla de la fuente elegida |
| Activos | `5495e0` carga registros de bytes de Text.bmd tras XOR3 | No identifica una codificación mediante una etiqueta del archivo |
| Red | Nombre/cuenta/clave de 10 bytes, textos y copias `strlen`/buffers según [04](04-protocolo.md) | El wire fija tamaños en bytes y no lleva una etiqueta de codificación |

Microsoft distingue explícitamente longitudes ANSI en bytes, longitudes W en unidades UTF-16,
caracteres y glifos. No deben intercambiarse esos conteos. [Longitudes de texto GDI](https://learn.microsoft.com/en-us/windows/win32/gdi/specifying-length-of-text-output-string),
[páginas de códigos](https://learn.microsoft.com/en-us/windows/win32/intl/code-pages).

### El avance multibyte no es un decodificador Unicode

`6a687f` se verificó completo contra los 429 bytes del PE. Su tabla precalculada contiene 932,
936, 949, 950 y 1361; también admite el resultado de GetCPInfo. La existencia de 949 en esa tabla
no demuestra que sea la página activa. En la rama SBCS escribe CP efectiva en `7d5d904`, limpia
la tabla y después llama un postproceso. El contenido final no se dedujo sólo de esa limpieza:
se leyó en el proceso vivo.

Los 22 bytes de `6a01fa`, candidata `_mbclen`, también coinciden con el PE. Retorna
`1 + ((tabla[unsigned(s[0])+1] & 4) != 0)`. No lee el segundo byte, no valida un trail y no trata
NUL especialmente. Lo llaman, entre otros, la búsqueda de emotes (`5769ba`), los cortes de
renglón (`632cc0`) y el banner (`46ab64`). «Avanza un byte» depende de la tabla activa, no es
una propiedad universal de los textos del ejecutable. La documentación moderna de
[_setmbcp](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setmbcp) ayuda a
identificar el contrato, pero no sustituye el ASM del CRT de este PE.

## Estado efectivo en esta VM

| Observación | Resultado |
| --- | --- |
| Sistema de la prueba propia | Windows NT 5.1, build 2600 (XP) |
| ACP / OEMCP | 1252 / 437 |
| LCID de sistema/usuario y del hilo del probe | `0409`; no equivale a haber leído el locale del hilo del juego |
| Layout del hilo del original | `04090409`, US; un único layout instalado |
| CRT del original, DWORD `7d5d904` | **1252** |
| Tabla CRT en `7d5da20`, 257 bytes | Sin ninguna entrada lead con máscara `04` |
| SHA-256 de esa tabla | `2cdc20a27a8fc6b37dca7a733ffba7159c9a4feb9e07e4658a04e66def29e3ee` |
| CP de locale CRT, DWORD `7d5d508` | 0; es un estado distinto de la página multibyte |
| Charset solicitado por el original, DWORD `6c76a0` | 134 |
| `IsValidCodePage(949)` en el probe | 1; disponible no significa seleccionada ni IME instalado |

Para este snapshot, `_mbclen` avanza uno con cualquier primer byte. No se afirma que toda
instalación o ejecución del cliente tenga esos mismos estados. No se cambió el locale del guest
ni se instaló un IME para producir estos resultados.

## Text.bmd no distingue Latin-1 de CP1252

Se copiaron del directorio del módulo realmente cargado en XP `Text.bmd`, `Filter.bmd` y
`FilterName.bmd`: sus hashes coinciden con los del repo. Text.bmd tiene 614.400 bytes y SHA-256
`5a596bebcc775d9d141310dd2a741b9053c616f6f5e11d78f23f485b5ab65cba`. Después de XOR3 `FC CF AB`,
su SHA-256 es `791c1189d94f813ba327c474a1959962b99c28a9ba5aab79ecd37680c8b3d417`.

Se reprodujeron los conteos de Q-C67: 2.048 registros, 1.645 no vacíos; 258 con bytes altos;
348 tramos altos de un byte y 22 de más de uno. Ninguno de esos 258 textos es UTF-8 válido;
7 son GB2312 válido, 19 CP949, y **258 tanto CP1252 como Latin-1**. No aparece ningún byte
`80..9f`: precisamente donde esas dos codificaciones difieren. Los registros 0 y 18, caras
de fuente, están vacíos. El registro 7 no tiene NUL dentro de sus 300 bytes.

Por lo tanto, este corpus no permite elegir Latin-1 frente a CP1252 ni demuestra que GDI
agrupe los bytes altos por haber solicitado charset 134. Las dos afirmaciones del research
anterior eran más fuertes que su evidencia. Macro.txt y los registros activos de los filtros
son ASCII y tampoco discriminan esas páginas.

Los literales coreanos de emotes son otra fuente de datos: los bytes en `6c90e0..6c912b`
contienen pares como `a4d0/a4cc/a4d1`, interpretables como los jamo descritos en CP949.
El SHA de esos 76 bytes es `9774980f882e296f70583569d780407d7dc263a48cc9458b7d581094bff6718c`.
Eso fija la interpretación de esos literales, no la página global del CRT ni la conversión
de los nombres, de Text.bmd o de la entrada del jugador.

## Entradas discriminantes en el EDIT original

Se pegó **CF_UNICODETEXT** sintético en Agregar, sin confirmar ni enviar el nombre. El
portapapeles previo se conservó opaco y se restauró; no se registró su contenido. Se leyeron
texto ANSI y selección del HWND conocido. El control tiene límite 10 y estilo `50000080`.

| Entrada | Bytes devueltos por el EDIT ANSI | Largo / caret |
| --- | --- | --- |
| `é` (U+00E9) | `e9` | 1 / 1 |
| `e` + U+0301 | `65 b4` | 2 / 2 |
| `€` (dentro de la prueba `abé€漢`) | `80` | Un byte |
| `漢` en esa misma prueba | `3f` | Un byte de sustitución |
| `Ａ`, U+FF21 | `41` | 1 / 1 |
| `Α`, U+0391 | `3f` | 1 / 1 |
| U+1F600, par UTF-16 `d83d de00` | **`3f 3f`** | 2 / 2 |
| `ab` + LF + `cd` | `61 62 0a 63 64` | 5 / 5 |
| `ab` + CRLF + `cd` | `61 62` | 2 / 2 |

No hubo normalización automática de `e` + acento a U+00E9. No corresponde eliminar silenciosamente
todo carácter no representable ni sustituir siempre un punto de código suplementario por un
único `?`. Tampoco pegar equivale al filtro de caracteres imprimibles que MuRet usa al teclear.
Estas observaciones prueban la ruta de pegado; todavía no prueban todas las rutas WM_CHAR/IME.

### Oráculo NLS independiente en XP

Un programa propio midió los 256 bytes de CP1252 y los **65.536 valores individuales de una
unidad UTF-16**, con flags 0 y WC_NO_BEST_FIT_CHARS: 131.072 filas. Esto incluye valores
surrogate aislados; no representa una enumeración de todos los caracteres Unicode ni todas
las secuencias. Hubo 441 diferencias de salida entre ambos flags. Con flags 0, 64.839 entradas
usaron el carácter predeterminado; con NO_BEST_FIT, 65.280. Diez secuencias adicionales se
midieron con ambos flags.

Flags 0 reprodujo los casos nativos anteriores: `e+0301 → 65 b4`, U+FF21 → `41`, U+0391 → `3f`
y el par del emoji → `3f 3f`. NO_BEST_FIT produjo `65 3f` y `3f` para los dos primeros. Este
acuerdo finito no demuestra todos los flags internos usados por USER32. Un conversor moderno
o su opción de transliteración no se considera equivalente por llamarse también «1252».
Microsoft documenta diferencias de conversión y de surrogates anteriores a Vista.
[WideCharToMultiByte](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte),
[MultiByteToWideChar](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar).

Huellas de las salidas completas, fuera de Git:

| Salida | SHA-256 |
| --- | --- |
| 256 decodificaciones | `f90d4725d34967819596b5562e997000af6e8609b9cabefbe5c5e247b6b77737` |
| 131.072 codificaciones | `572e7b7d87b2178b002e1c460783a7de1d040f45650139647f74c5185aabee95` |
| 20 secuencias | `35cea5cc2ce2a362c730427c971430b85e8b1794c920226b6ed165dd921b6773` |
| Probe de fuentes/glifos, segunda corrida | `db2af228bfb17327c5d2d8143ef637a9033c2c7ddf087fc0951ec5727a0f438d` |

## Fuente solicitada frente a fuente física

La prueba GDI propia repitió `CreateFontA` con altura 13, peso 400, calidad sin suavizado y
cara NULL, como la petición normal del original a 1024 píxeles. En este XP, los charsets
solicitados 0, 1, 134 y 129 dieron **Arial, tmCharSet=0 y página efectiva 1252**. Se comprobó
también cara vacía, con el mismo resultado. La petición GB2312 no basta para forzar una fuente
china disponible. GetTextCharsetInfo y TranslateCharsetInfo distinguen esos estados.
[Fuente lógica](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createfonta),
[charset seleccionado](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gettextcharsetinfo).

Con esa fuente propia, GetGlyphIndicesA para el byte `80` devolvió el mismo glifo que
GetGlyphIndicesW para U+20AC, mientras que U+0080 no tenía glifo. El ancho de `abcd` fue 23,
coincidente con el caret nativo 26 menos su margen 3. No se usaron handles GDI de otro proceso
como si fueran recursos propios. Esto respalda el perfil occidental CP1252; **no es una captura
del HFONT efectivo ni del bitmap/GL completo del juego**.

## Consecuencias para MuRet

- El wire y los activos se conservan como bytes. Decodificar para mostrar no cambia el contenido
  almacenado ni sus largos de red.
- D-C12 elegía Latin-1 basándose en un corpus que no la distingue de CP1252. El perfil occidental
  ahora necesita decodificar `80..9f` como Windows-1252 al pedir glifos a FreeType; `a0..ff` sigue
  igual. La decisión se actualiza en el ADR 0016, con regresión sintética discriminante.
- El encoder CP1252 y su best fit XP se usan en Agregar, mensaje (50 bytes), destinatario del
  susurro y nombre de personaje (10 bytes cada uno). `4691e0` deja el filtro `obj+4c` sin activar;
  el cero de Init no es un filtro. El nombre tampoco tiene filtro de entrada, aunque conserva
  sus comprobaciones posteriores. Login/códigos Ascii y el perfil Latin1 conservan sus contratos.
  No se descarta `€` ni se cambia el wire a UTF-8. Las regresiones comprueban también bytes emitidos
  de chat/susurro; no acreditan todas las rutas WM_CHAR/IME ni otros perfiles.
- La navegación, el clipboard y la composición del chat común todavía no se enrutan como en el
  EDIT viejo. Compartir el encoder no completa ese editor ni H10 (chats de amigos/cartas).
- SDL entrega texto UTF-8; sus offsets de composición están en caracteres UTF-8, con -1 para
  «no definido». No son offsets de bytes ANSI ni unidades UTF-16. La conversión debe conservar
  correspondencias y límites antes de tratar el buffer como el EDIT original.
  [SDL_TextEditingEvent](https://wiki.libsdl.org/SDL3/SDL_TextEditingEvent).
- El charset solicitado a GDI, CP del CRT, ACP de la UI y CP del portapapeles se registran por
  separado. CF_LOCALE puede intervenir en la conversión de CF_TEXT; el layout US de esta prueba
  no acredita un IME coreano. [Formatos del portapapeles](https://learn.microsoft.com/en-us/windows/win32/dataxchg/standard-clipboard-formats).

Siguen abiertos: entrada WM_CHAR discriminante frente a pegado, IME genuino y sus modos,
configuraciones DBCS, fuente/bitmap efectivo del proceso y correlación de bytes de texto en el
wire con servidor real. El emisor y el receptor no deben comenzar a tratar el protocolo antiguo
como UTF-8 por adoptar Unicode en la frontera SDL.


### Reconstrucción portable del perfil XP

El encoder propio reproduce las 65.536 conversiones individuales UTF-16 de
flags0: 256 correspondencias directas de la página y 441 aproximaciones best fit,
con `?` predeterminado. Una regresión recorre los 65.536 valores y contrasta la
huella FNV-1a64 `5bbef6816f8b8f6d` calculada del CSV nativo, además de vectores
discriminantes y el roundtrip de los 256 bytes. Los escalares suplementarios
entran como dos unidades surrogate y producen dos sustituciones. No se aplica
normalización ni transliteración del sistema moderno.

Para leer UTF-8 se reutiliza el validador ya existente de PathText, extraído a un
lector de un escalar. Una secuencia malformada rechaza la conversión completa
antes de tocar el campo. Se consultaron SDL_StepUTF8 (sólo en la capa SDL),
utf8cpp (sin puerto en el baseline), utf8proc y simdutf; sumar una dependencia
para este lector corto ya existente costaba más que reutilizarlo. El best fit
de XP queda como comportamiento reconstruido, no como promesa de las tablas de
una biblioteca Unicode actual.

Estos resultados no acreditan todavía todas las conversiones de secuencias
UTF-16, el teclado/IME del original ni perfiles DBCS. Las pruebas de pegado son
comparaciones nativas finitas; la tabla BMP es un oráculo NLS propio del mismo XP.
