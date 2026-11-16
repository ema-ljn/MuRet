# El EDIT de Windows: contrato y contraste de H9

Barrido del 2026-11-15. Las direcciones son **VA** del `main.exe` x86 PE32 de SHA-256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`, base preferida
`0x00400000`. El ASM prevalece sobre el pseudocódigo. Cada función tuvo un analista propio;
los lectores del proceso y las entradas sintéticas se ejecutaron únicamente en VMXP.

Este documento amplía [06](06-render-escenas-y-ui.md) y mantiene abierto P-C181. Distingue el
contrato estático, las observaciones del original y lo que todavía necesita comparación con MuRet.

## Funciones y dependencias

| VA | Contrato estático | Implementación relacionada | Límite de la evidencia |
| --- | --- | --- | --- |
| `4674c0` | Constructor: inicializa campos, colores blanco/negro/gris, posición candidata `(193,422)` y tamaño `(180,14)`; no crea HWND | `UI/OldControls.h` | No demuestra configuración del campo concreto |
| `467fe0` | `CreateWindowExA("edit")`, hijo del juego; límite por virtual `+3c` antes de subclasificar; DIB de 24 bits; al finalizar con éxito lo oculta | `UI/OldControls.h` | La visibilidad posterior depende de sus callers |
| `467f30` | `SendMessageA(hwnd, EM_SETLIMITTEXT, arg, 0)`, sin transformación ni copia del límite | `UI/TextField` | Los valores especiales y el codepage pertenecen a Windows |
| `467b10` | Subclase: Tab/Enter, filtro de caracteres y notificaciones IME; el resto llega al EDIT nativo | `Scenes/GameChat`, `UI/FriendWindows` | La edición por defecto requiere observación de Windows |
| `4682a0` | Con HWND, `SetFocus`, restauración condicional del IME y foco de lista; sólo con argumento **igual a 1**, `PostMessage(EM_SETSEL,-2,-1)` | `UI/FriendWindows` | La selección es diferida; no equivale a `EM_SETSEL(0,-1)` |
| `468260` | Con HWND, guarda el estado; 3 oculta, cualquier otro valor muestra | `UI/OldWindowBase` | No cambia foco ni habilitación |
| `468d50` | Mouse sobre `(x,y-4,w,h+8)`, foco por `left_click`, flechas/barra del wrapper | `UI/FriendWindows` | No calcula índices de caracteres; no usa `left_held` |
| `468320` | Pide al EDIT borrar/pintar al DC y transforma el bitmap, incluidos selección y preedición | `UI/OldWindowDraw` | Los quads por sí solos no demuestran igualdad con el bitmap GDI |
| `467710` | Restaura el WndProc anterior, libera DC/bitmap; no contiene `DestroyWindow` ni `SetFocus`, conserva `+88` | Destrucción de `OldEdit` | El caller determina si el HWND sigue vivo |
| `490b70` | Raise de ventana sin EDIT enfoca MU; Close con otra ventana visible sólo le encola Raised | `UI/OldWindows` | Close y Close seguido de Raise tienen efectos distintos |
| `467360` | Captura conversion/sentence en dos DWORD globales, apaga ambos y libera contexto | Adaptación de plataforma pendiente | No comprueba retornos de IMM32 |
| `4673a0` | Restaura íntegros los dos DWORD guardados y libera contexto | Adaptación de plataforma pendiente | SDL Start/StopTextInput no demuestra este contrato |

`6b9550 + 3c` apunta a `467f30`, comprobado en el PE. El límite de la caja de agregar es 10.
Un límite de cero no significa capacidad cero en el EDIT nativo. `WM_SETTEXT` no está limitado
por `EM_SETLIMITTEXT`. El probe propio de USER32 que se describe abajo deja el caret en cero tras
SetWindowTextA; no se atribuye esa semántica al setter genérico del port, que sus lectores anteriores usan
con caret al final. Falta distinguir cada caller nativo de SetText y sus selecciones posteriores.

## La subclase

`467b10` consulta Up, Down, Left y Right, en ese orden, antes de atender el mensaje; la primera
con byte alto `80` reinicia el contador del caret. `WM_CHAR` lo reinicia incluso si rechaza el
carácter. Descarta Escape y los bytes bajos `19..1c`: no corresponde añadir Ctrl+A o deshacer
por costumbre de un editor moderno.

Con padre lógico distinto de cero, Tab enfoca el destino `+98`, si existe, y se consume aunque
no exista. Enter se entrega al EDIT anterior si la lista F5 está armada exactamente en 1, el
virtual `+5c` devuelve exactamente 1 o `+c4` es exactamente 1. En otro caso encola Enter al padre.
Con padre cero, Tab y Enter se envían como `WM_CHAR` a la ventana del juego.

El filtro numérico tiene prioridad sobre el alfanumérico. La caja H9 usa **8**, que no aplica
ninguno de esos filtros de entrada; su bit 8 pide fondo negro. Los mensajes de inicio, fin y
composición IME se notifican primero al padre y después al EDIT anterior. Una notificación no
demuestra que el texto provisional haya sido confirmado.

## Observaciones nativas en VMXP

Sesión privada `native-c32/session-02`, casos `editor-start-01`, `editor-keyboard-01` y
`editor-arrows-01`. Cada caso comprobó el archivo del módulo cargado contra el SHA-256 anterior y
su base real `400000`. No se confirmó ni envió ningún nombre. Entradas por
`SendInput`; lecturas de HWND, selección y texto sintético por API de Windows. Los raws, capturas,
rutas personales y ejecutables quedan fuera de Git.

El guest usa ACP **1252**, layout `04090409` y un solo layout instalado. El EDIT observado tiene
estilo `50000080`, está visible y habilitado, límite 10, márgenes izquierdo/derecho 3/2. Su HWND
está fuera de la pantalla; el campo que el jugador ve es el bitmap del wrapper. Esto no autoriza
a añadir selección por hit test del texto en el wrapper.

| Secuencia sintética | Resultado observado |
| --- | --- |
| `abcd`, Home, Right, `x` | `axbcd`, caret 2 |
| Desde ese caret, Shift+Right dos veces | selección `[2,4)` |
| Escribir `y` | `axyd`, caret 3 |
| Home, Delete | `xyd`, caret 0 |
| End, Backspace | `xy`, caret 2 |
| Vaciar y escribir `0123456789x` | 10 bytes, caret 10; el byte 11 no entra |
| Seleccionar el primer byte del campo lleno y escribir `ab` | se reemplaza por `a`; `b` no entra |
| Selección `[0,1)`, clic en el campo dibujado | nada seleccionado, caret 10 |
| `abcdef`, anchor 1, caret 3; Left sin Shift | nada seleccionado, caret **2** |
| La misma selección; Right sin Shift | nada seleccionado, caret **4** |
| Anchor 5, caret 3; Left o Right sin Shift | caret **2** o **4**, respectivamente |
| Ctrl+A desde caret 4 sin selección | no cambia texto ni selección |

Las flechas **avanzan desde el extremo activo y quitan la selección**; no se limitan a colapsarla
al mínimo/máximo. La primera implementación propia asumía ese colapso y sus tests pasaban. La
observación XP obligó a corregir la expectativa y el código mediante una regresión roja/verde.

La sesión anterior observó que cerrar la caja con Escape o X puede dejar su HWND EDIT vivo y
con foco. Reabrir crea otro HWND y el anterior sigue válido. Con Confirmar mantenido durante el
cierre se observó foco MU: no debe atribuirse ese efecto al destructor sin resolver el Raise
posterior. No se toma un `GetWindowTextLengthA` de otro proceso como prueba de entrega de texto.

La nueva prueba `editor-orphan-text-01` resolvió la entrega que antes era desconocida: con
`abcd` en el campo, Escape dejó el mismo HWND con foco; teclear `z` produjo `abcdz`, y Left
movió su caret de 5 a 4. Se leyó mediante WM_GETTEXT/EM_GETSEL sobre ese campo sintético.
Después de Raise de Amigos, el foco volvió a MU y el buffer oculto conservó sus cinco bytes.
MuRet conserva ahora una copia del modelo del EDIT enfocado cuando destruye su wrapper. Recibe texto y
edición mientras conserve ese foco; un EDIT nuevo la reemplaza y Reset la descarta. Un marcador explícito
evita confundirla con un ID reutilizado. Enter en esa copia no referencia al padre destruido. Se reproduce
el buffer superviviente sin mantener la fuga nativa de HWND.

La revisión de `45aa80` también confirmó dos gates anteriores al mouse del EDIT: dentro del
rectángulo base, capture ajeno con hover cero, o hover ajeno sin capture propio en estado cero,
retornan antes de `+20`. El wrapper debe respetarlos. Fuera del rect base se llega a `+20`, que
usa su rect extendido; no corresponde agregar un gate global. El foco final de `468d50` tampoco
requiere estado cero: `4690b2..4690dd` lo ejecuta con estado distinto de cero si hay clic. Sus
writes son MouseUpdateTime=0 y máximo=6; no borra el clic ni el ID de otro botón.

## Dibujo: correcciones y dudas abiertas

En `468320`, `6c7af4` contiene **`Q`**, no `1`. La altura se mide y cachea con esa letra. Los
comentarios anteriores de H9 que decían `1` eran incorrectos.

La función llama directamente al WndProc anterior con `WM_ERASEBKGND` y `WM_PAINT`; por ello el
bitmap ya contiene selección y preedición aunque la función no llame `EM_GETSEL`. Fuera del
caret, el primer byte BGR de cada píxel se transforma así: `ff` a color `+9c`, `00` a `+a0`, los
demás a `+a4`. Dentro, `ff` pasa a `+a0` y los demás a `ffc8c8c8`.

Para una línea y sin composición, el ancho es `ftol(1.5 + 1.5 + extent.cx)`, acotado por el ancho
del control escalado. GetCaretPos **no pierde sus márgenes** antes de comparar las coordenadas.
La prueba X es estricta: `x > caret.x && x < caret.x + 1.5`. Un caret final puede quedar fuera
del bitmap; no corresponde restar un margen para que se vea. La observación de `rcCaret` fuera
de Draw no demuestra aún el valor que GetCaretPos devuelve inmediatamente después de WM_PAINT.

El contraste posterior cubrió pegado, conversión ANSI CP1252 y píxeles visibles de selección/caret;
los detalles están abajo y en [12-codificacion](12-codificacion.md). Siguen abiertos el bitmap/GL completo,
el HFONT efectivo del proceso, fuentes alternativas, scroll horizontal, composición real y sus modos
guardados/restaurados. La VM con un único teclado US no valida un IME genuino. Los tests de eventos SDL y composición
de MuRet son pruebas de la implementación, no validación nativa de IMM32. Tampoco estos casos
cierran H1, las rutas de servidor de amigos ni los cortes de otros hitos.


## SetText y atajos: contraste adicional en XP

`467ee0..467f2c` se verificó contra los 31 grupos de bytes del PE c32. La vtable
`6b9550 + 4c` apunta a este setter: NULL llama SetWindowTextA con una cadena vacía;
una cadena no nula se mide hasta NUL y se acepta hasta **255 bytes inclusive**. Con
256 o más no se llama a USER32 y se conserva el contenido anterior. No consulta el
límite de escritura del control y no manda EM_SETSEL después de la llamada. Los
callers `4698dd..4698ea` y `469a52..469a5f` mandan por separado
EM_SETSEL(10000,-1). No atribuir ese movimiento al setter genérico del port.

Un EDIT propio en XP NT5.1.2600/ACP1252, sin mostrar ventana ni cambiar el foco del
original, permitió separar USER32 de la subclase. SetWindowTextA dejó selección
0..0; EM_SETSEL(-2,-1) la dejó al final. Es un oráculo propio de USER32, no una traza
del setter del juego. Ctrl+Delete en `ab cd ef` desde caret0/3/6 dejó vacío/`ab `/
`ab cd `; con selección1..3 o3..1 borró sólo esos dos bytes. WM_CHAR7f añadió DEL
o reemplazó la selección. La navegación por palabra en `aXb`, para cada byte X,
separó 09/0a/20 (CtrlRight desde0 termina2; CtrlLeft desde3 termina2), CR (ambas1)
y los demás bytes no nulos (3/0). Esto no fija aún todas las posiciones ni todas
las secuencias de delimitadores. El propio EDIT oculto tampoco demuestra dirección
de selección/caret visible del original.

El original c32 confirmó sobre `ab cd ef` CtrlLeft8→6→3, CtrlRight3→6,
CtrlHome+CtrlDelete→vacío y CtrlBackspace→los ocho bytes seguidos de7f. La sesión
privada `editor-ctrl-keys-01` verificó nuevamente archivo del módulo y base.
No interpretar CtrlBackspace como el borrado de una palabra moderno.

Las sesiones `editor-paste-truncation-02` y `editor-paste-empty-01` del original,
sobre `abcd` completamente seleccionado, confirmaron: CF_UNICODETEXT vacío o
que empieza con CR borra la selección y deja caret0; `XY`+CR+`ZZ` deja `XY`,
caret2; LF solo queda como byte0a, caret1. El fallo de lectura/conversión no se
equipara a un pegado exitoso vacío. No se confirmó ningún nombre ni se envió a
la red en esas pruebas.


## Navegación por palabra de USER32 en XP

El probe propio `edit-word-own-01` usó un EDIT ANSI oculto del mismo XP, sin
mostrar ventanas ni cambiar el foco del juego. Restableció el estado de teclado
de su hilo al terminar. Sus nueve fixtures sintéticos incluyen espacios iniciales
y repetidos, coma/punto/guion, NBSP, TAB, LF y CR. Se recorrieron todos los prefijos
y posiciones: 188 filas más el control `a b` desde0. El SHA-256 del CSV es
`707d0ce237e2720601f450c2cc891aff92b2a6261afb9981d2b08d87f33ebcd8`; el FNV-1a64
de los cuatro offsets de selección de las 188 filas, un byte cada uno y sin el
control inicial, es `697d385b07224401`.

CtrlRight primero salta TAB/CR/espacio iniciales, luego una palabra y luego
TAB/espacios. LF termina el avance después de sí; CR encontrado después de una
palabra lo termina antes de sí. CtrlLeft salta TAB/LF/espacios inmediatamente
anteriores y retrocede la palabra; CR lo termina antes de ese byte. Puntuación
y NBSP no son separadores en las pruebas. No reemplazar este comportamiento por
una clasificación Unicode de whitespace. La implementación propia coincide en
las 189 filas, sin generalizar esa observación a todas las páginas DBCS.

Una selección hacia atrás se comprobó también por entrada física en el original:
`abcd`, End y dos ShiftLeft deja selección2..4 y caret15. Right sin Shift deja
caret del byte3 (píxel20); Left desde esa selección deja byte1 (píxel9). Esto
respalda el movimiento desde el caret activo. El EM_SETSEL de un EDIT propio
oculto no bastaba para demostrar esa dirección.

## Bitmap visible: selección y caret

PrintWindow del EDIT original devolvió0/error8, sin cambiar los píxeles sentinel
del DC del observador. No se contó como captura válida ni se interpretó como
caída del juego. La comparación siguiente usa capturas MKS de la salida visible
del cliente y el buffer sintético `abcd`.

En 1024×768, el margen izquierdo es3, el ancho del texto23 y GetCaretPos al final26.
La selección0..2 ocupa12 columnas y deja caret15. El gris observado de selección
fue RGB(148,150,148), compatible con la cuantización RGB565 del color fuente150
de `468320`. El caret mostró RGB(197,198,197), compatible con200; en su columna
había diez píxeles de ese gris y tres negros donde antes había glifo blanco.
Por eso no es correcto dibujar una columna sólida sobre el texto ya pintado:
la conversión del caret reemplaza tinta blanca por fondo y el resto por200.

Pasar el foco a la ventana de Amigos conservó EM_GETSEL 0..2 y ocultó el resaltado. Los píxeles del renglón
volvieron a ser iguales al caso sin selección. El color blanco que usa USER32
para la tinta seleccionada se convierte en `edit.textColor`, igual que la otra
tinta; no fija blanco en la salida final si el objeto tiene otro color.

El ancho del bitmap sigue siendo texto más1,5 más1,5, recortado por el campo;
GetCaretPos no resta el margen. Con ese perfil, el caret final y el de un campo
vacío pueden quedar fuera del bitmap. Se conserva ese recorte, en vez de ampliar
el bitmap para forzar un cursor visible. Las métricas y píxeles se verificaron en
este perfil; no acreditan fuentes alternativas, multiline ni preedición real.

## Enter encolado y modos IMM

Una revisión adversarial del callsite de Enter refutó un drenaje sincrónico:
`467cd1` llama `490b40`, que resuelve el padre con `490a00` y en `490b5e` llama
`459f70` para encolar7. No llama `45aa50` ni su virtual Step(1). Las excepciones
Enter de la lista armada, del virtual+5c y de+c4 siguen al oldWndProc en467d57.
Esto respalda el enqueue de `PressEnterInTheOldEdit`; no demuestra por sí solo el
orden del bucle entero de mensajes frente a un frame SDL.

Los229 bytes de `4673d0..4674b4` coinciden con el PE. Con el global5762ee0 en0,
no hace nada. Con bit0 del argumento, una guarda5762ed8 exactamente1 se pone0 y
sale de toda la función; de otro modo consulta IMM y, con conversión no nula,
guarda su DWORD entero en5762ed0, pone la guarda1 y luego pone ambos modos a0.
El bit1 usa5762edc y5762ed4 para sentence con el mismo esquema. Con flags3, la
segunda consulta ocurre después del primer Set; la primera guarda puede omitir
toda la segunda rama. No hay configuración de candidate/composition window ni
geometría en este helper. Los BOOL de IMM no se inspeccionan; no se atribuye una
salida determinista a locals sin inicializar si falla la consulta.

Las guardas son compatibles con supresión de notificaciones de retorno, pero su
entrega por un IME real sigue sin observarse. Guardar/restaurar los DWORD de
conversión y sentence de XP no equivale a iniciar/detener SDL text input. La
preedición y esos modos siguen pendientes del editor completo.
