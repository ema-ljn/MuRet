#pragma once

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

namespace Client::Terrain
{
// The required provider of the current factor and the current world's canonical table. The owner keeps the data
// alive during the query and serializes its mutations. ReadScale is called for Y and then X; Attributes is asked for
// after both conversions.
class ITerrainAttributeQuerySource
{
public:
    virtual ~ITerrainAttributeQuerySource() = default;
    virtual float ReadScale() = 0;
    virtual const Data::Terrain::TerrainAttributes& Attributes() = 0;
};
} // namespace Client::Terrain
