# 0015. Identidad de asignación de las ventanas con ID repetido

- **Estado:** propuesto e implementado en la rama de cierre H1/H9 por Codex (2026-11-14), para revisión del PR.
- **Research:** Q-C1364 de [07, quirks y bugs](../../Client/docs/research/07-quirks-y-bugs.md).
- **Criterio:** conservar el comportamiento recuperado del ASM, con ownership seguro y copia independiente de las proyecciones.

## Contexto

El contador nativo `0x459ef0` incrementa un DWORD. Las dos tablas comparan sus claves sin signo
(`0x48f6f2..0x48f6f5`, `0x49090c..0x49090e`) y no reemplazan una clave existente. El contador puede
cruzar el bit31 y volver a cero. No se identificó un trigger ordinario de colisión sin forzar el contador.

La fábrica conserva el registro previo, pero añade otra vez el ID al orden y asienta el objeto recién construido
(`0x48f7c6..0x48f831`). Si es Amigos, sus pestañas ya se registraron como **punteros directos** en la segunda
tabla (`0x4a232d..0x4a2348 → 0x4908d0`). `0x490a8a` devuelve ese puntero aun si su ventana no entró al mapa
principal. Por eso liberar inmediatamente el temporal cambia el contrato de lookup: no es sólo una diferencia de memoria.

`0x48ea50` vacía el mapa principal y las listas, pero **no** el mapa de pestañas. Este último se destruye en
`0x48e937..0x48e93e`, durante el destructor del manager. Las pestañas de objetos rechazados siguen vivas tras Reset.
Los campos `manager+0x48` y `base+0x44`, en cambio, guardan IDs: `0x48eff1..0x48eff6`, `0x49c8c2..0x49c8cb`.
El lookup del padre de `0x45abc0` vuelve a `0x490a00`; no debe sustituirse por un vínculo directo al objeto nuevo.

La salida previa por cascada agotada sí destruye el temporal (`0x48f700..0x48f70a`), incluida la limpieza
del botón pulsado por ID. No se retiene ese objeto como si hubiese alcanzado el insert duplicado.

## Decisión

Separar el ID nativo de 32 bits de la identidad de cada asignación del port. El mapa secundario guarda el ID del
owner y su identidad; el lookup valida ambos contra una ventana viva o contra una asignación no registrada.
El primer registro del mapa sigue ganando. El contador de identidad del port no se reinicia con Reset.

Los temporales rechazados se conservan por valor hasta destruir el manager. Reset conserva tanto esos objetos
como los registros secundarios. Destruir una ventana registrada deja su registro de pestaña anterior; una
asignación posterior con el mismo ID no se apropia de él. Una referencia a una asignación ya destruida se resuelve
como ausente: no se reproduce una desreferencia de memoria liberada ni se promete el comportamiento de un alias
que dependa de la reutilización de direcciones del allocator nativo.

El modelo es copiable por valor: una proyección conserva las identidades lógicas y su propio almacenamiento,
sin punteros hacia el manager original. Se mantiene el lookup común por ID más comprobación de identidad;
sólo hace falta recorrer las asignaciones rechazadas cuando no coincide el registro vivo.

## Consecuencias y verificación

Las regresiones de `OldWindowsTest` fijan primera inserción, orden unsigned, wrap y asentado del temporal sin
mover los botones del registro anterior. Las de `FriendWindowsTest` observan lookup y publicación de mensajes a
pestañas rechazadas, su geometría contra el padre recuperado por ID, supervivencia a Reset, primera pestaña viva
ante una clave repetida y ausencia de rebind tras Destroy/Reset. La copia del manager mantiene mensajes y estado
independientes, también después de resetearla.

Esta decisión conserva el contrato de las asignaciones vivas del original sin introducir una fuga sin dueño en
C++. No certifica lecturas de punteros inválidos ni el layout del allocator. Los mapas de chats/cartas H10 son
otros registros; no quedan reconstruidos por este ADR.
