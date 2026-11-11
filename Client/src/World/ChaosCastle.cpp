#include "World/ChaosCastle.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <format>
#include <string>

namespace Client::World
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

// 0x405e1e..0x405e2f: rand() & 0x80000007 con el ajuste del signo, el resto de dividir por 8.
constexpr int CellOdds = 8;
constexpr std::uint8_t FirstArea = 0;
constexpr std::uint8_t SecondArea = 3;
constexpr std::uint8_t ThirdArea = 6;
// 0x405eb0..0x405f0e: rand() % 30 (0x1e), menos 15 (0x6b87bc), más la celda por 100 (0x6b86b4).
constexpr int StoneSpread = 30;
constexpr double StoneBack = 15.0;
constexpr double CellSize = 100.0;
// 0x405ee6: la escala 1,5 (0x3fc00000).
constexpr float StoneScale = 1.5f;
// 0x405f2c..0x405f58: rand() % 5 y (rand() % 3 − 3) por 0,1f (0x6b87b8).
constexpr int ShakeOdds = 5;
constexpr int ShakeSpan = 3;
constexpr double ShakeStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
// 0x4054ad..0x40559c: los rand() % 10, el desparramo de 800 (0x320) menos 400 (0x6b87b0), la z menos 150
// (0x6b87ac) y la escala más 50 (0x6b86b0).
constexpr int PreludeOdds = 10;
constexpr int MeteorSpread = 800;
constexpr double MeteorBack = 400.0;
constexpr double MeteorDrop = 150.0;
constexpr int MeteorScaleSpan = 10;
constexpr double MeteorScaleBase = 50.0;
constexpr std::int32_t MeteorJoint = 0x6ee;
constexpr std::int32_t MeteorSubtype = 9;
// 0x4055b0: el 3, la cuenta de +0x60 y +0x8a.
constexpr std::int16_t TrapObject = 3;
constexpr std::int32_t TrapCount = 10;

const std::array<ChaosCastleCellRect, 4>* RectsOf(std::uint8_t area)
{
    if (area == FirstArea)
        return &ChaosCastleAreaZero;
    if (area == SecondArea)
        return &ChaosCastleAreaThree;
    if (area == ThirdArea)
        return &ChaosCastleAreaSix;
    return nullptr;
}

// fild del resto, fsub de 15, fild de la celda, fmul de 100 y faddp: exacto en la x87, guardado en float.
float StoneCoordinate(int spread, std::uint8_t cell)
{
    return F32((static_cast<double>(spread) - StoneBack) + (static_cast<double>(cell) * CellSize));
}
} // namespace

Core::Expected<void, std::string> StepChaosCastleCell(std::uint8_t x, std::uint8_t y,
                                                      const ChaosCastleCellContext& context)
{
    if (context.random == nullptr)
        return Core::Unexpected(std::string("0x405e1e: 0x405e00 saca rand() con cada celda, y el pase no lo tiene"));
    Render::Models::CrtRandom& random = *context.random;
    if (random.Next() % CellOdds != 0)
        return {};
    const std::array<ChaosCastleCellRect, 4>* rects = RectsOf(context.area);
    if (rects == nullptr)
        return {};
    bool inside = false;
    for (const ChaosCastleCellRect& rect : *rects)
    {
        if (x >= rect.x0 && x <= rect.x1 && y >= rect.y0 && y <= rect.y1)
        {
            inside = true;
            break;
        }
    }
    if (!inside)
        return {};
    const float stoneX = StoneCoordinate(random.Next() % StoneSpread, x);
    const float stoneY = StoneCoordinate(random.Next() % StoneSpread, y);
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x405f24: falta el pool de partículas para la piedra 0x5f6 de Chaos "
                                            "Castle"));
    const auto created = context.particles->CreateChecked(
        ChaosCastleFallingStone, glm::vec3(stoneX, stoneY, context.heroZ), glm::vec3(0.0f), glm::vec3(1.0f), 0,
        StoneScale, context.worldClock, random, 0);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x405f24: la piedra 0x5f6/0 de Chaos Castle falla con error {}",
                                            static_cast<unsigned>(created.Error())));
    if (random.Next() % ShakeOdds != 0)
        return {};
    const int shake = random.Next() % ShakeSpan - ShakeSpan;
    if (context.cameraShake == nullptr)
        return Core::Unexpected(std::string("0x405f58: falta el temblor de la cámara (0x7d325c8)"));
    *context.cameraShake = F32(static_cast<double>(shake) * ShakeStep);
    return {};
}

Core::Expected<std::int32_t, std::string> StepChaosCastlePrelude(std::int32_t count,
                                                                 const ChaosCastlePreludeContext& context)
{
    if (context.random == nullptr)
        return Core::Unexpected(std::string("0x4054ad: 0x405490 saca rand(), y el paso no lo tiene"));
    Render::Models::CrtRandom& random = *context.random;
    if (random.Next() % PreludeOdds == 0 && count != 0)
        return random.Next() % count;
    if (context.heroInSafeZone || random.Next() % PreludeOdds != 0)
        return 0;
    // fild del resto, fadd de la coordenada y fsub de 400, guardado en float; la z con fld, fsub y fstp.
    glm::vec3 position;
    position.x = F32((static_cast<double>(random.Next() % MeteorSpread) + static_cast<double>(context.heroPosition.x)) -
                     MeteorBack);
    position.y = F32((static_cast<double>(random.Next() % MeteorSpread) + static_cast<double>(context.heroPosition.y)) -
                     MeteorBack);
    position.z = F32(static_cast<double>(context.heroPosition.z) - MeteorDrop);
    const float scale = F32(static_cast<double>(random.Next() % MeteorScaleSpan) + MeteorScaleBase);
    if (!context.createJoint)
        return Core::Unexpected(std::string("0x405597: falta quien cree la articulación 0x6ee/9 de Chaos Castle"));
    glm::vec3 first = position;
    glm::vec3 second = position;
    glm::vec3 angles = context.heroAngles;
    const FishEffectRequest request{.type = MeteorJoint,
                                    .firstPosition = first,
                                    .secondPosition = second,
                                    .angles = angles,
                                    .subtype = MeteorSubtype,
                                    .ownerContext = nullptr,
                                    .scale = scale,
                                    .key = -1,
                                    .skill = 0,
                                    .skillSerial = 0,
                                    .target = -1};
    if (auto created = context.createJoint(request); !created.HasValue())
        return Core::Unexpected(created.Error());
    return 0;
}

void StepChaosCastleSeenObject(WorldObject& object, std::int32_t& selector, std::int32_t& count,
                               std::uint8_t& trapArmed)
{
    if (object.type != TrapObject)
        return;
    ++count;
    if (trapArmed != 0)
    {
        object.field60 = TrapCount;
        object.field8a = 1;
        trapArmed = 0;
        return;
    }
    if (selector == 0)
        return;
    --selector;
    object.field60 = TrapCount;
    object.field8a = selector == 0 ? std::int16_t{1} : std::int16_t{0};
}

float ChaosCastlePoseAlpha(std::int32_t map, std::uint8_t heroByte1a1, float alpha)
{
    constexpr float Fade = std::bit_cast<float>(0x3e19999au);
    if (!IsChaosCastleMap(map) || heroByte1a1 == 0)
        return alpha;
    return F32(static_cast<double>(alpha) - static_cast<double>(Fade));
}
} // namespace Client::World
