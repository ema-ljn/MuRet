#include "Scenes/MonsterBodyChain.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/MonsterBodyPasses.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
using Chained = Core::Expected<bool, std::string>;

constexpr std::int32_t FirstKalimaMap = 24;
constexpr std::int32_t LastKalimaMap = 29;
constexpr std::int32_t SeventhKalimaMap = 36;
constexpr std::int32_t CastleMap = 30;
constexpr std::int32_t Map34 = 0x22;
constexpr std::uint32_t BaseFlags = 0x2;
// 0x438fe0: el agua de Kalima, con Water01.. desde el slot 0x41 más el frame de 0x7d2d360.
constexpr std::int32_t WaterTexture = 0x41;
constexpr float PulseRate = std::bit_cast<float>(0x3b03126fu);   // 0,002f (0x6b89bc)
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);       // 0,1 (0x6b87b8)
constexpr float ThreeTenths = std::bit_cast<float>(0x3e99999au); // 0,3 (0x6b89dc)

double D(float value)
{
    return static_cast<double>(value);
}

bool InKalima(std::int32_t map)
{
    return (map >= FirstKalimaMap && map <= LastKalimaMap) || map == SeventhKalimaMap;
}

template <std::size_t N> bool OneOf(std::uint16_t value, const std::array<std::uint16_t, N>& values)
{
    return std::ranges::find(values, value) != values.end();
}

Chained Cut(std::string_view address, std::string_view what, std::uint16_t type)
{
    return Core::Unexpected(std::format("{}: {} (el tipo {:#x}) no está portado", address, what, type));
}

Chained Drawn(const Core::Expected<void, std::string>& drawn)
{
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    return true;
}

// 0x43ed20: los tipos de 0x191 a 0x197 (los de 0x11d a 0x121 y el 0x124 no los da ninguna alta).
Chained DrawTheLateChainCase(MonsterBodyFrame& frame)
{
    const std::uint16_t type = frame.actor.type;
    if (type >= 0x191 && type <= 0x197)
        return Cut("0x43ed20", "el caso de la cadena de 0x6002e0", type);
    return false;
}

