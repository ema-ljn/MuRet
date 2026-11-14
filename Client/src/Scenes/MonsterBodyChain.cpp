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
#include <initializer_list>
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
constexpr std::uint32_t AdditiveFlags = 0x42;
constexpr std::uint32_t ChromeFlags = 0x44;
constexpr std::uint32_t ShinyFlags = 0x48;
constexpr std::int32_t Chrome01 = 0x5c0;
constexpr std::uint8_t DeathAction = 6;
// 0x60cd74: +0x78 & 0x4000.
constexpr std::uint32_t RiderFlag = 0x4000;
constexpr float PulseRate = std::bit_cast<float>(0x3b03126fu);     // 0,002f (0x6b89bc)
constexpr float SlowPulseRate = std::bit_cast<float>(0x3a9d4952u); // 0,0012f (0x6b8ac4)
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);         // 0,1 (0x6b87b8)
constexpr float TwoTenths = std::bit_cast<float>(0x3e4ccccdu);     // 0,2 (0x6b8854)
constexpr float ThreeTenths = std::bit_cast<float>(0x3e99999au);   // 0,3 (0x6b89dc)
constexpr float FourTenths = std::bit_cast<float>(0x3ecccccdu);    // 0,4 (0x6b87b4)
constexpr float Half = 0.5f;                                       // 0x6b882c
constexpr float SixTenths = std::bit_cast<float>(0x3f19999au);     // 0,6 (0x6b8a20)
constexpr float SevenTenths = std::bit_cast<float>(0x3f333333u);   // 0,7 (0x6b8a5c)
constexpr float EightTenths = std::bit_cast<float>(0x3f4ccccdu);   // 0,8 (0x6b8a38)
constexpr float Ceiling = std::bit_cast<float>(0x3fa66666u);       // 1,3 (0x6b8ac0)

using Result = Core::Expected<void, std::string>;

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

