#include "Terrain/ColoredDecals.h"

#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/X87.h"
#include "World/ModelEffects.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Terrain
{
namespace
{
using Math::F32;

// 0x51ef79..0x51ef9d y 0x51eedd..0x51ef09: fild de rand() % 50 + 50, por 0,04f (0x6b8acc) y fstp.
float GrownScale(Crt::CrtRandom& random)
{
    constexpr int ScaleSpan = 50, ScaleBase = 50;
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3d23d70au)); // 0x6b8acc, 0,04f
    return F32(static_cast<double>(random.Next() % ScaleSpan + ScaleBase) * ScaleStep);
}
} // namespace

void BeginColoredDecal(World::ModelEffect& effect, Crt::CrtRandom& random)
{
    constexpr std::int32_t PlainLife = 20;                            // 0x51eebb
    constexpr std::int32_t PulsingLife = 40;                          // 0x51ef79
    constexpr std::int32_t BindingLife = 60;                          // 0x51eedd
    constexpr std::int32_t ChildLife = 40;                            // 0x51ef28
    constexpr std::int32_t GrowingLife = 10;                          // 0x51ef64
    constexpr int FullTurn = Math::FullTurn;                          // 0x51ef35
    constexpr float GrowingScale = std::bit_cast<float>(0x3dcccccdu); // 0x51ef6c, 0,1f
    effect.life = PlainLife;
    switch (effect.subtype)
    {
    case PulsingDecalSubtype:
    case CoolPulsingDecalSubtype:
        effect.life = PulsingLife;
        effect.transform.scale = GrownScale(random);
        break;
    case BindingDecalSubtype:
        effect.life = BindingLife;
        effect.transform.scale = GrownScale(random);
        // 0x51ef0c..0x51ef18: la partida (+0x17c) en la posición del argumento, la misma que copió el prefijo.
        effect.start = effect.transform.position;
        break;
    case BindingChildSubtype:
        effect.life = ChildLife;
        effect.transform.angles.z = static_cast<float>(random.Next() % FullTurn);
        break;
    case SpawningDecalSubtype:
        effect.life = PlainLife; // 0x51ef52
        break;
    case GrowingDecalSubtype:
        effect.life = GrowingLife;
        effect.transform.scale = GrowingScale;
        break;
    default:
        break; // 0x51db73: el prefijo y la vida.
    }
}

std::optional<std::size_t> CreateColoredDecal(std::span<World::ModelEffect> table,
                                              const World::ModelEffectArguments& arguments, Crt::CrtRandom& random)
{
    const auto slot = World::BeginModelEffect(table, arguments);
    if (slot.has_value())
        BeginColoredDecal(table[*slot], random);
    return slot;
}
} // namespace Client::Terrain
