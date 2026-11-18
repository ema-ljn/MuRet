# Factibilidad de tres recorridos normales de Castle Siege

Fecha: 2026-11-18. Revisión estática de MuRet `f5e8fc45` y del cliente C32 (`c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a`). No se editó el código, no se ejecutó el original ni se lanzaron builds. Este informe no certifica implementación, pruebas ni cierre de CL11.

## Decisión

Los tres recorridos parecen reconstruibles con la información estática disponible. No encontré, en los subtipos concretos alcanzados por estos callers, un local de pila sin inicializar comparable con los obstáculos ya documentados de los fragmentos o del 147. Hay que implementarlos y probarlos: las etiquetas «CL11» actuales son cortes de implementación, no evidencia de una dependencia desconocida.

| Recorrido | Trabajo nuevo necesario | Recursos | Tamaño relativo |
| --- | --- | --- | --- |
| `134/0`, llegadas `42d513` y `42d5af` | Constructor, seguimiento de vida del dueño, activación por guerra, ruta BMD y carga Aurora | BMD y textura presentes; falta registrar la carga | Pequeño |
| `19f/0`, ataque `42dd9c` | Constructor y emisor de `12b/2`; conectar caller | Subárbol `12b/2 → 6ac/1` existente; no dibujar cuerpo19f | Pequeño a mediano |
| `61c/0`, ataque `42dd48` | Emisor padre, joint `61c/3`, joint `632/5`, onda `6eb/4`, altura del terreno | Texturas y dibujo base disponibles | Mediano; mayor que los dos anteriores juntos |

Orden recomendado: `134/0`, `19f/0`, `61c/0`. Mantener los tres como un lote coherente de dependencias de Castle, con pruebas focales separadas. No hace falta ampliar el trabajo a los subtipos hermanos no alcanzados por estos callers.

## Fuente y método

ASM: `<research>/client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/`. Funciones principales: `42d070`, `42dcf0`, `518cf0`, `5208a0`, `530860`, `535840`, `52d620`, `52dc20`, `6002e0`, `615700` y `60d5f0`. Los saltos y constantes se contrastaron también con `<extracción-local>/main-c32-text.bin` y las secciones de datos del PE, sin ejecutarlo. El pseudocódigo se usó para navegar; las observaciones sensibles a argumentos, RNG o temporales se revisaron en ASM.

### Reglas comunes que no se deben perder

- Tabla durable de efectos: primer slot libre de200, prefijo antes del caso y sin consumo del constructor si está llena. Conservar campos retenidos que el prefijo no reinicializa.
- `5208a0` consume un RNG común al entrar a cada efecto activo; no confundirlo con el RNG específico descrito abajo. Conservar movimiento, animación cuando corresponda y decremento final de vida.
- Actualización en orden físico de slots: un hijo creado en un slot posterior puede recibir su paso en la misma vuelta. No tomar una instantánea de la lista.
- `52dc20` consume su RNG común para un efecto activo y visible aunque el tipo no tenga dibujo particular. Omitir el cuerpo19f o61c no autoriza omitir esa parte común.
- Primer quad de los joints utiliza el ancho retenido antes del constructor particular; probar reutilización de slots además de pools nuevos.

## 1. CastleArrival `134/0`

### Caller y constructor

`42d513` y `42d5af` llaman `518cf0(134, actor.position, actor.angles, actor.light, 0, actor, 120, 0, 0, 0, 0)`. En el segundo caller el actor es modelo19c. El dueño es el ActorState durable, no una proyección temporal.

La tabla del constructor resuelve al bloque `51d714..51d73a`:

- Vida100; escala `MOVSX(key16) * float(6b86c0)`; blendMesh0 y blendLight0.
- Cero RNG específico. Con key120 el resultado guardado es **bits `3f999999`**, no el literal `1.2f` (`3f99999a`). Conservar el producto extendido y el punto de guardado.

### Paso

Bloque `52952d..52959f`:

