#include "Scenes/CastleSiegeObjectServices.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/KalimaParticleKernels.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr int EmitChance = 3;

// 0x53fe70(tipo, +0x10, +0x1c, (1, 1, 1), subtipo, escala, 0).
Core::Expected<void, std::string> Emit(const GameObjectPass& pass, const World::WorldObject& object, std::int32_t type,
                                       std::int32_t subtype, float scale, std::string_view address)
{
    if (pass.particles == nullptr)
        return Core::Unexpected(
            std::format("{}: falta el pool de partículas 0x743a198 para {:#x}/{}", address, type, subtype));
    const auto created =
        pass.particles->CreateOwnedChecked(type, object.transform.position, object.transform.angles, glm::vec3(1.0f),
                                           subtype, scale, pass.worldClock, *pass.random, pass.scene, {});
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    return {};
}

// 0x42cb10..0x42cb52: con rand() % 3 en 0, 0x53fe70(0x6ec, …, 0, escala, 0).
Core::Expected<void, std::string> Spark(const GameObjectPass& pass)
{
    if (pass.random->Next() % EmitChance != 0)
        return {};
    return Core::Unexpected(std::string("0x42cb52: la chispa 6ec/0 de los objetos del 30 no está portada"));
}

// 0x42ca37..0x42ca53: el reloj por 0,002f (0x6b89bc), FSIN, por 0,3f (0x6b89dc) y más 0,7f (0x6b8a5c) en la
// x87 hasta el fstp de +0x68.
Core::Expected<void, std::string> Pulse(World::WorldObject& object, float worldClock)
{
    constexpr double Rate = static_cast<double>(std::bit_cast<float>(0x3b03126fu));
    constexpr float Swing = std::bit_cast<float>(0x3e99999au);
    constexpr double Base = static_cast<double>(std::bit_cast<float>(0x3f333333u));
    const auto pulse = Render::Models::Detail::SineProductPC53(static_cast<double>(worldClock) * Rate, Swing);
    if (!pulse)
        return Core::Unexpected(std::string("0x42ca45: la luz del 0x29 del 30 no cabe en el perfil PC53"));
    object.state.light = static_cast<float>(*pulse + Base);
    return {};
}
} // namespace

Core::Expected<void, std::string> StepCastleSiegeObjectFrame(World::WorldObject& object, const GameObjectPass& pass)
{
    if (pass.random == nullptr)
        return Core::Unexpected(std::string("0x42c9f0: falta el rand() de la CRT"));
    switch (object.type)
    {
    case 0:
        // 0x42ca25: con el asedio, nada.
        if (pass.siegeActive)
            return {};
        return Spark(pass);
    case 0x29:
        return Pulse(object, pass.worldClock);
    case 0x2a:
        // 0x42ca5b..0x42caaa: solo con el asedio.
        if (!pass.siegeActive || pass.random->Next() % EmitChance != 0)
            return {};
        return Core::Unexpected(std::string("0x42caaa: el humo 5f2/0x15 del 0x2a del 30 no está portado"));
    case 0x34:
        // 0x42cab9..0x42caf4: sin el asedio, sin rand().
        if (pass.siegeActive)
            return {};
        return Core::Unexpected(std::string("0x42caf4: el 6f4/6 del 0x34 del 30 no está portado"));
    case 0x35:
        if (!pass.siegeActive)
            return {};
        return Spark(pass);
    case 0x36:
    {
        // 0x42cb61..0x42cbab: el rand() después de armar los argumentos; & 0x80000001 con el arreglo del signo es
        // el resto por 2.
        if (pass.siegeActive)
            return {};
        constexpr int Splashes = 2;
        const std::int32_t type = World::WaterfallSplashFirst + (pass.random->Next() % Splashes);
        constexpr float Scale = 1.0f;
        return Emit(pass, object, type, 0, Scale, "0x42cbab");
    }
    default:
        return {};
    }
}
} // namespace Client::Scenes
