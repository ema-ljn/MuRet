#pragma once

#include "Core/Expected.h"
#include "Kernel/ModelEffectArguments.h"
#include "Protocol/Outgoing.h"
#include "Effects/ModelEffects.h"
#include "Effects/WizardModelEffects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models
namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Terrain
{
struct TerrainLighting;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct JointState;
} // namespace Client::Scenes

// CL11, lote magos: los servicios de World::WizardModelEffects desde el mundo de la escena. El dueño (+0x108) de los
// efectos es un personaje del pool (ActorState: el byte vivo, +0x2, la posición, +0x78, la acción +0x111, +0x16c y
// +0x1bc), el héroe es [0x743a170], el registro propio es el de WorldRuntime y OwnCharacterRecord (+0xf04), el asedio
// es CastleSiegeState::siegeActive (0x4294b0), los joints van a la tabla del mundo (0x530860,
// Scenes/FaunaJointCreation) y 0x517a20 es Skills::HitTheTargetsAround.
namespace Client::Effects
{

// Lo que pone la escena: las tablas del frame, el terreno del paso (la altura y los atributos), el temblor de la
// cámara, el sonido y el envío del C1 10.
struct WizardSceneParts
{
    Particles::Particles* particles = nullptr;
    float worldClock = 0.0f;
    Particles::FrameSprites* sprites = nullptr;
    const Terrain::WorldTerrain* terrain = nullptr;
    float fixedHeight = 1200.0f;
    float* cameraShake = nullptr;
    std::function<void(Audio::SoundId)> sound{};
    std::function<void(Protocol::Outgoing)> send{};
    // La tabla de joints (0x530860); vacía, la del mundo (WorldState::DrawTail).
    std::span<Scenes::JointState> joints{};
    // Los esqueletos del juego que lee 0x4ea030 después de los del mapa (`faunaModels`), para el empujón
    // del 0xc2/4 (0x524a8d). Sin ellos, un 0x20a que empuja corta.
    const Models::IModelLibrary* models = nullptr;
    // The dynamic light (Terrain::TerrainState::dynamicLight) the effects add to (0x5f9780). nullptr: adding one stops.
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna). nullptr: readers find no model, writers stop.
    Assets::FaunaModels* faunaModels = nullptr;
};

[[nodiscard]] Effects::WizardEffectServices WizardEffectServicesOf(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                   const WizardSceneParts& parts);

// Los de un llamador del paso de los personajes (0x4fd5f0, 0x4f97b0, 0x4e6380…): sin el temblor de la cámara.
[[nodiscard]] Effects::WizardEffectServices WizardEffectServicesOf(const Actors::CharacterStepContext& context);

// 0x518cf0(tipo, posición, ángulos, luz, subtipo, dueño, clave, 0, 0, 0, 0): los argumentos de un efecto de los magos.
[[nodiscard]] Kernel::ModelEffectArguments WizardArguments(std::int16_t type, const glm::vec3& position,
                                                           const glm::vec3& angles, const glm::vec3& light,
                                                           std::int32_t subtype, void* owner = nullptr,
                                                           std::uint16_t key = 0xffff);

// El alta de un efecto de los magos (Effects::CreateWizardModelEffect) desde un llamador del paso de los personajes,
// con sus servicios (WizardEffectServicesOf). Con la tabla llena, nada y sin rand(). `callerLight` es la luz del
// llamador que escriben el 0xc1/1 y el /2.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateWizardEffect(const Actors::CharacterStepContext& context, const Kernel::ModelEffectArguments& arguments,
                   glm::vec3* callerLight = nullptr);
} // namespace Client::Effects
