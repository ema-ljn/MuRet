#include "Scenes/GameDrawTail.h"

#include "Render/SpriteVertex.h"

#include "Scenes/GameTailDrawingPrimitives.h"
#include "Scenes/FaunaObject.h"
#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/TextureSlot.h"
#include "World/OriginalFtol.h"
#include "World/Sprites.h"
#include <glm/ext/vector_float3.hpp>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
using TailDrawing::Blend;
using TailDrawing::F32;
using TailDrawing::Quad;
} // namespace

namespace
{
void JointFace(Render::ModelBatch& batch, const JointState& joint, std::size_t segment, std::size_t first,
               std::size_t second, float u, float nextU, float v, float otherV, const glm::vec3& light)
{
    double factor = 1;
    if ((joint.type == 0x10f && (joint.subtype == 1 || joint.subtype == 2)) ||
        (joint.type == 0x5f2 && joint.subtype == 0))
    {
        const auto life = joint.life < 20 ? joint.life : 20;
        factor = static_cast<double>(life) * static_cast<double>(joint.type == 0x5f2 ? 0.1f : 0.05f);
    }
    const Render::Color color{F32(factor * light.x), F32(factor * light.y), F32(factor * light.z), 1};
    Quad(batch, {{{joint.points[segment][first], {u, v}, color},
                  {joint.points[segment][second], {u, otherV}, color},
                  {joint.points[segment + 1][second], {nextU, otherV}, color},
                  {joint.points[segment + 1][first], {nextU, v}, color}}});
}

glm::vec3 JointCenter(const JointState& joint, std::size_t segment)
{
    glm::vec3 center{};
    for (int component = 0; component < 3; ++component)
    {
        double sum = 0;
        for (const auto& point : joint.points[segment])
            sum += point[component];
        center[component] = F32(sum * 0.25);
    }
    return center;
}

void IcarusSprites(JointState& joint, const JointDrawResources& resources)
{
    if (!resources.originalFrame && !joint.icarusRotations.has_value())
        return; // Sin muestra original previa no inventar giros para los dibujos intermedios.
    if (resources.originalFrame)
        joint.icarusRotations = std::array<float, 2>{};
    const auto center = JointCenter(joint, 0);
    const float firstScale = joint.subtype == 6 ? 0.5f : 0.3f;
    const float secondScale = joint.subtype == 6 ? 1.5f : 1.0f;
    const auto first =
        resources.originalFrame ? static_cast<float>(resources.random.Next() % 360) : (*joint.icarusRotations)[0];
    (*joint.icarusRotations)[0] = first;
    static_cast<void>(resources.sprites.Create(0x62b, center, firstScale, joint.light, first, 3));
    const auto second =
        resources.originalFrame ? static_cast<float>(resources.random.Next() % 360) : (*joint.icarusRotations)[1];
    (*joint.icarusRotations)[1] = second;
    static_cast<void>(resources.sprites.Create(0x5fd, center, secondScale, joint.light, second, 3));
}

bool HalfTexture(const JointState& joint)
{
    return (joint.type == 0x62c && joint.subtype >= 0 && joint.subtype <= 4) ||
           (joint.subtype >= 11 && joint.subtype <= 13);
}
// u, la siguiente u y el corrimiento de 0x53c8ee (el de la segunda cara de 614 y 615).
Core::Expected<std::array<float, 3>, std::string> JointUvs(const JointState& joint, std::int32_t segment,
                                                           const JointDrawResources* resources)
{
    const double divisor = static_cast<double>(joint.maximumCount) - 1;
    float u = F32(static_cast<double>(joint.count - segment) / divisor);
    float nextU = F32(static_cast<double>(joint.count - segment - 1) / divisor);
    if (joint.orientation == 3)
    {
        u = F32(1 - static_cast<double>(segment) / divisor);
        nextU = F32(1 - static_cast<double>(segment + 1) / divisor);
    }
    const auto clock = World::OriginalFtolLow(resources != nullptr ? resources->worldClock : 0.0f);
    if (!clock.has_value())
        return Core::Unexpected(std::string("0x53c8f4: el reloj del joint excede ftol"));
    float scroll = F32(static_cast<double>(*clock % 1000) * static_cast<double>(0.001f));
    if (joint.repeatedTexture)
    {
        constexpr double RepeatScale = 0.0625;
        u = F32(static_cast<double>(joint.count - segment) * RepeatScale);
        nextU = F32(static_cast<double>(joint.count - segment - 1) * RepeatScale);
    }
    if (joint.type == 0x614 || joint.type == 0x615)
    {
        u = F32(static_cast<double>(u) * 2 - scroll);
        nextU = F32(static_cast<double>(nextU) * 2 - scroll);
    }
    const bool halfTexture = HalfTexture(joint);
    if (halfTexture)
    {
        const auto halfDivisor = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(joint.maximumCount) - 1u) / 2;
        u = F32(static_cast<double>(joint.count - segment) / halfDivisor - scroll);
        nextU = F32(static_cast<double>(joint.count - segment - 1) / halfDivisor - scroll);
    }
    if (joint.repeatedTexture)
    {
        scroll = F32(static_cast<double>(scroll) * 2);
        u = F32(static_cast<double>(u) * 2 - scroll);
        nextU = F32(static_cast<double>(nextU) * 2 - scroll);
    }
    return std::array<float, 3>{u, nextU, scroll};
}

