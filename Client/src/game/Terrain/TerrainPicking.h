#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct TerrainPickingSegment;
struct TerrainPickingState;
struct TerrainPickingTarget;
struct TerrainPolygon;
struct TerrainRange;
struct TerrainView;

// 0x5fc260(1): reset de flag/maxT, barrido y retry interno sólo si seleccionó. Conserva hit/XY al
// fallar. 06 § El rayo contra el terreno. forceVisible acepta después de evaluar ambos frustums.
// Contrato numérico PC53/nearest, stores float32 del ASM y sin FMA; no demuestra la CW del original.
// Requiere alturas/atributos, bloques 0..251, rayo finito, alturas en [0,765] y fixedHeight en
// [0,1201]. Valida sólo las esquinas visitadas. La selección retenida admite floats 0..254;
// el caller stock produce enteros. Ambos casos tienen anchos positivos y normales definidas.
// Snapshot estable y redondeo nearest son precondiciones; no reproduce flags/excepciones x87.
// Errores explícitos conservan el estado de entrada. No necesita mapping, luz ni texturas.
[[nodiscard]] Core::Expected<void, std::string> PickTerrain(TerrainPickingState& state, const TerrainView& view,
                                                            const TerrainRange& range, const TerrainPolygon& polygon,
                                                            const TerrainPickingSegment& segment,
                                                            bool forceVisible = false);

// Rama pura del consumidor MoveHero: pase anterior y OTRO retry, incluso tras miss, sobre la celda
// retenida. Publica target sólo si ese intento acierta (Value()==true); miss/error conserva target.
// La integración de Shift, cadencia/UI, rayo del último render y caminar queda en CL7.4E.
[[nodiscard]] Core::Expected<bool, std::string>
PickTerrainTarget(TerrainPickingState& state, TerrainPickingTarget& target, const TerrainView& view,
                  const TerrainRange& range, const TerrainPolygon& polygon, const TerrainPickingSegment& segment,
                  bool forceVisible = false);
} // namespace Client::World
