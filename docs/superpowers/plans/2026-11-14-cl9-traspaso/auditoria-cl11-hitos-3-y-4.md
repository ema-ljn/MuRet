# Auditoría de los cortes CL11 de los hitos 3 y 4

Auditor read-only, 2026-11-14. Worktrees: cl9-h3 @ aaf97eea, cl9-h4 @ aa1e5fa2 (nada editado ni compilado). Método:
cada corte con CL11 o 0x518cf0/0x530860/0x5208a0/0x53fe70/0x51fff0 cuya línea `git blame` atribuye a un commit
"CL9 hito 3"/"CL9 hito 4"; el ASM del .asm decompilado y, donde Ghidra no siguió la tabla (0x5208a0, 0x518cf0 tarde,
0x4fd5f0, 0x4dbf50, 0x52b984), `objdump -d` del PE. Las clases que MuRet ya crea salen de World/Particles,
Scenes/FaunaJointCreation y World/ModelEffects de cl9-h4 (y cl9-h2/h3).

Veredictos: **T** CL11 verdadero; **P1** patrón 1; **P2** patrón 2 (clase ya portada); **P2m** patrón 2 menor (la
alta es portable pero su paso corta en el cuadro siguiente con una clase CL11: el cliente cierra igual, un cuadro
después y con otra etiqueta); **R** rand() distinto antes del corte; **F** pool lleno (ver la nota general).

## Resumen

| Hito | Cortes | T | P1 | P2 | P2m | R |
|---|---|---|---|---|---|---|
| 3 | 83 | 71 | 0 | 4 | 1 | 7 |
| 4 | 79 | 68 | 0 | 6 | 4 | 1 |

F es sistemático (todos los `CutAt`/`Core::Unexpected` de alta de 0x518cf0/0x530860) y no se cuenta por línea.

## Hallazgos que no son CL11 verdadero, de más grave a menos

### H4-1 (P2, alto). La Flame (skill 5): 0x5de/0 con dueño — AttackSkillCases.cpp:316, 0x500531

- Alcance: toda Flame que se ve en juego normal (la skill de mago más común en los mapas 0..10), propia o ajena.
- Original: el punto de la celda (+0x39c/+0x39d, altura 0x5f93c0), 0x59fb80(5) y
  0x518cf0(0x5de, punto, o+0x1c, o+0xf4, 0, dueño o, o+0x8a, lugar, 0, 0, 0) → caso 0x51e7d8: vida 40 y la copia del
  registro propio +0xf04→+0x8c (abre/cierra). Si o es el héroe, abre/cierra del registro propio (con sus rand()) y el
  sonido 0x41ed10 en 0x500c03. El paso es el de StepFlameJet con el subtipo 0 más la rama del dueño 0x52ae2a: con el
  dueño = héroe y vida % 20 == 0, 0x517a20(+0x89, posición, 150,0f, +0x8c, +0x8a, 0).
- MuRet ya tiene: `World::ModelEffects::CreateFlameJet` (la misma alta sin dueño), `StepFlameJet`
  (World/GameModelEffects.cpp) y `SkillHits` (el port de 0x517a20, hito 4).
- Cambio: crear con `Begin(ModelEffectArguments{owner, key = o+0x8a, place})` + vida 40 + la copia del registro como en
  CreateFlameJet; agregar a StepFlameJet la rama del dueño que llama a SkillHits; el sonido y el registro del héroe.

### P2 (medio). Skill 0x35 (Greater Defense de la elfa en blanco): 0x5ac/2 — SkillReplies.cpp:527, 0x4cdc88

- Alcance: cada 0x35 que llega sin 0x400 en el blanco (buff de elfa, común).
- Original: alta del 0x5ac/2 (vida 1000, +0xd8 = 0), después pone 0x400 y sigue. Paso 0x52a8a7: vida 10/0 según 0x400
  del dueño, sale con 0x4000, +0xd8 += 1 y cada 50 se recoloca alrededor del dueño (+0x24 += 50, sen/cos·80, z + 50) y
  crea el joint 0x619/9 de ancho 15 (ya portado).
- Cambio: un hermano de `CreateFullSetFlare`/`StepFullSetFlare` (0x5ac/3, que ya usa 0x619/9), luego 0x400 y
  `HitUnlessHero`.

