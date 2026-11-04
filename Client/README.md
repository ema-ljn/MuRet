# Client

Reimplementación del `main.exe` de MU Online 1.02 (build `c32bbd5f…`): el programa del jugador.
Se conecta al ConnectServer, elige un GameServer, hace login y muestra la selección de personaje y
el mundo 3D. En C++20, para Windows y Linux, con SDL3 y OpenGL 3.3.

La regla: **modernizar sin sacrificar fidelidad**. Los bytes en la red, la lectura de los archivos
`Data\` del cliente original y la velocidad de la lógica son los del original; lo moderno es la
implementación y lo que el original no fija (por ejemplo, dibujar a más FPS entre dos ticks).

Qué hace el original, byte a byte, está en el [research](docs/research/README.md) (un archivo por
tema), reconstruido desde la carpeta `client-research/` de
[EmaNLJN/mu-server-repack-research-102i](https://github.com/EmaNLJN/mu-server-repack-research-102i).
Qué hace MuRet y en qué orden está en el
[roadmap del cliente](../docs/superpowers/specs/2026-10-30-cliente-design.md).

La `Data\` del cliente original está en [`assets/Data/`](assets/README.md), tal cual: es el
original que MuRet reimagina y la base de los tests. El cliente igual la lee de la carpeta que se
le pase (`--data` o `[Data] Path`).

## Estado

Ola CL1: `Main` abre una ventana SDL3 de 640×480 con OpenGL 3.3 core, la limpia de un color e
imprime `GL_VERSION` y `GL_RENDERER`; todavía no se conecta. `ClientLib` resuelve rutas de `Data\`
como Windows (sin distinguir mayúsculas) y lee desde sus bytes `Text.bmd`, `Gate.bmd`, `Item.bmd`
y `Skill.bmd`, con el XOR3 y el checksum de las tablas `.bmd`, y el terreno de un mundo:
`EncTerrain<N>.map` (las capas de tiles), `EncTerrain<N>.att` (los atributos) y `EncTerrain<N>.obj`
(los objetos), con su cifrado K16, `TerrainHeight.OZB` (las alturas) y `TerrainLight.OZJ` (la luz).
De las texturas, el OZT (un TGA de 32 bits, en RGBA) y el OZJ (un JPEG, en RGB, con libjpeg-turbo),
en un buffer de lados potencia de 2 como el del original. Con eso termina el código de la CL1:
`message.wtf` no entra, porque es del launcher y `main.exe` no lo lee. La próxima es la CL2, el
protocolo sin red, con su [plan](../docs/superpowers/plans/2026-11-02-ola-cl2-protocolo-sin-red.md).

Desde la CL2.5, `Protocol/Connection` lee y escribe los paquetes del login, campo por campo y sin
red: pedir la lista de servidores (`F4 02`), leerla (`F4 06`), elegir uno (`F4 03`) y leer su
dirección, el saludo del GameServer (`F1 00`), el login (`F1 01`) y su respuesta, el aviso del
checksum malo (`F1 03`) y el keepalive (`0E`). Lo que sale es un `Outgoing`: el paquete, con el
XOR32 si lo lleva, y si va en claro o como C3. La versión y el serial del build están en
`Protocol/BuildConstants.h`, y el XOR3 pasó a `Core`, porque lo usan las tablas y el login.

Desde la CL3.1, la ventana y el contexto GL salieron de `main.cpp` a `Platform/`: `SdlContext`
(el `SDL_Init` y el `SDL_Quit`), `Window` (la ventana con su contexto 3.3 core, la tabla de GL de
`GlLoader` y el VSync de `SwapInterval`) y `PumpEvents`, que pasa los eventos de SDL a
`App::WindowState`. `Platform/` es el target `ClientPlatform`, el único que enlaza SDL3: `ClientLib`
no la ve, y sus tests corren sin ventana. `main.cpp` queda en el armado, por fases, como los
pasos de `WinMain` en MuMain: la config antes de `SDL_Init`, el log, la ventana y el bucle. La
escena de prueba (`Scenes/TestScene`) y el medidor de `--stats` (`App/FrameRateMeter`) tienen su
archivo y sus tests.

Desde la CL4.5, con un host, `Main` se conecta: la `ServerSession` va del ConnectServer al login
aceptado en el GameServer y después manda el keepalive cada 20 s, consultada por el bucle en cada
frame. En la CL4 la cuenta salía del entorno y se elegía el primer servidor de la lista; desde la
CL5.4, la sesión la empieza la escena de login al entrar, y desde la CL5.5 espera al jugador.

Desde la CL5.1, lo que dibuja con OpenGL es el target `ClientRender` (`src/Render/Gl/`), el único
con GL además de `ClientPlatform`: `ClientLib` sigue sin verlo. Trae el shader del 2D, `GlTexture`
(sube una textura de la CL1 con su filtro y su wrap), un framebuffer para leer lo dibujado y
`GlRenderer`, que dibuja triángulos en píxeles con la y para arriba. La tabla de GL es la X-macro de
`Platform/GlFunctionList.h`: una función nueva es una línea ahí.

Las texturas de una escena son un juego, un dato (`Scenes/TextureSets.h`, el del login y el de la
carga): `Data::Textures::LoadTextureSet` las busca en `Data\` como el original (el nombre hasta el
primer punto y `.OZJ` u `.OZT`) y las lee, y `Gl::TextureTable` las sube a la tabla de la escena,
por el slot del original.

Desde la CL5.3, el texto (`Render/Text/`): las cuatro fuentes de `WinMain` con su alto por el
ancho de la pantalla (`FontSet`), los cambios de color `0x02` del chat (`InlineColors`), dónde va un
texto y cómo se corta en cajas de 256 columnas (`TextLayout`), y `FreeTypeRasterizer`, que mide y
dibuja con FreeType en un bit, detrás de `IGlyphRasterizer`. `TextCache` rasteriza cada texto una
vez por fuente y colores, guarda sus cajas de 256 × 32 en los slots del texto de la tabla (después
de los 1.845 del original) y las dibuja con el `SpriteBatch`. La fuente sale de `[Video] FontFile`;
sin la clave, `App::ChooseFontFiles` busca la cara del original en la lista de fuentes de Windows
(`Platform/SystemFonts.h`), y en Linux es un error que nombra la clave. En el `Text.bmd` del
laboratorio la cara está vacía: con esos datos, también en Windows hace falta `FontFile`. El
cliente dibuja texto desde la CL5.4, que conecta todo esto en `main.cpp`.

Desde la CL5.4, con una carpeta de datos (`--data` o `[Data] Path`), `Main` corre las escenas del
original en un `Scenes::SceneManager`, con la tabla de transiciones de `DAT_006d5dc8`: la carga
(`LoadingScene`, con su UI de 800 × 600 y la barra), el login (`LoginScene`: los fondos, la arena
y los tres textos de abajo, con la versión de MuRet) y una selección vacía, adonde va el login
aceptado. Antes de la ventana lee `Text.bmd` y elige la fuente, como `WinMain`. El mundo detrás
del login es un dato de la escena (el mapa 55, `World56`), que dibuja la CL7; hasta entonces, ahí
queda el negro. Sin carpeta de datos, `Main` sigue con la escena de prueba, como en CI.

Desde la CL5.5, el login tiene su UI (`UI/`): la lista de servidores cuando llega, la ventana de la
cuenta y la clave cuando el GameServer saluda, y los avisos de `Text.bmd`, cada uno con su OK. La
`ServerSession` espera al jugador en dos estados, `ChoosingServer` y `WaitingCredentials`, y la
escena abre la ventana que va cuando el estado cambia. Las ventanas se atienden en el paso, con el
mouse en píxeles del framebuffer y el texto tecleado de SDL, que `Main` prende solo mientras está la
ventana de la cuenta. Un login rechazado muestra su aviso y vuelve a la cuenta, por la misma
conexión; el OK del aviso de una conexión que falló cierra el juego, como el original (Q-C69). Para
depurar la red sin la UI, `--auto-login` elige el primero de la lista y entra con la cuenta del
entorno, como en la CL4.

Desde la CL6.3, el login aceptado lleva a la selección de personaje del original. Los modelos
`.bmd` se leen en `Data/Models/` (CL6.1). La pose, el skinning, el avance de la animación y los
pases de brillo se calculan en la CPU, en `Render/Models/` (CL6.2), y se comparan vértice por
vértice con las trazas de GL. La selección pide la lista (`F3 00`) y pone a cada personaje en su
lugar, animado, con su clase y su equipo (`GameLogic/CharacterAppearance`). Lleva el contorno el
que está bajo el mouse; un clic elige uno, y Enter o el doble clic entran. La carga del mundo
dibuja su frame, y la escena del juego manda el `F3 03`. El terreno detrás de la selección y el
mundo son de la CL7: hasta entonces, el juego queda en negro.

Desde la CL6.4, el login dibuja en su pase 3D el logo (`Logo03.bmd`, un objeto del mundo) y el
personaje de la clase 4 montado en el caballo oscuro, con la luz del terreno del mapa 55 en su lugar
(`World/TerrainLighting`), como el original. El logo se aclara en los primeros 40 pasos, el
personaje hace uno de sus dos gestos cada tanto (`Scenes/LoginModels`) y el caballo aparece de a
poco y lo sigue (`Scenes/DarkHorse`).

Desde la CL6.5, la selección tiene la barra de abajo del original (`UI/SelectionBar`): conectar
entra como Enter, y borrar pide la confirmación y el código y manda el `F3 02`; con la respuesta, el
personaje se va de la selección o sale el aviso que diga el GameServer. Crear todavía no abre su
ventana (es de la PR que sigue) y el botón del menú no hace nada (CL8).

## Auditoría y mantenimiento

La [auditoría de C++20 y contrato 1.02i del 2026-11-03](../docs/audits/2026-11-03-cliente-cpp20.md)
registra los hallazgos, sus reproducciones y los límites de verificación. El
[plan de seguimiento](../docs/superpowers/plans/2026-11-03-seguimiento-auditoria-cliente.md)
los separa en tareas y PR por concepto de CL3.4 y CL5.1; las correcciones siguen pendientes.

## Compilar y correr

Hace falta vcpkg (las dependencias, SDL3, libjpeg-turbo, glm, FreeType y GoogleTest, están en
`vcpkg.json` y se bajan solas al configurar) y la variable `VCPKG_ROOT` apuntando a su clon; ver
el [README raíz](../README.md#compilar).
Desde la raíz del repo:

```bash
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
./build/linux-x64-debug/Client/Main              # corre hasta que se cierre la ventana
./build/linux-x64-debug/Client/Main --frames 60  # dibuja 60 frames y sale
./build/linux-x64-debug/Client/Main --vsync off --stats  # sin VSync: FPS y pasos por segundo cada 5 s
./build/linux-x64-debug/Client/Main --vsync off --max-fps 144 --stats  # sin VSync, hasta 144 FPS
```

Al salir, `Main` imprime los frames y los pasos que corrió. La lógica va a 25 pasos por segundo con
cualquier tasa de frames (CL3.2). En Linux con Xvfb (sin retrazo, así que VSync no frena), `--stats`
da cientos de FPS y 25 pasos por segundo; con `--max-fps N`, hasta N FPS. Sin VSync y sin tope, el
bucle no duerme y usa un núcleo entero: `--max-fps` (o `[Video] MaxFps`) le pone un tope, como el
`FpsCap` de MuMain, que tampoco topea por defecto y también va solo con VSync apagado. Con VSync
el tope no se usa (el log lo dice): el swap ya frena al refresco, y un tope que no lo divide
dibujaría a saltos. Con `--pacing faithful` (el ritmo del original, VSync apagado), da 21,33 FPS y
25 pasos por segundo, y no lleva tope. En Windows queda por probar a mano (Emanuel).

En Windows: `cmake --preset windows-x64`, `cmake --build --preset windows-x64-debug` y
`build\windows-x64\Client\Debug\Main.exe`.

Los headers de la biblioteca estándar (y GoogleTest en los tests) se precompilan
(`cmake/PrecompiledHeaders.cmake`): un build limpio usa como un cuarto menos de CPU. Para compilar
como el job de Linux del CI, que verifica que cada archivo incluya lo que usa, configurá con
`-DMURET_PRECOMPILED_HEADERS=OFF`. Ese job corre además `misc-include-cleaner` de clang-tidy 18
sobre `Client/`; cómo correrlo a mano lo dice la regla 7 de
[`docs/CODING_RULES.md`](../docs/CODING_RULES.md).

## Correr y configurar `Main`

Lo que depende del lugar (el servidor, la carpeta de datos, el video) sale de la línea de comandos
o de `muret-client.ini`: la línea de comandos pisa la config, y la config, los valores por
defecto. Todo se valida antes de abrir la ventana: un error termina `Main` con un mensaje que
empieza con `[error]` y nombra la opción, la clave o la ruta, sin abrir nada. Sin argumentos ni
config, `Main` corre con los valores por defecto, como en CI.

| Opción | Qué hace |
| --- | --- |
| `--auto-login` | con un servidor, elige el primero de la lista y entra con la cuenta y la clave del entorno, sin la UI (la sección de abajo). Sin un servidor, de `/u /p` o de `[Server] Host`, es un error |
| `connect /u<host> /p<puerto>` | el servidor, como lo pasa el launcher. `connect` es opcional, las letras van en mayúscula o minúscula y en cualquier orden. El valor va pegado a la letra. El puerto va de 1 a 65535, y el host, de 1 a 255 bytes de ASCII visible: una IP o un nombre, que con acentos va en punycode (Q-C37). Uno sin el otro, o uno repetido, es un error (Q-C36), y `/y` y `/z`, también |
| `--config <ruta>` | lee esa config en vez de `muret-client.ini`; si no existe, es un error que la nombra |
| `--data <ruta>` | la carpeta de datos del cliente original, en vez de `[Data] Path`; si no existe o no es una carpeta, es un error que la nombra (Q-C35) |
| `--max-fps <N>` | el tope de FPS del modo normal con VSync apagado, en vez de `[Video] MaxFps`: 0 (sin tope), o de 25 (un frame por paso de la lógica) a 1000. Después de dibujar, `Main` duerme hasta la meta del frame: 1 s / N después de la del anterior, o de donde terminó el anterior si se pasó. Así lo que corre entre frames no se suma, y el tope nunca da menos FPS que no tenerlo. Con VSync no se usa. Con `--pacing faithful`, un tope que no es 0 es un error que nombra `--pacing faithful` y `--max-fps`; `--max-fps 0` vale, y saca un `MaxFps` de la config |

Las rutas de `--config` y `--data` se toman desde la carpeta actual, como cualquier argumento.
En Windows llegan en UTF-8, como las de la config, así que andan aunque tengan letras que no están
en la página ANSI; en Linux van tal cual las da el sistema, también un nombre que no es UTF-8.
Cada opción va una vez: una repetida es un error. Dos límites de Windows:

- Una `\` justo antes de una comilla cuenta como una comilla de verdad, y la parte entre comillas
  sigue abierta: `--data "C:\MU\Data\" --frames 60` llega como un solo argumento,
  `C:\MU\Data" --frames 60`, y las opciones que siguen se pierden. Va `"C:\MU\Data\\"` (dos `\`)
  o `"C:\MU\Data"`, sin la última.
- Un nombre con una mitad de UTF-16 suelta (NTFS lo deja, pero no es texto) llega con esa mitad
  cambiada por U+FFFD, porque SDL la cambia al pasar a UTF-8: el error dice que la ruta no existe.

Por ejemplo:

```bash
./build/linux-x64-debug/Client/Main connect /u127.0.0.1 /p44405 --data /ruta/a/Data
./build/linux-x64-debug/Client/Main --config ~/mu/otra.ini
```

Sin host no es un error: `Main` no mira el entorno ni las claves, y corre sin un servidor, como en
CI.

### Con la carpeta de datos: la carga y el login

Con `--data` (o `[Data] Path`), `Main` lee `Local\Text.bmd` y elige la fuente antes de abrir la
ventana, y corre la carga y el login con las texturas de esa carpeta. Sin `Text.bmd`, o sin una
fuente, termina con un error que lo dice. La fuente va en `[Video] FontFile` de
`muret-client.ini`: en Linux siempre, y en Windows también con la de `assets/Data/`, cuya cara
está vacía. Por ejemplo, en Linux:

```ini
[Video]
FontFile = /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

```bash
./build/linux-x64-debug/Client/Main --data /ruta/a/Data
```

Se ve la carga (cinco imágenes y la barra, que avanza una vez por paso, así que dura menos de
medio segundo) y el login: los fondos, la arena que corre y los tres textos de abajo, con la
versión de MuRet en vez de la del original. Una textura que falta corta `Main` con su nombre. Con
un host, además, la escena de login conecta al entrar y muestra su UI: la lista de servidores, un
clic en un grupo y otro en un servidor, y la ventana de la cuenta y la clave (Tab cambia de campo
y Enter manda). Con el login aceptado pasa a la selección, que pide la lista de personajes y los
muestra en sus lugares (CL6.3).

### Con un servidor: la cuenta y las claves

Con un host, de `/u` o de `[Server] Host`, `Main` necesita antes de abrir la ventana las claves
de la conexión y, con `--auto-login`, antes que ellas, la cuenta y la clave del entorno:

1. Solo con `--auto-login`: la cuenta y la clave del login, en `MURET_CLIENT_ACCOUNT` y
   `MURET_CLIENT_PASSWORD`. Sin `--auto-login` se teclean en la ventana de la cuenta y el entorno
   no se mira. No van en la config, que es un archivo, ni en la línea de comandos, que queda en el
   historial y en `ps`. Cada una va de 1 a 10 bytes, el campo del login: si falta, está vacía o
   pasa de 10, el error nombra la variable y nunca trae su valor. Se cargan sin que se vean en la
   pantalla:

   ```bash
   IFS= read -rs -p "cuenta: " MURET_CLIENT_ACCOUNT; echo
   IFS= read -rs -p "clave: " MURET_CLIENT_PASSWORD; echo
   export MURET_CLIENT_ACCOUNT MURET_CLIENT_PASSWORD
   ```

   ```powershell
   # -MaskInput es de PowerShell 7.1 o más nuevo.
   $env:MURET_CLIENT_ACCOUNT = Read-Host "cuenta" -MaskInput
   $env:MURET_CLIENT_PASSWORD = Read-Host "clave" -MaskInput
   ```

   Con `IFS=`, `read` no les saca los espacios del principio ni del final. Al terminar, la clave
   se borra del entorno de la consola: `unset MURET_CLIENT_PASSWORD` en bash, o
   `Remove-Item Env:MURET_CLIENT_PASSWORD` en PowerShell.

2. Las claves de la conexión con el GameServer, `Enc1.dat` y `Dec2.dat`, de la carpeta de datos
   (`--data` o `[Data] Path`), sin mirar mayúsculas. Sin carpeta, o si alguna no está o no carga,
   el error nombra las dos y dice qué pasó con cada una. No se versionan (D-C7).

Con eso, `Main` arranca la red del sistema y la sesión, y el log dice cada paso: "se conecta al
ConnectServer", "conectó con el ConnectServer", "la lista trae N servidores: se espera la
elección", "se elige un servidor: se pide su dirección", la dirección del GameServer (sin
decirla), "el GameServer saludó: se espera la cuenta y la clave", "se manda el login", "el
GameServer aceptó el login" y, desde ahí, "se mandó el keepalive al GameServer" cada 20 s. La
lista y la cuenta esperan al jugador sin tope; las respuestas de los servidores, 30 s. Ni la
cuenta ni la clave ni las direcciones van al log, tampoco lo tecleado. Un login rechazado muestra
su aviso y, con su OK, la cuenta otra vez, con el foco donde va. Si la conexión falla (un servidor
que rechaza, otra versión, una conexión que se pierde, o 30 s sin respuesta), el log dice qué y
la escena muestra el aviso de la conexión: su OK cierra el juego, como el del original (Q-C69). La
sesión no se reconecta sola. Con un servidor ocupado, la lista vuelve a pedirse y se elige otro
(Q-C71); cancelar en la cuenta vuelve a la lista.

