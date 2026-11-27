#include "Actors/Drawing/GameHanging.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Actors/Attachment.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Actors/ActorName.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterAnimation.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Scenes/GuildState.h"
#include "World/ChaosCastle.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/ScepterGlow.h"
#include "World/Sprites.h"
#include "World/WeaponSparkKernels.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace Client::Actors::Drawing
{
namespace
{
using Math::BoneTransform;
using Result = Core::Expected<void, std::string>;

// The sprites' textures (0x61ccb0): flare01, Object9's Impack03, Spark02, Spark03, lightning2, Shiny02 and Shiny03.
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t Impact = 0x5ad;
constexpr std::uint16_t SecondSpark = 0x5c5;
constexpr std::uint16_t ThirdSpark = 0x5c6;
constexpr std::uint16_t Lightning = 0x5d5;
constexpr std::uint16_t Shiny = 0x5fd;
constexpr std::uint16_t ThirdShiny = 0x5fe;

constexpr std::int32_t NoItem = -1; // an item's 0xffff, signed (MOVSX)
constexpr std::int32_t Bolts = 0xa19;
constexpr std::int32_t Arrows = 0xa21;
constexpr std::int32_t Helper02 = 0x1c13;
constexpr std::int32_t Helper06 = 0x1c17;
constexpr std::uint8_t RedPk = 6;
constexpr std::uint32_t HiddenAttachmentFlag = 0x4000;
constexpr std::uint32_t FrozenAnimationFlag = 0x1000;
// One rand() for each byte of +0x38e when it closes (0x5098e5 and the loop of 0x50e298).
constexpr std::uint16_t Byte38eCloseRandoms = 1;
constexpr std::uint8_t WingsBone = static_cast<std::uint8_t>(Tables::PlayerWingsBone);
// 0x5096ac: the model whose origin 0x509340 shifts and whose z it turns (BossHead01, from map 5).
constexpr std::int32_t CarriedHead = 0xef;
// 0x743a160..0x743a168: what 0x5096ac adds to the origin of 0xef. No code writes them and they are in the .bss (the
// initialized image ends at 0x6e6000): 0 (*inferred*, P-C353).
constexpr glm::vec3 CarriedHeadShift{0.0f, 0.0f, 0.0f};
constexpr std::uint8_t PetBone = static_cast<std::uint8_t>(Tables::PlayerPetBone);
// 0x6b8a44: in action 0x3e the right hand hangs nothing until frame 4 passes.
constexpr float LateSwingFrame = 4.0f;
// 0x50de63: Blood Castle, where +0x360 from 1 to 3 hangs the event's weapon.
constexpr std::int32_t FirstBloodCastle = 11;
constexpr std::int32_t LastBloodCastle = 17;
// 0x6b88d4 (0.01f): the lowest alpha 0x414470 and 0x5ffdf0 draw.
constexpr float DrawnLayerAlpha = std::bit_cast<float>(0x3c23d70au);
// 0x413fe0: GMs wear layer 5; 0x41407c creates those from 5 to 13.
constexpr std::uint8_t GameMasterLayerKind = 5;
constexpr std::uint8_t LastLayerKind = 13;
constexpr std::uint32_t GameMasterFlag = 0x1000000;
constexpr std::uint8_t GameMasterControlCode = 0x20;
constexpr std::uint8_t SecondGameMasterControlCode = 8;
// 0x4140ab..0x4140c4: 0x414370(0x12a, 0x14, 1, 70, −5, 0, 0, 0, 45): Skill\MuSign.bmd on the owner's bone 20, shifted
// (70, −5, 0), with the fixed angles (0, 0, 45) (+0x93 at 1: it does not copy the owner's).
constexpr std::uint16_t MuSign = 0x12a;
constexpr std::uint8_t MuSignBone = 0x14;
constexpr glm::vec3 MuSignOffset{70.0f, -5.0f, 0.0f};
constexpr glm::vec3 MuSignAngles{0.0f, 0.0f, 45.0f};
// +0xdc: 0x414370 sets 0.5, which 0x414470 passes to 0x4ec200.
constexpr float MuSignSpeed = 0.5f;
// 0x6002e0 with 0x12a: RenderBody(2) with the light at 1 and RenderBody(0x44) with (1, 0.5, 0).
constexpr std::uint32_t BasePass = 2;
constexpr std::uint32_t ChromePass = 0x44;
constexpr glm::vec3 MuSignChromeLight{1.0f, 0.5f, 0.0f};

using Math::D;
using Math::F32;

// rand() % n like the original's IDIV: rand() is not negative.
int Mod(Crt::CrtRandom& random, int n)
{
    return random.Next() % n;
}

// FSIN of `argument` times the float `factor`, a single R53, and FSIN plus `addend` (the FADD of a float), a single
// R53. DrawGameHanging checks the clock first: with it, both always give a value.
double SineTimes(double argument, float factor)
{
    return Math::Detail::SineProductPC53(argument, factor).value_or(0.0);
}

double SinePlus(double argument, double addend)
{
    return Math::Detail::SineSumPC53(argument, addend).value_or(0.0);
}

// The state of the hanging slot and of its model in one frame of 0x50ba30.
struct Frame
{
    Kernel::ActorState& actor;
    const GameHangingPass& pass;
    // [ebp+0xc] of 0x50ba30: (rand() % 30 + 70) · 0.01 of the rand of 0x50db57.
    float lum = 0.0f;
    // +0x68 of the model of the character's type, with which 0x4ec190 scales the offset: the scale the body was posed
    // with, even if the caller of 0x509340 changed the character's (0x50c7dd).
    float bodyScale = 0.0f;

    // +0x6c..+0x74 of the type's model (GameHangingPass::modelOrigin): the record's, not the object's.
    [[nodiscard]] glm::vec3 ModelOrigin() const
    {
        return pass.modelOrigin.value_or(actor.position);
    }

    // 0x4ec110 with the type's model (the character's scale and ModelOrigin) on bone `bone` of its own pose.
    // DrawGameHanging checks first that the parts' bones exist.
    [[nodiscard]] glm::vec3 BodyPoint(std::uint8_t bone, const glm::vec3& offset) const
    {
        return Math::OriginalTransformPosition(Math::OriginalMatrixOf(pass.body[bone]), offset, actor.scale,
                                               ModelOrigin());
    }

    // 0x4ec110 on bone `bone` of the global buffer, with the character's scale and `origin`. A bone no pose has written
    // yet is the .bss one: all at 0.
    [[nodiscard]] glm::vec3 GlobalPoint(std::size_t bone, const glm::vec3& offset, const glm::vec3& origin) const
    {
        const BoneTransform matrix = bone < pass.carry->bones.size() ? pass.carry->bones[bone] : BoneTransform(0.0f);
        return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, actor.scale, origin);
    }

    // 0x5491e0: the additive sprite; the owner is not stored.
    void Sprite(std::uint16_t texture, const glm::vec3& point, float scale, const glm::vec3& light,
                float rotation = 0.0f) const
    {
        static_cast<void>(pass.sprites->Create(texture, point, scale, light, rotation, 0));
    }

    // 0x5092d0: the sprite on bone `bone` of the global buffer, with the character's scale and the origin `origin` of
    // the model it receives.
    void BoneSprite(std::uint16_t texture, std::size_t bone, float scale, const glm::vec3& light,
                    const glm::vec3& origin) const
    {
        Sprite(texture, GlobalPoint(bone, {}, origin), scale, light);
    }

    // 0x530860 at `address`, on the frame's joints. With the pool full it neither creates nor draws rand().
    [[nodiscard]] Result Joint(const World::FishEffectRequest& request, std::string_view address) const
    {
        if (pass.joints.empty())
            return Core::Unexpected(std::format("{}: faltan los joints del frame (0x530860)", address));
        auto created = Scenes::CreateFaunaJoint(pass.joints, request, pass.random);
        if (!created.HasValue())
            return Core::Unexpected(created.Error());
        return {};
    }

    // 0x53fe70 at `address` with the owner at 0, on the frame's particles and the world clock.
    [[nodiscard]] Result Particle(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                  const glm::vec3& light, std::int32_t subtype, float scale,
                                  std::string_view address) const
    {
        if (pass.particles == nullptr)
            return Core::Unexpected(std::format("{}: faltan las partículas del frame (0x53fe70)", address));
        const auto created =
            pass.particles->CreateChecked(type, position, angles, light, subtype, scale, pass.worldClock, pass.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{}: la partícula {:#x}/{} falla con el error {}", address, type,
                                                subtype, static_cast<int>(created.Error())));
        return {};
    }
};

// The place of what hangs through the flag's path (0x5093d2..0x5095e2). The angles' 0 is that of 0x6b86a0.
Core::Expected<Actors::HangingPlacement, std::string> PlacementOf(const Kernel::ActorState& actor, std::int32_t model)
{
    // 0x5093f3..0x50947c: with +0x376 from 0x84 to 0x86 (Blood Castle's statues, 0x50c83b), 0xc1c and 0x225 at (90, 0,
    // 90) and (0, 80, 120), and 0xa24 at (10, 0, 0) and (0, 110, 80). With another model (0x5095e0), the place's matrix
    // stays as it was on the stack: 0x50ba30 only hangs those three on them, and no addition gives that +0x376 to a
    // 0x20a.
    if (actor.word376 >= 0x84 && actor.word376 <= 0x86)
    {
        if (model == 0xc1c || model == 0x225)
            return Actors::HangingPlacement{{90.0f, 0.0f, 90.0f}, {0.0f, 80.0f, 120.0f}};
        if (model == 0xa24)
            return Actors::HangingPlacement{{10.0f, 0.0f, 0.0f}, {0.0f, 110.0f, 80.0f}};
        return Core::Unexpected(std::format("0x5095e0: el lugar del modelo {:#x} con el +0x376 {:#x} es la matriz sin "
                                            "iniciar de la pila de 0x509340",
                                            model, actor.word376));
    }
    const bool crossbow = (model >= 0xa1a && model < Arrows) || model == 0xa22 || (model >= 0xa24 && model < 0xa26);
    if (crossbow)
        return Actors::HangingPlacement{{0.0f, 20.0f, 180.0f}, {-10.0f, 8.0f, 40.0f}};
    if (model == 0xc1b)
        return Actors::HangingPlacement{{110.0f, 180.0f, 90.0f}, {-10.0f, 5.0f, -10.0f}};
    if (model == 0xa26)
        return Actors::HangingPlacement{{-60.0f, 0.0f, -80.0f}, {-5.0f, 20.0f, 0.0f}};
    if ((model >= 0xa12 && model < 0xc12) || model == 0xc1a)
        return Actors::HangingPlacement{{70.0f, 0.0f, 90.0f}, {-10.0f, 5.0f, 10.0f}};
    return Actors::HangingPlacement{{70.0f, 0.0f, 90.0f}, {-20.0f, 5.0f, 40.0f}};
}

// 0x4ec190 and 0x50966b: the origin of what hangs without the flag, with the scale `scale` of the type's model.
glm::vec3 OriginOf(const BoneTransform& parent, const glm::vec3& offset, const Kernel::ActorState& actor, float scale)
{
    const glm::vec3 turned = Math::OriginalVectorRotate(offset, Math::OriginalMatrixOf(parent));
    glm::vec3 origin{};
    for (int k = 0; k < 3; ++k)
        origin[k] = F32(D(F32(D(scale) * D(turned[k]))) + D(actor.position[k]));
    return origin;
}

// 0x509a92..0x50b801: the sprites by model, after drawing it, with its pose in the global buffer.
Result ItemSprites(const Frame& frame, std::int32_t model, const glm::vec3& origin)
{
    Kernel::ActorState& actor = frame.actor;
    Crt::CrtRandom& random = frame.pass.random;
    const double clock = D(frame.pass.worldClock);
    // [ebp+0x28]: (rand() % 30 + 70) · 0.005, even in the models without a case.
    const float l = F32(D(static_cast<float>(Mod(random, 30) + 70)) * D(0.005f));
    const auto at = [&](std::size_t bone, const glm::vec3& offset) { return frame.GlobalPoint(bone, offset, origin); };
    switch (model)
    {
    case 0x228:
    {
        // 0x509ad9: two Impack03 at the sides of the guard.
        const double x = SineTimes(clock * D(0.4f), 0.25f) + D(0.7f);
        const glm::vec3 light{F32(x), F32(x - D(0.5f)), F32(x - D(0.5f))};
        const glm::vec3 left = at(0, glm::vec3(5.0f, -22.0f, -10.0f));
        frame.Sprite(Impact, left, 0.75f, light);
        const glm::vec3 right = at(0, glm::vec3(-5.0f, -22.0f, -10.0f));
        frame.Sprite(Impact, right, 0.75f, light);
        return {};
    }
    case 0x229:
    {
        const float p = F32(D(static_cast<float>(Mod(random, 60))) * D(0.05f) - D(1.5f));
        const float s = F32(D(static_cast<float>(Mod(random, 30))) * D(0.05f) + D(1.5f));
        const float rotation = F32(clock * D(0.0006f) * D(360.0f));
        const glm::vec3 light{0.2f, 0.2f, F32(SineTimes(clock * D(0.7f), 0.2f) + D(0.5f))};
        const glm::vec3 tip = at(4, glm::vec3(0.0f, p, p));
        frame.Sprite(Shiny, tip, s, light, rotation);
        frame.Sprite(Shiny, tip, F32(D(s) - D(0.4f)), light, F32(D(rotation) + D(90.0f)));
        frame.Sprite(Flare, tip, 2.3f, light);
        const glm::vec3 guard = at(4, glm::vec3(30.0f, 0.0f, 0.0f));
        frame.Sprite(Flare, guard, 2.0f, light);
        const glm::vec3 sixth = at(6, glm::vec3{});
        frame.Sprite(Flare, sixth, 2.0f, light);
        const glm::vec3 seventh = at(7, glm::vec3{});
        frame.Sprite(Flare, seventh, 2.0f, light);
        const glm::vec3 eighth = at(8, glm::vec3{});
        frame.Sprite(Flare, eighth, 1.0f, light);
        return {};
    }
    case 0x225:
    {
        const glm::vec3 light{l, l * 0.3f, l * 0.1f};
        for (std::size_t bone = 2; bone <= 8; ++bone)
        {
            const glm::vec3 point = at(bone, glm::vec3{});
            if (Mod(random, 3) == 0)
                frame.Sprite(Shiny, point, 0.6f, glm::vec3(0.4f), static_cast<float>(Mod(random, 360)));
            frame.Sprite(Flare, point, 2.0f, light);
        }
        // 0x509ea8: outside actions 0x0d to 0x21 and 0x38 to 0x3c, one rand() and two points nobody uses.
        const std::uint8_t action = actor.animation.action;
        if (!(action >= 0x0d && action <= 0x21) && !(action >= 0x38 && action <= 0x3c))
            static_cast<void>(random.Next());
        return {};
    }
    case 0x226:
    {
        // 0x509f17..0x509ff8: EDI from 0x68b184c to 0x68b18ac, bones 1 and 2; on each, the two flare01 at the same
        // point, the pulsing one and the gray one (0.3).
        for (std::size_t bone = 1; bone <= 2; ++bone)
        {
            const double x = SineTimes(clock * D(0.002f), 0.35f) + D(0.65f);
            const glm::vec3 light{F32(x * D(0.43f)), F32(x * D(0.14f)), F32(x * D(0.6f))};
            const glm::vec3 point = at(bone, glm::vec3{});
            frame.Sprite(Flare, point, F32(x * D(0.9f)), light);
            frame.Sprite(Flare, point, F32(SineTimes(clock * D(0.002f), 0.5f) + D(0.4f)), glm::vec3(0.3f));
        }
        return {};
    }
    case 0x227:
    case 0x231:
    {
        // 0x50a005..0x50a138: in action 0x8c, outside map 10 and with half of the rand() calls, bone 1 shifted (rand()
        // % 30 − 15, rand() % 30 − 15, +20) releases four times the 0x617/0 joint (scale 10, no owner) and the 0x5c5/0
        // particle with the white light, each pair with the angles (rand() % 60 + 150, 0, the character's z).
        if (actor.animation.action != 0x8c || frame.pass.map == 10 || (random.Next() & 1) != 0)
            return {};
        glm::vec3 point = at(1, glm::vec3{});
        point.x = F32(D(static_cast<float>(Mod(random, 30))) - D(15.0f) + D(point.x));
        point.y = F32(D(static_cast<float>(Mod(random, 30))) - D(15.0f) + D(point.y));
        point.z = F32(D(point.z) + D(20.0f));
        const glm::vec3 white(1.0f);
        for (int spark = 0; spark < 4; ++spark)
        {
            glm::vec3 angles{static_cast<float>(Mod(random, 60) + 150), 0.0f, actor.angles.z};
            if (auto created = frame.Joint({0x617, point, point, angles, 0, nullptr, 10.0f, -1, 0, 0, -1}, "0x50a110");
                !created.HasValue())
                return created;
            if (auto created = frame.Particle(SecondSpark, point, angles, white, 0, 1.0f, "0x50a12f");
                !created.HasValue())
                return created;
        }
        return {};
    }
    case 0x619:
    {
        const glm::vec3 point = at(1, glm::vec3{});
        frame.Sprite(Flare, point, 2.0f, {l, l * 0.9f, 0.0f});
        frame.Sprite(Flare, point, F32(SinePlus(clock * D(0.002f), D(0.5f))), glm::vec3(0.5f));
        return {};
    }
    case 0x61a:
    {
        const float scale = F32(SinePlus(clock * D(0.001f), D(1.0f)));
        const glm::vec3 light{l * 0.2f, l * 0.1f, l * 3.0f};
        const glm::vec3 back = at(1, glm::vec3(-15.0f, 0.0f, 0.0f));
        frame.Sprite(Flare, back, scale, light);
        const glm::vec3 front = at(1, glm::vec3(10.0f, 0.0f, 0.0f));
        frame.Sprite(Flare, front, scale, light);
        const float rotation = F32(SineTimes(clock * D(0.01f), 360.0f));
        const glm::vec3 white(F32(SinePlus(clock * D(0.001f), D(1.0f)) * D(0.5f)));
        frame.Sprite(Shiny, front, 0.6f, white, F32(D(360.0f) - D(rotation)));
        frame.Sprite(Shiny, front, 0.6f, white, rotation);
        return {};
    }
    case 0x61b:
    {
        // 0x50a683..0x50a75f: ESI from 1 to 4 over 0x68b184c + ESI · 0x30, bones 2 to 5; with ESI at 3 (bone 4), the
        // Shiny02.
        for (std::size_t bone = 2; bone <= 5; ++bone)
        {
            const double x = SineTimes(clock * D(0.002f), 0.35f) + D(0.65f);
            const glm::vec3 point = at(bone, glm::vec3(-10.0f, 0.0f, 0.0f));
            frame.Sprite(Flare, point, 1.0f, {F32(x * D(0.6f)), F32(x * D(0.8f)), F32(x)});
            if (bone == 4)
                frame.Sprite(Shiny, point, 0.6f, glm::vec3(0.5f), static_cast<float>(Mod(random, 360)));
        }
        return {};
    }
    case 0x61c:
    {
        const float v = F32(SineTimes(clock * D(0.001f), 0.5f) + D(0.7f));
        const glm::vec3 head = at(2, glm::vec3{});
        const glm::vec3 light{v, v * 0.8f, v * 0.6f};
        frame.Sprite(Flare, head, 1.5f, light);
        frame.Sprite(Shiny, head, v * 0.5f, light);
        const float x = v * -10.0f;
        const float rotation = static_cast<float>(Mod(random, 360));
        const glm::vec3 tip = at(1, glm::vec3(x, 0.0f, 0.0f));
        const glm::vec3 white{0.6f, 0.8f, 1.0f};
        frame.Sprite(Flare, tip, 0.7f, white);
        frame.Sprite(Shiny, tip, 0.5f, white, rotation);
        frame.Sprite(Shiny, tip, 0.4f, white, F32(D(360.0f) - D(rotation)));
        return {};
    }
    case 0x61d:
    {
        const glm::vec3 light{1.0f, 0.6f, 0.3f};
        const glm::vec3 first = at(1, glm::vec3{});
        frame.Sprite(Flare, first, 1.3f, light);
        for (int i = 0; i < 3; ++i)
        {
            const glm::vec3 point = at(2, glm::vec3(static_cast<float>(i * 15 - 10), 0.0f, 0.0f));
            frame.Sprite(Flare, point, 1.3f, light);
            frame.Sprite(Shiny, point, 0.4f, light, static_cast<float>(Mod(random, 360)));
        }
        return {};
    }
    case 0x61e:
    {
        const float rotation = static_cast<float>(Mod(random, 360));
        const float x = F32(SineTimes(clock * D(0.002f), 0.3f) + D(0.7f));
        const glm::vec3 point = at(1, glm::vec3{});
        frame.Sprite(Flare, point, 2.0f, {0.0f, x * 0.5f, x});
        frame.Sprite(Shiny, point, 1.0f, glm::vec3(0.5f), rotation);
        frame.Sprite(Shiny, point, 1.0f, glm::vec3(0.5f), F32(D(360.0f) - D(rotation)));
        return {};
    }
    case 0x61f:
    {
        const glm::vec3 light{l, l * 0.3f, l * 0.1f};
        const glm::vec3 point = at(0, glm::vec3{});
        if (Mod(random, 3) == 0)
            frame.Sprite(Shiny, point, 0.6f, glm::vec3(0.4f), static_cast<float>(Mod(random, 360)));
        frame.Sprite(Flare, point, 2.0f, light);
        return {};
    }
    case World::HdkMaceModel:
    {
        if (frame.pass.carry->bones.empty())
            return Core::Unexpected(std::string("0x50a169: el cetro no dejó huesos en el buffer global"));
        World::CreateScepterGlow(*frame.pass.sprites, Math::OriginalMatrixOf(frame.pass.carry->bones[0]), actor.scale,
                                 origin, random);
        return {};
    }
    case 0x81c:
    {
        const glm::vec3 light{l * 0.2f, l * 0.1f, l * 0.8f};
        for (std::size_t bone = 1; bone <= 8; ++bone)
        {
            const glm::vec3 point = at(bone, glm::vec3{});
            frame.Sprite(Flare, point, 1.3f, light);
        }
        return {};
    }
    case 0xa16:
    case 0xa17:
    {
        const glm::vec3 light{l, l * 0.6f, l * 0.2f};
        for (const std::size_t bone : {std::size_t{2}, std::size_t{6}})
            frame.BoneSprite(Shiny, bone, 1.0f, light, origin);
        return {};
    }
    case 0xa18:
    case 0xa23:
    {
        const glm::vec3 light =
            model == 0xa18 ? glm::vec3{l * 0.6f, l, l * 0.8f} : glm::vec3{l * 0.5f, l * 0.5f, l * 0.8f};
        for (std::size_t bone = 13; bone <= 18; ++bone)
            frame.BoneSprite(Shiny, bone, 1.0f, light, origin);
        if (model == 0xa23)
            for (std::size_t bone = 5; bone <= 8; ++bone)
                frame.BoneSprite(Shiny, bone, 1.0f, light, origin);
        return {};
    }
    case 0xa25:
    {
        for (std::size_t bone = 1; bone <= 5; ++bone)
        {
            const glm::vec3 point = at(bone, glm::vec3(0.0f, 0.0f, 10.0f));
            glm::vec3 light{l * 0.5f, l * 0.5f, l * 0.8f};
            frame.Sprite(Shiny, point, 1.0f, light);
            if (bone == 5)
                light = glm::vec3(1.0f);
            frame.Sprite(Flare, point, 2.0f, light);
        }
        return {};
    }
    case 0xa26:
    {
        const float scale = F32(D(F32(SinePlus(clock * D(0.001f), D(1.0f)))) * D(0.8f));
        const glm::vec3 light{l * 3.0f, l, l};
        const glm::vec3 sixth = at(6, glm::vec3{});
        frame.Sprite(Flare, sixth, scale, light);
        const glm::vec3 second = at(2, glm::vec3{});
        frame.Sprite(Flare, second, scale, light);
        const float rotation = F32(SineTimes(clock * D(0.01f), 360.0f));
        const double x = SineTimes(clock * D(0.001f), 1.0f) * 0.3 + 0.30000001192092896;
        if (actor.safeZone != 0)
            return {};
        const glm::vec3 nock = at(9, glm::vec3(10.0f, 0.0f, 0.0f));
        frame.Sprite(Flare, nock, scale, glm::vec3(F32(x)));
        const glm::vec3 shine{F32(D(F32(x)) * D(3.0f)), F32(x), F32(x)};
        frame.Sprite(Shiny, nock, 0.8f, shine, F32(D(360.0f) - D(rotation)));
        frame.Sprite(Shiny, nock, 0.8f, shine, rotation);
        return {};
    }
    case 0xa27:
    {
        // 0x50af03..0x50afac: with half of the rand() calls, the 0x5c6/0xb spark of 0.8 on bone 4 and on bone 12, with
        // the light (0.8, 0.8, 0.2) and the character's angles.
        if ((random.Next() & 1) == 1)
        {
            const glm::vec3 light{0.8f, 0.8f, 0.2f};
            const glm::vec3 fourth = at(4, glm::vec3{});
            if (auto created = frame.Particle(ThirdSpark, fourth, actor.angles, light, World::WeaponSparkSubtype, 0.8f,
                                              "0x50af72");
                !created.HasValue())
                return created;
            const glm::vec3 twelfth = at(12, glm::vec3{});
            if (auto created = frame.Particle(ThirdSpark, twelfth, actor.angles, light, World::WeaponSparkSubtype, 0.8f,
                                              "0x50afa7");
                !created.HasValue())
                return created;
        }
        const glm::vec3 seventh = at(7, glm::vec3{});
        frame.Sprite(Flare, seventh, 2.0f, {0.5f, 0.5f, 0.1f});
        const glm::vec3 fifteenth = at(15, glm::vec3{});
        frame.Sprite(Flare, fifteenth, 2.0f, {0.5f, 0.5f, 0.1f});
        const glm::vec3 string = at(0, glm::vec3(20.0f, 0.0f, 0.0f));
        for (const float scale : {0.8f, 1.3f, 2.0f})
            frame.Sprite(Flare, string, scale, {1.0f, 1.0f, 0.4f});
        return {};
    }
    case 0xc1c:
    {
        const glm::vec3 light{l, l * 0.3f, l * 0.1f};
        for (int i = 0; i < 10; ++i)
        {
            const glm::vec3 point = at(0, glm::vec3(static_cast<float>(i * 30 - 180), -40.0f, 0.0f));
            if (Mod(random, 3) == 0)
                frame.Sprite(Shiny, point, 0.6f, glm::vec3(0.4f), static_cast<float>(Mod(random, 360)));
            frame.Sprite(Flare, point, 2.0f, light);
        }
        return {};
    }
    case 0xc1d:
    {
        const double x = SineTimes(clock * D(0.002f), 0.3f) + D(0.7f);
        const float rotation = static_cast<float>(Mod(random, 360));
        const float opposite = F32(D(360.0f) - D(rotation));
        constexpr std::array<std::pair<std::size_t, std::array<float, 2>>, 3> Bones{
            {{5, {1.0f, 0.5f}}, {6, {1.0f, 0.5f}}, {8, {2.0f, 1.0f}}}};
        for (const auto& [bone, scales] : Bones)
        {
            const glm::vec3 point = at(bone, glm::vec3{});
            frame.Sprite(Flare, point, scales[0], {0.0f, F32(x * D(0.5f)), F32(x)});
            frame.Sprite(Shiny, point, scales[1], glm::vec3(0.5f), rotation);
            frame.Sprite(Shiny, point, scales[1], glm::vec3(0.5f), opposite);
        }
        return {};
    }
    case 0xc1e:
    {
        // 0x50b411..0x50b4e7: Spark03 at (0, p − 170, p) of bone 0, with p = rand() % 60 · 0.05f − 1.5f and the scale
        // rand() % 15 · 0.05f + 1.8f. The point stays in the character's +0x40, and the 0x618/0x11 joint (scale 30,
        // with the character as owner) is born there.
        const float p = F32(D(static_cast<float>(Mod(random, 60))) * D(0.05f) - D(1.5f));
        const float s = F32(D(static_cast<float>(Mod(random, 15))) * D(0.05f) + D(1.8f));
        glm::vec3 point = at(0, glm::vec3(0.0f, F32(D(p) - D(170.0f)), p));
        frame.Sprite(ThirdSpark, point, s, glm::vec3(0.4f));
        actor.effectOrigin = point;
        glm::vec3 angles = actor.angles;
        if (auto created = frame.Joint({0x618, point, point, angles, 0x11, &actor, 30.0f, -1, 0, 0, -1}, "0x50b4e7");
            !created.HasValue())
            return created;
        // 0x50b4ef..0x50b5d8: another p (Q-C362), which shifts the offset without going back to 0x4ec110, and the scale
        // t = rand() % 15 · 0.05f + 1. At the same point, with the light (1, 0.4, 1): a flash of t, Shiny02 of t and of
        // t − 0.3f turned 90 and the 0x5c6/0xb spark of 2 with the character's angles.
        static_cast<void>(Mod(random, 60));
        const float t = F32(D(static_cast<float>(Mod(random, 15))) * D(0.05f) + D(1.0f));
        const glm::vec3 violet{1.0f, 0.4f, 1.0f};
        frame.Sprite(Flare, point, t, violet);
        frame.Sprite(Shiny, point, t, violet);
        frame.Sprite(Shiny, point, F32(D(t) - D(0.3f)), violet, 90.0f);
        if (auto created =
                frame.Particle(ThirdSpark, point, actor.angles, violet, World::WeaponSparkSubtype, 2.0f, "0x50b5d8");
            !created.HasValue())
            return created;
        // 0x50b5dd..0x50b763: with the light (a, 0.1, a), a = sin(clock · 0.7f) · 0.2f + 0.5f − 0.1f, and the turn g =
        // clock · 0.0006f · 360: Shiny02 of 1.5 and 1.2 at (0, 10, 0) of bone 2, of 1 and 0.7 at (−40, −10, 0) of bone
        // 1 and of 1.2 and 1 at (−160, −5, 0) of bone 1, turned g and g + 90.
        const float a = F32(SineTimes(clock * D(0.7f), 0.2f) + D(0.5f) - D(0.1f));
        const glm::vec3 light{a, 0.1f, a};
        const float rotation = F32(clock * D(0.0006f) * D(360.0f));
        const float turned = F32(D(rotation) + D(90.0f));
        constexpr std::array<std::tuple<std::size_t, glm::vec3, float, float>, 3> Shines{{
            {2, {0.0f, 10.0f, 0.0f}, 1.5f, 1.2f},
            {1, {-40.0f, -10.0f, 0.0f}, 1.0f, 0.7f},
            {1, {-160.0f, -5.0f, 0.0f}, 1.2f, 1.0f},
        }};
        for (const auto& [bone, offset, first, second] : Shines)
        {
            point = at(bone, offset);
            frame.Sprite(Shiny, point, first, light, rotation);
            frame.Sprite(Shiny, point, second, light, turned);
        }
        return {};
    }
    case 0xe21:
    {
        const glm::vec3 point = at(1, glm::vec3(15.0f, -15.0f, 0.0f));
        const glm::vec3 light{l * 0.6f, l * 0.6f, l + l};
        frame.Sprite(Shiny, point, 1.5f, light);
        frame.Sprite(Flare, point, F32(D(l) + D(1.5f)), light);
        return {};
    }
    default:
        return {};
    }
}

// 0x509340: what hangs from `part`, with the model `model`, from its body bone.
Result HangItem(const Frame& frame, const glm::vec3& offset, Kernel::ActorItemPart& part, std::int32_t model,
                std::uint8_t level, std::uint8_t excellent, bool placed)
{
    Kernel::ActorState& actor = frame.actor;
    const GameHangingPass& pass = frame.pass;
    if (!Actors::HangsAsAModel(static_cast<std::uint16_t>(model)) ||
        (actor.flags78 & HiddenAttachmentFlag) == HiddenAttachmentFlag)
        return {};
    const auto type = static_cast<std::uint16_t>(model);
    // 0x5093bb: 0x607b60 builds the object with the item's type.
    Models::ObjectRandoms randoms{};
    for (std::size_t index = 0; index < HangingRandomCount(type); ++index)
        randoms[index] = pass.random.Next();
    const Models::ObjectState state = Models::NativeItemObjectState(type, pass.frameClock, randoms);
    if (part.bone >= pass.body.size())
        return Core::Unexpected(
            std::format("0x509340: el hueso {:#x} del cuerpo no existe", static_cast<int>(part.bone)));
    const BoneTransform& parent = pass.body[part.bone];
    BoneTransform root = parent;
    glm::vec3 origin = actor.position;
    if (placed)
    {
        const auto placement = PlacementOf(actor, model);
        if (!placement.HasValue())
            return Core::Unexpected(placement.Error());
        root = Actors::PlacedRoot(parent, placement.Value());
    }
    else
    {
        origin = OriginOf(parent, offset, actor, frame.bodyScale);
    }
    // 0x50968c..0x5096f1: the object's angles at 0; 0xef (only +0x376 at 0x2a hangs it, 0x50c79a) adds
    // 0x743a160..0x743a168 to the origin, sets its model's head bone (+0x54) to 0 and takes the world clock's bits
    // (0x5767b88) to z. The angles go to the model's +0x78 (0x4eb860), and the pose does not read them: its root is
    // that of 0x68c4a1c. The head subtracts the object's x and y angles, which are 0, and leaves the key's quaternion:
    // no effect, +0x54 is not stored.
    glm::vec3 angles{};
    if (model == CarriedHead)
    {
        for (int k = 0; k < 3; ++k)
            origin[k] = F32(D(CarriedHeadShift[k]) + D(origin[k]));
        angles.z = pass.worldClock;
    }
    std::uint8_t value = 0;
    {
        const Models::LogicalAccess access(actor.byte38eAccesses, pass.random, Byte38eCloseRandoms);
        value = actor.byte38e;
    }
    // 0x509926: with '4', a player between actions 0x22 and 0x97 and from frame 5 to 10.
    if (value == 0x34 && actor.type == Tables::PlayerSkeletonModel && actor.animation.action >= 0x22 &&
        actor.animation.action <= 0x97 && actor.animation.frame >= 5.0f && actor.animation.frame <= 10.0f)
    {
        part.animation.previousKey = 2;
        part.animation.frame = 3.2f;
    }
    // Q-C102: a model the original does not load is a zeroed table entry, and 0x509340 does not look at it: 0x4ec200
    // and 0x4eb860 do nothing without actions (+0x26), 0x4ebc80 has no meshes to walk (+0x24) and neither does 0x60b3d0
    // (its rand() belongs to the set, which 0x509340 passes at 0). The light of 0x509a92 and the model's case run the
    // same. So the hand of a type that is not an item (0xff0 gives 0x1202) draws nothing. Not ported: the per-level
    // effects of 0x60b3d0 (0x530860, 0x5491e0) on such a model.
    const Models::Skeleton* skeleton = pass.models.SkeletonOf(type);
    if (pass.models.Find(type) == nullptr || skeleton == nullptr)
        return ItemSprites(frame, model, origin);
    // 0x5093a3 and 0x50997e: the model's action is the part's, except 0x1a18 in the safe zone (with the part's model,
    // not the one that draws).
    const std::uint8_t action = part.model == 0x1a18 && actor.safeZone != 0 ? 1 : part.animation.action;
    // 0x5099a2..0x5099ed: 0x4ec200 advances the animation except with the flag on a weapon from 0xa12 to 0xc11 or with
    // +0x78 & 0x1000, and without actions (+0x26) it does nothing. It reads the action of +0xa0 without looking at how
    // many the model has: with one too many, it reads the heap after its array (Q-C1119), and it stops. 0x4eb860 takes
    // it to 0 (ChooseKeys).
    const bool still = placed && model >= 0xa12 && model < 0xc12;
    const bool advances =
        !still && (actor.flags78 & FrozenAnimationFlag) != FrozenAnimationFlag && !skeleton->actions.empty();
    if (advances && action >= skeleton->actions.size())
        return Core::Unexpected(std::format("0x4ec200: el modelo {:#x} no tiene la acción {}, que lee fuera de su "
                                            "arreglo (Q-C1119)",
                                            model, static_cast<int>(action)));
    Models::AnimationState animation = part.animation;
    animation.action = action;
    if (pass.originalFrame && advances)
    {
        static_cast<void>(
            Models::AdvanceAnimation(animation, skeleton->actions[action], part.speed, Models::AnimationScene::Other));
        part.animation.frame = animation.frame;
        part.animation.previousAction = animation.previousAction;
        part.animation.previousKey = animation.previousKey;
    }
    // 0x509a1a: 0x4eb860 in the global buffer with the root of 0x68c4a1c; the dummies stay as they were.
    Scenes::FaunaDrawCarry& carry = *pass.carry;
    if (carry.bones.size() < skeleton->bones.size())
        carry.bones.resize(skeleton->bones.size(), BoneTransform(0.0f));
    const auto bones = std::span(carry.bones).first(skeleton->bones.size());
    if (!Models::PoseAttached(*skeleton, animation, root, bones))
        return Core::Unexpected(std::format("0x4eb860: la pose del modelo {:#x} que cuelga no es válida", model));
    DrawnObject object;
    object.model = type;
    object.animation = animation;
    object.transform = {.position = origin, .angles = angles, .scale = actor.scale};
    object.state = state;
    object.color = actor.drawnLight;
    object.alpha = actor.alpha;
    if (auto published = Scenes::PublishFaunaPoseRaw(carry, pass.models, type, bones, object.transform, false, true);
        !published.HasValue())
        return published;
    // 0x509a8a: 0x60b3d0 with the level · 8, the excellent, no set, and 2 (0x102 with +0x376 at 0x43 or 0x89).
    ItemDrawContext resources = pass.itemContext != nullptr ? *pass.itemContext : ItemDrawContext{};
    ItemSpecialDrawContext special = resources.special != nullptr ? *resources.special : ItemSpecialDrawContext{};
    glm::vec3 objectLight{};
    special.worldClock = pass.worldClock;
    special.random = &pass.random;
    special.sprites = pass.sprites;
    special.objectFlags = actor.word376 == 0x43 || actor.word376 == 0x89 ? 0x102u : 2u;
    special.objectLight = &objectLight;
    special.publishedObject = &object;
    resources.special = &special;
    resources.ownerContext = &actor;
    if (!resources.poseBones)
        resources.poseBones = [&carry]() { return std::span<const BoneTransform>(carry.bones); };
    if (auto drawn = DrawItemFromMeshes(pass.batch, pass.models, object, pass.frameClock, carry.meshes,
                                        static_cast<std::uint8_t>(level & 15), excellent, 0, resources);
        !drawn.HasValue())
        return drawn;
    return ItemSprites(frame, model, origin);
}

// 0x50dcda..0x50dd70: with the back, the weapon of BackHandOf; without it, the bolts of the left or the arrows of the
// right.
struct BackItem
{
    bool selected = false;
    std::size_t hand = 0;
};

BackItem BackItemOf(const Kernel::ActorState& actor, bool back)
{
    const std::int32_t left = static_cast<std::int16_t>(actor.hands[1].model);
    const std::int32_t right = static_cast<std::int16_t>(actor.hands[0].model);
    if (back)
    {
        if ((left >= 0xa12 && left < Bolts) || left == 0xa23 || left == 0xa26 || left == 0xa27)
            return {true, 1};
        if ((right >= 0xa1a && right < Arrows) || (right >= 0xa22 && right < 0xa23) ||
            (right >= 0xa24 && right < 0xc12))
            return {true, 0};
        return {true, left == Bolts ? std::size_t{1} : std::size_t{0}};
    }
    if (left == Bolts)
        return {true, 1};
    return {right == Arrows, 0};
}

float PlayerActionSpeed(const GameHangingPass& pass, std::size_t action)
{
    const Models::Skeleton* player = pass.models.SkeletonOf(Tables::PlayerSkeletonModel);
    return player != nullptr && action < player->actions.size() ? player->actions[action].speed : 0.0f;
}

// 0x50ddde..0x50de60: the weapon or the quiver on the back, from the wings' slot.
Result HangTheBack(Frame& frame, bool back, std::uint8_t& petExcellent)
{
    Kernel::ActorState& actor = frame.actor;
    const BackItem selected = BackItemOf(actor, back);
    const Kernel::ActorItemPart& hand = actor.hands[selected.hand];
    std::int32_t model = static_cast<std::int16_t>(hand.model);
    std::uint8_t level = hand.level;
    petExcellent = hand.flags1b;
    // 0x50dd8f: with +0x88 at 4 in Lorencia, subtypes 0xd1 to 0xd3 carry Bow+8 (0xa1b).
    const bool guard = actor.byte88 == 4 && frame.pass.map == 0 && actor.subtype >= 0xd1 && actor.subtype <= 0xd3;
    if (guard)
    {
        model = 0xa1b;
        level = 8;
    }
    else if (!selected.selected || model == NoItem)
    {
        return {};
    }
    Kernel::ActorItemPart& wings = actor.wings;
    const float savedFrame = wings.animation.frame;
    wings.bone = WingsBone;
    wings.speed = WingKeysPerOriginalFrame(actor.animation.action);
    const bool placed = model >= 0x212 && model < 0x1012;
    if (placed)
        wings.speed = PlayerActionSpeed(frame.pass, 1);
    if (auto hung = HangItem(frame, {0.0f, 0.0f, 15.0f}, wings, model, level, hand.flags1b, placed); !hung.HasValue())
        return hung;
    // 0x50de5a: only the frame comes back; the previous action and key stay as the weapon left them (Q-C296).
    wings.animation.frame = savedFrame;
    return {};
}

// 0x50de84..0x50defd: in Blood Castle with +0x360, the event's weapon on the back, from the wings' slot: 0x509340(0, 0,
// 15, c, +0x2f8, model, 0, 0, 1, 1, 0). The model is 0xc1c, 0x225 or 0xa24 with +0x360 from 1 to 3 and, with another
// value (the GMs' 5), the one left in [ebp-0x18]: that of the slot the back chose (0x50dd97), even if it did not hang.
// It does not overwrite the step with Player.bmd's (0x50de23) or bring the frame back (0x50de5a), and 0x50de8a sets to
// 0 the excellent Helper02 then carries ([ebp-0x24], Q-C1331).
Result HangTheEventWeapon(Frame& frame, bool back, std::uint8_t& petExcellent)
{
    constexpr std::array<std::int32_t, 3> EventWeapons{0xc1c, 0x225, 0xa24};
    Kernel::ActorState& actor = frame.actor;
    petExcellent = 0;
    Kernel::ActorItemPart& wings = actor.wings;
    wings.bone = WingsBone;
    wings.speed = WingKeysPerOriginalFrame(actor.animation.action);
    std::int32_t model = static_cast<std::int16_t>(actor.hands[BackItemOf(actor, back).hand].model);
    const auto weapon = static_cast<std::size_t>(actor.byte360);
    if (weapon >= 1 && weapon <= EventWeapons.size())
        model = EventWeapons[weapon - 1];
    // Q-C1332: with the slot empty (−1, a GM without a weapon), 0x50934f takes entry −1 of [0x5769ad8] and writes in
    // the 0xe0 bytes before the model table.
    if (model < 0)
        return Core::Unexpected(std::format("0x50def8: el arma del evento de Blood Castle con +0x360 en {} cuelga el "
                                            "modelo {} y 0x509340 escribe antes de la tabla de modelos (Q-C1332)",
                                            weapon, model));
    return HangItem(frame, {0.0f, 0.0f, 15.0f}, wings, model, 0, 0, true);
}

// 0x50df22..0x50e05d: the wings and Helper02 with its spark.
Result HangWingsAndPet(Frame& frame, std::uint8_t petExcellent)
{
    Kernel::ActorState& actor = frame.actor;
    Kernel::ActorItemPart& wings = actor.wings;
    if (wings.model != Kernel::NoItemModel)
    {
        wings.bone = WingsBone;
        wings.speed = WingKeysPerOriginalFrame(actor.animation.action);
        if (auto hung = HangItem(frame, {0.0f, 0.0f, 15.0f}, wings, static_cast<std::int16_t>(wings.model), wings.level,
                                 wings.flags1b, false);
            !hung.HasValue())
            return hung;
    }
    Kernel::ActorItemPart& pet = actor.pet;
    if (static_cast<std::int16_t>(pet.model) != Helper02)
        return {};
    pet.bone = PetBone;
    pet.speed = Models::PetKeysPerOriginalFrame;
    // 0x50dfa9: the excellent is that of the weapon on the back (Q-C297).
    if (auto hung = HangItem(frame, {20.0f, 0.0f, 0.0f}, pet, Helper02, pet.level, petExcellent, false);
        !hung.HasValue())
        return hung;
    const glm::vec3 point = frame.BodyPoint(pet.bone, {20.0f, 0.0f, 15.0f});
    frame.Sprite(Flare, point, 1.5f, {frame.lum * 0.5f, 0.0f, 0.0f});
    return {};
}

// 0x50e76e..0x50e86a: the action and how much the hand's weapon advances, by the character's action.
void SetTheHandPace(const Frame& frame, Kernel::ActorItemPart& part, std::int32_t model)
{
    const std::uint8_t action = frame.actor.animation.action;
    const float one = PlayerActionSpeed(frame.pass, 1);
    if (action >= 0x2e && action <= 0x31)
    {
        part.animation.action = 0;
        part.speed = PlayerActionSpeed(frame.pass, 0x2e);
    }
    else if (model == 0x614)
    {
        const bool swing = action >= 0x23 && action <= 0x24;
        part.animation.action = swing ? 2 : 1;
        part.speed = swing ? PlayerActionSpeed(frame.pass, 0x23) : one;
    }
    else if (model == 0x617)
    {
        part.animation.action = 0;
        part.speed = F32(D(one) + D(one));
    }
    else if (model == 0xc18)
    {
        part.animation.action = 0;
        part.speed = F32(D(one) * D(15.0f));
    }
    else if (model >= 0x212 && model < 0x412)
    {
        part.animation.action = 0;
        part.speed = one;
    }
    else
    {
        part.animation.action = 0;
        part.speed = 0.0f;
        part.animation.frame = 0.0f;
        part.animation.previousKey = 0;
    }
}

// 0x50e896..0x50e908: the light of the weapon's sprites by its level.
glm::vec3 LevelLight(float lum, std::uint8_t level)
{
    if (level >= 7)
        return {lum * 0.5f, lum * 0.4f, lum * 0.3f};
    if (level >= 5)
        return {lum * 0.3f, lum * 0.3f, lum * 0.5f};
    if (level >= 3)
        return {lum * 0.5f, lum * 0.3f, lum * 0.3f};
    return glm::vec3(lum * 0.3f);
}

// 0x50e921..0x50f828: the sprites of the weapons in the hand, with the body and the global buffer.
Result HandSprites(const Frame& frame, const Kernel::ActorItemPart& part, glm::vec3 light)
{
    const double clock = D(frame.pass.worldClock);
    const float lum = frame.lum;
    Crt::CrtRandom& random = frame.pass.random;
    // 0x50f195 and 0x50f3f9: 0x5092d0 with [ebp−0x14], the type's model.
    const glm::vec3 position = frame.ModelOrigin();
    const auto at = [&](const glm::vec3& offset) { return frame.BodyPoint(part.bone, offset); };
    const auto blade = [&](const glm::vec3& offset) -> Result
    {
        const glm::vec3 point = at(offset);
        frame.Sprite(Flare, point, 1.4f, light);
        return {};
    };
    switch (static_cast<std::int16_t>(part.model))
    {
    case 0x216:
    case 0x217:
    case 0x21f:
        return blade({0.0f, -110.0f, 5.0f});
    case 0x219:
    case 0x21c:
        return blade({0.0f, -110.0f, 0.0f});
    case 0x21a:
    case 0x21b:
        return blade({0.0f, -110.0f, -5.0f});
    case 0x21d:
    case 0x220:
        return blade({0.0f, -150.0f, 0.0f});
    case 0x21e:
        light = glm::vec3(lum);
        return blade({0.0f, -160.0f, 0.0f});
    case 0x223:
    {
        // 0x50eb43..0x50ecf0: the width sin(clock · 0.004f) · 10 + 20 and two 0x610/4 joints with the character as
        // owner and its angles, from (0, −20, −40) to (0, −160, −10) and from (0, −10, 28) to (0, −145, 18) of the
        // hand. No sprites.
        const float width = F32(SineTimes(clock * D(0.004f), 10.0f) + D(20.0f));
        glm::vec3 angles = frame.actor.angles;
        constexpr std::array<std::tuple<glm::vec3, glm::vec3, std::string_view>, 2> Edges{{
            {{0.0f, -20.0f, -40.0f}, {0.0f, -160.0f, -10.0f}, "0x50ec26"},
            {{0.0f, -10.0f, 28.0f}, {0.0f, -145.0f, 18.0f}, "0x50ecf0"},
        }};
        for (const auto& [from, to, address] : Edges)
        {
            glm::vec3 first = at(from);
            glm::vec3 second = at(to);
            if (auto created =
                    frame.Joint({0x610, first, second, angles, 4, &frame.actor, width, -1, 0, 0, -1}, address);
                !created.HasValue())
                return created;
        }
        return {};
    }
    case 0x224:
    {
        const glm::vec3 point = at(glm::vec3(0.0f, -20.0f, 15.0f));
        const double x = SinePlus(clock * D(0.004f), D(1.0f)) * D(0.3f);
        const float v = F32(x);
        frame.Sprite(Shiny, point, F32(D(v) + D(1.0f)), {F32(x * D(0.2f)), F32(x * D(0.2f)), v});
        // 0x50eda0..0x50ee6e: three 0x614/10 joints without an owner from that point to (0, −133, 7) of the hand, with
        // the character's angles and the width v · 20 + 20.
        glm::vec3 first = point;
        glm::vec3 second = at(glm::vec3(0.0f, -133.0f, 7.0f));
        glm::vec3 angles = frame.actor.angles;
        const float width = F32(D(v) * D(20.0f) + D(20.0f));
        for (const std::string_view address : {"0x50ee26", "0x50ee46", "0x50ee69"})
        {
            if (auto created = frame.Joint({0x614, first, second, angles, 10, nullptr, width, -1, 0, 0, -1}, address);
                !created.HasValue())
                return created;
        }
        return {};
    }
    case 0x617:
    {
        light = {lum, lum * 0.6f, lum * 0.4f};
        for (int y = 0; y > -160; y -= 20)
        {
            if (Mod(random, 4) >= 3)
                continue;
            const glm::vec3 point = at(glm::vec3(0.0f, F32(D(static_cast<float>(y)) - D(30.0f)), 0.0f));
            frame.Sprite(Flare, point, 1.0f, light);
        }
        return {};
    }
    case 0x618:
    {
        const glm::vec3 head = at(glm::vec3(0.0f, -84.0f, 0.0f));
        const float v = F32(SineTimes(clock * D(0.004f), 0.3f) + D(0.7f));
        light = {v, v * 0.2f, v * 0.1f};
        frame.Sprite(Shiny, head, F32(D(v) + D(1.5f)), light);
        for (int i = 0; i > -5; --i)
        {
            const glm::vec3 point = at(glm::vec3(0.0f, F32(D(static_cast<float>(i)) * D(20.0f) - D(10.0f)), 0.0f));
            frame.Sprite(Shiny, point, 1.0f, light);
        }
        light = {v * 0.5f, v * 0.1f, v * 0.05f};
        for (const std::size_t bone : {std::size_t{2}, std::size_t{6}})
            frame.BoneSprite(Shiny, bone, 1.0f, light, position);
        return {};
    }
    case 0xa1f:
    case 0xa20:
    {
        light = part.model == 0xa1f ? glm::vec3{lum * 0.2f, lum * 0.4f, lum * 0.6f}
                                    : glm::vec3{lum * 0.6f, lum * 0.4f, lum * 0.2f};
        for (int z = 0; z > -120; z -= 20)
        {
            const glm::vec3 point = at(glm::vec3(0.0f, -20.0f, static_cast<float>(z)));
            frame.Sprite(Flare, point, 2.0f, light);
        }
        return {};
    }
    case 0xa22:
    {
        light = {lum * 0.4f, lum * 0.6f, lum};
        for (int i = 0; i > -6; --i)
        {
            const glm::vec3 point = at(glm::vec3(0.0f, -10.0f, F32(D(static_cast<float>(i)) * D(20.0f))));
            frame.Sprite(Flare, point, 2.0f, light);
        }
        return {};
    }
    case 0xc16:
    {
        const glm::vec3 point = at(glm::vec3(0.0f, -90.0f, 0.0f));
        frame.Sprite(Shiny, point, 2.0f, {lum * 0.4f, lum * 0.8f, lum * 0.6f});
        return {};
    }
    case 0xc17:
    {
        const glm::vec3 point = at(glm::vec3(0.0f, -145.0f, 0.0f));
        light = {lum * 0.4f, lum * 0.6f, lum};
        frame.Sprite(Shiny, point, 1.5f, light);
        frame.Sprite(Lightning, point, 0.3f, light);
        return {};
    }
    case 0xc18:
    {
        const glm::vec3 point = at(glm::vec3(0.0f, -145.0f, 0.0f));
        light = {lum, lum * 0.6f, lum * 0.4f};
        frame.Sprite(SecondSpark, point, 3.0f, light);
        frame.Sprite(ThirdShiny, point, 1.5f, light);
        // 0x50f4fd..0x50f5b1: four 0x5c5/1 sparks of the hand, shifted (rand() % 20 − 10, rand() % 20 − 10 − 90, rand()
        // % 20 − 10), with the character's angles and the same light.
        for (int spark = 0; spark < 4; ++spark)
        {
            const auto x = static_cast<float>(Mod(random, 20) - 10);
            const float y = F32(D(static_cast<float>(Mod(random, 20) - 10)) - D(90.0f));
            const auto z = static_cast<float>(Mod(random, 20) - 10);
            const glm::vec3 offset = at(glm::vec3(x, y, z));
            if (auto created = frame.Particle(SecondSpark, offset, frame.actor.angles, light, 1, 1.0f, "0x50f5a8");
                !created.HasValue())
                return created;
        }
        // 0x50f5b7..0x50f676: from 0 to −180, in steps of 20, with three of every four rand() calls a flash of 1 at (0,
        // y + 60, 0) with the light (l, l · 0.2, l · 0.1).
        light = {lum, lum * 0.2f, lum * 0.1f};
        for (int y = 0; y > -200; y -= 20)
        {
            if ((random.Next() & 3) >= 3)
                continue;
            const glm::vec3 flare = at(glm::vec3(0.0f, F32(D(static_cast<float>(y)) + D(60.0f)), 0.0f));
            frame.Sprite(Flare, flare, 1.0f, light);
        }
        return {};
    }
    case 0xc19:
    {
        light = {lum * 0.4f, lum * 0.6f, lum};
        frame.BoneSprite(Shiny, 0x1b, 2.0f, light, position);
        for (std::size_t bone = 0x1c; bone <= 0x25; ++bone)
            frame.BoneSprite(Flare, bone, 1.5f, light, position);
        return {};
    }
    case 0xc1b:
    {
        light = {lum * 0.6f, lum * 0.6f, lum + lum};
        const glm::vec3 head = at(glm::vec3(0.0f, -120.0f, 5.0f));
        frame.Sprite(Shiny, head, 1.5f, light);
        frame.Sprite(Flare, head, F32(D(lum) + D(1.0f)), light);
        const glm::vec3 foot = at(glm::vec3(0.0f, 100.0f, 10.0f));
        frame.Sprite(Flare, foot, F32(D(lum) + D(1.0f)), light);
        return {};
    }
    case 0xe20:
    {
        const glm::vec3 point = at(glm::vec3(20.0f, 0.0f, 0.0f));
        frame.Sprite(Shiny, point, 1.5f, {lum * 0.4f, lum * 0.6f, F32(D(lum) * D(1.5f))});
        return {};
    }
    default:
        return {};
    }
}

// 0x50e298..0x50e9ea: one hand, with +0x38e opened and closed before looking at it.
Result HangTheHand(Frame& frame, std::size_t index)
{
    Kernel::ActorState& actor = frame.actor;
    std::uint8_t value = 0;
    {
        const Models::LogicalAccess access(actor.byte38eAccesses, frame.pass.random, Byte38eCloseRandoms);
        value = actor.byte38e;
    }
    const std::uint8_t action = actor.animation.action;
    const bool right = index == 0;
    if (action == 0x3e && right && !(actor.animation.frame > LateSwingFrame))
        return {};
    if (value == ')' && action == 0x3d && right)
        return {};
    Kernel::ActorItemPart& part = actor.hands[index];
    const std::int32_t model = static_cast<std::int16_t>(part.model);
    if (model == NoItem || model == Bolts || model == Arrows || model == Helper06)
        return {};
    SetTheHandPace(frame, part, model);
    if (auto hung = HangItem(frame, {}, part, model, part.level, part.flags1b, false); !hung.HasValue())
        return hung;
    const glm::vec3 light = LevelLight(frame.lum, part.level);
    // 0x50e902: the reds and the character's level 4 (+0x1d6) do not carry the sprites.
    if (actor.pk >= RedPk || actor.level == 4)
        return {};
    return HandSprites(frame, part, light);
}

bool SomethingHangs(const Kernel::ActorState& actor)
{
    return actor.wings.model != Kernel::NoItemModel || static_cast<std::int16_t>(actor.pet.model) == Helper02 ||
           std::ranges::any_of(actor.hands,
                               [](const Kernel::ActorItemPart& hand) { return hand.model != Kernel::NoItemModel; });
}

bool WeaponsOnTheBack(const Kernel::ActorState& actor, std::int32_t map)
{
    constexpr std::uint8_t FirstBackAction = 0x99;
    constexpr std::uint8_t LastBackAction = 0xb8;
    const bool event = map >= 11 && map <= 23;
    const bool restingInFlight = (map == 7 || (map >= 24 && map <= 29) || map == 36) &&
                                 (actor.animation.action == 0x15 || actor.animation.action == 0x1d);
    return !event && (actor.safeZone != 0 || restingInFlight ||
                      (actor.animation.action >= FirstBackAction && actor.animation.action <= LastBackAction));
}

// 0x413fe0: 0x576960 looks for "webzen" (0x6c64ac) in the name of +0x1d9.
bool NamedLikeAGameMaster(const Kernel::ActorState& actor)
{
    return ActorNameContains(actor, "webzen");
}

// 0x413fe0: a GM ("webzen" in the name, +0x78 & 0x1000000 or the CtlCode at 0x20 or 8) drops a layer other than 5 and
// sets +0x360 to 5. Then, with no layer and with +0x360 from 5 to 13, it creates it: with +0x360 at 5, MuSign's
// (0x414370); those from 6 to 13 use the 0x637 atlas (0x4147c0). From 6 to 13 only packets write it: the 0x12
// (0x4c89e1, ViewportPlayers.h) and 0x40c140 copy a byte, and 0x42ab40 (ApplyCastleSiegeFlags), 0x42ad50 and 0x42ae60
// choose it by the flags 0x40000, 0x80000, 0x400000 and 0x800000 of +0x78 (0x42ab40, in map 30); the drawing of those
// layers (0x414820) reads +0x360 and +0x368. Nobody but 0x414340 sets +0x360 back to 0: the 5 stays in the record.
Result StepTheLayer(Kernel::ActorState& actor)
{
    const bool gameMaster = NamedLikeAGameMaster(actor) || (actor.flags78 & GameMasterFlag) == GameMasterFlag ||
                            actor.controlCode == GameMasterControlCode ||
                            actor.controlCode == SecondGameMasterControlCode;
    if (gameMaster)
    {
        if (actor.layer.has_value() && actor.byte360 != GameMasterLayerKind)
            Kernel::ReleaseTheLayer(actor);
        actor.byte360 = GameMasterLayerKind;
    }
    if (actor.layer.has_value() || actor.byte360 < GameMasterLayerKind || actor.byte360 > LastLayerKind)
        return {};
    actor.layer = Kernel::GameMasterLayer{};
    if (actor.byte360 != GameMasterLayerKind)
    {
        // The object of 0x4147c0 has no animation: its +8 is the variant 0..7. It shares the action byte of MuSign's
        // alternate state; the owner's layer type decides which of the two layouts is read.
        constexpr std::uint8_t FirstSiegeRole = 6;
        actor.layer->animation.action = static_cast<std::uint8_t>(actor.byte360 - FirstSiegeRole);
    }
    return {};
}

// 0x414470, the drawing of layer 5: MuSign on the owner's bone 20, with 0x4ec200 at 0.5 and 0x602e90(object, 1, 0, 0).
// With the layer's alpha (+0x178) below 0.01 (or unordered) it does nothing (0x414496); 0x414370 leaves it at 1 and
// only 0x5ffdf0 lowers it, in Chaos Castle with the hero's +0x1a1.
Result DrawMuSign(const Frame& frame, Kernel::GameMasterLayer& layer)
{
    const GameHangingPass& pass = frame.pass;
    if (!(layer.alpha >= DrawnLayerAlpha))
        return {};
    if (pass.carry == nullptr)
        return Core::Unexpected(std::string("0x5ffdf0: falta el buffer global de la pose (0x68b181c)"));
    if (MuSignBone >= pass.body.size())
        return Core::Unexpected(
            std::format("0x4144d6: el hueso {:#x} del cuerpo no existe", static_cast<int>(MuSignBone)));
    const Models::Skeleton* skeleton = pass.models.SkeletonOf(MuSign);
    if (pass.models.Find(MuSign) == nullptr || skeleton == nullptr)
        return Core::Unexpected(std::string("0x414470: falta Skill\\MuSign.bmd (0x12a) de la capa de los GM"));
    DrawnObject object;
    object.model = MuSign;
    // 0x4144d6: 0x4ec110 with the owner's model on its bone 20, translated; +0x93 at 1 keeps the angles.
    object.transform = {.position = frame.BodyPoint(MuSignBone, MuSignOffset), .angles = MuSignAngles, .scale = 1.0f};
    // 0x414555: 0x4ec200 on the action of +0x115 (0). Without actions or with one key it does nothing; between original
    // frames, it draws it as it was left.
    if (pass.originalFrame && layer.animation.action < skeleton->actions.size())
        static_cast<void>(Models::AdvanceAnimation(layer.animation, skeleton->actions[layer.animation.action],
                                                   MuSignSpeed, Models::AnimationScene::Other));
    object.animation = layer.animation;
    // 0x414370: the blendMesh (+0x68) and the hidden mesh (+0x5c) at −1, the blendMesh's light at 1, the per-vertex
    // light (+0xec) on and the alpha at 1. 0x5ffce0 adds the terrain's light, which 0x6002e0 overwrites. The U, the V
    // and the +0x78 that 0x6002e0 compares with 0x80 stay uninitialized (Q-C302): here, at 0 and other than 0x80.
    object.state = {};
    object.color = glm::vec3(1.0f);
    object.vertexLight = true;
    // 0x5ffdff..0x5ffe3b: in maps 18 to 23, with the hero's +0x1a1, the alpha drops by 0.15f and stays stored (once per
    // original frame); below 0.01, 0x5ffdf0 returns without posing and 0x602e90 does not call 0x6002e0.
    if (pass.originalFrame)
        layer.alpha = World::ChaosCastlePoseAlpha(pass.map, pass.heroByte1a1, layer.alpha);
    if (!(layer.alpha >= DrawnLayerAlpha))
        return {};
    object.alpha = layer.alpha;
    // 0x5ffdf0 with the second argument at 1: the pose in the global buffer, without taking the bones to the world.
    Scenes::FaunaDrawCarry& carry = *pass.carry;
    if (auto posed = Scenes::PublishFaunaPose(carry, pass.models, object, false); !posed.HasValue())
        return posed;
    if (auto drawn = DrawItemMeshPass(pass.batch, pass.models, object, pass.frameClock, carry.meshes, -1, BasePass);
        !drawn.HasValue())
        return drawn;
    DrawnObject chrome = object;
    chrome.color = MuSignChromeLight;
    return DrawItemMeshPass(pass.batch, pass.models, chrome, pass.frameClock, carry.meshes, -1, ChromePass);
}

// 0x414820: the owner's x and y, z from bone 20 with the offset (120, 0, 0), the 0x637 atlas without rand or animation.
Result DrawTheSiegeLayer(const Frame& frame)
{
    constexpr glm::vec3 Offset{120.0f, 0.0f, 0.0f};
    const Kernel::ActorState& actor = frame.actor;
    const GameHangingPass& pass = frame.pass;
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::string("0x41494d: falta la tabla de sprites del asedio"));
    if (MuSignBone >= pass.body.size())
        return Core::Unexpected(std::string("0x414848: falta el hueso20 de la capa del asedio"));
    std::uint8_t variant = actor.layer->animation.action;
    const bool leaderRole = actor.byte360 == 6 || actor.byte360 == 7 || actor.byte360 == 8 || actor.byte360 == 12;
    constexpr std::uint8_t Master = 0x80;
    if (leaderRole && actor.guildStatus == Master)
    {
        if (pass.guild == nullptr)
            return Core::Unexpected(std::string("0x41489c: falta la tabla del gremio para la capa del maestro"));
        const auto index = static_cast<std::int16_t>(actor.word1f2);
        const auto unionName = Scenes::UnionNameAt(*pass.guild, index);
        const auto guildName = Scenes::GuildNameAt(*pass.guild, index);
        if (!unionName || !guildName)
            return Core::Unexpected(std::string("0x4148aa: el índice de la capa sale de la tabla del gremio"));
        if (unionName->empty() || *unionName == *guildName)
            variant = static_cast<std::uint8_t>(variant + (actor.byte360 == 12 ? 1 : 3));
    }
    glm::vec3 position = actor.position;
    position.z = frame.BodyPoint(MuSignBone, Offset).z;
    static_cast<void>(pass.sprites->Create(World::AtlasSprite, position, 1.0f, glm::vec3(1.0f), 0.0f, variant));
    return {};
}

