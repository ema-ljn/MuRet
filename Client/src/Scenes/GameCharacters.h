#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Animation.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Scenes/ActorState.h"

#include <cstddef>
#include <cstdint>
#include <functional>
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
struct TerrainLighting;
} // namespace Client::World

// Los personajes en el dibujo del juego (0x6374dd, 0x510d00), hasta donde MuRet los tiene. Contrato: 06
// § El dibujo del juego.
namespace Client::Scenes
{
class CharacterCloth;
class IModelLibrary;
class WorldState;
struct FaunaDrawCarry;
struct ItemDrawContext;

// Los recursos del pase 0x510d00 en el juego. La luz es el buffer dinámico, no su base sombreada.
// Los dos lugares de picking arrancan en -1 en la .data (0x6c8e30 y 0x6c8e2c); el caller publica
// sus cambios cuando porta 0x59ec80. Las telas pertenecen a cada lugar entre frames. Lo que cuelga
// (GameHanging.h) se posa en el buffer global de la pose y se dibuja con los globales de 0x60b3d0.
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
    // El buffer global de la pose (0x68b181c) y los globales de 0x60b3d0 que presta el dueño del frame.
    FaunaDrawCarry* poseCarry = nullptr;
    const ItemDrawContext* itemContext = nullptr;
    Render::Models::OriginalFramePace pace = Render::Models::OriginalFramePace::ByTheClock;
    std::function<void(std::uint16_t, std::span<const Render::Models::BoneTransform>,
                       const Render::Models::ObjectTransform&, bool)>
        onPose{};
    // Publica las matrices del dibujo intermedio al buffer que presta el caller, sin avanzar el estado.
    bool publishProjectedPose = false;
    std::function<std::span<const Render::Models::BoneTransform>(std::uint16_t)> poseBones{};
    // Los modelos del mundo, donde 0x618970 carga el Angel (0x1f7) de Chaos Castle: en el original, el mismo
    // arreglo global. Sin ellos, un jugador en los mapas 18 a 23 corta.
    const IModelLibrary* worldModels = nullptr;
};

// El cuerpo del jugador ordinario, su luz del terreno y el override del héroe, reutilizando
// DrawCharacter, y después lo que cuelga con DrawGameHanging (0x50db57..0x50e9fa): el arma a la espalda,
// las alas, Helper02 y las manos. Lo aún no portado corta con su dirección: monstruos, los casos de
// 0x509340 con joints o partículas y los efectos especiales. Entre frames del original, lo que cuelga se
// dibuja sobre una copia del personaje y del rand(): no avanza ni gasta nada.
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
