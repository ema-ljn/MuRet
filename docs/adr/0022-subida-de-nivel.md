# 0022. Campos de subida de nivel en el wire

**Estado** · Propuesto para el complemento de CL9 (2026-11-18).

## Contexto

El GameServer 1.02i emite 18 bytes en `F3 05`. El cliente c32 lee dos palabras
adicionales desde `0x4d2ed0` para su registro propio. El buffer de la cola
incluye datos fuera del frame. [Q-C1369](../../Client/docs/research/07-quirks-y-bugs.md#subida-de-nivel).

## Decisión

Leer únicamente palabras completas del frame. Los siete campos del servidor
son obligatorios. Cada palabra de la extensión ausente vale cero, siguiendo
el criterio de Q-C144 para la respuesta de entrada al mundo.

## Consecuencias

Los 18 bytes del GameServer bastan para actualizar las estadísticas y
despachar los efectos y el sonido. Una variante que envíe las dos palabras
las conserva. Los tests de protocolo prueban ambos casos y el truncado.
El test de integración pasa un frame real por la sesión y el aplicador.
[Wire y ASM](../../Client/docs/research/04-protocolo.md#subir-de-nivel-f3-05).
