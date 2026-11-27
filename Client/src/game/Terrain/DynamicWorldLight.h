#pragma once

#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainLighting.h"

#include <memory>

// La tabla dinámica de 0x7b4ccc4 y el rango que dejó el último 0x5fbb00: viven de una escena a
// otra, sin vaciarse al cargar un mapa. Contrato: 06 § La luz del terreno en un punto; 08 P-C46 (a).
namespace Client::Terrain
{
class DynamicWorldLight
{
public:
    DynamicWorldLight();

    [[nodiscard]] Terrain::TerrainLighting& Lighting();
    [[nodiscard]] const Terrain::TerrainLighting& Lighting() const;
    [[nodiscard]] const Terrain::TerrainRange& Range() const;

    // 0x5fbb00 en el dibujo: publica los cuatro límites sin copiar ni vaciar luz.
    void PublishRange(const Terrain::TerrainRange& range);
    // Primer bloque de 0x5fbf60, en el paso: copia base en la ventana del último dibujo, con
    // lastX/lastY + 3 y wrap. Fuera de ella conserva la historia. No actualiza las olas ni suma luces.
    // Los límites mantienen las precondiciones enteras de Terrain::CopyTerrainLightWindow.
    void Update(const Terrain::TerrainLighting& base);

private:
    std::unique_ptr<Terrain::TerrainLighting> m_lighting;
    Terrain::TerrainRange m_range{};
};
} // namespace Client::Terrain
