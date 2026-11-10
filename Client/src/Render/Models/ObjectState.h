#pragma once

#include "Core/IClock.h"

#include <glm/ext/vector_float2.hpp>

#include <array>
#include <cstdint>
#include <optional>

// Lo que un objeto le pasa a RenderBody (0x4ef0c0) en cada pase de 0x60b3d0, y quién lo escribe
// (Client/docs/research/06-render-escenas-y-ui.md, "El script en un personaje" y "Lo que cuelga").
namespace Client::Render::Models
{
// Sin blendMesh: ninguna malla va por esa rama de 0x4ecc30, salvo las de R.
inline constexpr std::int16_t NoBlendMesh = -1;
// Un blendMesh menor que −1: 0x4ecc30 pasa todas las mallas por la rama del blendMesh.
inline constexpr std::int16_t EveryMeshBlends = -2;
// Sin malla oculta.
inline constexpr std::int16_t NoHiddenMesh = -1;

// El +0x64 (el blendMesh: 0x4ecc30 lo compara con la textura de cada malla), el +0x68 (su luz), el
// +0x6c y el +0x70 (la U y la V que se suman a las UV) y el +0x58 (la malla que el pase base no
// dibuja) del objeto. Los de un jugador, los de siempre (0x511290).
struct ObjectState
{
    std::int16_t blendMesh = NoBlendMesh;
    float light = 1.0f;
    glm::vec2 uv{};
    std::int16_t hiddenMesh = NoHiddenMesh;

    friend bool operator==(const ObjectState&, const ObjectState&) = default;
};

// Los dos rand() que puede llamar 0x607b60, en su orden (de 0 a 32767, como el de MSVC).
using ObjectRandoms = std::array<int, 2>;

// El objeto que 0x509340 le arma, en cada frame, a lo que cuelga: 0x607b60 con el tipo `type` (el
// modelo del ítem) y lo que 0x60b3d0 le escribe por su tipo antes de dibujarlo (la luz de Wing05,
// Wing06 y Staff12).
// 607b60 sin las luces posteriores de60b3d0.
[[nodiscard]] ObjectState NativeItemObjectState(std::uint16_t type, Core::Duration clock, ObjectRandoms randoms);

[[nodiscard]] ObjectState HangingObjectState(std::uint16_t type, Core::Duration clock, ObjectRandoms randoms);

// La luz que 0x60b3d0 le escribe al objeto que dibuja, por el modelo: la de los FShoots_R de
// PantMale25 y HelmMale25 late, y las Test20 la vuelven a 1. Las demás no la tocan.
[[nodiscard]] std::optional<float> ModelLightOf(std::uint16_t model, Core::Duration clock);

// La luz que 0x50ba30 le pone al personaje de la clase 4 en la parte del casco: sin(0,001 · T) ·
// 0,1 + 0,7, con los floats de 0x6b89c8, 0x6b87b8 y 0x6b8a5c.
[[nodiscard]] float MaskedClassLightAt(Core::Duration clock);
} // namespace Client::Render::Models
