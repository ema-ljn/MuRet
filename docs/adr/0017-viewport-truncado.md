# 0017. Un registro de viewport incompleto conserva TooShort

- **Estado:** documenta el lector existente durante el cierre de H1 (2026-11-15).
- **Contrato:** [04, otros jugadores](../../Client/docs/research/04-protocolo.md#los-otros-jugadores-12-14-45-25-y-df)
  y [diagnóstico real del anillo](../../Client/docs/research/14-transformacion-en-vmxp.md).

## Contexto

El handler45 del original (`0x4c92a0`) recorre registros con stride28 y lee hasta
el byte24. La sesión XP recibió un registro de22 bytes al equipar un anillo.
Los campos de nombre y flags del prefijo no suministran el destino/dirección
faltantes. Leer memoria contigua al paquete no define un resultado portable.

Este tamaño no demuestra por sí solo la causa del bit de inmovilidad presente
en el prefijo ni la versión del servidor; el diagnóstico conserva esa duda.

## Decisión

Se mantiene la frontera del lector puro `ReadTransformed`: devuelve
`ProtocolError::TooShort` antes de crear un resultado parcial si faltan bytes
leídos por el handler. Para N registros, se necesitan `5+(N−1)*28+25` bytes,
incluida cabecera y cantidad; cantidad0 conserva el resultado vacío.

No se exige el padding no leído del último registro: uno de25 bytes es válido.
Tampoco se rellenan sus tres campos faltantes con ceros para aceptar el de22,
ni se añade implícitamente un perfil de otro servidor. Los otros lectores del
viewport mantienen sus strides y últimos accesos documentados.

## Verificación y alcance

`ViewportTest.ATransformRecordNeedsItsTargetAndDirectionButNotUnusedPadding`
construye datos sintéticos sin nombres reales: rechaza22 y acepta25 conservando
destino/dirección. `TransformedReadsTheTypeAndTheShortRecord` comprueba el
registro completo. No se versiona el PCAP ni se afirma reproducir una lectura
fuera del buffer del original.

Esta decisión fija un error de protocolo del lector; no repara el emisor del
servidor, determina la política de desconexión ni certifica toda la integración.