- Lee el dueño de+108 y su byte0. Con dueño nulo o byte distinto de1 apaga el efecto; aun así escribe U y continúa la cola común.
- Con dueño vivo: `4294b0()` decide hiddenMesh−1 durante Siege y−2 fuera; vida2; blendLight `sin(clock * float(.001)) * float(.1) + float(.2)`.
- En ambos caminos U=`clock * float(.0005)`. Las constantes reales son las del PE; no sustituir por dobles decimales.
- **No copia la posición del dueño en cada paso.** El aura permanece donde se creó. No emite hijos. La vida viva termina en1 después de la cola común.

### Dibujo y recursos

`52d620` admite134 en el intervalo BMD[BE,150]. `6002e0` no tiene rama134: base modo2 y cola común `602365` de flags+78. Puede reutilizar la infraestructura del cuerpo de efectos de Castle y la publicación de pose, preservando el filtro alfa del wrapper `602e90 → 5ffdf0`.

Falta agregar134 a los archivos de modelos cargados y a la selección del renderer. `615700` carga **`Data\\Skill\\Aurora.bmd`** y primero asigna sus texturas desde **`Monster\\`**, REPEAT/NEAREST. Después vuelve a pedirlo en el loop Skill `616e20`; la búsqueda nativa por nombre reutiliza el slot ya asignado. No eliminar la primera llamada ni cambiarla por Skill.

Se descifró Aurora.bmd como datos con el K16 ya documentado por MuRet: **1malla, 1hueso, 1acción; textura `Monster81_R.jpg`**. Están presentes:

- `Client/assets/Data/skill/Aurora.bmd`.
- `Client/assets/Data/Monster/Monster81_R.OZJ`.

No depende de `Object3/aurora.OZJ`; el nombre del BMD por sí solo no identifica su textura. No hace falta inventar un slot fijo de textura.

### Estado reutilizable y prueba necesaria

ModelEffect ya tiene ownerContext, hasOwner, vida, transform, animation, objectState, U y flags78. Para leer vivo al ActorState, un helper de Scenes puede prestar el byte/estado del dueño mediante servicios; no reinterpretar ModelEffectOwner::effect como ActorState ni crear otro dueño duplicado en WorldState.

Pruebas: key signed y bits120, pool lleno0RNG, dueño vivo/muerto/nulo, war/peace, reloj/U, ausencia de seguimiento espacial, vida/animación comunes, carga real BMD+textura y render de hiddenMesh. Cierra la creación normal de los actores que actualmente aborta en esos dos Arrival.

## 2. Emisor `19f/0`

### Caller y constructor

`42dcf0` sólo participa en mapa30. Para `actor.word376 == 0x68` sólo crea el efecto cuando byte381==5. Copia XYZ del actor, suma500 a Z y llama19f/0 sin dueño y con key−1 en `42dd9c`; devuelve AL1 también cuando no dispara o el pool está lleno.

Constructor `51e4ba..51e4f5`: vida20, hiddenMesh3, speed bits`3ea8f5c3` (.3300000131), acción2 mediante4ea030 y ángulosXYZ0. Sin RNG específico. Subtipos distintos de0 no son parte de este caller.

### Paso y descendencia

`52aa6a..52aaff`: si vida%5==0, copia **primero** XYZ del efecto a un local, después modifica X con `rand()%100−50` y Y restando `rand()%30+15`; Z conserva el valor inicializado. Crea `12b/2` con ese punto, ángulos y luz del efecto, ownerNULL, key−1. El último salto pasa por el fade común `52725b`: vida<5 multiplica alfa por bits`3f44ec4f` (.76923078299), después la cola común. No se anima en el intervalo general porque19f está fuera[AD,151).

Emite en vidas20,15,10,5. Cada intento consume2RNG del emisor; si el hijo entra añade los3RNG del constructor12b/2. Pool hijo lleno conserva los2 consumos del caller. Ese `12b/2` ya existe en SiegeStoneEffects/SiegeStoneStep: fragmento, caída/rebote y emisión de `6ac/1`, también implementada. Revisar el orden dinámico entre padre/hijos, no rehacer esos kernels.

