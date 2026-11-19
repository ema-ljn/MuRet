#pragma once

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/GameDrawTail.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct ClothWorld;
} // namespace Client::Render::Models

namespace Client::World
{
class FrameSprites;
class ModelEffects;
class Particles;
struct ModelEffect;
struct TerrainLighting;
} // namespace Client::World

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

namespace Client::Scenes
{
struct ActorState;
struct SkillHitsRequest;
} // namespace Client::Scenes

// Lo que comparten el alta de los joints (0x530860), su paso (0x535840) y las familias que viven en sus propios
// archivos (FaunaJointCreation.cpp despacha).
namespace Client::Scenes::JointKernels
{
// 0x530afa..0x530c38 (y el final de 0x535520): los cuatro puntos del primer tramo, (∓ancho/2, 0, 0) y (0, 0,
// ∓ancho/2) girados por la matriz y sumados a la posición. En el alta el ancho es el retenido del uso anterior.
void FirstQuad(JointState& joint, const Render::Models::OriginalMatrix& matrix);
void FirstQuad(JointState& joint);
// 0x535520(joint, matriz): la cuenta + 1 hasta los puntos − 1, corre los tramos y rehace el primero.
[[nodiscard]] Core::Expected<void, std::string> AppendJointTail(JointState& joint,
                                                                const Render::Models::OriginalMatrix& matrix);
[[nodiscard]] Core::Expected<void, std::string> AppendJointTail(JointState& joint);
// 0x535520 sin rehacer el primer tramo: la cuenta y la corrida de los tramos. Sólo para una cola que no se dibuja y
// cuya matriz es indefinida (Q-C1623).
[[nodiscard]] Core::Expected<void, std::string> AdvanceJointTail(JointState& joint);
// 0x535020(joint, matriz, cruzado): lo mismo que 0x535520, pero con `crossed` el segundo par de puntos va en y,
// (0, ∓ancho/2, 0), en vez de en z.
[[nodiscard]] Core::Expected<void, std::string>
AppendCrossedJointTail(JointState& joint, const Render::Models::OriginalMatrix& matrix, bool crossed);

// 0x4ec110(el modelo del jugador, los huesos propios del dueño + hueso · 0x30, offset, punto, 1): lo que el alta de
// algunos joints lee de un personaje dueño (el 614/2, 0x5313fe). La escala y el origen son los que el último dibujo
// dejó en el modelo del jugador (0x20a). Sin el servicio, esas altas cortan.
using PlayerBonePoint = std::function<glm::vec3(const ActorState& owner, std::size_t bone, const glm::vec3& offset)>;
struct CreationServices
{
    PlayerBonePoint playerBonePoint{};
    // 0x41ed10(sonido, 0, 0): el 0x613/1 suena al nacer (0x5330c5). Sin él, no suena (la CL10 decide cómo).
    std::function<void(std::int32_t)> soundRequest{};
    // El registro propio ([0x7681eb4]): la cuenta de sus accesos abiertos y su +0xf04, que el 0x613/0 y el /5 leen con
    // un acceso (0x532675, 0x532bab; el cierre que lo deja en 0 saca 3.848 rand()). Sin ellos, esas altas cortan.
    std::uint8_t* ownRecordDepth = nullptr;
    const std::uint8_t* ownVolley = nullptr;
    // Los 400 personajes (0x743a16c): el 614/0xf elige entre ellos los que encadena (0x531922). Sin ellos, corta.
    std::span<const ActorState> actors{};
};

// 0x520170(efecto): el choque de la flecha que el 0x60f/0xb y el /0x19 con skill 1 piden desde su paso (0x53ae6d), con
// la flecha en la posición del joint. Lo pone el que tiene el contexto de las flechas (Scenes/ArrowEffects.h).
using EffectHit = std::function<Core::Expected<void, std::string>(World::ModelEffect&)>;

// Lo que el 0x613/0, /5 y /0x13 de ancho 80 leen después de soltar su 0xd0 (0x539a50..0x539add): el asedio de
// 0x4294b0 ([0x7239ec], WorldRuntime::siegeActive), los atributos de 0x5f82c0 (0x7d0d348) y los blancos del héroe de
// 0x517a20 (Scenes::HitTheTargetsAround).
struct VolleyServices
{
    bool siegeActive = false;
    const Data::Terrain::TerrainAttributes* attributes = nullptr;
    std::function<Core::Expected<void, std::string>(const SkillHitsRequest&)> skillHits{};
};

// Lo que el paso de una familia de joints le pide al frame: el pool entero (las altas que hace el paso caen en él,
// 0x53703d), el lugar del joint, y los servicios que el paso común ya recibe.
struct StepServices
{
    std::span<JointState> joints;
    std::size_t index = 0;
    Render::Models::CrtRandom* random = nullptr;
    Render::Models::ClothWorld* cloth = nullptr;
    World::Particles* particles = nullptr;
    World::FrameSprites* sprites = nullptr;
    World::TerrainLighting* lighting = nullptr;
    float worldClock = 0;
    const std::function<void(std::int32_t)>* soundRequest = nullptr;
    // 0x53584f..0x53586c: [ebp−0x30] y [ebp+8], la posición menos el destino en x y en y, en float, antes del
    // movimiento común. Las lee el 0x613/0 y el /5 (0x539b33).
    float startDeltaX = 0;
    float startDeltaY = 0;
    // 0x5f93c0(x, y): la altura del terreno, como la deja en la x87. La pide el 0x613/0 y el /5 (0x539c19).
    const std::function<double(float, float)>* terrainHeight = nullptr;
    // Los 400 personajes (0x743a16c): el 0x10f/4 y el /9 miran el de su +0xa0c (0x535d63). Sin ellos, cortan.
    std::span<const ActorState> actors{};
    // El choque de la flecha dueña (0x520170). Sin él, el 0x60f/0xb y el /0x19 con skill 1 cortan.
    const EffectHit* effectHit = nullptr;
    // El paso de la familia sale de 0x535840 (ret) sin la cola ni la vida de 0x53c393 (el 0x618 con el dueño muerto,
    // 0x538216).
    bool* returned = nullptr;
    // La tabla de efectos con modelo (0x7491fd8): el 0x613/0x13 suelta ahí el 0x11b/0 (0x539b08). Sin ella, corta.
    World::ModelEffects* effects = nullptr;
    // El héroe ([0x743a170]): el 0x61c/0 compara su dueño con él (0x53b548). Sin él, el 0x61c/0 con dueño corta.
    const ActorState* hero = nullptr;
    // El 0x613/0, /5 y /0x13 de ancho 80 (0x539a50). Sin ellos, cortan ahí.
    const VolleyServices* volley = nullptr;
};

// El dueño de un joint de las familias de este archivo, cuando es un personaje (ActorState): el objeto del original,
// con el byte vivo en +0, la posición en +0x10, los ángulos en +0x1c y la luz en +0xf4.
[[nodiscard]] const ActorState* ActorOwner(const JointState& joint);

double D(float value);
float F32(double value);
} // namespace Client::Scenes::JointKernels