Core::Expected<glm::vec3, std::string> BloodCastleSegmentLight(const JointState& joint, std::int32_t segment,
                                                               const JointDrawResources* resources)
{
    glm::vec3 segmentLight;
    const auto* owner = static_cast<const FaunaObject*>(joint.ownerContext);
    segmentLight = glm::vec3(1);
    if (owner != nullptr)
    {
        const double base = static_cast<double>(owner->position.z) + (joint.subtype == 9 ? 180 : 50);
        const double reduction = (static_cast<double>(joint.points[static_cast<std::size_t>(segment)][0].z) - base) *
                                 static_cast<double>(0.01f);
        segmentLight = joint.light;
        if (reduction > 0)
            segmentLight = {F32(static_cast<double>(joint.light.x) - reduction),
                            F32(static_cast<double>(joint.light.y) - reduction),
                            F32(static_cast<double>(joint.light.z) - reduction)};
    }
    if (segment == joint.count / 2)
    {
        if (resources == nullptr)
            return Core::Unexpected(std::string("0x53cba2: faltan los sprites del joint0x10f"));
        const auto center = JointCenter(joint, static_cast<std::size_t>(segment));
        static_cast<void>(resources->sprites.Create(0x62b, center, 0.7f, segmentLight, 0, 0));
    }
    return segmentLight;
}

Core::Expected<glm::vec3, std::string> EnergyArrowSegmentLight(JointState& joint, std::int32_t segment,
                                                               const JointDrawResources* resources)
{
    if (joint.ownerContext == nullptr)
        return joint.light;
    constexpr float Fade = std::bit_cast<float>(0x3f7f6fd2u); // 6ba318.
    const double red = static_cast<double>(joint.light.x) * Fade;
    joint.light = {F32(red), F32(static_cast<double>(joint.light.y) * Fade),
                   F32(static_cast<double>(joint.light.z) * Fade)};
    const double reduction = static_cast<double>(segment) * static_cast<double>(0.01f);
    const glm::vec3 color{F32(red - reduction), F32(static_cast<double>(joint.light.y) - reduction),
                          F32(static_cast<double>(joint.light.z) - reduction)};
    if (resources == nullptr)
        return Core::Unexpected(std::string("0x53ccb2: faltan los sprites del joint0x619"));
    static_cast<void>(resources->sprites.Create(0x62b, JointCenter(joint, static_cast<std::size_t>(segment)),
                                                joint.subtype == 9 ? 0.5f : 0.7f, color, 0, 0));
    return color;
}

Core::Expected<glm::vec3, std::string> JointSegmentLight(JointState& joint, std::int32_t segment,
                                                         const JointDrawResources* resources)
{
    if (joint.type == 0x619 && (joint.subtype == 9 || joint.subtype == 10))
        return EnergyArrowSegmentLight(joint, segment, resources);
    const bool halfTexture = HalfTexture(joint);
    glm::vec3 segmentLight = joint.light;
    if (joint.type == 0x10f && (joint.subtype == 0 || joint.subtype == 4 || joint.subtype == 9))
        return BloodCastleSegmentLight(joint, segment, resources);
    if (halfTexture)
    {
        const double factor = static_cast<double>(joint.count - segment - 1) / joint.maximumCount * 2;
        segmentLight = {F32(factor * joint.light.x), F32(factor * joint.light.y), F32(factor * joint.light.z)};
    }
    if (joint.type == 0x61c && joint.subtype == 1)
    {
        const double factor = (1 - static_cast<double>(joint.count - segment) / joint.count) * 2;
        segmentLight = {F32(factor * joint.light.x), F32(factor * joint.light.y), F32(factor * joint.light.z)};
    }
    if (joint.type == 0x615 && joint.subtype == 0)
    {
        const auto whole = World::OriginalFtolLow(joint.light.z);
        if (!whole.has_value())
            return Core::Unexpected(std::string("0x53cccd: el color del joint excede ftol"));
        segmentLight = glm::vec3(*whole == segment  ? F32(static_cast<double>(joint.light.z) - segment)
                                 : *whole < segment ? 0.0f
                                                    : 0.7f);
    }
    return segmentLight;
}