### Dibujo y recursos

**No agregar un cuerpo BMD al emisor19f.** `52d620` lo excluye y `52dc20` no tiene caso19f. Debe mantener sólo el consumo común del pase de suelo cuando corresponde. Los hijos12b/2 usan el renderer ya existente.

El slot de modelo19f corresponde a Monster78, ya cargado por WorldModels::LoadTheSpearman para mapa30, con archivo disponible `Monster/Monster78.bmd`; esto sirve a los actores. Su existencia no convierte al emisor en un cuerpo dibujable.

ModelEffect ya expone todo el estado que requiere19f. Pruebas: mapa/caller/paso5, posición+500, acción2, lista de cuatro emisiones, RNG con reservas0..4, orden hijo en la misma vuelta, alfa final y ausencia de draw19f. Cierra la descarga de piedras desde ese atacante del castillo.

## 3. Emisor `61c/0` y cadena de joints

### Caller y padre

`42dcf0`, mapa30 y `actor.word376 == 0x120`, llama61c/0 en **`42dd48`** con posición, ángulos y luz del actor, ownerNULL, key−1. Este caso no usa byte381 como gate.

Constructor **`51f15d..51f1e7`**:

- Vida20, velocity(0,−550,0), ángulos(−90,0,yawEntrada).
- copiedAngles(0,0,rand()%360): **1RNG**, incluso si el pool de joints está lleno.
- Joint `61c/3` en la posición, ángulos del efecto, dueñoNULL, ancho80, key−1; call `51f1c5`.
- Después del intento guarda la posición original en start+17c.

Paso **`52c19a..52c34e`**: para subtipo0 y vidas pares menores11 (10,8,6,4,2), pone ángulos(90,0,0), suma72 a copiedAngles.z, rota velocity por copiedAngles y suma start para obtener un local **completamente escrito**. Le suma a Z `rand()%400+700` y crea joint **`632/5`** con ancho110 y dueño=&efecto (call `52c266`, retorno52c26b). El RNG del punto se consume aunque esté lleno el pool de joints. Después conserva el movimiento/TTL común. La rama61c/1 y sus615/6 no son descendencia de este caller0.

El padre61c no dibuja cuerpo BMD. ModelEffect ya tiene start, copiedAngles y los demás campos necesarios.

### Joint `61c/3`

Constructor `530860`, caso61c compartido2..6 y ajuste3: vida20, máximo12, count−1, ancho80, textura remapeada a**60f**, speed10, velocity.x10.5, velocity.y0 del prefijo y velocity.z2, orientación0 y field9ec10. Guarda en+3c un vector(0,0,yaw). Sin RNG propio.

Paso `535840`, caso61c compartido2..6: velocidad+=velocity.z; velocity.z crece .5 con vida<15 o velocity.x antes. Cuando la cola alcanza máximo−1 se apaga tailEnabled; con vida<field9ec multiplicaRGB por bits3f44ec4f. El subtipo3 no toma los emisores particulares de los hermanos2 y4. Conservar movimiento/cola/TTL comunes y el primer quad de ancho retenido.

Dibujo: joint común con textura60f y orientación0; no requiere un nuevo renderer.

### Joint `632/5`

Constructor `530860`, caso632 `<4 || ==5`: vida40, máximo10, speed70, ancho110, key0, field9ec150; consume**1RNG de orientación**. El ajuste5 cambia textura a**60f**, máximo15, speed=`rand()%20+10` (segundoRNG), orientación2 y conserva los ángulos de entrada. Copia los ángulos a+3c. No omitir el primer RNG que después se pisa. Pool lleno:0RNG del constructor.

Paso `535840`, caso632 común `<4 || ==5`:

