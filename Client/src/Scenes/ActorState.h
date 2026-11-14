#pragma once

#include "GameLogic/CharacterAppearance.h"
#include "GameLogic/Path.h"
#include "Protocol/Character/Opcodes.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Scenes/CharacterHead.h"
#include "Scenes/DarkSpirit.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Un registro del pool durable de personajes, de 0x438 bytes en el original (06, "Los personajes de
// la lista"). Es una proyección con lo que MuRet lee hoy, no el layout. Arranca en cero, como lo deja
// el REP STOSD de WinMain (0x4bafa6..0x4bafb3).
namespace Client::Scenes
{
// El modelo de una parte sin ítem.
inline constexpr std::uint16_t NoItemModel = 0xffff;

// La capa de los GM: el objeto de 0x1e8 bytes que 0x413fe0 crea con +0x360 en 5 (0x414370 con MuSign, el
// 0x12a). Lo único que cambia entre dibujos es la animación (+0x115 la acción, +0x116 la anterior, +0x118
// el frame y +0x11c la key anterior), que 0x414470 avanza con 0x4ec200; el lugar lo vuelve a calcular cada vez.
struct GameMasterLayer
{
    Render::Models::AnimationState animation;
    // +0x178 de la capa (el +0x174 de su objeto): 0x414370 lo pone en 1 y solo 0x5ffdf0 lo baja, en Chaos Castle
    // con +0x1a1 del héroe.
    float alpha = 1.0f;
};

// Una parte puesta, de a 0x20 bytes: las cinco del cuerpo desde +0x218, las manos en +0x2b8 y
// +0x2d8, las alas en +0x2f8 y la mascota en +0x318 (06, "El equipo propio en el personaje").
struct ActorItemPart
{
    std::uint16_t model = 0; // +0, 0xffff sin ítem
    std::uint8_t level = 0;  // +2
    // +3 y +4: 0x512a30 copia tal cual el byte D+0x1b y el D+0x1c del ítem.
    std::uint8_t flags1b = 0;
    std::uint8_t flags1c = 0;
    // +5: el hueso del cuerpo del que cuelga. 0x511290 pone 0x21 y 0x2a en las manos de un 0x20a
    // (0x5126ce) y 0x50ba30 el de las alas (0x2f) y el de la mascota (0x22) antes de cada 0x509340.
    std::uint8_t bone = 0;
    // +6 la acción, +7 la anterior, +8 el frame y +0xc la key anterior de lo que cuelga, que 0x509340
    // avanza con 0x4ec200 en cada frame. Nadie los pone a 0 al construir: quedan los del uso anterior.
    Render::Models::AnimationState animation{};
    // +0x10: lo que avanza por frame, que 0x50ba30 escribe antes de cada 0x509340.
    float speed = 0.0f;
};

struct ActorState
{
    // +0: vivo. Lo prende 0x511290 y lo apaga 0x510f70 al cargar otro mapa (06, "La carga del mapa").
    bool alive = false;
    // +0x10, +0x14 y +0x18. 0x511290 calcula la Z con XY 50/50 y el mundo vigente, y después
    // 0x517270 escribe la X y la Y del lugar (CL7.3 B).
    glm::vec3 position{};
    // Si el word318 (+0x318) es 0x1c15: lo que la selección escribe de él y lo que el próximo 0x511290
    // lee para sumar el corrimiento de la mascota. El login lo deja en 0x1c16 en el lugar 2. 0x512a30
    // escribe el word entero en pet.model y lo pone al día también acá.
    bool previousHad1c15 = false;
    // Propio de MuRet: cuántas veces lo construyó 0x511290. La vista rearma lo visual con cada una,
    // aunque los bytes sean los mismos (06, "Cada F3/00 aceptado y alta F3/01 exitosa reconstruye").
    std::uint64_t revision = 0;

