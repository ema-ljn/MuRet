#include "World/CastleSiegeEmission.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "World/CastleSiegeObjectStep.h"
#include "World/ModelEffects.h"
#include "Terrain/TerrainDefines.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

namespace Client::World
{
namespace
{
using Result = Core::Expected<void, std::string>;
using Crt::CrtRandom;
constexpr std::int32_t FirstBattleSound = 0x1e3;
constexpr std::int32_t LastBattleSound = 0x1e7;
constexpr std::int16_t NearStone = 0x12b;
constexpr std::int16_t FarStone = 0x12c;
constexpr std::int16_t BattleArrow = 0xdb;
constexpr float RockZ = 500.0f;
constexpr float ArrowZ = 350.0f;
constexpr float CellScale = Terrain::CellSize;
constexpr std::int32_t ArrowSpacing = 60;

// El negativo pequeño se corre otros100+rand; no se reemplaza como el positivo (42bb5c/42bc73/42c22f).
int SeparateFromTheHero(int offset, int threshold, CrtRandom& random)
{
    constexpr int Correction = 100;
    constexpr int Jitter = 30;
    if (offset > 0 && offset < threshold)
        return random.Next() % Jitter + Correction;
    if (offset < 0 && offset > -threshold)
        return offset - Correction - random.Next() % Jitter;
    return offset;
}

Result Emit(const CastleSiegePrelude& prelude, const Kernel::ModelEffectArguments& arguments)
{
    if (!prelude.emit)
        return Core::Unexpected(std::string("0x42bd01/0x42c31e/0x42c4e5: falta el alta de efectos 0x518cf0"));
    return prelude.emit(arguments);
}

// 42baa2..42bd01: el rand%3 precede al rango; el caller vuelve a leerY aun sin crear una piedra.
Result EmitTheRock(const CastleSiegePrelude& prelude, std::int32_t y)
{
    CrtRandom& random = *prelude.random;
    const bool far = random.Next() % 3 == 0;
    if ((far && (y <= 70 || y >= 177)) || (!far && (y <= 50 || y >= 131)))
        return {};
    const int offset = SeparateFromTheHero(random.Next() % 1000 - 500, far ? 100 : 30, random);
    Kernel::ModelEffectArguments arguments{
        .type = far ? FarStone : NearStone,
        .position = {static_cast<float>(static_cast<double>(prelude.heroPosition.x) + offset), 0.0f, RockZ},
        .light = glm::vec3(1.0f)};
    const std::int32_t targetY =
        far ? (y < 179 ? 179 : y - random.Next() % 10 - 10) : (y > 55 ? 45 : y - random.Next() % 10 - 10);
    const int jitter = random.Next() % 40 - 20;
    const float base = static_cast<float>(static_cast<double>(targetY) * CellScale);
    arguments.position.y = far ? static_cast<float>(static_cast<double>(base) - jitter)
                               : static_cast<float>(static_cast<double>(targetY) * CellScale + jitter);
    return Emit(prelude, arguments);
}

// 42c198..42c31e: FSUBP resta n*60; cada vuelta suma60 ANTES de emitir. Dos rand por vuelta, uno descartado.
Result EmitTheArrowRow(const CastleSiegePrelude& prelude, std::int32_t y)
{
    if (prelude.logicSteps % 60 != 0 || !((y > 58 && y < 113) || (y > 117 && y < 159)))
        return {};
    CrtRandom& random = *prelude.random;
    const int count = ((random.Next() & 3) + 2) / 2;
    int offset = SeparateFromTheHero(random.Next() % 1400 - 700 - count * ArrowSpacing, 100, random);
    for (int index = 0; index < count * 2; ++index)
    {
        offset += ArrowSpacing;
        const bool side = (random.Next() & 1) != 0;
        const bool south = y < 113 || (y <= 117 && side);
        static_cast<void>(random.Next());
        Kernel::ModelEffectArguments arguments{
            .type = BattleArrow,
            .position = {static_cast<float>(static_cast<double>(prelude.heroPosition.x) + offset),
                         static_cast<float>(static_cast<double>(y + (south ? -10 : 10)) * CellScale), ArrowZ},
            .angles = {0.0f, 0.0f, south ? 180.0f : 0.0f},
            .light = glm::vec3(1.0f),
            .subtype = 4};
        if (auto emitted = Emit(prelude, arguments); !emitted.HasValue())
            return emitted;
    }
    return {};
}

// 42c3ed..42c41a: dentro82..112 el segundo rand%10 se pide sólo si el primero dio0; afuera, siempre uno.
int ArrowSubtype(std::int32_t y, CrtRandom& random)
{
    if (y >= 82 && y <= 112 && random.Next() % 10 != 0)
        return 3;
    return random.Next() % 10 == 0 ? 3 : 4;
}

Result EmitTheSingleArrow(const CastleSiegePrelude& prelude, std::int32_t y)
{
    if (prelude.logicSteps % 10 != 0 || y <= 58 || y >= 159)
        return {};
    CrtRandom& random = *prelude.random;
    const int offset = SeparateFromTheHero(random.Next() % 1400 - 700, 100, random);
    const int subtype = ArrowSubtype(y, random);
    const bool side = (random.Next() & 1) != 0;
    const bool south = y < 82 || (y <= 112 && side);
    const int delta = random.Next() % 3;
    const std::int32_t target = south ? y - delta - 10 : y + delta + 10;
    const int jitter = random.Next() % 60 - 30;
    const float base = static_cast<float>(static_cast<double>(target) * CellScale);
    Kernel::ModelEffectArguments arguments{
        .type = BattleArrow,
        .position = {static_cast<float>(static_cast<double>(prelude.heroPosition.x) + offset),
                     south ? static_cast<float>(static_cast<double>(target) * CellScale + jitter)
                           : static_cast<float>(static_cast<double>(base) - jitter),
                     ArrowZ},
        .angles = {0.0f, 0.0f, south ? 180.0f : 0.0f},
        .light = glm::vec3(1.0f),
        .subtype = subtype};
    return Emit(prelude, arguments);
}
} // namespace

Core::Expected<void, std::string> EmitTheCastleBattle(const CastleSiegePrelude& prelude)
{
    if (prelude.random == nullptr)
        return Core::Unexpected(std::string("0x42b5fe: falta el rand de la batalla del castillo"));
    CrtRandom& random = *prelude.random;
    if (random.Next() % 10 == 0)
    {
        for (std::int32_t sound = FirstBattleSound; sound <= LastBattleSound; ++sound)
            if (random.Next() % 100 == 0)
                prelude.sound(Audio::SoundId{sound});
    }
    if (!prelude.heroY)
        return Core::Unexpected(std::string("0x42b6d6/0x42bd09: falta la lectura protegida de la Y del héroe"));
    if (prelude.logicSteps % 20 == 0)
    {
        const std::int32_t y = prelude.heroY();
        if (auto rock = EmitTheRock(prelude, y); !rock.HasValue())
            return rock;
    }
    const std::int32_t y = prelude.heroY();
    if (auto row = EmitTheArrowRow(prelude, y); !row.HasValue())
        return row;
    return EmitTheSingleArrow(prelude, y);
}
} // namespace Client::World
