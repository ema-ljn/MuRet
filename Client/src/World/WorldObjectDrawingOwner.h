#pragma once
#include "World/WorldObjectRegistrations.h"
#include <cstdint>
namespace Client::World
{
// Entry del scan400: ID durable, flags+78 y byte+370 vivos.
struct WorldObjectDrawingOwner
{
    WorldObjectIdentity identity;
    std::uint32_t flags;
    std::uint8_t state;
};
} // namespace Client::World
