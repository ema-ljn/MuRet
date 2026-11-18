# CL11 requerida por Crywolf/Fortress: factibilidad de F8 y C1A

2026-11-18. Auditoría estática inicial del snapshot R4 `f5e8fc45`. Las tablas registran el comportamiento del original y las diferencias observadas en R4. La implementación posterior de H11 conectó estos llamadores con señales propias. Los contratos implementados y las diferencias de trayectoria, geometría, emisiones y RNG figuran en el [ADR 0020](../../../docs/adr/0020-senales-crywolf.md).

Fuente nativa: exports `*.asm` de `mainexe-vmxp-c32bbd5f/decompiled/functions`, pseudocódigo sólo como índice. Cuando el export ASM omite bloques intercalados, se desensambló estáticamente el tramo de `main-c32-text.bin`, VA base401000. PE c32 sólo leído como bytes de datos para constantes y tablas; no ejecutado, cargado ni emulado. SHA de referencia: c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a.

## Decisión práctica

Las dos familias son **finitas y determinadas** en estas rutas normales. No apareció pila indeterminada ni un nuevo constructor desconocido en su descendencia requerida. C1A es un cierre pequeño que puede abordarse ahora. F8 es abordable como una entrega focal posterior, pero incluye un kernel de joints, owners mutables/choque, un efecto hijo y un recurso de animación; quitar dos cortes no lo cierra.

Estimación pendiente desde este informe, por un implementador con RED/GREEN, revisión ASM y pruebas reales:

| Familia | Producción orientativa | Pruebas orientativas | Trabajo focal restante |
| --- | --- | --- | --- |
| C1A/0 y ambos callers | 80–140 líneas, 3–5 archivos | 180–300 líneas | 1,5–3 horas |
| F8/1, 60f/19 y /b,105/3,610/0..3, recurso y conexiones | 300–550 líneas, 7–10 archivos | 400–750 líneas | 4–7 horas |
| Entrega única de ambas, con revisión cruzada | Sin expansión de WorldState necesaria | Casos del caller hasta paso/dibujo | 6–10 horas más build/CI integrado |

El tiempo de build/CI depende de la cola compartida y de qué headers se amplíen; reservar 45–120 minutos adicionales para recompilación integrada y revisión de sus resultados. No propongo mezclarlo con la cabeza verde que root está publicando. Conviene rama aislada y commits tests/producción revisables, preservando R4. Las cifras son presupuesto de implementación/validación, no una promesa de que los proveedores todavía no portados ya funcionen.

## F8: contrato y grafo completo requerido

```text
43257a Crywolf / 42fd76 Fortress
  F8/1, owner=actor, key=WORD actor+8a
    ctor51a2bd..51a417 -> TRES60f/19 (25 decimal), skill1 sólo el del medio
    step523f33..524067, vida13 -> 105/3 (owner=F8)
      ctor519fc9..51a10e -> CUATRO610/0,1,2,3 (owner=105)
      step523b8b..523d35 -> sigue alF8; sin hijos nuevos en mapas34/35/sub3
    60f/19 -> 5fd + 5ac + 5ac sprites, choque520170 con skill1
              vida1 -> partícula5ed/1, escala0.6, blanco
    610/0..3 -> geometría de cola; no hijos
  Variante F8 no1 del proveedor compartido -> CUATRO60f/b, skill1 sólo el índice1
    mismo paso orbital, sprites; no explosión terminal /b
```

**Corrección de base:** `PUSH0x19` en51a39c es25 decimal. El rótulo actual `60f/0x25` de ArrowEffects es erróneo. Eso no convierte el corte en obsoleto: el kernel actual sólo crea/pasa60f/0,1,a,12. Tampoco acepta un ModelEffect como owner de60f: el guard de `CreateFaunaJoint` sólo permite619/1 y610/13. Se comprobaron ambos puntos; no se dedujo dependencia por el rótulo.

