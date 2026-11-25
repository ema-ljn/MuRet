#include "World/Fish.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>
#include <bit>
#include <cmath>
#include <span>
#include <glm/ext/vector_float3.hpp>
#include "Assets/ModelIds.h"
#include "Core/Expected.h"
#include "Maps/MapId.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "World/IFishStepServices.h"

namespace Client::World
{
namespace
{
constexpr std::size_t OrdinaryCount = 3;
constexpr std::size_t ExtendedCount = 10;
constexpr std::uint8_t LorenciaWater = 5;
constexpr std::uint16_t BlockedAttribute = 8;

using Assets::Bug01Model;
using Assets::Bug02Model;
using Assets::CrowModel;
using Assets::Fish01Model;
using Assets::Fish04Model;
using Assets::Fish08Model;
using Assets::Fish09Model;
using Assets::RatModel;
using Assets::ScorpionModel;
using Maps::AidaMap;
using Maps::ArenaMap;
using Maps::AtlansMap;
using Maps::CharacterSelectMap;
using Maps::CrywolfMap;
using Maps::DungeonMap;
using Maps::IsKalimaMap;
using Maps::LorenciaMap;
using Maps::TarkanMap;

} // namespace

std::size_t FishStepCount(std::int32_t map)
{
    // 0x447043..0x447064; 33 no comparte el prefijo extendido de 8.
    if (map == AtlansMap || map == TarkanMap || IsKalimaMap(map) || map == CrywolfMap)
        return ExtendedCount;
    return OrdinaryCount;
}

bool FishDirectSpawnAllowed(std::int32_t map, std::uint8_t surface, std::uint16_t attributes)
{
    // 0x447108..0x447189. Los mapas8/33/34/54 llegan al fallback, con relecturas posteriores.
    if (map == LorenciaMap)
        return surface == LorenciaWater;
    if (map == DungeonMap || map == ArenaMap)
        return attributes < BlockedAttribute;
    if (map == AtlansMap || IsKalimaMap(map))
        return FishOpenWaterTile(attributes);
    return false;
}

bool FishSpawnEventGate(std::int32_t map)
{
    // 0x425070 reconoce exclusivamente los dos mapas de evento y no consume RNG.
    return map == AidaMap || map == CharacterSelectMap;
}

bool FishOpenWaterTile(std::uint16_t attributes)
{
    return attributes == 0 || attributes == 2;
}

FishMovement FishMovementFor(std::int16_t model, std::int32_t timer)
{
    if ((model >= Fish01Model && model <= CrowModel) || timer > 0)
        return FishMovement::Complete;
    return FishMovement::Reduced;
}

std::int32_t FishMovementMode(std::int16_t model)
{
    return model == Bug01Model || model == ScorpionModel ? 1 : 0;
}

bool FishNeedsOscillation(std::int16_t model)
{
    return model == Fish08Model || model == Fish09Model;
}

bool FishCopiesWorldPosition(std::int16_t model)
{
    return model == Fish01Model || model == ScorpionModel;
}

bool FishHasModel(std::int16_t model)
{
    return model != -1;
}
} // namespace Client::World

namespace Client::World
{
std::uint32_t FishTileIndex(std::int32_t x, std::int32_t y)
{
    constexpr std::uint32_t CoordinateMask = 0xff;
    constexpr unsigned RowShift = 8;
    return (static_cast<std::uint32_t>(x) & CoordinateMask) +
           ((static_cast<std::uint32_t>(y) & CoordinateMask) << RowShift);
}

namespace
{
constexpr float TileScale = std::bit_cast<float>(std::uint32_t{0x3c23d70a});
using Math::Tenth;
// Literales recuperados en contratos de la misma imagen: K6b8a20 y K6b89a8.
constexpr float OrdinarySpeedNumerator = std::bit_cast<float>(std::uint32_t{0x3f19999a});
constexpr float EffectSpeedNumerator = std::bit_cast<float>(std::uint32_t{0x40200000});
constexpr double FarDistance = 1500;
constexpr double SoundDistance = 600;
constexpr double HalfTurn = Math::HalfTurnAsDouble;
constexpr double FullTurn = Math::FullTurnAsDouble;
constexpr std::int32_t CandidateWidth = 1024;
constexpr std::int32_t CandidateRadius = 512;

class FishStep
{
public:
    FishStep(std::span<FishStepView, FishPoolSize> pool, const FishStepWorld& world, IFishStepServices& services)
        : m_pool(pool), m_world(world), m_services(services)
    {
    }

