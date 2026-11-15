# CL9 H1/H9: editor XP y contraste nativo parcial

El cliente se reconstruye según ASM y pseudocódigo; prevalece el ASM. El servidor
del laboratorio está en otra PC y no se modifica como parte de este trabajo.
Emanuel confirmó que el sistema de amigos/chat no conecta y que Agregar+Enter
produce desconexión. Se conserva el comportamiento del cliente y se separan las
pruebas locales de lo que ese servidor permite observar.

## Cambios del cliente

- Perfil occidental CP1252: decoder del rasterizador y encoder de entrada con el
  best fit de XP, sin cambiar bytes del wire ni normalizar Unicode. Agregar,
  mensaje, destinatario y nombre comparten el perfil; login/códigos Ascii y las
  validaciones posteriores de creación conservan su alcance.
- Cursor y selección por bytes; insertar, borrar y pegar en medio; reemplazo en
  el límite; Ctrl+flechas según el word break observado en XP; Ctrl+Delete hasta
  fin de línea y Ctrl+Backspace como DEL. Ctrl+A conserva su no-op original.
- Eventos de texto, teclas, modificadores y composición en orden. Se mantiene
  Text() para lectores anteriores. FocusLost cancela la preedición del modelo,
  para no depender de una cancelación que todos los backends SDL no garantizan.
- Portapapeles con adaptador SDL: distingue texto vacío válido de fallo o falta
  de formato textual; no lee ni cambia el clipboard del host en los tests dummy.
- Buffer del EDIT que sigue enfocado al destruir su wrapper. Se conserva un
  snapshot acotado, sin reproducir la fuga de HWND del original; un foco nuevo
  reemplaza el snapshot y Reset lo limpia. Enter no apunta al padre destruido.
- El mouse respeta el rechazo del gate común y el foco/cooldown de 468d50. El
  dibujo usa origen en píxeles, selección, caret real e inversión de su columna;
  conserva el recorte del caret vacío/final observado en el perfil XP.
- Los fallos intencionales de death tests Linux se aíslan en sus hijos para no
  generar reportes del escritorio. No se cambia la configuración global.

## Qué se comparó con el original