BodyPassArgs WithTexture(BodyPassArgs args, std::int32_t texture)
{
    args.texture = texture;
    return args;
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

// 0x43ed4a..0x43ed6c y sus copias del 0x193 al 0x197: al morir (+0x111 en 6) con el alpha por debajo de 0,5 (FCOMP con
// C0: también un NaN), 0x4ee770(0, 0x607), los efectos por vértice de la CL11.
Result CutWhenFading(const ActorState& actor, std::string_view address)
{
    if (actor.animation.action == DeathAction && !(actor.alpha >= Half))
        return Core::Unexpected(std::format("{}: los efectos por vértice del tipo {:#x} al morir (0x4ee770(0, 0x607), "
                                            "CL11) no están portados",
                                            address, actor.type));
    return {};
}

// Los de 0x4ecc30 en 0x43ed20: los del personaje con la textura en +0x58.
BodyPassArgs MeshArgsOf(const ActorState& actor)
{
    return WithTexture(ActorPassArgs(actor), actor.dword58);
}

Result DrawMeshes(MonsterBodyModel& model, std::initializer_list<std::int16_t> meshes, std::uint32_t flags,
                  const BodyPassArgs& args)
{
    for (const std::int16_t mesh : meshes)
    {
        if (auto drawn = DrawMeshPass(model, mesh, flags, args); !drawn.HasValue())
            return drawn;
    }
    return {};
}

// sin(argumento) · factor + base en la x87 (PC53), sin redondear a un float.
Core::Expected<double, std::string> WaveOf(double argument, float factor, float base, std::string_view address)
{
    const auto product = Render::Models::Detail::SineProductPC53(argument, factor);
    if (!product)
        return Core::Unexpected(std::format("{}: el seno del reloj no cabe en el perfil PC53", address));
    return *product + D(base);
}

// 0x7d325c8 en (rand() % 8 − 8) · 0,1f (AND 0x80000007 con el ajuste del signo: el resto con signo).
Result Shake(MonsterBodyFrame& frame, std::string_view address)
{
    const std::int32_t step = (frame.random.Next() % 8) - 8;
    if (frame.cameraShake == nullptr)
        return Core::Unexpected(std::format("{}: falta el temblor de la cámara (0x7d325c8)", address));
    *frame.cameraShake = F32(static_cast<double>(step) * D(Tenth));
    return {};
}

// 0x439d90 con un monstruo (el tipo no es el 0x20a): con +0x110 en 0x21 o 0x22, 0x4ee770(6, 0x10a, 1 o 2) (CL11) y
// después +0x110 en 0 y el sonido 0x88. MuRet solo pone 0 y 1 en +0x110 (las puertas del 30, MonsterStepCases.h).
Result CutTheStruckGlow(const ActorState& actor)
{
    if (actor.byte110 == 0x21 || actor.byte110 == 0x22)
        return Core::Unexpected(std::format("0x439dc5: los efectos por vértice del tipo {:#x} con +0x110 en {:#x} "
                                            "(0x4ee770(6, 0x10a), CL11) no están portados",
                                            actor.type, actor.byte110));
    return {};
}

// 0x43edc1..0x43f493: el 0x192 (Kundun; +0x60 en 100 desde su alta, 0x43a155).
Result DrawKundun(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    const float frameNumber = actor.animation.frame;
    const bool dying = actor.animation.action == DeathAction;
    constexpr std::int32_t Fallen = 0x5a;
    if (dying && frameNumber > std::bit_cast<float>(0x416ccccdu) && actor.dword60 == Fallen)
    {
        // 0x43edff..0x43ee38: el sonido 0x1dd, +0x60 en 10 y la malla 0 con 2 y la textura +0x58; 0x43ee3d..0x43ee7e:
        // el temblor; después 0x4ec110 sobre el hueso 0xc0/0x30 y las partículas 0x124, 0x11d y 0x11e de 0x518cf0.
        RequestBodySound(frame, 0x1dd);
        actor.dword60 = 10;
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, MeshArgsOf(actor)); !drawn.HasValue())
            return drawn;
        if (auto shaken = Shake(frame, "0x43ee7e"); !shaken.HasValue())
            return shaken;
        return Core::Unexpected(std::string("0x43eeb9: las partículas de Kundun al caer (0x518cf0(0x124), CL11) no "
                                            "están portadas"));
    }
    if (dying && frameNumber >= 8.0f && actor.dword60 >= 100)
    {
        // 0x43f182: +0x60 no es 0x5a, el sonido 0x1de; 0x43f195: +0x60 en 0x5a; 0x43f1a6..0x43f243: 0x56 vueltas de
        // rand() % 10 con los joints 0x613 y 0x6ee de 0x530860 (CL11): corta en el primero, después de su rand().
        RequestBodySound(frame, 0x1de);
        actor.dword60 = Fallen;
        static_cast<void>(frame.random.Next());
        return Core::Unexpected(std::string("0x43f20d: los joints de Kundun al morir (0x530860(0x613), CL11) no están "
                                            "portados"));
    }
    if (actor.dword60 < Fallen)
        return {};
    // 0x43f279..0x43f395: el alpha guardado en 1, RenderBody(2) y (0,4; 0,4; 0,3) con las mallas 1 a 6 cromadas.
    actor.alpha = 1.0f;
    if (auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
        return drawn;
    model.object.color = {FourTenths, FourTenths, ThreeTenths};
    const BodyPassArgs args = MeshArgsOf(actor);
    if (auto drawn = DrawMeshes(model, {1, 2, 3, 4, 5, 6}, ChromeFlags, args); !drawn.HasValue())
        return drawn;
    // 0x43f39a..0x43f48e: en la acción 5 con el frame en (3,5; 6) o en (8; 11), las mallas 1, 2, 3 y 5 aditivas.
    const bool swinging = (frameNumber > 3.5f && frameNumber < 6.0f) || (frameNumber > 8.0f && frameNumber < 11.0f);
    if (actor.animation.action == 5 && swinging)
        return DrawMeshes(model, {1, 2, 3, 5}, AdditiveFlags, args);
    return {};
}

