#include "World/Birds.h"
#include "World/IBirdStepServices.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <cstring>
#include <span>
#include <system_error>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::World
{
namespace
{
constexpr std::size_t OrdinaryCount = 5;
constexpr std::size_t AtlansCount = 40;
constexpr std::size_t IcarusCount = 13;
constexpr std::size_t KalimaCount = 2;
constexpr std::int16_t SingleGlowType = 0xae;
constexpr std::int16_t DoubleGlowType = 0xbc;
constexpr double LightStep = static_cast<double>(0.01f);
constexpr double RedFactor = static_cast<double>(0.2f);
constexpr double BlueFactor = static_cast<double>(0.4f);
} // namespace

std::size_t BirdStepCount(std::int32_t map)
{
    // 446120-contract § Pool: el draw posterior visita40, aunque aquí se procesen menos.
    if (map == 7)
        return AtlansCount;
    if (map == 10)
        return IcarusCount;
    if ((map >= 24 && map <= 29) || map == 36)
        return KalimaCount;
    return OrdinaryCount;
}

void FinishOrdinaryBirdMotion(const BirdStepTail& bird, const std::int32_t& map, const std::uint16_t& tileAttributes)
{
    if (bird.timer <= 0 && map == 7 && tileAttributes == 1)
    {
        bird.angles.z = static_cast<float>(static_cast<double>(bird.angles.z) + 180.0);
        // 44691a: unordered conserva el giro; se resta360 una sola vez, también en igualdad.
        if (bird.angles.z >= 360)
            bird.angles.z = static_cast<float>(static_cast<double>(bird.angles.z) - 360.0);
        bird.timer = 10;
        bird.state = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bird.state) + 1u);
    }
    if (bird.state >= 2)
        bird.alive = 0;
    bird.timer = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bird.timer) - 1u);
}