Core::Expected<void, std::string> DrawJointSegment(Render::ModelBatch& batch, JointState& joint, std::int32_t segment,
                                                   const JointDrawResources* resources)
{
    const auto uv = JointUvs(joint, segment, resources);
    if (!uv.HasValue())
        return Core::Unexpected(uv.Error());
    float u = uv.Value()[0];
    float nextU = uv.Value()[1];
    if (joint.type == 0x61c && joint.subtype == 0)
    {
        const auto gray =
            F32((static_cast<double>(joint.maximumCount) - segment) / joint.maximumCount * 2 * joint.light.x);
        JointFace(batch, joint, static_cast<std::size_t>(segment), 0, 1, u, nextU, 0, 1, glm::vec3(gray));
        return {};
    }
    const auto light = JointSegmentLight(joint, segment, resources);
    if (!light.HasValue())
        return Core::Unexpected(light.Error());
    const auto segmentLight = light.Value();
    if (joint.type == 0x610 && (joint.subtype == 6 || joint.subtype == 8) && segment == 0)
    {
        if (resources == nullptr)
            return Core::Unexpected(std::string("0x53cd83: faltan el RNG y los sprites del joint 0x610"));
        IcarusSprites(joint, *resources);
    }
    const float v = joint.orientation == 1 ? 1.0f : 0.0f;
    const float otherV = 1 - v;
    if (joint.orientation == 2)
    {
        u = F32(1 - static_cast<double>(u));
        nextU = F32(1 - static_cast<double>(nextU));
    }
    const auto at = static_cast<std::size_t>(segment);
    if ((joint.drawEnabled & 1u) != 0)
        JointFace(batch, joint, at, 2, 3, u, nextU, otherV, v, segmentLight);
    if ((joint.drawEnabled & 2u) != 0)
    {
        // 0x53d01d..0x53d043: en 614 y 615 la segunda cara corre las dos u en dos veces el corrimiento.
        if (joint.type == 0x614 || joint.type == 0x615)
        {
            const double twice = static_cast<double>(uv.Value()[2]) + static_cast<double>(uv.Value()[2]);
            u = F32(static_cast<double>(u) + twice);
            nextU = F32(static_cast<double>(nextU) + twice);
        }
        JointFace(batch, joint, at, 0, 1, u, nextU, v, otherV, segmentLight);
    }
    return {};
}

Core::Expected<void, std::string> CommonJoint(Render::ModelBatch& batch, JointState& joint,
                                              const JointDrawResources* resources)
{
    if (joint.type == 0x62b && joint.subtype == 20)
        Blend(batch, Render::ModelBlend::InverseColorAdditive);
    auto state = batch.State();
    state.texture = Render::TextureSlot{joint.texture};
    state.rawTextureName.reset();
    state.space = Render::ModelCoordinateSpace::World;
    batch.ApplyOperation(state, Render::ModelStateOperation::Binding);
    if (joint.count >= static_cast<std::int32_t>(joint.points.size()))
        return Core::Unexpected(std::string("0x53c82b: el joint excede sus puntos canónicos"));
    for (std::int32_t segment = 0; segment < joint.count; ++segment)
    {
        if (joint.type == 0x619 && (joint.subtype == 9 || joint.subtype == 10) && segment == joint.count - 1)
            continue;
        if (joint.type == 0x5f2 && joint.subtype == 0 && segment == 0)
            continue;
        if (auto drawn = DrawJointSegment(batch, joint, segment, resources); !drawn.HasValue())
            return drawn;
    }
    return {};
}
} // namespace

namespace
{
Core::Expected<void, std::string> DrawJointsChecked(Render::ModelBatch& batch, std::span<JointState> joints,
                                                    std::uint8_t mode, const JointDrawResources* resources)
{
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        auto& joint = joints[i];
        if (!joint.alive || joint.count <= 0 || !joint.drawEnabled)
            continue;
        if ((mode == 1 && joint.category == 2) || (mode == 2 && joint.category == 1))
            continue;
        switch (joint.blend)
        {
        case 1:
            Blend(batch, Render::ModelBlend::Additive);
            break;
        case 2:
            Blend(batch, Render::ModelBlend::Alpha);
            break;
        case 3:
            Blend(batch, Render::ModelBlend::Subtractive);
            break;
        case 4:
            Blend(batch, Render::ModelBlend::InverseColorAdditive);
            break;
        default:
            break;
        }
        if (joint.type == 0x619 && joint.subtype == 8)
        {
            auto state = batch.State();
            state.depthTest = false;
            batch.ApplyState(state);
        }
        if (auto drawn = CommonJoint(batch, joint, resources); !drawn.HasValue())
            return drawn;
        if (joint.type == 0x619 && joint.subtype == 8)
        {
            auto state = batch.State();
            state.depthTest = true;
            batch.ApplyState(state);
        }
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawJoints(Render::ModelBatch& batch, std::span<JointState> joints, std::uint8_t mode)
{
    return DrawJointsChecked(batch, joints, mode, nullptr);
}
Core::Expected<void, std::string> DrawJoints(Render::ModelBatch& batch, std::span<JointState> joints, std::uint8_t mode,
                                             const JointDrawResources& resources)
{
    return DrawJointsChecked(batch, joints, mode, &resources);
}
} // namespace Client::Scenes
