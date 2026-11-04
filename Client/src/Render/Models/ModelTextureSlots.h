#pragma once

#include "Data/Models/Model.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Los slots de las texturas de los modelos, como los reparten 0x60d530, 0x60d550 y 0x60d5f0
// (Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario"): como datos, sin GL,
// para comparar con las trazas y con lor_slots.bin, que nombran slots (pregunta 13 del plan de la
// CL6). Qué escena empieza en qué slot es de quien la arma (CL6.3).
namespace Client::Render::Models
{
class ModelTextureSlots
{
public:
    // 0x60d530: el próximo slot libre en `next`; la búsqueda por nombre empieza en el 0.
    explicit ModelTextureSlots(TextureSlot next);

    // Las rutas de ToLoad apuntan a las que guarda: una copia apuntaría a las del otro. Movido, siguen
    // valiendo, porque un std::deque movido no mueve sus elementos.
    ModelTextureSlots(const ModelTextureSlots&) = delete;
    ModelTextureSlots& operator=(const ModelTextureSlots&) = delete;
    ModelTextureSlots(ModelTextureSlots&&) = default;
    ModelTextureSlots& operator=(ModelTextureSlots&&) = default;
    ~ModelTextureSlots() = default;

    // 0x60d530 otra vez, como cada cargador del original: corre el próximo libre y deja lo que ya
    // tiene nombre. Puede ir para atrás (0x615700 vuelve al 0x138, Q-C89).
    void SetNext(TextureSlot next);

    // 0x60d5f0: el slot de cada malla de `model`, en su orden. Cada nombre de textura se busca, sin la
    // carpeta y distinguiendo mayúsculas (Q-C90), en los slots del 0 al próximo libre: si está, la
    // malla usa ese; si no, se pide `folder` + el nombre en el próximo, que se corre aunque el archivo
    // falte. Después, las de la piel y el pelo van a su slot fijo (Data::Models::SkinSlotOf), y el
    // que se pidió queda ocupado. `sampling` es el de las OZJ: las OZT van siempre con GL_NEAREST.
    std::vector<TextureSlot> Assign(const Data::Models::Model& model, std::string_view folder,
                                    Data::Textures::Sampling sampling);

    // Lo que pidió Assign y hay que subir, en orden, con el slot y el muestreo de cada una
    // (Data::Textures::LoadTextureSet): la carpeta, el nombre hasta el primer punto y la extensión del
    // cargador del original (Data::Models::ModelTextureLoaderOf), así abre el mismo archivo.
    [[nodiscard]] std::span<const Data::Textures::TextureSetEntry> ToLoad() const;

    [[nodiscard]] TextureSlot Next() const;

    // El nombre desnudo que guarda `slot`, como la tabla de 0x7d3a198: el de la última malla que lo
    // pidió, o vacío si nadie lo pidió o si está fuera de la tabla. Para comparar con un volcado.
    [[nodiscard]] std::string_view NameOf(TextureSlot slot) const;

private:
    std::optional<TextureSlot> Find(std::string_view name) const;
    TextureSlot Request(std::string_view folder, std::string_view name, Data::Textures::Sampling sampling);

    // El nombre desnudo que guarda cada slot, como la tabla de 0x7d3a198; vacío, sin nombre.
    std::vector<std::string> m_names;
    std::size_t m_next = 0;
    // Las rutas de ToLoad: un std::deque no mueve lo que ya guardó, y los string_view de m_toLoad
    // siguen valiendo.
    std::deque<std::string> m_paths;
    std::vector<Data::Textures::TextureSetEntry> m_toLoad;
};
} // namespace Client::Render::Models