// 0x43f8f9..0x43fb99: el 0x197, entre 0x4ecc10 y 0x4ecc20.
Result DrawThe197(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    const bool dark = actor.subtype == 9;
    // 0x43f945..0x43f9d0: las mallas 0, 1, 3 y 5 con 2, sin el blendMesh.
    BodyPassArgs args = MeshArgsOf(actor);
    args.blendMesh = -1;
    if (auto drawn = DrawMeshes(model, {0, 1, 3, 5}, BaseFlags, args); !drawn.HasValue())
        return drawn;
    // 0x43f9d5..0x43fa23: v = sin(T · 0,002f) · 0,3f + 0,7f; con el subtipo 9, +0x68 en 0,5 y (v; v · 0,4; v · 0,4);
    // si no, +0x68 en 1 y (v · 0,3; v · 0,6; v).
    const auto wave = WaveOf(D(frame.worldClock) * D(PulseRate), ThreeTenths, SevenTenths, "0x43f9e5");
    if (!wave.HasValue())
        return Core::Unexpected(wave.Error());
    const double v = wave.Value();
    if (dark)
    {
        actor.blendMeshLight = Half;
        const float dim = F32(v * D(FourTenths));
        model.object.color = {F32(v), dim, dim};
    }
    else
    {
        actor.blendMeshLight = 1.0f;
        model.object.color = {F32(v * D(ThreeTenths)), F32(v * D(SixTenths)), F32(v)};
    }
    // 0x43fa26..0x43fa6f: las mallas 2 y 4 con 0x440 (la onda de 0x4ecc30) y su blendMesh.
    return Core::Unexpected(std::string("0x4ed775: la onda de 0x4ecc30 (la bandera 0x400) de las mallas 2 y 4 del tipo "
                                        "0x197 no está portada"));
}

