#pragma once

#include "Data/Terrain/TerrainObjects.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldObjects.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Client::Tests
{
// Fixtures de entradas válidas: registrar un failure hace fallar el test antes de devolver el prefijo.
// Las pruebas del canal de error llaman directamente a la API y observan su resultado completo.
inline std::vector<World::WorldObject>
CheckedWorldObjectProjection(std::span<const Data::Terrain::TerrainObject> records, std::uint8_t map,
                             World::ObjectScene scene, World::WorldObjectRegistrations* registrations = nullptr)
{
    auto result = registrations == nullptr ? World::CreateWorldObjects(records, map, scene)
                                           : World::CreateWorldObjects(records, map, scene, *registrations);
    EXPECT_FALSE(result.failure.has_value());
    return std::move(result.objects);
}
} // namespace Client::Tests
