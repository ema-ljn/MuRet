#include "Scenes/CrywolfObjectServices.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "World/KalimaParticleKernels.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
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
// 0x430537: la cuenta pasa de 70.000 (0x11170) y sortea el clima.
constexpr std::int32_t WeatherPeriod = 70000;
constexpr int WeatherCount = 3;
constexpr std::int16_t GlowObject = 0x29;
constexpr std::int16_t ChimneyObject = 0x54;
constexpr std::uint8_t SmokeState = 1;
constexpr std::uint16_t GlowTexture = 0x60f;
constexpr float GlowScale = std::bit_cast<float>(0x3fa66666u); // 0x430646: 1,3.
constexpr double GlowStep = 1.0;                               // 0x6b87d4.
constexpr double FullTurn = 360.0;                             // 0x6b869c.
constexpr double GlowHeight = 350.0;                           // 0x6b89ac.
constexpr std::int32_t CounterMiddle = 20, CounterTop = 40;
constexpr glm::vec3 White(1.0f);

float F32(double value)
{
    return static_cast<float>(value);
}

Core::Expected<void, std::string> EmitAt(const GameObjectPass& pass, const World::WorldObject& object,
                                         const glm::vec3& position, std::int32_t type, std::int32_t subtype,
                                         const glm::vec3& light, std::string_view address)
{
    if (pass.particles == nullptr)
    {
        return Core::Unexpected(
            std::format("{}: falta el pool de partículas 0x743a198 para {:#x}/{}", address, type, subtype));
    }
    // 0x53fe70(tipo, posición, ángulos, luz, subtipo, escala del objeto, dueño): solo la 622/9 recibe el objeto,
    // y su paso no lo lee (0x5476a8).
    const auto created = pass.particles->CreateChecked(type, position, object.transform.angles, light, subtype,
                                                       object.transform.scale, pass.worldClock, *pass.random);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    }
    return {};
}

Core::Expected<void, std::string> Emit(const GameObjectPass& pass, const World::WorldObject& object, std::int32_t type,
                                       std::int32_t subtype, const glm::vec3& light, std::string_view address)
{
    return EmitAt(pass, object, object.transform.position, type, subtype, light, address);
}

// 0x4308ca..0x430932: el humo 5f2/33 50 (0x6b86b0) más arriba, con la luz de otro rand(): L = ((rand() & 3) + 3) ·
// 0,1f en la x87, fst del rojo, y el verde y el azul por 0,6f (0x6b8a20) y 0,2f (0x6b8854) desde L sin redondear.
Core::Expected<void, std::string> RisingSmoke(const World::WorldObject& object, const GameObjectPass& pass)
{
    constexpr double Lift = 50.0;
    constexpr int LevelMask = 3, LevelBase = 3;
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    constexpr double Green = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    const glm::vec3& position = object.transform.position;
    const glm::vec3 above(position.x, position.y, F32(static_cast<double>(position.z) + Lift));
    const double level = static_cast<double>((pass.random->Next() & LevelMask) + LevelBase) * Tenth;
    const glm::vec3 light(F32(level), F32(level * Green), F32(level * Blue));
    return EmitAt(pass, object, above, World::BreathParticle, World::CrywolfRisingSmokeSubtype, light, "0x430932");
}