### Contra el servidor del laboratorio (a mano)

CI no tiene la VM del laboratorio, así que el login de punta a punta se prueba a mano. Son el paso
4 de la CL4.5, en el
[plan de la CL4](../docs/superpowers/plans/2026-11-02-ola-cl4-red-del-cliente.md), y el paso 10
de la CL6.3, en el
[plan de la CL6](../docs/superpowers/plans/2026-11-03-ola-cl6-modelos-y-seleccion.md). Lo que
falta correr está en P-C21 del [08](docs/research/08-pendientes.md).

1. Levantar la VM del laboratorio con el ConnectServer y el GameServer andando. La cuenta de
   prueba necesita personajes: MuRet todavía no los crea (CL6.5), así que se crean antes con el
   cliente original, de clases distintas y con algo de equipo.
2. Correr, con la IP del ConnectServer de la VM y la carpeta `Client/assets/Data`, que trae
   `Enc1.dat` y `Dec2.dat`:

   ```bash
   ./build/linux-x64-debug/Client/Main connect /u<ip del CS> /p44405 --data Client/assets/Data
   ```

   En Windows, lo mismo con `build\windows-x64\Client\Debug\Main.exe`, o con `[Server] Host`,
   `[Server] Port` y `[Data] Path` en el `muret-client.ini` junto al ejecutable. Desde la CL5.4,
   con la carpeta de datos corren la carga y el login, así que la config necesita
   `[Video] FontFile` (la sección de arriba), también en Windows.

