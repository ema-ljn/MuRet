#include "Monsters/Drawing/MonsterBody.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelTextures.h"
#include "Data/Textures/TextureSet.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Monsters/Drawing/MonsterBodyChain.h"
#include "Monsters/Drawing/MonsterBodyPasses.h"
#include "Actors/Drawing/VertexEffects.h"
#include "Math/Ftol.h"
#include "Audio/SoundId.h"

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

namespace Client::Monsters::Drawing
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
constexpr Audio::SoundId DeathSound{0x87};
// The death's per-vertex effects: stones 0x10a of type 0x18e (0x601c01) and 0x10c of type 0x18f (0x601c3d).
constexpr std::int32_t StoneEffects = 0x10a;
constexpr std::int32_t GateStoneEffects = 0x10c;

// The constants of 0x6002e0 (EDI is 1.0f from 0x60043c on).
using Math::Tenth;
constexpr float ThreeTenths = std::bit_cast<float>(0x3e99999au);
constexpr float FourTenths = std::bit_cast<float>(0x3ecccccdu);
constexpr float SixTenths = std::bit_cast<float>(0x3f19999au);
constexpr float EightTenths = std::bit_cast<float>(0x3f4ccccdu);
constexpr float PulseRate = std::bit_cast<float>(0x3b03126fu); // 0.002f (0x6b89bc)
using Math::Hundredth;                                         // 0x6b88d4

using Math::D;
using Math::F32;

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

// FLD T, FMUL rate, FSIN, FMUL swing, FADD base and FSTP: the pulse in a float.
Core::Expected<float, std::string> PulseOf(float worldClock, float swing, float base, std::string_view address)
{
    const auto product = Math::Detail::SineProductPC53(D(worldClock) * D(PulseRate), swing);
    if (!product)
        return Core::Unexpected(std::format("{}: el pulso no cabe en el perfil PC53", address));
    return F32(*product + D(base));
}

// The cases of 0x6002e0 and its chain for types no monster spawn gives (MonsterKinds: their models are world objects):
// the case's address, or nothing.
std::optional<std::string_view> UnreachableCaseOf(const Kernel::ActorState& actor, std::int32_t map)
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
    // No spawn gives type 0x18a, but +0x376 at 0x4d draws it with the type + 1 (0x50c244): it has its case.
    constexpr std::array<std::uint16_t, 14> Late{0x10a, 0x10b, 0x12b, 0x12c, 0x12d, 0x12e, 0x12f,
                                                 0x130, 0x199, 0xf9,  0x11b, 0x114, 0x140, 0x12a};
    if (OneOf(type, Late) || (type == 0x112 && subtype == 1) || (type == 0xcc && (subtype == 3 || subtype == 4)) ||
        (type == 0xcb && (subtype == 2 || subtype == 3)) || (type == 0x138 && subtype == 0) || type == 0x139 ||
        (type == 0x109 && subtype == 1))
        return "0x600654";
    if (type == 0x111 || type == 0xed || type == 0xee)
        return "0x60246a";
    return std::nullopt;
}

// 0x600436..0x6004a4: 0x171 in map 10, 0x164 in map 9 and 0x16d with the fourth argument.
glm::vec3 EarlyColorOf(const Kernel::ActorState& actor, std::int32_t map, std::uint16_t special, glm::vec3 color)
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

// 0x600e69..0x600ec1: +0x78 & 1 and & 2.
glm::vec3 FlagColorOf(const Kernel::ActorState& actor, glm::vec3 color)
{
    if ((actor.flags78 & 1u) != 0)
        return (actor.flags78 & 2u) != 0 ? glm::vec3{ThreeTenths, 1.0f, EightTenths}
                                         : glm::vec3{ThreeTenths, 1.0f, 0.5f};
    if ((actor.flags78 & 2u) != 0)
        return {ThreeTenths, 0.5f, 1.0f};
    return color;
}

// 0x6004f7..0x600548: type 0x16c.
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
    // 0x600b35: RenderBody(2) and RET, without 0x602365.
    return DrawBodyPass(model, BaseFlags, ActorPassArgs(frame.actor));
}

// What follows a case after the chain: the common RenderBody(2) (0x60235e) or straight to 0x602365.
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

// The meshes `meshes` with (2, −1) and then (0x44, Chrome01): type 0x17f (0x600f9f..0x601080) and type 0x186 with 0x12d
// (0x601471..0x601507).
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

// 0x601091..0x601159: type 0x17c with subtype 1, the light at sin(T · 0.002f) · 0.01f + 1 (a float) and four additive
// meshes: mesh 0 with 0x6f6 and blendMesh 0, mesh 1 with 0x6f5 and mesh 2 twice, with blendMesh 1.
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