// 0x43ed20 con los tipos de 0x191 a 0x197, hasta 0x43fc04.
Result DrawTheLateChainBody(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    switch (actor.type)
    {
    case 0x191:
    {
        if (auto cut = CutWhenFading(actor, "0x43ed6c"); !cut.HasValue())
            return cut;
        // 0x43ed71..0x43edb7: RenderBody(2) sin la malla oculta y la malla 0 con 0x48 y Chrome01.
        BodyPassArgs args = ActorPassArgs(actor);
        args.hiddenMesh = -1;
        if (auto drawn = DrawBodyPass(model, BaseFlags, args); !drawn.HasValue())
            return drawn;
        return DrawMeshPass(model, 0, ShinyFlags, WithTexture(ActorPassArgs(actor), Chrome01));
    }
    case 0x192:
        return DrawKundun(frame);
    case 0x193:
        if (auto cut = CutWhenFading(actor, "0x43f514"); !cut.HasValue())
            return cut;
        // 0x43f519..0x43f575: RenderBody(2) y (0,5; 0,1; 0) con la malla 0 cromada.
        if (auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
            return drawn;
        model.object.color = {Half, Tenth, 0.0f};
        return DrawMeshPass(model, 0, ChromeFlags, MeshArgsOf(actor));
    case 0x194:
    {
        if (auto cut = CutWhenFading(actor, "0x43f5b9"); !cut.HasValue())
            return cut;
        // 0x43f5be..0x43f63e: RenderBody(2) sin la malla oculta y (0,25; 0,2; 0,1) con las mallas 0 y 2 cromadas.
        BodyPassArgs args = ActorPassArgs(actor);
        args.hiddenMesh = -1;
        if (auto drawn = DrawBodyPass(model, BaseFlags, args); !drawn.HasValue())
            return drawn;
        model.object.color = {0.25f, TwoTenths, Tenth};
        return DrawMeshes(model, {0, 2}, ChromeFlags, MeshArgsOf(actor));
    }
    case 0x195:
    {
        if (auto cut = CutWhenFading(actor, "0x43f682"); !cut.HasValue())
            return cut;
        // 0x43f687..0x43f738: con el subtipo 9, (0,3; 0,1; 0,1); la malla 0 aditiva y las 1 a 3 con 2.
        const bool dark = actor.subtype == 9;
        if (dark)
            model.object.color = {ThreeTenths, Tenth, Tenth};
        const BodyPassArgs args = MeshArgsOf(actor);
        if (auto drawn = DrawMeshPass(model, 0, AdditiveFlags, args); !drawn.HasValue())
            return drawn;
        if (auto drawn = DrawMeshes(model, {1, 2, 3}, BaseFlags, args); !drawn.HasValue())
            return drawn;
        if (dark)
        {
            // 0x43f746..0x43f7bc: (1; 0,6; 0,3) con la malla 1 cromada al alpha 0,5, con 1 la 1 con 0x240 y después
            // (1; 0,6; 0,1).
            model.object.color = {1.0f, SixTenths, ThreeTenths};
            BodyPassArgs faded = args;
            faded.alpha = Half;
            if (auto drawn = DrawMeshPass(model, 1, ChromeFlags, faded); !drawn.HasValue())
                return drawn;
            model.object.color = glm::vec3(1.0f);
            if (auto drawn = DrawMeshPass(model, 1, 0x240, args); !drawn.HasValue())
                return drawn;
            model.object.color = {1.0f, SixTenths, Tenth};
        }
        else
        {
            // 0x43f7ca: (0,1; 0,6; 1).
            model.object.color = {Tenth, SixTenths, 1.0f};
        }
        // 0x43f7df..0x43f836: las mallas 0 y 2 cromadas y, con el subtipo 9, 0x439d90.
        if (auto drawn = DrawMeshes(model, {0, 2}, ChromeFlags, args); !drawn.HasValue())
            return drawn;
        return dark ? CutTheStruckGlow(actor) : Result{};
    }
    case 0x196:
    {
        if (auto cut = CutWhenFading(actor, "0x43f87d"); !cut.HasValue())
            return cut;
        // 0x43f882..0x43f8ef: RenderBody(2) y (0,5; sin(T · 0,002f) · 0,3f + 0,7f; 0,5) con la malla 0 cromada al
        // alpha 0,8.
        if (auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
            return drawn;
        const auto wave = WaveOf(D(frame.worldClock) * D(PulseRate), ThreeTenths, SevenTenths, "0x43f8bf");
        if (!wave.HasValue())
            return Core::Unexpected(wave.Error());
        model.object.color = {Half, F32(wave.Value()), Half};
        BodyPassArgs args = MeshArgsOf(actor);
        args.alpha = EightTenths;
        return DrawMeshPass(model, 0, ChromeFlags, args);
    }
    default:
        if (auto cut = CutWhenFading(actor, "0x43f933"); !cut.HasValue())
            return cut;
        return DrawThe197(frame);
    }
}

// 0x60cd60(modelo, objeto, `flags`, 1, `scale`) con un monstruo o un NPC: sin +0x78 & 0x4000, el +0x44 del modelo en
// 0 (sin la luz por vértice), 0x4ebc80 sobre los huesos propios con 0x6c881c en `scale`, que deja esas mallas en
// 0x5786f9c para lo que sigue de 0x6002e0, y RenderBody(flags) con lo del personaje. El 0xc5 (0x60cdd9) no llega acá.
Result DrawTheWiderShell(MonsterBodyFrame& frame, std::uint32_t flags, float scale)
{
    if ((frame.actor.flags78 & RiderFlag) == RiderFlag)
        return {};
    MonsterBodyModel& model = frame.model;
    model.object.vertexLight = false;
    model.rescaled = SkinObjectMeshes(model.models, model.object, frame.bones, scale);
    model.meshes = model.rescaled;
    return DrawBodyPass(model, flags, ActorPassArgs(frame.actor));
}

// 0x43ed20: los tipos de 0x191 a 0x197 (los de 0x11d a 0x121 y el 0x124 no los da ninguna alta). Después de cada caso,
// 0x43fc04: con +0x78 & 0x100 o & 8, v = sin(T · 0,001f) · 0,2f + 0,5f en la x87, el color (v · 0,1; v · 0,3; v · 0,6)
// o (v · 0,1; v · 0,6; v · 0,3) y 0x60cd60(…, 0x44, 1, 1,3f). Devuelve 1.
Chained DrawTheLateChainCase(MonsterBodyFrame& frame)
{
    const std::uint16_t type = frame.actor.type;
    if (type < 0x191 || type > 0x197)
        return false;
    if (auto drawn = DrawTheLateChainBody(frame); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    const std::uint32_t flags = frame.actor.flags78;
    const bool blue = (flags & 0x100u) != 0;
    if (!blue && (flags & 8u) == 0)
        return true;
    const auto wave = WaveOf(D(frame.worldClock) * D(std::bit_cast<float>(0x3a83126fu)), TwoTenths, Half, "0x43fc36");
    if (!wave.HasValue())
        return Core::Unexpected(wave.Error());
    const double v = wave.Value();
    const float third = F32(v * D(ThreeTenths));
    const float sixth = F32(v * D(SixTenths));
    frame.model.object.color = {F32(v * D(Tenth)), blue ? third : sixth, blue ? sixth : third};
    return Drawn(DrawTheWiderShell(frame, ChromeFlags, std::bit_cast<float>(0x3fa66666u)));
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

// La f de 0x442ee9..0x442f24 y 0x4430c9..0x443104: min(sin(T · 0,0012f) · 0,8f + 1,3f; 1,3f) en la x87 (FCOM y
// TEST 0x41: con un NaN queda).
Core::Expected<double, std::string> GlowOf(float worldClock, std::string_view address)
{
    const auto wave = WaveOf(D(worldClock) * D(SlowPulseRate), EightTenths, Ceiling, address);
    if (!wave.HasValue())
        return wave;
    return wave.Value() > D(Ceiling) ? D(Ceiling) : wave.Value();
}

// (ftol(T) % 10.000) · 0,0002f en un float (CDQ e IDIV: el resto con el signo del dividendo): la U y la V de 0x442e10.
Core::Expected<float, std::string> DriftOf(float worldClock, std::string_view address)
{
    const auto clock = World::OriginalFtolLow(D(worldClock));
    if (!clock)
        return Core::Unexpected(std::format("{}: el reloj del mundo no cabe en 0x69fd3c", address));
    return F32(static_cast<double>(*clock % 10000) * D(std::bit_cast<float>(0x3951b717u)));
}

// 0x442eda..0x442fe4: el 0x1a4, sin RenderBody.
Result DrawThe1a4(MonsterBodyFrame& frame)
{
    MonsterBodyModel& model = frame.model;
    const BodyPassArgs args = ActorPassArgs(frame.actor);
    const auto glow = GlowOf(frame.worldClock, "0x442f01");
    if (!glow.HasValue())
        return Core::Unexpected(glow.Error());
    const auto drift = DriftOf(frame.worldClock, "0x442f4d");
    if (!drift.HasValue())
        return Core::Unexpected(drift.Error());
    // Con el +0x88 en 0 y el color por f, la malla 0 con 2 y la U y la V corridas y después aditiva; con el +0x88 en
    // 0xff y el color guardado, la 1 con 2.
    const glm::vec3 saved = model.object.color;
    const double f = glow.Value();
    model.object.unlitMesh = std::int16_t{0};
    model.object.color = {F32(f * D(saved.r)), F32(f * D(saved.g)), F32(f * D(saved.b))};
    BodyPassArgs drifting = args;
    drifting.uv = {drift.Value(), drift.Value()};
    if (auto drawn = DrawMeshPass(model, 0, BaseFlags, drifting); !drawn.HasValue())
        return drawn;
    if (auto drawn = DrawMeshPass(model, 0, AdditiveFlags, args); !drawn.HasValue())
        return drawn;
    model.object.unlitMesh.reset();
    model.object.color = saved;
    return DrawMeshPass(model, 1, BaseFlags, args);
}

// 0x4430a9..0x4432a2: el 0x1a6; con el cuarto argumento, el color en 1 y los pases cromados con Chrome01.
Result DrawThe1a6(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    const bool special = frame.special != 0;
    const BodyPassArgs args = ActorPassArgs(actor);
    const BodyPassArgs chromed = WithTexture(args, Chrome01);
    if (special)
        model.object.color = glm::vec3(1.0f);
    const auto glow = GlowOf(frame.worldClock, "0x4430e1");
    if (!glow.HasValue())
        return Core::Unexpected(glow.Error());
    // 0x44310a..0x44312e: con el +0x88 en 1, el rojo por f · 0,1 (en la x87), el verde por f y el azul por f · 0,1
    // guardado en un float.
    const glm::vec3 saved = model.object.color;
    const double f = glow.Value();
    const double tenth = f * D(Tenth);
    model.object.unlitMesh = std::int16_t{1};
    model.object.color = {F32(tenth * D(saved.r)), F32(f * D(saved.g)), F32(D(F32(tenth)) * D(saved.b))};
    const auto drift = DriftOf(frame.worldClock, "0x443137");
    if (!drift.HasValue())
        return Core::Unexpected(drift.Error());
    BodyPassArgs drifting = args;
    drifting.uv = {drift.Value(), drift.Value()};
    if (auto drawn = DrawMeshPass(model, 1, BaseFlags, drifting); !drawn.HasValue())
        return drawn;
    const auto second =
        special ? DrawMeshPass(model, 1, ChromeFlags, chromed) : DrawMeshPass(model, 1, AdditiveFlags, args);
    if (!second.HasValue())
        return second;
    // 0x4431c8..0x44320e: con el +0x88 en 0xff y el color guardado, la malla 0 con 2 al alpha sin(T · 0,0012f) · 0,2f +
    // 0,8f (un float).
    model.object.unlitMesh.reset();
    model.object.color = saved;
    const auto fade = WaveOf(D(frame.worldClock) * D(SlowPulseRate), TwoTenths, EightTenths, "0x4431f0");
    if (!fade.HasValue())
        return Core::Unexpected(fade.Error());
    BodyPassArgs faded = args;
    faded.alpha = F32(fade.Value());
    if (auto drawn = DrawMeshPass(model, 0, BaseFlags, faded); !drawn.HasValue())
        return drawn;
    if (special)
    {
        if (auto drawn = DrawMeshPass(model, 0, ChromeFlags, chromed); !drawn.HasValue())
            return drawn;
    }
    // 0x44323e..0x44329b: con el +0x88 en 2, la malla 2 con el blendMesh en 2 y la luz en 0,5; con el cuarto argumento,
    // cromada, y +0xc en el +0x68 del modelo (la escala que 0x5ffdf0 le copió del mismo +0xc: no cambia).
    model.object.unlitMesh = std::int16_t{2};
    BodyPassArgs lit = args;
    lit.blendMesh = 2;
    lit.light = Half;
    if (auto drawn = DrawMeshPass(model, 2, BaseFlags, lit); !drawn.HasValue())
        return drawn;
    if (special)
    {
        if (auto drawn = DrawMeshPass(model, 2, ChromeFlags, chromed); !drawn.HasValue())
            return drawn;
    }
    model.object.unlitMesh.reset();
    return {};
}

// 0x4432b0..0x4433d0: el 0x1a7; con el cuarto argumento, el color en 1 y los pases cromados con Chrome01.
Result DrawThe1a7(MonsterBodyFrame& frame)
{
    MonsterBodyModel& model = frame.model;
    const bool special = frame.special != 0;
    const BodyPassArgs args = ActorPassArgs(frame.actor);
    const BodyPassArgs chromed = WithTexture(args, Chrome01);
    if (special)
        model.object.color = glm::vec3(1.0f);
    // 0x4432d0..0x443353: el color por (0,5; 0,6; 0,8) y la malla 0; 0x443358..0x4433b7: (0,5; 0,8; 0,6) y la 1.
    const glm::vec3 saved = model.object.color;
    model.object.color = {F32(D(saved.r) * D(Half)), F32(D(saved.g) * D(SixTenths)), F32(D(saved.b) * D(EightTenths))};
    for (const std::int16_t mesh : {std::int16_t{0}, std::int16_t{1}})
    {
        if (mesh == 1)
            model.object.color = {Half, EightTenths, SixTenths};
        if (auto drawn = DrawMeshPass(model, mesh, BaseFlags, args); !drawn.HasValue())
            return drawn;
        if (special)
        {
            if (auto drawn = DrawMeshPass(model, mesh, ChromeFlags, chromed); !drawn.HasValue())
                return drawn;
        }
    }
    model.object.color = saved;
    return {};
}

// 0x4433de..0x443571: el 0x1a9.
Result DrawThe1a9(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    // RenderBody(2) sin la malla 5; el alpha guardado en 1 y la malla 5 con 2 y la textura 0x63b y con la suya, sin el
    // blendMesh.
    BodyPassArgs body = ActorPassArgs(actor);
    body.hiddenMesh = 5;
    if (auto drawn = DrawBodyPass(model, BaseFlags, body); !drawn.HasValue())
        return drawn;
    actor.alpha = 1.0f;
    BodyPassArgs loose = ActorPassArgs(actor);
    loose.blendMesh = -1;
    if (auto drawn = DrawMeshPass(model, 5, BaseFlags, WithTexture(loose, 0x63b)); !drawn.HasValue())
        return drawn;
    if (auto drawn = DrawMeshPass(model, 5, BaseFlags, loose); !drawn.HasValue())
        return drawn;
    // 0x443459..0x443519: (1; 0,5; 0) con la malla 4 cromada; (0,75; 0,65; 0,5) con las 0, 1 y 3.
    const glm::vec3 saved = model.object.color;
    const BodyPassArgs args = ActorPassArgs(actor);
    model.object.color = {1.0f, Half, 0.0f};
    if (auto drawn = DrawMeshPass(model, 4, ChromeFlags, args); !drawn.HasValue())
        return drawn;
    model.object.color = {0.75f, std::bit_cast<float>(0x3f266666u), Half};
    if (auto drawn = DrawMeshes(model, {0, 1, 3}, ChromeFlags, args); !drawn.HasValue())
        return drawn;
    // 0x44351e..0x443558: la malla 0 con 2, el blendMesh en 0, la luz en sin(T · 0,0012f) · 0,3f + 0,6f (un float) y la
    // textura 0x63a; después el color guardado.
    const auto light = WaveOf(D(frame.worldClock) * D(SlowPulseRate), ThreeTenths, SixTenths, "0x44353b");
    if (!light.HasValue())
        return Core::Unexpected(light.Error());
    BodyPassArgs lit = WithTexture(args, 0x63a);
    lit.blendMesh = 0;
    lit.light = F32(light.Value());
    if (auto drawn = DrawMeshPass(model, 0, BaseFlags, lit); !drawn.HasValue())
        return drawn;
    model.object.color = saved;
    return {};
}

// 0x4403b0 → 0x442e10, en todos los mapas (los tipos del 31 de 0x4403b0 no los da ninguna alta): los monstruos de
// 0x440530, entre 0x4ecc10 y 0x4ecc20; devuelve 1.
Chained DrawThe4403b0Case(MonsterBodyFrame& frame)
{
    MonsterBodyModel& model = frame.model;
    const ActorState& actor = frame.actor;
    switch (actor.type)
    {
    case 0x1a3:
    {
        // 0x442e3d..0x442ecc: RenderBody(2) sin la malla 1 y, con (0,6; 0,4; 0,4), la 1; después el color guardado.
        BodyPassArgs body = ActorPassArgs(actor);
        body.hiddenMesh = 1;
        if (auto drawn = DrawBodyPass(model, BaseFlags, body); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        const glm::vec3 saved = model.object.color;
        model.object.color = {SixTenths, FourTenths, FourTenths};
        const auto drawn = DrawMeshPass(model, 1, BaseFlags, ActorPassArgs(actor));
        model.object.color = saved;
        return Drawn(drawn);
    }
    case 0x1a4:
        return Drawn(DrawThe1a4(frame));
    case 0x1a5:
    {
        // 0x442fe9..0x443095: RenderBody(2) sin la malla 0; con 1, la 0 sin el blendMesh y con la luz en 1, y con
        // (0,8; 0,6; 1), cromada; después el color guardado.
        BodyPassArgs body = ActorPassArgs(actor);
        body.hiddenMesh = 0;
        if (auto drawn = DrawBodyPass(model, BaseFlags, body); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        const glm::vec3 saved = model.object.color;
        BodyPassArgs lit = ActorPassArgs(actor);
        lit.blendMesh = -1;
        lit.light = 1.0f;
        model.object.color = glm::vec3(1.0f);
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, lit); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        model.object.color = {EightTenths, SixTenths, 1.0f};
        const auto drawn = DrawMeshPass(model, 0, ChromeFlags, lit);
        model.object.color = saved;
        return Drawn(drawn);
    }
    case 0x1a6:
        return Drawn(DrawThe1a6(frame));
    case 0x1a7:
        return Drawn(DrawThe1a7(frame));
    case 0x1a9:
        return Drawn(DrawThe1a9(frame));
    default:
        return false;
    }
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

// 0x433b2f..0x433c6e: el 0x1af, entre 0x4ecc10 y 0x4ecc20. El acumulador 0x723c14 (+0,1f por dibujo y a 0 pasado 1)
// no lo lee nadie fuera de su propia cuenta: MuRet no lo guarda. Cada malla del modelo (+0x24, con signo) con 2, la U
// guardada en +0x6c en (ftol(T) % 10.000) · 0,0005f (0x6b8a68) y +0x64 en −1; antes de la 2, el alpha guardado en 0,3
// como máximo, +0x64 en −2 y el +0x88 del modelo en 2, que nadie vuelve a 0xff: lo ve 0x602365 en este dibujo, y en
// los siguientes solo el contorno de 0x5ffdf0 (sin luz ni textura) y la malla 2 de acá, que lo vuelve a poner. Al
// final, el alpha guardado en 1.
Result DrawThe1af(MonsterBodyFrame& frame)
{
    ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    const auto* found = model.models.Find(actor.type);
    const std::size_t count = found != nullptr ? found->meshes.size() : 0;
    for (std::size_t mesh = 0; mesh < count; ++mesh)
    {
        if (mesh == 2)
        {
            if (actor.alpha > ThreeTenths)
                actor.alpha = ThreeTenths;
            actor.dword64 = -2;
            model.object.unlitMesh = std::int16_t{2};
        }
        else
        {
            actor.dword64 = -1;
        }
        const auto clock = World::OriginalFtolLow(D(frame.worldClock));
        if (!clock)
            return Core::Unexpected<std::string>("0x433bf7: el reloj del mundo no cabe en 0x69fd3c");
        actor.textureU = F32(static_cast<double>(*clock % 10000) * D(std::bit_cast<float>(0x3a03126fu)));
        if (auto drawn = DrawMeshPass(model, static_cast<std::int16_t>(mesh), BaseFlags, ActorPassArgs(actor));
            !drawn.HasValue())
            return drawn;
    }
    actor.alpha = 1.0f;
    return {};
}

// 0x4306a0: solo en el 34 (0x4303c0); sus tipos del mundo no los da ninguna alta; los de los monstruos, 0x4339d0 con
// su tabla (0x434138 y 0x43410c), que devuelve 1 con todos ellos.
Chained DrawTheMap34Case(MonsterBodyFrame& frame)
{
    if (frame.map != Map34)
        return false;
    MonsterBodyModel& model = frame.model;
    const float half = Half;
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
        return Drawn(DrawThe1af(frame));
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
