# 0019. Señales propias para las altas y los ataques del asedio

**Estado** · Propuesto en el cierre de H13 (2026-11-18).

## Contexto

Los llamadores `0x42d513` y `0x42d5af` crean `134/0` con el actor y la clave
120. `0x42dd48` y `0x42dd9c` crean `61c/0` y `19f/0` durante el ataque.
`0x518cf0` devuelve el control si los 200 lugares están ocupados. MuRet se
detenía al llegar a esas ramas con un lugar disponible.
[Análisis del ASM](../../Client/docs/research/2026-11-18-cl9-castle-dependencias-asm.md).

## Decisión

Conservar los puntos de entrada, sus argumentos y el orden del pool. Las tres
señales se dibujan como triángulos sin textura en el pase del mundo. La llegada
queda en su punto de alta mientras el actor vive y es visible durante el asedio.
Los ataques tienen una dispersión ámbar y un pulso violeta. Cada señal de ataque
se libera al acabar su vida y el pulso conserva el sorteo del alta de `0x51f193`.
Al reutilizar un lugar, el pase de suelo conserva la visibilidad del dibujo
anterior y su sorteo de `0x52dc46`.

## Consecuencias

La geometría, las emisiones y la secuencia de RNG de sus efectos secundarios
corresponden al diseño de MuRet. Las pruebas de viewport y ataque comprueban los
llamadores con lugares libres y con el pool lleno. Las pruebas de paso y dibujo
comprueban vida, geometría sin recursos y restauración del material. La VM
queda para validar los recorridos con un servidor de asedio y observar las señales.
