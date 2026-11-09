#pragma once
#include <cstdint>
namespace Client::World
{
enum class WorldObjectDrawingError : std::uint8_t
{
    MissingModel,
    MissingBinding,
    IndeterminateInput,
    InvalidTime,
    ProtectedBoundary,
};
} // namespace Client::World