// 0x438fe0: el 0x173 de Kalima, el 0xc5 y los tipos bajos de Kalima; si no, 0x43ed20.
Chained DrawTheKalimaCase(MonsterBodyFrame& frame)
{
    MonsterBodyModel& model = frame.model;
    const ActorState& actor = frame.actor;
    const std::uint16_t type = actor.type;
    if (type == 0x173 && InKalima(frame.map))
    {
        // 0x43900a..0x439662: con el color en 0, RenderBody(2); después el color en 1.
        model.object.color = glm::vec3(0.0f);
        const auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor));
        model.object.color = glm::vec3(1.0f);
        return Drawn(drawn);
    }
    if (type == 0xc5)
    {
        // 0x43904c..0x439091: (1; 0,1; 0,1) con el subtipo 1, si no 1; RenderBody(2) sin malla oculta.
        model.object.color = actor.subtype == 1 ? glm::vec3{1.0f, Tenth, Tenth} : glm::vec3(1.0f);
        BodyPassArgs args = ActorPassArgs(actor);
        args.hiddenMesh = -1;
        return Drawn(DrawBodyPass(model, BaseFlags, args));
    }
    if (!InKalima(frame.map) || type >= 0xa0)
        return DrawTheLateChainCase(frame);
    // 0x4394a0..0x4394c4: RenderBody(2).
    if (auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    constexpr std::array<std::uint16_t, 18> Dry{2,    4,    0xc,  0xe,  0xf,  0x12, 0x14, 0x15, 0x1b,
                                                0x1d, 0x1e, 0x1f, 0x20, 0x29, 0x2b, 0x34, 0x36, 0x37};
    if (type <= 0x42 && !OneOf(type, Dry))
    {
        // 0x439570..0x4395ba: el color en sin(T · 0,002f) · 0,1f + 0,3f (FST a los tres) y RenderBody(2) con el alpha
        // en 0,3 y el agua.
        const auto product = Render::Models::Detail::SineProductPC53(D(frame.worldClock) * D(PulseRate), Tenth);
        if (!product)
            return Core::Unexpected<std::string>("0x43957c: el agua de Kalima no cabe en el perfil PC53");
        model.object.color = glm::vec3(static_cast<float>(*product + D(ThreeTenths)));
        BodyPassArgs args = ActorPassArgs(actor);
        args.alpha = ThreeTenths;
        args.texture = WaterTexture + static_cast<std::int32_t>(frame.cloth.waterTextureFrame);
        if (auto drawn = DrawBodyPass(model, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
    }
    // 0x4395bf..0x439662: el 0x22 y el 0xf, el 0x1d y el 0x20 no los da ninguna alta de monstruo.
    if (type == 0x22 || type == 0xf || type == 0x1d || type == 0x20)
        return Cut("0x4395bf", "el pase de después del cuerpo de Kalima de 0x438fe0", type);
    return true;
}

// 0x42cc10: solo en el 30. Los tipos del asedio los dibuja DrawCastleSiegeBody; los de los monstruos, 0x42e3d0.
Chained DrawTheCastleCase(MonsterBodyFrame& frame)
{
    if (frame.map != CastleMap)
        return false;
    MonsterBodyModel& model = frame.model;
    const ActorState& actor = frame.actor;
    DrawnObject object = model.object;
    const BodyPassArgs args = ActorPassArgs(actor);
    object.alpha = args.alpha;
    object.state.blendMesh = static_cast<std::int16_t>(args.blendMesh);
    object.state.light = args.light;
    object.state.uv = args.uv;
    object.state.hiddenMesh = static_cast<std::int16_t>(args.hiddenMesh);
    object.retainedMeshes = model.meshes;
    const auto siege = DrawCastleSiegeBody(model.batch, model.models, object,
                                           CastleSiegeBodyPass{.type = static_cast<std::int16_t>(actor.type),
                                                               .field04 = actor.subtype,
                                                               .siegeActive = frame.siegeActive,
                                                               .worldClock = frame.worldClock},
                                           model.frameClock);
    if (!siege.HasValue())
        return Core::Unexpected(siege.Error());
    if (siege.Value())
        return true;
    constexpr std::array<std::uint16_t, 5> Monsters{0x19b, 0x19c, 0x1a8, 0x1f8, 0x1ff};
    if (OneOf(actor.type, Monsters))
        return Cut("0x42e3d0", "el caso del monstruo del 30 de la cadena de 0x6002e0", actor.type);
    return false;
}

// 0x4403b0 → 0x442e10 (los tipos del 31 no los da ninguna alta).
Chained DrawThe4403b0Case(MonsterBodyFrame& frame)
{
    const std::uint16_t type = frame.actor.type;
    if ((type >= 0x1a3 && type <= 0x1a7) || type == 0x1a9)
        return Cut("0x442e10", "el caso de la cadena de 0x6002e0 (por 0x4403b0)", type);
    return false;
}

// 0x425d90: 0x425070 (el 33 o el 54, sin usar lo que da) y 0x428df0 en todos los mapas.
Chained DrawThe425d90Case(MonsterBodyFrame& frame)
{
    const std::uint16_t type = frame.actor.type;
    if (type == 0x1b7 || type == 0x1b8 || type == 0x1bb)
        return Cut("0x428df0", "el caso de la cadena de 0x6002e0 (por 0x425d90)", type);
    return false;
}

// 0x4306a0: solo en el 34; los tipos del mundo no los da ninguna alta; 0x4339d0 los de los monstruos.
Chained DrawTheMap34Case(MonsterBodyFrame& frame)
{
    if (frame.map != Map34)
        return false;
    const std::uint16_t type = frame.actor.type;
    constexpr std::array<std::uint16_t, 10> Monsters{0x171, 0x17c, 0x17d, 0x17e, 0x1ab,
                                                     0x1ad, 0x1af, 0x1b0, 0x1b1, 0x1b4};
    if (OneOf(type, Monsters))
        return Cut("0x4339d0", "el caso del monstruo del 34 de la cadena de 0x6002e0 (por 0x4306a0)", type);
    return false;
}
} // namespace

Chained DrawTheBodyChain(MonsterBodyFrame& frame)
{
    const auto kalima = DrawTheKalimaCase(frame);
    if (!kalima.HasValue() || kalima.Value())
        return kalima;
    const auto castle = DrawTheCastleCase(frame);
    if (!castle.HasValue() || castle.Value())
        return castle;
    // 0x600f18: 0x4403b0 y 0x425d90 reciben si el cuarto argumento es distinto de 0, y 0x4306a0 el cuarto argumento;
    // 0x42f170 (0x42ee20: el 35) llama a 0x42fa20, que da 0.
    const auto third = DrawThe4403b0Case(frame);
    if (!third.HasValue() || third.Value())
        return third;
    const auto fifth = DrawThe425d90Case(frame);
    if (!fifth.HasValue() || fifth.Value())
        return fifth;
    return DrawTheMap34Case(frame);
}
} // namespace Client::Scenes
