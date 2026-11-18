# 0018. El cambio de servidor continúa si falla el guardado de macros

**Estado** · Propuesto durante el cierre de H13 (2026-11-18).

## Contexto

En `B1 00`, `0x412b5e` cierra el socket anterior y `0x412b6d` llama al guardado
de `Macro.txt` (`0x61c170`, Q-C449). Al retornar, el flujo llega a `Sleep(20)`
en `0x412b77` y conecta en `0x412b86`. El llamador no examina un resultado del
guardado. Si falla la apertura, `0x61c170` entrega el puntero nulo a las
rutinas de escritura y cierre. Esto deja indefinido el resultado de ese caso.
[Research de red](../../Client/docs/research/03-red-y-cifrado.md).

## Decisión

MuRet registra el error que devuelve el guardado local y continúa la espera y
reconexión. Así el proceso conserva las macros en memoria y puede seguir hacia
el servidor indicado por `B1`. Antes del cambio, un archivo inaccesible dejaba
la sesión en `ApplicationFailed` después de haber cerrado el socket anterior.

## Consecuencias

La persistencia local puede fallar en ese cambio de servidor. La espera, el
destino y el consumo de RNG de la preparación se verifican en
`SessionEventApplierTest.TheMapSwitchReconnectsWhenTheMacrosCannotBeSaved`,
que hace llegar `B1` por la sesión mientras `Macro.txt` es un directorio.
