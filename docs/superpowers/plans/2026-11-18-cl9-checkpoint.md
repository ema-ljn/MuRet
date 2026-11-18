# CL9: checkpoint publicado para continuar

Guardado el 2026-11-18 a pedido de Emanuel. **Es un respaldo del trabajo en curso;
no declara CL9 terminada ni todos los checks aprobados.** ASM c32bbd5f prevalece
sobre pseudocódigo. El original no se ejecutó en el host.

## Qué está respaldado

| Entrega | Rama publicada | Revisión de código guardada |
| --- | --- | --- |
| H9/H10, PR [347](https://github.com/ema-ljn/MuRet/pull/347) | `claude/cl9-hito-10` | `ed24a9e3` |
| H6, PR [348](https://github.com/ema-ljn/MuRet/pull/348) | `claude/cl9-hito-6` | `eedc3780` |
| H12, PR [349](https://github.com/ema-ljn/MuRet/pull/349) | `claude/cl9-hito-12` | `38e4e930` |
| H13 y conexiones finales, PR [363](https://github.com/ema-ljn/MuRet/pull/363) | `codex/cl9-h13-cierre` | código hasta `25c71930`; este documento se añade después |
| Recursos/Fire: checkpoint WIP, sin otro PR | `codex/cl9-golden-resources` | `26c3ab09` |

La pila sigue `main` ← 347 ← 348 ← 349 ← 363. Base auditada: `main` en
`c8d6dbb3`. Se conservaron los cambios remotos por merges normales. No se mergeó
a main ni se hizo force push.

`26c3ab09` guarda cuatro archivos de tests: recursos del epílogo de agua y el
oráculo de consultas nativas que pide Fire. **Producción Fire/order todavía no
está implementada en ese checkpoint.** Sus 22 pruebas ejecutadas son LoadedWorld15
y Kalima7; el nuevo oráculo Fire/order aún necesita RED y la reparación correspondiente.
No interpretar esa rama de respaldo como una entrega lista para merge.

## Correcciones incluidas en el PR363

- Implementación H12/H13, conexiones de Crywolf, Battle Soccer, NPC y cierres.
- Revisión Astra de transporte: Esc ahora usa el sender real de GameScene.
- Callers determinados antes cortados: ED/E1, joints618, llegada Crywolf y C5→C8/C9.
- Renderer de dos pases del altar Crywolf51; estados, RGB y precisión PC53.
- Warnings Windows: retornos del visitante y literales tipados, incluidos en `d8a10262`.
- Regresiones positivas de Luck, entrada1a1d, flechas/ataque, chat/cierres, bendición,
  dibujo fallido y cuadros completos de Chaos Castle18..23. Se conservaron los guards ASM.
- Fixtures de inventario inicializadas por el reset real de selección; el guard661b90
  sigue mirando los WORD de los 32 slots aunque `occupied=0`.

Las regresiones focales se compilaron por separado con headers y bibliotecas
coherentes, comprobando hashes antes/después. Entre los resultados registrados:
GameScene34/34, altar13/13, H2+CameraTour55/55, Luck5/5, entrada1a1d6/6,
flechas/ataque49/49, chat/diálogos68/68 y T16/dibujo77/77. No sumarlos como si
fueran una suite completa de una única ejecución. Astra revisó también los cambios
de oráculos; no encontró un A/B pendiente en esos cambios revisados.

## Validación completa que falta repetir

Una ejecución local registró 9410 casos, 46 fallos y ningún salto, con GL requerido
y referencias históricas disponibles. **Ese total no acredita el SHA publicado:**
al sincronizar se conservaron timestamps anteriores a algunos objetos compilados,
y Ninja reutilizó objetos de tests viejos. Los fallos mezclan oráculos anteriores
con casos pendientes de confirmar. No convertir esa lista automáticamente en 46
bugs de producción ni en 46 cambios de expectativas.

La biblioteca de producción y los headers f5e8 usados por las verificaciones
manuales sí se comprobaron: sus 27 unidades modificadas se recompilaron en el log
R4 y los enlaces focales conservaron el SHA de los archives. El error de caché
invalida la atribución del total de tests, no esas compilaciones independientes.

Siguiente paso obligatorio: sincronizar la cabeza final sin conservar timestamps
obsoletos, invalidar los objetos afectados y sus dependientes, o hacer una build
nueva. Después repetir CTest completo, GL obligatorio, fuente real, formato18,
include-cleaner18 y Linux/Windows sobre las cabezas publicadas. Los CI anteriores
detectaron un test Luck obsoleto, fixtures sin reset de selección y los warnings
MSVC corregidos; consultar siempre los checks del SHA nuevo.

En este entorno Xvfb necesita permiso de red/local sockets del ejecutor; sin él
falla al crear sockets y SDL informa que no hay video. La prueba con permiso abrió
OpenGL4.5/llvmpipe. Usar `MURET_CLIENT_REQUIRE_GL=1`, fuente DejaVu y los entornos
generados por el recuperador; no aceptar los saltos de GL como aprobación.

Los 62 archivos y 51 recursos del manifiesto histórico se recuperaron con hashes
verificados, fuera de Git. Se pueden recuperar otra vez con los scripts existentes.
Memoria, claves y tráfico privados no se suben al repo. El perfil histórico2f16
no se transforma por eso en una nueva observación de c32 en VM.

## Recursos y fidelidad pendientes

Fire01 es modeloC2, `Skill/Fire01.bmd` (615d4c), con textura consultada en el
barrido616e20..616e46. El trabajo WIP debe registrar ese recurso y ordenar las
consultas alcanzadas del615700. El pedido de texturas10f en61619f ocurre antes
del alta RidingSpear01.bmd en6161b5: en el perfil BMD sin Data2/SMD actual no
dispone de esas mallas; su asignación efectiva corresponde al barrido final.
No anticipar mallas futuras para forzar el resultado de la consulta anterior.
No fijar slots arbitrarios ni cambiar sólo el conteo del logo: verificar nombres,
reutilización, sampling y orden. El valor455 nativo y el456 actual no son equivalentes.

Las dependencias externas determinadas se detallan en los nuevos informes:

- [Castle134/19f/61c](../../../Client/docs/research/2026-11-18-cl9-castle-dependencias-asm.md).
- [Crywolf F8/C1A](../../../Client/docs/research/2026-11-18-cl9-crywolf-dependencias-asm.md).

Son trabajo de implementación, no sólo capturas VM. F8 usa60f/**0x19** (25decimal)
o0xb; el rótulo0x25 que todavía tiene un diagnóstico del código es incorrecto.
Los informes identifican constructores, pasos, dibujo, recursos, owners y pools.

También permanecen las fronteras de pila nativa, flechas protegidas, EDIT/IME XP
y pickingCL7.4. Ver [estado de los trece hitos](../../../Client/docs/research/2026-11-18-cl9-estado-integrado.md)
y [runbook VM](../../../Client/docs/research/scripts/game/capture/RUNBOOK-cl9-castle-crywolf.md).
La captura VM aún no se ejecutó. El siguiente agente debe mantener esos límites
explícitos y continuar por la fuente ASM, sin declarar éxito vacío en rutas normales.
