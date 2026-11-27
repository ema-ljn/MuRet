#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Tables/ModelTextureCalls.h"
#include "Render/ITextureStore.h"
#include "Models/IModelLibrary.h"
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
#include <vector>

// Los modelos del personaje (0x60dbe0), ítems (0x60ff50) y efectos (0x615700), con sus esqueletos
// y enlaces de mallas a texturas para las escenas. Contrato en 06 § El modelo en el binario.
namespace Client::Scenes
{
class GameModels final : public Models::IModelLibrary
{
public:
    // Reconstruye las tablas de modelos y los enlaces de 0x60f7d0, 0x611850 y 0x615700 en sus fases.
    // Conserva los metadatos de texturas ya publicados si una subida posterior falla. Un modelo
    // obligatorio ausente detiene la carga y los demás recursos ausentes se registran en el log (Q-C23).
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot,
                                                         Render::ITextureStore& textures, Core::Log& log);

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    [[nodiscard]] std::optional<Models::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    // El slot de cada textura que subió, solo si quedó publicada: cada una va al de su entrada,
    // que es su selector. Es lo que 0x620780 liga para las partículas.
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    // El tamaño de una textura que subió.
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;
    // Transfiere la textura temporal a la selección, que pasa a ser dueña del slot físico.
    [[nodiscard]] std::optional<Models::TextureMetadata> TakeStartupSelectionTexture(Render::ITextureStore& store);

    // Cuántos modelos se leyeron.
    [[nodiscard]] std::size_t Count() const;
    // Los slots que repartió la carga, con el nombre de cada uno.
    [[nodiscard]] const Models::ModelTextureSlots& TextureSlots() const;

private:
    [[nodiscard]] Core::Expected<void, std::string> LoadResources(const std::filesystem::path& dataRoot,
                                                                  Render::ITextureStore& textures, Core::Log& log);
    void AssignTextures(std::span<const Tables::ModelTextureCall> calls);

    std::map<std::uint16_t, Data::Models::Model> m_models;
    std::map<std::uint16_t, Models::Skeleton> m_skeletons;
    std::map<std::uint16_t, std::vector<Render::TextureSlot>> m_meshTextures;
    // La metadata de las texturas que efectivamente subió.
    std::map<std::uint32_t, Models::TextureMetadata> m_textureMetadata;
    Render::ITextureStore* m_startupTextureStore = nullptr;
    // Desde 0x138 (0x60f7d0); Load le corre el próximo a cada cargador.
    Models::ModelTextureSlots m_slots{Render::TextureSlot{0x138}};
};
} // namespace Client::Scenes