3. En la lista, un clic en el grupo y otro en el servidor. En la ventana de la cuenta, teclear
   una cuenta del laboratorio con una clave mala y Enter: sale el aviso de la clave, y su OK
   vuelve a la cuenta con el foco en la clave. Teclear la buena y Enter: el log dice "el
   GameServer aceptó el login", "se pide la lista de personajes", "la lista trae N personajes: se
   espera la elección" y "la selección tiene N personajes". Con `--auto-login` y la cuenta en el
   entorno (la sección de arriba), lo mismo sin teclear, salvo la clave mala.
4. En la selección: cada personaje de la cuenta en su lugar, con su clase y su equipo, animado y
   parado; el que está bajo el mouse con el contorno; un clic lo elige (se ve más claro) y un clic
   en el vacío lo suelta.
5. Con uno elegido, Enter: el log dice "se elige el personaje del lugar X para entrar al mundo" y
   "se entra al mundo con el personaje del lugar X", se ve un frame de la carga del mundo (las
   cuatro `LSBg`) y después el negro del juego. El log del GameServer muestra que ese personaje
   entró. Cerrar y repetirlo con doble clic en vez de Enter, y con el botón de conectar de la
   barra.
6. Mirar el log durante dos minutos: desde "el GameServer aceptó el login", seis o siete "se mandó
   el keepalive al GameServer", uno cada 20 s, sin "se perdió la conexión con el GameServer". Con
   `[Server] Obfuscation = foxanti`, que es el valor por defecto: el GameServer del laboratorio
   usa FoxAnti.
