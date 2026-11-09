#include "Scenes/WorldObjectRenderState.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/TextureSlot.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"
#include "World/WorldObjectDrawingModel.h"
#include "World/WorldObjectDrawingError.h"
#include "World/WorldObjectStepModel.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <utility>

namespace Client::Scenes
{
namespace
{
bool MarkPosedBones(const Render::Models::Skeleton& skeleton, std::vector<std::uint8_t>& known)
{
    for (std::size_t index = 0; index < skeleton.bones.size() && !skeleton.actions.empty(); ++index)
    {
        const auto& bone = skeleton.bones[index];
        if (bone.dummy)
            continue;
        if (bone.parent != -1 && (bone.parent < 0 || static_cast<std::size_t>(bone.parent) >= index ||
                                  !known[static_cast<std::size_t>(bone.parent)]))
            return false;
        known[index] = true;
    }
    return true;
}

Render::Models::OriginalMatrix WorldBone(const Render::Models::BoneTransform& bone,
                                         const Render::Models::ObjectTransform& transform)
{
    auto matrix = Render::Models::OriginalMatrixOf(bone);
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
            matrix[row * 4 + column] *= transform.scale;
        matrix[row * 4 + 3] = matrix[row * 4 + 3] * transform.scale + transform.position[static_cast<int>(row)];
    }
    return matrix;
}
} // namespace
struct WorldObjectRenderState::ModelState
{
    World::WorldObjectStepModel step;
    World::ModelEffectAnimationBinding effect{std::ref(step.action), {}};
    const Data::Models::Model* bound = nullptr;
    glm::vec3 light{};
    glm::vec3 shadowOrigin{};
    std::int16_t meshCount = 0;
    std::int16_t frame = 0;
    std::vector<std::int16_t> materials;
    std::vector<std::int16_t> keys;
    World::WorldObjectDrawingModel view{step, light, shadowOrigin, meshCount, {}, frame, {}};
    std::optional<DrawnObject> current;
};

WorldObjectRenderState::WorldObjectRenderState() = default;
WorldObjectRenderState::~WorldObjectRenderState() = default;
void WorldObjectRenderState::ReleaseModels()
{
    for (auto& [type, entry] : m_models)
    {
        (void)type;
        entry->bound = nullptr;
        entry->step.skeleton = nullptr;
        entry->effect.actions = {};
        entry->view.meshMaterials = {};
        entry->view.actionKeys = {};
        entry->materials.clear();
        entry->keys.clear();
        entry->current.reset();
    }
}

WorldObjectRenderState::ModelState& WorldObjectRenderState::Entry(std::int16_t type)
{
    auto& entry = m_models[type];
    if (!entry)
    {
        entry = std::make_unique<ModelState>();
    }
    return *entry;
}

World::WorldObjectStepModel& WorldObjectRenderState::Step(std::int16_t type)
{
    return Entry(type).step;
}

std::int16_t& WorldObjectRenderState::Frame(std::int16_t type)
{
    return Entry(type).frame;
}

Core::Expected<std::reference_wrapper<World::WorldObjectDrawingModel>, World::WorldObjectDrawingError>
WorldObjectRenderState::Model(std::int16_t type, const IModelLibrary& models)
{
    const auto id = std::bit_cast<std::uint16_t>(type);
    const auto* model = models.Find(id);
    const auto* skeleton = models.SkeletonOf(id);
    if (model == nullptr || skeleton == nullptr || model->meshes.size() > std::numeric_limits<std::int16_t>::max())
    {
        return Core::Unexpected(World::WorldObjectDrawingError::MissingModel);
    }
    auto& entry = Entry(type);
    entry.step.skeleton = skeleton;
    if (entry.bound != model)
    {
        entry.meshCount = static_cast<std::int16_t>(model->meshes.size());
        entry.bound = model;
    }
    entry.materials.clear();
    for (const auto slot : models.MeshTextures(id))
    {
        entry.materials.push_back(std::bit_cast<std::int16_t>(slot.value));
    }
    entry.keys.clear();
    for (const auto& action : skeleton->actions)
    {
        entry.keys.push_back(action.keyCount);
    }
    entry.view.meshMaterials = entry.materials;
    entry.view.actionKeys = entry.keys;
    return std::ref(entry.view);
}

