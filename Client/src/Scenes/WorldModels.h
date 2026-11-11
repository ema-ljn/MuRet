#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Render/ITextureStore.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/TextureMetadata.h"

#include <glm/ext/vector_float2.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Los modelos de los objetos de un mundo (CL7.2): lo que 0x616ec0 lee para el mapa. Primero los
// especiales de cada familia, con sus texturas desde 0x60d530(0x69); Lorencia usa después los
// 112 nombres de 0x619a21..0x619ea1. En los demás sigue el lazo
// genérico (0x61a0c4..0x61a0e1): k de 0 a 159 es Object<N>\Object<k + 1>.bmd ("%s0%d.bmd" hasta el 9).
// Sus texturas, de Object<N>\, siguen la tabla desde 0x60d530(0x41c), con GL_NEAREST y
// repitiendo (0x60d5f0(k, "Object<N>\\", 0x2901, 0x2600, 1)). Contrato: 05 § Los modelos de cada mundo
// y 06 § El mundo del login. Los efectos y objetos fijos de los especiales los crea MapLoad.
namespace Client::Scenes
{
struct WorldSpecialModelSet;
class ModelTextureTable;
inline constexpr std::size_t WorldModelCount = 160;
// El dragón que vuela en el login (06 § El dragón de fondo): solo en el mapa 55.
inline constexpr std::uint16_t DragonModel = 0xb4;
// El lancero del asedio: 0x612d90(0x4d) lo carga en el modelo 0x152 + 0x4d (Monster78) con el mapa 30 (0x42aa6b).
inline constexpr std::uint16_t SpearmanModel = 0x19f;
inline constexpr Render::TextureSlot WorldModelsFirstSlot{0x41c};

class WorldModels final : public IModelLibrary
{
public:
    // Lee los de `map` desde `dataRoot` y les da sus texturas siguiendo a `gameSlots` (la tabla de la
    // carga, global en el original) desde 0x41c, o una tabla vacía sin ella, y las sube. Una familia
    // cuyos especiales requieren un servicio ausente devuelve error después de soltar la anterior. Un modelo que
    // falta se saltea sin avisar, como el original (Q-C23). Una textura que falta o no sube se saltea
    // con un aviso, como en GameModels: el original la encuentra por nombre en la tabla, cargada por
    // otro (fire01 y fire02 del tipo 89, de 0x615700, que MuRet todavía no carga entera), y su malla
    // sale sin textura. `globalModels`, si se presta, vive hasta Release y no cambia: Blood Castle
    // lo exige antes de consumir recursos; Gate01/02 ausentes se saltean como archivos opcionales.
    // El mapa superpone solo sus bindings de
    // texturas, sin copiar ni modificar esos modelos. Los slots y metadata propios prevalecen sobre
    // los globales, y solo se publican tras una subida efectiva.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         Render::ITextureStore& textures, Core::Log& log,
                                                         const Render::Models::ModelTextureSlots* gameSlots,
                                                         const IModelLibrary* globalModels = nullptr,
                                                         ModelTextureTable* sharedTextures = nullptr);
    // Suelta las texturas y los modelos.
    void Release();

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    [[nodiscard]] std::optional<TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;

    // Cuántos se leyeron.
    [[nodiscard]] std::size_t Count() const;
    // Los slots que repartió Load, si cargó: el próximo es donde sigue el logo (0x61c7c0).
    [[nodiscard]] const Render::Models::ModelTextureSlots* TextureSlots() const;
    // 612d90 continúa el cursor global con las texturas de Monster32/34.
    [[nodiscard]] Render::Models::ModelTextureSlots* TextureSlots();

private:
    void LoadFile(const std::filesystem::path& dataRoot, const Data::Models::ModelFile& file, Core::Log& log,
                  std::size_t& missing);
    void AssignSpecialTexture(const Data::Models::ModelFile& file, const WorldSpecialModelSet& specials);
    [[nodiscard]] Core::Expected<void, std::string> LoadSpecials(const std::filesystem::path& dataRoot,
                                                                 const WorldSpecialModelSet& specials, Core::Log& log,
                                                                 std::size_t& missing);
    void LoadObjects(const std::filesystem::path& dataRoot, std::uint8_t map, Core::Log& log, std::size_t& missing);
    [[nodiscard]] Core::Expected<void, std::string> ConfigureActions(std::uint8_t map,
                                                                     const WorldSpecialModelSet& specials);
    [[nodiscard]] Core::Expected<void, std::string> LoadTheSpearman(const std::filesystem::path& dataRoot,
                                                                    Core::Log& log, std::size_t& missing);
    void UploadTextures(const std::filesystem::path& dataRoot, Render::ITextureStore& textures, Core::Log& log);

    std::string m_folder;
    std::map<std::uint16_t, Data::Models::Model> m_models;
    std::map<std::uint16_t, Render::Models::Skeleton> m_skeletons;
    std::map<std::uint16_t, std::vector<Render::TextureSlot>> m_meshTextures;
    std::optional<Render::Models::ModelTextureSlots> m_slots;
    Render::ITextureStore* m_store = nullptr;
    ModelTextureTable* m_sharedTextures = nullptr;
    std::size_t m_requestStart = 0;
    // Los que subió Load, para soltarlos.
    std::vector<Render::TextureSlot> m_uploaded;
    std::map<std::uint32_t, TextureMetadata> m_textureMetadata;
    // No poseedor: los modelos y texturas globales viven hasta Release; no se copian ni se modifican.
    const IModelLibrary* m_globalModels = nullptr;
};
} // namespace Client::Scenes
