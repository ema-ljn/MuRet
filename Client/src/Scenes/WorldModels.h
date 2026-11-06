#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Render/ITextureStore.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Los modelos de los objetos de un mundo (CL7.2): lo que 0x616ec0 lee para el mapa. Primero los
// especiales del mapa, con sus texturas desde 0x60d530(0x69): en el 55, el dragón 0xb4; en el 0,
// Bird01/Fish01. Lorencia usa los 112 nombres de 0x619a21..0x619ea1. En los demás sigue el lazo
// genérico (0x61a0c4..0x61a0e1): k de 0 a 159 es Object<N>\Object<k + 1>.bmd ("%s0%d.bmd" hasta el 9).
// Sus texturas, de Object<N>\, siguen la tabla desde 0x60d530(0x41c), con GL_NEAREST y
// repitiendo (0x60d5f0(k, "Object<N>\\", 0x2901, 0x2600, 1)). Contrato: 06 § El mundo del login.
namespace Client::Scenes
{
inline constexpr std::size_t WorldModelCount = 160;
// El dragón que vuela en el login (06 § El dragón de fondo): solo en el mapa 55.
inline constexpr std::uint16_t DragonModel = 0xb4;
inline constexpr Render::TextureSlot WorldModelsFirstSlot{0x41c};

class WorldModels final : public IModelLibrary
{
public:
    // Lee los de `map` desde `dataRoot` y les da sus texturas siguiendo a `gameSlots` (la tabla de la
    // carga, global en el original) desde 0x41c, o una tabla vacía sin ella, y las sube. Un modelo que
    // falta se saltea sin avisar, como el original (Q-C23). Una textura que falta o no sube se saltea
    // con un aviso, como en GameModels: el original la encuentra por nombre en la tabla, cargada por
    // otro (fire01 y fire02 del tipo 89, de 0x615700, que MuRet todavía no carga entera), y su malla
    // sale sin textura.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         Render::ITextureStore& textures, Core::Log& log,
                                                         const Render::Models::ModelTextureSlots* gameSlots);
    // Suelta las texturas y los modelos.
    void Release();

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;

    // Cuántos se leyeron.
    [[nodiscard]] std::size_t Count() const;
    // Los slots que repartió Load, si cargó: el próximo es donde sigue el logo (0x61c7c0).
    [[nodiscard]] const Render::Models::ModelTextureSlots* TextureSlots() const;

private:
    std::string m_folder;
    std::map<std::uint16_t, Data::Models::Model> m_models;
    std::map<std::uint16_t, Render::Models::Skeleton> m_skeletons;
    std::map<std::uint16_t, std::vector<Render::TextureSlot>> m_meshTextures;
    std::optional<Render::Models::ModelTextureSlots> m_slots;
    Render::ITextureStore* m_store = nullptr;
    // Los que subió Load, para soltarlos.
    std::vector<Render::TextureSlot> m_uploaded;
};
} // namespace Client::Scenes
