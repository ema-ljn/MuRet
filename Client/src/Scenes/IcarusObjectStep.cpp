#include "Scenes/IcarusObjectStep.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Sprites.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// 0x6b87ac (150), 0x6b8a48 (0,05f), 0x6b89dc (0,3f), 0x6ba748 (0,081f), 0x6b8900 (300), 0x6b8bf4 (40),
// 0x6b88c4 (1.000), 0x6b89e4 (0,02f) y 0x6b87a8 (10).
constexpr double LightSpreadCenter = 150.0;
constexpr double LightStep = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr double LightRedGreen = static_cast<double>(std::bit_cast<float>(0x3e99999au));
constexpr double LightBlue = static_cast<double>(std::bit_cast<float>(0x3da5e354u));
constexpr double JointDrop = 300.0;
constexpr double JointWidthBase = 40.0;
constexpr double FlareDrop = 1000.0;
constexpr double SpriteLightStep = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
constexpr double SelectionWidthBase = 10.0;

constexpr std::int32_t PreludeGate = 50;
constexpr std::int32_t LightSpread = 300;
constexpr std::int32_t LightBase = 4;
constexpr std::int32_t LightRadius = 2;
constexpr std::int32_t JointGate = 5;
constexpr std::int32_t JointWidthSpread = 10;
constexpr std::int32_t FlareGate = 10;
constexpr std::int32_t FlareSpread = 5000;
constexpr std::int32_t FlareSpreadCenter = 2500;
constexpr std::int32_t SelectionGate = 10;
constexpr std::int32_t SpriteLightSpread = 10;
constexpr std::int32_t SelectionWidthSpread = 20;
constexpr std::int16_t LastSelectableType = 5;

constexpr std::int32_t SkyJoint = 0x615;
constexpr std::int32_t ObjectJoint = 0x614;
constexpr std::int32_t ObjectJointSubtype = 6;
constexpr std::uint16_t SelectionSprite = 0x623;
constexpr float SelectionSpriteScale = 0.5f;

// 0x605ebb..0x6060b0: por lado, el corrimiento que gira con −45° antes de sumarse (o restarse, el 1) al
// héroe, el giro del segundo y el corrimiento que gira con él.
struct Side
{
    glm::vec3 first;
    bool subtract;
    float yaw;
    glm::vec3 second;
};
constexpr std::array<Side, 4> Sides{{
    {{-400.0f, -1000.0f, 0.0f}, false, 240.0f, {-200.0f, -1000.0f, 0.0f}},
    {{-300.0f, -400.0f, 0.0f}, true, 210.0f, {-500.0f, -1000.0f, 0.0f}},
    {{-200.0f, -400.0f, 0.0f}, false, 235.0f, {-1000.0f, -1500.0f, 0.0f}},
    {{-200.0f, 400.0f, 0.0f}, false, 200.0f, {-600.0f, -1200.0f, 0.0f}},
}};
constexpr float SideYaw = -45.0f;

// rand() & 3 con la corrección del signo de la división de C, que con rand() no corre.
std::int32_t Quarter(std::int32_t value)
{
    constexpr std::int32_t Mask = 3;
    return value % (Mask + 1);
}

Core::Expected<void, std::string> Join(const IcarusObjectServices& services, std::int32_t type, glm::vec3 first,
                                       glm::vec3 second, glm::vec3 angles, std::int32_t subtype, void* owner,
                                       float width)
{
    if (!services.createJoint)
        return Core::Unexpected(std::string("0x530860: falta la tabla de uniones de Icarus"));
    constexpr std::int32_t NoKey = -1;
    constexpr std::int32_t NoTarget = -1;
    return services.createJoint(
        World::FishEffectRequest{type, first, second, angles, subtype, owner, width, NoKey, 0, 0, NoTarget});
}