### P2 (medio-bajo). El grito del cuervo: partícula 0x5ac/6 — GameCharacterEffects.cpp:679, 0x418a26

- Alcance: cada grito del Dark Raven (estado 5/6, +0x38d ≤ 14) de cualquier Dark Lord visible.
- Original: por hueso no ficticio (de a dos desde rand() & 1), 0x4ec110 punto y
  0x53fe70(0x5ac, punto, cuervo+0x1c, luz [ebp−0x20], 6, 1,3f, 0).
- Cambio: `Particles::CreateChecked(0x5ac, …, 6, 1.3f, …)` (la FlareSix ya existe).

### P2 (medio-bajo). Las órdenes del cuervo: joints 0x60f/0, no /2 — PetStats.cpp:170 y :173, 0x421f9a/0x421fae

- Alcance: el A9 de las órdenes del Dark Raven.
- Original: 0x418390 = 5 y 0x418400 = 15 × 0x530860(0x60f, pos, pos, ángulos, **subtipo 0**, dueño = el objeto del
  cuervo (+0xc), 40,0f, **clave 2**, 0, 0, −1), sin rand(). El mensaje de MuRet ("0x60f/2") lee la clave como subtipo.
- Cambio: `CreateFaunaJoint` con la clase `CreateSparkle` (60f/0, ya portada); falta un `JointOwnerKind` del cuervo (la
  luz +0xf4 y la acción +0x111 del dueño). Ancho 40 > 10: vida 50 y el tercer rand().

### P2 (bajo, inalcanzable con el GS 0.97). Level-up de otro jugador — SkillReplies.cpp:860, 0x4dbfc5

- Original (0x4dbf50): 15 × 0x530860(0x60f, o+0x10, o+0x10, o+0x1c, **subtipo 0**, dueño o, 40,0f, clave 2, 0, 0, −1),
  después 0x518cf0(0x61e, o+0x10, o+0x1c, o+0xf4, 0, dueño o, −1, 0…) y el sonido 0x98. El mensaje dice "0x60f/2":
  la misma confusión clave/subtipo. 0x60f/0 con un ActorState de dueño ya se crea (`CreateSparkle`); 0x61e/0 es el caso
  0x51eebb (vida 20, sin rand()) y su paso 0x52b984 no hace nada con el subtipo 0.
- Cambio: CreateFaunaJoint ×15 + generalizar `CreatePetSummon` al subtipo 0 + sonido. El GS 0.97 no manda el paquete
  (P-C262): prioridad baja.

### P2 (mapas de evento 24..29 y 36). El terremoto 0xfa — MonsterAttackStep.cpp:257, :243, :237

- :257 0x43ae45: 0xfa/1 con dueño c, estado 1. Creable entero con
  `ModelEffects::CreateEarthquake(…, ModelEffectOwner{&actor.type, nullptr, &actor.angles}, …)` (como
  `ShakeTheGround` del hito 4).
- :243 0x43b570: 0xfa estado 0, creable; su paso corta después en la vida 13 (0x5224ec, la 0x104 sin portar), que es un
  CL11 verdadero más adelante.
- :237 0x43ab42: crear el 0xfa (estado 1) y cortar en 0x43ab5f, en el 0x615 (que sí es CL11).

### P2 (bajo). El brillo de la mano 0xa25 — ArrowEffects.cpp:729 (h3), 0x53e642

- Original (0x53e2b0): 0x4ec110 hueso de la mano (+0x2bd) con (0, 10, −130); 15 × {rand() % 360 → ángulo z;
  rand() & 1 == 0 → joint 0x530860(0x617, pt, pt, ángulos, 3, sin dueño, 10,0f, −1, 0, 0, −1); partícula
  0x53fe70(0x5c5, pt, ángulos, (1,1,1), 0, 1,0f, 0)}.
- Las dos clases están portadas (joint 0x617/3, partícula 0x5c5/0): `CreateFaunaJoint` + `Particles::CreateChecked`.

### R (bajo). 0x51fff0 y 0x51fe10 cortan antes de sus rand()

