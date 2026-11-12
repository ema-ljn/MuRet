#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Animation.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"

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
class ModelEffects;
class Particles;
struct TerrainLighting;
struct TerrainPolygon;
struct WorldTerrain;
} // namespace Client::World

// Los personajes en el dibujo del juego (0x6374dd, 0x510d00), hasta donde MuRet los tiene. Contrato: 06
// § El dibujo del juego.
namespace Client::Scenes
{
class CharacterCloth;
class IModelLibrary;
class WorldState;
struct DrawnCharacter;
struct FaunaDrawCarry;
struct FullSetScan;
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
    // Los joints (0x530860) y las partículas (0x53fe70) del frame, para lo que cuelga y el conjunto completo, y los
    // efectos con modelo (0x518cf0), para el 5ac/3 del contador de +0x35c.
    std::span<JointState> joints{};
    World::Particles* particles = nullptr;
    World::ModelEffects* effects = nullptr;
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
    // El trapecio de este dibujo (0x5fba90), que mira el cuervo (0x4182f0), y la altura fija de la cámara
    // (0x6c9d5c) de la altura del terreno bajo su sombra (0x5f93c0).
    const World::TerrainPolygon* polygon = nullptr;
    float fixedHeight = 1200.0f;
    // El terreno de esa altura; sin él, el del mapa del mundo (WorldState::Map). La selección tiene el suyo.
    const World::WorldTerrain* terrain = nullptr;
};

// El cuerpo del jugador ordinario, su luz del terreno y el override del héroe, reutilizando
// DrawCharacter, después lo que cuelga con DrawGameHanging (0x50db57..0x50e9fa): el arma a la espalda,
// las alas, Helper02 y las manos; los efectos del jugador con DrawGameCharacterEffects (0x50ea20..0x510ab6)
// y el cuervo con DrawGameRaven (0x420df0). Lo aún no portado corta con su dirección: monstruos, los casos
// de 0x509340 con joints o partículas y lo que crean los efectos. Entre frames del original, lo que cuelga y
// los efectos se dibujan sobre una copia del personaje y del rand(): no avanza ni gasta nada.
[[nodiscard]] Core::Expected<void, std::string> DrawGameCharacters(Render::ModelBatch& batch,
                                                                   const IModelLibrary& models, WorldState& world,
                                                                   const GameCharacterPass& pass);

// 0x50db57..0x510a7a sobre un 0x20a con el cuerpo, las partes y la tela ya dibujados (DrawCharacter con
// CharacterDrawPass::Body), en el login (la escena 2) y en la selección (la 4), que corren el mismo 0x50ba30 que el
// juego: el cuerpo en sus huesos propios y, con el alpha desde 0,01, la escala y la posición en el modelo
// compartido (0x6001b0, Q-C412), lo que cuelga (DrawGameHanging) y los efectos del jugador
// (DrawGameCharacterEffects), sin 0x510ef0, que sólo corre en la escena 5, y el cuervo (0x510abd → 0x420df0) con el
// trapecio de `pass` (sin él, lo dibuja el caller, como el login). `actor.drawnLight` ya tiene la luz de lo que
// cuelga (+0x3b4, 0x50db94); `scan` es lo que devolvió 0x505820 en este dibujo. Lee de `pass` el flujo global
// (clothWorld), el buffer global de la pose, los sprites, los joints, las partículas, los efectos, los ítems y, para
// el cuervo, la luz dinámica, el terreno y su altura fija. Entre frames del original, sobre una copia del personaje y
// del rand().
[[nodiscard]] Core::Expected<void, std::string>
DrawCharacterTail(Render::ModelBatch& batch, const IModelLibrary& models, WorldState& world, ActorState& actor,
                  std::size_t slot, const DrawnCharacter& character, const GameCharacterPass& pass,
                  const FullSetScan& scan, bool originalFrame);

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
