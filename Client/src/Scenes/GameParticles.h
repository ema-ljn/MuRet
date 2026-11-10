#pragma once

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class Particles;
struct WorldObject;
} // namespace Client::World

// Emisión, paso y dibujo de las partículas del juego, sobre la tabla compartida de 0x743a198.
// Contrato estático: 06 § Después de los objetos, P-C55 (a).
namespace Client::Scenes
{
class IModelLibrary;
struct DrawnObject;

// 0x4ec110 con Translate=0, después de los dos sorteos del desplazamiento de cada gota.
// El caller presta el buffer global de la pose vigente y conserva sus huesos anteriores/dummy.
using WaterspoutBonePoint =
    std::function<Core::Expected<glm::vec3, std::string>(std::size_t bone, const glm::vec3& offset)>;

// 0x603011..0x60313f, después del rand() común de 0x602ef4: luz y V, guard de mitad de los frames,
// y dos 5f2/0 con sus rand() intercalados y huesos 1 y 4. Pool lleno no consume rand() de Create.
[[nodiscard]] Core::Expected<void, std::string>
StepGameWaterspoutFrame(World::WorldObject& object, World::Particles& particles, Render::Models::CrtRandom& random,
                        const float& worldClock, const WaterspoutBonePoint& bonePoint);

// Con el modelo que acaba de posarse. Un hueso que no escribió ese modelo necesita la pose global
// retenida, todavía sin representar: devuelve el error después de sus sorteos de X y Z.
[[nodiscard]] Core::Expected<void, std::string>
StepGameWaterspoutFrame(World::WorldObject& object, World::Particles& particles, Render::Models::CrtRandom& random,
                        const float& worldClock, const IModelLibrary& models, const DrawnObject& drawn);

// 0x637128 → 0x5446e0: también con el pool vacío consume los dos rand() del viento.
[[nodiscard]] Core::Expected<void, std::string>
StepGameParticles(World::Particles& particles, Render::Models::CrtRandom& random, const std::int32_t& map,
                  const std::function<double(float, float)>& terrainHeight = {});

// 0x637558 → 0x548550(0), en el pase de ojo después de los sprites. Conserva la geometría y
// las operaciones anteriores a un fallo y devuelve la dirección y el lugar que falló.
[[nodiscard]] Core::Expected<void, std::string>
DrawGameParticles(Render::ModelBatch& batch, World::Particles& particles, const IModelLibrary& textures,
                  const Render::Models::OriginalMatrix& view, float worldClock, std::uint8_t mode = 0);
} // namespace Client::Scenes
