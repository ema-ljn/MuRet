#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/Guild/Guild.h"
#include "Guild/GuildPanels.h"

#include <cstdint>
#include <string>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The incoming guild, union and war packets (CL9 milestone 8; 04 § El gremio, la alianza y la guerra; 06 § El
// gremio): what 0x4e2b10 and its functions do on WorldRuntime::guild, the characters, the panels and the chat. None
// looks at the scene or the encryption flag.
namespace Client::Guild
{

struct GuildReceiveContext
{
    Kernel::WorldState& world;
    // The CRT's rand(): the panels registry's queries draw it (0x6599a0 and 0x65cae0).
    Crt::CrtRandom& random;
    // What measures the messages (0x555190, 0x554610) and the dialog's rows (0x6600d0); without it, whatever uses them
    // stops.
    const Render::Text::ITextMeasure* measure = nullptr;
    Core::PixelSize screen{640, 480};
    // Text.bmd: the texts of the results and the dialogs.
    const Tables::TextTable* texts = nullptr;
    // 0x57678bc: the player's key (the E6 compares it).
    std::uint16_t playerKey = 0;
    // GetTickCount(): what the panels' dialog opens with (0x6600d0).
    std::uint32_t now = 0;
    // The GameServer: the C1s that the 50, 53, 62, 63, 65, E5 and EB send, and the windows when they close.
    GuildSender send{};
    // The models' actions (0x4ea030) for the war gesture: the 62 and the 63 call 0x576a60 with the hero
    // (Actors::PlayTheEmote, CL9 milestone 1) before their C1 18. Without them, they stop before the gesture.
    const Models::IModelLibrary* models = nullptr;
    // The 54 opens creation and closes the NPC/castle/battle windows through the world's canonical services.
    const Scenes::WorldEntryResources* worldEntry = nullptr;
};

// 0x4e2b10 with a guild packet: 50 (0x4d74b0), 51 (0x4d7a20), 52 (0x4d7b60), 53 (0x4d7c30), 54 (0x4e4279), 56
// (0x4d7ff0), 5D (0x4e42f0), 60 (0x4d8150), 61 (0x4e4382), 62 (0x4d8260), 63 (0x4d8870), 64 (0x4e4400), 65 (0x4d9260),
// 66 (0x4e42c7 → 0x447b90), 67 (0x4e44aa), E1 (0x4d9810), E5 (0x6565e0), E6 (0x4d9940), E9 (0x4d9e50) and EB
// (0x4d9b80).
[[nodiscard]] Core::Expected<void, std::string> ApplyGuildPacket(const Protocol::Guild::GuildPacket& packet,
                                                                 const GuildReceiveContext& context);

// 0x4c1800: no war (0x576796c at 0), no rival (0x6c82f0 at −1 and the first byte of 0x5767804 at 0) and +0x374 of
// each live character at 1 if it is of the hero's guild (with a guild) and at 0 if not. The 60 without a war and the
// no of dialog 0x7f call it (0x62c854 and 0x62c86a, twice).
void ClearTheWar(Kernel::WorldState& world);
} // namespace Client::Guild
