#include "Scenes/MonsterBodyChain.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Data/Models/Model.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/MonsterBodyPasses.h"
#include "World/OriginalFtol.h"

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
    // 0x42e3d0: los monstruos del asedio (solo en el 30, la batalla de Castle Siege): del hito 13 de la CL9.
    constexpr std::array<std::uint16_t, 5> Monsters{0x19b, 0x19c, 0x1a8, 0x1f8, 0x1ff};
    if (OneOf(actor.type, Monsters))
        return Cut("0x42e3d0", "el caso del monstruo del 30 de la cadena de 0x6002e0 (hito 13)", actor.type);
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

// −(ftol(T)) % 10.000 (NEG, CDQ e IDIV: el resto con el signo del dividendo) por `rate` en un float: la U y la V que
// corren en 0x428df0.
Core::Expected<float, std::string> ScrollOf(float worldClock, float rate, std::string_view address)
{
    const auto clock = World::OriginalFtolLow(D(worldClock));
    if (!clock)
        return Core::Unexpected(std::format("{}: el reloj del mundo no cabe en 0x69fd3c", address));
    const auto negated = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(*clock));
    return static_cast<float>(static_cast<double>(negated % 10000) * D(rate));
}

// 0x428df0 (desde 0x425d90, en todos los mapas): el 0x1bb, el 0x1b8 y el 0x1b7. Entre 0x4ecc10 y 0x4ecc20, pases
// sueltos con el +0x88 del modelo en 0 o 1 y la luz por la del objeto; devuelve 1.
Chained DrawTheMeshCase(MonsterBodyFrame& frame)
{
    MonsterBodyModel& model = frame.model;
    const std::uint16_t type = frame.actor.type;
    if (type != 0x1b7 && type != 0x1b8 && type != 0x1bb)
        return false;
    constexpr std::uint32_t RedChrome = 0x46;
    const BodyPassArgs args = ActorPassArgs(frame.actor);
    const auto pass = [&](std::int16_t mesh, std::uint32_t flags, const BodyPassArgs& with)
    { return DrawMeshPass(model, mesh, flags, with); };
    if (type == 0x1bb)
    {
        // 0x428e1b..0x428f2d: la malla 0 con 1 y después con (1, 0, 0) y 0x46, con el +0x88 en 0; la 1 con 1; la 2 con
        // el +0x88 en 1, con 1 y después con (0,5; 0; 0) y 0x46.
        model.object.color = glm::vec3(1.0f);
        model.object.unlitMesh = std::int16_t{0};
        if (auto drawn = pass(0, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        model.object.color = {1.0f, 0.0f, 0.0f};
        if (auto drawn = pass(0, RedChrome, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        model.object.unlitMesh.reset();
        model.object.color = glm::vec3(1.0f);
        if (auto drawn = pass(1, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        model.object.unlitMesh = std::int16_t{1};
        if (auto drawn = pass(2, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        model.object.color = {0.5f, 0.0f, 0.0f};
        const auto drawn = pass(2, RedChrome, args);
        model.object.unlitMesh.reset();
        return Drawn(drawn);
    }
    model.object.color = glm::vec3(std::bit_cast<float>(0x3f666666u));
    if (type == 0x1b8)
    {
        // 0x428f39..0x42903b: con 0,9, las mallas 0 a 2; con el +0x88 en 0, la 3 con la U y la V en el reloj por
        // 0,0006f (0x6b8a50) y la 3 con 0x46.
        for (const std::int16_t mesh : {std::int16_t{0}, std::int16_t{1}, std::int16_t{2}})
        {
            if (auto drawn = pass(mesh, BaseFlags, args); !drawn.HasValue())
                return Core::Unexpected(drawn.Error());
        }
        model.object.unlitMesh = std::int16_t{0};
        const auto scroll = ScrollOf(frame.worldClock, std::bit_cast<float>(0x3a1d4952u), "0x428fcf");
        if (!scroll.HasValue())
            return Core::Unexpected(scroll.Error());
        BodyPassArgs scrolled = args;
        scrolled.uv = {scroll.Value(), scroll.Value()};
        if (auto drawn = pass(3, BaseFlags, scrolled); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        const auto drawn = pass(3, RedChrome, args);
        model.object.unlitMesh.reset();
        return Drawn(drawn);
    }
    // 0x429047..0x429100: el 0x1b7 con 0,9 y el +0x88 en 0: la malla 0 con la U y la V en el reloj por 0,0003f
    // (0x6b8a4c) y con 0x46; con el +0x88 en 0xff, la 1.
    model.object.unlitMesh = std::int16_t{0};
    const auto scroll = ScrollOf(frame.worldClock, std::bit_cast<float>(0x399d4952u), "0x429072");
    if (!scroll.HasValue())
        return Core::Unexpected(scroll.Error());
    BodyPassArgs scrolled = args;
    scrolled.uv = {scroll.Value(), scroll.Value()};
    if (auto drawn = pass(0, BaseFlags, scrolled); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    if (auto drawn = pass(0, RedChrome, args); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    model.object.unlitMesh.reset();
    return Drawn(pass(1, BaseFlags, args));
}

// 0x425d90: 0x425070 (el 33 o el 54, sin usar lo que da) y 0x428df0 en todos los mapas.
Chained DrawThe425d90Case(MonsterBodyFrame& frame)
{
    return DrawTheMeshCase(frame);
}

// 0x4339d0 (desde 0x4306a0, en el 34): cada malla del modelo (+0x24) con 2 y después con `flags` y `texture`.
Chained DrawEveryMeshTwice(MonsterBodyFrame& frame, std::uint32_t flags, std::int32_t texture)
{
    MonsterBodyModel& model = frame.model;
    const auto* found = model.models.Find(frame.actor.type);
    const std::size_t count = found != nullptr ? found->meshes.size() : 0;
    const BodyPassArgs args = ActorPassArgs(frame.actor);
    BodyPassArgs second = args;
    second.texture = texture;
    for (std::size_t mesh = 0; mesh < count; ++mesh)
    {
        if (auto drawn = DrawMeshPass(model, static_cast<std::int16_t>(mesh), BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        if (auto drawn = DrawMeshPass(model, static_cast<std::int16_t>(mesh), flags, second); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
    }
    return true;
}

// 0x433df9 y 0x433b03: RenderBody(2) sin la malla 5.
Chained DrawWithoutMesh5(MonsterBodyFrame& frame)
{
    BodyPassArgs args = ActorPassArgs(frame.actor);
    args.hiddenMesh = 5;
    return Drawn(DrawBodyPass(frame.model, BaseFlags, args));
}

// 0x4306a0: solo en el 34 (0x4303c0); sus tipos del mundo no los da ninguna alta; los de los monstruos, 0x4339d0 con
// su tabla (0x434138 y 0x43410c), que devuelve 1 con todos ellos.
Chained DrawTheMap34Case(MonsterBodyFrame& frame)
{
    if (frame.map != Map34)
        return false;
    MonsterBodyModel& model = frame.model;
    constexpr std::int32_t Chrome01 = 0x5c0;
    constexpr std::uint32_t ChromeFlags = 0x44;
    const float half = 0.5f;
    const glm::vec3 pale{std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3f4ccccdu),
                         std::bit_cast<float>(0x3f19999au)};
    switch (frame.actor.type)
    {
    case 0x1b0:
        // 0x4339fa: con 1, RenderBody(2) sin la malla 5.
        model.object.color = glm::vec3(1.0f);
        return DrawWithoutMesh5(frame);
    case 0x1ab:
        // 0x433a4b: con 1, cada malla con 2 y con 0x240 (el cromo 2, aditivo) y 0x6b1.
        model.object.color = glm::vec3(1.0f);
        return DrawEveryMeshTwice(frame, 0x240, 0x6b1);
    case 0x1ad:
        // 0x433b03: RenderBody(2) sin la malla 5, con el color que trae.
        return DrawWithoutMesh5(frame);
    case 0x1af:
        // 0x433b2f: el acumulador 0x723c14 (+0,1 por dibujo, a 0 pasado 1), la malla 2 con el alpha guardado en 0,3
        // como máximo, +0x64 en −2 y el +0x88 de su modelo en 2, la U (+0x6c) guardada con el reloj y el alpha en 1
        // al final: MuRet no guarda la U de un personaje.
        return Cut("0x433b2f", "el caso del 34 de 0x4339d0 (la U guardada en +0x6c)", frame.actor.type);
    case 0x1b4:
    case 0x1b1:
        // 0x433c75 y 0x433d37: sin el cuarto argumento, 0x433df9; con él, (0,5; 0,5; 0,6) o (0,5; 0,5; 0,8) y cada
        // malla con 2 y con 0x44 y Chrome01.
        if (frame.special == 0)
            return DrawWithoutMesh5(frame);
        model.object.color = {half, half,
                              frame.actor.type == 0x1b4 ? std::bit_cast<float>(0x3f19999au)
                                                        : std::bit_cast<float>(0x3f4ccccdu)};
        return DrawEveryMeshTwice(frame, ChromeFlags, Chrome01);
    case 0x17c:
    case 0x17d:
    case 0x17e:
    case 0x171:
        // 0x433e25, 0x433edc, 0x433f95 y 0x43404c: con (0,6; 0,8; 0,6), cada malla con 2 y con 0x44 y Chrome01.
        model.object.color = pale;
        return DrawEveryMeshTwice(frame, ChromeFlags, Chrome01);
    default:
        return false;
    }
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