// 0x4305db..0x43068c.
Core::Expected<void, std::string> Glow(const World::WorldObject& object, const GameObjectPass& pass,
                                       CrywolfGlobals& crywolf)
{
    // fst guarda el giro en float y fcomp compara con 360 la suma que quedó en la x87; NaN no vuelve a 0.
    const double turned = static_cast<double>(crywolf.glowRotation) + GlowStep;
    crywolf.glowRotation = F32(turned);
    if (turned >= FullTurn)
        crywolf.glowRotation = 0.0f;
    if (pass.objectDrawCounter == nullptr)
        return Core::Unexpected(std::string("0x430613: falta la cuenta de 0x6046a0 (0x5763538)"));
    std::int32_t lift = *pass.objectDrawCounter;
    if (lift > CounterMiddle)
        lift = CounterTop - lift;
    // fild, más 350 y más la z del objeto (fadd st, st(1)), en la x87 hasta el fstp.
    const glm::vec3 point(
        object.transform.position.x, object.transform.position.y,
        F32((static_cast<double>(lift) + GlowHeight) + static_cast<double>(object.transform.position.z)));
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::string("0x43066b: falta el pool de sprites (0x5491e0) para el 0x29 del 34"));
    // 0x5491e0(0x60f, punto, 1,3, luz, objeto, giro, 0): MuRet no guarda el dueño del sprite (Q-C303).
    static_cast<void>(pass.sprites->Create(GlowTexture, point, GlowScale, White, 0.0f, 0));
    static_cast<void>(pass.sprites->Create(GlowTexture, point, GlowScale, White, crywolf.glowRotation, 0));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepCrywolfObjectFrame(const World::WorldObject& object, const GameObjectPass& pass)
{
    if (pass.random == nullptr)
        return Core::Unexpected(std::string("0x430510: falta el rand() de la CRT"));
    if (pass.crywolf == nullptr)
        return Core::Unexpected(std::string("0x430523: faltan los globales de Crywolf (0x723c0c y 0x5763358)"));
    CrywolfGlobals& crywolf = *pass.crywolf;
    ++crywolf.frameCount;
    if (crywolf.frameCount > WeatherPeriod)
    {
        const int drawn = pass.random->Next();
        crywolf.frameCount = 0;
        crywolf.weather = drawn % WeatherCount;
    }
    if (object.type == GlowObject)
        return Glow(object, pass, crywolf);
    if (object.type == ChimneyObject)
    {
        if (pass.random->Next() % EmitChance != 0 || crywolf.state != SmokeState)
            return {};
        return Emit(pass, object, World::BreathParticle, World::CrywolfSmokeSubtype, White, "0x4305cc");
    }
    return {};
}