- MonsterAttackStep.cpp:344 (0x43259e), :496 (0x4f5d3c), :513 (0x4f6450), :573 (0x4f7962), :583 (0x4f79af) y
  AttackSkillCases.cpp:329 (0x501494, Inferno): 0x51fff0 gira el punto (sin rand()) y llama a 0x51fe10(punto, 1), que
  saca rand() % 60 + 150 y rand() % 30 antes de la primera 0x5c5/2 (CL11). El corte verdadero va después de esos dos
  rand(): llamar a `BurstAt` (h3 ArrowEffects.cpp:339, hoy en un namespace anónimo; hay que exponerlo) en el primer
  punto girado, que ya corta en 0x51fe9e con la misma secuencia.
- ActorDying.cpp:158 (0x4fbba8, Chaos Castle): tres rand() % 160 y después 0x51fe10 → los tres rand() y `BurstAt`.
- ActorDying.cpp:175 (0x4fbcdd, los diez 0x5ac/5): rand() % 32 y rand() % 100 antes del primer 0x5ac/5.

### P2m (la etiqueta y el punto del corte cambian; el cierre no)

- MonsterAttackStep.cpp:614 (0x4f7fd2, 0xe2 de la mano para +0x376 0x36/0x39/0x97, Iron Wheel y Tarkan): la alta está
  portada (`BeginTheAquagoldArrow`; la clave es −1, no +0x8a; con 0x39 dos más con los +0x24 desplazados) y el paso
  `StepAquagoldArrow` corta en la 0x607/1 del cuadro siguiente.
- AttackSkillCases.cpp:390 (0x504561, 0x61e/2, la 0x1b) y :394/:257 (0x50470f y 0x4fdf1e, 0x61e/3, la 0x1c y las
  invocaciones 0x1e..0x24): alta igual a la 0x61e/1 (caso 0x51eebb, `CreatePetSummon` con el subtipo), pero el paso
  0x52b984 → 0x518900 crea joints 0x619/2 y /3 (CL11) tras dos rand().
- SkillReplies.cpp:553 (0x4cdd84, 0x30 Swell Life, 0x5ac/1): alta portable (vida 1000), pero el paso 0x52a7b7 crea
  partículas 0x5ac/4 (sin portar).

### F (sistemático). El pool lleno

Todos los `CutAt`/`Core::Unexpected` de MonsterAttackStep, AttackSkillCases, AttackFrameSkills, SkillReplies,
ActorCombat, ActorDying y el cuervo cortan aunque el pool esté lleno; el original con el pool lleno no crea nada y
sigue. Inofensivo hoy (el corte es CL11 igual), pero cada port P2 de arriba tiene que tratar el `nullopt` como "sigue".
Los cortes de partícula de ArrowEffects ya usan `CreateChecked` (bien).

## Detalles menores (no cambian el veredicto)

- SkillReplies.cpp:565: la etiqueta 0x4cddfa es la dirección de retorno; el `call 0x518cf0` está en 0x4cddf5.
- AttackSkillCases.cpp:149 (0x4fe424, 0x61e/6): T, no P2m: el caso 0x61e/6 tiene alta propia (vida 60 y
  rand() % 50 + 50 en la escala, 0x51eedd) y su paso crea 0x5ac/8.
- Fuera de alcance (sin etiqueta CL11): MonsterAttackStep 0x432acc (Q-C1050), GameModelEffects 0x5252e6 (Q-C1176),
  HeroSkillCasts 0x55be23 (0x5fe portado; el corte es del modelo compartido).

## Hito 3: tabla de cortes