| Etapa | ASM/VAs | Contrato / reutilización |
| --- | --- | --- |
| Callers | 432553..43257a;42fd43..42fd81 | Actor tipo1ad, etapa14. Posición+10, ángulos+1c, luz+f4, owneractor, keyWORD+8a y argumentos restantes0. Todo determinado. Reemplazar ambos cortes por proveedor compartido, conservando salidaAL1 y umbral del caller de ataque. |
| Prefijo/altaF8 | 518cf0;51a2bd..51a417 | `ModelEffects::Begin` ya porta pool/prefijo. Vida30, blendMesh−2, escala1, z+130, velocidad(0,−60,0), start+17c con boca rotada(−10,−100,15). **Escribe también los ángulos del caller**: x/y0,zdel efecto; el wiring nuevo debe prestar `&actor.angles`, no sólo copiar angles al argumento. Final copia cuenta propia por51a1b0..51a1b6. |
| AltasjointF8 | 51a382..51a3c9;51a3d0..51a417 | Tres intentos60f/19 enF8/1, cuatro60f/b en otra variante; ownerF8, ancho50,key−1, skill1 únicamente índice1, serial0. Cada alta efectiva consume un rand de giro; lleno no. Deben intentarse todos, incluso si sólo quedan1–2slots. |
| Constructor60f/b y19 | 533677prefijo60f;533d5f..533e0c;5342be..5342cf | Vida30, máximo15, ancho30, velocidad0 heredada, giro random%3000 envelocity.x, field9ec0, target=segundo punto. Colores /b(.2,.2,1), /19(.9,.4,.6). Todo cabe enJointState actual; FirstQuad debe preceder las escrituras de ancho como en el prefijo actual, preservando ancho retenido del slot. |
| Paso60f/b y19 | 53a87a..53b01f;53b318..53b366;53c393..53c3c5 | Siowner muerto:alive0/life−1. Lee contador protegido7d3a0f0, índice físico del joint, paridad y aritmética32wrap para la fase; conserva puntos de redondeo de sin/cos. Interpola/oscila centro y ángulos, escribe owner.position en skill1 y llama520170. Tres sprites con seis rand aun si el pool sprites está lleno. /19vida1 emite5ed/1 blanco conangles0/escala.6; /b omite esa explosión. Cola/life común todavía corre. |
| PasoF8 | 523f33..524067;52ccbe..52cd3f | ArrowEffects ya cubre yaw+60 en/1, luz y choque. Falta crear105/3 al llegar vida13 y mantener orden: emisión antes del choque/luz y cola. La cola deF8 mueve start+17c, no position (52cd0c..52cd39). El StepArrow actual termina en StepModelEffect genérico, que mueve position: esta excepción falta y debe corregirse como parte del cierre, incluso con pool joints lleno. |
| Constructor105/3 | 519fc9..51a10e;tabla datos51fbb8 | Tabla3→51a01b (leída delPE como datos). Vida100, blend0,light1,turnDirection0, escala10,light(.9,.4,.6), start=owner.position. Cuatro intentos610/sub0..3, owner105, ancho10, key30, skill3, serial0; sonido7a y escala final1 incluso con pool joints lleno. Si no hay slot de modelo, no entra al cuerpo ni al sonido. No cierra registro propio como una flecha. |
| Paso105/3 | dispatch52cef4/52ce14→523b8b;523b8b..523d35 | OwnerF8 muerto ponevida−1; si vivo incrementa turnDirection, copia owner.angles aangles/copiedAngles, conserva posición anterior enstart y sigue owner.position. Sólo subtipo1 crea614/3 ysprite: **no alcanzable desde105/3**. Efectos especiales de mapa Kalima24..29/36 tampoco alcanzables en34/35. Cola común animación/vida sí corresponde. |
| Constructor610/0..3 | 531c43..531d1f;tabla534e60 | Vida=key30, key0, máximo50, selector color3 leeowner105.light; ancho20 en tramo común. Subtipos ponen phase(speed)+9d8=0/90/180/240 yvelocity=(argumentScale×const6b8b94,0,0). Sin RNG. Campos ya disponibles. |
| Paso610/0..3 | 537d46..537eec;53c393 | Leeowner vivo/position/start/copiedAngles, tres subpasos de interpolación/rotación yAppendJointTail; fadeRGB envida<15. Conowner muerto fija life−1 y conserva centro anterior con z+50. Después corre cola común (cuartoAppend) y decremento, **sin saltar esa cola**. No crea efectos/partículas. |
| Dibujo | 52d620;53c410 familia | F8 y105/3 omiten cuerpo nativamente; los joints van porDrawJoints común. `GameJointDrawing` ya tiene blend, puntos, caras/UV y media textura para /b(11decimal); probar /19 y610/0..3. Sprites y5ed usan providers actuales. |

