#pragma once

#include "World/TerrainCells.h"
#include "World/TerrainLighting.h"

#include <memory>

// La tabla dinámica de 0x7b4ccc4 y el rango que dejó el último 0x5fbb00: viven de una escena a
// otra, sin vaciarse al cargar un mapa. Contrato: 06 § La luz del terreno en un punto; 08 P-C46 (a).
namespace Client::Scenes
{
class DynamicWorldLight
{
public:
    DynamicWorldLight();

    [[nodiscard]] World::TerrainLighting& Lighting();
    [[nodiscard]] const World::TerrainLighting& Lighting() const;
    [[nodiscard]] const World::TerrainRange& Range() const;

    // 0x5fbb00 en el dibujo: publica los cuatro límites sin copiar ni vaciar luz.
    void PublishRange(const World::TerrainRange& range);
    // Primer bloque de 0x5fbf60, en el paso: copia base en la ventana del último dibujo, con
    // lastX/lastY + 3 y wrap. Fuera de ella conserva la historia. No actualiza las olas ni suma luces.
    // Los límites mantienen las precondiciones enteras de World::CopyTerrainLightWindow.
    void Update(const World::TerrainLighting& base);

private:
    std::unique_ptr<World::TerrainLighting> m_lighting;
    World::TerrainRange m_range{};
};
} // namespace Client::Scenes