7. Cancelar: en otra corrida, en la ventana de la cuenta, el botón de cancelar (o Esc). El
   original cierra el GameServer, vuelve a conectar al ConnectServer y muestra la lista otra vez
   (06 § La cuenta y la clave). Anotar si MuRet vuelve a la lista o se cierra, y si antes sale un aviso.
   Hacer lo mismo con el cliente original en la VM.
8. Cerrar la ventana y, si se usó `--auto-login`, borrar la clave del entorno
   (`unset MURET_CLIENT_PASSWORD`).

El log (`muret-client.log`, junto al ejecutable) no trae la cuenta, la clave ni las direcciones:
se puede compartir. Las capturas de pantalla quedan fuera de Git.

Si se captura el cable para comparar el login y el keepalive con los de la sesión real del 04
(P-C05), la captura queda fuera de Git, porque se descifra con las claves reales, y la comparación
se mira sin imprimir la cuenta. Acá se anota solo el resultado: todavía no se corrió. Sin la VM, la
UI de la CL5.5 se probó contra un ConnectServer y un GameServer falsos en 127.0.0.1 (la lista, la
elección, la cuenta, un login rechazado, cancelar y el aviso que cierra el juego), con las
capturas fuera de Git.

### `muret-client.ini`

`Main` lo busca junto al ejecutable (la carpeta que da `SDL_GetBasePath`), no en la carpeta
actual. Si no está, usa los valores por defecto; con `--config`, el archivo tiene que existir. Una
carpeta en su lugar, un archivo de más de 64 KiB o uno que no se puede leer son errores.
Todas las claves son opcionales:

```ini
[Server]
Host = 127.0.0.1         ; de 1 a 255 bytes, sin espacios
Port = 44405             ; de 1 a 65535
Obfuscation = foxanti    ; foxanti o none

[Data]
Path = C:\MU\Data        ; la carpeta de datos del cliente original

[Video]
Width = 640
Height = 480
VSync = on               ; on, adaptive u off
Pacing = decoupled       ; decoupled o faithful
MaxFps = 0               ; tope de FPS con VSync off: 0 (sin tope), o de 25 a 1000
FontFile = letra.ttf     ; la fuente TrueType del texto
BoldFontFile = negrita.ttf ; su negrita (sin darla, se ensancha la de FontFile)

[Log]
Path = muret-client.log
```

| Clave | Por defecto | Qué es |
| --- | --- | --- |
| `[Server] Host` | ninguno | el host del ConnectServer; lo pide la red (CL4) |
| `[Server] Port` | 44405 | el puerto del ConnectServer, el compilado en el original (`0x6d5dc4`) |
| `[Server] Obfuscation` | `foxanti` | la forma de los paquetes que espera el servidor: `foxanti`, la capa de FoxAnti del servidor del laboratorio, o `none`, un servidor que habla como `main.exe`. `/u /p` no la cambia |
| `[Data] Path` | ninguna | la carpeta `Data\` de una instalación del cliente original |
| `[Video] Width`, `Height` | 640, 480 | el tamaño de la ventana, que no cambia. Tiene que ser una de las cinco del original (640 × 480, 800 × 600, 1024 × 768, 1280 × 1024 y 1600 × 1200) o un modo del monitor, el del escritorio o uno de pantalla completa; si no, `Main` no abre la ventana y el log dice cuáles valen |
| `[Video] VSync` | `on`, y `off` en el modo fiel | como `--vsync`. Con `Pacing = faithful` (o `--pacing faithful`), `on` o `adaptive` es un error que nombra `Pacing` y `VSync` |
| `[Video] Pacing` | `decoupled` | como `--pacing` |
| `[Video] MaxFps` | 0 (sin tope) | como `--max-fps`. Con `Pacing = faithful`, un tope que no es 0 es un error que nombra `Pacing` y `MaxFps` |
| `[Video] FontFile` | ninguna | la fuente TrueType (o una colección TTC: su primera cara) de todo el texto. El original pide la suya por nombre a Windows; MuRet necesita un archivo. Las cuatro fuentes del original salen de esta, cada una con su alto (`Render/Text/FontSet.h`). Sin la clave, en Windows se busca la cara del original en la lista de fuentes del sistema; en Linux, o si la cara está vacía (como en el `Text.bmd` del laboratorio), es un error |
| `[Video] BoldFontFile` | ninguna | la de la negrita. Sin darla, la negrita es la de `FontFile` ensanchada un píxel, como la que simula Windows |
| `[Log] Path` | `muret-client.log` junto al ejecutable | el archivo del log |

Una ruta relativa de la config (`[Data] Path`, `[Log] Path` y las dos fuentes) se toma desde la
carpeta del ejecutable, no desde la actual; que una fuente exista lo mira FreeType al abrirla. Un
par de comillas dobles alrededor de una ruta o del host se saca (lo que da "Copiar como ruta" del
Explorador). El archivo es UTF-8, con o sin BOM, y con fin de línea `\n` o `\r\n`.

Las secciones, las claves y los valores de `Obfuscation`, `VSync` y `Pacing` no distinguen
mayúsculas. Un `;` al principio de la línea, o después de un espacio, empieza un comentario; `#`
no. Son errores, y el mensaje dice el archivo y la línea (`muret-client.ini, línea 3: ...`):

