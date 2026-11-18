#include "Scenes/ActorRemains.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "World/GroundMarks.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x52e841: el monstruo de hielo (Monster16.bmd, el tipo 22 del 0x13, en Devias), que se rompe en pedazos.
constexpr std::uint16_t IceMonsterModel = 0x161;
constexpr int IceShards = 10; // 0x52e85a
// 0x52e88a..0x52e8a5: los que no dejan nada.
constexpr std::array<std::uint16_t, 3> CleanModels{0x159, 0x160, 0x164};
// 0x52e8ae: dos marcas.
constexpr int BloodMarks = 2;
constexpr int OffsetSpan = 100;  // 0x52e8bc
constexpr int OffsetCentre = 50; // 0x52e8c3
constexpr int SizeMask = 3;      // 0x52e926
constexpr int SizeBase = 8;      // 0x52e932
// 0x6b87b8 (0,1f), un FMUL de float sobre el FILD.
constexpr double SizeStep = static_cast<double>(0.1f);
constexpr int Degrees = 360; // 0x52e952
// El tipo de las dos marcas (0x52e967): la sangre, Effect\blood01.tga.
constexpr std::int32_t BloodMark = 0x5e3;
constexpr std::int32_t NoBone = -1;

bool IsClean(std::uint16_t type)
{
    for (const std::uint16_t model : CleanModels)
        if (model == type)
            return true;
    return false;
}

// 0x4ec110(modelo, +0x120 + hueso · 0x30, corrimiento, punto, 1): el hueso de los huesos propios por la escala y más la
// posición del modelo compartido. Sin huesos propios (antes del primer dibujo), la matriz en cero (Q-C318).
glm::vec3 BonePoint(const ActorState& actor, std::size_t bone, const glm::vec3& offset,
                    const ActorRemainsContext& context)
{
    const Render::Models::BoneTransform matrix =
        bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Render::Models::BoneTransform(0.0f);
    return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset,
                                                     context.modelScale, context.modelOrigin);
}
} // namespace

Core::Expected<void, std::string> LeaveTheRemains(ActorState& actor, const ActorRemainsContext& context)
{
    // 0x52e81c..0x52e83b: el DWORD +0x54 del modelo del tipo (MOVSX del WORD +2).
    if (context.skeleton == nullptr)
        return Core::Unexpected(
            std::format("0x52e834: 0x52e810 lee el hueso +0x54 del modelo {:#x}, que no está cargado", actor.type));
    const std::int32_t bone = context.skeleton->headBone;
    if (bone == NoBone)
        return {};
    // 0x52e841..0x52e881: el byte vivo de c en 0 y diez 0x518cf0(0xca, +0x10, +0x1c, +0xf4, 0, 0, −1, 0, 0, 0, 0).
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
    // 0x52e8ab..0x52e975. Un +0x54 negativo, o fuera de los huesos propios ya posados, haría leer a 0x4ec110 otra
    // memoria: corta. Sin posar (antes del primer dibujo), la matriz en cero (Q-C318).
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
        // 0x52e921..0x52e963: el lado antes que el giro.
        const auto size =
            static_cast<float>(static_cast<double>((context.random.Next() & SizeMask) + SizeBase) * SizeStep);
        const auto angle = static_cast<float>(context.random.Next() % Degrees);
        const World::GroundMarkRequest request{
            .type = BloodMark, .position = point, .angle = angle, .color = actor.light, .size = size};
        if (auto added = World::AddGroundMark(context.marks, request, context.random); !added.HasValue())
            return Core::Unexpected(std::format("0x52e96c: {}", added.Error()));
    }
    return {};
}
} // namespace Client::Scenes