Reutilizable: WorldMap.effects estable, WorldState.DrawTail().joints estable, ModelEffect.start/copiedAngles/ownerContext/state/byte89/word8c, JointState completo, OriginalAngleMatrix/VectorRotate, AppendJointTail, FrameSprites, Particles5ed, TerrainLighting, ClothWorld.logicSteps/openCounterAccesses y LogicalAccess.SmallLogicalCloseRandoms=4.

Nuevas conexiones concretas de F8:

- Tipar correctamente el ModelEffect como owner de60f,105y610/0..3; no reinterpretar comoActorState/FaunaObject.
- Prestar el choque520170 al paso de joints. Hoy StepJoints recibecloth,particles,sprites,lighting,sound, pero noWorldState ni los servicios deArrowStepContext; `HitWithTheArrow` es privado deArrowEffects. Hace falta un hook focal o helper compartido con recursos reales. Sin ese hook los joints se verían pero no completarían impactos.
- Reutilizar infraestructura PC53 y wrap32; JointDirectionPC53 actual fija rate0.5. Las constantes de /b y /19 difieren: no reutilizar ciegamente esa función con sus coeficientes actuales.
- Mantener orden actual de escena: modelos antes de joints; los hijos posteriores en pools se pueden procesar el mismo tick según su índice. Probar owner que muere en impacto y qué joints siguientes lo leen.

### Recursos F8 y ajuste de dibujo necesario

Texturas ya cargadas:60f `Effect\\Flare.jpg` LINEAR/CLAMP;610 `Effect\\Flare02.jpg` LINEAR/REPEAT;5fd `Effect\\Shiny02.jpg`;5ac `Effect\\flare01.jpg`;5ed `Effect\\Explotion01.jpg`, las últimas LINEAR/CLAMP. No nuevos assets binarios ni slots fijos.

**Recurso faltante real:** modelo105 `Data\\Skill\\Piercing.bmd`, Native61669e→6166a3 (60da30), texturas `Skill\\` NEAREST/REPEAT616e3e→616e46. Aunque no se dibuje su cuerpo/3,52cc04 avanza la animación del BMD. No aparece105 en las listas GameModels/SkillModelFiles/SiegeModelFiles actuales. Incluir carga y prueba de metadata/animación real; no certificarlo sólo con skeleton inventado en test.

No se encontró cargaF8 en615700 revisado. El modelo vacío nativo no es una indeterminación:4ec20c..4ec214 devuelve1 si actionsWORD+26==0, sin leer el puntero de acciones. No inventar un BMD F8.

Hay además un corte artificial del dibujo actual que debe entrar al cierre: `DrawGameModelEffectsChecked` pide `models.Find` en modo1 **antes** de comprobar `OmittedEffect`; F8 blend−2 sin BMD alcanzaría error52d68c. Native52d68c lee meshCountWORD+24 del headerBSS0, la comparación firmada0<−2 es falsa y luego omiteF8. Probar ambos modos con el modelo omitido y conservar `seen`; resolver esa lectura vacía sin introducir un asset ficticio.

## C1A: contrato y descendencia

```text
4329db (Crywolf) / 4f6780 (skill50 del ataque)
  18 intentosC1A/0, ánguloZ=actor.z+i×20, ownerNULL,key−1
    ctor51f84f..51f8a7, sin hijos/RNG propio
    step52c9f2..52caa0: si z<terrainHeight
      5ed/0 blanco en posición+(0,0,80), escala1
      seis intentosC8/C9/0 (cada selecciónrand&1 antes del alta)
        CreateStoneChip actual -> paso/rotación/rebote actual
          Fire5d9/sub1..3 ocasional, providers existentes
      C1A.alive0
    luzcomún521216 y cola52cb98 todavía se ejecutan
    drawStaff09 existente
```

