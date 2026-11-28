#include "Monsters/Drawing/MonsterBodyChain.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Maps/MapId.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Data/Models/Model.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Joints/FaunaJointCreation.h"
#include "Effects/KundunEffects.h"
#include "Effects/KundunFallEffects.h"
#include "Monsters/Drawing/MonsterBodyPasses.h"
#include "Actors/Drawing/VertexEffects.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

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
#include <vector>

namespace Client::Monsters::Drawing
{
namespace
{
using Chained = Core::Expected<bool, std::string>;

using Maps::IsKalimaMap;
constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
constexpr std::int32_t Map34 = Maps::CrywolfMap;
constexpr std::uint32_t BaseFlags = 0x2;
// 0x438fe0: the Kalima water, with Water01.. from slot 0x41 plus the frame of 0x7d2d360.
constexpr std::int32_t WaterTexture = 0x41;
constexpr std::uint32_t AdditiveFlags = 0x42;
constexpr std::uint32_t ChromeFlags = 0x44;
constexpr std::uint32_t ShinyFlags = 0x48;
constexpr std::int32_t Chrome01 = 0x5c0;
constexpr std::uint8_t DeathAction = 6;
// 0x60cd74: +0x78 & 0x4000.
constexpr std::uint32_t RiderFlag = 0x4000;
constexpr float PulseRate = std::bit_cast<float>(0x3b03126fu);     // 0.002f (0x6b89bc)
constexpr float SlowPulseRate = std::bit_cast<float>(0x3a9d4952u); // 0.0012f (0x6b8ac4)
using Math::Tenth;                                                 // 0x6b87b8
constexpr float TwoTenths = std::bit_cast<float>(0x3e4ccccdu);     // 0x6b8854
constexpr float ThreeTenths = std::bit_cast<float>(0x3e99999au);   // 0x6b89dc
constexpr float FourTenths = std::bit_cast<float>(0x3ecccccdu);    // 0x6b87b4
using Math::Half;                                                  // 0x6b882c
constexpr float SixTenths = std::bit_cast<float>(0x3f19999au);     // 0x6b8a20
constexpr float SevenTenths = std::bit_cast<float>(0x3f333333u);   // 0x6b8a5c
using Math::Twentieth;                                             // 0x6b8a48
constexpr float EightTenths = std::bit_cast<float>(0x3f4ccccdu);   // 0x6b8a38
constexpr float Ceiling = std::bit_cast<float>(0x3fa66666u);       // 1.3 (0x6b8ac0)

using Result = Core::Expected<void, std::string>;

using Math::D;
using Math::F32;

BodyPassArgs WithTexture(BodyPassArgs args, std::int32_t texture)
{
    args.texture = texture;
    return args;
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

// 0x43ed4a..0x43ed6c and its copies from 0x193 to 0x197: on death (+0x111 at 6) with the alpha below 0.5 (FCOMP with
// C0: a NaN too), 0x4ee770(0, 0x607, 0, 0, 0) with the type's model over the meshes of 0x5ffdf0 in this draw: the
// 0x607/2 (Actors/Drawing/VertexEffects.h), in the original's frames.
Result EmitWhenFading(MonsterBodyFrame& frame, std::uint32_t address)
{
    constexpr std::int32_t FadingParticles = 0x607;
    const Kernel::ActorState& actor = frame.actor;
    if (actor.animation.action != DeathAction || actor.alpha >= Half || !frame.originalFrame)
        return {};
    const MonsterBodyModel& model = frame.model;
    const auto* found = model.models.Find(Models::ModelId{model.object.model});
    if (found == nullptr)
        return {};
    if (auto emitted = Actors::Drawing::EmitVertexEffectsOf(
            *found, model.object.transform.scale, model.meshes, {.type = FadingParticles},
            {.particles = frame.particles, .worldClock = frame.worldClock}, frame.random);
        !emitted.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, emitted.Error()));
    return {};
}

// The ones of 0x4ecc30 in 0x43ed20: the character's, with the texture in +0x58.
BodyPassArgs MeshArgsOf(const Kernel::ActorState& actor)
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

// sin(argument) · factor + base on the x87 (PC53), not rounded to a float.
Core::Expected<double, std::string> WaveOf(double argument, float factor, float base, std::string_view address)
{
    const auto product = Math::Detail::SineProductPC53(argument, factor);
    if (!product)
        return Core::Unexpected(std::format("{}: el seno del reloj no cabe en el perfil PC53", address));
    return *product + D(base);
}

// 0x7d325c8 at (rand() % 8 − 8) · 0.1f (AND 0x80000007 with the sign fix-up: the signed remainder).
Result Shake(MonsterBodyFrame& frame, std::string_view address)
{
    const std::int32_t step = (frame.random.Next() % 8) - 8;
    if (frame.cameraShake == nullptr)
        return Core::Unexpected(std::format("{}: falta el temblor de la cámara (0x7d325c8)", address));
    *frame.cameraShake = F32(static_cast<double>(step) * D(Tenth));
    return {};
}

// 0x439d90 with a monster (the type is not 0x20a): with +0x110 at 0x21 or 0x22, 0x4ee770(6, 0x10a, 1 or 2, 0, 0) with
// the type's model over this draw's meshes (the 0x10b/2 or /3 stones at each corner of mesh 6, in the original's
// frames), then +0x110 at 0 and sound 0x88. 0x439d40 sets those two values when the 1B arrives with 0xc9 or 0xca
// (SettleMonsterFlags, ActorFlags.h); the gates of map 30 use 0 and 1, and 0x42dcb0 uses 3 (ActorState::byte110).
Result StrikeTheGlow(MonsterBodyFrame& frame)
{
    constexpr std::uint8_t BlueStrike = 0x21, GreenStrike = 0x22;
    constexpr std::int32_t StruckMesh = 6, Stones = 0x10a, BlueStones = 1, GreenStones = 2;
    constexpr Audio::SoundId StruckSound = Sound::HitCristalSound;
    Kernel::ActorState& actor = frame.actor;
    if (actor.byte110 != BlueStrike && actor.byte110 != GreenStrike)
        return {};
    if (frame.originalFrame)
    {
        if (frame.effects == nullptr)
            return Core::Unexpected(std::format("0x439dc5: faltan los efectos con modelo para los vértices del tipo "
                                                "{:#x}",
                                                actor.type));
        MonsterBodyModel& model = frame.model;
        const auto* found = model.models.Find(Models::ModelId{model.object.model});
        if (found != nullptr)
        {
            const Actors::Drawing::VertexEffectCall call{
                .mesh = StruckMesh, .type = Stones, .subtype = actor.byte110 == BlueStrike ? BlueStones : GreenStones};
            if (auto emitted = Actors::Drawing::EmitVertexEffectsOf(
                    *found, model.object.transform.scale, model.meshes, call,
                    {.effects = frame.effects, .effectHooks = frame.effectHooks}, frame.random);
                !emitted.HasValue())
                return Core::Unexpected(std::format("0x439dc5: {}", emitted.Error()));
        }
    }
    actor.byte110 = 0;
    RequestBodySound(frame, StruckSound);
    return {};
}

// A piece of Kundun's fall: the type, the subtype, the key (+0x8a), +0x89 (its step's threshold), the point (0x4ec110
// over bone 4 with this offset, in bits) and the call to 0x518cf0.
struct FallingPiece
{
    std::int16_t type = 0;
    std::int32_t subtype = 0;
    std::int16_t key = 0;
    std::uint8_t byte89 = 0;
    std::array<std::uint32_t, 3> offset{};
    std::uint32_t call = 0;
};

// 0x43ee84..0x43f123, in that order.
constexpr std::array<FallingPiece, 8> FallingPieces{{
    {0x124, 3, -130, 3, {0x421c0000u, 0xc0f00000u, 0xbf000000u}, 0x43eeb9}, // (39, −7.5, −0.5)
    {0x11d, 3, -130, 4, {0x41c00000u, 0xc0f00000u, 0x42020000u}, 0x43ef12}, // (24, −7.5, 32.5)
    {0x11e, 3, -130, 5, {0x41c00000u, 0xc1080000u, 0xc2020000u}, 0x43ef6b}, // (24, −8.5, −32.5)
    {0x11f, 2, -130, 6, {0xbf000000u, 0x40800000u, 0x3f000000u}, 0x43efc3}, // (−0.5, 4, 0.5)
    {0x120, 3, -130, 1, {0xc0200000u, 0xc1b00000u, 0x42580000u}, 0x43f01c}, // (−2.5, −22, 54)
    {0x121, 3, -130, 2, {0xc0900000u, 0xc1c40000u, 0xc2540000u}, 0x43f075}, // (−4.5, −24.5, −53)
    {0x122, 4, -10, 2, {0xc3080000u, 0xc3198000u, 0u}, 0x43f0ca},           // (−136, −153.5, 0)
    {0x123, 5, -130, 2, {0xc3070000u, 0xc3190000u, 0u}, 0x43f123},          // (−135, −153, 0)
}};

// 0x43ee84..0x43f14c: the eight pieces (0x518cf0(piece, bone 4's point, Kundun's angles and light, subtype, Kundun as
// owner, key, +0x89, 0, 0, 0)) and the 0x128/2 at its position (0x43f144, without an owner and with the key at −1);
// then 0x43fc04. With the table full, each spawn does nothing and takes no rand(). The point is 0x4ec110's with
// Translate at 1: the scale (+0x68) and origin (+0x6c) 0x5ffdf0 copied to the shared model in this same draw, those of
// `model.object.transform` (MonsterDrawing.cpp publishes them with PlaceModel in the original's frame).
Result ScatterKundun(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    if (frame.effects == nullptr)
        return Core::Unexpected(std::string("0x43eeb9: la caída de Kundun pide la tabla de efectos (0x518cf0)"));
    constexpr std::size_t FallBone = 4; // [esi+0x120] + 0xc0
    if (frame.bones.size() <= FallBone)
        return Core::Unexpected(std::string("0x43ee90: Kundun sin el hueso 4 en su buffer propio (+0x120)"));
    const MonsterBodyModel& model = frame.model;
    const auto matrix = Math::OriginalMatrixOf(frame.bones[FallBone]);
    for (const FallingPiece& piece : FallingPieces)
    {
        const glm::vec3 offset{std::bit_cast<float>(piece.offset[0]), std::bit_cast<float>(piece.offset[1]),
                               std::bit_cast<float>(piece.offset[2])};
        const glm::vec3 point = Math::OriginalTransformPosition(matrix, offset, model.object.transform.scale,
                                                                model.object.transform.position);
        const Kernel::ModelEffectArguments arguments{.type = piece.type,
                                                     .position = point,
                                                     .angles = actor.angles,
                                                     .light = actor.light,
                                                     .subtype = piece.subtype,
                                                     .owner = &actor,
                                                     .key = std::bit_cast<std::uint16_t>(piece.key),
                                                     .byte89 = piece.byte89};
        const auto made = Effects::CreateTheKundunPiece(*frame.effects, arguments, frame.random, frame.terrainHeight);
        if (!made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", piece.call, made.Error()));
    }
    constexpr std::int32_t EchoSubtype = 2;
    const auto called = Effects::CreateTheKundunCall(*frame.effects, {.type = Effects::KundunCallEffect,
                                                                      .position = actor.position,
                                                                      .angles = actor.angles,
                                                                      .light = actor.light,
                                                                      .subtype = EchoSubtype});
    if (!called.HasValue())
        return Core::Unexpected(std::format("0x43f144: {}", called.Error()));
    return {};
}

// 0x43edc1..0x43f493: type 0x192 (Kundun; +0x60 at 100 from its spawn, 0x43a155).
Result DrawKundun(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    const float frameNumber = actor.animation.frame;
    const bool dying = actor.animation.action == DeathAction;
    constexpr std::int32_t Fallen = 0x5a;
    if (dying && frameNumber > std::bit_cast<float>(0x416ccccdu) && actor.dword60 == Fallen)
    {
        // 0x43edff..0x43ee38: sound 0x1dd, +0x60 at 10 and mesh 0 with 2 and texture +0x58; 0x43ee3d..0x43ee7e: the
        // shake; then 0x4ec110 over bone 0xc0/0x30 and the 0x124, 0x11d and 0x11e particles of 0x518cf0. The original
        // draws once per frame, after 0 to N steps (ADR 0005): between original frames (a projected copy of Kundun)
        // MuRet draws the mesh, but does not shake, drop the pieces or take their rand(), like the storm's creators.
        RequestBodySound(frame, Audio::SoundId{0x1dd});
        actor.dword60 = 10;
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, MeshArgsOf(actor)); !drawn.HasValue())
            return drawn;
        if (!frame.originalFrame)
            return {};
        if (auto shaken = Shake(frame, "0x43ee7e"); !shaken.HasValue())
            return shaken;
        return ScatterKundun(frame);
    }
    if (dying && frameNumber >= 8.0f && actor.dword60 >= 100)
    {
        // 0x43f182: +0x60 is not 0x5a, sound 0x1de; 0x43f195: +0x60 at 0x5a; 0x43f1a6..0x43f243: 0x56 rounds, each with
        // its rand() % 10, the angles (−10, 0, (r + 10) · i) and the point (x, y, z + 200), both on the stack, which
        // receive the 0x613/3 and the 0x6ee/3 (0x530860(type, point, point, angles, 3, no owner, 50, 0, 0, 0, −1)).
        // Then the shake (0x43f249..0x43f269) and the body, which already has +0x60 at 0x5a. As in the 0x43edff branch,
        // between original frames (the projected copy of Kundun, with +0x60 at 0x5a) MuRet draws the body, but does not
        // release the rays, shake or take their rand(): the original does it once per frame.
        RequestBodySound(frame, Audio::SoundId{0x1de});
        actor.dword60 = Fallen;
        if (frame.originalFrame)
        {
            constexpr int Rays = 0x56;             // 0x43f23d
            constexpr std::int32_t Spread = 10;    // 0x43f1ba
            constexpr double SpreadBase = 10.0;    // 0x6b87a8
            constexpr double Rise = 200.0;         // 0x6b8928
            constexpr float Tilt = -10.0f;         // 0x43f1a6: 0xc1200000
            constexpr float RayWidth = 50.0f;      // 0x43f1cc: 0x42480000
            constexpr std::int32_t RaySubtype = 3; // 0x43f1d9
            constexpr std::array<std::int32_t, 2> RayJoints{0x613, 0x6ee};
            glm::vec3 angles{Tilt, 0.0f, 0.0f};
            glm::vec3 point{};
            for (int ray = 0; ray < Rays; ++ray)
            {
                angles.x = Tilt;
                angles.y = 0.0f;
                const auto spread = static_cast<std::int32_t>(frame.random.Next() % Spread);
                // Without the table, it cuts at the first 0x530860, after the rand() of 0x43f1b4.
                if (frame.joints.empty())
                    return Core::Unexpected(
                        std::string("0x43f20d: falta la tabla de joints (0x530860) para Kundun al morir"));
                point = {actor.position.x, actor.position.y, F32(D(actor.position.z) + Rise)};
                angles.z = F32((static_cast<double>(spread) + SpreadBase) * static_cast<double>(ray));
                for (const std::int32_t type : RayJoints)
                {
                    const Kernel::JointRequest request{.type = type,
                                                       .firstPosition = point,
                                                       .secondPosition = point,
                                                       .angles = angles,
                                                       .subtype = RaySubtype,
                                                       .ownerContext = nullptr,
                                                       .scale = RayWidth,
                                                       .key = 0,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
                    const auto created = Joints::CreateFaunaJoint(frame.joints, request, frame.random);
                    if (!created.HasValue())
                        return Core::Unexpected(
                            std::format("{}: {}", type == RayJoints[0] ? "0x43f20d" : "0x43f234", created.Error()));
                }
            }
            if (auto shaken = Shake(frame, "0x43f269"); !shaken.HasValue())
                return shaken;
        }
    }
    if (actor.dword60 < Fallen)
        return {};
    // 0x43f279..0x43f395: the alpha stored at 1, RenderBody(2) and (0.4, 0.4, 0.3) with meshes 1 to 6 chromed.
    actor.alpha = 1.0f;
    if (auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
        return drawn;
    model.object.color = {FourTenths, FourTenths, ThreeTenths};
    const BodyPassArgs args = MeshArgsOf(actor);
    if (auto drawn = DrawMeshes(model, {1, 2, 3, 4, 5, 6}, ChromeFlags, args); !drawn.HasValue())
        return drawn;
    // 0x43f39a..0x43f48e: in action 5 with the frame in (3.5, 6) or in (8, 11), meshes 1, 2, 3 and 5 additive.
    const bool swinging = (frameNumber > 3.5f && frameNumber < 6.0f) || (frameNumber > 8.0f && frameNumber < 11.0f);
    if (actor.animation.action == 5 && swinging)
        return DrawMeshes(model, {1, 2, 3, 5}, AdditiveFlags, args);
    return {};
}

// 0x43f8f9..0x43fb99: type 0x197, between 0x4ecc10 and 0x4ecc20.
Result DrawThe197(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    const bool dark = actor.subtype == 9;
    // 0x43f945..0x43f9d0: meshes 0, 1, 3 and 5 with 2, without the blendMesh.
    BodyPassArgs args = MeshArgsOf(actor);
    args.blendMesh = -1;
    if (auto drawn = DrawMeshes(model, {0, 1, 3, 5}, BaseFlags, args); !drawn.HasValue())
        return drawn;
    // 0x43f9d5..0x43fa23: v = sin(T · 0.002f) · 0.3f + 0.7f; with subtype 9, +0x68 at 0.5 and (v, v · 0.4, v · 0.4);
    // otherwise, +0x68 at 1 and (v · 0.3, v · 0.6, v).
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
    // 0x43fa26..0x43fa6f: meshes 2 and 4 with 0x440 (additive, with the wave of 0x4ecc30, 0x4ed775) and their
    // blendMesh, with the light of +0x68.
    constexpr std::uint32_t WavingGlow = 0x440;
    args = MeshArgsOf(actor);
    for (const std::int16_t mesh : {std::int16_t{2}, std::int16_t{4}})
    {
        args.blendMesh = mesh;
        if (auto drawn = DrawMeshPass(model, mesh, WavingGlow, args); !drawn.HasValue())
            return drawn;
    }
    args.blendMesh = -1;
    if (dark)
    {
        // 0x43fa7a..0x43fae8: with 1, meshes 0, 1 and 3 with 0x48, without the blendMesh.
        model.object.color = glm::vec3(1.0f);
        if (auto drawn = DrawMeshes(model, {0, 1, 3}, ShinyFlags, args); !drawn.HasValue())
            return drawn;
    }
    else
    {
        // 0x43faea..0x43fb47: meshes 2 and 4 with 2, their blendMesh and the light at sin(T · 0.002f) · 0.3f + 0.7f (a
        // float).
        const auto light = WaveOf(D(frame.worldClock) * D(PulseRate), ThreeTenths, SevenTenths, "0x43fb00");
        if (!light.HasValue())
            return Core::Unexpected(light.Error());
        BodyPassArgs lit = args;
        lit.light = F32(light.Value());
        for (const std::int16_t mesh : {std::int16_t{2}, std::int16_t{4}})
        {
            lit.blendMesh = mesh;
            if (auto drawn = DrawMeshPass(model, mesh, BaseFlags, lit); !drawn.HasValue())
                return drawn;
        }
    }
    // 0x43fb4c..0x43fb8b: the light at sin(T · 0.002f) · 0.1f + 0.05f (a float), stored in +0x68, and mesh 6 with 2 and
    // blendMesh 6.
    const auto glow = WaveOf(D(frame.worldClock) * D(PulseRate), Tenth, Twentieth, "0x43fb62");
    if (!glow.HasValue())
        return Core::Unexpected(glow.Error());
    actor.blendMeshLight = F32(glow.Value());
    BodyPassArgs last = MeshArgsOf(actor);
    last.blendMesh = 6;
    if (auto drawn = DrawMeshPass(model, 6, BaseFlags, last); !drawn.HasValue())
        return drawn;
    // 0x43fb97: 0x439d90.
    return StrikeTheGlow(frame);
}

// 0x43ed20 with the types 0x191 to 0x197, up to 0x43fc04.
Result DrawTheLateChainBody(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    switch (actor.type)
    {
    case 0x191:
    {
        if (auto cut = EmitWhenFading(frame, 0x43ed6c); !cut.HasValue())
            return cut;
        // 0x43ed71..0x43edb7: RenderBody(2) without the hidden mesh and mesh 0 with 0x48 and Chrome01.
        BodyPassArgs args = ActorPassArgs(actor);
        args.hiddenMesh = -1;
        if (auto drawn = DrawBodyPass(model, BaseFlags, args); !drawn.HasValue())
            return drawn;
        return DrawMeshPass(model, 0, ShinyFlags, WithTexture(ActorPassArgs(actor), Chrome01));
    }
    case 0x192:
        return DrawKundun(frame);
    case 0x193:
        if (auto cut = EmitWhenFading(frame, 0x43f514); !cut.HasValue())
            return cut;
        // 0x43f519..0x43f575: RenderBody(2) and (0.5, 0.1, 0) with mesh 0 chromed.
        if (auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
            return drawn;
        model.object.color = {Half, Tenth, 0.0f};
        return DrawMeshPass(model, 0, ChromeFlags, MeshArgsOf(actor));
    case 0x194:
    {
        if (auto cut = EmitWhenFading(frame, 0x43f5b9); !cut.HasValue())
            return cut;
        // 0x43f5be..0x43f63e: RenderBody(2) without the hidden mesh and (0.25, 0.2, 0.1) with meshes 0 and 2 chromed.
        BodyPassArgs args = ActorPassArgs(actor);
        args.hiddenMesh = -1;
        if (auto drawn = DrawBodyPass(model, BaseFlags, args); !drawn.HasValue())
            return drawn;
        model.object.color = {0.25f, TwoTenths, Tenth};
        return DrawMeshes(model, {0, 2}, ChromeFlags, MeshArgsOf(actor));
    }
    case 0x195:
    {
        if (auto cut = EmitWhenFading(frame, 0x43f682); !cut.HasValue())
            return cut;
        // 0x43f687..0x43f738: with subtype 9, (0.3, 0.1, 0.1); mesh 0 additive and meshes 1 to 3 with 2.
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
            // 0x43f746..0x43f7bc: (1, 0.6, 0.3) with mesh 1 chromed at alpha 0.5, with 1 mesh 1 with 0x240 and then (1,
            // 0.6, 0.1).
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
            // 0x43f7ca: (0.1, 0.6, 1).
            model.object.color = {Tenth, SixTenths, 1.0f};
        }
        // 0x43f7df..0x43f836: meshes 0 and 2 chromed and, with subtype 9, 0x439d90.
        if (auto drawn = DrawMeshes(model, {0, 2}, ChromeFlags, args); !drawn.HasValue())
            return drawn;
        return dark ? StrikeTheGlow(frame) : Result{};
    }
    case 0x196:
    {
        if (auto cut = EmitWhenFading(frame, 0x43f87d); !cut.HasValue())
            return cut;
        // 0x43f882..0x43f8ef: RenderBody(2) and (0.5, sin(T · 0.002f) · 0.3f + 0.7f, 0.5) with mesh 0 chromed at alpha
        // 0.8.
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
        if (auto cut = EmitWhenFading(frame, 0x43f933); !cut.HasValue())
            return cut;
        return DrawThe197(frame);
    }
}

// 0x60cd60(model, object, `flags`, 1, `scale`) with a monster or an NPC: without +0x78 & 0x4000, the model's +0x44 at 0
// (no per-vertex light), 0x4ebc80 over its own bones with 0x6c881c at `scale`, which leaves those meshes in 0x5786f9c
// for the rest of 0x6002e0, and RenderBody(flags) with the character's. 0xc5 (0x60cdd9) does not get here.
Result DrawTheWiderShell(MonsterBodyFrame& frame, std::uint32_t flags, float scale)
{
    if ((frame.actor.flags78 & RiderFlag) == RiderFlag)
        return {};
    MonsterBodyModel& model = frame.model;
    model.object.vertexLight = false;
    model.rescaled = Actors::Drawing::SkinObjectMeshes(model.models, model.object, frame.bones, scale);
    model.meshes = model.rescaled;
    return DrawBodyPass(model, flags, ActorPassArgs(frame.actor));
}

// 0x43ed20: the types 0x191 to 0x197 (no spawn gives 0x11d to 0x121 or 0x124). After each case, 0x43fc04: with +0x78 &
// 0x100 or & 8, v = sin(T · 0.001f) · 0.2f + 0.5f on the x87, the color (v · 0.1, v · 0.3, v · 0.6) or (v · 0.1, v ·
// 0.6, v · 0.3) and 0x60cd60(…, 0x44, 1, 1.3f). Returns 1.
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

// 0x438fe0: Kalima's 0x173, the 0xc5 and Kalima's low types; otherwise, 0x43ed20.
Chained DrawTheKalimaCase(MonsterBodyFrame& frame)
{
    MonsterBodyModel& model = frame.model;
    const Kernel::ActorState& actor = frame.actor;
    const std::uint16_t type = actor.type;
    if (type == 0x173 && IsKalimaMap(frame.map))
    {
        // 0x43900a..0x439662: with the color at 0, RenderBody(2); then the color at 1.
        model.object.color = glm::vec3(0.0f);
        const auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor));
        model.object.color = glm::vec3(1.0f);
        return Drawn(drawn);
    }
    if (type == 0xc5)
    {
        // 0x43904c..0x439091: (1, 0.1, 0.1) with subtype 1, otherwise 1; RenderBody(2) without a hidden mesh.
        model.object.color = actor.subtype == 1 ? glm::vec3{1.0f, Tenth, Tenth} : glm::vec3(1.0f);
        BodyPassArgs args = ActorPassArgs(actor);
        args.hiddenMesh = -1;
        return Drawn(DrawBodyPass(model, BaseFlags, args));
    }
    if (!IsKalimaMap(frame.map) || type >= 0xa0)
        return DrawTheLateChainCase(frame);
    // 0x4394a0..0x4394c4: RenderBody(2).
    if (auto drawn = DrawBodyPass(model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    constexpr std::array<std::uint16_t, 18> Dry{2,    4,    0xc,  0xe,  0xf,  0x12, 0x14, 0x15, 0x1b,
                                                0x1d, 0x1e, 0x1f, 0x20, 0x29, 0x2b, 0x34, 0x36, 0x37};
    if (type <= 0x42 && !OneOf(type, Dry))
    {
        // 0x439570..0x4395ba: the color at sin(T · 0.002f) · 0.1f + 0.3f (FST to all three) and RenderBody(2) with the
        // alpha at 0.3 and the water.
        const auto product = Math::Detail::SineProductPC53(D(frame.worldClock) * D(PulseRate), Tenth);
        if (!product)
            return Core::Unexpected<std::string>("0x43957c: el agua de Kalima no cabe en el perfil PC53");
        model.object.color = glm::vec3(static_cast<float>(*product + D(ThreeTenths)));
        BodyPassArgs args = ActorPassArgs(actor);
        args.alpha = ThreeTenths;
        args.texture = WaterTexture + static_cast<std::int32_t>(frame.cloth.waterTextureFrame);
        if (auto drawn = DrawBodyPass(model, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
    }
    // 0x4395bf..0x439662: no monster spawn gives 0x22, 0xf, 0x1d or 0x20.
    if (type == 0x22 || type == 0xf || type == 0x1d || type == 0x20)
        return Cut("0x4395bf", "el pase de después del cuerpo de Kalima de 0x438fe0", type);
    return true;
}

Result EmitCastleVertices(MonsterBodyFrame& frame, std::int32_t type)
{
    if (frame.effects == nullptr)
        return Core::Unexpected(std::string("0x4ee770: falta el pool de efectos del monstruo del asedio"));
    return Actors::Drawing::EmitVertexEffects(frame.model.models, frame.model.object, frame.model.meshes, 0, type, 0,
                                              *frame.effects, frame.effectHooks, frame.random);
}

Chained DrawCastleGuard(MonsterBodyFrame& frame)
{
    if (!frame.siegeActive)
        return true;
    Kernel::ActorState& actor = frame.actor;
    if (actor.animation.action == DeathAction)
    {
        actor.alive = false;
        RequestBodySound(frame, Audio::SoundId{0x1e8});
        return Drawn(EmitCastleVertices(frame, 0x10a));
    }
    if (auto drawn = DrawBodyPass(frame.model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    BodyPassArgs chrome = WithTexture(ActorPassArgs(actor), Chrome01);
    chrome.blendMesh = 0;
    frame.model.object.color = glm::vec3(ThreeTenths);
    if (auto drawn = DrawBodyPass(frame.model, ChromeFlags, chrome); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    frame.model.object.color = {TwoTenths, TwoTenths, Half};
    if (auto drawn = DrawBodyPass(frame.model, ShinyFlags, chrome); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    frame.model.object.color = glm::vec3(1.0f);
    return true;
}

Chained DrawCastleLife(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    actor.blendMeshLight = F32(static_cast<double>(actor.byte93) * D(Tenth));
    model.object.color = glm::vec3(actor.blendMeshLight);
    const BodyPassArgs args{};
    if (actor.byte93 >= 2)
    {
        if (auto drawn = DrawMeshPass(model, 0, BaseFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
    }
    if (actor.byte93 >= 3)
    {
        if (auto drawn = DrawMeshPass(model, 2, AdditiveFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
    }
    if (actor.byte93 >= 4)
    {
        model.object.color = glm::vec3(F32(D(actor.blendMeshLight) * D(Half)));
        if (auto drawn = DrawMeshPass(model, 1, AdditiveFlags, args); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
    }
    if (actor.byte93 < 5)
        return true;
    BodyPassArgs scroll;
    scroll.blendMesh = 3;
    scroll.uv.x = F32(D(frame.worldClock) * D(std::bit_cast<float>(0x38d1b717u)));
    model.object.color = glm::vec3(Half);
    if (auto drawn = DrawMeshPass(model, 3, AdditiveFlags, scroll); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    model.object.color = glm::vec3(TwoTenths);
    return Drawn(DrawMeshPass(model, 4, AdditiveFlags, scroll));
}

Result DrawCastleStoneAt(MonsterBodyFrame& frame, std::size_t bone)
{
    if (bone >= frame.bones.size())
        return Core::Unexpected(std::format("0x42e4cf: falta el hueso {} de la catapulta", bone));
    MonsterBodyModel& model = frame.model;
    constexpr std::uint16_t StoneModel = 0x12b; // 5769ad8+105a0 = modelo12b, stridee0.
    const auto* skeleton = model.models.SkeletonOf(Models::ModelId{StoneModel});
    if (skeleton == nullptr)
        return Core::Unexpected(std::string("0x42e4fc: falta el esqueleto del proyectil 0x12b"));
    Actors::Drawing::DrawnObject stone = model.object;
    stone.model = StoneModel;
    stone.color = model.preludeColor;
    stone.unlitMesh.reset();
    stone.animation = {};
    stone.transform.position = Math::OriginalTransformPosition(
        Math::OriginalMatrixOf(frame.bones[bone]), {}, model.object.transform.scale, model.object.transform.position);
    stone.transform.scale = Ceiling;
    stone.transform.lockedRootHeight = 0.0f;
    std::vector<Math::BoneTransform> posed(skeleton->bones.size(), Math::BoneTransform(1.0f));
    if (!Models::PoseSkeleton(*skeleton, stone.animation, stone.transform, {}, posed))
        return Core::Unexpected(std::string("0x42e4fc: no se pudo posar el proyectil 0x12b"));
    model.rescaled = Actors::Drawing::SkinObjectMeshes(model.models, stone, posed, model.boneScale);
    model.meshes = model.rescaled;
    MonsterBodyModel projectile{.batch = model.batch,
                                .models = model.models,
                                .object = stone,
                                .meshes = model.meshes,
                                .frameClock = model.frameClock,
                                .worldClock = model.worldClock};
    return DrawBodyPass(projectile, BaseFlags, ActorPassArgs(frame.actor));
}

Chained DrawCastleCatapult(MonsterBodyFrame& frame)
{
    if (auto drawn = DrawBodyPass(frame.model, BaseFlags, ActorPassArgs(frame.actor)); !drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    const float key = frame.actor.animation.frame;
    // 0x42e484/0x42e499: C0 includes NaN; with NaN and action != 0 only bone 12's stone is drawn.
    if (frame.actor.animation.action == 0 || !(key >= 2.0f) || key >= 22.0f)
    {
        if (auto drawn = DrawCastleStoneAt(frame, 12); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
    }
    if (frame.actor.animation.action == 1 && key > 13.0f && !(key >= 22.0f))
        return Drawn(DrawCastleStoneAt(frame, 44));
    return true;
}

Chained DrawCastleMonster(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    switch (actor.type)
    {
    case 0x19b:
        if (actor.animation.action == DeathAction)
        {
            actor.alive = false;
            RequestBodySound(frame, Audio::SoundId{0x87});
            if (auto emitted = EmitCastleVertices(frame, 0x131); !emitted.HasValue())
                return Core::Unexpected(emitted.Error());
        }
        else if (auto drawn = DrawBodyPass(frame.model, BaseFlags, ActorPassArgs(actor)); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        return false; // 42e6f9 y42e729: BL permanece en0.
    case 0x19c:
        return DrawCastleGuard(frame);
    case 0x1a8:
        return DrawCastleLife(frame);
    case 0x1f8:
        return DrawCastleCatapult(frame);
    case 0x1ff:
    {
        frame.model.object.color = {ThreeTenths, ThreeTenths, 1.0f};
        const auto water = static_cast<std::int32_t>(frame.cloth.waterTextureFrame) + WaterTexture;
        if (auto drawn = DrawBodyPass(frame.model, AdditiveFlags, WithTexture(ActorPassArgs(actor), water));
            !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        frame.model.object.color = {ThreeTenths, 1.0f, ThreeTenths};
        const auto clock = Math::OriginalFtolLow(D(frame.worldClock) * D(std::bit_cast<float>(0x3c23d70au)));
        if (!clock)
            return Core::Unexpected(std::string("0x42e682: el reloj del agua no cabe en 0x69fd3c"));
        return Drawn(
            DrawBodyPass(frame.model, AdditiveFlags, WithTexture(ActorPassArgs(actor), *clock % 32 + WaterTexture)));
    }
    default:
        return false;
    }
}

// 0x42cc10: only in map 30. DrawCastleSiegeBody draws the siege types; 0x42e3d0, the monsters'.
Chained DrawTheCastleCase(MonsterBodyFrame& frame)
{
    if (frame.map != CastleMap)
        return false;
    MonsterBodyModel& model = frame.model;
    const Kernel::ActorState& actor = frame.actor;
    Actors::Drawing::DrawnObject object = model.object;
    const BodyPassArgs args = ActorPassArgs(actor);
    object.alpha = args.alpha;
    object.state.blendMesh = static_cast<std::int16_t>(args.blendMesh);
    object.state.light = args.light;
    object.state.uv = args.uv;
    object.state.hiddenMesh = static_cast<std::int16_t>(args.hiddenMesh);
    object.retainedMeshes = model.meshes;
    const auto siege = Actors::Drawing::DrawCastleSiegeBody(
        model.batch, model.models, object,
        Actors::Drawing::CastleSiegeBodyPass{.type = static_cast<std::int16_t>(actor.type),
                                             .field04 = actor.subtype,
                                             .siegeActive = frame.siegeActive,
                                             .worldClock = frame.worldClock},
        model.frameClock);
    if (!siege.HasValue())
        return Core::Unexpected(siege.Error());
    if (siege.Value())
        return true;
    return DrawCastleMonster(frame);
}

// The f of 0x442ee9..0x442f24 and 0x4430c9..0x443104: min(sin(T · 0.0012f) · 0.8f + 1.3f, 1.3f) on the x87 (FCOM and
// TEST 0x41: a NaN stays).
Core::Expected<double, std::string> GlowOf(float worldClock, std::string_view address)
{
    const auto wave = WaveOf(D(worldClock) * D(SlowPulseRate), EightTenths, Ceiling, address);
    if (!wave.HasValue())
        return wave;
    return wave.Value() > D(Ceiling) ? D(Ceiling) : wave.Value();
}

// (ftol(T) % 10000) · 0.0002f in a float (CDQ and IDIV: the remainder with the dividend's sign): the U and V of
// 0x442e10.
Core::Expected<float, std::string> DriftOf(float worldClock, std::string_view address)
{
    const auto clock = Math::OriginalFtolLow(D(worldClock));
    if (!clock)
        return Core::Unexpected(std::format("{}: el reloj del mundo no cabe en 0x69fd3c", address));
    return F32(static_cast<double>(*clock % 10000) * D(std::bit_cast<float>(0x3951b717u)));
}

// 0x442eda..0x442fe4: type 0x1a4, without RenderBody.
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

// 0x4430a9..0x4432a2: type 0x1a6; with the fourth argument, the color at 1 and the chromed passes with Chrome01.
Result DrawThe1a6(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    const bool special = frame.special != 0;
    const BodyPassArgs args = ActorPassArgs(actor);
    const BodyPassArgs chromed = WithTexture(args, Chrome01);
    if (special)
        model.object.color = glm::vec3(1.0f);
    const auto glow = GlowOf(frame.worldClock, "0x4430e1");
    if (!glow.HasValue())
        return Core::Unexpected(glow.Error());
    // 0x44310a..0x44312e: with +0x88 at 1, the red times f · 0.1 (on the x87), the green times f and the blue times f ·
    // 0.1 stored in a float.
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
    // 0x4431c8..0x44320e: with +0x88 at 0xff and the saved color, mesh 0 with 2 at alpha sin(T · 0.0012f) · 0.2f + 0.8f
    // (a float).
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
    // 0x44323e..0x44329b: with +0x88 at 2, mesh 2 with blendMesh 2 and the light at 0.5; with the fourth argument,
    // chromed, and +0xc in the model's +0x68 (the scale 0x5ffdf0 copied to it from the same +0xc: it does not change).
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

// 0x4432b0..0x4433d0: type 0x1a7; with the fourth argument, the color at 1 and the chromed passes with Chrome01.
Result DrawThe1a7(MonsterBodyFrame& frame)
{
    MonsterBodyModel& model = frame.model;
    const bool special = frame.special != 0;
    const BodyPassArgs args = ActorPassArgs(frame.actor);
    const BodyPassArgs chromed = WithTexture(args, Chrome01);
    if (special)
        model.object.color = glm::vec3(1.0f);
    // 0x4432d0..0x443353: the color times (0.5, 0.6, 0.8) and mesh 0; 0x443358..0x4433b7: (0.5, 0.8, 0.6) and mesh 1.
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

// 0x4433de..0x443571: type 0x1a9.
Result DrawThe1a9(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
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
    // 0x443459..0x443519: (1, 0.5, 0) with mesh 4 chromed; (0.75, 0.65, 0.5) with meshes 0, 1 and 3.
    const glm::vec3 saved = model.object.color;
    const BodyPassArgs args = ActorPassArgs(actor);
    model.object.color = {1.0f, Half, 0.0f};
    if (auto drawn = DrawMeshPass(model, 4, ChromeFlags, args); !drawn.HasValue())
        return drawn;
    model.object.color = {0.75f, std::bit_cast<float>(0x3f266666u), Half};
    if (auto drawn = DrawMeshes(model, {0, 1, 3}, ChromeFlags, args); !drawn.HasValue())
        return drawn;
    // 0x44351e..0x443558: mesh 0 with 2, blendMesh 0, the light at sin(T · 0.0012f) · 0.3f + 0.6f (a float) and texture
    // 0x63a; then the saved color.
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

// 0x4403b0 → 0x442e10, in every map (no spawn gives the map 31 types of 0x4403b0): the monsters of 0x440530, between
// 0x4ecc10 and 0x4ecc20; returns 1.
Chained DrawThe4403b0Case(MonsterBodyFrame& frame)
{
    MonsterBodyModel& model = frame.model;
    const Kernel::ActorState& actor = frame.actor;
    switch (actor.type)
    {
    case 0x1a3:
    {
        // 0x442e3d..0x442ecc: RenderBody(2) without mesh 1 and, with (0.6, 0.4, 0.4), mesh 1; then the saved color.
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
        // 0x442fe9..0x443095: RenderBody(2) without mesh 0; with 1, mesh 0 without the blendMesh and with the light at
        // 1, and with (0.8, 0.6, 1), chromed; then the saved color.
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

// −(ftol(T)) % 10000 (NEG, CDQ and IDIV: the remainder with the dividend's sign) times `rate` in a float: the U and V
// that scroll in 0x428df0.
Core::Expected<float, std::string> ScrollOf(float worldClock, float rate, std::string_view address)
{
    const auto clock = Math::OriginalFtolLow(D(worldClock));
    if (!clock)
        return Core::Unexpected(std::format("{}: el reloj del mundo no cabe en 0x69fd3c", address));
    const auto negated = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(*clock));
    return static_cast<float>(static_cast<double>(negated % 10000) * D(rate));
}

// 0x428df0 (from 0x425d90, in every map): types 0x1bb, 0x1b8 and 0x1b7. Between 0x4ecc10 and 0x4ecc20, single passes
// with the model's +0x88 at 0 or 1 and the light times the object's; returns 1.
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
        // 0x428e1b..0x428f2d: mesh 0 with 1 and then with (1, 0, 0) and 0x46, with +0x88 at 0; mesh 1 with 1; mesh 2
        // with +0x88 at 1, with 1 and then with (0.5, 0, 0) and 0x46.
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
        // 0x428f39..0x42903b: with 0.9, meshes 0 to 2; with +0x88 at 0, mesh 3 with the U and V at the clock times
        // 0.0006f (0x6b8a50) and mesh 3 with 0x46.
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
    // 0x429047..0x429100: type 0x1b7 with 0.9 and +0x88 at 0: mesh 0 with the U and V at the clock times 0.0003f
    // (0x6b8a4c) and with 0x46; with +0x88 at 0xff, mesh 1.
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

// 0x425d90: 0x425070 (map 33 or 54, without using what it returns) and 0x428df0 in every map.
Chained DrawThe425d90Case(MonsterBodyFrame& frame)
{
    return DrawTheMeshCase(frame);
}

// 0x4339d0 (from 0x4306a0, in map 34): each mesh of the model (+0x24) with 2 and then with `flags` and `texture`.
Chained DrawEveryMeshTwice(MonsterBodyFrame& frame, std::uint32_t flags, std::int32_t texture)
{
    MonsterBodyModel& model = frame.model;
    const auto* found = model.models.Find(Models::ModelId{frame.actor.type});
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

// 0x433df9 and 0x433b03
Chained DrawWithoutMesh5(MonsterBodyFrame& frame)
{
    BodyPassArgs args = ActorPassArgs(frame.actor);
    args.hiddenMesh = 5;
    return Drawn(DrawBodyPass(frame.model, BaseFlags, args));
}

// 0x433b2f..0x433c6e: type 0x1af, between 0x4ecc10 and 0x4ecc20. Nobody reads the accumulator 0x723c14 (+0.1f per draw
// and back to 0 past 1) outside its own count: MuRet does not keep it. Each mesh of the model (+0x24, signed) with 2,
// the U stored in +0x6c at (ftol(T) % 10000) · 0.0005f (0x6b8a68) and +0x64 at −1; before mesh 2, the stored alpha
// capped at 0.3, +0x64 at −2 and the model's +0x88 at 2, which nobody resets to 0xff: 0x602365 sees it in this draw,
// and in the next ones only the outline of 0x5ffdf0 (no light or texture) and mesh 2 here, which sets it again. At the
// end, the stored alpha at 1.
Result DrawThe1af(MonsterBodyFrame& frame)
{
    Kernel::ActorState& actor = frame.actor;
    MonsterBodyModel& model = frame.model;
    const auto* found = model.models.Find(Models::ModelId{actor.type});
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
        const auto clock = Math::OriginalFtolLow(D(frame.worldClock));
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

// 0x4306a0: only in map 34 (0x4303c0); no spawn gives its world types; the monsters', 0x4339d0 with its table (0x434138
// and 0x43410c), which returns 1 with all of them.
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
        // 0x4339fa: with 1, RenderBody(2) without mesh 5.
        model.object.color = glm::vec3(1.0f);
        return DrawWithoutMesh5(frame);
    case 0x1ab:
        // 0x433a4b: with 1, each mesh with 2 and with 0x240 (chrome 2, additive) and 0x6b1.
        model.object.color = glm::vec3(1.0f);
        return DrawEveryMeshTwice(frame, 0x240, 0x6b1);
    case 0x1ad:
        // 0x433b03: RenderBody(2) without mesh 5, with the color it brings.
        return DrawWithoutMesh5(frame);
    case 0x1af:
        return Drawn(DrawThe1af(frame));
    case 0x1b4:
    case 0x1b1:
        // 0x433c75 and 0x433d37: without the fourth argument, 0x433df9; with it, (0.5, 0.5, 0.6) or (0.5, 0.5, 0.8) and
        // each mesh with 2 and with 0x44 and Chrome01.
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
        // 0x433e25, 0x433edc, 0x433f95 and 0x43404c: with (0.6, 0.8, 0.6), each mesh with 2 and with 0x44 and Chrome01.
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
    // 0x600f18: 0x4403b0 and 0x425d90 receive whether the fourth argument is not 0, and 0x4306a0 the fourth argument;
    // 0x42f170 (0x42ee20: map 35) calls 0x42fa20, which returns 0.
    const auto third = DrawThe4403b0Case(frame);
    if (!third.HasValue() || third.Value())
        return third;
    const auto fifth = DrawThe425d90Case(frame);
    if (!fifth.HasValue() || fifth.Value())
        return fifth;
    return DrawTheMap34Case(frame);
}
} // namespace Client::Monsters::Drawing