Core::Expected<std::reference_wrapper<World::ModelEffectAnimationBinding>, World::WorldObjectDrawingError>
WorldObjectRenderState::EffectAnimation(std::int16_t type, const IModelLibrary& models)
{
    const auto model = Model(type, models);
    if (!model.HasValue())
    {
        return Core::Unexpected(model.Error());
    }
    auto& entry = Entry(type);
    entry.effect.actions = entry.step.skeleton->actions;
    return std::ref(entry.effect);
}

Core::Expected<void, World::WorldObjectDrawingError> WorldObjectRenderState::Pose(const IModelLibrary& models,
                                                                                  const DrawnObject& object)
{
    const auto type = std::bit_cast<std::int16_t>(object.model);
    const auto model = Model(type, models);
    if (!model.HasValue())
    {
        return Core::Unexpected(model.Error());
    }
    const auto& skeleton = *model.Value().get().step.skeleton;
    const auto count = skeleton.bones.size();
    m_bones.resize(std::max(count, m_bones.size()), Render::Models::BoneTransform(0.0f));
    m_worldBones.resize(m_bones.size());
    m_known.resize(m_bones.size(), false);
    auto known = m_known;
    if (!MarkPosedBones(skeleton, known))
    {
        return Core::Unexpected(World::WorldObjectDrawingError::IndeterminateInput);
    }
    if (!Render::Models::PoseSkeleton(skeleton, object.animation, object.transform, glm::vec2{},
                                      std::span(m_bones).first(count)))
    {
        return Core::Unexpected(World::WorldObjectDrawingError::MissingBinding);
    }
    for (std::size_t index = 0; index < count && !skeleton.actions.empty(); ++index)
    {
        if (skeleton.bones[index].dummy)
        {
            continue;
        }
        m_worldBones[index] = WorldBone(m_bones[index], object.transform);
    }
    m_known = std::move(known);
    Entry(type).current = object;
    return {};
}

std::span<const Render::Models::BoneTransform> WorldObjectRenderState::Bones() const
{
    return m_bones;
}

std::span<const std::uint8_t> WorldObjectRenderState::KnownBones() const
{
    return m_known;
}

bool WorldObjectRenderState::BoneKnown(std::size_t bone) const
{
    return bone < m_known.size() && m_known[bone];
}

const DrawnObject* WorldObjectRenderState::Current(std::int16_t type) const
{
    const auto found = m_models.find(type);
    if (found == m_models.end() || !found->second->current)
        return nullptr;
    const auto& entry = *found->second;
    if (entry.step.skeleton == nullptr)
        return nullptr;
    const auto& object = *entry.current;
    // SkinMesh usa una sola raíz actual. Una matriz mundial retenida distinta exige otro binding.
    for (std::size_t index = 0; index < entry.step.skeleton->bones.size() && index < m_bones.size(); ++index)
    {
        if (!BoneKnown(index))
            continue;
        const auto prepared = WorldBone(m_bones[index], object.transform);
        if (std::bit_cast<std::array<std::uint32_t, 12>>(prepared) !=
            std::bit_cast<std::array<std::uint32_t, 12>>(m_worldBones[index]))
            return nullptr;
    }
    return &object;
}

std::optional<std::int16_t> WorldObjectRenderState::TypeOf(const World::WorldObjectDrawingModel& model) const
{
    for (const auto& [type, entry] : m_models)
    {
        if (&entry->view == &model)
        {
            return type;
        }
    }
    return std::nullopt;
}

Core::Expected<glm::vec3, World::WorldObjectDrawingError>
WorldObjectRenderState::Transform(std::size_t bone, const glm::vec3& input) const
{
    if (!BoneKnown(bone))
    {
        return Core::Unexpected(World::WorldObjectDrawingError::IndeterminateInput);
    }
    return Render::Models::OriginalVectorTransform(input, m_worldBones[bone]);
}
} // namespace Client::Scenes
