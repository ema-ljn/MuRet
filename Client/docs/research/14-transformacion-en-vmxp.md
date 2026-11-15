# Anillo de transformación: modelo, bloqueo y protocolo

Diagnóstico del 2026-11-15 durante H1. Original PE32 x86, base400000,
SHA-256 `c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
Se separan las lecturas de XP, el contrato ASM y las inferencias. El servidor
está en otra PC; no se cambió su configuración ni se parcheó el cliente original.

## El modelo tiene caminata

El tooltip identifica **Budge Dragon Anillo de transformación**. El actor toma
tipo0x154: es el modelo Monster03, no el número340 de monstruo del protocolo.
El creador del monstruo2 carga `Monster\Monster03.bmd`; el43 reutiliza el modelo.

El archivo real del guest coincide con `Client/assets/Data/Monster/Monster03.bmd`:
SHA-256 `9758900d276f4db86f237a31c0617fa00ee04469d964865e71bdda01e20ae474`.
La tabla cargada tiene7 acciones. Se leyeron sus descriptores efectivos: acción0
con6 keys y acción2 con7 keys. No hay evidencia de un archivo de modelo
incompleto en este caso. No tener la animación de gesto175 es normal para ese
modelo; no necesita las205 acciones de Player.bmd para caminar.

## Contrato de movimiento y reposo

| Función | Evidencia ASM relevante |
| --- | --- |
| `0x59adf0`, MoveHero | No veta el tipo0x154 ni exige0x20a para iniciar camino. Acción0 pasa la guarda de acción con byte80 fuera de1/2 y alpha≥0.7. Otros gates de entrada, UI, estado y pathfinding siguen aplicándose. |
| `0x4f37c0`, caminar | `0x4f3872` separa0x20a; cualquier otro tipo llega a `0x4f3d83` y pide acción2 de su modelo. Zona segura sólo reinicia Run; no impide esa elección. No activa Movement por sí misma. |
| `0x4f2250`, reposo | NoPlayer lee celda lógicaY y luegoX con cierres separados. Pide0, salvo0x172 en TerrainWall&1, que pide7. Después corre su sufijo de azar y tabla de sonidos. No escribe Movement. |
| `0x56acd0`, SendMove | `0x56ad0c..0x56ad14` comprueba `object+0x78 & 0x20`; si está activo, salta al retorno0x56b796, antes del D3 y de poner Movement=1. No consulta modelo ni cantidad de animaciones en esa guarda. |

Las funciones fueron asignadas a analistas separados. El bloque del noPlayer
y prólogo/sufijo de reposo se contrastó en860 instrucciones con el PE; SendMove,
en746 instrucciones. El pseudocódigo no sustituyó esos cotejos.

## Lo observado en XP

Se usaron lectura externa de campos en claro y entradas normales de teclado y
mouse. No se escribieron flags ni se alteraron posiciones protegidas. Para el
cambio de equipo se identificó el anillo por su tooltip y se trasladó entre su
slot y una celda vacía del inventario; después quedó reequipado.

| Observación | Resultado |
| --- | --- |
| Transformado, antes de cambiar equipo | Tipo0x154, acción0, Movement=0; flags78 contiene0x20. Alpha1, byte80=0, dying=0, input/UI sin bloquear, cadencia31≥6 y movementBlocked=0. |
| Quitar el anillo | Una solicitud24 y una respuesta24; actor pasa a0x20a y flags78=0. |
| Volver a equipar | Una solicitud24 y una respuesta24; vuelve0x154 y reaparece0x20, junto con otros flags. |
| `^^` transformado en reposo | Un18 antes del chat y eco del servidor;38 muestras conservan acción0/Movement0. El gate del gesto usa la acción1..12/8a, no el hecho de estar físicamente quieto. |
| Intentos de caminar | No se observó Movement=1, incluso con DOWN250ms y probando iniciar antes de abrir chat. No son casos aprobados de caminata. |

El bit0x20 es una condición suficiente para que **esa llamada a SendMove** no
active movimiento. No prueba por sí solo el origen del bit ni que todos los
demás gates pasen en cada instante.

## Qué muestra el cable y qué sigue sin demostrarse

Al reequipar llegó un C2/45 con cuenta1 y **22 bytes de registro**. El cliente
consume registros de28 bytes y lee hasta el byte24: flags en8..11, nombre en
12..21, destino en22/23 y dirección/PK en24. El nombre real coincide con el
offset12; no se identificó como el formato0.97 sólo por medir22 bytes. En el
campo8..11 del cable ya está activo0x20. No es evidencia de que el nombre se
haya interpretado como flags.

Por tanto hay dos hechos distintos: el estado que interpreta como inmovilidad
está presente en el prefijo recibido, y el registro de esta sesión carece del
sufijo de destino/dirección que espera el ASM. **El tamaño corto no demuestra
por sí solo por qué se envió0x20.** La captura es del cable, anterior a posibles
adaptaciones dentro del proceso; no se tomó una traza del buffer justo al entrar
al dispatcher. No se establece aquí producto/versión del servidor ni un fallo
concreto de una DLL.

Al quitar el anillo llegó además un12 con registro de32 bytes, frente a44
esperados por este cliente. Estos datos acotan P-C160 para el entorno real;
no certifican que sus paquetes sean los de un GameServer1.02 compatible.
PCAP, nombres, seriales, bytes de ítems, capturas y manifiestos quedan privados.

## Diferencias independientes corregidas en MuRet

La comparación encontró un hueco propio aun sin reproducir el estado bloqueado
del servidor: el paso del héroe transformado no prestaba el esqueleto de su
tipo, y reposo cortaba todo noPlayer. Se completa la rama de reposo y se prestan
esqueleto/sonidos en caminar, llegada,18,D3,paso,NPC y transiciones alcanzables.

El modelo ausente representa el registro nativo con cero acciones: SetAction
conserva acción/frame y continúa el sufijo. Los sonidos mantienen el fallback
de diez WORD cero de FaunaModels, no se inventa silencio−1. La llegada también
debe prestar la tabla para los Player de subtipo209..211; una regresión con
seed1 observó su omisión antes de corregirla.

Los tests de integración usan un transformado de siete acciones y recorren
clic→D3→caminar2→llegada0. No simulan que la sesión XP haya logrado caminar
con0x20 activo. El protocolo portable sigue el contrato de1.02 y conserva sus
diagnósticos de longitud; este trabajo no inventa un perfil de compatibilidad
para aceptar silenciosamente el registro corto observado.