| Etapa | VAs/campos | Estado real |
| --- | --- | --- |
| Callers | 4325a6..4329ee;4f6734..4f6791 | Ambos comprueban etapa14, WORD376=3b y skill protegida50. El ayudante Crywolf devuelve AL=0 tras su tanda y permite que corra la segunda tanda en el paso común; el salto `4f6791` alcanza la cola de ataque. Cada loop18 ejecuta todos los intentos, incluso con el pool lleno. Sin RNG entre ángulos salvo las lecturas protegidas. Constructor recibe posición, ángulos y luz determinados. |
| Constructor | 51f84f..51f8a7 | Vida30,blend−2,scale1,z+=280(6b8c78),pitch+=20(6b86d0),velocity(0,−80,−10). **−80 esbitsc2a00000 en51f88e; la lectura preliminar del pseudo −0.3125 era incorrecta.** PrefijoModelEffects::Begin ya pone velocidad0 antes de estas escrituras. Sin RNG propio. |
| Paso/contacto | 52c9f2..52caa0 | Height5f93c0; estricto height>z, unordered no impacta. Color5ed está inicializado a(1,1,1) en prologo520921..52092f, no es local indeterminado. Posición explosión z+80(6b9ffc). Seis seleccionesrand&1+C8 y seis altas con owner0/key−1; CreateStoneChip existente5RNG por alta efectiva. La selección se consume aun cuando el pool ya está lleno. |
| Cola posterior | 521216..52124e;52cb98..52cd94 | Colorluminosidad(.2,.4,1), radio2, luminosidad calculada porrand inicial yfadevida. El branchimpacto apagaalive **pero no salta la luz ni el movimiento/decremento de cola**. Un wrapper que salga poralive0 sería distinto. C1A fuera[ad,151) no animaBMD en la cola;sí gira/suma velocidad. |
| Hijos | 5ed/0;C8/C9/0 | 5edconstructor/paso/atlas/dynamiclight ya existentes;sub0 no tiene hijos. C8/C9 coinciden conFirst/SecondStoneChipEffect,CreateStoneChip yStepStoneChip actuales; fuego5d9/sub1..3 ya aceptado/stepped/dibujado. No nuevo constructor pendiente en esa descendencia. |
| Dibujo/recursos | GenericEffect incluyeC1A;PlayerModels.cpp322 | Staff09 se carga como Item\\Staff09.BMD, texturaItem existente. C8/C9 Stone01/02 ytexturasSkill cargadas porGameModels.cpp55..56/77..78. 5edExplotion01 y5d9Fire ya enTextureSets. No slot fijo niasset nuevo requerido. |

Los servicios actuales `ModelEffectStepServices` ya prestan effects,terrainHeight,lighting. ModelEffect tiene todo el estado; no hace falta duplicar contador/RNG ni ampliar WorldState. Los dos callers deberían usar un solo constructor C1A canónico, mantener las18tentativas y la lectura protegida previa.

## Límites de la evaluación y pruebas necesarias antes de declarar cierre

No se encontró indeterminación en los constructores requeridos. Persisten trabajo de implementación y validación, especialmente los redondeos PC53 de60f/b/19, la metadata realPiercing, el filtro de dibujo modo1 y el wiring del choque desdejoints. No se ejecutó el original para resolver nada.

Pruebas significativas propuestas (no tests de texto fuente):

- CallerF8 en34/35 porStepTheAttack, contador/tipos cercanos/skill, owner/key/ángulos del caller y prefix/RNG; pools modelos/joints vacíos, parciales y llenos.
- Tres altas60f/19 ycuatro/b con índice1skill1; geometría/ownerposition/choque yCRT con contador libre/anidado, índicespares/impares ywrap32; spritesllenos conservan rand ya extraído. Owner muerto, life1 yexplosión5ed/1.
- Vida13 delF8 crea105/3 una sola vez, cuatro610/0..3, sonido7a aun sin joints, sin sonido con poolmodeloslleno; siguiente tick lee posiciones/start/copiedAngles correctos. Piercing carga metadata real. Draw comúnF8/105 omitido enmodos0/1 yDrawJoints produce vértices/UV/texturas nativos.
- C1A callers generan18ángulos y18altas, con huecos parciales/fullpool conservan intentos/RNG. Vuelo/impacto/NaNaltura, explosiónblanca yseisC8/C9, luminosidad ycola incluso después dealive0; dibujo usaStaff09 real. Descendientes avanzan/dibujan sin un nuevoCut.

Con esas pruebas y revisión ASM se puede cerrar esta dependencia normal de CL9 sin abarcar todos los subtipos de CL11. Quedan fuera los otros efectos CL11 no alcanzados por estas dos familias; no se confunde ese límite con declarar CL11 completa.
