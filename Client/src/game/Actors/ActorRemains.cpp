#include "Actors/ActorRemains.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Terrain/GroundMarks.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Actors
{
namespace
{
// 0x52e841: the ice monster (Monster16.bmd, type 22 of 0x13, in Devias), which breaks into pieces.
constexpr std::uint16_t IceMonsterModel = 0x161;
constexpr int IceShards = 10; // 0x52e85a
// 0x52e88a..0x52e8a5: those that leave nothing.
constexpr std::array<std::uint16_t, 3> CleanModels{0x159, 0x160, 0x164};
// 0x52e8ae: two marks.
constexpr int BloodMarks = 2;
constexpr int OffsetSpan = 100;  // 0x52e8bc
constexpr int OffsetCentre = 50; // 0x52e8c3
constexpr int SizeMask = 3;      // 0x52e926
constexpr int SizeBase = 8;      // 0x52e932
// 0x6b87b8 (0.1f), a float FMUL over the FILD.
constexpr double SizeStep = static_cast<double>(0.1f);
constexpr int Degrees = 360; // 0x52e952
// The type of the two marks (0x52e967): blood, Effect\blood01.tga.
constexpr std::int32_t BloodMark = 0x5e3;
constexpr std::int32_t NoBone = -1;

bool IsClean(std::uint16_t type)
{
    for (const std::uint16_t model : CleanModels)
        if (model == type)
            return true;
    return false;
}

// 0x4ec110(model, +0x120 + bone · 0x30, offset, point, 1): the bone from its own bones times the scale plus the
// shared model's position. Without its own bones (before the first drawing), the matrix at zero (Q-C318).
glm::vec3 BonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset,
                    const ActorRemainsContext& context)
{
    const Math::BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Math::BoneTransform(0.0f);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, context.modelScale,
                                           context.modelOrigin);
}
} // namespace

Core::Expected<void, std::string> LeaveTheRemains(Kernel::ActorState& actor, const ActorRemainsContext& context)
{
    // 0x52e81c..0x52e83b: the DWORD +0x54 of the type's model (MOVSX of WORD +2).
    if (context.skeleton == nullptr)
        return Core::Unexpected(
            std::format("0x52e834: 0x52e810 lee el hueso +0x54 del modelo {:#x}, que no está cargado", actor.type));
    const std::int32_t bone = context.skeleton->headBone;
    if (bone == NoBone)
        return {};
    // 0x52e841..0x52e881: the live byte of c at 0 and ten 0x518cf0(0xca, +0x10, +0x1c, +0xf4, 0, 0, −1, 0, 0, 0, 0).
    if (actor.type == IceMonsterModel)
    {
        actor.alive = false;
        for (int shard = 0; shard < IceShards; ++shard)
            static_cast<void>(
                context.effects.CreateIceShard(actor.position, actor.angles, actor.light, context.random));
        return {};
    }
    if (IsClean(actor.type))
        return {};
    // 0x52e8ab..0x52e975. A negative +0x54, or one outside the own bones already posed, would make 0x4ec110 read
    // other memory: it stops. Unposed (before the first drawing), the matrix at zero (Q-C318).
    const auto index = static_cast<std::size_t>(bone);
    if (bone < 0 || (!actor.bodyPose.empty() && index >= actor.bodyPose.size()))
        return Core::Unexpected(std::format("0x52e91c: el hueso +0x54 ({}) del modelo {:#x} cae fuera de los {} huesos "
                                            "propios",
                                            bone, actor.type, actor.bodyPose.size()));
    for (int mark = 0; mark < BloodMarks; ++mark)
    {
        const int x = context.random.Next() % OffsetSpan - OffsetCentre;
        const int y = context.random.Next() % OffsetSpan - OffsetCentre;
        const glm::vec3 offset{static_cast<float>(x), static_cast<float>(y), 0.0f};
        const glm::vec3 point = BonePoint(actor, index, offset, context);
        // 0x52e921..0x52e963: the side before the turn.
        const auto size =
            static_cast<float>(static_cast<double>((context.random.Next() & SizeMask) + SizeBase) * SizeStep);
        const auto angle = static_cast<float>(context.random.Next() % Degrees);
        const Terrain::GroundMarkRequest request{
            .type = BloodMark, .position = point, .angle = angle, .color = actor.light, .size = size};
        if (auto added = Terrain::AddGroundMark(context.marks, request, context.random); !added.HasValue())
            return Core::Unexpected(std::format("0x52e96c: {}", added.Error()));
    }
    return {};
}
} // namespace Client::Actors
