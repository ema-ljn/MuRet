#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/Skinning.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct ClothWorld;
} // namespace Client::Models

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Actors
{
class IEffectHooks;
} // namespace Client::Actors

namespace Client::Scenes
{
struct JointState;
} // namespace Client::Scenes

namespace Client::Monsters::Drawing
{

// Lo que 0x4ef0c0 (RenderBody) y 0x4ecc30 (RenderMesh) reciben después de las banderas: el alpha, el blendMesh, su
// luz, la U, la V, la malla oculta (solo RenderBody) y la textura (−1: la de cada malla; si no, ese slot).
struct BodyPassArgs
{
    float alpha = 1.0f;
    std::int32_t blendMesh = -1;
    float light = 1.0f;
    glm::vec2 uv{};
    std::int32_t hiddenMesh = -1;
    std::int32_t texture = -1;
};

// Los de un pase con lo del personaje: +0x174, +0x64, +0x68, +0x6c (ActorState::textureU), +0x70 (en 0: solo el alta
// la escribe), +0x58 y la textura −1.
[[nodiscard]] BodyPassArgs ActorPassArgs(const Kernel::ActorState& actor);

// El modelo compartido mientras 0x6002e0 lo dibuja: el color (+0x48..+0x50), el +0x88 (sin luz y con la UV corrida;
// 0xff es nullopt), la luz por vértice (+0x44) y las mallas que dejó 0x4ebc80 en los huesos del personaje.
struct MonsterBodyModel
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& models;
    Actors::Drawing::DrawnObject object;
    std::span<const Models::SkinnedMesh> meshes;
    Core::Duration frameClock{};
    // El reloj del mundo (0x5767b88) en ms, que lee la onda de 0x4ecc30 (la bandera 0x400, 0x4ed775).
    float worldClock = 0.0f;
    // Las mallas de otro 0x4ebc80 en medio de 0x6002e0 (0x60cd60 desde 0x43fc9d, a otra escala): `meshes` las mira
    // desde ahí, como el original lee 0x5786f9c en lo que sigue.
    std::vector<Models::SkinnedMesh> rescaled{};
    // RGB de5ffce0 antes de los tintes de6002e0:42e458 lo reconstruye para el proyectil12b.
    glm::vec3 preludeColor{1.0f};
    // 6c881c vigente: el accesorio12b de42e3d0 conserva el modo del actor (1,1.2,1.4).
    float boneScale = 1.0f;
};

// 0x4ef0c0: RenderBody(flags, args) con el estado del modelo. El pase base (2 con la textura de cada malla) es el de
// DrawObjectFromMeshes; los demás, el de DrawItemMeshPass, que deja en el color del modelo lo que guarda. Una
// textura negativa distinta de −1 o banderas fuera de las de DrawItemMeshPass son un error.
[[nodiscard]] Core::Expected<void, std::string> DrawBodyPass(MonsterBodyModel& model, std::uint32_t flags,
                                                             const BodyPassArgs& args);

// 0x4ecc30: RenderMesh(mesh, flags, args) (sin la malla oculta, que no mira). Una malla fuera del modelo no dibuja.
[[nodiscard]] Core::Expected<void, std::string> DrawMeshPass(MonsterBodyModel& model, std::int16_t mesh,
                                                             std::uint32_t flags, const BodyPassArgs& args);

// Lo que 0x6002e0 y su cadena leen y escriben fuera del modelo, para un monstruo o un NPC de 0x602e90.
struct MonsterBodyFrame
{
    MonsterBodyModel& model;
    Kernel::ActorState& actor;
    // Los huesos que posó 0x5ffdf0 en el buffer propio (+0x120).
    std::span<const Math::BoneTransform> bones;
    // El mapa (0x6c9dd0) y el cuarto argumento de 0x602e90 (SpecialOf).
    std::int32_t map = 0;
    std::uint16_t special = 0;
    // El reloj del mundo (0x5767b88) en ms.
    float worldClock = 0.0f;
    // El rand() de la CRT y el contador de pasos protegido (0x7d3a0f0) con su textura del agua (0x7d2d360).
    Crt::CrtRandom& random;
    Models::ClothWorld& cloth;
    // 0x4294b0: el byte del asedio.
    bool siegeActive = false;
    // El sonido pedido a 0x41ed10 (CL10); solo en los frames del original.
    std::optional<Audio::SoundId>* lastSoundId = nullptr;
    std::function<void(Audio::SoundId)> soundRequest{};
    bool originalFrame = true;
    // 0x7d325c8, el temblor de la cámara (GameCharacterPass::cameraShake). nullptr: corta si un caso lo escribe.
    float* cameraShake = nullptr;
    // 4ee770 consume el mismo pool de518cf0 que el resto del frame.
    Effects::ModelEffects* effects = nullptr;
    // Who creates the stones and shells of 0x4ee770 (GameCharacterPass::effectHooks). nullptr: they stop.
    const Actors::IEffectHooks* effectHooks = nullptr;
    // Las partículas del frame (0x53fe70), para los 0x607/2 de 0x4ee770 al morir los 0x191..0x197 (0x43ed6c).
    Particles::Particles* particles = nullptr;
    // 0x5f93c0, la altura del terreno que piden las altas de los pedazos de Kundun al caer (0x51e26e). Vacía, esas
    // altas cortan.
    std::function<double(float, float)> terrainHeight{};
    // 0x530860: la tabla de joints (0x74a8d78, WorldState::DrawTail), para los rayos de Kundun al morir (0x43f20d),
    // que sólo la tocan en un frame del original; entre frames, la copia proyectada (GameCharacterPass::joints).
    // Vacía: corta ahí.
    std::span<Scenes::JointState> joints{};
};

// 0x41ed10(id, objeto, 0) en un frame del original: lo anota para CL10.
void RequestBodySound(MonsterBodyFrame& frame, Audio::SoundId sound);

// 0x7d3a0f0: el contador de pasos lógicos, con los 4 rand() de su cierre si no hay otro acceso abierto.
[[nodiscard]] std::int32_t ReadLogicSteps(MonsterBodyFrame& frame);
} // namespace Client::Monsters::Drawing
