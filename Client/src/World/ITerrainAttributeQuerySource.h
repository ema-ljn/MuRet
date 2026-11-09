#pragma once

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

namespace Client::World
{
// Proveedor obligatorio del factor vigente y la tabla canónica del mundo actual.
// El dueño mantiene vivos los datos durante la consulta y serializa sus mutaciones.
// ReadScale se llama para Y y luego X; Attributes se pide después de ambas conversiones.
class ITerrainAttributeQuerySource
{
public:
    virtual ~ITerrainAttributeQuerySource() = default;
    virtual float ReadScale() = 0;
    virtual const Data::Terrain::TerrainAttributes& Attributes() = 0;
};
} // namespace Client::World
