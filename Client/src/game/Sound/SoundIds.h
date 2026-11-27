#pragma once

#include "Audio/SoundId.h"

// The sounds of the catalog that more than one module asks for, named after their WAV (SoundCatalog.cpp).
namespace Client::Sound
{
inline constexpr Audio::SoundId ButtonClickSound{0x19};
inline constexpr Audio::SoundId CreateWindowSound{0x1c};
inline constexpr Audio::SoundId GetItemSound{0x1d};
inline constexpr Audio::SoundId WhisperSound{0x26};
inline constexpr Audio::SoundId ExplosionSound{0x40};
inline constexpr Audio::SoundId MeteoriteSound{0x42};
inline constexpr Audio::SoundId GemSound{0x45};
inline constexpr Audio::SoundId WallFallSound{0x61};
inline constexpr Audio::SoundId KnightSkill3Sound{0x72};
inline constexpr Audio::SoundId MagicSound{0x76};
inline constexpr Audio::SoundId AquaFlashSound{0x7a};
inline constexpr Audio::SoundId SoulBarrierSound{0x84};
inline constexpr Audio::SoundId HitCristalSound{0x88};

// The WAVs Blood Castle and Chaos Castle loop as their music (0x41ed10 with loop 1) and stop with a rewind (0x41ee00).
inline constexpr Audio::SoundId BloodCastleSound{0x8b};
inline constexpr Audio::SoundId ChaosCastleSound{0x99};
inline constexpr Audio::SoundId ChaosSound{0x9e};
} // namespace Client::Sound
