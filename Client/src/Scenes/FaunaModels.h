#pragma once

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ConstructedBmdPrefix.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/ModelTextureTable.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/ext/vector_float2.hpp>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Core
{
class Log;
}
namespace Client::Render
{
class ITextureStore;
}
namespace Client::Render::Models
{
class ModelTextureSlots;
}
namespace Client::Scenes
{
struct FaunaModelLoadContext
{
    const std::filesystem::path& dataRoot;
    Render::ITextureStore& textures;
    Core::Log& log;
    Render::Models::ModelTextureSlots& slots;
    ModelTextureTable* sharedTextures = nullptr;
};
struct FaunaResourceSound
{
    std::int32_t id;
    std::string_view path;
    std::int32_t buffers = 2;
    std::int32_t flags = 1;
};
// 612d90(1f/21): modelos globales 171/173, carga diferida en el primer spawn.
// El owner dura más que los cambios de mapa; Bind sólo cambia la biblioteca prestada.
class FaunaModels final : public IModelLibrary
{
public:
    FaunaModels() = default;
    FaunaModels(const FaunaModels&) = delete;
    FaunaModels& operator=(const FaunaModels&) = delete;
    FaunaModels(FaunaModels&&) = delete;
    FaunaModels& operator=(FaunaModels&&) = delete;
    ~FaunaModels() override;
    void Bind(const IModelLibrary* base);
    // 60dc4c: el caller entrega su elección CRT en la inicialización global de modelos.
    [[nodiscard]] Core::Expected<void, std::string> InitializePrefix(std::uint16_t constructedBeforeBase);
    [[nodiscard]] const Render::Models::Skeleton* PrefixModel() const;
    [[nodiscard]] std::optional<std::uint16_t> PrefixOffset() const;
    [[nodiscard]] Core::Expected<void, std::string> Prepare(std::int32_t monster, FaunaModelLoadContext& context);
    void Release();
    [[nodiscard]] ModelTextureTable& TextureTable();
    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    [[nodiscard]] std::optional<TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;
    [[nodiscard]] std::span<const FaunaResourceSound> SoundFiles() const;
    [[nodiscard]] std::span<const std::int16_t, 10> SoundActions(std::uint16_t model) const;

private:
    struct Entry
    {
        Data::Models::Model model;
        Render::Models::Skeleton skeleton;
        std::vector<Render::TextureSlot> textures;
        std::array<std::int16_t, 10> sounds{};
    };
    // No se reinicia al salir de un mapa ni al soltar sus texturas.
    ConstructedBmdPrefix m_prefix;
    ModelTextureTable m_textureTable;
    ModelTextureTable* m_sharedTextures = nullptr;
    const IModelLibrary* m_base = nullptr;
    Render::ITextureStore* m_store = nullptr;
    std::map<std::uint16_t, Entry> m_models;
    std::map<std::uint32_t, TextureMetadata> m_metadata;
    std::vector<Render::TextureSlot> m_uploaded;
    std::vector<FaunaResourceSound> m_sounds;
};
} // namespace Client::Scenes
