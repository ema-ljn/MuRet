#include "Scenes/MonsterBody.h"

#include "Core/Expected.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/ActorState.h"
#include "Scenes/MonsterBodyChain.h"
#include "Scenes/MonsterBodyPasses.h"
#include "World/OriginalFtol.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;

constexpr std::int32_t FirstBloodCastle = 11;
constexpr std::int32_t LastBloodCastle = 17;
constexpr std::int32_t ChromeTexture = 0x5c0;
constexpr std::uint32_t BaseFlags = 0x2;
constexpr std::uint32_t AdditiveFlags = 0x42;
constexpr std::uint32_t ChromeFlags = 0x44;
constexpr std::uint32_t ShinyFlags = 0x48;
constexpr std::uint8_t DeathAction = 6;
constexpr std::int32_t DeathSound = 0x87;

// Las constantes de 0x6002e0 (EDI es 1,0f desde 0x60043c).
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);       // 0,1
constexpr float ThreeTenths = std::bit_cast<float>(0x3e99999au); // 0,3
constexpr float FourTenths = std::bit_cast<float>(0x3ecccccdu);  // 0,4
constexpr float SixTenths = std::bit_cast<float>(0x3f19999au);   // 0,6
constexpr float EightTenths = std::bit_cast<float>(0x3f4ccccdu); // 0,8
constexpr float PulseRate = std::bit_cast<float>(0x3b03126fu);   // 0,002f (0x6b89bc)
constexpr float Hundredth = std::bit_cast<float>(0x3c23d70au);   // 0,01f (0x6b88d4)

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

bool Within(std::uint16_t value, std::uint16_t first, std::uint16_t last)
{
    return value >= first && value <= last;
}

template <std::size_t N> bool OneOf(std::uint16_t value, const std::array<std::uint16_t, N>& values)
{
    return std::ranges::find(values, value) != values.end();
}

Result Cut(std::string_view address, std::string_view what, std::uint16_t type)
{
    return Core::Unexpected(std::format("{}: {} (el tipo {:#x}) no está portado", address, what, type));
}

BodyPassArgs WithTexture(BodyPassArgs args, std::int32_t texture)
{
    args.texture = texture;
    return args;
}

// FLD T, FMUL rate, FSIN, FMUL swing, FADD base y FSTP: el pulso en un float.
Core::Expected<float, std::string> PulseOf(float worldClock, float swing, float base, std::string_view address)
{
    const auto product = Render::Models::Detail::SineProductPC53(D(worldClock) * D(PulseRate), swing);
    if (!product)
        return Core::Unexpected(std::format("{}: el pulso no cabe en el perfil PC53", address));
    return F32(*product + D(base));
}

// Los casos de 0x6002e0 y su cadena de tipos que no da ninguna alta de monstruo (MonsterKinds: sus modelos son
// objetos del mundo): la dirección del caso, o nada.
std::optional<std::string_view> UnreachableCaseOf(const ActorState& actor, std::int32_t map)
{
    const std::uint16_t type = actor.type;
    const std::int32_t subtype = actor.subtype;
    constexpr std::array<std::uint16_t, 9> Own{0x110, 0x137, 0x136, 0xbf, 0x14a, 0x14b, 0x14c, 0x117, 0xc0};
    if (OneOf(type, Own) || Within(type, 0xa8, 0xac))
        return "0x6004ed";
    const bool byMap = (map == 0 && type == 0x69) ||
                       (map == 4 && (type == 0x17 || type == 0x13 || type == 0x14 || type == 3 || type == 4)) ||
                       (map == 8 && type == 0x51) ||
                       (map >= FirstBloodCastle && map <= LastBloodCastle && (type == 0x1c || type == 0x1d)) ||
                       (map == 3 && type == 0x144) || (map == 0x37 && type == 0x5a);
    if (byMap)
        return "0x600a4a";
    if (type == 0x125 || type == 0x127)
        return "0x438fe0";
    if (Within(type, 0x11d, 0x121) || type == 0x124)
        return "0x43ed20";
    if (map == 0x1f && (type == 10 || type == 0x1b || type == 0x34 || type == 0x36))
        return "0x4403b0";
    if (type == 0x13d || type == 0x13e)
        return "0x442e10";
    constexpr std::array<std::uint16_t, 12> Map34{0x24, 0x29, 0x38, 0x39, 0x47, 0x48,
                                                  0x49, 0x4a, 0x4d, 0x4e, 0x51, 0xfa};
    if (map == 0x22 && OneOf(type, Map34))
        return "0x4306a0";
    constexpr std::array<std::uint16_t, 15> Late{0x18a, 0x10a, 0x10b, 0x12b, 0x12c, 0x12d, 0x12e, 0x12f,
                                                 0x130, 0x199, 0xf9,  0x11b, 0x114, 0x140, 0x12a};
    if (OneOf(type, Late) || (type == 0x112 && subtype == 1) || (type == 0xcc && (subtype == 3 || subtype == 4)) ||
        (type == 0xcb && (subtype == 2 || subtype == 3)) || (type == 0x138 && subtype == 0) || type == 0x139 ||
        (type == 0x109 && subtype == 1))
        return "0x600654";
    if (type == 0x111 || type == 0xed || type == 0xee)
        return "0x60246a";
    return std::nullopt;
}

