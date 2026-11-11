#include "Scenes/EventMapObjectFrame.h"

#include "Core/Expected.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/ObjectFrameSprites.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/BloodCastleParticleKernels.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

// 0x6b89c8 (0,001f), 0x6b86d0 (20), 0x6b89dc (0,3f), 0x6b8a5c (0,7f), 0x6b882c (0,5), 0x6b8818 (2) y 0x6b87d4 (1).
constexpr double Thousandth = static_cast<double>(std::bit_cast<float>(0x3a83126fu));
constexpr double AngleRate = 20.0;
constexpr float PulseSwing = std::bit_cast<float>(0x3e99999au);
constexpr double PulseBase = static_cast<double>(std::bit_cast<float>(0x3f333333u));
constexpr double Half = 0.5;
constexpr double TorchLead = 2.0;
constexpr double TorchStep = 1.0;

constexpr std::int32_t RainParticle = 0x68;
constexpr std::int32_t GlowSpriteTexture = 0x5ac;
constexpr std::int32_t OrbTexture = 0x60f;
constexpr std::int32_t TorchOrbSubtype = 4;
// 0x6042a5..0x604285: la luz y la escala de la 60f/4.
constexpr glm::vec3 TorchOrbLight{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f4ccccdu)};
constexpr float TorchOrbScale = std::bit_cast<float>(0x3e428f5cu);

constexpr std::int16_t DevilSquareRainObject = 2;
constexpr std::int16_t GlowingStatue = 0xb;
constexpr std::int16_t PulsingOrb = 0xd;
constexpr std::int16_t Torch = 0x25;
// 0x603df9 y 0x60433a..0x604361: los huesos y los desplazamientos.
constexpr std::size_t FirstRainBone = 23;
constexpr std::size_t SecondRainBone = 31;
constexpr glm::vec3 RainOffset{-15.0f, 0.0f, 0.0f};
constexpr std::array<std::size_t, 7> StatueBones{1, 2, 4, 6, 9, 10, 11};
constexpr glm::vec3 StatueOffset{0.0f, 0.0f, 2.0f};
constexpr float StatueSprite = 0.5f;
constexpr std::size_t OrbBone = 3;

Core::Expected<void, std::string> Emit(const GameObjectPass& pass, std::int32_t type, const glm::vec3& position,
                                       const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                                       float scale, std::string_view address)
{
    if (pass.particles == nullptr || pass.random == nullptr)
        return Core::Unexpected(
            std::format("{}: falta el pool de partículas 0x743a198 para {:#x}/{}", address, type, subtype));
    const auto created =
        pass.particles->CreateChecked(type, position, angles, light, subtype, scale, pass.worldClock, *pass.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    return {};
}

// 0x6042a5..0x6042ac: 0x53fe70(0x60f, posición, ángulos, (1; 0,8f; 0,8f), 4, 0,19f, 0), con el reloj del mundo y
// la trigonometría de la órbita prestados.
Core::Expected<void, std::string> Orbit(const GameObjectPass& pass, const glm::vec3& position, const glm::vec3& angles)
{
    if (pass.particles == nullptr)
        return Core::Unexpected(std::string("0x6042ac: falta el pool de partículas 0x743a198 para 0x60f/4"));
    World::PortableParticleTrigonometry trigonometry;
    const auto created =
        pass.particles->CreateChecked(World::OrbitParticle, position, angles, TorchOrbLight, TorchOrbSubtype,
                                      TorchOrbScale, std::cref(pass.worldClock), *pass.random, 0, trigonometry);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("0x6042ac: el alta de 0x60f/4 falla con error {}", static_cast<unsigned>(created.Error())));
    return {};
}

// 0x5491e0(textura, punto, escala, luz, objeto, 0, 0): MuRet no guarda el dueño del sprite (Q-C303).
Core::Expected<void, std::string> Sprite(const GameObjectPass& pass, std::int32_t texture, const glm::vec3& position,
                                         float scale, const glm::vec3& light, std::string_view address)
{
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de sprites (0x5491e0)", address));
    static_cast<void>(pass.sprites->Create(static_cast<std::uint16_t>(texture), position, scale, light, 0.0f, 0));
    return {};
}

Core::Expected<void, std::string> RainAt(const World::WorldObject& object, const GameObjectPass& pass, std::size_t bone,
                                         std::string_view address)
{
    const auto point = ObjectBonePoint(pass, bone, RainOffset);
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    return Emit(pass, RainParticle, point.Value(), object.transform.angles, glm::vec3(1.0f), 0, 1.0f, address);
}

