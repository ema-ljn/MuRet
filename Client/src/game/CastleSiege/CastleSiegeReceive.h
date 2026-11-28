#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/CastleSiege/CastleSiege.h"
#include "Protocol/Outgoing.h"

#include <functional>
#include <filesystem>
#include <cstdint>
#include <string>

namespace Client::Core
{
class Log;
}

namespace Client::Tables
{
class TextTable;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Kernel
{
class WorldState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Terrain
{
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The incoming castle packets outside the battle (CL9 milestone 12; 04 § Castle Siege, las ventanas del castillo):
// what 0x4e2b10 and its functions do with B2 (0x00..0x10, 0x1A and 0x1F), B3, B4, B5, B6 and B8 on
// WorldRuntime::castleSiege, the hero, the panels and the chat. None looks at the scene or the encryption flag.
namespace Client::CastleSiege
{

// The GameServer: the C1 that B2 02 with 1 sends (0x4df1a0).
using CastleSender = std::function<void(Protocol::Outgoing)>;

struct CastleReceiveContext
{
    Kernel::WorldState& world;
    // The CRT's rand(): what B2 00 opens (0x6599a0) goes through the panel registry.
    Crt::CrtRandom& random;
    // What is discarded (a record that would fall outside 0x7d5c504..0x7d5c658, Q-C1360) goes to the log.
    Core::Log& log;
    // What measures the messages (0x555190); without a measurer, whatever shows one stops with an error.
    const Render::Text::ITextMeasure* measure = nullptr;
    Core::PixelSize screen{640, 480};
    // Text.bmd: 860, 1386, 1500..1527, 1609 and 1616.
    const Tables::TextTable* texts = nullptr;
    CastleSender send{};
    std::filesystem::path dataRoot{};
    std::uint32_t tick = 0;
    const Scenes::WorldEntryResources* entryResources = nullptr;
    // The current map's terrain, whose .att and light the siege's B2 reloads (0x4294c0); without it, that stops.
    Terrain::WorldTerrain* terrain = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna), whose texture table takes the B9 02's castle flag;
    // without them, that stops.
    Assets::FaunaModels* faunaModels = nullptr;
};

// 0x4e2b10 with a castle packet: B2 00 (0x4e50bd), 01 (0x4df080), 02 (0x4df1a0), 03 (0x4e5186), 04 (0x4e51fa), 05
// (0x4df7d0), 06 (0x4e52b7), 07 (0x4df950), 08 (0x4e537b), 09 (0x4e53e7), 10 (0x4e548a), 1A (0x4e54f6) and 1F
// (0x4e5532); B3 (0x4dfc60), B4 (0x4dfce0), B5 (0x4dfd80), B6 (0x4e0ed0) and B8 01 (0x4e0700).
[[nodiscard]] Core::Expected<void, std::string>
ApplyCastleSiegePacket(const Protocol::CastleSiege::CastleSiegePacket& packet, const CastleReceiveContext& context);

// 0x64b0f0, what B2 00 with 1 or 2 stores in [0x7d536f8] after opening panel 0x80000: +4 at 1 (the open byte,
// DelegatedPanels::castleSiege), the state, the two names with strncpy, the three dates and the rest.
void StoreTheCastleState(const Protocol::CastleSiege::CastleState& state, Kernel::WorldRuntime& runtime);
} // namespace Client::CastleSiege