// 0x4142f0: with a layer and without +0x78 & 0x4000, it draws it; with +0x360 from 6 to 13, only if 0x4294b0 (the siege
// byte): without it, it draws nothing.
Result DrawTheLayer(const Frame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    if (!actor.layer.has_value() || (actor.flags78 & HiddenAttachmentFlag) == HiddenAttachmentFlag)
        return {};
    if (actor.byte360 > GameMasterLayerKind && actor.byte360 <= LastLayerKind)
    {
        if (!frame.pass.siegeActive)
            return {};
        return DrawTheSiegeLayer(frame);
    }
    return DrawMuSign(frame, *actor.layer);
}

// 0x50dcce..0x50e05d, only with a 0x20a: the back, Blood Castle's event weapon, the GMs' layer and, outside Chaos
// Castle, the wings and Helper02.
Result HangThePlayer(Frame& frame, bool back)
{
    Kernel::ActorState& actor = frame.actor;
    const GameHangingPass& pass = frame.pass;
    std::uint8_t petExcellent = 0;
    if (auto hung = HangTheBack(frame, back, petExcellent); !hung.HasValue())
        return hung;
    // 0x50de63: in Blood Castle (11 to 17) with +0x360, the event's weapon. +0x360 is written by the 9B (0x511150, from
    // 1 to 3) and 0x413fe0 (the GMs' 5, which stays in the record from their first draw).
    if (pass.map >= FirstBloodCastle && pass.map <= LastBloodCastle && actor.byte360 != 0)
    {
        if (auto hung = HangTheEventWeapon(frame, back, petExcellent); !hung.HasValue())
            return hung;
    }
    // 0x50df01 and 0x50df07: the GMs' layer, before the wings.
    if (auto layer = StepTheLayer(actor); !layer.HasValue())
        return layer;
    if (auto drawn = DrawTheLayer(frame); !drawn.HasValue())
        return drawn;
    if (!(pass.map >= 18 && pass.map <= 23))
    {
        if (auto hung = HangWingsAndPet(frame, petExcellent); !hung.HasValue())
            return hung;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawGameHanging(Kernel::ActorState& actor, const GameHangingPass& pass)
{
    // 0x50db57: the rand() of the light of what hangs, even without any item.
    Frame frame{actor, pass, F32(D(static_cast<float>(Mod(pass.random, 30) + 70)) * D(0.01f)), actor.scale};
    // 0x50dcc2: what is not a 0x20a player jumps to 0x50e060, without the back, the event's weapon, the layer or the
    // wings.
    const bool player = actor.type == Tables::PlayerSkeletonModel;
    if (SomethingHangs(actor) && (pass.carry == nullptr || pass.sprites == nullptr))
        return Core::Unexpected<std::string>(
            "0x509340: faltan el buffer global de la pose (0x68b181c) o la tabla de sprites (0x5491e0)");
    if (!std::isfinite(pass.worldClock) || std::abs(pass.worldClock) >= 0x1p40f)
        return Core::Unexpected<std::string>("0x5767b88: el reloj del mundo está fuera del dominio de FSIN");
    // The body bones the parts hang from and their sprites come out of (BodyPoint does not look at them).
    if (SomethingHangs(actor))
    {
        // What is not a 0x20a hangs no wings or pet: only its hands are looked at.
        const std::array<std::uint8_t, 4> bones{actor.hands[0].bone, actor.hands[1].bone,
                                                player ? WingsBone : actor.hands[0].bone,
                                                player ? PetBone : actor.hands[0].bone};
        for (const std::uint8_t bone : bones)
        {
            if (bone >= pass.body.size())
                return Core::Unexpected(
                    std::format("0x509340: el hueso {:#x} del cuerpo no existe", static_cast<int>(bone)));
        }
    }
    const bool back = WeaponsOnTheBack(actor, pass.map);
    if (player)
    {
        if (auto hung = HangThePlayer(frame, back); !hung.HasValue())
            return hung;
    }
    if (back)
        return {};
    // 0x50e060..0x50e0c0: before the hands, except the back and +0x4000, an effect of a 0x20a every 30 steps.
    if (player && pass.originalFrame && (actor.flags78 & 0x4800u) == 0x800u && actor.byte88 == 1 &&
        pass.logicSteps % 30 == 0)
        return Core::Unexpected<std::string>(
            "0x50e0c0: el efecto periódico de la bandera +0x78 & 0x800 no está portado");
    for (std::size_t index = 0; index < actor.hands.size(); ++index)
    {
        if (auto hung = HangTheHand(frame, index); !hung.HasValue())
            return hung;
    }
    return {};
}

Core::Expected<void, std::string> HangFromTheWingsSlot(Kernel::ActorState& actor, const GameHangingPass& pass,
                                                       const glm::vec3& offset, bool placed, float bodyScale)
{
    if (pass.carry == nullptr || pass.sprites == nullptr)
        return Core::Unexpected<std::string>(
            "0x509340: faltan el buffer global de la pose (0x68b181c) o la tabla de sprites (0x5491e0)");
    if (!std::isfinite(pass.worldClock) || std::abs(pass.worldClock) >= 0x1p40f)
        return Core::Unexpected<std::string>("0x5767b88: el reloj del mundo está fuera del dominio de FSIN");
    const Frame frame{actor, pass, 0.0f, bodyScale};
    // 0x50c7e4 and 0x50c8a2: the model is the slot's, signed (MOVSX); 0x509340 with the level, the excellent and the
    // eleventh argument at 0 and the tenth at 1, as in the other calls of 0x50ba30.
    return HangItem(frame, offset, actor.wings, static_cast<std::int16_t>(actor.wings.model), 0, 0, placed);
}
} // namespace Client::Actors::Drawing
