# 0020. Señales propias para el proyectil y la descarga radial de Crywolf

**Estado** · Propuesto en la actualización de H11 (2026-11-18).

## Contexto

Los ataques de etapa 14 en Crywolf y fortaleza crean `F8/1` en `0x43257a` y
`0x42fd76`. `0x4329db` y `0x4f6780` ejecutan sendas tandas de 18 fragmentos
`C1A/0`. El ayudante de Crywolf permite que el paso común ejecute la segunda
tanda. MuRet cortaba estos recorridos con lugares de modelo disponibles.
[Análisis del ASM](../../Client/docs/research/2026-11-18-cl9-crywolf-dependencias-asm.md).

## Decisión

Conservar las condiciones, los argumentos, el orden de reserva y la continuación
de cada llamador. `F8/1` conserva dueño, clave y copia de la cuenta protegida.
En vida 13 intenta reservar `105/3`; el pedido de sonido `0x7a` depende de la
reserva y precede al choque y la luz. El pulso sigue al proyectil mientras vive.
Los fragmentos usan el giro, la velocidad y la vida extraídos de `0x51f84f`.
El contacto exige altura del terreno mayor que Z y permite completar la luz,
el movimiento y el decremento de ese paso (`0x52c9f2`, `0x521216`).

El proyectil usa el movimiento compartido de MuRet como ruta de su centro.
En el original la cola mueve el punto de partida y un joint modifica la
posición efectiva. Las tres señales se construyen con triángulos aditivos sin
texturas en el primer pase del mundo. Los fragmentos se extinguen al chocar
con una luz en el punto de contacto. Su representación reemplaza el cuerpo y
las emisiones de contacto. El proyectil y el pulso usan geometría propia en
lugar de las cadenas `60f/19` y `610/0..3`.

## Consecuencias

El uso de pools y RNG secundarios difiere allí donde cambia la emisión. Las
pruebas locales cubren altas, saturación, continuación, vida y dibujo. El
runbook deja para la VM el contraste de los recorridos con un servidor que
produzca los estados de Crywolf y el registro de las diferencias en pantalla.
