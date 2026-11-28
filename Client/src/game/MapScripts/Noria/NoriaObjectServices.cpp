#include "MapScripts/Noria/NoriaObjectServices.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/PoseBuffer.h"
#include "Math/FloatConstants.h"
#include "Math/OriginalRotation.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "WorldObjects/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::MapScripts::Noria
{
namespace
{
using Math::Hundredth;
constexpr float Red = std::bit_cast<float>(0x3ecccccdu);
constexpr float Green = std::bit_cast<float>(0x3f333333u);
constexpr float FairyGreen = std::bit_cast<float>(0x3f4ccccdu);
constexpr float FairyTurnScale = std::bit_cast<float>(0x3dcccccdu);
constexpr std::uint16_t FairyRing = 0x5d5;
constexpr std::int32_t FairyJoint = 0x617;
constexpr std::array<std::size_t, 3> TreeBones{2, 4, 6};
constexpr std::array<std::size_t, 4> HouseBones{4, 7, 10, 13};
constexpr std::array<std::size_t, 5> FairyBones{61, 62, 63, 64, 65};

Core::Expected<glm::vec3, std::string> Point(const WorldObjects::GameObjectPass& pass, std::size_t bone)
{
    if (pass.retainedPose == nullptr)
        return Core::Unexpected(std::format("0x4ec110: falta la pose global del hueso{} de Noria", bone));
    // The native .bss starts at zero; indices not yet published keep that zero matrix.
    if (bone >= pass.retainedPose->bones.size())
        return glm::vec3(0.0f);
    return Math::OriginalVectorTransform(glm::vec3(0.0f), Math::OriginalMatrixOf(pass.retainedPose->bones[bone]));
}

Core::Expected<void, std::string> Sprite(const WorldObjects::GameObjectPass& pass, std::uint16_t texture,
                                         const glm::vec3& position, const glm::vec3& light, float scale,
                                         float rotation = 0.0f)
{
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::string("0x5491e0: falta el pool de sprites de Noria"));
    static_cast<void>(pass.sprites->Create(texture, position, scale, light, rotation, 0));
    return {};
}

Core::Expected<void, std::string> Flare(const WorldObjects::GameObjectPass& pass, std::size_t bone,
                                        const glm::vec3& light, float scale)
{
    const auto position = Point(pass, bone);
    if (!position.HasValue())
        return Core::Unexpected(position.Error());
    return Sprite(pass, WorldObjects::StreetLightGlowTexture, position.Value(), light, scale);
}

Core::Expected<void, std::string> Particle(const WorldObjects::GameObjectPass& pass,
                                           const WorldObjects::WorldObject& object, std::int32_t type,
                                           std::int32_t subtype, const glm::vec3& position, const glm::vec3& angles)
{
    if (pass.particles == nullptr)
        return Core::Unexpected(std::string("0x53fe70: falta el pool de partículas de Noria"));
    const auto created = pass.particles->CreateChecked(type, position, angles, glm::vec3(1.0f), subtype, 1.0f,
                                                       pass.worldClock, *pass.random, 0);
    static_cast<void>(object);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("0x53fe70:Noria{:#x}/{} error{}", type, subtype, static_cast<int>(created.Error())));
    return {};
}

Core::Expected<void, std::string> Fairy(const WorldObjects::WorldObject& object, const glm::vec3& light,
                                        const WorldObjects::GameObjectPass& pass)
{
    const auto turn = Math::OriginalFtolLow(static_cast<double>(pass.worldClock) * FairyTurnScale);
    if (!turn)
        return Core::Unexpected(std::string("0x6035b4:ftol del giro de Noria fuera de dominio"));
    const float rotation = static_cast<float>(*turn % 360);
    const auto ringPoint = Point(pass, 57);
    if (!ringPoint.HasValue())
        return Core::Unexpected(ringPoint.Error());
    for (const float direction : {1.0f, -1.0f})
    {
        const auto ring = Sprite(pass, FairyRing, ringPoint.Value(), light, 1.0f, rotation * direction);
        if (!ring.HasValue())
            return ring;
    }
    if (pass.random == nullptr)
        return Core::Unexpected(std::string("0x603675: falta el RNG de Noria"));
    for (const auto bone : FairyBones)
    {
        const auto position = Point(pass, bone);
        if (!position.HasValue())
            return Core::Unexpected(position.Error());
        const auto flare = Sprite(pass, WorldObjects::StreetLightGlowTexture, position.Value(), glm::vec3(1.0f), 1.0f);
        if (!flare.HasValue())
            return flare;
        if ((pass.random->Next() & 31) == 0)
        {
            for (const int subtype : {0, 1})
            {
                const auto emitted = Particle(pass, object, Particles::ShinyParticle, subtype, position.Value(),
                                              object.transform.angles);
                if (!emitted.HasValue())
                    return emitted;
            }
        }
    }
    const auto center = Point(pass, 58);
    if (!center.HasValue())
        return Core::Unexpected(center.Error());
    if ((pass.random->Next() & 7) != 0)
        return {};
    glm::vec3 position = center.Value();
    for (int index = 0; index < 8; ++index)
    {
        glm::vec3 angles;
        angles.x = static_cast<float>(pass.random->Next() % 60 + 60);
        angles.y = 140.0f;
        angles.z = static_cast<float>(pass.random->Next() % 30);
        if (!pass.createJoint)
            return Core::Unexpected(std::string("0x603764: falta el productor durable de juntas617"));
        const Kernel::JointRequest request{FairyJoint, position, position, angles, 0, nullptr, 10.0f, -1, 0, 0, -1};
        const auto created = pass.createJoint(request);
        if (!created.HasValue())
            return created;
        const auto emitted = Particle(pass, object, Particles::FairySparkParticle, 0, position, angles);
        if (!emitted.HasValue())
            return emitted;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepNoriaObjectFrame(WorldObjects::WorldObject& object, int commonRandom,
                                                       const WorldObjects::GameObjectPass& pass)
{
    const float intensity = static_cast<float>(static_cast<double>(commonRandom % 30 + 70) * Hundredth);
    const glm::vec3 light(
        static_cast<float>(static_cast<double>(intensity) * Red),
        static_cast<float>(static_cast<double>(intensity) * (object.type == 0x27 ? FairyGreen : Green)), intensity);
    switch (object.type)
    {
    case 1:
        for (const auto bone : TreeBones)
        {
            const auto flare = Flare(pass, bone, light, 0.5f);
            if (!flare.HasValue())
                return flare;
        }
        return {};
    case 9:
        return Flare(pass, 1, light, 1.5f);
    case 0x11:
        for (const auto bone : HouseBones)
        {
            const auto flare = Flare(pass, bone, light, 1.0f);
            if (!flare.HasValue())
                return flare;
        }
        return {};
    case 0x23:
        return Flare(pass, 3, light, 1.5f);
    case 0x27:
        return Fairy(object, light, pass);
    default:
        return {};
    }
}
} // namespace Client::MapScripts::Noria
