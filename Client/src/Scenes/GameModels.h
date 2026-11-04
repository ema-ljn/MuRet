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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

// Los modelos del personaje (0x60dbe0) y de los ítems (0x60ff50), con el esqueleto de cada uno y el
// slot de la textura de cada malla, como los lee la carga en su avance 1 (0x61ccb0) y los usan las
// escenas que siguen. Viven en main.cpp, como GameData: se leen una vez, y sus texturas quedan.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario", y el plan de
// la CL6 (pregunta 4 y CL6.3).
namespace Client::Scenes
{
class GameModels final : public IModelLibrary
{
public:
    // Lee las 213 filas de PlayerModelFiles y las 243 de ItemModelFiles, y les da a sus mallas las
    // texturas que piden 0x60f7d0 y 0x611850 (PlayerTextureCalls e ItemTextureCalls: cada llamada con
    // su carpeta, desde 0x138 y desde 0x25a, con ModelTextureSlots), y las sube con las fijas de
    // PlayerFixedTextures. Un modelo que el original exige (Player, PlayerTest) y falta corta, con un
    // error que lo nombra (Q-C23); otro que falta, o una textura que no carga, se saltea con una
    // línea en el log (el original, con una textura que falta, se cierra).
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot,
                                                         Render::ITextureStore& textures, Core::Log& log);

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override;
    [[nodiscard]] const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override;
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override;

    // Cuántos modelos se leyeron.
    [[nodiscard]] std::size_t Count() const;
    // Los slots que repartió la carga, con el nombre de cada uno.
    [[nodiscard]] const Render::Models::ModelTextureSlots& TextureSlots() const;

private:
    void AssignTextures(std::span<const Data::Models::ModelTextureCall> calls);

    std::map<std::uint16_t, Data::Models::Model> m_models;
    std::map<std::uint16_t, Render::Models::Skeleton> m_skeletons;
    std::map<std::uint16_t, std::vector<Render::TextureSlot>> m_meshTextures;
    // Desde 0x138 (0x60f7d0); Load le corre el próximo a cada cargador.
    Render::Models::ModelTextureSlots m_slots{Render::TextureSlot{0x138}};
};
} // namespace Client::Scenes