- una línea que no es `[Sección]` ni `Clave = valor`, o con texto después del `]`;
- una sección o una clave que no existen, una clave antes de la primera sección, repetida o sin
  valor;
- un valor que no se puede leer (un puerto de 0, un `VSync = sometimes`, un `Width = 480px`);
- una ruta que no es UTF-8 o que tiene caracteres de control, un host que no es ASCII visible, y
  un `\r` sin `\n`.

### El log

`Main` escribe el log en `stderr` y en el archivo de `[Log] Path`, por defecto `muret-client.log`
junto al ejecutable (la carpeta de `SDL_GetBasePath`). Los errores de la línea de comandos y de la
config, que llegan antes de saber qué archivo es, van solo a `stderr`. Cada línea empieza con su
nivel: `[debug]`, `[info]`, `[warning]` o `[error]`; el mínimo es uno solo, `info`, para las dos
salidas. Los mensajes de SDL entran al mismo log, con `SDL: ` adelante y su prioridad pasada a uno
de esos cuatro. Si el archivo no se puede abrir, el log va solo a `stderr`, con un aviso que nombra
la ruta. Si después una escritura falla (un disco lleno), sale una sola vez a `stderr` un
`[warning]` que nombra la ruta, el archivo se cierra y desde ahí el log sigue solo en `stderr`. El
archivo no se empieza de nuevo: cada corrida agrega al final, desde una línea `[info] MuRet Client
arranca`, como el `MuError.log` del original, así el log de una caída sigue ahí al volver a abrir, y
dos clientes abiertos a la vez escriben los dos sin pisarse. Para que no crezca sin fin, al abrir se
vacía si ya pasa de 1 MiB (el original corta el principio, Q-C48). Si un segundo cliente lo abre
cuando ya pasó el tope, lo vacía también para el primero, que pierde lo que había escrito antes; lo
que escribe después sí queda.

