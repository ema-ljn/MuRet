#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Animation.h"
#include "Scenes/ActorState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
}

namespace Client::Render::Models
{
struct ClothWorld;
}

namespace Client::World
{
class FrameSprites;
class ScepterGlowCarry;
struct TerrainLighting;
} // namespace Client::World

// Los personajes en el dibujo del juego (0x6374dd, 0x510d00), hasta donde MuRet los tiene. Contrato: 06
// § El dibujo del juego.
namespace Client::Scenes
{
class CharacterCloth;
class IModelLibrary;
class WorldState;

// Los recursos del pase 0x510d00 en el juego. La luz es el buffer dinámico, no su base sombreada.
// Los dos lugares de picking arrancan en -1 en la .data (0x6c8e30 y 0x6c8e2c); el caller publica
// sus cambios cuando porta 0x59ec80. Las telas y los cetros pertenecen a cada lugar entre frames.
struct GameCharacterPass
{
    const World::TerrainLighting* dynamicLighting = nullptr;
    Render::Models::ClothWorld* clothWorld = nullptr;
    Core::Duration frameClock{};
    int originalFrames = 1;
    std::int32_t hovered = -1;
    std::int32_t targeted = -1;
    std::span<CharacterCloth> cloths{};
    World::FrameSprites* sprites = nullptr;
    std::span<World::ScepterGlowCarry> scepters{};
    Render::Models::OriginalFramePace pace = Render::Models::OriginalFramePace::ByTheClock;
};

// El cuerpo del jugador ordinario, su luz del terreno y el override del héroe, reutilizando
// DrawCharacter; el rand de 0x50db57 separa cuerpo/tela y lo que cuelga. Lo aún no portado corta
// con su dirección: monstruos, armas a espalda/en mano, alas/Helper02 y efectos especiales.
[[nodiscard]] Core::Expected<void, std::string> DrawGameCharacters(Render::ModelBatch& batch,
                                                                   const IModelLibrary& models, WorldState& world,
                                                                   const GameCharacterPass& pass);

// 0x510d00: el bit del CtlCode (+0x1d8) con el que el héroe no se dibuja en la escena 5.
inline constexpr std::uint8_t UndrawnHeroControlBit = 0x04;

// Variante sin recursos de dibujo: recorre el pool sin rand y conserva el corte con un vivo y visto.
// Con otro que el héroe, la rama del asedio (0x4294b0 lee 0x7239ec, que MuRet deja en 0, P-C41 c) no corre. El héroe
// (`hero`) con +0x1d8 & 4 no se dibuja: 0x510e57 le pone +0x13c en 1.000 y +0x148, +0x158 y +0x168 en 1.
// Los demás, vivos y vistos (+0x16c), necesitan los recursos del overload anterior: corta ahí
// (0x510ea1), con los de antes ya mirados. Al final,
// 0x42da60: 0x7239dc y 0x7239e0 en 0 (MuRet no los tiene) y, con 0x7239d9 en 0 (solo el mapa 30 lo pone
// en 1), World::OpenTheGates sobre `walls` (Q-C276).
[[nodiscard]] Core::Expected<void, std::string> DrawGameCharacters(std::span<ActorState> actors, std::size_t hero,
                                                                   Data::Terrain::TerrainLayer<std::uint16_t>& walls);
} // namespace Client::Scenes