| Archivo:línea | Dirección | Qué corta | Veredicto |
|---|---|---|---|
| MonsterAttackStep.cpp:191 | 0x439ca6 | 0xc2/8 ×5 (0x439bd0) | T |
| MonsterAttackStep.cpp:194 | 0x439ba5 | 0x11b/1 ×3 (0x439b20) | T |
| MonsterAttackStep.cpp:197 | 0x439d09 | 0xcb/3, 0xcc/4 | T |
| MonsterAttackStep.cpp:237 | 0x43ab42 | 0xfa + 0x615 | P2 |
| MonsterAttackStep.cpp:243 | 0x43b570 | 0xfa estado 0 | P2 |
| MonsterAttackStep.cpp:245 | 0x43b5ac | 0xf6/0, 0x5de/1 | T |
| MonsterAttackStep.cpp:252 | 0x43ac29 | 0x11a | T |
| MonsterAttackStep.cpp:257 | 0x43ae45 | 0xfa/1 | P2 |
| MonsterAttackStep.cpp:271 | 0x43b1f4 | 0xc2/7 ×3 | T |
| MonsterAttackStep.cpp:277 | 0x43b0ed | joints 0x610/6 | T |
| MonsterAttackStep.cpp:283 | 0x43b241 | 0xf6/5, 0x5de/2 | T |
| MonsterAttackStep.cpp:295 | 0x42dd48 | 0x61c | T |
| MonsterAttackStep.cpp:297 | 0x42dd9c | 0x19f | T |
| MonsterAttackStep.cpp:313 | 0x42919a | 0xcf/3 ×5 | T |
| MonsterAttackStep.cpp:317 | 0x429202 | 0x141 | T |
| MonsterAttackStep.cpp:321 | 0x42925a | 0x61c/1 | T |
| MonsterAttackStep.cpp:334 | 0x432532 | 0xe1/1 (joint de la alta) | T |
| MonsterAttackStep.cpp:338 | 0x43257a | 0xf8/1 | T |
| MonsterAttackStep.cpp:344 | 0x43259e | 0x51fff0 | R |
| MonsterAttackStep.cpp:349 | 0x4329db | 0xc1a ×18 | T |
| MonsterAttackStep.cpp:371 | 0x432b63 | 0xed/1 | T |
| MonsterAttackStep.cpp:482 | 0x4f5d13 | 0x625 ×20 | T |
| MonsterAttackStep.cpp:491 | 0x4f6145 | 0xf6 | T |
| MonsterAttackStep.cpp:496 | 0x4f5d3c | 0x51fff0 | R |
| MonsterAttackStep.cpp:501 | 0x4f609d | 0xf5 | T |
| MonsterAttackStep.cpp:506 | 0x4f6422 | 0xf6/1 | T |
| MonsterAttackStep.cpp:513 | 0x4f6450 | 0x51fff0 | R |
| MonsterAttackStep.cpp:515 | 0x4f6780 | 0xc1a ×18 | T |
| MonsterAttackStep.cpp:523 | 0x4f67fb | 0x5e9 hueso 63 | T |
| MonsterAttackStep.cpp:525 | 0x4f6b51 | 0x5e8 | T |
| MonsterAttackStep.cpp:534 | 0x4f6ec5 | 0xc2/2 ×3 | T |
| MonsterAttackStep.cpp:535 | 0x4f6fc5 | 0xc2 (ScatterAndCut, 2 rand()) | T |
| MonsterAttackStep.cpp:539 | 0x4f7050 | 0xc2/1 ×18 | T |
| MonsterAttackStep.cpp:546 | 0x4f70b8 | 0xcb, 0xcc | T |
| MonsterAttackStep.cpp:547 | 0x4f80cf | ScatterAndCut | T |
| MonsterAttackStep.cpp:556 | 0x4f756a | joints 0x613/1 ×36 | T |
| MonsterAttackStep.cpp:565 | 0x4f7930 | ScatterAndCut | T |
| MonsterAttackStep.cpp:567 | 0x4f789b | 0xf6/1 | T |
| MonsterAttackStep.cpp:573 | 0x4f7962 | 0x51fff0 | R |
| MonsterAttackStep.cpp:583 | 0x4f79af | 0x51fff0 | R |
| MonsterAttackStep.cpp:592 | 0x4f80cf | 0x106/1 | T |
| MonsterAttackStep.cpp:606 | 0x4f7f3c | joints 0x613/3 ×40 | T |
| MonsterAttackStep.cpp:614 | 0x4f7fd2 | 0xe2 de la mano | P2m |
| MonsterAttackStep.cpp:619 | 0x4f80cf | ScatterAndCut | T |
| MonsterAttackStep.cpp:651 | 0x4f81cc | 0x5e8 | T |
| MonsterAttackStep.cpp:658 | 0x4f8227 | 0xf5 ×2 | T |
| MonsterAttackStep.cpp:659 | 0x4f8287 | 0xc2/6 ×2 | T |
| MonsterAttackStep.cpp:666 | 0x4f8326 | 0x106/1, joint 0x614/2 | T |
| MonsterAttackStep.cpp:671 | 0x4f83e5 | 0xc2/5 | T |
| MonsterAttackStep.cpp:676 | 0x4f845e | 0x106/1, 0x614/2 | T |
| MonsterAttackStep.cpp:681 | 0x4f852d | 0x106/1, 0x614/2 | T |
| MonsterAttackStep.cpp:686 | 0x4f8592 | joints 0x60f/7 ×8 | T |
| MonsterAttackStep.cpp:691 | 0x4f8654 | joints 0x614/2 ×12 | T |
| MonsterAttackStep.cpp:696 | 0x4f87f8 | joints 0x5bc/1 ×8 | T |
| MonsterAttackStep.cpp:703 | 0x4f88d0 | joints 0x61b/0 | T |
| MonsterAttackStep.cpp:709 | 0x4f89df | joints 0x61b/1 | T |
| MonsterAttackStep.cpp:728 | 0x4f8b31 | joints 0x61b/1 | T |
| MonsterAttackStep.cpp:734 | 0x4f8bfb | joints 0x614/0 | T |
| MonsterAttackStep.cpp:740 | 0x4f8ca6 | joints 0x61b/0 | T |
| MonsterAttackStep.cpp:744 | 0x4f8d63 | joints 0x614/0 ×8 | T |
| MonsterAttackStep.cpp:748 | 0x4f8e46 | joints 0x614/2 ×12 | T |
| MonsterAttackStep.cpp:751 | 0x4f8f07 | joints 0x614/0 (antes de 0x5ca/0) | T |
| ArrowEffects.cpp:135, :151, :164 | 0x51c56c, 0x51c713 | joints de la alta de la flecha | T |
| ArrowEffects.cpp:224, :227, :230, :236 | 0x51a24a, 0x51a405, 0x51a173, 0x51bf35 | joints 0x610/0xe, 0x60f/0xb, 0x610/0xd·0xc, 0x618/5 | T |
| ArrowEffects.cpp:571 | 0x526d5f | | T |
| ArrowEffects.cpp:604 | 0x52434e | 0x607/1 | T |
| ArrowEffects.cpp:654 | 0x526bc2 | 0x60f/5 | T |
| ArrowEffects.cpp:656 | 0x526c05 | | T |
| ArrowEffects.cpp:729 | 0x53e642 | brillo 0xa25 | P2 |
| ArrowEffects.cpp:742, :777 | 0x5208a0 | pasos de flechas sin alta | T |
| ArrowEffects.cpp:763 | 0x5267dc | paso de 0xe1/1 | T |
| ActorCombat.cpp:464 | 0x4f4f5b | trampa 0x27 → 0xd4 | T |
| ActorCombat.cpp:471 | 0x4f4fb9 | trampa 0x33 → 0x5da | T |
| ActorCombat.cpp:509 | 0x4f58a3 | 0xd5/0xd6/0xe5/0xe6 de la muerte | T |
| ActorDying.cpp:158 | 0x4fbba8 | ráfaga de Chaos Castle | R |
| ActorDying.cpp:175 | 0x4fbcdd | 0x5ac/5 ×10 | R |
| GameModelEffects.cpp (paso 0xfa) | 0x5224ec | 0x104 en la vida 13 | T |