// 0x600436..0x6004a4: el 0x171 en el 10, el 0x164 en el 9 y el 0x16d con el cuarto argumento.
glm::vec3 EarlyColorOf(const ActorState& actor, std::int32_t map, std::uint16_t special, glm::vec3 color)
{
    if (map == 10 && actor.type == 0x171)
        color = {std::bit_cast<float>(0x3ca3d70au), std::bit_cast<float>(0x3d4ccccdu),
                 std::bit_cast<float>(0x3e19999au)};
    if (map == 9 && actor.type == 0x164)
        color = {0.0f, ThreeTenths, 1.0f};
    if (special != 0 && actor.type == 0x16d)
        color = {0.0f, 0.0f, 1.0f};
    return color;
}

// 0x600e69..0x600ec1: +0x78 & 1 y & 2.
glm::vec3 FlagColorOf(const ActorState& actor, glm::vec3 color)
{
    if ((actor.flags78 & 1u) != 0)
        return (actor.flags78 & 2u) != 0 ? glm::vec3{ThreeTenths, 1.0f, EightTenths}
                                         : glm::vec3{ThreeTenths, 1.0f, 0.5f};
    if ((actor.flags78 & 2u) != 0)
        return {ThreeTenths, 0.5f, 1.0f};
    return color;
}

// 0x6004f7..0x600548: el 0x16c.
Result DrawTheTintedBody(MonsterBodyFrame& frame)
{
    MonsterBodyModel& model = frame.model;
    model.object.color = {FourTenths, SixTenths, 1.0f};
    model.object.unlitMesh = std::int16_t{0};
    if (auto drawn = DrawBodyPass(model, BaseFlags, WithTexture(ActorPassArgs(frame.actor), ChromeTexture));
        !drawn.HasValue())
        return drawn;
    model.object.color = glm::vec3(1.0f);
    model.object.unlitMesh.reset();
    // 0x600b35: RenderBody(2) y RET, sin 0x602365.
    return DrawBodyPass(model, BaseFlags, ActorPassArgs(frame.actor));
}

// Lo que sigue a un caso de después de la cadena: el RenderBody(2) común (0x60235e) o directo 0x602365.
enum class LateNext
{
    Common,
    Tail,
};

using LateResult = Core::Expected<LateNext, std::string>;

LateResult Done(const Result& drawn, LateNext next)
{
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    return next;
}

// Las mallas `meshes` con (2, −1) y después (0x44, Chrome01): el 0x17f (0x600f9f..0x601080) y el 0x186 con el
// 0x12d (0x601471..0x601507).
Result DrawChromedMeshes(MonsterBodyFrame& frame, std::span<const std::int16_t> meshes)
{
    const BodyPassArgs args = ActorPassArgs(frame.actor);
    for (const std::int16_t mesh : meshes)
    {
        if (auto drawn = DrawMeshPass(frame.model, mesh, BaseFlags, args); !drawn.HasValue())
            return drawn;
        if (auto drawn = DrawMeshPass(frame.model, mesh, ChromeFlags, WithTexture(args, ChromeTexture));
            !drawn.HasValue())
            return drawn;
    }
    return {};
}