std::optional<glm::vec3> BirdGlowLight(std::int16_t type, Render::Models::CrtRandom& random)
{
    if (type != SingleGlowType && type != DoubleGlowType)
        return std::nullopt;
    // 446b60-contract § AE/BC: el segundo producto usa q retenido, no el primer storeF32.
    const int base = type == SingleGlowType ? 64 : 128;
    const double q = static_cast<double>((random.Next() % 32) + base) * LightStep;
    if (type == SingleGlowType)
    {
        const auto blue = static_cast<float>(q * BlueFactor);
        return glm::vec3{static_cast<float>(q * RedFactor), blue, blue};
    }
    return glm::vec3{static_cast<float>(q), static_cast<float>(q * RedFactor), 0};
}
namespace
{
// Perfil aprobado PC53/RN: el binding establece RN y compila sin fast-math/FMA.
// El store volatile fuerza el redondeo binario64 por operación, también con evaluación extendida.
double Round53(double value)
{
    volatile double rounded = value;
    return rounded;
}
float StoreFloat(double value)
{
    return static_cast<float>(Round53(value));
}

// Offsets de446120-contract. memcpy evita aliasing/alineación sobre la vista prestada.
enum Field : std::size_t
{
    Active = 0,
    Type = 2,
    State = 4,
    Speed = 0xc,
    Position = 0x10,
    Angles = 0x1c,
    Hidden = 0x58,
    Timer = 0x60,
    Blend = 0x64,
    Phase = 0x84,
    Scale = 0xd8,
    Size = 0xe0,
    Extra = 0xe4,
    Flag = 0xe8,
    Light = 0xf4,
    Action0 = 0x110,
    Action1 = 0x111,
    Kind = 0x16d,
    One = 0x170,
    Other = 0x174,
};
template <typename T> T Read(BirdRecord bird, std::size_t field)
{
    T value;
    std::memcpy(&value, bird.data() + field, sizeof value);
    return value;
}
template <typename T> void Write(BirdRecord bird, std::size_t field, const T& value)
{
    std::memcpy(bird.data() + field, &value, sizeof value);
}
void Byte(BirdRecord bird, Field field, std::uint8_t value)
{
    Write(bird, field, value);
}
bool Kalima(std::int32_t map)
{
    return (map >= 24 && map <= 29) || map == 36;
}
std::int32_t Decrement(std::int32_t value)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) - 1u);
}
std::error_code Prefix(IBirdStepServices& s)
{
    if (s.Mode() == 0)
        return {};
    const glm::vec3& cachedActor = s.ActorPosition();
    if (s.Random() % 40 == 0)
    {
        const int x = s.Random() % 600 - 200;
        const float px = StoreFloat(static_cast<double>(x) + s.ActorPosition().x);
        const int y = s.Random() % 400 + 200;
        const auto& actor = s.ActorPosition();
        const glm::vec3 position{px, StoreFloat(static_cast<double>(y) + actor.y),
                                 StoreFloat(static_cast<double>(actor.z) + 300)};
        if (auto error = s.PrefixEffect(position))
            return error;
        if (auto error = s.Audio(0x42, nullptr))
            return error;
    }
    return s.PrefixLight(cachedActor.x, cachedActor.y);
}
std::error_code GenericBirth(BirdRecord b, IBirdStepServices& s)
{
    Byte(b, Kind, 1);
    Write(b, Speed, 0.8f);
    Write(b, Size, 10.0f);
    Write(b, Hidden, std::int32_t{-1});
    Write(b, Blend, std::int32_t{-1});
    Write(b, Phase, StoreFloat((s.Random() % 314) * static_cast<double>(0.01f)));
    const int x = s.Random() % 1024 - 512;
    Write(b, Position, StoreFloat(x + static_cast<double>(s.ActorPosition().x)));
    const int y = s.Random() % 1024 - 512;
    const auto& actor = s.ActorPosition();
    Write(b, Position + 4, StoreFloat(y + static_cast<double>(actor.y)));
    Write(b, Position + 8, actor.z);
    const float height = StoreFloat(s.Height(Read<float>(b, Position), Read<float>(b, Position + 4)));
    const int z = s.Random() % 200 + 150;
    Write(b, Angles, glm::vec3{0});
    Write(b, Position + 8, StoreFloat(static_cast<double>(height) + z));
    return {};
}
std::error_code OrdinaryBirth(BirdRecord b, std::int32_t index, std::int32_t map, IBirdStepServices& s)
{
    std::fill(b.begin(), b.end(), std::byte{0});
    Byte(b, Active, 1);
    Write(b, Scale, 1.0f);
    Byte(b, Flag, 1);
    Write(b, Timer, std::int32_t{0});
    Write(b, State, std::int32_t{0});
    Write(b, Light, glm::vec3{0.5f});
    Write(b, Other, 0.0f);
    Write(b, One, 1.0f);
    Write(b, Extra, 13.0f);
    Byte(b, Action0, 0);
    Byte(b, Action1, 0);
    std::int16_t type = 0;
    if (map == 0)
        type = 0xad;
    else if (map == 1 || map == 4)
        type = 0xaf;
    else if (map == 3)
    {
        type = 0xae;
        Write(b, Scale, 0.3f);
        Byte(b, Flag, 0);
        Write(b, Light, glm::vec3{1});
    }
    else if (map >= 11 && map <= 17)
        type = 0xbc;
    Write(b, Type, type);
    if (type == 0)
    {
        for (auto handler : {BirdHandler::SpawnIcarus, BirdHandler::SpawnAtlans, BirdHandler::SpawnKalima})
        {
            const auto result = s.Call(handler, b, index);
            if (result.error)
                return result.error;
            if (result.value == 1)
                return {};
            if (result.value != 0)
                break;
        }
    }
    return GenericBirth(b, s);
}
std::error_code SpecialBirth(BirdRecord b, IBirdStepServices& s)
{
    if (s.Random() % 300 != 0)
        return {};
    Byte(b, Active, 1);
    if (auto error = s.PrepareSpecial())
        return error;
    Write(b, Type, std::int16_t{0x171});
    const int speed = s.Random() % 3 + 6;
    Write(b, Other, 1.0f);
    Write(b, One, 1.0f);
    Write(b, Scale, 0.5f);
    Byte(b, Flag, 1);
    Byte(b, Kind, 0);
    Write(b, State, std::int32_t{0});
    Write(b, Hidden, std::int32_t{-1});
    Write(b, Blend, std::int32_t{-1});
    Write(b, Speed, StoreFloat(speed * static_cast<double>(0.1f)));
    Write(b, Timer, s.Random() % 128 + 128);
    const int phase = s.Random() % 10;
    Write(b, Angles, 0.0f);
    const int mode = s.Mode();
    Write(b, Angles + 4, 0.0f);
    Write(b, Angles + 8, -90.0f);
    Write(b, Phase, StoreFloat(phase * static_cast<double>(0.1f)));
    if (mode == 3)
        Write(b, State, std::int32_t{1});
    const int x = s.Random() % 600 - 100;
    Write(b, Position, StoreFloat(x + static_cast<double>(s.ActorPosition().x)));
    const int y = s.Random() % 400 + 200;
    const auto& actor = s.ActorPosition();
    Write(b, Position + 4, StoreFloat(y + static_cast<double>(actor.y)));
    Write(b, Position + 8, StoreFloat(static_cast<double>(actor.z) + 300));
    return {};
}
std::error_code SpecialMotion(BirdRecord b, IBirdStepServices& s)
{
    if (auto error = s.Rotation(b))
        return error;
    const glm::vec3 vector{StoreFloat(static_cast<double>(Read<float>(b, Speed)) * 40), 0, 0};
    const auto delta = s.Transform(b, vector);
    Write(b, Position, StoreFloat(static_cast<double>(Read<float>(b, Position)) + delta.x));
    Write(b, Position + 4, StoreFloat(static_cast<double>(Read<float>(b, Position + 4)) + delta.y));
    Write(b, Position + 8, StoreFloat(static_cast<double>(Read<float>(b, Position + 8)) + delta.z));
    const auto height = s.Height(Read<float>(b, Position), Read<float>(b, Position + 4));
    Write(b, Position + 8, StoreFloat(height + 300));
    const float sine = s.SinSpill(Read<float>(b, Phase));
    const double wave = Round53(s.Wave(sine) * 100.0);
    const auto timer = Decrement(Read<std::int32_t>(b, Timer));
    Write(b, Timer, timer);
    const double lowered = Round53(static_cast<double>(Read<float>(b, Position + 8)) - wave);
    Write(b, Position + 8, StoreFloat(lowered + 100.0));
    const double phaseStep = Round53(static_cast<double>(Read<float>(b, Speed)) * static_cast<double>(0.05f));
    Write(b, Phase, StoreFloat(phaseStep + Read<float>(b, Phase)));
    if (timer <= 0)
        Byte(b, Active, 0);
    if (s.Random() % 128 == 0)
        return s.Audio(0x14e, nullptr);
    return {};
}
std::error_code OrdinaryAudio(BirdRecord b, std::int32_t tile, IBirdStepServices& s)
{
    const auto& actor = s.ActorPosition();
    if (!s.AudioDistance(Read<glm::vec3>(b, Position), actor))
        return {};
    const auto type = Read<std::int16_t>(b, Type);
    if (type == 0xad)
    {
        if (s.Random() % 512 == 0)
            if (auto error = s.Audio(0xc, b.data()))
                return error;
        if (s.Random() % 512 == 0)
            return s.Audio(0xd, b.data());
    }
    else if (type == 0xaf)
    {
        if (s.Random() % 256 == 0)
            return s.Audio(0xe, b.data());
    }
    else if (type == 0xbc && s.TileAttributes(tile) == 1)
    {
        if (s.Random() % 128 == 0)
            return s.Audio(0x8a, b.data());
    }
    return {};
}
std::error_code OrdinaryMotion(BirdRecord b, std::int32_t index, std::int32_t tile, IBirdStepServices& s)
{
    const auto type = Read<std::int16_t>(b, Type);
    std::optional<BirdHandler> handler;
    switch (type)
    {
    case 0xad:
    case 0xbc:
        handler = BirdHandler::MoveAdBc;
        break;
    case 0xae:
        handler = BirdHandler::MoveAe;
        break;
    case 0xaf:
        handler = BirdHandler::MoveAf;
        break;
    case 0x10f:
        handler = BirdHandler::Move10f;
        break;
    case 0x173:
        handler = BirdHandler::Move173;
        break;
    default:
        break;
    }
    if (handler)
        if (auto error = s.Call(*handler, b, index).error)
            return error;
    if (auto error = s.Call(BirdHandler::CommonMotion, b, index).error)
        return error;
    auto alive = Read<std::uint8_t>(b, Active);
    auto state = Read<std::int32_t>(b, State);
    auto timer = Read<std::int32_t>(b, Timer);
    auto angles = Read<glm::vec3>(b, Angles);
    // Evita consultas de mapa/tile que el original no alcanza.
    const int map = timer <= 0 ? s.Map() : 0;
    const std::uint16_t attributes = timer <= 0 && map == 7 ? s.TileAttributes(tile) : 0;
    FinishOrdinaryBirdMotion({alive, state, timer, angles}, map, attributes);
    Byte(b, Active, alive);
    Write(b, State, state);
    Write(b, Timer, timer);
    Write(b, Angles, angles);
    return OrdinaryAudio(b, tile, s);
}
std::error_code Update(BirdRecord b, std::int32_t index, std::int32_t tile, IBirdStepServices& s)
{
    const auto type = Read<std::int16_t>(b, Type);
    const auto model = s.BorrowModel(type);
    if (!model.identity || !model.action)
        return std::make_error_code(std::errc::bad_address);
    const float factor = type == 0x171 || type == 0x173 ? 0.5f : 1.0f;
    const bool special = s.Mode() != 0 && type == 0x171;
    if (special)
        if (auto error = s.Call(BirdHandler::SelectAction, b, 7).error)
            return error;
    *model.action = Read<std::uint8_t>(b, Action1);
    if (auto error = s.Pose(model, b, factor))
        return error;
    return special ? SpecialMotion(b, s) : OrdinaryMotion(b, index, tile, s);
}
bool SpawnAllowed(std::int32_t map, std::int32_t tile, IBirdStepServices& s)
{
    if (map == 7)
    {
        const auto attributes = s.TileAttributes(tile);
        return attributes == 0 || attributes == 2;
    }
    return map == 0 || map == 1 || map == 3 || map == 4 || map == 10 || (map >= 11 && map <= 17) || Kalima(map);
}
} // namespace

