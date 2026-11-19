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
class Particles;
struct TerrainLighting;
} // namespace Client::World

namespace Client::Scenes
{
struct ActorState;
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
};

// El dueño de un joint de las familias de este archivo, cuando es un personaje (ActorState): el objeto del original,
// con el byte vivo en +0, la posición en +0x10, los ángulos en +0x1c y la luz en +0xf4.
[[nodiscard]] const ActorState* ActorOwner(const JointState& joint);

double D(float value);
float F32(double value);
} // namespace Client::Scenes::JointKernels
