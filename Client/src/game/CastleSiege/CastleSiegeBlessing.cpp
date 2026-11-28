#include "CastleSiege/CastleSiegeBlessing.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ActorState.h"
#include "Particles/Particles.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
// DAT006c72c8..6c72e7, cuatro pares float32 del ejecutable c32bbd5f.
constexpr std::array<std::array<float, 2>, 4> DefenderMarks{
    {{8200, 13000}, {10700, 13000}, {9400, 18200}, {9400, 22700}}};
bool InRange(const Kernel::ActorState& actor, float x, float y)
{
    const double dx = static_cast<double>(x) - actor.position.x;
    const double dy = static_cast<double>(y) - actor.position.y;
    // 4295b0 mantiene los cuadrados en la x87; unordered entra como C0/C2.
    return !(std::sqrt(dx * dx + dy * dy) > 380.0);
}
} // namespace
Core::Expected<void, std::string> DrawTheCastleBlessing(const Kernel::ActorState& actor, std::uint16_t heroGuildIndex,
                                                        float markX, float markY, std::int32_t logicSteps,
                                                        World::Particles& particles, Crt::CrtRandom& random)
{
    bool close = false;
    if ((actor.flags78 & 0x40000) != 0)
    {
        for (const auto& point : DefenderMarks)
            if (InRange(actor, point[0], point[1]))
            {
                close = true;
                break;
            }
    }
    else if (markX != 0.0f && !std::isnan(markX) && markY != 0.0f && !std::isnan(markY) &&
             heroGuildIndex == actor.word1f2)
        close = InRange(actor, markX, markY);
    if (!close || logicSteps % 10 != 0)
        return {};
    const auto emitted = particles.CreateChecked(World::SiegeBlessingParticle, actor.position, actor.angles,
                                                 actor.light, 0, 1.0f, 0.0f, random);
    if (!emitted.HasValue())
        return Core::Unexpected(std::string("0x42de7f: falla el alta de la bendición638"));
    return {};
}
} // namespace Client::Scenes