Core::Expected<void, std::string> CrywolfObjectBody(const World::WorldObject& object, World::ObjectDraw& draw,
                                                    const GameObjectPass& pass)
{
    if (pass.crywolf == nullptr)
        return Core::Unexpected(std::string("0x4306a0: faltan los globales de Crywolf (0x723c04 y 0x5763358)"));
    const CrywolfGlobals& crywolf = *pass.crywolf;
    const bool frame = pass.originalFrame;
    if (frame && pass.random == nullptr)
        return Core::Unexpected(std::string("0x4306a0: falta el rand() de la CRT"));
    switch (object.type)
    {
    case 0x24:
        // 0x4306db: el color del modelo en 0,4 y RenderBody(2, …).
        draw.color = glm::vec3(std::bit_cast<float>(0x3ecccccdu));
        return {};
    case 0x29:
        // 0x4307d5..0x4307e3: la luz (0,2; 0,7; 0) va a un local que nadie lee (Q-C343).
        return {};
    case 0x38:
    {
        // 0x43071f: RenderBody(2, …) y después, con el estado en 0, los sorteos.
        if (!frame || crywolf.state != 0)
            return {};
        constexpr double LightStep = static_cast<double>(std::bit_cast<float>(0x38d1b717u)); // 0x6b8a94: 0,0001.
        constexpr int LightSpan = 10000;
        const float level = F32(static_cast<double>(pass.random->Next() % LightSpan) * LightStep);
        if (pass.random->Next() % EmitChance != 0)
            return {};
        if (pass.effects == nullptr)
            return Core::Unexpected(std::string("0x4307a3: falta la tabla de efectos (0x518cf0) para el 0x61d/3"));
        // 0x518cf0(0x61d, posición, ángulos, luz, 3, objeto, 4, 0, 0, 0, 0); llena, no crea ninguno.
        constexpr std::uint16_t DecalDisturbance = 4;
        static_cast<void>(pass.effects->CreateFadingDecal(object.transform.position, object.transform.angles,
                                                          glm::vec3(level), World::CrywolfDecalSubtype,
                                                          DecalDisturbance));
        return {};
    }
    case 0x39:
    case 0x47:
    {
        draw.kind = World::ObjectDrawKind::Hidden;
        if (!frame || pass.random->Next() % EmitChance != 0)
            return {};
        const bool smoke = object.type == 0x39;
        if (auto spark = Emit(pass, object, World::CrywolfFanta, World::ShortFantaSubtype, White,
                              smoke ? "0x430820" : "0x43088b");
            !spark.HasValue())
            return spark;
        if (!smoke)
            return {};
        return Emit(pass, object, World::BreathParticle, World::CrywolfSmokeSubtype, White, "0x430852");
    }
    case 0x48:
    {
        draw.kind = World::ObjectDrawKind::Hidden;
        constexpr int SmokeChance = 10;
        if (!frame || pass.random->Next() % SmokeChance != 0)
            return {};
        return RisingSmoke(object, pass);
    }
    case 0x49:
        draw.kind = World::ObjectDrawKind::Hidden;
        // 0x430942: con el clima en 0, la arena del HUD en 1, en cada 0x6002e0 del original.
        if (frame && crywolf.weather == 0)
            pass.crywolf->sandOverlay = 1;
        return {};
    case 0x4a:
        draw.kind = World::ObjectDrawKind::Hidden;
        if (!frame || pass.random->Next() % EmitChance != 0 || crywolf.state != SmokeState)
            return {};
        return Emit(pass, object, World::BreathParticle, World::CrywolfSmokeSubtype, White, "0x430993");
    case 0x4d:
    case 0x4e:
    {
        draw.kind = World::ObjectDrawKind::Hidden;
        constexpr int CloudChance = 6;
        if (!frame || pass.random->Next() % CloudChance != 0)
            return {};
        // 0x4309ce y 0x4309f6: (0,02; 0,03; 0,02) para el 0x4d y (0,03; 0,06; 0,05) para el 0x4e.
        const glm::vec3 light = object.type == 0x4d
                                    ? glm::vec3(std::bit_cast<float>(0x3ca3d70au), std::bit_cast<float>(0x3cf5c28fu),
                                                std::bit_cast<float>(0x3ca3d70au))
                                    : glm::vec3(std::bit_cast<float>(0x3cf5c28fu), std::bit_cast<float>(0x3d75c28fu),
                                                std::bit_cast<float>(0x3d4ccccdu));
        return Emit(pass, object, World::CloudParticle, World::CrywolfCloudSubtype, light, "0x430a23");
    }
    case 0x51:
        draw.kind = World::ObjectDrawKind::Hidden;
        // 0x430a33: con el estado en 2, dos 0x4ecc30 de la malla 0 con la luz de 0x723c08 (CL9.5).
        if (crywolf.state == 2)
            return Core::Unexpected(std::string("0x430a40: el 0x51 del 34 con el estado de Crywolf 2 no está "
                                                "portado"));
        return {};
    case 0xfa:
        // 0x430b23: el color del modelo en (0; 0; 0,9) y RenderBody(2, …).
        draw.color = glm::vec3(0.0f, 0.0f, std::bit_cast<float>(0x3f666666u));
        return {};
    default:
        break;
    }
    // 0x4339d0: 0x4339dd..0x4339e5 solo despacha 0x171..0x1b4; los demás devuelven 0.
    constexpr std::int16_t FirstMonsterCase = 0x171, LastMonsterCase = 0x1b4;
    if (object.type >= FirstMonsterCase && object.type <= LastMonsterCase)
        return Core::Unexpected(
            std::format("0x4339d0: el caso del {:#x} en el 34 no está portado", static_cast<int>(object.type)));
    return {};
}
} // namespace Client::Scenes
