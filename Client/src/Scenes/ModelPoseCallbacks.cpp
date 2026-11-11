#include "Scenes/ModelPoseCallbacks.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Scenes/CharacterDrawing.h"

#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> DrawPublishedObject(Render::ModelBatch& batch, const IModelLibrary& models,
                                                      const DrawnObject& object, Core::Duration frameClock,
                                                      const TailMeshProvider& posedMeshes)
{
    if (!posedMeshes)
        return Core::Unexpected(std::string("0x4ebc80: falta el scratch retenido después de publicar la pose"));
    const auto meshes = posedMeshes();
    const auto* model = models.Find(object.model);
    if (model != nullptr && meshes.size() < model->meshes.size())
        return Core::Unexpected(std::string("0x4ebc80: el scratch retenido no cubre las mallas del modelo"));
    DrawObjectFromMeshes(batch, models, object, frameClock, meshes);
    return {};
}
} // namespace Client::Scenes
