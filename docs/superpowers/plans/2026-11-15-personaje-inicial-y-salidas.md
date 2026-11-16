# Primera entrada y salidas del cliente

Base: `main@9071f5f3` (#354). Emanuel reportó personaje invisible o mal formado al crear/entrar por
primera vez, imagen detenida al volver a selección y el motivo 2 de salida descartado. Pidió contrastar
ASM/pseudocódigo y publicar un único PR después de la verificación.

## Causas y contrato recuperado

Fuente primaria: `mu-server-repack-research-102i@14f0f931`, cliente c32bbd5f; exports individuales
`decompiled/functions`. ASM prevalece sobre pseudocódigo. No se ejecuta ni emula el original en este host.

| Recorrido | Evidencia y reparación |
| --- | --- |
| Primer ingreso | `633f91 → 5bbe10`, `5bbf73..5bbf8e`, `5bc092`: antes del reset parcial se vacían doce equipos y la mochila, sólo id/ocupación, y se cierra el registro propio. MuRet omitía esta llamada: id0 del arranque se proyectaba como modelo0x212 en las cinco partes. La segunda entrada encontraba ids vacíos por F303/F310 y por eso podía verse bien. |
| RNG y metadatos | El vaciado tiene su propio cierre de 3.848 CRT si profundidad=0; el reset parcial mantiene su cierre independiente. No se sustituye por ClearForInventoryReply: éste borra flags1b y otra grilla que 5bbe10 no toca. Las seis grillas todavía no modeladas permanecen como dependencias, sin declarar portada toda 5bbe10. |
| Lista y escena | `4c38eb` reinicia selección dentro de la respuesta de salida. Si F1021 y F300 llegan en un Poll, Enter no puede volver a borrar la recepción nueva. El estado se reinicia en el aplicador de retorno; el login conserva el reset anterior a su primer pedido. |
| Volver a servidores | `4c275c..4c2779` limpia sólo en escena5, antes del guard de cifrado. En claro se reporta F103 y se conserva la escena. Cifrado: `4c2f0a` cierra GS; `4c2f0f → 61cc50` limpia también desde selección; `4c2f1a..4c2f51` ordena login y reinicia globales. El estado Idle permite que LoginScene conecte al CS. La generación descarta el resto del lote viejo y su Closed. |
| Cursor y salida pedida | `4c3630`/`4c2774` devuelven el ítem del cursor antes de liberar actores. Se reutilizan ReturnTheCursorItem e InventoryServicesOf, conservando errores, prefijos y RNG. `4c273f` libera logoutRequested con toda respuesta legible, incluso clara o de motivo desconocido. |
| CI de main | La corrida43761845027 falla compilando test_input_events.cpp:115/130 por range-loop-construct con GCC15 y Werror. Se enlazan los pares por const&; no se desactiva el warning ni los tests. |
| Pila de los tests Windows | La misma corrida registra SegFault en cuatro InventoryReplyTest y ScreenshotsTest.AHeldFrameWithTheRequestCuts. Los cuerpos de prueba reservan 1.263.664–1.265.184 bytes al contener dos WorldState. Los cinco reproducen SIGSEGV en Linux con pila de 1 MiB. Los fixtures poseen ahora WorldState en heap, conservando referencias y comprobaciones. Los cinco pasan con ese mismo límite. |

No se añade un vestido final al F310 vacío: `4c5e71..4c5eb6` no lo hace. La reparación está en el writer
omitido de selección, no en cambiar el receptor de inventario. La liberación de 61cc50 no mata ítems del piso;
la de 61eef0 sí. Las dos rutas se distinguen en las regresiones.

## Verificación y revisión

Los tests iniciales observaron en rojo los doce ids/ocupaciones y RNG, lista perdida al entrar a la vista,
reset tardío de selección, F1022 claro/cifrado y logoutRequested. La revisión adversarial fresca Astra6high
detectó además el cleanup CPU de 61cc50 y la devolución del cursor; ambos tuvieron regresiones rojas.
Los fixtures que exigían conservar id0 al entrar a selección se corrigieron contra 633f91/5bbe10.

El test de primera entrada utiliza Data y modelos reales, crea un personaje clase0 por la sesión y comprueba
partes de clase y geometría emitida. Se reforzó para que F303/F310 atraviesen transporte, sesión y
SessionEventApplier y para exigir InWorld; el publisher directo de la primera versión del test no lo acreditaba.
Se verifica inventario vacío y sólo mochila. También se prueba una salida con ítem levantado después del pedido.

La revisión adversarial acotada posterior levantó ambos P2 sin nuevos Critical/Important. No ejecutó builds
ni tests por su cuenta. El build ALL termina en exit0 y una compilación posterior confirma que no hay
trabajo pendiente. El build local usa GCC14, Debug, warnings como errores y headers precompilados apagados.

La corrida final completa con Data y los corpus privados pasa los 7.014 casos sin GL, sin omitidos,
en 288,18 s. Los 106 tests GL pasan con Xvfb, software GL y contexto obligatorio, también sin omitidos:
7.120 casos en total. Esto incluye los tres de primera entrada/salida, los diecisiete del applier y
los trece del pipeline de selección. El esperado de RNG de ese pipeline incorpora el cierre de
3.848 CRT recuperado de 633f91/5bc092; su estado final se verificó también con el LCG independiente.
Los cinco casos que desbordaban la pila pasan además con el límite de 1 MiB.

Formato18 e include-cleaner18 cubren los 24 archivos C++ modificados/nuevos sin hallazgos pendientes.
El resultado de Windows/MSVC y del CI remoto corresponde a la corrida del PR, publicada en su descripción;
los resultados locales no acreditan una ejecución de Windows ni de la VM original.

## Repetición posterior en el cliente/VM

1. Desde proceso nuevo: crear clase0, entrar con inventario vacío o sólo mochila y comprobar cuerpo/animación.
   Repetir borrando/recreando el mismo lugar y con un personaje existente en el primer ingreso.
2. Volver a selección con F1021/F300 agrupados y separados: lista visible, sin retención indefinida del frame;
   elegir y entrar otra vez. No esperar una segunda lista que ya llegó.
3. Join another server desde juego y selección: cierre del GS, escena2, nueva lista del CS y login utilizable.
   Diferenciarlo de Exit game (motivo0).
4. Devolver el cursor antes de soltar actores; comprobar orden y RNG con la fuente. Un F1022 claro conserva
   su limpieza previa, sin cambiar de escena.
5. Registrar SHA del cliente, commits, pasos y evidencia privada según el
   [runbook de CL9](../../../Client/docs/research/scripts/game/capture/RUNBOOK-cl9-vm.md).
   Esta entrega no certifica una nueva sesión del original ni cierra toda CL9.
