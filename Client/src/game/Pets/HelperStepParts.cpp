#include "Pets/HelperStepParts.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Maps/MapId.h"
#include "Math/X87.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Pets/HelperTable.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "Particles/Particles.h"
#include "Terrain/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Pets
{
namespace
{
// 0x4440bb and 0x4440c9: the rider's position times 0.01f (0x6b86c0), truncated (0x69fd3c).
constexpr double CellsPerUnit = static_cast<double>(0.01f);
// 0x5f82a0: the low byte of each coordinate.
constexpr std::int32_t CellMask = 0xff;
// 0x4440e5: bit 0 of the attribute's low byte.
constexpr std::uint16_t SafeCellBit = 0x01;
// 0x444484 and 0x4444bd: on map 2 the dust is the 5f2; on 10, none.
using Maps::DeviasMap;
using Maps::IcarusMap;
// 0x3f800000
constexpr float ParticleScale = 1.0f;
constexpr std::int32_t DustSubtype = 0;
// 0x444425..0x444476: (rand() & 63) − 32 in x and y, and (rand() & 31) − 16 in z.
constexpr int DustSpread = 64;
constexpr int DustSpreadCentre = 32;
constexpr int DustHeight = 32;
constexpr int DustHeightCentre = 16;
// 0x444325..0x4443d1: the 5f2 count, rand() % 5 + 5, each at rand() % 50 − 25 in x and y and (rand() & 15) − 8 − 10
// (0x6b87a8) in z.
constexpr int PuffSpan = 5;
constexpr int PuffBase = 5;
constexpr int PuffSpread = 50;
constexpr int PuffCentre = 25;
constexpr int PuffHeight = 16;
constexpr int PuffHeightCentre = 8;
constexpr double PuffSink = 10.0;
// 0x444817..0x44481d: the rider's +0x80 values with which alpha falls; 0x44481f, the others'.
constexpr std::uint8_t FadingRider = 1;
constexpr std::uint8_t OtherFadingRider = 2;
constexpr float ShownAlpha = 1.0f;
constexpr double FadeRate = static_cast<double>(0.1f); // 0x6b87b8
constexpr float HiddenAlpha = 0.0f;

using Math::F32;

// x + v on the x87: the exact integer, the helper's float and a single rounding to float at the fstp.
float Shifted(int by, float value)
{
    return F32(static_cast<double>(by) + static_cast<double>(value));
}
} // namespace

Core::Expected<bool, std::string> InTheRidersSafeCell(const HelperStepContext& context, const Kernel::ActorState& rider,
                                                      std::string_view address)
{
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
    {
        return Core::Unexpected(std::format("{}: el paso del ayudante lee el atributo de la celda del jinete "
                                            "(0x7d0d348) sin el mapa cargado",
                                            address));
    }
    const std::optional<std::int32_t> y =
        Math::OriginalFtolLow(static_cast<double>(rider.position.y) * CellsPerUnit); // 0x4440c1
    const std::optional<std::int32_t> x =
        Math::OriginalFtolLow(static_cast<double>(rider.position.x) * CellsPerUnit); // 0x4440d0
    if (!x || !y)
    {
        return Core::Unexpected(std::format("{}: la posición del jinete por 0,01 no entra en el entero de 0x69fd3c, y "
                                            "su continuación no está observada",
                                            address));
    }
    const std::size_t cell =
        Data::Terrain::CellIndex(static_cast<std::uint8_t>(*x & CellMask), static_cast<std::uint8_t>(*y & CellMask));
    return (terrain.attributes->cells[cell] & SafeCellBit) != 0;
}

Core::Expected<glm::vec3, std::string> GlobalPosePoint(const Kernel::WorldState& world, std::size_t bone,
                                                       const glm::vec3& offset, std::string_view address)
{
    return GlobalPosePoint(world.PoseCarry().bones, bone, offset, address);
}

Core::Expected<glm::vec3, std::string> GlobalPosePoint(std::span<const Math::BoneTransform> bones, std::size_t bone,
                                                       const glm::vec3& offset, std::string_view address)
{
    constexpr std::size_t GlobalPoseBones = 200;
    if (bone >= GlobalPoseBones)
    {
        return Core::Unexpected(
            std::format("{}: el hueso {} pasa los 200 del buffer global de la pose (0x68b181c)", address, bone));
    }
    if (bone >= bones.size())
    {
        return glm::vec3(0.0f);
    }
    return Math::OriginalVectorTransform(offset, Math::OriginalMatrixOf(bones[bone]));
}

Core::Expected<void, std::string> EmitHelperParticle(const HelperStepContext& context, std::int32_t type,
                                                     std::int32_t subtype, const glm::vec3& position,
                                                     const glm::vec3& angles, const glm::vec3& light,
                                                     std::string_view address)
{
    const auto created = context.world.Particles().CreateChecked(type, position, angles, light, subtype, ParticleScale,
                                                                 context.worldClock, context.random);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    }
    return {};
}

Core::Expected<void, std::string> ScatterTheDust(const Kernel::HelperState& helper, const HelperStepContext& context,
                                                 std::string_view address)
{
    Crt::CrtRandom& random = context.random;
    const int x = (random.Next() % DustSpread) - DustSpreadCentre;
    const int y = (random.Next() % DustSpread) - DustSpreadCentre;
    const int z = (random.Next() % DustHeight) - DustHeightCentre;
    const glm::vec3 place(Shifted(x, helper.position.x), Shifted(y, helper.position.y), Shifted(z, helper.position.z));
    const std::int32_t map = context.world.Runtime().map;
    if (map == IcarusMap)
    {
        return {};
    }
    const std::int32_t type = map == DeviasMap ? std::int32_t{Particles::BreathParticle} : Particles::DustParticle;
    return EmitHelperParticle(context, type, DustSubtype, place, helper.angles, HelperDustLight, address);
}

Core::Expected<void, std::string> PuffTheIcarusDust(const glm::vec3& base, const glm::vec3& angles,
                                                    const HelperStepContext& context, std::string_view address)
{
    Crt::CrtRandom& random = context.random;
    static_cast<void>(random.Next()); // 0x444325..0x444336 and 0x444d6a..0x444d7b: the guard, always positive.
    int puffs = 0;
    do
    {
        const int x = (random.Next() % PuffSpread) - PuffCentre;
        const int y = (random.Next() % PuffSpread) - PuffCentre;
        const int z = (random.Next() % PuffHeight) - PuffHeightCentre;
        const glm::vec3 puff(Shifted(x, base.x), Shifted(y, base.y),
                             F32((static_cast<double>(z) + static_cast<double>(base.z)) - PuffSink));
        if (auto emitted = EmitHelperParticle(context, Particles::BreathParticle, DustSubtype, puff, angles,
                                              HelperDustLight, address);
            !emitted.HasValue())
        {
            return emitted;
        }
        ++puffs;
    } while (puffs < (random.Next() % PuffSpan) + PuffBase);
    return {};
}

void FadeWithTheRider(Kernel::HelperState& helper, const Kernel::ActorState& rider)
{
    if (rider.byte80 != FadingRider && rider.byte80 != OtherFadingRider)
    {
        helper.horse.alpha = ShownAlpha;
        return;
    }
    const double faded = static_cast<double>(helper.horse.alpha) - FadeRate;
    helper.horse.alpha = faded < 0.0 ? HiddenAlpha : F32(faded);
}
} // namespace Client::Pets