    // Lo que leen y escriben 0x512a30 y 0x4f2250 (06, "El equipo propio en el personaje" y "Caminar").
    std::uint16_t type = 0;   // +2: 0x20a es un jugador
    std::int32_t subtype = 0; // +4: el sufijo de 0x4f2250 lo compara con 209 a 211, con signo
    float scale = 0.0f;       // +0xc, de 0x512930
    // La caja del personaje, de +0x124 a +0x138, relativa a su posición: 0x511290 pone el piso en (−60, −60, 0)
    // (0x512270..0x5122b9) y el resto por el tipo (las tablas de 0x512700 y 0x5126e4, y 0x42d7f0 con el 0x19b y el
    // 0x1f8). El picking (0x59e700) prueba la caja de +0x13c, que 0x505de0 arma con estos valores y la posición.
    glm::vec3 boundsMin{};
    // +0x130 y +0x134, la X y la Y del otro rincón.
    float boundsMaxX = 0.0f;
    float boundsMaxY = 0.0f;
    // +0x138, el techo de la caja. 0x5133b0 le suma 70. Lo lee también el globo (0x5a9050), que va 60 más arriba.
    float boundsTop = 0.0f;
    // +0x58, +0x60 y +0x64: 0x511290 pone el primero y el último en −1 (0x511d45, 0x51248d) y no toca el del medio;
    // los casos de 0x5141e0 los escriben por el tipo. +0x58 en −2 no se dibuja (0x42da90 y 0x42dfe0 lo escriben en el
    // mapa 30); +0x60 es el contador de la partícula del 0xc5 y el estado de los ataques de Kundun en 0x43c150
    // (KalimaSiegeHelpers.h). Quién lee los otros, sin leer.
    std::int32_t dword58 = 0;
    std::int32_t dword60 = 0;
    std::int32_t dword64 = 0;
    // +0x92 (1), +0x93 (0xa), +0x94 (1,2f), +0x98 (0) y +0xe8 (1) desde 0x511290 (0x511c70..0x511c88, 0x511bfe y
    // 0x511be6). El 13 pone en +0x93 los bits 4..6 del tipo alto (0x4c9ee9), que la barra de vida del 0x1a8 lee
    // (0x42a2c5, Scenes/HealthBars). Quién lee los otros, sin leer.
    std::uint8_t byte92 = 0;
    std::uint8_t byte93 = 0;
    float float94 = 0.0f;
    std::uint8_t byte98 = 0;
    std::uint8_t byteE8 = 0;
    // +0xcc, +0xd0 y +0xe4: el empujón del tipo 0xf1 que pone el DF (0x4c6b40): 20 en +0xe4 y la diferencia de celdas
    // por 400 en las otras dos. +0xd8: 0x511290 lo pone en 0 (0x511c56); el creador de Castle Siege le copia la Z
    // (0x42d23e). Quién los lee, sin leer.
    float floatCC = 0.0f;
    float floatD0 = 0.0f;
    // +0xd4: el tercer giro del 0xf1, que 0x4fb910 suma al yaw y achica a 0,8f. Ningún camino del personaje del pool lo
    // escribe (0x511290 tampoco: queda el del uso anterior del lugar, en MuRet 0; *estático*).
    float floatD4 = 0.0f;
    float floatD8 = 0.0f;
    float floatE4 = 0.0f;
    // +0x38a (0, 0x511c0c), +0x38f (0x42d7f0 lo pone en 0 con el 0x19b), +0x394 (0, 0x511ceb), +0x3e4 y +0x42d (el bit
    // 7 del tipo alto del 13, 0x4c9ee3): los escriben las altas de los monstruos. Quién los lee, sin leer.
    std::uint8_t byte38a = 0;
    std::uint8_t byte38f = 0;
    std::int32_t dword394 = 0;
    std::uint8_t byte3e4 = 0;
    std::uint8_t byte42d = 0;
    // +0x110 (0x511290 lo pone en 0, 0x511c69) y +0x100: 0x42d890 los pone en 1 y en 0x12c cuando el 286 o el 287 del
    // mapa 30 terminan la acción 5 (0x42d8fd y 0x42d904); 0x42da90 los lee y los escribe: abiertas y el tiempo que les
    // queda, que baja 1 en cada paso (StepTheCastleObject, MonsterStepCases.h).
    std::uint8_t byte110 = 0;
    std::int32_t dword100 = 0;
    // +0x3a4: la cola 0x5140c0 le copia el WORD de [0x57678b0] (WorldRuntime::heroSlot).
    std::uint16_t word3a4 = 0;
    // +0x428: −0x80 desde 0x511290 (0x511c77); el 68 lo pone en 0x96 (0x4e0b8a). Quién lo lee, sin leer.
    std::int32_t dword428 = 0;
    // +0x7c: con 0x200000, 0x4f2250 sienta al personaje (la acción 0xca). Quién lo escribe, sin leer.
    std::uint32_t flags7c = 0;
    // +0x78: 0x511290 lo pone en 0 (0x511d51). Del jinete lo leen el paso del caballo (con 0x4000, sin
    // polvo ni aliento: 0x444414 y 0x44455a), su dibujo (con 0x4000, 0x602e90 con 10: 0x445310) y 0x5ffdf0
    // (con 0x1000, el frame del caballo en 0: 0x5ffefc). Quién más lo escribe en el personaje, sin leer.
    std::uint32_t flags78 = 0;
    // +8: con 1, el paso del caballo oscuro de este jinete saca doce 60f/0 (0x44468d) y lo vuelve a 0 (0x444758),
    // sea cual sea. 0x511290 no lo escribe y 0x5140c0 lo pone en 0. Sólo lo pone en 1 la respuesta del A9 de las
    // mascotas (0x421db0, 0x4220a8..0x4220c1), en el héroe y con su byte 4 en 0xfe (con 0xff, 2), sin portar.
    std::uint8_t byte8 = 0;
    // La acción en +0x111, la anterior en +0x112, el frame en +0x114 y la key anterior en +0x118: lo que
    // pone 0x4ea030.
    Render::Models::AnimationState animation{};
    std::uint8_t characterClass = 0;                            // +0x1d4
    std::uint8_t skin = 0;                                      // +0x1d5
    std::uint16_t level = 0;                                    // +0x1d6, nivel de la lista o del F3 03
    std::uint8_t guildStatus = 0;                               // +0x368, 0xff desde 0x511290
    std::uint16_t head = 0;                                     // +0x1f8, de 0x512930: 0xffff sin cabeza
    std::array<ActorItemPart, GameLogic::BodyPartCount> body{}; // +0x218, en el orden de GameLogic::BodyPart
    std::array<ActorItemPart, 2> hands{};                       // +0x2b8 la derecha y +0x2d8 la izquierda
    ActorItemPart wings;                                        // +0x2f8
    ActorItemPart pet;                                          // +0x318
    // +0x358: 0x511290 lo pone en 0 (0x511c99). En el héroe publicado, 0x40d010 lo pone en 0 y en 1 si
    // hay un conjunto completo (06, "Los stats del registro propio"); en la respuesta del F3 03 ese
    // publicado todavía es el anterior (Q-C197). La lista de la selección le copia el bit 0 de b[10],
    // sin portar.
    std::uint8_t byte358 = 0;
    // +0x35c: con +0x358, 0x50fd74 lo cuenta en cada dibujo y lo vuelve a 0 con el efecto 0x5ac/3 de cada
    // 100. 0x511290 lo pone en 0 (0x511ca0); nadie más lo escribe.
    std::int32_t counter35c = 0;
    // +0x376: con 0x101, 0x4f2250 elige la acción de volar. Quién lo escribe, sin leer.
    std::uint16_t word376 = 0;
    std::uint8_t run = 0; // +0x38c, el Run de Caminar
    // +0x1c, +0x20 y +0x24: 0x511290 pone los dos primeros en 0 y el ángulo que recibe en el tercero.
    glm::vec3 angles{};
    // +0x40, +0x44 y +0x48: el punto del bastón 0xc1e que 0x509340 deja en cada dibujo (0x50b4ca). El joint 0x618/0x11
    // que suelta ahí toma de acá su posición al crearse (0x5321f7) y en cada paso (0x53822f). 0x511290 no lo escribe:
    // queda el del uso anterior del registro (Q-C363).
    glm::vec3 effectOrigin{};
    // +0x17c, +0x180 y +0x184: la posición que 0x4fcee0 copia antes del frame 3 y fuera de la acción 0x8d
    // (0x4fcf06..0x4fcf1b). La escribe también la habilidad (0x4f5b70), sin portar; 0x511290 no la toca: queda la
    // del uso anterior del registro (Q-C363). El joint 0x610/4 de la espada 0x223 sigue de acá al personaje (0x537d55).
    glm::vec3 trailOrigin{};
    // +0x28 y +0x2c, los ángulos de la cabeza, y +0x34 y +0x38, hacia dónde gira: los mueve 0x505de0
    // (0x5068eb..0x5069ab, Scenes::StepCharacterHead) y los lee la pose (0x4eb860). 0x511290 no los escribe:
    // quedan los de WinMain o los del uso anterior del registro. Del héroe, el blanco lo escribe MoveHero con
    // el mouse (0x59b1c2..0x59b287, CL7.4), sin portar.
    CharacterHead headTurn{};
    // +0x120: los huesos del cuerpo en el mundo como los dejó el último dibujo del juego (0x509340, la pose de
    // 0x4eb860 con la cabeza). 0x511290 suelta el buffer y pide otro sin iniciar (0x5123c4..0x5123ff): antes
    // del primer dibujo, vacío (Q-C318). Lo lee la burbuja de Atlans (0x506d25).
    std::vector<Render::Models::BoneTransform> bodyPose;
    // +0x430: el cuervo que 0x420d60 crea y 0x420dd0 mueve dentro del cuerpo de este dueño.
    std::optional<DarkSpirit> darkSpirit;
    // +0x360: con 5, la capa de los GM, que 0x413fe0 pone en cada dibujo de un GM; del 1 al 3, el arma del
    // evento de Blood Castle (0x50de84). El 4 (y el 0) lo escriben 0x513430, 0x5139f0, 0x5bc740 y 0x5be150, y del
    // 6 al 13 los paquetes (el 0x12 en 0x4c89e1, 0x40c140, y 0x42ab40, 0x42ad50 y 0x42ae60 con banderas de
    // +0x78), sin portar. 0x511290 no lo toca: queda el del uso anterior del registro.
    std::uint8_t byte360 = 0;
    // +0x364: la capa que 0x413fe0 crea con +0x360 entre 5 y 13. 0x511290 la deja en 0 sin soltarla.
    std::optional<GameMasterLayer> layer;
    // +0xf4, +0xf8 y +0xfc, la luz propia: 0x511290 la deja en 0. La lee el efecto de llegada (0x518cf0).
    glm::vec3 light{};
    // +0x3b4..+0x3bc: la luz del dibujo de 0x50ba30, distinta de la luz propia de +0xf4.
    glm::vec3 drawnLight{};
    // +0x170 y +0x174: la meta del alpha y el alpha. 0x511290 los pone en 1 y la respuesta del F3 03
    // baja el alpha del héroe a 0 (0x4c4981): entra transparente.
    float alphaTarget = 0.0f;
    float alpha = 0.0f;
    // +0x16d: cómo lleva 0x4e9980 el alpha a su meta, de a 0,05 con 1 y un décimo de lo que falta con 0.
    // 0x511290 lo pone en 1 (0x511bdf).
    std::uint8_t alphaMode = 0;
    // +0x68: la luz del blendMesh, que 0x4e9980 baja al alpha cuando es mayor y nunca sube (Q-C112).
    // 0x511290 la pone en 1,0 (0x512499).
    float blendMeshLight = 0.0f;
    // +0x6c: la U de los pases de 0x4ef0c0 y 0x4ecc30 del personaje (la V, +0x70, no la escribe nadie más que el alta:
    // en 0). 0x511290 las pone en 0 (0x511d7b y 0x511d7e); solo 0x433b2f (el 0x1af en el 34) guarda la U.
    float textureU = 0.0f;
    // +0x188: el contador de pasos de lógica (0x7d3a0f0) cuando lo construyó 0x511290.
    std::int32_t stepStamp = 0;
    // +0x16c: si se ve. 0x508f00 lo escribe en cada paso con el trapecio del último dibujo (0x5fba90) y,
    // vivo, otra vez 0x508ea0. 0x511290 lo pone en 0 (0x511bd8).
    bool seen = false;
    // +0x13c, +0x148, +0x158 y +0x168: 0x510d00 los pone en 1.000, 1, 1 y 1 en el héroe con +0x1d8 & 4 en
    // la escena 5 (0x510e57), en vez de dibujarlo. Qué son y quién los lee, sin leer.
    float field13c = 0.0f;
    float field148 = 0.0f;
    float field158 = 0.0f;
    float field168 = 0.0f;
    // +0x389: distinto de 0, se está muriendo y 0x508f00 no lo pone en el TerrainWall. 0x511290 lo pone
    // en 0 (0x511c05); quién más lo escribe, sin leer.
    std::uint8_t dying = 0;
    // +0x88, la clase del objeto: 0x511290 y la respuesta del F3 03 lo ponen en 1 (un jugador); con 8,
    // 0x508f00 no lo pone en el TerrainWall (0x508f60). 0x60cf70 contornea el 4 con otros colores y 0x50d374
    // viste al 1 con el Angel en Chaos Castle.
    std::uint8_t byte88 = 0;
    // +0x89 y +0x8a (un short): el pulso del 0x62d de Kundun (0x192) en la acción 5 (0x43c7bb..0x43c8e3), que fuera de
    // ella pone el +0x8a en 0. 0x511290 no los toca; los otros que escriben +0x8a (0x4f9f6f, 0x4fdc10, 0x5ff9ec) son
    // caminos que MuRet no corre con un monstruo (*estático*).
    std::uint8_t byte89 = 0;
    std::int16_t word8a = 0;
    std::uint8_t controlCode = 0; // +0x1d8, el CtlCode de la respuesta del F3 03
    // +0x1d9: el nombre. La respuesta del F3 03 copia ahí los diez bytes del registro propio (0x4c47a2).
    std::array<std::byte, Protocol::Character::NameSize> name{};
    // +0x1a1: sólo lo escriben los paquetes de 0x4d07c0 y 0x4d0a30 (CL9.5); en Chaos Castle, prendido en el
    // héroe, 0x5ffdf0 baja el alpha de cada objeto que posa (0x5ffe09).
    std::uint8_t byte1a1 = 0;
    std::uint8_t byte1e3 = 0; // +0x1e3: la respuesta del F3 03 lo pone en 0 (0x4c4844); qué es, sin leer
    // +0x1e4..+0x1f1: lo que un nombre de monstruo de más de diez bytes pisa después de +0x1e3 (el strcpy sin tope de
    // 0x5141e0 y el strcat del 1F, Q-C847). Qué guarda el original ahí, sin leer.
    std::array<std::byte, 14> nameSpill{};
    // +0x1f4: la clave del personaje. 0x517270 pone el lugar de la selección (0x5172a8) y la respuesta del
    // F3 03 la del jugador (0x57678bc, 0x4c4516). 0x510f70 mata a los que no tienen la del héroe.
    std::uint16_t key = 0;
    std::uint8_t pk = 0; // +0x375: 3 desde 0x511290, después el de la respuesta del F3 03
    // Caminar: +0x3e8.., +0x39a/+0x39b (blanco, separado de la celda adelantada), +0x388 y +0x3e5.
    GameLogic::Path path;
    std::uint8_t targetX = 0;
    std::uint8_t targetY = 0;
    std::uint8_t direction = 0;
    std::uint8_t byte369 = 0;
    std::uint8_t byte370 = 0;
    // +0x372: 0x511290 lo pone en 0 (0x511ccf) y el 0x12 de estado 1 lo conserva del registro anterior (0x4c895f,
    // 0x4c89ed). Qué es y quién más lo lee, sin leer.
    std::uint8_t byte372 = 0;
    std::uint8_t byte374 = 0;
    // +0x42c: 0x511290 lo pone en 1 (0x511c92); sin él, el picking de 0x59e700 no lo elige (0x59e95e). En el mapa 30
    // lo apagan y lo prenden 0x42d890, 0x42da90 y 0x42dfe0 en los objetos del asedio.
    std::uint8_t byte42c = 0;
    // +0x381: la habilidad activa inhibe la orientación quieta (59b3e4). Retenido por 511290.
    std::uint8_t byte381 = 0;
    std::uint16_t word1f2 = 0;
    std::uint16_t damage = 0; // +0x384
    std::uint8_t byte399 = 0;
    std::uint8_t skillIndex = 0;          // +0x425
    std::uint8_t firstFootstepSound = 0;  // +0x3e0, retenidos hasta terminar la animación
    std::uint8_t secondFootstepSound = 0; // +0x3e1
    std::uint8_t byte80 = 0;              // +0x80
    std::uint8_t movementCooldown = 0;
    std::uint8_t movementType = 0;       // +0x379
    float movementPhase = 0.0f;          // +0x84
    std::uint16_t baseMovementSpeed = 0; // +0x386, 10 desde 0x511290
    std::uint8_t movement = 0;           // +0x378, el Movement de Caminar
    // La celda lógica protegida (+0x41c/+0x420), que MovePath adelanta. El blanco del servidor es
    // targetX/Y (+0x39a/b); 0x511290 inicializa las dos parejas con el mismo valor.
    std::uint8_t cellX = 0;
    std::uint8_t cellY = 0;
    // Cuántos accesos tienen abiertas las entradas de +0x41c y +0x420 (Render::Models::LogicalAccess).
    std::uint8_t cellXAccesses = 0;
    std::uint8_t cellYAccesses = 0;
    // Los cuatro bytes de +0x41c y +0x420 como los deja el último acceso de 0x511290: si su cierre dejó la cuenta en
    // 0, los rand() & 0xff de 0x412890 (Q-C125); si no, la celda en claro. Solo los lleva 0x511290: el único que los
    // lee sin abrir, el 13 (0x4c9d01..0x4c9d36), lo hace justo después del alta (Q-C849).
    std::uint32_t cellXLeft = 0;
    std::uint32_t cellYLeft = 0;
    // Profundidad de la entrada de un byte +0x38e, que el dibujo consulta antes de cada mano.
    std::uint8_t byte38eAccesses = 0;
    // +0x38e: el valor lógico. Con ')' (0x29) el lazo de las manos de 0x50ba30 saltea la derecha en la
    // acción 0x3d, y con '4' (0x34) 0x509340 pone el frame de lo que cuelga en 3,2 en las acciones 0x22 a
    // 0x97. Lo escriben 0x4cc530 y 0x4f5b70, sin portar, y el 1A (0x4ceee4, Scenes::ApplyMagicAttackCount).
    std::uint8_t byte38e = 0;
    // +0x390: 0x511290 lo pone en 0 (0x51186b), y lo ponen en 1 el estado de las misiones (0x418e20 y 0x418ee0, con la
    // misión 3 terminada, CL9.1), sin portar: queda en 0. Con 1, el tooltip de la skill del Dark Knight de nivel 220
    // o más escribe el "Combo" (0x5b99d1).
    std::uint8_t byte390 = 0;
    // +0x3e2, la zona segura: cualquier byte distinto de 0. 0x511290 y 0x505de0 copian el bit 0x01 del
    // .att de la celda.
    std::uint8_t safeZone = 0;
    // +0x3e3: distinto de 0, 0x512a30 y 0x512930 no tocan la ropa. 0x511290 lo pone en 0 (0x511d68).
    std::uint8_t byte3e3 = 0;
};

// 0x414340: con una capa en +0x364, la suelta y vuelve +0x360 a 0; sin ella, no toca nada.
inline void ReleaseTheLayer(ActorState& actor)
{
    if (actor.layer.has_value())
    {
        actor.layer.reset();
        actor.byte360 = 0;
    }
}
} // namespace Client::Scenes
