#pragma once

#include "Core/Expected.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "World/WorldObjectDrawingError.h"
#include "World/WorldObjectStepModel.h"
#include "World/IWorldObjectStepModels.h"

#include <cstddef>
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Client::World
{
struct WorldObjectDrawingModel;
struct ModelEffectAnimationBinding;
} // namespace Client::World
namespace Client::Scenes
{
class IModelLibrary;
struct DrawnObject;

// Único dueño de M+a0/M+88 y de la pose global; los servicios sólo toman préstamos.
class WorldObjectRenderState : public World::IWorldObjectStepModels
{
public:
    WorldObjectRenderState();
    ~WorldObjectRenderState() override;
    WorldObjectRenderState(const WorldObjectRenderState&) = delete;
    WorldObjectRenderState& operator=(const WorldObjectRenderState&) = delete;
    WorldObjectRenderState(WorldObjectRenderState&&) = delete;
    WorldObjectRenderState& operator=(WorldObjectRenderState&&) = delete;
    [[nodiscard]] World::WorldObjectStepModel& Step(std::int16_t type) override;
    [[nodiscard]] std::int16_t& Frame(std::int16_t type);
    void ReleaseModels();
    [[nodiscard]] Core::Expected<std::reference_wrapper<World::WorldObjectDrawingModel>, World::WorldObjectDrawingError>
    Model(std::int16_t type, const IModelLibrary& models);
    // Vista estable; presta recursos de models hasta ReleaseModels. El adapter no se retiene.
    [[nodiscard]] Core::Expected<std::reference_wrapper<World::ModelEffectAnimationBinding>,
                                 World::WorldObjectDrawingError>
    EffectAnimation(std::int16_t type, const IModelLibrary& models);
    [[nodiscard]] Core::Expected<void, World::WorldObjectDrawingError> Pose(const IModelLibrary& models,
                                                                            const DrawnObject& object);
    [[nodiscard]] std::span<const Render::Models::BoneTransform> Bones() const;
    [[nodiscard]] std::span<const std::uint8_t> KnownBones() const;
    [[nodiscard]] bool BoneKnown(std::size_t bone) const;
    // nullptr si SkinMesh con la raíz del objeto no representa la historia mundial retenida.
    [[nodiscard]] const DrawnObject* Current(std::int16_t type) const;
    [[nodiscard]] std::optional<std::int16_t> TypeOf(const World::WorldObjectDrawingModel& model) const;
    [[nodiscard]] Core::Expected<glm::vec3, World::WorldObjectDrawingError> Transform(std::size_t bone,
                                                                                      const glm::vec3& input) const;

private:
    struct ModelState;
    ModelState& Entry(std::int16_t type);
    std::map<std::int16_t, std::unique_ptr<ModelState>> m_models;
    std::vector<Render::Models::BoneTransform> m_bones;
    std::vector<std::uint8_t> m_known;
    std::vector<Render::Models::OriginalMatrix> m_worldBones;
};
} // namespace Client::Scenes