// 0x601091..0x601159: el 0x17c con el subtipo 1, la luz en sin(T · 0,002f) · 0,01f + 1 (un float) y cuatro mallas
// aditivas: la 0 con 0x6f6 y el blendMesh en 0, la 1 con 0x6f5 y la 2 dos veces, con el blendMesh en 1.
Result DrawTheGlowingMeshes(MonsterBodyFrame& frame)
{
    const auto light = PulseOf(frame.worldClock, Hundredth, 1.0f, "0x6010df");
    if (!light.HasValue())
        return Core::Unexpected(light.Error());
    BodyPassArgs args = ActorPassArgs(frame.actor);
    args.light = light.Value();
    args.blendMesh = 0;
    if (auto drawn = DrawMeshPass(frame.model, 0, AdditiveFlags, WithTexture(args, 0x6f6)); !drawn.HasValue())
        return drawn;
    args.blendMesh = 1;
    if (auto drawn = DrawMeshPass(frame.model, 1, AdditiveFlags, WithTexture(args, 0x6f5)); !drawn.HasValue())
        return drawn;
    if (auto drawn = DrawMeshPass(frame.model, 2, AdditiveFlags, args); !drawn.HasValue())
        return drawn;
    return DrawMeshPass(frame.model, 2, AdditiveFlags, args);
}

// 0x601be5..0x601c08 y 0x601c2d..0x601c44: el 0x4ee770 del 0x18e y del 0x18f, después de su sonido.
Result CutTheDeathMesh(MonsterBodyFrame& frame, bool dies, std::int32_t effect)
{
    if (dies)
        frame.actor.alive = false;
    RequestBodySound(frame, DeathSound);
    return Core::Unexpected(std::format("0x4ee770: los efectos por vértice del tipo {:#x} (0x4ee770(0, {:#x}), CL11) "
                                        "no están portados",
                                        frame.actor.type, effect));
}

