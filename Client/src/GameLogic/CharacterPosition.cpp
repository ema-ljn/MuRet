#include "GameLogic/CharacterPosition.h"

#include "Render/Models/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>

namespace Client::GameLogic
{
namespace
{
// 0x508a80: constantes del archivo, perfil FP de ADR 0012.
constexpr std::uint16_t FloatingItem = 0x1c15;
constexpr std::uint16_t BobbingType = 0x0154;
constexpr std::int32_t NoMap = -1;
constexpr std::int32_t FirstHighMap = 8;
constexpr std::int32_t SecondHighMap = 10;
constexpr double FloatingOffset = 30.0;
constexpr double HighFloatingOffset = 90.0;
constexpr double BobbingAmplitude = 70.0;
constexpr float PhaseIncrement = std::bit_cast<float>(0x3e19999au);

double HeightOffset(CharacterPositionState state)
{
    // El mapa es un snapshot posterior a acumular XYZ; los otros dos filtros son cortocircuitos.
    const std::int32_t map = state.map.get();
    if (map == NoMap || state.item.get() != FloatingItem || state.safeZone.get() != 0)
    {
        return 0.0;
    }
    return map == FirstHighMap || map == SecondHighMap ? HighFloatingOffset : FloatingOffset;
}
} // namespace

void UpdateCharacterPosition(CharacterPositionState state, const std::function<double()>& speed,
                             const std::function<double(float, float)>& height)
{
    const auto matrix = Render::Models::OriginalAngleMatrix(state.angles);
    assert(speed);
    const auto forward = static_cast<float>(-speed());
    const glm::vec3 delta = Render::Models::OriginalVectorRotate({0.0f, forward, 0.0f}, matrix);
    const auto x = static_cast<float>(static_cast<double>(delta.x) + static_cast<double>(state.position.x));
    state.position.x = x;
    const auto y = static_cast<float>(static_cast<double>(delta.y) + static_cast<double>(state.position.y));
    state.position.y = y;
    state.position.z = static_cast<float>(static_cast<double>(delta.z) + static_cast<double>(state.position.z));
    const double offset = HeightOffset(state);
    assert(height);
    const double terrainHeight = height(x, y);
    // CMP del tipo precede al store de altura: la consulta puede modificarlo por alias.
    const bool bobbing = state.type.get() == BobbingType;
    state.position.z = static_cast<float>(offset == 0.0 ? terrainHeight : terrainHeight + offset);
    if (bobbing)
    {
        const auto sine = static_cast<float>(std::sin(static_cast<double>(state.phase)));
        const double product = std::fabs(static_cast<double>(sine)) * BobbingAmplitude;
        state.position.z = static_cast<float>((static_cast<double>(state.position.z) - product) + BobbingAmplitude);
    }
    state.phase = static_cast<float>(static_cast<double>(state.phase) + static_cast<double>(PhaseIncrement));
}
} // namespace Client::GameLogic