// 0x601be5..0x601c0d and 0x601c21..0x601c49: types 0x18e and 0x18f; on death (+0x111 at 6), the alive byte at 0. Then
// sound 0x87 and 0x4ee770(0, effect, 0, 0, 0) with the type's model over the meshes 0x5ffdf0 left in this draw
// (0x5786f9c), in the original's frames, and the tail (0x602365).
LateResult EmitTheDeathMesh(MonsterBodyFrame& frame, bool dies, std::int32_t effect)
{
    if (dies)
        frame.actor.alive = false;
    RequestBodySound(frame, DeathSound);
    if (!frame.originalFrame)
        return LateNext::Tail;
    if (frame.effects == nullptr)
        return Core::Unexpected(
            std::format("0x4ee770: faltan los efectos con modelo para los vértices del tipo {:#x}", frame.actor.type));
    MonsterBodyModel& model = frame.model;
    return Done(Actors::Drawing::EmitVertexEffects(model.models, model.object, model.meshes, 0, effect, 0,
                                                   *frame.effects, frame.effectHooks, frame.random),
                LateNext::Tail);
}

// 0x457e00, vtable+4 of the edge class (0x6b8d4c), with the fourth argument at 1 (0x60121f): whether it reaches
// 0x457740, which marks in +0x22 the faces of mesh 1 that do not face the camera-to-hero direction and gathers their
// silhouette edges with the +0x1a neighborhood (P-C332 b). It does not reach it with the alpha below 0.01f or a NaN
// (0x457e53), with +0x58 or +0x64 (as int16) at −2 or 1, nor with a four-component texture in mesh 1 (+0x28 of its slot
// in 0x7d3a198, 0x457efb; a .tga's, as in HasFourComponents); with zero triangles it does nothing. Without mesh 1, the
// original reads outside the meshes.
Core::Expected<bool, std::string> MarksTheSilhouette(const MonsterBodyFrame& frame)
{
    const Kernel::ActorState& actor = frame.actor;
    if (!(actor.alpha >= Hundredth))
        return false;
    const auto hidden = static_cast<std::int16_t>(actor.dword58);
    const auto blend = static_cast<std::int16_t>(actor.dword64);
    if (hidden == -2 || blend == -2 || hidden == 1 || blend == 1)
        return false;
    const Data::Models::Model* found = frame.model.models.Find(Models::ModelId{actor.type});
    if (found == nullptr || found->meshes.size() < 2)
        return Core::Unexpected(
            std::format("0x457e97: el tipo {:#x} sin la malla 1 lee fuera de sus mallas", actor.type));
    const Data::Models::Mesh& mesh = found->meshes[1];
    if (Data::Models::ModelTextureLoaderOf(mesh.textureName) == Data::Textures::TextureLoader::Ozt)
        return false;
    return !mesh.triangles.empty();
}