// 0x601654..0x601bb1: el 0x18e.
LateResult DrawTheSpiritBody(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    if (actor.animation.action == DeathAction)
        return Core::Unexpected(CutTheDeathMesh(frame, true, 0x10a).Error());
    // 0x60166b..0x601b11: el contador de pasos (0x7d3a0f0) menos +0x188, con signo (JL).
    const std::int32_t steps = ReadLogicSteps(frame);
    if (static_cast<std::int32_t>(static_cast<std::uint32_t>(steps) - static_cast<std::uint32_t>(actor.stepStamp)) <
        0x19)
        return Core::Unexpected(CutTheDeathMesh(frame, false, 0x10a).Error());
    MonsterBodyModel& model = frame.model;
    const BodyPassArgs args = ActorPassArgs(actor);
    if (auto drawn = DrawBodyPass(model, BaseFlags, args); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    model.object.color = glm::vec3(1.0f);
    if (auto drawn = DrawBodyPass(model, ChromeFlags, WithTexture(args, ChromeTexture)); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    model.object.color = {ThreeTenths, ThreeTenths, 1.0f};
    return Done(DrawBodyPass(model, ShinyFlags, WithTexture(args, ChromeTexture)), LateNext::Tail);
}

// 0x600f74..0x60235c: los casos de 0x6002e0 después de su cadena.
LateResult DrawTheLateCase(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    const bool special = frame.special != 0;
    const BodyPassArgs args = ActorPassArgs(actor);
    switch (actor.type)
    {
    case 0x17f:
        if (!special)
            return LateNext::Common;
        model.object.color = glm::vec3(1.0f);
        {
            constexpr std::array<std::int16_t, 3> Meshes{0, 1, 2};
            return Done(DrawChromedMeshes(frame, Meshes), LateNext::Tail);
        }
    case 0x17c:
        if (actor.subtype != 1)
            return LateNext::Common;
        return Done(DrawTheGlowingMeshes(frame), LateNext::Tail);
    case 0x190:
    {
        // 0x60115e..0x6011a4: RenderBody(2) y la malla 2 con el blendMesh en 2 y la luz en 1.
        if (auto drawn = DrawBodyPass(model, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        BodyPassArgs lit = args;
        lit.blendMesh = 2;
        lit.light = 1.0f;
        return Done(DrawMeshPass(model, 2, BaseFlags, lit), LateNext::Tail);
    }
    case 0x183:
        // 0x6011ae..0x6011e5: la malla 0; después, 0x457d60 arma la clase de los bordes (vtable 0x6b8d4c) y 0x457f70
        // la dibuja (0x6011f1..0x601241).
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return Core::Unexpected(
            Cut("0x6011f1", "la clase de los bordes (0x457d60 y 0x457f70) de 0x6002e0", 0x183).Error());
    case 0x188:
    {
        if (!special)
            return LateNext::Common;
        // 0x601264..0x6012c3: con 0,1, RenderBody(2) sin la malla 1; con (1; 0,1; 0,1), la malla 1.
        model.object.color = glm::vec3(Tenth);
        BodyPassArgs hidden = args;
        hidden.hiddenMesh = 1;
        if (auto drawn = DrawBodyPass(model, BaseFlags, hidden); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        model.object.color = {1.0f, Tenth, Tenth};
        return Done(DrawMeshPass(model, 1, BaseFlags, args), LateNext::Tail);
    }
    case 0x180:
        if (!special)
            return LateNext::Common;
        model.object.color = glm::vec3(1.0f);
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, WithTexture(args, 0x62f)); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return Done(DrawMeshPass(model, 2, BaseFlags, WithTexture(args, 0x631)), LateNext::Tail);
    case 0x181:
        if (!special)
            return LateNext::Common;
        model.object.color = glm::vec3(1.0f);
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, WithTexture(args, 0x630)); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return Done(DrawMeshPass(model, 1, BaseFlags, WithTexture(args, 0x62f)), LateNext::Tail);
    case 0x182:
    {
        if (!special)
            return LateNext::Common;
        // 0x6013b4..0x6013c4: RenderBody(2) sin la malla 1, con 0x62e y el color en 1 (0x601436..0x60235e).
        model.object.color = glm::vec3(1.0f);
        BodyPassArgs hidden = WithTexture(args, 0x62e);
        hidden.hiddenMesh = 1;
        return Done(DrawBodyPass(model, BaseFlags, hidden), LateNext::Tail);
    }
    case 0x186:
        if (!special)
        {
            // 0x6013d7..0x60142d: con el +0x88 en 0 y (0,4; 0,3; 0,5), RenderBody(2) con 0x624; después el +0x88 en
            // 0xff, el color en 0,5 y el común (0x601430).
            model.object.unlitMesh = std::int16_t{0};
            model.object.color = {FourTenths, ThreeTenths, 0.5f};
            if (auto drawn = DrawBodyPass(model, BaseFlags, WithTexture(args, 0x624)); !drawn.HasValue())
                return Core::Unexpected(drawn.Error());
            model.object.unlitMesh.reset();
            model.object.color = glm::vec3(0.5f);
            return LateNext::Common;
        }
        if (frame.special == 0x12d)
        {
            model.object.color = glm::vec3(1.0f);
            constexpr std::array<std::int16_t, 2> Meshes{0, 1};
            return Done(DrawChromedMeshes(frame, Meshes), LateNext::Tail);
        }
        // 0x601518: (0,1; 1; 0,8) y el común.
        model.object.color = {Tenth, 1.0f, EightTenths};
        return LateNext::Common;
    case 0x187:
        // 0x60152e..0x601577: con (0,9; 0,8; 1), RenderBody(4) con 0x5c1; el color en 1 y el común.
        model.object.color = {std::bit_cast<float>(0x3f666666u), EightTenths, 1.0f};
        if (auto drawn = DrawBodyPass(model, 0x4, WithTexture(args, 0x5c1)); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        model.object.color = glm::vec3(1.0f);
        return LateNext::Common;
    case 0x185:
    {
        // 0x60157c..0x6015f1: RenderBody(2) sin la malla 1 y la malla 1 dos veces con 0x402 (la onda de 0x4ecc30,
        // 0x4ed775), la textura en +0x58 y el alpha en 0,5 y en +0x174.
        BodyPassArgs hidden = args;
        hidden.hiddenMesh = 1;
        if (auto drawn = DrawBodyPass(model, BaseFlags, hidden); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return Core::Unexpected(
            Cut("0x4ed775", "la onda de 0x4ecc30 (la bandera 0x400) de la malla 1 de 0x6002e0", 0x185).Error());
    }
    case 0x18e:
        return DrawTheSpiritBody(frame);
    case 0x18f:
        if (actor.animation.action == DeathAction)
            return Core::Unexpected(CutTheDeathMesh(frame, true, 0x10c).Error());
        return LateNext::Common;
    default:
        // El 0x198 (0x601d61 → 0x602170) y los demás: el común.
        return LateNext::Common;
    }
}

// 0x602365..0x602466: lo que 0x6002e0 agrega después del cuerpo.
Result DrawTheTail(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    if ((actor.flags78 & 2u) != 0)
    {
        BodyPassArgs frozen = ActorPassArgs(actor);
        frozen.blendMesh = -2;
        frozen.light = 1.0f;
        if (auto drawn = DrawBodyPass(model, BaseFlags, frozen); !drawn.HasValue())
            return drawn;
    }
    if (actor.type == 0x177)
    {
        // 0x602391..0x602431: la luz en sin(T · 0,002f) · 0,3f + 0,5f (un float); la malla 0 con la textura de la
        // malla 6 (IndexTexture[6], +0x38 del modelo) y el blendMesh en 0, y la malla 3 cromada con el blendMesh en 3 y
        // la V en −((ftol(T) % 100) · 0,01f), el resto con el signo del dividendo (CDQ e IDIV).
        const auto light = PulseOf(frame.worldClock, ThreeTenths, 0.5f, "0x6023c4");
        if (!light.HasValue())
            return Core::Unexpected(light.Error());
        const auto textures = model.models.MeshTextures(actor.type);
        if (textures.size() <= 6)
            return Core::Unexpected(
                std::format("0x6023b0: el modelo {:#x} no tiene la textura de la malla 6", actor.type));
        BodyPassArgs args = ActorPassArgs(actor);
        args.light = light.Value();
        args.blendMesh = 0;
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, WithTexture(args, textures[6].value)); !drawn.HasValue())
            return drawn;
        const auto clock = World::OriginalFtolLow(D(frame.worldClock));
        if (!clock)
            return Core::Unexpected<std::string>("0x6023f8: el reloj del mundo no cabe en 0x69fd3c");
        const std::int32_t rest = *clock % 100;
        args.blendMesh = 3;
        args.uv.y = F32(-(static_cast<double>(rest) * D(Hundredth)));
        if (auto drawn = DrawMeshPass(model, 3, ChromeFlags, args); !drawn.HasValue())
            return drawn;
    }
    if (actor.type == 0x1f3 || actor.type == 0x1f4)
    {
        // 0x60243e..0x602465: la malla 0 aditiva con +0x64, +0x68 y la U y la V en 0.
        BodyPassArgs args = ActorPassArgs(actor);
        args.uv = {};
        if (auto drawn = DrawMeshPass(model, 0, AdditiveFlags, args); !drawn.HasValue())
            return drawn;
    }
    return {};
}
} // namespace

Result DrawMonsterBody(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    if (const auto address = UnreachableCaseOf(actor, frame.map))
        return Cut(*address, "el caso de 0x6002e0 de un tipo que no da ninguna alta de monstruo", actor.type);
    model.object.color = EarlyColorOf(actor, frame.map, frame.special, model.object.color);
    // 0x6004aa: con +0x74 en 0x80, RenderBody(0x82) y nada más. El +0x74 no está en ActorState: ninguna alta lo
    // escribe y MuRet lo toma en 0 (*inferido*).
    if (actor.type == 0x16c)
        return DrawTheTintedBody(frame);
    model.object.color = FlagColorOf(actor, model.object.color);
    // 0x600ec4..0x600ef1: con el alpha en 1 y +0x68 en 0,05, el 0x175, el 0x161, el 0x184 y el 0x185 guardan +0x68
    // en 1.
    constexpr std::array<std::uint16_t, 4> Lit{0x175, 0x161, 0x184, 0x185};
    if (OneOf(actor.type, Lit) && std::bit_cast<std::uint32_t>(actor.alpha) == 0x3f800000u &&
        std::bit_cast<std::uint32_t>(actor.blendMeshLight) == 0x3d4ccccdu)
        actor.blendMeshLight = 1.0f;
    const auto chained = DrawTheBodyChain(frame);
    if (!chained.HasValue())
        return Core::Unexpected(chained.Error());
    if (!chained.Value())
    {
        const auto late = DrawTheLateCase(frame);
        if (!late.HasValue())
            return Core::Unexpected(late.Error());
        // 0x60235e: RenderBody(2, alpha, +0x64, +0x68, +0x6c, +0x70, +0x58, −1).
        if (late.Value() == LateNext::Common)
        {
            if (auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
                return drawn;
        }
    }
    return DrawTheTail(frame);
}
} // namespace Client::Scenes
