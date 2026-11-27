#pragma once

#include "Assets/ModelTable.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Tables/ModelTextureCalls.h"
#include "Render/ITextureStore.h"
#include "Models/IModelLibrary.h"
#include "Models/MeshTraits.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Models/TextureMetadata.h"

#include <glm/ext/vector_float2.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>

// The models of the character (0x60dbe0), items (0x60ff50) and effects (0x615700), with their skeletons and
// mesh-to-texture links for the scenes. Contract in 06 § El modelo en el binario.
namespace Client::Assets
{
class GameModels final : public Models::IModelLibrary
{
public:
    // Rebuilds the model tables and the links of 0x60f7d0, 0x611850 and 0x615700 in their phases. Keeps the texture
    // metadata already published if a later upload fails. A missing required model stops the load, and the other
    // missing resources go to the log (Q-C23).
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot,
                                                         Render::ITextureStore& textures, Core::Log& log);

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Models::MeshTraits> MeshTraitsOf(std::uint16_t model) const override;
    [[nodiscard]] std::optional<Models::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    // The slot of each texture that was uploaded, only if it was published: each goes to its entry's, which is its
    // selector. It is what 0x620780 binds for the particles.
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;
    // Hands the temporary texture to the selection, which becomes the owner of the physical slot.
    [[nodiscard]] std::optional<Models::TextureMetadata> TakeStartupSelectionTexture(Render::ITextureStore& store);

    [[nodiscard]] std::size_t Count() const;
    [[nodiscard]] const Models::ModelTextureSlots& TextureSlots() const;

private:
    [[nodiscard]] Core::Expected<void, std::string> LoadResources(const std::filesystem::path& dataRoot,
                                                                  Render::ITextureStore& textures, Core::Log& log);
    void AssignTextures(std::span<const Tables::ModelTextureCall> calls);

    ModelTable m_models;
    // Only the textures that were actually uploaded.
    std::map<std::uint32_t, Models::TextureMetadata> m_textureMetadata;
    Render::ITextureStore* m_startupTextureStore = nullptr;
    // From 0x138 (0x60f7d0); Load moves the next one for each loader.
    Models::ModelTextureSlots m_slots{Render::TextureSlot{0x138}};
};
} // namespace Client::Assets
