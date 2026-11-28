#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Render/ITextureStore.h"
#include "Models/IModelLibrary.h"
#include "Models/MeshTraits.h"
#include "Models/ModelId.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/ActorState.h"
#include "Assets/ModelTable.h"
#include "Assets/SceneTextures.h"
#include "Models/TextureMetadata.h"
#include "Ui/Widget.h"

#include <glm/ext/vector_float2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Client::Models
{
struct ClothWorld;
}

namespace Client::Actors
{
struct ActorConstructionContext;
} // namespace Client::Actors

// The create window's preview (CL6.5; 06 § La ventana de crear and "La vista previa"): the
// models the selection uploads (0x61ca30) and the loose object 0x7437050, which 0x63e3e0 builds with the
// class's NewFace and 0x63e460 draws in its own viewport with its own camera (through 0x50ba30,
// which with a model that is not the character's goes to 0x602e90, without shadow). *Static*.
namespace Client::FrontEnd
{
// Logo05 at 0xa4 and NewFace01 to 05 at 0xa8 to 0xac (0x60da30 with "Data\Logo\"); their textures, from
// Logo\ from 0x60d530(0x6f8), those of 0xa4 with GL_LINEAR and those of the NewFace with GL_NEAREST, both
// repeating (0x60d5f0, with the fifth argument at 1).
inline constexpr std::uint16_t SelectionLogoModel = 0xa4;
inline constexpr std::uint16_t FirstNewFaceModel = 0xa8;
inline constexpr std::size_t NewFaceCount = 5;
inline constexpr Render::TextureSlot SelectionModelsFirstSlot{0x6f8};

// In the order 0x61ca30 reads them and gives them textures.
[[nodiscard]] std::array<Data::Models::ModelFile, 1 + NewFaceCount> SelectionModelFiles();

// The six models with their skeleton and each mesh's slot. The speed of actions 0 and 1 of
// the NewFace is 0.3 (0x3e99999a, at the end of 0x61ca30).
class SelectionModels final : public Models::IModelLibrary
{
public:
    // Reads the six from `dataRoot` and gives them their textures following `gameSlots` (the load's
    // table, which is global in the original) from 0x6f8, or an empty table without it, and uploads them. A
    // missing one is skipped with a log line; a texture that does not load stops, like the original,
    // which closes. It also receives the metadata of the temporary slot that already lives in this backend.
    [[nodiscard]] Core::Expected<void, std::string>
    Load(const std::filesystem::path& dataRoot, Render::ITextureStore& textures, Core::Log& log,
         const Models::ModelTextureSlots* gameSlots,
         std::optional<Models::TextureMetadata> startupTexture = std::nullopt);
    // 0x61cc50
    void Release();

    [[nodiscard]] const Data::Models::Model* Find(Models::ModelId model) const override;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(Models::ModelId model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(Models::ModelId model) const override;
    [[nodiscard]] std::span<const Models::MeshTraits> MeshTraitsOf(Models::ModelId model) const override;
    [[nodiscard]] std::optional<Models::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;
    // The slots Load handed out, if it loaded.
    [[nodiscard]] const Models::ModelTextureSlots* TextureSlots() const;

private:
    Assets::ModelTable m_models;
    std::optional<Models::ModelTextureSlots> m_slots;
    std::optional<Assets::SceneTextures> m_textures;
    // The common load leaves this slot to the selection when it enters.
    Render::ITextureStore* m_startupStore = nullptr;
    std::optional<Models::TextureMetadata> m_startupTexture;
};

// The preview object: the class (+0x1d4, the window's) and its animation.
using CreatePreview = Kernel::ActorState;

// 0x63e3e0: the full constructor, including the terrain and the shared rand().
[[nodiscard]] Core::Expected<void, std::string> RebuildPreview(CreatePreview& preview, std::uint8_t characterClass,
                                                               const Models::IModelLibrary& models,
                                                               const Actors::ActorConstructionContext& construction,
                                                               Models::ClothWorld& cloth);

// Presentation adapter without terrain; main uses RebuildPreview with the full constructor.
void ChoosePreviewClass(CreatePreview& preview, std::uint8_t characterClass, const Models::IModelLibrary& models);

// One step of 0x63e430: 0x4f9580 advances it (the action speed times 2 with the NewFace) and, if the
// action ended, action 0. It also runs besides 0x508ea0 from the scene logic: with the window up it
// advances twice, and with a notice, only from the scene.
void StepPreview(CreatePreview& preview, const Models::IModelLibrary& models);

// 0x63e460: the class's offset stays on the actor for each original frame. The camera reads
// the position before that offset; the intermediate draws keep the last frame's.
// The color is 4.8 per vertex (0x6002e0).
[[nodiscard]] Actors::Drawing::DrawnObject DrawnPreview(CreatePreview& preview, const Models::IModelLibrary& models,
                                                        float stepAlpha, int originalFrames = 1);

// 0x6338a0(object, (10, −500, 48), (−90, 0, 0)) with the fov at 10 (0x63e460) and the near and far of
// scene 4. The position is the object's before its class's offset.
[[nodiscard]] Render::Camera3D PreviewCamera();
[[nodiscard]] Render::Camera3D PreviewCamera(const CreatePreview& preview);

// 0x620db0(ftol(x / rx), ftol(y / ry), ftol(410 / rx), ftol(335 / ry)), with (x, y) the window box's
// corner in pixels.
[[nodiscard]] Render::ViewportRect PreviewViewport(const Ui::Rect& box, Core::PixelSize screen);
} // namespace Client::FrontEnd
