#pragma once

#include "GameLogic/CharacterAppearance.h"
#include "GameLogic/Path.h"
#include "Protocol/Character/Opcodes.h"
#include "Render/Models/Pose.h"
#include "Scenes/CharacterHead.h"
#include "Scenes/DarkSpirit.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

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
    // +0x7c: con 0x200000, 0x4f2250 sienta al personaje (la acción 0xca). Quién lo escribe, sin leer.
    std::uint32_t flags7c = 0;
    // +0x78: 0x511290 lo pone en 0 (0x511d51). Del jinete lo leen el paso del caballo (con 0x4000, sin
    // polvo ni aliento: 0x444414 y 0x44455a), su dibujo (con 0x4000, 0x602e90 con 10: 0x445310) y 0x5ffdf0
    // (con 0x1000, el frame del caballo en 0: 0x5ffefc). Quién más lo escribe en el personaje, sin leer.
    std::uint32_t flags78 = 0;
    // +8: con 1, el paso del caballo de este jinete saca doce 0x60f (0x44468d) y lo vuelve a 0 (0x444758).
    // 0x511290 no lo escribe; quién lo pone en 1, sin leer.
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
    // +0x376: con 0x101, 0x4f2250 elige la acción de volar. Quién lo escribe, sin leer.
    std::uint16_t word376 = 0;
    std::uint8_t run = 0; // +0x38c, el Run de Caminar
    // +0x1c, +0x20 y +0x24: 0x511290 pone los dos primeros en 0 y el ángulo que recibe en el tercero.
    glm::vec3 angles{};
    // +0x28 y +0x2c, los ángulos de la cabeza, y +0x34 y +0x38, hacia dónde gira: los mueve 0x505de0
    // (0x5068eb..0x5069ab, Scenes::StepCharacterHead) y los lee la pose (0x4eb860). 0x511290 no los escribe:
    // quedan los de WinMain o los del uso anterior del registro. Del héroe, el blanco lo escribe MoveHero con
    // el mouse (0x59b1c2..0x59b287, CL7.4), sin portar.
    CharacterHead headTurn{};
    // +0x430: el cuervo que 0x420d60 crea y 0x420dd0 mueve dentro del cuerpo de este dueño.
    std::optional<DarkSpirit> darkSpirit;
    // +0x360: con 5, la capa de los GM, que 0x413fe0 pone en cada dibujo de un GM; del 1 al 3, el arma del
    // evento de Blood Castle (0x50de84). El 4 y del 6 al 13 los escriben 0x513430, 0x5139f0, 0x5bc740 y
    // 0x5be150, sin portar. 0x511290 no lo toca: queda el del uso anterior del registro.
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
    // +0x88: 0x511290 y la respuesta del F3 03 lo ponen en 1; con 8, 0x508f00 no lo pone en el
    // TerrainWall (0x508f60). Qué es, sin leer.
    std::uint8_t byte88 = 0;
    std::uint8_t controlCode = 0; // +0x1d8, el CtlCode de la respuesta del F3 03
    // +0x1d9: el nombre. La respuesta del F3 03 copia ahí los diez bytes del registro propio (0x4c47a2).
    std::array<std::byte, Protocol::Character::NameSize> name{};
    std::uint8_t byte1e3 = 0; // +0x1e3: la respuesta del F3 03 lo pone en 0 (0x4c4844); qué es, sin leer
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
    std::uint8_t byte374 = 0;
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
    // Profundidad de la entrada de un byte +0x38e, que el dibujo consulta antes de cada mano.
    std::uint8_t byte38eAccesses = 0;
    // +0x38e: el valor lógico. Con ')' (0x29) el lazo de las manos de 0x50ba30 saltea la derecha en la
    // acción 0x3d, y con '4' (0x34) 0x509340 pone el frame de lo que cuelga en 3,2 en las acciones 0x22 a
    // 0x97. Lo escriben 0x4cc530 y 0x4f5b70, sin portar: queda en 0.
    std::uint8_t byte38e = 0;
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
