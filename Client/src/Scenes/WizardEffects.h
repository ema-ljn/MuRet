#pragma once

#include "Protocol/Outgoing.h"
#include "World/WizardModelEffects.h"

#include <cstdint>
#include <functional>
#include <span>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class FrameSprites;
class Particles;
struct WorldTerrain;
} // namespace Client::World

// CL11, lote magos: los servicios de World::WizardModelEffects desde el mundo de la escena. El dueño (+0x108) de los
// efectos es un personaje del pool (ActorState: el byte vivo, +0x2, la posición, +0x78, la acción +0x111, +0x16c y
// +0x1bc), el héroe es [0x743a170], el registro propio es el de WorldRuntime y OwnCharacterRecord (+0xf04), el asedio
// es WorldRuntime::siegeActive (0x4294b0), los joints van a la tabla del mundo (0x530860, Scenes/FaunaJointCreation) y
// 0x517a20 es Scenes::HitTheTargetsAround.
namespace Client::Scenes
{
class WorldState;
struct CharacterStepContext;
struct JointState;

// Lo que pone la escena: las tablas del frame, el terreno del paso (la altura y los atributos), el temblor de la
// cámara, el sonido y el envío del C1 10.
struct WizardSceneParts
{
    World::Particles* particles = nullptr;
    float worldClock = 0.0f;
    World::FrameSprites* sprites = nullptr;
    const World::WorldTerrain* terrain = nullptr;
    float fixedHeight = 1200.0f;
    float* cameraShake = nullptr;
    std::function<void(std::int32_t)> sound{};
    std::function<void(Protocol::Outgoing)> send{};
    // La tabla de joints (0x530860); vacía, la del mundo (WorldState::DrawTail).
    std::span<JointState> joints{};
};

[[nodiscard]] World::WizardEffectServices WizardEffectServicesOf(WorldState& world, Render::Models::CrtRandom& random,
                                                                 const WizardSceneParts& parts);

// Los de un llamador del paso de los personajes (0x4fd5f0, 0x4f97b0, 0x4e6380…): sin el temblor de la cámara.
[[nodiscard]] World::WizardEffectServices WizardEffectServicesOf(const CharacterStepContext& context);
} // namespace Client::Scenes
