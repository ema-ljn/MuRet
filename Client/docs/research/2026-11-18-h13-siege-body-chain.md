# H13: cuerpos del asedio y efectos por vértice

Reconstrucción didáctica estática del `main.exe` SHA-256
`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`.
Fuente: exports `0042e3d0`, `004ee770`, `00518cf0`, `005208a0` y lectura del `.text`
con VA inicial `0x401000`; constantes del PE leídas como datos. No se ejecutó el PE.
El ASM prevalece sobre los nombres/cases del pseudocódigo.

| VA original | Implementación | Contrato reconstruido |
| --- | --- | --- |
| `42e3d0..42e968` | `Scenes/MonsterBodyChain.cpp` | Cinco tipos del mapa 30: `19b`, `19c`, `1a8`, `1f8`, `1ff`. Retornos, pases, RGB, mutaciones de vida, sonidos y RNG. |
| `4ee770..4ef079` | `Scenes/VertexEffects.cpp` | Selectores `10a/0`, `12d/0`, `12e/0`, `12f/0`, `131/0`; triángulos y esquinas en orden, sin deduplicación. |
| `51d2a9..51d5b7` | `World/SiegeVertexEffects.cpp` | Constructores `12f/130` y `12d/e` subtipos 0/1 alcanzados: ocho sorteos, con rotación anterior a los tres ángulos finales. |
| `51d5b8..51d713` | `World/SiegeVertexEffects.cpp` | `131..133`: ocho sorteos; nueve para `131/0`, que pasa a subtipo 1. |
| `51bafb..51bc42` | `World/ModelEffects.cpp` | Reutilización del constructor existente de `10a/b` para los hijos del guardia. |
| `5291ce..529304`, `5293ef..529528` | `World/SiegeVertexEffects.cpp`, `World/GameModelEffects.cpp` | Movimiento, rozamiento, gravedad, rebote, desgaste, inclinación y alpha específicos; prólogo y epílogo comunes. |
| `52cbf4..52cd94` | `World/GameModelEffects.cpp`, `World/ModelEffects.cpp` | Sorteo inicial, animación, segundo movimiento con velocidad girada y decremento de vida. |

`19b` vuelve con falso incluso después de dibujar o de morir: el caller ejecuta su
cuerpo común. `19c` sin asedio vuelve con verdadero sin dibujar. No se interpreta
el retorno como una afirmación de que se emitieron vértices.

La catapulta `1f8` dibuja el modelo `12b` (`5769ad8 + 105a0`, stride `e0`), no un
registro del pool. Usa escala 1,3 y hueso 12 si la acción es 0 o el frame está fuera
`[2,22)`; usa el hueso 44 en la acción 1 con `13 < frame < 22`. Se conservan las
ramas unordered del ASM. `42e458` reconstruye el color de `5ffce0` para la piedra,
antes de los tintes de `flags78` aplicados al cuerpo principal. El scratch de la
piedra queda publicado para la cola del caller.

`4ee770` lee count de esquinas como byte con signo e índices como WORD con signo.
Cada esquina consume sus propios gates. Un pool de modelos lleno omite solamente
el constructor: no elimina los siguientes sorteos ni las siguientes esquinas.
Los constructores de `131/0` consumen el noveno sorteo antes del gate siguiente.

## Frontera indeterminada preservada

`529516..529520` y `525302..525310` pasan a la partícula `5f3` una posición
`LEA [EBP-28]`. El prólogo y las rutas hacia esas llamadas no inicializan esos tres
floats. El puerto conserva el prefijo completo y el sorteo `rand()%10`; cuando
queda lugar en partículas devuelve la VA precisa, sin inventar la posición.
Cuando las 3.000 partículas están ocupadas, `53fe70` retorna antes de leer ese
puntero y el puerto completa la animación, el segundo movimiento y la vida.
Esto requiere evidencia dinámica de la VM para resolver el contenido de la pila.

## Verificación

Tres regresiones de `test_monster_body` se observaron rojas por el corte `42e3d0`
antes de implementar: retorno falso de `19b`, `19c` fuera de batalla y mallas de
`1a8`. Los tests nuevos también observan muerte/sonido/pool lleno, selección de
huesos y bordes 2/13/22/NaN, RGB de overlays, textura del agua, esquinas repetidas,
RNG del constructor y movimiento doble.

Las unidades puras se compilan con GCC 14, C++20, `-ffp-contract=off` y warnings
como errores, reutilizando sólo dependencias sin `WorldState` del archivo local.
La integración completa, las cargas de recursos y el renderer de los hijos se
verifican en el worktree del integrador; este informe no acredita comparación
dinámica con el ejecutable original ni subtipos 2/3 de `12d/e` de otros callers.

Resultado local final: **47/47** pruebas de `MonsterBodyTest`, `VertexEffectsTest`
y `SiegeVertexEffectsTest` en verde. Compilación de los `.cpp` modificados con GCC
14 y warnings como errores; `clang-format` 18.1.8 y `misc-include-cleaner` de
clang-tidy 18.1.8 sin hallazgos en los archivos modificados. El factor óseo
compartido de la catapulta también se prueba con 1,2; no se sustituye por 1 en el
accesorio. El SHA-256 del PE estático se comprobó con `sha256sum`.
