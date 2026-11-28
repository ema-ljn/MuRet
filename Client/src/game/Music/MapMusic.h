#pragma once

#include "Audio/MusicPlayer.h"

#include <cstdint>
#include <functional>

namespace Client::Music
{
// 0x6388dd..0x639413, the music block of 0x637700: the 11 groups in their order. inTheDeviasChurch runs only on map 2
// in the safe zone.
void StepTheMapMusic(Audio::MusicPlayer& player, std::int32_t map, bool safeZone, std::int32_t heroCellLayer,
                     const std::function<bool()>& inTheDeviasChurch);

// 0x622b00: 0x4acf60 over the 22 slots.
void StopTheWorldMusic(Audio::MusicPlayer& player);
} // namespace Client::Music
