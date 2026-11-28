#pragma once

#include "Kernel/KalimaWater.h"
#include "MapScripts/Kalima/KalimaWaves.h"
#include "MapScripts/Kalima/KalimaWorld.h"

namespace Client::Tests
{
// The waves of the map's water, which only KalimaWorld implements.
inline const MapScripts::Kalima::KalimaWaveState& WavesOf(const Kernel::KalimaWater& water)
{
    return dynamic_cast<const MapScripts::Kalima::KalimaWorld&>(water).Waves();
}
} // namespace Client::Tests