// 0x601654..0x601bb1: type 0x18e.
LateResult DrawTheSpiritBody(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    if (actor.animation.action == DeathAction)
        return EmitTheDeathMesh(frame, true, StoneEffects);
    // 0x60166b..0x601b11: the step counter (0x7d3a0f0) minus +0x188, signed (JL).
    const std::int32_t steps = ReadLogicSteps(frame);
    if (static_cast<std::int32_t>(static_cast<std::uint32_t>(steps) - static_cast<std::uint32_t>(actor.stepStamp)) <
        0x19)
        return EmitTheDeathMesh(frame, false, StoneEffects);
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

// 0x600f74..0x60235c: the cases of 0x6002e0 after its chain.
LateResult DrawTheLateCase(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
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
        // 0x60115e..0x6011a4: RenderBody(2) and mesh 2 with blendMesh 2 and the light at 1.
        if (auto drawn = DrawBodyPass(model, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        BodyPassArgs lit = args;
        lit.blendMesh = 2;
        lit.light = 1.0f;
        return Done(DrawMeshPass(model, 2, BaseFlags, lit), LateNext::Tail);
    }
    case 0x183:
    {
        // 0x6011ae..0x6011e5: mesh 0; then 0x457d60 builds the edge class (vtable 0x6b8d4c), 0x457e00 gathers mesh 1's
        // silhouette edges and 0x457f70 draws them with 0x458000, one rand() per edge (0x6011f1..0x601241), and
        // vtable+8 frees it. Without 0x457740 it gathers none and nothing shows; with it, it cuts before those rand():
        // it lacks the triangles' neighborhood and the subtractive quad (P-C332 b).
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        const auto marks = MarksTheSilhouette(frame);
        if (!marks.HasValue())
            return Core::Unexpected(marks.Error());
        if (!marks.Value())
            return LateNext::Tail;
        return Core::Unexpected(
            Cut("0x6011f1", "la clase de los bordes (0x457740 y 0x457f70) de 0x6002e0", 0x183).Error());
    }
    case 0x188:
    {
        if (!special)
            return LateNext::Common;
        // 0x601264..0x6012c3: with 0.1, RenderBody(2) without mesh 1; with (1, 0.1, 0.1), mesh 1.
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
        // 0x6013b4..0x6013c4: RenderBody(2) without mesh 1, with 0x62e and the color at 1 (0x601436..0x60235e).
        model.object.color = glm::vec3(1.0f);
        BodyPassArgs hidden = WithTexture(args, 0x62e);
        hidden.hiddenMesh = 1;
        return Done(DrawBodyPass(model, BaseFlags, hidden), LateNext::Tail);
    }
    case 0x186:
        if (!special)
        {
            // 0x6013d7..0x60142d: with +0x88 at 0 and (0.4, 0.3, 0.5), RenderBody(2) with 0x624; then +0x88 at 0xff,
            // the color at 0.5 and the common one (0x601430).
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
        // 0x601518: (0.1, 1, 0.8) and the common one.
        model.object.color = {Tenth, 1.0f, EightTenths};
        return LateNext::Common;
    case 0x187:
        // 0x60152e..0x601577: with (0.9, 0.8, 1), RenderBody(4) with 0x5c1; the color at 1 and the common one.
        model.object.color = {std::bit_cast<float>(0x3f666666u), EightTenths, 1.0f};
        if (auto drawn = DrawBodyPass(model, 0x4, WithTexture(args, 0x5c1)); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        model.object.color = glm::vec3(1.0f);
        return LateNext::Common;
    case 0x185:
    {
        // 0x60157c..0x6015f1: RenderBody(2) without mesh 1 and mesh 1 twice with 0x402 (the wave of 0x4ecc30,
        // 0x4ed775), the texture in +0x58 and the alpha at 0.5 and at +0x174; then 0x602365.
        constexpr std::uint32_t Waving = 0x402;
        BodyPassArgs hidden = args;
        hidden.hiddenMesh = 1;
        if (auto drawn = DrawBodyPass(model, BaseFlags, hidden); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        BodyPassArgs waving = WithTexture(args, actor.dword58);
        waving.alpha = 0.5f;
        if (auto drawn = DrawMeshPass(model, 1, Waving, waving); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return Done(DrawMeshPass(model, 1, Waving, WithTexture(args, actor.dword58)), LateNext::Tail);
    }
    case 0x18a:
        // 0x601601..0x60164f, the second model of +0x376 at 0x4d (0x50c244): with +0x88 at 0 and the color at 1,
        // RenderBody(0x42) with Chrome01 (0x5c0); then +0x88 at 0xff, the color at 0.6 and the common one (0x602216).
        model.object.unlitMesh = std::int16_t{0};
        model.object.color = glm::vec3(1.0f);
        if (auto drawn = DrawBodyPass(model, AdditiveFlags, WithTexture(args, ChromeTexture)); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        model.object.unlitMesh.reset();
        model.object.color = glm::vec3(SixTenths);
        return LateNext::Common;
    case 0x18e:
        return DrawTheSpiritBody(frame);
    case 0x18f:
        if (actor.animation.action == DeathAction)
            return EmitTheDeathMesh(frame, true, GateStoneEffects);
        return LateNext::Common;
    default:
        // Type 0x198 (0x601d61 → 0x602170) and the others: the common one.
        return LateNext::Common;
    }
}

// 0x602365..0x602466: what 0x6002e0 adds after the body.
Result DrawTheTail(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
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
        // 0x602391..0x602431: the light at sin(T · 0.002f) · 0.3f + 0.5f (a float); mesh 0 with mesh 6's texture
        // (IndexTexture[6], +0x38 of the model) and blendMesh 0, and mesh 3 chromed with blendMesh 3 and the V at
        // −((ftol(T) % 100) · 0.01f), the remainder with the dividend's sign (CDQ and IDIV).
        const auto light = PulseOf(frame.worldClock, ThreeTenths, 0.5f, "0x6023c4");
        if (!light.HasValue())
            return Core::Unexpected(light.Error());
        const auto textures = model.models.MeshTextures(Models::ModelId{actor.type});
        if (textures.size() <= 6)
            return Core::Unexpected(
                std::format("0x6023b0: el modelo {:#x} no tiene la textura de la malla 6", actor.type));
        BodyPassArgs args = ActorPassArgs(actor);
        args.light = light.Value();
        args.blendMesh = 0;
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, WithTexture(args, textures[6].value)); !drawn.HasValue())
            return drawn;
        const auto clock = Math::OriginalFtolLow(D(frame.worldClock));
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
        // 0x60243e..0x602465: mesh 0 additive with +0x64, +0x68 and the U and V at 0.
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
    Kernel::ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    model.preludeColor = model.object.color;
    if (const auto address = UnreachableCaseOf(actor, frame.map))
        return Cut(*address, "el caso de 0x6002e0 de un tipo que no da ninguna alta de monstruo", actor.type);
    model.object.color = EarlyColorOf(actor, frame.map, frame.special, model.object.color);
    // 0x6004aa: with +0x74 at 0x80, RenderBody(0x82) and nothing else. +0x74 is not in ActorState: no spawn writes it
    // and MuRet takes it as 0 (*inferred*).
    if (actor.type == 0x16c)
        return DrawTheTintedBody(frame);
    model.object.color = FlagColorOf(actor, model.object.color);
    // 0x600ec4..0x600ef1: with the alpha at 1 and +0x68 at 0.05, types 0x175, 0x161, 0x184 and 0x185 store 1 in +0x68.
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
} // namespace Client::Monsters::Drawing
