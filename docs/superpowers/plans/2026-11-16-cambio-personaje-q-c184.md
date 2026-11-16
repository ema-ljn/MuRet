# Cambio de personaje con equipo vaciado: plan y verificación

> Para agentes: ejecutar con `superpowers:executing-plans`, conservando los pasos comprobables.

**Objetivo:** evitar el cierre al crear un mago después de volver de un caballero equipado.
**Arquitectura:** conservar los writers de selección/F303/F310 y resolver las contribuciones que
el ASM demuestra nulas en sus consumidores. Mantener estricta la lectura directa de ItemStatBonusOf.
**Tecnologías:** C++20, GoogleTest/CTest, GCC/MSVC, clang-format18 e include-cleaner18.
**Contrato:** [stats propios](../../../Client/docs/research/06-render-escenas-y-ui.md#los-stats-del-registro-propio),
Q-C184/Q-C192; exports del cliente c32bbd5f en `mu-server-repack-research-102i@14f0f931`.
Base: `main@985496cd`, que ya incorpora #355; su CI Linux/Windows terminó en verde.

## Restricciones y revisión

- ASM prevalece sobre pseudocódigo. No ejecutar el original ni sus DLL en este host.
- No borrar durabilidad, flags ni opciones residuales: el original los conserva al vaciar equipo.
- Ambos consumidores de 40dc50 deben resolver el valor cero; un arreglo en uno deja el otro corte.
- Conservar las doce aperturas/cierres del registro, incluso en el camino de contribución nula.
- Con bono distinto de cero, conservar Q-C184: el selector desconocido sí puede afectar los stats.
- En los lugares 1..7, IDffff devuelve AX=0 desde 54e680 antes de leer opciones; el aporte al WORD
  sigue siendo cero para todos los factores finitos de 54ee00. No sustituir la durabilidad base por cero.
- Probar transporte, sesión, aplicador y escenas con Data/modelos reales. El test integrado controla
  el cambio de escenas: no acredita SceneManager, WorldLoading, GPU real ni reproducción en VM.

## Tarea única: contribuciones nulas y regreso con otro personaje

**Archivos:** OwnDerivedStats.h/.cpp para el valor de 40dc6e y su consumidor; OwnItemSets.cpp para
el consumidor 40e033; OwnStatHelpers.cpp para el consumidor de 54e680. Regresiones en
test_own_derived_stats, test_own_item_sets, test_own_stat_helpers, test_inventory_reply y
test_character_first_world_entry. Sin dependencias nuevas.

- [x] Reproducir en rojo cuatro regresiones: stats, copias/RNG, word62 y caballero→selección→mago.
  La última reproduce literalmente `550d10 (550d24): 40de10, lugar 0: 40dc50, tipo -1, Q-C184`.
- [x] Añadir ItemStatBonusValueOf con `(flags1c >> 2) & 3`. En ambos consumidores, IDffff con valor
  cero no requiere conocer el selector. La comprobación de las copias queda dentro del acceso lógico.
- [x] Resolver la contribución nula de IDffff en AddSlotsToWord62, conservando los otros pasos.
- [x] Revisión adversarial fresca Astra6high: sin Critical/Important de implementación.
- [x] Build completo, cuatro regresiones y suite local; formato e includes de los nueve archivos C++.
- [ ] Repetir la secuencia en VM; el CI remoto de Linux/Windows queda enlazado en el PR.

## Evidencia y límite

40dc6e calcula el valor; 40dc88 lee el selector. Los únicos dos CALL directos encontrados son
40e033 y 550d6d: sus cuatro ramas suman `5 * valor` a un WORD y las otras no suman. Con valor cero,
los 256 selectores producen el mismo estado. Se resuelve esa rama en los consumidores; la función que
promete devolver el selector real sigue exigiendo el byte. El criterio se limita al sentinel IDffff.

54e686..54e690 devuelve AX=0 para IDffff y conserva el residuo alto de EAX. 550ae8..550aee enmascara
sólo el multiplicando a 16 bits; 550afb..550b00 conserva el residuo alto al acumular en 32 bits.
Ese residuo es múltiplo de 65.536: 550b17..550b1b publica únicamente DX y no cambia el WORD final.
54ee00 sólo devuelve 0, 0,2, 0,3 o 0,5 bajo el perfil FP enmascarado del cliente. La contribución al
WORD es nula sin depender del byte de durabilidad base leído en `base(Item.bmd)-0x27`.
Se consultó Context7/cppreference sobre las conversiones y aritmética unsigned módulo 2^N de C++20.

Las cuatro regresiones tuvieron ciclo rojo→verde. El build completo con GCC14, Debug, warnings como
errores y PCH apagados termina en exit0. La corrida final pasa 7.018/7.018 casos sin GL (326,79 s) y
106/106 gráficos con contexto obligatorio y Xvfb (15,97 s): 7.124 tests, cero fallos y cero omitidos,
con Data y los corpus privados disponibles. clang-format18 e include-cleaner18 verifican los nueve
archivos C++ modificados. El CI remoto de Linux/GCC15 y Windows/MSVC se consulta en el PR; el verde
anterior de #355 no se atribuye a esta corrección. La prueba integrada verifica el slot de selección
con ChosenCharacter(): el lugar del héroe en el pool se elige por rand() y es otro dato.

Esta equivalencia cubre el estado portable y RNG; no reproduce los accesos inválidos ni las banderas FP.
`s31_item.bin` empieza en la propia tabla y no captura su prefijo: P-C38(a) sigue abierto para los casos
con bono no nulo. Menor diferido de la revisión: añadir un caso de suma con slot vacío de valor cero
seguido de un ítem válido con bono. La revisión fue estática; no ejecutó builds ni tests.

## Repetición en VM y captura pendiente

El [handoff y programa de captura](../../../Client/docs/research/scripts/game/capture/RUNBOOK-qc184-vm.md)
prepara dos modalidades de WinDbg y una lectura pasiva previa/posterior a adjuntar. Es la entrega
operativa para el agente con VM; la preparación/análisis del host no acredita una ejecución nativa.

1. Con proceso nuevo, crear/entrar con caballero, equipar arma ordinaria, volver a selección, crear
   mago en otro lugar y entrar. Comprobar cuerpo, animación y que no se cierre por Q-C184.
2. Repetir con mochila, armadura/escudo y elecciones de personajes existentes. Conservar SHA y log.
3. Para P-C38(a), en el original c32bbd5f con ImageBase 0x400000, capturar en WinDbg antes y después
   de cambiar personaje: `dd 07758550 L1` y `db poi(07758550)-40 L40`. La región de 64 bytes contiene
   los offsets −0x10 y −0x27; no está en el volcado que comienza en la tabla.
4. Un breakpoint en 0040dc50, antes del PUSH EBP, permite registrar el ítem con
   `db poi(@esp+4) L20`: incluye ID, durabilidad y flags1c. Registrar también el caller y ambos
   valores de lectura. Los volcados permanecen privados; devolver sólo hashes y el informe.
5. Seguir el [runbook de CL9](../../../Client/docs/research/scripts/game/capture/RUNBOOK-cl9-vm.md).
   No ejecutar el original ni sus DLL fuera de la VM. Esto no cierra toda CL9.
