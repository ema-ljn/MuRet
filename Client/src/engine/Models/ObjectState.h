#pragma once

#include "Core/IClock.h"
#include "EngineApi.h"

#include <glm/ext/vector_float2.hpp>

#include <array>
#include <cstdint>
#include <optional>

// What an object passes to RenderBody (0x4ef0c0) in each pass of 0x60b3d0, and who writes it
// (Client/docs/research/06-render-escenas-y-ui.md, "El script en un personaje" and "Lo que cuelga").
namespace Client::Models
{
// No blendMesh: no mesh goes down that branch of 0x4ecc30, except R's.
inline constexpr std::int16_t NoBlendMesh = -1;
// A blendMesh below −1: 0x4ecc30 sends every mesh down the blendMesh branch.
inline constexpr std::int16_t EveryMeshBlends = -2;
inline constexpr std::int16_t NoHiddenMesh = -1;

// The object's +0x64 (the blendMesh: 0x4ecc30 compares it with each mesh's texture), +0x68 (its light), +0x6c and
// +0x70 (the U and V added to the UVs) and +0x58 (the mesh the base pass does not draw). A player's are the usual ones
// (0x511290).
struct ObjectState
{
    std::int16_t blendMesh = NoBlendMesh;
    float light = 1.0f;
    glm::vec2 uv{};
    std::int16_t hiddenMesh = NoHiddenMesh;

    friend bool operator==(const ObjectState&, const ObjectState&) = default;
};

// The two rand() 0x607b60 may call, in order (0 to 32767, like MSVC's).
using ObjectRandoms = std::array<int, 2>;

// The object 0x509340 builds every frame for what hangs: 0x607b60 with type `type` (the item's model) and what
// 0x60b3d0 writes for its type before drawing it (the light of Wing05, Wing06 and Staff12).
// 607b60 without the later lights of 60b3d0.
[[nodiscard]] ENGINE_API ObjectState NativeItemObjectState(std::uint16_t type, Core::Duration clock,
                                                           ObjectRandoms randoms);

[[nodiscard]] ENGINE_API ObjectState HangingObjectState(std::uint16_t type, Core::Duration clock,
                                                        ObjectRandoms randoms);

// The light 0x60b3d0 writes on the object it draws, by model: the FShoots_R one of PantMale25 and HelmMale25 pulses,
// and the Test20 ones set it back to 1. The rest leave it alone.
[[nodiscard]] ENGINE_API std::optional<float> ModelLightOf(std::uint16_t model, Core::Duration clock);

// The light 0x50ba30 gives the class 4 character on the helmet part: sin(0.001 · T) · 0.1 + 0.7, with the floats of
// 0x6b89c8, 0x6b87b8 and 0x6b8a5c.
[[nodiscard]] ENGINE_API float MaskedClassLightAt(Core::Duration clock);
} // namespace Client::Models
