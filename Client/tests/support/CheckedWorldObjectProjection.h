#pragma once

#include "Data/Terrain/TerrainObjects.h"
#include "Kernel/WorldObject.h"
#include "WorldObjects/WorldObjects.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Client::Tests
{
// Fixtures for valid inputs: recording a failure fails the test before returning the prefix. The error channel tests
// call the API directly and observe their full result.
inline std::vector<Kernel::WorldObject>
CheckedWorldObjectProjection(std::span<const Data::Terrain::TerrainObject> records, std::uint8_t map,
                             WorldObjects::ObjectScene scene, Kernel::WorldObjectRegistrations* registrations = nullptr)
{
    auto result = registrations == nullptr ? WorldObjects::CreateWorldObjects(records, map, scene)
                                           : WorldObjects::CreateWorldObjects(records, map, scene, *registrations);
    EXPECT_FALSE(result.failure.has_value());
    return std::move(result.objects);
}
} // namespace Client::Tests