BirdStepResult StepBirds(std::span<BirdRecord> birds, IBirdStepServices& services)
{
    BirdStepResult result{};
    if (birds.size() < 40)
    {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }
    if (auto error = Prefix(services))
    {
        result.error = error;
        return result;
    }
    const auto& actor = services.ActorPosition();
    const auto y = services.ConvertTileCoordinate(Round53(static_cast<double>(actor.y) * static_cast<double>(0.01f)));
    const auto x = services.ConvertTileCoordinate(Round53(static_cast<double>(actor.x) * static_cast<double>(0.01f)));
    const auto tile = services.TileIndex(x, y);
    auto map = services.Map();
    for (std::size_t i = 0; i < 40; ++i)
    {
        bool gate = true;
        if (Kalima(map))
        {
            gate = services.Random() % 10 == 0;
            if (i >= 2)
                break; // El draw terminal precede al límite,446289..4462aa.
            map = services.Map();
        }
        else if (i >= BirdStepCount(map))
            break;
        ++result.visited;
        const auto bird = birds[i];
        const auto index = static_cast<std::int32_t>(i);
        if (Read<std::uint8_t>(bird, Active) == 0 && gate)
        {
            if (services.Mode() != 0)
                result.error = SpecialBirth(bird, services);
            else if (SpawnAllowed(map, tile, services))
                result.error = OrdinaryBirth(bird, index, map, services);
            if (result.error)
                return result;
            map = services.Map();
        }
        if (Read<std::uint8_t>(bird, Active) == 0)
            continue;
        result.error = Update(bird, index, tile, services);
        if (result.error)
            return result;
        result.error = services.Call(BirdHandler::Finalize, bird, index).error;
        if (result.error)
            return result;
        ++result.finalized;
        map = services.Map();
    }
    return result;
}

} // namespace Client::World
