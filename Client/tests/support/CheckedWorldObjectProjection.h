#pragma once

#include "Data/Terrain/TerrainObjects.h"
#include "WorldObjects/WorldObjectRegistrations.h"
#include "WorldObjects/WorldObjects.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Client::Tests
{
// Fixtures de entradas válidas: registrar un failure hace fallar el test antes de devolver el prefijo.
// Las pruebas del canal de error llaman directamente a la API y observan su resultado completo.
inline std::vector<WorldObjects::WorldObject>
CheckedWorldObjectProjection(std::span<const Data::Terrain::TerrainObject> records, std::uint8_t map,
                             WorldObjects::ObjectScene scene,
                             WorldObjects::WorldObjectRegistrations* registrations = nullptr)
{
    auto result = registrations == nullptr ? WorldObjects::CreateWorldObjects(records, map, scene)
                                           : WorldObjects::CreateWorldObjects(records, map, scene, *registrations);
    EXPECT_FALSE(result.failure.has_value());
    return std::move(result.objects);
}
} // namespace Client::Tests