// 0x60433a..0x6043e9.
Core::Expected<void, std::string> StatueGlows(const World::WorldObject& object, const GameObjectPass& pass)
{
    const double phase =
        (static_cast<double>(object.transform.angles.z) * AngleRate) + static_cast<double>(pass.worldClock);
    const std::optional<double> wave = Render::Models::Detail::SineSumPC53(phase * Thousandth, 1.0);
    if (!wave)
        return Core::Unexpected(std::string("0x604374: el seno del reloj no cabe en el perfil PC53"));
    const double level = *wave * Half;
    const glm::vec3 light{F32(level), F32(level * Half), 0.0f};
    for (const std::size_t bone : StatueBones)
    {
        const auto point = ObjectBonePoint(pass, bone, StatueOffset);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        if (auto created = Sprite(pass, GlowSpriteTexture, point.Value(), StatueSprite, light, "0x6043dd");
            !created.HasValue())
            return created;
    }
    return {};
}

// 0x6042bb..0x60432b.
Core::Expected<void, std::string> PulsingOrbSprite(const GameObjectPass& pass)
{
    const std::optional<double> wave =
        Render::Models::Detail::SineProductPC53(static_cast<double>(pass.worldClock) * Thousandth, PulseSwing);
    if (!wave)
        return Core::Unexpected(std::string("0x6042d0: el seno del reloj no cabe en el perfil PC53"));
    const float level = F32(*wave + PulseBase);
    const auto point = ObjectBonePoint(pass, OrbBone, glm::vec3(0.0f));
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    return Sprite(pass, OrbTexture, point.Value(), F32(static_cast<double>(level) + Half), glm::vec3(level),
                  "0x60432b");
}

// 0x604199..0x6041c2 y 0x60421c..0x604245: ftol de la fase (más `lead`) % 4 y la fase más 1, guardada.
Core::Expected<bool, std::string> TorchTurn(World::WorldObject& object, double lead, std::string_view address)
{
    const std::optional<std::int32_t> turn = World::OriginalFtolLow(static_cast<double>(object.field84) + lead);
    if (!turn)
        return Core::Unexpected(std::format("{}: la fase del 0x25 no tiene un ftol acreditado", address));
    constexpr std::int32_t TorchPeriod = 4;
    object.field84 = F32(static_cast<double>(object.field84) + TorchStep);
    return *turn % TorchPeriod == 0;
}

// 0x604171..0x6042ac.
Core::Expected<void, std::string> TorchFrame(World::WorldObject& object, const GameObjectPass& pass)
{
    const glm::vec3& position = object.transform.position;
    const glm::vec3& angles = object.transform.angles;
    if (pass.random->Next() % 2 == 0)
    {
        const auto turn = TorchTurn(object, TorchLead, "0x6041a5");
        if (!turn.HasValue())
            return Core::Unexpected(turn.Error());
        if (turn.Value())
        {
            if (auto flame =
                    Emit(pass, World::TorchFlameParticle, position, angles, glm::vec3(1.0f), 0, 1.0f, "0x6041e4");
                !flame.HasValue())
                return flame;
            if (auto spark =
                    Emit(pass, World::TorchSparkParticle, position, angles, glm::vec3(1.0f), 0, 1.0f, "0x6041fd");
                !spark.HasValue())
                return spark;
        }
    }
    if (pass.random->Next() % 2 != 0)
        return {};
    const auto turn = TorchTurn(object, 0.0, "0x604222");
    if (!turn.HasValue())
        return Core::Unexpected(turn.Error());
    if (!turn.Value())
        return {};
    if (auto smoke = Emit(pass, World::CloudParticle, position, angles, glm::vec3(1.0f), World::TorchSmokeSubtype, 1.0f,
                          "0x604265");
        !smoke.HasValue())
        return smoke;
    if (auto spark = Emit(pass, World::TorchSparkParticle, position, angles, glm::vec3(1.0f), 1, 1.0f, "0x60427e");
        !spark.HasValue())
        return spark;
    return Orbit(pass, position, angles);
}
} // namespace

Core::Expected<void, std::string> StepDevilSquareObjectFrame(const World::WorldObject& object,
                                                             const GameObjectPass& pass)
{
    if (object.type != DevilSquareRainObject)
        return {};
    if (pass.random == nullptr)
        return Core::Unexpected(std::string("0x603e0e: el 2 del mapa 9 saca rand(), y el pase no lo tiene"));
    if (pass.random->Next() % 4 == 0)
    {
        if (auto rain = RainAt(object, pass, FirstRainBone, "0x603e4e"); !rain.HasValue())
            return rain;
    }
    if (pass.random->Next() % 4 == 0)
    {
        if (auto rain = RainAt(object, pass, SecondRainBone, "0x603e96"); !rain.HasValue())
            return rain;
    }
    return RainAt(object, pass, FirstRainBone, "0x603ee0");
}

Core::Expected<void, std::string> StepBloodCastleObjectFrame(World::WorldObject& object, const GameObjectPass& pass)
{
    if (object.type == GlowingStatue)
        return StatueGlows(object, pass);
    if (object.type == PulsingOrb)
        return PulsingOrbSprite(pass);
    if (object.type != Torch)
        return {};
    if (pass.random == nullptr)
        return Core::Unexpected(std::string("0x604186: el 0x25 de los mapas 11 a 17 saca rand(), y el pase no lo "
                                            "tiene"));
    return TorchFrame(object, pass);
}
} // namespace Client::Scenes