- Suma10 a speed; si tailEnabled agrega un tramo; suma otros10.
- Rota(0,−speed,0) por el vector de+3c y añade a la posición. Esto se suma al movimiento común inicial, no lo reemplaza.
- Consulta altura del terreno (`5f93c0`). Si Z<altura−150, fija Z enaltura−150. Si tailEnabled y key!=1 emite **6eb/4**, luz(1,.8,.3), ownerNULL; luego key1, speed−20 y RGB×bits`3f555555` (.83333331347).
- Un rand()%360 por paso para sprite60f de escala2.5. Los sprites llenos no permiten saltar ese consumo. Conserva cola/TTL común.

El dibujo existente ya admite orientación2 y su inversión de U. El owner del joint5 no se desreferencia en esta rama; puede conservarse opaco como dueño ModelEffect, sin leerlo como ActorState.

### Último hijo `6eb/4`

Constructor **`51f542..51f576`**: vida10, escala `(rand()%6 + 6.0f) * float(.1)`; **1RNG** si la tabla de efectos acepta el alta. Paso **`52c6c6..52c6d4`**: escala+=float(.3), clamp común; no aplicar el fadeRGB de los subtipos0..3. Actualmente CreateCastleShockWave implementa5..8, no4; GameModelEffects tampoco cierra4. Hay que extender ambos.

Su dibujo ya está en **GroundEffectDrawing::DrawMapDecal**, caso6eb, con escala y luz. El «pulse subtipo4» que aparece en DrawColoredDecal pertenece a61e, **no a esta onda**. No copiar esa lógica.

### Recursos y estado faltante

Ya están en tablas y assets las texturas60f (`Effect\\Flare.jpg`, LinearClamp) y6eb (`Effect\\ShockWave.jpg`, LinearClamp). Los joints concretos61c/3 y632/5 remapean a60f; no necesitan cargar una nueva textura632 ni BMD61c.

JointState expone speed, velocity, key, field9ec, orientación, texture, ancho, vida y cola. **No expone actualmente el vector nativo+3c.** `pathIncrement` representa+24 en los kernels existentes; no es un alias seguro de+3c porque son campos retenidos distintos. Si se implementa esta cadena, representar de forma explícita ese vec3 nativo con referencias ASM, sin aliasar un campo de otro offset para evitar tocar layout. Es el único estado de registro adicional identificado; no requiere inventar WorldRuntime nuevo.

Además, StepJoints hoy no recibe proveedor de altura. Debe prestarse la consulta real de terreno por callback o contexto de servicios añadido al final de la interfaz, junto con el acceso al pool de efectos para6eb/4. Esta es integración de servicios, no razón para inventar estado global. Los puntos del padre están inicializados; las ramas gigantes61c/0 del joint,632/6 y sus hermanos no son alcanzadas aquí y no se incluyen.

Pruebas: constructor padre con pools de joints libres/llenos, cinco puntos con bits nativos y RNG, 61c/3 en vida15/14 y saturación de cola, 632/5 con sus2RNG aun cuando se pisa la orientación, terreno plano/desnivelado, colisión única, sprite/pool lleno y reservas del hijo6eb, ancho retenido al reciclar, orden de movimiento común y específico. Cierra el efecto normal de ese atacante y su impacto; no certifica las otras ramas CL11 de los mismos tipos.

## Tamaño e integración estimados

Estimación de alcance, no compromiso horario:134 implica un kernel pequeño, dos callers de llegada y registro BMD/texturas/draw;19f otro kernel pequeño, un caller y reutilización del subárbol existente;61c exige un kernel padre, dos kernels de joint y un subtipo de onda más servicios de terreno/efectos. Un lote de orden **600–1000 líneas de implementación y 700–1200 de pruebas** es razonable, sujeto a cómo se separen los módulos y sin contar la documentación.134/19f podrían verificarse antes de arrancar la parte de joints.

La ruta61c toca JointState y por eso invalida objetos que usan su layout; integrar durante una compilación congelada sería contraproducente. Preparar el lote en worktree propio y hacer una única recompilación coherente después del candidato actual. Probar en Linux y Windows y después comparar capturas de la VM. No hay base para afirmar100% de estos recorridos sólo con esta revisión estática.
