#include "MapScripts/Crywolf/CrywolfObjectServices.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Protocol/Events/Events.h"
#include "Math/OriginalRetainedMath.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Particles/KalimaParticleKernels.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "WorldObjects/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::MapScripts::Crywolf
{
namespace
{
constexpr int EmitChance = 3;
// 0x430537: the count passes 70000 (0x11170) and rolls the weather.
constexpr std::int32_t WeatherPeriod = 70000;
constexpr int WeatherCount = 3;
constexpr std::int16_t GlowObject = 0x29;
constexpr std::int16_t ChimneyObject = 0x54;
constexpr std::uint16_t GlowTexture = 0x60f;
constexpr float GlowScale = std::bit_cast<float>(0x3fa66666u); // 0x430646: 1.3.
constexpr double GlowStep = 1.0;                               // 0x6b87d4.
constexpr double FullTurn = Math::FullTurnAsDouble;            // 0x6b869c.
constexpr double GlowHeight = 350.0;                           // 0x6b89ac.
constexpr std::int32_t CounterMiddle = 20, CounterTop = 40;
constexpr glm::vec3 White(1.0f);

using Math::F32;

Core::Expected<void, std::string> EmitAt(const WorldObjects::GameObjectPass& pass,
                                         const WorldObjects::WorldObject& object, const glm::vec3& position,
                                         std::int32_t type, std::int32_t subtype, const glm::vec3& light,
                                         std::string_view address)
{
    if (pass.particles == nullptr)
    {
        return Core::Unexpected(
            std::format("{}: falta el pool de partículas 0x743a198 para {:#x}/{}", address, type, subtype));
    }
    // 0x53fe70(type, position, angles, light, subtype, object's scale, owner): only the 0x622/9 receives the object,
    // and its step does not read it (0x5476a8).
    const auto created = pass.particles->CreateChecked(type, position, object.transform.angles, light, subtype,
                                                       object.transform.scale, pass.worldClock, *pass.random);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    }
    return {};
}

Core::Expected<void, std::string> Emit(const WorldObjects::GameObjectPass& pass,
                                       const WorldObjects::WorldObject& object, std::int32_t type, std::int32_t subtype,
                                       const glm::vec3& light, std::string_view address)
{
    return EmitAt(pass, object, object.transform.position, type, subtype, light, address);
}

// 0x4308ca..0x430932: the smoke 0x5f2/33 50 (0x6b86b0) higher, with the light of another rand(): L = ((rand() & 3) +
// 3) · 0.1f in the x87, fst of the red, and the green and the blue times 0.6f (0x6b8a20) and 0.2f (0x6b8854) from the
// unrounded L.
Core::Expected<void, std::string> RisingSmoke(const WorldObjects::WorldObject& object,
                                              const WorldObjects::GameObjectPass& pass)
{
    constexpr double Lift = 50.0;
    constexpr int LevelMask = 3, LevelBase = 3;
    constexpr double Tenth = Math::TenthAsDouble;
    constexpr double Green = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    const glm::vec3& position = object.transform.position;
    const glm::vec3 above(position.x, position.y, F32(static_cast<double>(position.z) + Lift));
    const double level = static_cast<double>((pass.random->Next() & LevelMask) + LevelBase) * Tenth;
    const glm::vec3 light(F32(level), F32(level * Green), F32(level * Blue));
    return EmitAt(pass, object, above, Particles::BreathParticle, Particles::CrywolfRisingSmokeSubtype, light,
                  "0x430932");
}

// 0x4305db..0x43068c.
Core::Expected<void, std::string> Glow(const WorldObjects::WorldObject& object,
                                       const WorldObjects::GameObjectPass& pass, Kernel::CrywolfGlobals& crywolf)
{
    // fst stores the turn in float and fcomp compares the sum left in the x87 with 360; NaN does not go back to 0.
    const double turned = static_cast<double>(crywolf.glowRotation) + GlowStep;
    crywolf.glowRotation = F32(turned);
    if (turned >= FullTurn)
        crywolf.glowRotation = 0.0f;
    if (pass.objectDrawCounter == nullptr)
        return Core::Unexpected(std::string("0x430613: falta la cuenta de 0x6046a0 (0x5763538)"));
    std::int32_t lift = *pass.objectDrawCounter;
    if (lift > CounterMiddle)
        lift = CounterTop - lift;
    // fild, plus 350 and plus the object's z (fadd st, st(1)), in the x87 up to the fstp.
    const glm::vec3 point(
        object.transform.position.x, object.transform.position.y,
        F32((static_cast<double>(lift) + GlowHeight) + static_cast<double>(object.transform.position.z)));
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::string("0x43066b: falta el pool de sprites (0x5491e0) para el 0x29 del 34"));
    // 0x5491e0(0x60f, point, 1.3, light, object, turn, 0): MuRet does not store the sprite's owner (Q-C303).
    static_cast<void>(pass.sprites->Create(GlowTexture, point, GlowScale, White, 0.0f, 0));
    static_cast<void>(pass.sprites->Create(GlowTexture, point, GlowScale, White, crywolf.glowRotation, 0));
    return {};
}
// 0x430a40..0x430ac1: PC53 up to each FSTP of the RGB, including the products of the two channels without a sine.
Core::Expected<glm::vec3, std::string> AltarLight(float worldClock, std::int32_t light)
{
    constexpr double Rate = static_cast<double>(std::bit_cast<float>(0x3b83126fu));      // 6b89e8.
    constexpr double Quiet = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));     // 6b87b8.
    constexpr double RedStep = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));   // 6b8ad0.
    constexpr double GreenStep = static_cast<double>(std::bit_cast<float>(0x3d23d70au)); // 6b8acc.
    const double factor = light > 10 ? Quiet : 1.0;
    // FSIN retains 64 bits; 0x430a72 multiplies by 3 before the first PC53 rounding.
    const auto sine = Math::Detail::SineProductPC53(static_cast<double>(worldClock) * Rate, 3.0f);
    if (!sine)
        return Core::Unexpected(std::string("0x430a70: el seno del altar Crywolf no cabe en el perfil PC53"));
    const double red = *sine * factor + 5.0;
    return glm::vec3(F32(red - static_cast<double>(light) * RedStep), F32(static_cast<double>(light) * GreenStep - 1.0),
                     F32(static_cast<double>(light) * RedStep - 0.5));
}
} // namespace

