#pragma once

#include <cstdint>
#include <map>

namespace Client::WorldObjects
{
// model+88: a selection shared per type, not a per-instance field. Absent equals the loaded model's initial -1. The
// owner keeps or clears the table with its models' lifecycle.
struct ObjectModelStepState
{
    std::map<std::int16_t, std::int8_t> unlitMeshes;
};
} // namespace Client::WorldObjects