## Hito 4: tabla de cortes

| Archivo:línea | Dirección | Qué corta | Veredicto |
|---|---|---|---|
| MonsterAttackStep.cpp:780 | 0x4f8fef | joints 0x614/0 (Lightning) | T |
| ArrowEffects.cpp:149 | 0x51a08a | joint 0x610 de la estela 0x105 | T |
| ArrowEffects.cpp:330 | 0x51e01a | | T |
| ArrowEffects.cpp:530, :585 | 0x52055f, 0x52083f | joint 0x60f/6 (skill 0x34) | T |
| ArrowEffects.cpp:703 | 0x5241e9 | 0x5d9/5 del asedio | T |
| ArrowEffects.cpp:771 | 0x526961 | 0x60f/5 de 0xe1/1 | T |
| ArrowEffects.cpp:906, :909, :912, :918 | 0x51a24a, 0x51a405, 0x51a173, 0x51bf35 | joints (movidos de h3) | T |
| GameCharacterEffects.cpp:654 | 0x418aff | joints del ataque del cuervo | T |
| GameCharacterEffects.cpp:662 | 0x418c0a | 0x113 | T |
| GameCharacterEffects.cpp:679 | 0x418a26 | partícula 0x5ac/6 del grito | P2 |
| GameCharacterEffects.cpp:690 | 0x418a8c | 0x4ee770 | T |
| PetStats.cpp:170 | 0x421f9a | 0x418390: 5 joints 0x60f/0 | P2 |
| PetStats.cpp:173 | 0x421fae | 0x418400: 15 joints 0x60f/0 | P2 |
| AttackFrameSkills.cpp:155, :176, :260, :267, :277, :299, :322, :358, :375, :383 | 0x4f99fe, 0x4f9b49, 0x4f9c8d, 0x4f9d1c, 0x4f9e49, 0x4f9ecb, 0x4f9f3e, 0x4fa4fa, 0x4fa9ca, 0x4faa81 | | T (10) |
| SkillReplies.cpp:438 | 0x4cd664 | 0x628 (skill 0x37) | T |
| SkillReplies.cpp:451 | 0x4cd6ff | 0xc1/1 (0x33) | T |
| SkillReplies.cpp:512 | 0x4cdb72 | 0x138 (0x45) | T |
| SkillReplies.cpp:527 | 0x4cdc88 | 0x5ac/2 (0x35) | P2 |
| SkillReplies.cpp:553 | 0x4cdd84 | 0x5ac/1 (0x30) | P2m |
| SkillReplies.cpp:565 | 0x4cddfa | 0x112 (0x3b) | T |
| SkillReplies.cpp:860 | 0x4dbfc5 | level-up: 0x60f/0 ×15, 0x61e/0 | P2 |
| AttackSkillCases.cpp:132 | (0x530860) | joint 0x632 sobre el blanco | T |
| AttackSkillCases.cpp:149 | 0x4fe424 | 0x61e/6 | T |
| AttackSkillCases.cpp:164, :167, :170 | (0x518cf0), 0x502ae6 | 0x117/0, 0x117/1, 0x138 | T |
| AttackSkillCases.cpp:240 | 0x5026a4 | | T |
| AttackSkillCases.cpp:257 | 0x4fdf1e | 0x61e/3 (invocaciones) | P2m |
| AttackSkillCases.cpp:267..311 (13 cortes) | 0x4fe0e4, 0x4fe172, 0x4fe322, 0x4fe590, 0x4fe856, 0x4feae8, 0x4febbb, 0x4fec15, 0x4fec9a, 0x4fecf3, 0x4ff4ef, 0x4ff5b2, 0x4ffdaf | tabla de skills con cabeza (base 5) | T |
| AttackSkillCases.cpp:316 | 0x500531 | 0x5de/0 de la Flame | **P2 (H4-1)** |
| AttackSkillCases.cpp:319, :323, :327 | 0x500c49, 0x500cb9, 0x500d7b | | T |
| AttackSkillCases.cpp:329 | 0x501494 | 0x51fff0 (Inferno) | R |
| AttackSkillCases.cpp:333, :337 | 0x501c67, 0x502975 | | T |
| AttackSkillCases.cpp:390 | 0x504561 | 0x61e/2 (0x1b) | P2m |
| AttackSkillCases.cpp:394 | 0x50470f | 0x61e/3 (0x1c) | P2m |
| AttackSkillCases.cpp:396..461 (15 cortes) | 0x504768, 0x5048db, 0x504926, 0x50497e, 0x5049ef, 0x504a3a, 0x504b33, 0x504b80, 0x504bce, 0x504c38, 0x504cc0, 0x504d05, 0x504e40, 0x50501a, 0x505053 | tabla de skills con blanco (base 1) | T |
| (SparkTheTarget) | 0x52e77b | 0x5c6/0 | T |

Las filas agrupadas de AttackSkillCases se contaron una por corte (44 cortes CL11 en el archivo, sin 0x4fe3ce, 0x50243d
y 0x504876, que no son CL11).