Lo que conviene buscar ahí: el aviso de un frame de más de 1 s, que cuenta 1 s (un breakpoint, o
una ventana arrastrada en Windows, Q-C44), y que empieza con `[warning] un frame pasó de 1000 ms`;
y los errores del arranque.

## Tests

```bash
ctest --preset linux-x64-debug     # en Windows: ctest --preset windows-x64-debug
```

Cada `tests/<módulo>/test_<algo>.cpp` es su propio ejecutable (`test_packet_reader`), como en
MuMain, y cada `TEST` es un test de CTest con el nombre de su suite: `ctest -R PacketReader` corre
los del lector. Lo que comparten está en `tests/support/` (la biblioteca `ClientTestSupport`).

Los tests golden leen archivos reales del cliente original: los de `Client/assets/Data/`, sin
nada que configurar, también en CI. Para probar con otra copia de `Data\` (`Enc1.dat`, `Player\`,
`World1\`…), `MURET_CLIENT_GOLDEN_DIR` apunta a ella, al correr o al configurar:

```bash
MURET_CLIENT_GOLDEN_DIR=/ruta/a/Data ctest --preset linux-x64-debug
cmake --preset linux-x64-debug -DMURET_CLIENT_GOLDEN_DIR=/ruta/a/Data   # queda en la caché
```

Si la carpeta no existe, o le falta el archivo que el test pide, fallan.

Los OZJ, además, se comparan con lo que da la IJG libjpeg 6b del original, que no está en CI: el
tamaño y un hash de cada uno están en `tests/vectors/jpeg/lab-6b.txt`, y cómo se sacan, en el
README de esa carpeta. Es lo que hay que correr al cambiar de versión de libjpeg-turbo.

Algunos además comparan con la memoria del `main.exe` original, con los archivos ya cargados: los
volcados de `client-research/mu-golden/memoria-cliente`, en el repo de research (fuera de Git).
La variable apunta a esa carpeta, la que tiene una carpeta por captura (`2026-10-09-devias`,
`2026-10-09-lorencia` y `2026-10-09-lorencia-obj`). Necesitan las dos carpetas, con la misma regla,
y la de los volcados va solo en el entorno:

```bash
MURET_CLIENT_GOLDEN_DIR=/ruta/a/Data MURET_CLIENT_MEMORY_DIR=/ruta/a/memoria-cliente ctest --preset linux-x64-debug
```

Los de la red leen el cable del laboratorio: el `muproxy.log` que su proxy escribió entre el
cliente original y los servidores, en `analysis/mainexe/evidence/` de `mu-online-reverse-lab`.
Lleva el login del jugador, así que queda fuera de Git, y los tests cuentan paquetes y no imprimen
un byte del cable, ni al fallar. `MURET_CLIENT_WIRE_DIR` apunta a la carpeta que lo tiene, con la
misma regla, solo en el entorno, y no necesita las otras dos.

Desde la CL2.3, la carpeta del cable también tiene las claves del servidor con el que se capturó,
`Dec1.dat` y `Enc2.dat`, de `gameserver/evidence/keyfiles/` en `mu-server-repack-research-102i`:
con ellas el servidor descifra lo que manda el cliente y cifra lo que le contesta. Como `evidence/`
no las tiene, la carpeta se arma aparte, con enlaces a los tres archivos:

```bash
mkdir -p /ruta/a/cable
ln -s /ruta/a/mu-online-reverse-lab/analysis/mainexe/evidence/muproxy.log /ruta/a/cable/
ln -s /ruta/a/mu-server-repack-research-102i/gameserver/evidence/keyfiles/Dec1.dat /ruta/a/cable/
ln -s /ruta/a/mu-server-repack-research-102i/gameserver/evidence/keyfiles/Enc2.dat /ruta/a/cable/
MURET_CLIENT_WIRE_DIR=/ruta/a/cable ctest --preset linux-x64-debug
```

Los tests que comparan estas claves con las del cliente, `Enc1.dat` y `Dec2.dat`, necesitan
también la carpeta golden, y no muestran un valor de una clave, ni al fallar.

Los del login (`GoldenLogin`, desde la CL2.5) usan solo la carpeta del cable, con `Dec1.dat`:
eligen del cable los login, los keepalive, los saludos y las respuestas, y miran cabeceras,
tamaños, opcodes y la versión. De la cuenta, la clave y el serial no miran nada.

Los de `tests/render_gl/` (desde la CL5.1) abren una ventana escondida con un contexto 3.3 core
(`tests/support/GlTestContext.h`), dibujan en un framebuffer de 64 × 64 y leen los píxeles. Llevan
la etiqueta `gl`. Sin pantalla o sin OpenGL 3.3 se saltean, y con `MURET_CLIENT_REQUIRE_GL=1`
fallan con el motivo: así los corre el CI, en Xvfb, junto con los del lab de la CL5.0:

```bash
MURET_CLIENT_REQUIRE_GL=1 xvfb-run -a ctest --preset linux-x64-debug -L '^gl$' --no-tests=error
```

Los de FreeType (`FreeTypeRasterizerTest`, desde la CL5.3) necesitan una fuente TrueType, que
MuRet no versiona: `MURET_CLIENT_FONT_FILE` apunta al archivo, con la regla de las carpetas (sin
la variable se saltean; si no es un archivo, o FreeType no lo abre, fallan). No comparan con GDI:
miden y dibujan con esa fuente y miran que no haya suavizado, el alto de la celda y los avances.
En CI es una fuente del runner:

```bash
export MURET_CLIENT_FONT_FILE=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
ctest --preset linux-x64-debug -R FreeType
```

## Estructura (a medida que avanzan las olas)

```
Client/
  CMakeLists.txt
  src/
    main.cpp      arma las capas y arranca; sin lógica
    App/          línea de comandos, estado de la ventana, bucle con tick fijo (CL3); la sesión
                  con los servidores (CL4) y el --auto-login (CL5.5)
    Core/         Expected, ByteReader, Xor3; config, log, reloj (CL1+)
    Platform/     ventana, contexto GL, input: lo único que ve SDL3 (ClientPlatform, CL3)
    Data/         tablas .bmd, Text.bmd, terreno, OZJ/OZT, modelos: bytes a tipos, sin GL (CL1, CL6)
    Network/      socket, paquetes, C3/C4, FoxAnti      (CL2, CL4)
    Protocol/     opcodes, structs (el login, en Connection/), dispatcher (CL2, CL4, CL9); el
                  pedido de la mascota (A9), en Pet/ (CL4)
    Render/       OpenGL 3.3: texturas, sprites, texto  (CL5+); lo que llama a GL, en Gl/
                  (ClientRender, CL5.1); la pose, el skinning y el brillo de los modelos, en
                  Models/, y el pase 3D (CL6)
    Scenes/       la escena de prueba (CL3); el SceneManager, la carga, el login (CL5);
                  la selección y el juego (CL6, CL7)
    World/        mapa, cámara, objetos                 (CL7)
    GameLogic/    la apariencia del personaje (CL6); entidades, items, chat (CL7+)
    UI/           widgets, la lista, la cuenta y los avisos del login (CL5.5); HUD (CL8)
    Audio/        música y efectos                      (CL10)
  tests/          GoogleTest                            (CL0.2+)
  assets/         la Data\ del original, tal cual, y su manifiesto de hashes
  docs/research/  qué hace el original, byte a byte (01..09)
```

## Créditos

El texto se dibuja con [FreeType](https://freetype.org), que se usa bajo la FreeType License (FTL),
y la FTL pide decirlo así en la documentación:

> Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All
> rights reserved.