Core::Expected<void, std::string> StepCrywolfObjectFrame(const WorldObjects::WorldObject& object,
                                                         const WorldObjects::GameObjectPass& pass)
{
    if (pass.random == nullptr)
        return Core::Unexpected(std::string("0x430510: falta el rand() de la CRT"));
    if (pass.crywolf == nullptr)
        return Core::Unexpected(std::string("0x430523: faltan los globales de Crywolf (0x723c0c y 0x5763358)"));
    Kernel::CrywolfGlobals& crywolf = *pass.crywolf;
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
        if (pass.random->Next() % EmitChance != 0 || crywolf.state != Protocol::Events::CrywolfOccupiedState)
            return {};
        return Emit(pass, object, Particles::BreathParticle, Particles::CrywolfSmokeSubtype, White, "0x4305cc");
    }
    return {};
}

Core::Expected<void, std::string> CrywolfObjectBody(const WorldObjects::WorldObject& object,
                                                    WorldObjects::ObjectDraw& draw,
                                                    const WorldObjects::GameObjectPass& pass)
{
    if (pass.crywolf == nullptr)
        return Core::Unexpected(std::string("0x4306a0: faltan los globales de Crywolf (0x723c04 y 0x5763358)"));
    const Kernel::CrywolfGlobals& crywolf = *pass.crywolf;
    const bool frame = pass.originalFrame;
    if (frame && pass.random == nullptr)
        return Core::Unexpected(std::string("0x4306a0: falta el rand() de la CRT"));
    switch (object.type)
    {
    case 0x24:
        // 0x4306db: the model color at 0.4 and RenderBody(2, …).
        draw.color = glm::vec3(std::bit_cast<float>(0x3ecccccdu));
        return {};
    case 0x29:
        // 0x4307d5..0x4307e3: the light (0.2; 0.7; 0) goes to a local nobody reads (Q-C343).
        return {};
    case 0x38:
    {
        // 0x43071f: RenderBody(2, …) and then, with the state at 0, the random draws.
        if (!frame || crywolf.state != Protocol::Events::CrywolfPeaceState)
            return {};
        constexpr double LightStep = static_cast<double>(std::bit_cast<float>(0x38d1b717u)); // 0x6b8a94: 0.0001.
        constexpr int LightSpan = 10000;
        const float level = F32(static_cast<double>(pass.random->Next() % LightSpan) * LightStep);
        if (pass.random->Next() % EmitChance != 0)
            return {};
        if (pass.effects == nullptr)
            return Core::Unexpected(std::string("0x4307a3: falta la tabla de efectos (0x518cf0) para el 0x61d/3"));
        // 0x518cf0(0x61d, position, angles, light, 3, object, 4, 0, 0, 0, 0); full, it creates none.
        constexpr std::uint16_t DecalDisturbance = 4;
        static_cast<void>(pass.effects->CreateFadingDecal(object.transform.position, object.transform.angles,
                                                          glm::vec3(level), Effects::CrywolfDecalSubtype,
                                                          DecalDisturbance));
        return {};
    }
    case 0x39:
    case 0x47:
    {
        draw.kind = WorldObjects::ObjectDrawKind::Hidden;
        if (!frame || pass.random->Next() % EmitChance != 0)
            return {};
        const bool smoke = object.type == 0x39;
        if (auto spark = Emit(pass, object, Particles::CrywolfFanta, Particles::ShortFantaSubtype, White,
                              smoke ? "0x430820" : "0x43088b");
            !spark.HasValue())
            return spark;
        if (!smoke)
            return {};
        return Emit(pass, object, Particles::BreathParticle, Particles::CrywolfSmokeSubtype, White, "0x430852");
    }
    case 0x48:
    {
        draw.kind = WorldObjects::ObjectDrawKind::Hidden;
        constexpr int SmokeChance = 10;
        if (!frame || pass.random->Next() % SmokeChance != 0)
            return {};
        return RisingSmoke(object, pass);
    }
    case 0x49:
        draw.kind = WorldObjects::ObjectDrawKind::Hidden;
        // 0x430942: with the weather at 0, the HUD sand at 1, in each of the original's 0x6002e0.
        if (frame && crywolf.weather == 0)
            pass.crywolf->sandOverlay = 1;
        return {};
    case 0x4a:
        draw.kind = WorldObjects::ObjectDrawKind::Hidden;
        if (!frame || pass.random->Next() % EmitChance != 0 || crywolf.state != Protocol::Events::CrywolfOccupiedState)
            return {};
        return Emit(pass, object, Particles::BreathParticle, Particles::CrywolfSmokeSubtype, White, "0x430993");
    case 0x4d:
    case 0x4e:
    {
        draw.kind = WorldObjects::ObjectDrawKind::Hidden;
        constexpr int CloudChance = 6;
        if (!frame || pass.random->Next() % CloudChance != 0)
            return {};
        // 0x4309ce and 0x4309f6: (0.02; 0.03; 0.02) for the 0x4d and (0.03; 0.06; 0.05) for the 0x4e.
        const glm::vec3 light = object.type == 0x4d
                                    ? glm::vec3(std::bit_cast<float>(0x3ca3d70au), std::bit_cast<float>(0x3cf5c28fu),
                                                std::bit_cast<float>(0x3ca3d70au))
                                    : glm::vec3(std::bit_cast<float>(0x3cf5c28fu), std::bit_cast<float>(0x3d75c28fu),
                                                std::bit_cast<float>(0x3d4ccccdu));
        return Emit(pass, object, Particles::CloudParticle, Particles::CrywolfCloudSubtype, light, "0x430a23");
    }
    case 0x51:
        draw.kind = WorldObjects::ObjectDrawKind::Hidden;
        // 0x430a33: the caller runs the two direct RenderMesh, without the generic RenderBody.
        if (crywolf.state == Protocol::Events::CrywolfWarState)
        {
            const auto color = AltarLight(pass.worldClock, crywolf.ui.light);
            if (!color.HasValue())
                return Core::Unexpected(color.Error());
            draw.color = color.Value();
        }
        return {};
    case 0xfa:
        // 0x430b23: the model color at (0; 0; 0.9) and RenderBody(2, …).
        draw.color = glm::vec3(0.0f, 0.0f, std::bit_cast<float>(0x3f666666u));
        return {};
    default:
        break;
    }
    // 0x4339d0: 0x4339dd..0x4339e5 only dispatches 0x171..0x1b4; the others return 0.
    constexpr std::int16_t FirstMonsterCase = 0x171, LastMonsterCase = 0x1b4;
    if (object.type >= FirstMonsterCase && object.type <= LastMonsterCase)
        return Core::Unexpected(
            std::format("0x4339d0: el caso del {:#x} en el 34 no está portado", static_cast<int>(object.type)));
    return {};
}
} // namespace Client::MapScripts::Crywolf