El módulo real de cada sesión original se copió sólo como datos y se verificó
contra SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`
y base 400000. El cliente y los lectores de su proceso se ejecutaron sólo en XP.
No hubo inyección, parches, escritura de su memoria ni restauración de snapshots.
Raw, nombres, logs de red, capturas y fuentes de los observadores quedan privados.

| Caso | Observación | Alcance del resultado |
| --- | --- | --- |
| H1-E1 parado | Enviar `^^` produjo dos C1/18 iguales, acción 8e y dirección 1, seguidos por un solo chat; el servidor devolvió el chat | Coincidencia de secuencia y wire de ese caso. No acredita animación/pose completa, caminando, elfo ni transformación |
| H9-B1 | Se conservan las tres ramas previas con F, F6 y Confirmar mantenido | Coincidencias acotadas; botón ajeno no alcanzado dinámicamente |
| H9-U1 | Cerrar Agregar dejó un EDIT vivo/enfocado: `abcd`, cerrar y escribir `z` produjo `abcdz`; Left movió el caret | Respalda buffer superviviente y foco; no es una política universal de todos los cierres |
| Editor y codificación | Pegado discriminante, selección, límite 10, dirección de caret, CR/LF, Ctrl+Delete/Backspace y bitmap visible | Perfil ANSI CP1252 de este XP; ver research 11/12 |
| H9-Q1 parcial | Una solicitud C1 de Agregar dirigida sólo al propio personaje; el peer confirmó TCP y cerró la conexión aproximadamente 13 ms después, sin respuesta de amigos | Envío único observado. Impedimento del servidor confirmado por Emanuel; no se repite la solicitud. No demuestra el orden interno completo de Draw/WndProc |

La primera captura del adaptador del host no vio tráfico del switch virtual; no
se contó como prueba. La válida usó el capturador oficial de VMware y filtró sólo
la conexión real del cliente identificada mediante netstat. Una consulta a un
perfil viejo de VMware no correspondía al servidor real: no se usa como evidencia
sobre su estado.

En H1, la dirección 1 se conserva como el byte observado en el wire; no se
correlacionó con un ángulo legible del héroe. El conteo de acciones del modelo
no sustituye la captura del descriptor de animación 175. Por eso ese caso sólo
respalda las cantidades, heads y orden de paquetes previstos por el ASM.

PrintWindow del EDIT no produjo un bitmap válido; no se convirtió ese fallo del
observador en una caída del original. Las conclusiones gráficas vienen de la
salida visible MKS, con texto sintético, y del ASM de 468320.

## Oráculos independientes y revisión

El probe NLS propio de XP midió 256 decodificaciones, 65.536 unidades UTF-16 con
dos flags y 20 secuencias. El encoder coincide con las 65.536 salidas de flags 0;
los 441 best fit están ordenados y son únicos. El probe de word break aporta 188
filas más su control inicial. Estos programas propios no se presentan como
trazas de todas las rutas internas del juego.

La revisión independiente detectó alias de una vista del campo, cancelación de
preedit perdida al perder foco y resets de blink omitidos en atajos; se añadieron
regresiones antes de corregirlos. La última revisión encontró que el nuevo perfil
del nombre aceptaba el CR de Enter antes de confirmar: un test observó el fallo
con nombres de cuatro/cinco bytes y con Euro. El caller separa ahora Enter del
texto; el encoder conserva su contrato de conversión. Otra pasada contrastó el
dibujo, la paleta, la inversión y la conservación de la semántica anterior de TextPieces. Los detalles
y límites están en [11-edit-win32](../../../Client/docs/research/11-edit-win32.md)
y [12-codificacion](../../../Client/docs/research/12-codificacion.md).

## Verificación final

- Build Linux completo con GCC 15, C++20, warnings como errores y sin PCH: exit 0.
  La última corrección de Enter se recompiló y enlazó antes de ejecutar la suite.
- CTest completo: **7.074 aprobados, cero fallos y cero salteos**, en 217,59 s,
  con Data real y los corpus de memoria, wire y trazas recuperados. Los 59 casos
  del manifest se identificaron por nombre: 59 aprobados, ninguno ausente.
- Incluye 106 tests etiquetados GL, con `SDL_VIDEO_DRIVER=offscreen` y
  `MURET_CLIENT_REQUIRE_GL=1`; no se contó un skip como validación gráfica.
- Enter en creación: un test que falló antes del arreglo pasa después de separar
  el control del texto. El resto de la suite pasó con ese delta final.
- clang-format 18 y misc-include-cleaner 18: 69 archivos de C++ comprobados,
  incluidos los headers por separado, sin hallazgos pendientes. Se corrigieron
  cuatro dependencias indirectas de headers y se recompiló el delta.
- Death tests Linux: ocho cores históricos antes y después, **cero nuevos**
  durante la suite. Se conservaron los anteriores.
- Arranque local acotado: Main con Data real llegó de Loading a Login, cargó 482
  modelos y salió con 60 frames usando OpenGL. No equivale a probar H9 dentro del
  juego con un servidor.

No se hereda el CI del PR #353: **Windows no se ejecutó para este delta**. Los logs,
JUnit y comprobaciones quedan en el archivo privado de la pasada; no se versionan
raws ni estado generado. Los checks standalone RED/GREEN se conservan como
complemento de esta corrida completa.

## Pendientes y cortes

- IME genuino, sus modos de conversión/sentence, preedición visible y candidate UI
  no equivalen aún al EDIT completo de XP. La composición se conserva separada y
  se cancela correctamente en el modelo, pero no se declara implementado todo
  P-C181. Multiline, scroll y perfiles DBCS necesitan sus contratos propios.
- H1-E1 caminando/elfo/transformación, DFf1 por mecánica real y bendición repetida
  siguen sin nueva validación nativa. No se fabrican estados del original.
- H9-D1, quads/RNG completos y botón ajeno no quedaron certificados por estas
  capturas. Los tests locales observan los casos que declaran, no todo el juego.
- El editor de mensaje/susurro comparte el perfil CP1252, pero todavía no enruta
  navegación, clipboard y composición como el EDIT viejo. Este pendiente del
  cliente se conserva separado del impedimento del servidor.
- El flujo real de amigos/chat requiere soporte del servidor; su desconexión no
  se trata como el resultado deseado del cliente. Los chats con amigos y cartas
  de H10 y las integraciones H3/H4/H7/H13 conservan sus dueños y cortes.

H1/H9 no se declaran 100% terminados ni se cierra toda CL9 con esta reparación.