// 0x605d70.
Core::Expected<std::int32_t, std::string> Selector(std::int32_t count, const IcarusObjectServices& services)
{
    Render::Models::CrtRandom& random = services.random;
    if (random.Next() % PreludeGate != 0)
        return std::int32_t{0};
    // 0x605d91..0x605db8: la x con la x87 (entero + x del héroe − 150); la y del héroe tal cual.
    const std::int32_t spread = random.Next() % LightSpread;
    const glm::vec3& hero = services.heroPosition;
    const float lightX = F32((static_cast<double>(spread) + D(hero.x)) - LightSpreadCenter);
    // 0x605dbb: un rand() que no se usa.
    static_cast<void>(random.Next());
    const double level = static_cast<double>(Quarter(random.Next()) + LightBase) * LightStep;
    const float redGreen = F32(level * LightRedGreen);
    const glm::vec3 light{redGreen, redGreen, F32(level * LightBlue)};
    if (auto lit =
            World::AddDynamicLight(services.lighting, glm::vec3(lightX, hero.y, 0.0f), light, LightRadius, "0x605e0a");
        !lit.HasValue())
        return Core::Unexpected(lit.Error());
    // 0x605e49: 0x518cf0(0xbb, héroe, 0, color, 0, 0, −1, 0, 0, 0, 0), la nube.
    if (services.effects == nullptr)
        return Core::Unexpected(std::string("0x605e49: falta la tabla de efectos para el 0xbb de Icarus"));
    static_cast<void>(
        services.effects->CreateIcarusEffect(World::IcarusCloudEffect, hero, glm::vec3(0.0f), light, random));
    std::int32_t selector = 0;
    if (count != 0)
        selector = random.Next() % count;
    if (random.Next() % JointGate != 0)
        return selector;
    const Render::Models::OriginalMatrix tilt = Render::Models::OriginalAngleMatrix(glm::vec3(0.0f, 0.0f, SideYaw));
    const Side& side = Sides[static_cast<std::size_t>(Quarter(random.Next()))];
    // 0x605edc..0x60605c: 0x5fcbd0(&v, &m, &v), con la entrada y la salida en el mismo vector.
    glm::vec3 offset = side.first;
    Render::Models::OriginalVectorRotateInPlace(offset, tilt);
    // 0x605ee6/0x605f62: en la x87, el corrimiento más (o el héroe menos) y guardado.
    const glm::vec3 center =
        side.subtract
            ? glm::vec3(F32(D(hero.x) - D(offset.x)), F32(D(hero.y) - D(offset.y)), F32(D(hero.z) - D(offset.z)))
            : glm::vec3(F32(D(offset.x) + D(hero.x)), F32(D(offset.y) + D(hero.y)), F32(D(offset.z) + D(hero.z)));
    const Render::Models::OriginalMatrix turn = Render::Models::OriginalAngleMatrix(glm::vec3(0.0f, 0.0f, side.yaw));
    glm::vec3 reach = side.second;
    Render::Models::OriginalVectorRotateInPlace(reach, turn);
    // 0x6060d2..0x606123: la segunda punta (centro − alcance) y la primera (centro + alcance), las dos 300 más
    // abajo, sin giro.
    const glm::vec3 near{F32(D(center.x) - D(reach.x)), F32(D(center.y) - D(reach.y)),
                         F32((D(center.z) - D(reach.z)) - JointDrop)};
    const glm::vec3 far{F32(D(center.x) + D(reach.x)), F32(D(center.y) + D(reach.y)),
                        F32((D(center.z) + D(reach.z)) - JointDrop)};
    for (int joint = 0; joint < 2; ++joint)
    {
        const float width = F32(static_cast<double>(random.Next() % JointWidthSpread) + JointWidthBase);
        if (auto joined = Join(services, SkyJoint, far, near, glm::vec3(0.0f), 0, nullptr, width); !joined.HasValue())
            return Core::Unexpected(joined.Error());
    }
    return selector;
}
} // namespace

Core::Expected<std::int32_t, std::string> StepIcarusObjectPrelude(std::int32_t count,
                                                                  const IcarusObjectServices& services)
{
    const auto selector = Selector(count, services);
    if (!selector.HasValue())
        return selector;
    Render::Models::CrtRandom& random = services.random;
    if (random.Next() % FlareGate != 0)
        return selector;
    // 0x60623b..0x6062ba: x e y con la x87 (entero + coordenada del héroe); la z, la del héroe − 1.000.
    const glm::vec3& hero = services.heroPosition;
    const float x = F32(static_cast<double>((random.Next() % FlareSpread) - FlareSpreadCenter) + D(hero.x));
    const float y = F32(static_cast<double>((random.Next() % FlareSpread) - FlareSpreadCenter) + D(hero.y));
    const glm::vec3 position{x, y, F32(D(hero.z) - FlareDrop)};
    if (services.effects == nullptr)
        return Core::Unexpected(std::string("0x6062d9: falta la tabla de efectos para el 0x5ac de Icarus"));
    static_cast<void>(services.effects->CreateIcarusEffect(World::IcarusFlareEffect, position, glm::vec3(0.0f),
                                                           glm::vec3(1.0f), random));
    return selector;
}

Core::Expected<void, std::string> StepIcarusSeenObject(World::WorldObject& object, std::int32_t selector,
                                                       std::int32_t& count, const IcarusObjectServices& services)
{
    ++count;
    if (selector == 0)
        return {};
    Render::Models::CrtRandom& random = services.random;
    if (random.Next() % SelectionGate != 0 || object.type < 0 || object.type > LastSelectableType)
    {
        --count;
        return {};
    }
    // 0x60642b..0x606499: los tres canales en orden, cada uno (rand() % 10) · 0,02f guardado.
    glm::vec3 light{};
    light.x = F32(static_cast<double>(random.Next() % SpriteLightSpread) * SpriteLightStep);
    light.y = F32(static_cast<double>(random.Next() % SpriteLightSpread) * SpriteLightStep);
    light.z = F32(static_cast<double>(random.Next() % SpriteLightSpread) * SpriteLightStep);
    if (services.sprites == nullptr)
        return Core::Unexpected(std::string("0x60649c: falta el pool de sprites para el 0x623 de Icarus"));
    // 0x5491e0(0x623, objeto, 0,5, luz, héroe, 0, 0): MuRet no guarda el dueño del sprite (Q-C303).
    static_cast<void>(
        services.sprites->Create(SelectionSprite, object.transform.position, SelectionSpriteScale, light, 0.0f, 0));
    for (int joint = 0; joint < 2; ++joint)
    {
        const float width = F32(static_cast<double>(random.Next() % SelectionWidthSpread) + SelectionWidthBase);
        if (auto joined = Join(services, ObjectJoint, object.transform.position, object.transform.position,
                               object.transform.angles, ObjectJointSubtype, &object, width);
            !joined.HasValue())
            return joined;
    }
    return {};
}
} // namespace Client::Scenes
