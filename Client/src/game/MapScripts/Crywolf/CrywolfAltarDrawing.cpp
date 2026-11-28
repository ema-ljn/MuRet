#include "MapScripts/Crywolf/CrywolfAltarDrawing.h"

#include "Actors/Drawing/CharacterDrawing.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Kernel/WorldObject.h"
#include "Models/IModelLibrary.h"
#include "Models/Skinning.h"
#include "Render/ModelBatch.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::MapScripts::Crywolf
{
Core::Expected<void, std::string> DrawCrywolfAltarMeshes(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                         Actors::Drawing::DrawnObject& object,
                                                         Core::Duration frameClock,
                                                         Kernel::ObjectModelStepState* modelState)
{
    std::vector<Models::SkinnedMesh> skinned;
    std::span<const Models::SkinnedMesh> meshes = object.retainedMeshes;
    if (meshes.empty())
    {
        Actors::Drawing::SkinTheDrawnObject(models, object, frameClock, skinned);
        meshes = skinned;
    }
    // 0x430ab5..0x430b14: the second pass reads the RGB the first one may have stored inside 0x4ecc30.
    for (const auto& [selection, flags] :
         {std::pair{std::int8_t{0}, std::uint32_t{0x44}}, std::pair{std::int8_t{1}, std::uint32_t{0x240}}})
    {
        object.unlitMesh = selection;
        if (modelState != nullptr)
            modelState->unlitMeshes[static_cast<std::int16_t>(object.model)] = selection;
        if (!meshes.empty())
        {
            if (auto drawn = Actors::Drawing::DrawItemMeshPass(batch, models, object, frameClock, meshes, 0, flags,
                                                               std::nullopt, &object.color);
                !drawn.HasValue())
                return drawn;
        }
    }
    object.unlitMesh = std::int16_t{-1};
    if (modelState != nullptr)
        modelState->unlitMeshes[static_cast<std::int16_t>(object.model)] = std::int8_t{-1};
    return {};
}
} // namespace Client::MapScripts::Crywolf
