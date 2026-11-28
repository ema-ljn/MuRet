#include "MapScripts/CastleSiege/CastleSiegeObjectServices.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/OriginalRetainedMath.h"
#include "Particles/KalimaParticleKernels.h"
#include "Particles/Particles.h"
#include "Kernel/WorldObject.h"
#include "Kernel/ObjectPass.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::MapScripts::CastleSiege
{
namespace
{
constexpr int EmitChance = 3;

// 0x53fe70(type, +0x10, +0x1c, (1, 1, 1), subtype, scale, 0). None of the 30's reads the scene or the owner.
Core::Expected<void, std::string> Emit(const Kernel::GameObjectPass& pass, const Kernel::WorldObject& object,
                                       std::int32_t type, std::int32_t subtype, float scale, std::string_view address)
{
    if (pass.particles == nullptr)
        return Core::Unexpected(
            std::format("{}: falta el pool de partículas 0x743a198 para {:#x}/{}", address, type, subtype));
    const auto created = pass.particles->CreateChecked(type, object.transform.position, object.transform.angles,
                                                       glm::vec3(1.0f), subtype, scale, pass.worldClock, *pass.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    return {};
}

// 0x42cb10..0x42cb52: with rand() % 3 at 0, 0x53fe70(0x6ec, …, 0, scale, 0).
Core::Expected<void, std::string> Spark(const Kernel::GameObjectPass& pass, const Kernel::WorldObject& object)
{
    if (pass.random->Next() % EmitChance != 0)
        return {};
    return Emit(pass, object, Particles::CrywolfFanta, 0, object.transform.scale, "0x42cb52");
}

// 0x42ca37..0x42ca53: the clock times 0.002f (0x6b89bc), FSIN, times 0.3f (0x6b89dc) and plus 0.7f (0x6b8a5c) in the
// x87 up to the fstp of +0x68.
Core::Expected<void, std::string> Pulse(Kernel::WorldObject& object, float worldClock)
{
    constexpr double Rate = static_cast<double>(std::bit_cast<float>(0x3b03126fu));
    constexpr float Swing = std::bit_cast<float>(0x3e99999au);
    constexpr double Base = static_cast<double>(std::bit_cast<float>(0x3f333333u));
    const auto pulse = Math::Detail::SineProductPC53(static_cast<double>(worldClock) * Rate, Swing);
    if (!pulse)
        return Core::Unexpected(std::string("0x42ca45: la luz del 0x29 del 30 no cabe en el perfil PC53"));
    object.state.light = static_cast<float>(*pulse + Base);
    return {};
}
} // namespace

Core::Expected<void, std::string> StepCastleSiegeObjectFrame(Kernel::WorldObject& object,
                                                             const Kernel::GameObjectPass& pass)
{
    if (pass.random == nullptr)
        return Core::Unexpected(std::string("0x42c9f0: falta el rand() de la CRT"));
    switch (object.type)
    {
    case 0:
        // 0x42ca25: with the siege, nothing.
        if (pass.siegeActive)
            return {};
        return Spark(pass, object);
    case 0x29:
        return Pulse(object, pass.worldClock);
    case 0x2a:
        // 0x42ca5b..0x42caaa: only with the siege, Crywolf's smoke 0x5f2/0x15.
        if (!pass.siegeActive || pass.random->Next() % EmitChance != 0)
            return {};
        return Emit(pass, object, Particles::BreathParticle, Particles::CrywolfSmokeSubtype, object.transform.scale,
                    "0x42caaa");
    case 0x34:
        // 0x42cab9..0x42caf4: without the siege, without rand().
        if (pass.siegeActive)
            return {};
        return Emit(pass, object, Particles::KalimaWaterfallLast, Particles::BrazierFallSubtype, object.transform.scale,
                    "0x42caf4");
    case 0x35:
        if (!pass.siegeActive)
            return {};
        return Spark(pass, object);
    case 0x36:
    {
        // 0x42cb61..0x42cbab: the rand() after building the arguments; & 0x80000001 with the sign fix is the remainder
        // by 2.
        if (pass.siegeActive)
            return {};
        constexpr int Splashes = 2;
        const std::int32_t type = Particles::WaterfallSplashFirst + (pass.random->Next() % Splashes);
        constexpr float Scale = 1.0f;
        return Emit(pass, object, type, 0, Scale, "0x42cbab");
    }
    default:
        return {};
    }
}
} // namespace Client::MapScripts::CastleSiege
