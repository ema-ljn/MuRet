#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelTextureCalls.h"
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

// Los modelos del personaje (0x60dbe0), de los ítems (0x60ff50) y el caballo oscuro con los tres que
// van antes de él en sus texturas (0x615700), con el esqueleto de cada uno y el slot de la textura de
// cada malla, como los lee la carga en su avance 1 (0x61ccb0) y los usan las escenas que siguen.
// Viven en main.cpp, como GameData: se leen una vez, y sus texturas quedan. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario" y "El caballo oscuro",
// y el plan de la CL6 (pregunta 4, CL6.3 y CL6.4).
namespace Client::Scenes
{
class GameModels final : public IModelLibrary
{
public:
    // Lee las 213 filas de PlayerModelFiles, las 243 de ItemModelFiles y las 7 de SkillModelFiles, y
    // les da a sus mallas las texturas que piden 0x60f7d0, 0x611850 y 0x615700 (PlayerTextureCalls,
    // ItemTextureCalls y SkillTextureCalls: cada llamada con su carpeta, desde 0x138, 0x25a y 0x3b8,
    // con ModelTextureSlots), Gate02 desde Monster en 0x69 antes del bloque 0x3b8, y las sube con las fijas de
    // PlayerFixedTextures, las de los pases de brillo (GlowTextureSet, del avance 0 de 0x61ccb0) y las de
    // los sprites (EffectTextureSet), el atlas de daño (DamageTextureSet, de 0x61bd80) y los joints
    // emitidos por fauna (FaunaJointTextureSet). Un
    // modelo que el original exige (Player, PlayerTest) y falta corta, con un
    // error que lo nombra (Q-C23); otro que falta, o una textura que no carga, se saltea con una
    // línea en el log (el original, con una textura que falta, se cierra).
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot,
                                                         Render::ITextureStore& textures, Core::Log& log);

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;
    [[nodiscard]] std::optional<TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override;
    // El slot de cada textura que subió, solo si quedó publicada: cada una va al de su entrada,
    // que es su selector. Es lo que 0x620780 liga para las partículas.
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override;
    // El tamaño de una textura que subió.
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override;

    // Cuántos modelos se leyeron.
    [[nodiscard]] std::size_t Count() const;
    // Los slots que repartió la carga, con el nombre de cada uno.
    [[nodiscard]] const Render::Models::ModelTextureSlots& TextureSlots() const;

private:
    void AssignTextures(std::span<const Data::Models::ModelTextureCall> calls);

    std::map<std::uint16_t, Data::Models::Model> m_models;
    std::map<std::uint16_t, Render::Models::Skeleton> m_skeletons;
    std::map<std::uint16_t, std::vector<Render::TextureSlot>> m_meshTextures;
    // La metadata de las texturas que efectivamente subió.
    std::map<std::uint32_t, TextureMetadata> m_textureMetadata;
    // Desde 0x138 (0x60f7d0); Load le corre el próximo a cada cargador.
    Render::Models::ModelTextureSlots m_slots{Render::TextureSlot{0x138}};
};
} // namespace Client::Scenes
