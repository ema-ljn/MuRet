#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skinning.h"
#include "Scenes/CharacterDrawing.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Client::Render::Models
{
class CrtRandom;
struct ClothWorld;
} // namespace Client::Render::Models

namespace Client::Scenes
{
struct ActorState;

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
[[nodiscard]] BodyPassArgs ActorPassArgs(const ActorState& actor);

// El modelo compartido mientras 0x6002e0 lo dibuja: el color (+0x48..+0x50), el +0x88 (sin luz y con la UV corrida;
// 0xff es nullopt), la luz por vértice (+0x44) y las mallas que dejó 0x4ebc80 en los huesos del personaje.
struct MonsterBodyModel
{
    Render::ModelBatch& batch;
    const IModelLibrary& models;
    DrawnObject object;
    std::span<const Render::Models::SkinnedMesh> meshes;
    Core::Duration frameClock{};
    // El reloj del mundo (0x5767b88) en ms, que lee la onda de 0x4ecc30 (la bandera 0x400, 0x4ed775).
    float worldClock = 0.0f;
    // Las mallas de otro 0x4ebc80 en medio de 0x6002e0 (0x60cd60 desde 0x43fc9d, a otra escala): `meshes` las mira
    // desde ahí, como el original lee 0x5786f9c en lo que sigue.
    std::vector<Render::Models::SkinnedMesh> rescaled{};
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
    ActorState& actor;
    // Los huesos que posó 0x5ffdf0 en el buffer propio (+0x120).
    std::span<const Render::Models::BoneTransform> bones;
    // El mapa (0x6c9dd0) y el cuarto argumento de 0x602e90 (SpecialOf).
    std::int32_t map = 0;
    std::uint16_t special = 0;
    // El reloj del mundo (0x5767b88) en ms.
    float worldClock = 0.0f;
    // El rand() de la CRT y el contador de pasos protegido (0x7d3a0f0) con su textura del agua (0x7d2d360).
    Render::Models::CrtRandom& random;
    Render::Models::ClothWorld& cloth;
    // 0x4294b0: el byte del asedio.
    bool siegeActive = false;
    // El sonido pedido a 0x41ed10 (CL10); solo en los frames del original.
    std::optional<std::int32_t>* lastSoundId = nullptr;
    bool originalFrame = true;
    // 0x7d325c8, el temblor de la cámara (GameCharacterPass::cameraShake). nullptr: corta si un caso lo escribe.
    float* cameraShake = nullptr;
};

// 0x41ed10(id, objeto, 0) en un frame del original: lo anota para CL10.
void RequestBodySound(MonsterBodyFrame& frame, std::int32_t sound);

// 0x7d3a0f0: el contador de pasos lógicos, con los 4 rand() de su cierre si no hay otro acceso abierto.
[[nodiscard]] std::int32_t ReadLogicSteps(MonsterBodyFrame& frame);
} // namespace Client::Scenes
