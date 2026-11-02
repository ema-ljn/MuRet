# Progreso de aprendizaje

Lo leen y actualizan el estilo `Tutor MuRet` y el subagente `muret-reviewer`. El comando `/tutor`
solo lo lee.

## Dónde estamos

- Ola: 0 (Fundaciones)
- Sub-ola: **0.0 resuelta como ejemplo trabajado** (del código al programa), el 2026-11-01. A
  pedido tuyo la resolvió Claude: la misión (`Labs::HeadOffset` en `MuRetLabs`), el jefe (la ODR) y
  el carril R, con las salidas y las respuestas en [`labs/0.0-enlace/README.md`](../../labs/0.0-enlace/README.md).
  Para cerrarla falta lo tuyo: contar con tus palabras las preguntas de la
  [guía](ola-0.0-del-codigo-al-programa.md#preguntas-para-chequearte) y completar su sección final
- La 0.1 está hecha (andamiaje CMake + `Server/DataServer/`); falta compilar en Windows y responder [las preguntas](ola-0.1-cmake.md#te-toca), que se cierran junto con la 0.0
- Próxima del servidor: la 0.2, ahora más chica: vcpkg, GoogleTest, los test presets y el CI ya
  llegaron con la CL0.2 y la CL0.3 del cliente; le quedan los tests del DataServer
- **Cliente (tu foco): CL0.2 y CL0.3 resueltas como ejemplo trabajado** el 2026-11-01, después de
  que eligieras ir directo al cliente: `ClientLib` con la resolución de rutas de `Data\`,
  `ClientTests` con la carpeta golden, la ventana SDL3 con OpenGL 3.3 core y el CI en Windows y
  Linux. Para cerrarla: las [preguntas de la CL0](../superpowers/plans/2026-10-30-ola-cl0-fundaciones-cliente.md#para-cerrar-la-cl0-emanuel)
  y correr `Main.exe` en tu Windows. Conceptos para explicar: targets y qué se linkea a quién,
  vcpkg en modo manifiesto y su baseline, loader de GL, tests salteables. Próxima: la CL1
  (formatos sin ventana), con su [plan](../superpowers/plans/2026-11-01-ola-cl1-formatos-sin-ventana.md)
  y la [guía de la CL1.0](ola-cl1.0-bytes-y-archivos.md). El 2026-11-01 pediste seguir con el
  cliente: Claude escribe la CL1 como ejemplo trabajado. Ya están el `Expected` propio
  (`Client/src/Core/Expected.h`) y la CL1.1: `ByteReader`, XOR3, el checksum de las tablas y
  `Text.bmd`, con tests contra el archivo real y contra la memoria del original. También la
  CL1.2: `Gate`, `Item` y `Skill.bmd` sobre una misma `RecordTable<RecordSize>`; `Item` informa su
  checksum y carga igual (Q-C01), `Skill` lo exige, y un archivo corto es un error (Q-C24). La
  parte de `labs/` de la CL1.0 (la `ByteView`, el jefe Q-C07), las actividades 1 a 6 de la guía y
  el checksum de dos dwords a mano (actividad 12) quedan para después, igual que el test opcional
  del comportamiento viejo de Q-C24 (paso 7 de la CL1.2). La CL1.3 (terreno) va en dos PRs; el
  primero (#19) trae K16, `EncTerrain.map` y `EncTerrain.att`, comparados con la memoria del
  original. El segundo, `EncTerrain.obj` y las alturas de `TerrainHeight.OZB`, leídas donde las
  lee el original y no donde dice el BMP (Q-C02). La CL1.4 (texturas) va en tres PRs: los
  volcados de memoria por captura, el OZT, y el OZJ con libjpeg-turbo, que da los mismos bytes que
  la libjpeg 6b del original en los 2.380 JPEG del laboratorio. Fuera de ellos difiere en casos
  que quedan escritos (Q-C33), y el plan de un decodificador propio compatible con la 6b queda en
  pausa por si turbo no alcanza ([su plan](../superpowers/plans/2026-11-01-cl1.4-decodificador-jpeg.md)).
  Con el OZJ entra también la luz del terreno, `TerrainLight.OZJ`, igual a la que tenía en memoria
  el original en Lorencia y Devias. La CL1.5 (`message.wtf`) salió de la ola: es del launcher, y
  `main.exe` no lo lee
  ([05](../../Client/docs/research/05-formatos-de-datos.md#el-pipeline-de-las-dlls)). Con eso el
  código de la CL1 está entero; para cerrarla falta lo tuyo (la parte de `labs/`, las actividades
  de la guía y explicar los conceptos de abajo). Próxima: la CL2 (protocolo sin red), con su
  [plan](../superpowers/plans/2026-11-02-ola-cl2-protocolo-sin-red.md) y la
  [guía de la CL2.0](ola-cl2.0-flujos-y-bloques.md). El 2026-11-02 pediste que Claude la escriba
  entera, también la CL2.0 en `labs/` y los pasos `TODO(human)`, hasta que digas que participás:
  lo que resuelva Claude queda marcado acá como resuelto por Claude. **La CL2.0 la resolvió
  Claude** el 2026-11-02: el `FrameCutter` de a un byte, el `BitWriter` y el `BitReader` MSB
  primero, el inverso modular con Euclides extendido y el jefe, un modelo del corte del original
  que lee bytes viejos (Q-C03), con las respuestas de las 14 actividades en
  [`labs/cl2.0-flujos/README.md`](../../labs/cl2.0-flujos/README.md). Con ella llegó GoogleTest a
  `labs/`. Para cerrarla falta lo tuyo: las preguntas y la sección final de la guía. **La CL2.1
  también la escribió Claude**: el `ByteReader` pasó a `Core`, con `ReadU16Be`, y llegó el corte
  de `Network`. `Packet` valida el tipo y el tamaño al armarse, y `PacketReader` espera los bytes
  que faltan donde el original lee bytes viejos (Q-C03), y se para con un error ante un tipo
  inválido, un tamaño que no llega al opcode o uno de más de `0x2000` (Q-C38, Q-C39 y Q-C40). Se
  prueba contra el cable del laboratorio: los 689 paquetes que mandó el cliente y los 144 que
  contestó el ConnectServer salen enteros y sin bytes de más. **La CL2.2, también**:
  `PacketWriter` arma un C1 o un C2 campo por campo y rechaza lo que no entra en vez de truncarlo
  (Q-C06), y el XOR32 del motor de envío va aparte, con el opcode en claro también en un C2, como
  en el binario y no como en `mu-client`.
  **La CL2.3, también**: los bloques del SimpleModulus y los archivos de clave, con una clave de
  cifrar y una de descifrar que son tipos distintos, y un módulo que no sirve rechazado al cargar
  (Q-C41). Contra las claves reales, cada par del cliente y del servidor es inverso. Encima,
  `EncryptPacket` y `DecryptPacket` arman y rearman los C3/C4 con sus seriales: el serial que no
  coincide se resincroniza (Q-C04), el C4 más grande se descifra entero (Q-C05), y el paquete
  rearmado declara lo que mide (Q-C10). Se descifran los 183 C3 que mandó el cliente original y
  los 25 C3/C4 que contestó el GameServer, con el serial en orden en cada conexión, y
  `EncryptPacket` vuelve a cifrar los 183 como el original salvo el último bloque, donde el
  original deja bytes de su pila.
- Documentación lista: el [roadmap](../superpowers/specs/2026-10-25-dataserver-port-design.md) (qué hace MuRet, las olas 0 a 8 y la tabla de las 9 a 14), el [camino de aprendizaje](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md) (cómo se aprende), el [research del original](../../Server/DataServer/docs/research/) (01..10, qué hace el binario byte a byte), los [planes por ola](../superpowers/plans/) y una [guía de estudio por ola](README.md)
- De la 0.3 ya están `docs/CODING_RULES.md`, `AGENTS.md` y `.clang-format`, adaptados de MuMain; quedan los ADR 0001 (tuyo) y 0002

## Conceptos

**Niveles:** 1 **usar** (lo escribiste con tests) · 2 **explicar** (lo contás sin mirar) ·
3 **romper** (predecís la falla y la provocás) · 4 **transferir** (lo aplicás en otro contexto).
Una sub-ola se cierra en el nivel 3; el 4 es ejercicio extra.

**Estado:** un concepto queda **dominado** cuando lo respondés bien en dos sesiones distintas. Si
la explicación tiene huecos, queda **flojo** y entra primero en el repaso del arranque de cada
sesión, junto con los de repaso más viejo.

| Concepto | Ola | Nivel | Estado | Último repaso |
| --- | --- | --- | --- | --- |
| unidad de traducción, archivo objeto y símbolos (`nm`) | 0.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| enlace externo e interno, `inline` y la ODR | 0.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| bibliotecas estáticas y dinámicas, imports del PE | 0.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| errores como valor: `Expected`, `std::variant` y `[[nodiscard]]` | CL1.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| little-endian y enteros armados con desplazamientos (`ByteReader`) | CL1.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| XOR que se deshace solo y aritmética de 32 bits que da la vuelta (XOR3, checksum) | CL1.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| registros de largo fijo sin `\0`, `std::string_view` y su lifetime (Q-C07) | CL1.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| plantillas con un parámetro que no es un tipo (`RecordTable<RecordSize>`) y `std::span` de largo fijo | CL1.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| quirk de contrato o bug interno: un checksum que se informa (Q-C01), otro que se exige, y un archivo corto (Q-C24) | CL1.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| un cifrado con estado (K16): cada byte depende del cifrado anterior y de su posición en el archivo, y qué hace falta para descifrar un pedazo suelto | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| una grilla 2D guardada fila por fila (`CellIndex`), y un tipo que no deja salirse de ella | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| floats que no son lo que parecen: `b * (1.0f / 255)` no es `b / 255.0f`, y `std::bit_cast` para ver sus bits | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| un registro con floats: se leen 4 bytes little-endian y `std::bit_cast` los pasa a float sin tocar un bit, también un NaN | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| leer un formato como lo lee el original y no como dice su cabecera (`bfOffBits`, Q-C02), y por qué eso deja afuera a una biblioteca | CL1.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| una imagen dentro de una textura de lados potencia de 2, y el orden de las filas (abajo hacia arriba en el TGA) y de los canales (BGRA) | CL1.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| una biblioteca de C que avisa sus errores con `setjmp`/`longjmp`, y por qué entre los dos no puede quedar vivo un objeto con destructor (`JpegImage.cpp`) | CL1.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| probar que una biblioteca hace lo mismo que la del original: los mismos bytes en los datos reales y en vectores chicos que corren en CI (`CODING_RULES.md` §10) | CL1.4 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-01 |
| TCP entrega un flujo y no mensajes: el corte como máquina de estados que recibe de a un byte (`FrameCutter`), y sus invariantes | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un cifrado encadenado (XOR32) y uno por bloques (`SimpleModulus`), y por qué con la clave en el binario los dos son ofuscación | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| aritmética modular y el inverso con Euclides extendido (`ModularInverse`), y por qué la clave de cifrado del cliente es la inversa de la de descifrado del servidor | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| campos de bits MSB primero (`BitWriter`, `BitReader`), y por qué un carril del `SimpleModulus` no es un campo de 18 bits | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| la aritmética sin signo que da la vuelta, el desborde con signo que es comportamiento indefinido, y `std::uint16_t * std::uint16_t`, que se multiplica como `int` | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un test de propiedad con `std::mt19937` y semilla fija, tomando la salida del motor y no una distribución | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un bug que lee bytes viejos de un buffer que se reusa (Q-C03), modelado para verlo fallar | CL2.0 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un lector que recibe lecturas enteras y saca paquetes enteros, sin leer un byte que no llegó (`PacketReader`), y un error que queda hasta que se cierra la conexión | CL2.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un tipo que se valida al armarse (`Packet::FromBytes`) y deja cambiar el cuerpo pero no la cabecera (`MutableBody`) | CL2.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| probar contra una captura del cable sin mostrar lo que lleva: contar paquetes y decir dónde difieren, nunca qué valen (`SameBytesWithoutValues`) | CL2.1 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| constructores con nombre (`PacketWriter::C1`, `C2`) y métodos que devuelven `*this` para encadenarse, con un `Finish()` `const` que no cambia el escritor | CL2.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un cifrado encadenado que se deshace en el lugar yendo de atrás para adelante (`Xor32DecodeBytes`), y separarlo del armado para probar cada etapa sola | CL2.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| vectores de test armados por un modelo escrito de nuevo desde el research, y no por el código que prueban (`make_vectors.py`) | CL2.2 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| una plantilla con un parámetro `enum` para que dos claves del mismo formato sean tipos distintos (`SimpleModulusKey<KeyRole>`), y una fábrica que valida antes de construir (`FromLanes`) | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un checksum que cubre el claro y no el largo útil: qué cambios detecta y cuáles no (`SimpleModulusTest.TheChecksumDoesNotCoverTheUsefulSize`) | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| vectores que fijan lo que un ida y vuelta no ve: el orden de los bits 16 y 17 de un carril, y el producto de 32 bits que da la vuelta | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| un contador por conexión que detecta un paquete perdido o repetido, y resincronizar en vez de arrastrar el desfase (`PacketSerials`, Q-C04) | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |
| varios errores en un paquete: cuál gana y por qué (el checksum malo sobre el largo útil imposible, porque es el que lleva el aviso) | CL2.3 | — | pendiente: lo resolvió Claude, falta que lo expliques | 2026-11-02 |

## Preguntas para olas futuras

- Cliente (2026-10-30): nació `Client/` con su [research](../../Client/docs/research/README.md) y su [roadmap](../superpowers/specs/2026-10-30-cliente-design.md) (olas CL0 a CL10, SDL3 + OpenGL 3.3, tick fijo y FPS desacoplados sin perder fidelidad). ~~¿Cuándo arranca la CL1 respecto de la ola 1 del servidor?~~ Decidido el 2026-11-01: el cliente va directo (pregunta 3 del roadmap del cliente)
