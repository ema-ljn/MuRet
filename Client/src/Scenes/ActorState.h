#pragma once

#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/Pose.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>

// Un registro del pool durable de personajes, de 0x438 bytes en el original (06, "Los personajes de
// la lista"). Es una proyección con lo que MuRet lee hoy, no el layout. Arranca en cero, como lo deja
// el REP STOSD de WinMain (0x4bafa6..0x4bafb3).
namespace Client::Scenes
{
// El modelo de una parte sin ítem.
inline constexpr std::uint16_t NoItemModel = 0xffff;

// Una parte puesta, de a 0x20 bytes: las cinco del cuerpo desde +0x218, las manos en +0x2b8 y
// +0x2d8, las alas en +0x2f8 y la mascota en +0x318 (06, "El equipo propio en el personaje").
struct ActorItemPart
{
    std::uint16_t model = 0; // +0, 0xffff sin ítem
    std::uint8_t level = 0;  // +2
    // +3 y +4: 0x512a30 copia tal cual el byte D+0x1b y el D+0x1c del ítem.
    std::uint8_t flags1b = 0;
    std::uint8_t flags1c = 0;
};

struct ActorState
{
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
    // La acción en +0x111, la anterior en +0x112, el frame en +0x114 y la key anterior en +0x118: lo que
    // pone 0x4ea030.
    Render::Models::AnimationState animation;
    std::uint8_t characterClass = 0;                            // +0x1d4
    std::uint8_t skin = 0;                                      // +0x1d5
    std::uint16_t head = 0;                                     // +0x1f8, de 0x512930: 0xffff sin cabeza
    std::array<ActorItemPart, GameLogic::BodyPartCount> body{}; // +0x218, en el orden de GameLogic::BodyPart
    std::array<ActorItemPart, 2> hands{};                       // +0x2b8 la derecha y +0x2d8 la izquierda
    ActorItemPart wings;                                        // +0x2f8
    ActorItemPart pet;                                          // +0x318
    // +0x376: con 0x101, 0x4f2250 elige la acción de volar. Quién lo escribe, sin leer.
    std::uint16_t word376 = 0;
    std::uint8_t run = 0; // +0x38c, el Run de Caminar
    // +0x3e2, la zona segura: cualquier byte distinto de 0. 0x511290 y 0x505de0 copian el bit 0x01 del
    // .att de la celda.
    std::uint8_t safeZone = 0;
    // +0x3e3: distinto de 0, 0x512a30 y 0x512930 no tocan la ropa. 0x511290 lo pone en 0 (0x511d68).
    std::uint8_t byte3e3 = 0;
};
} // namespace Client::Scenes