    Core::Expected<void, std::error_code> Run()
    {
        std::int32_t map = m_world.map;
        for (std::size_t i = 0; i < FishPoolSize; ++i)
        {
            if (i >= FishStepCount(map))
                continue;
            auto& fish = m_pool[i];
            if (fish.active == 0)
            {
                auto spawned = Spawn(fish, map);
                if (!spawned)
                    return Core::Unexpected(m_error);
                if (!*spawned)
                    continue;
            }
            if (fish.active == 0)
                continue;
            if (!Update(fish, i))
                return Core::Unexpected(m_error);
            map = m_world.map;
        }
        return {};
    }

private:
    template <class T> std::optional<T> Read(Core::Expected<T, std::error_code> result)
    {
        if (!result.HasValue())
        {
            m_error = result.Error();
            return std::nullopt;
        }
        return result.Value();
    }
    template <class T> bool Accept(Core::Expected<T, std::error_code> result)
    {
        if (!result.HasValue())
        {
            m_error = result.Error();
            return false;
        }
        return true;
    }
    bool Context(const FishStepView& fish)
    {
        if (fish.context)
            return true;
        m_error = std::make_error_code(std::errc::bad_address);
        return false;
    }
    bool Hero()
    {
        if (m_world.hero)
            return true;
        m_error = std::make_error_code(std::errc::bad_address);
        return false;
    }
    std::optional<std::uint32_t> Tile(const FishStepView& fish)
    {
        const double y = static_cast<double>(fish.position.y) * TileScale;
        auto yi = Read(m_services.Truncate(y));
        if (!yi)
            return std::nullopt;
        const double x = static_cast<double>(fish.position.x) * TileScale;
        auto xi = Read(m_services.Truncate(x));
        if (!xi)
            return std::nullopt;
        return FishTileIndex(*xi, *yi);
    }
    std::optional<std::uint16_t> Attribute(std::uint32_t tile)
    {
        if (tile < m_world.attributes.size())
            return m_world.attributes[tile];
        m_error = std::make_error_code(std::errc::result_out_of_range);
        return std::nullopt;
    }
    std::optional<std::uint8_t> Surface(std::uint32_t tile)
    {
        if (tile < m_world.surfaces.size())
            return m_world.surfaces[tile];
        m_error = std::make_error_code(std::errc::result_out_of_range);
        return std::nullopt;
    }
    std::optional<bool> Admit(std::uint32_t tile, std::int32_t& map)
    {
        map = m_world.map;
        if (map == LorenciaMap)
        {
            auto surface = Surface(tile);
            if (!surface)
                return std::nullopt;
            if (*surface == 5)
                return true;
        }
        else if (map == DungeonMap || map == ArenaMap || map == AtlansMap || IsKalimaMap(map))
        {
            auto attr = Attribute(tile);
            if (!attr)
                return std::nullopt;
            if (FishDirectSpawnAllowed(map, 0, *attr))
                return true;
        }
        auto gate = Read(m_services.EventGate());
        if (!gate)
            return std::nullopt;
        if (*gate != 0)
        {
            auto attr = Attribute(tile);
            if (!attr)
                return std::nullopt;
            if (FishOpenWaterTile(*attr))
                return true;
        }
        map = m_world.map;
        if (map != TarkanMap && map != CrywolfMap)
            return false;
        auto attr = Attribute(tile);
        if (!attr)
            return std::nullopt;
        return FishOpenWaterTile(*attr);
    }
    std::optional<bool> Spawn(FishStepView& fish, std::int32_t& map)
    {
        auto x = Read(m_services.Random());
        if (!x || !Hero())
            return std::nullopt;
        fish.position.x =
            static_cast<float>(static_cast<double>(*x % CandidateWidth - CandidateRadius) + m_world.hero->position.x);
        auto y = Read(m_services.Random());
        if (!y || !Hero())
            return std::nullopt;
        fish.position.y =
            static_cast<float>(static_cast<double>(*y % CandidateWidth - CandidateRadius) + m_world.hero->position.y);
        fish.position.z = m_world.hero->position.z;
        auto tile = Tile(fish);
        if (!tile)
            return std::nullopt;
        auto admitted = Admit(*tile, map);
        if (!admitted)
            return std::nullopt;
        if (!*admitted)
            return false;
        if (!Initialize(fish, map))
            return std::nullopt;
        return true;
    }
    bool Initialize(FishStepView& fish, std::int32_t& map)
    {
        fish.active = 1;
        fish.field174 = 0.0f;
        fish.field170 = 1;
        fish.field64 = -1;
        fish.light = glm::vec3(0.5f);
        fish.angles = glm::vec3(0);
        fish.fieldE8 = 1;
        fish.field16d = 1;
        fish.counter = 0;
        fish.field58 = -1;
        auto timer = Read(m_services.Random());
        if (!timer)
            return false;
        fish.timer = *timer % 128;
        auto scale = Read(m_services.Random());
        if (!scale)
            return false;
        fish.turnRate = 13;
        map = m_world.map;
        fish.field8f = 1;
        const float initialScale = static_cast<float>(static_cast<double>(*scale % 4 + 4) * Tenth);
        fish.scale = initialScale;
        if (!Specialize(fish, map, initialScale))
            return false;
        if (!IsKalimaMap(map))
            return true;
        fish.model = -1;
        auto draw = Read(m_services.Random());
        if (!draw)
            return false;
        const double retainedScale = static_cast<double>(*draw % 4 + 8) * Tenth;
        fish.turnRate = 9;
        fish.scale = static_cast<float>(retainedScale);
        fish.timer = 70;
        fish.speed = static_cast<float>(static_cast<double>(EffectSpeedNumerator) / retainedScale);
        if (!Effect(fish, 0x610, 8, 50))
            return false;
        map = m_world.map;
        return true;
    }
    bool Effect(FishStepView& fish, std::int32_t type, std::int32_t subtype, float scale)
    {
        if (!Context(fish))
            return false;
        return Accept(m_services.CreateEffect(
            {type, fish.position, fish.position, fish.angles, subtype, fish.context, scale, -1, 0, 0, -1}));
    }
    bool Specialize(FishStepView& fish, std::int32_t& map, float initialScale)
    {
        if (map == DungeonMap || map == ArenaMap)
        {
            fish.model = map == DungeonMap ? RatModel : Bug01Model;
            fish.speed = static_cast<float>(static_cast<double>(OrdinarySpeedNumerator) / initialScale);
            return true;
        }
        if (map == LorenciaMap)
        {
            fish.model = Fish01Model;
            auto draw = Read(m_services.Random());
            if (!draw)
                return false;
            fish.field170 = static_cast<float>(static_cast<double>(*draw % 2 + 2) * Tenth);
            fish.speed = static_cast<float>(static_cast<double>(OrdinarySpeedNumerator) / fish.scale);
        }
        else if (map == TarkanMap || map == AidaMap)
        {
            fish.model = Bug02Model;
            auto draw = Read(m_services.Random());
            if (!draw)
                return false;
            const double retainedScale = static_cast<double>(*draw % 4 + 8) * Tenth;
            fish.scale = static_cast<float>(retainedScale);
            fish.turnRate = 9;
            fish.timer = 100;
            fish.previousPosition = fish.position;
            fish.speed = static_cast<float>(static_cast<double>(EffectSpeedNumerator) / retainedScale);
            if (!Effect(fish, 0x618, 4, 30))
                return false;
        }
        else if (map == CrywolfMap)
        {
            if (!Hero())
                return false;
            if (m_world.hero->flag == 1)
            {
                fish.active = 0;
                return true;
            }
            fish.model = ScorpionModel;
            auto scale = Read(m_services.Random());
            if (!scale)
                return false;
            fish.scale = static_cast<float>(static_cast<double>(*scale % 4 + 8) * Tenth);
            auto speed = Read(m_services.Random());
            if (!speed)
                return false;
            const double numerator = static_cast<double>(*speed % 8 + 1) * Tenth;
            fish.previousPosition = fish.position;
            fish.turnRate = 1;
            fish.timer = 100;
            fish.speed = static_cast<float>(numerator / fish.scale);
            if (!Effect(fish, 0x697, 0, 30))
                return false;
        }
        else if (map == AtlansMap)
        {
            if (!SpecializeAtlans(fish))
                return false;
        }
        else
            return true;
        map = m_world.map;
        return true;
    }
    bool SpecializeAtlans(FishStepView& fish)
    {
        auto scale = Read(m_services.Random());
        if (!scale)
            return false;
        fish.scale = static_cast<float>(static_cast<double>(*scale % 2 + 8) * Tenth);
        if (!Hero())
            return false;
        const double heroY = static_cast<double>(m_world.hero->position.y) * TileScale;
        const bool lower = !(heroY >= 128);
        auto model = Read(m_services.Random());
        if (!model)
            return false;
        fish.model = static_cast<std::int16_t>(lower ? *model % 4 + Fish04Model : *model % 2 + Fish08Model);
        if (!lower && FishNeedsOscillation(fish.model))
        {
            fish.field64 = 0;
            fish.oscillation = 1;
        }
        fish.speed = static_cast<float>((lower ? 1.0 : 0.5) / fish.scale);
        auto turn = Read(m_services.Random());
        if (!turn)
            return false;
        fish.turnRate = lower ? (*turn % 2 == 0 ? 2.0f : 3.0f) : (*turn % 2 == 0 ? 1.0f : 2.0f);
        auto phase = Read(m_services.Random());
        if (!phase)
            return false;
        fish.phase = static_cast<float>(static_cast<double>(*phase % 32) * Tenth);
        auto height = Read(m_services.Height(fish.position.x, fish.position.y));
        if (!height)
            return false;
        const float savedHeight = static_cast<float>(*height);
        auto z = Read(m_services.Random());
        if (!z)
            return false;
        fish.position.z = static_cast<float>(static_cast<double>(savedHeight) + (*z % 150 + 50));
        return true;
    }
    bool Update(FishStepView& fish, std::size_t index)
    {
        if (FishHasModel(fish.model))
        {
            auto borrowed = Read(m_services.LookupModel(fish.model));
            if (!borrowed)
                return false;
            borrowed->action = fish.action;
            const float speed = static_cast<float>(static_cast<double>(fish.speed) * 0.5);
            if (!Accept(m_services.AdvanceModel(*borrowed, fish, speed)))
                return false;
        }
        const std::int16_t model = fish.model;
        if (FishNeedsOscillation(model))
        {
            auto product = Read(m_services.OscillationProductPC53(fish.phase));
            if (!product)
                return false;
            const double shifted = *product + 0.5;
            fish.oscillation = static_cast<float>(shifted);
            fish.phase = static_cast<float>(static_cast<double>(fish.phase) + Tenth);
        }
        const auto movement = FishMovementFor(model, fish.timer);
        const auto mode = movement == FishMovement::Complete ? FishMovementMode(model) : 0;
        if (!Accept(m_services.Move(fish, mode)))
            return false;
        if (movement == FishMovement::Complete && !FullMovement(fish, index))
            return false;
        return Epilogue(fish);
    }
    bool FullMovement(FishStepView& fish, std::size_t index)
    {
        if (!Accept(m_services.Steer(fish, index, m_pool)))
            return false;
        if (!Accept(m_services.AngleMatrix(fish)))
            return false;
        auto draw = Read(m_services.Random());
        if (!draw)
            return false;
        const float length = static_cast<float>(static_cast<double>(*draw % 4 + 6) * fish.speed);
        auto output = Read(m_services.Rotate(glm::vec3(length, 0, 0), fish.matrix));
        if (!output)
            return false;
        fish.position.x = static_cast<float>(static_cast<double>(output->x) + fish.position.x);
        const float savedX = fish.position.x;
        fish.position.y = static_cast<float>(static_cast<double>(output->y) + fish.position.y);
        const float savedY = fish.position.y;
        fish.position.z = static_cast<float>(static_cast<double>(output->z) + fish.position.z);
        auto height = Read(m_services.Height(savedX, savedY));
        if (!height)
            return false;
        fish.position.z = static_cast<float>(*height);
        const double aheadX = static_cast<double>(output->x) * 3;
        fish.aheadX = static_cast<float>(aheadX + fish.position.x);
        const double aheadY = static_cast<double>(output->y) * 3;
        fish.aheadY = static_cast<float>(aheadY + fish.position.y);
        auto tile = Tile(fish);
        if (!tile)
            return false;
        if (!Collision(fish, *tile))
            return false;
        const std::int16_t model = fish.model;
        if (FishCopiesWorldPosition(model))
        {
            fish.previousPosition.x = fish.position.x;
            fish.previousPosition.y = fish.position.y;
            fish.previousPosition.z = fish.position.z;
        }
        if (!Hero())
            return false;
        const auto& hero = m_world.hero->position;
        const double dx = static_cast<double>(fish.position.x) - hero.x;
        const double dy = static_cast<double>(fish.position.y) - hero.y;
        const double xx = dx * dx;
        const double yy = dy * dy;
        const double sum = xx + yy;
        const double distance = std::sqrt(sum);
        if (distance >= FarDistance)
            fish.active = 0;
        if (distance >= SoundDistance || model != RatModel)
            return true;
        auto sound = Read(m_services.Random());
        if (!sound)
            return false;
        if (*sound % 256 == 0 && (!Context(fish) || !Accept(m_services.Sound(15, fish.context, 0))))
            return false;
        return true;
    }
    bool Collision(FishStepView& fish, std::uint32_t tile)
    {
        std::int32_t map = m_world.map;
        bool special = map == TarkanMap || IsKalimaMap(map);
        if (!special)
        {
            auto gate = Read(m_services.EventGate());
            if (!gate)
                return false;
            special = *gate != 0;
            if (special)
                map = m_world.map;
        }
        bool collision = false;
        if (special)
        {
            auto attr = Attribute(tile);
            if (!attr)
                return false;
            collision = *attr == 1 || *attr >= 8;
        }
        else
        {
            const auto model = fish.model;
            if (model == Fish01Model)
            {
                auto surface = Surface(tile);
                if (!surface)
                    return false;
                collision = *surface != 5;
            }
            if (!collision && model == RatModel)
            {
                auto attr = Attribute(tile);
                if (!attr)
                    return false;
                collision = *attr >= 8;
            }
            if (!collision && m_world.map == AtlansMap)
            {
                auto attr = Attribute(tile);
                if (!attr)
                    return false;
                collision = *attr == 1 || *attr >= 8;
            }
        }
        if (collision)
            Turn(fish, special);
        else if (fish.counter > 0)
            --fish.counter;
        if (special && IsKalimaMap(map) && fish.timer <= 1)
            fish.active = 0;
        if (fish.counter >= 2)
            fish.active = 0;
        return true;
    }
    static void Turn(FishStepView& fish, bool special)
    {
        const double angle = static_cast<double>(fish.angles.z) + HalfTurn;
        fish.angles.z = static_cast<float>(angle);
        const auto next = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(fish.counter) + 1U);
        if (angle >= FullTurn)
        {
            if (!special)
                fish.counter = next;
            fish.angles.z = static_cast<float>(angle - FullTurn);
        }
        fish.counter = next;
    }
    bool Epilogue(FishStepView& fish)
    {
        fish.timer = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(fish.timer) - 1U);
        if (fish.timer <= 0 && fish.model != Bug01Model)
        {
            auto reset = Read(m_services.Random());
            if (!reset)
                return false;
            if (*reset % 64 == 0)
            {
                auto timer = Read(m_services.Random());
                if (!timer)
                    return false;
                fish.timer = *timer % 128;
            }
        }
        return Accept(m_services.Finalize(fish));
    }
    std::span<FishStepView, FishPoolSize> m_pool;
    const FishStepWorld& m_world;
    IFishStepServices& m_services;
    std::error_code m_error;
};
} // namespace

Core::Expected<void, std::error_code> StepFish(std::span<FishStepView, FishPoolSize> pool, const FishStepWorld& world,
                                               IFishStepServices& services)
{
    return FishStep(pool, world, services).Run();
}
} // namespace Client::World
