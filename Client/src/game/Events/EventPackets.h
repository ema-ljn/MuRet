#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Outgoing.h"
#include "Audio/SoundId.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
struct Skeleton;
} // namespace Client::Models

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Kernel
{
class WorldState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Terrain
{
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

// CL9 milestone 11: what the event packets (Protocol::Events, 04 § Los eventos) do on arrival, like dispatcher 0x4e2b10
// and its handlers, on WorldRuntime's globals: the notices (0x632e30, 0x633300), the messages (0x555190), the .att
// (0x7d0d348), the event object of [0x723c60], the chips (0x78abb00), Crywolf and the characters. What belongs to
// another wave or milestone stops with its address, after what the original did before; what the original does outside
// its tables is dropped (EventPacketResult::discarded).
namespace Client::Events
{
struct EventMapObject;

struct EventPacketContext
{
    Kernel::WorldState& world;
    // The CRT rand(), which the panel closes spend (0x657a90, 0x65cae0).
    Crt::CrtRandom& random;
    // Text.bmd: without the table, a notice stops.
    const Tables::TextTable* texts = nullptr;
    // Item.bmd, from which 0x5b1c70 takes the item name of 47; without the table, that notice stops.
    const Tables::ItemTable* items = nullptr;
    // The meter and the screen of the messages (0x555190); without a meter, a message stops.
    const Render::Text::ITextMeasure* measure = nullptr;
    Core::PixelSize screen{640, 480};
    // GetTickCount when applying the packet (0x40ba60).
    std::uint32_t tickCount = 0;
    // Player.bmd, the skeleton of 0x20a for 0x4ea030 from 0x5111a0; without it, that case stops.
    const Models::Skeleton* playerSkeleton = nullptr;
    // The models by type: 0x4ea030 reads the actions of the model of the character's type (+2), like the hero's 0xca in
    // BD 03, which with a transformation ring is not Player.bmd. Without them, that case stops.
    const Models::IModelLibrary* models = nullptr;
    // The C1 03 31 that closes the event NPC (0x658a18 and 0x658c0e) from 90 and the closes of all panels. Empty,
    // closing it stops.
    std::function<void()> sendWindowClose{};
    // The world packets' send(): the C1 05 18 of BD 03 with result 1 (0x4e11e1). Empty, that send stops.
    std::function<void(Protocol::Outgoing)> send{};
    // The Data folder, from which BD 00 reloads the light and the .att with another Crywolf state (0x42ff81 and
    // 0x42ffe8). Empty, that change stops.
    std::filesystem::path dataRoot{};
    // What 0x657a90 sends and uses when closing the NPC windows (0x657ab0 with 0x40, 0x100, 0x8000 and 0x10000): C1 03
    // 82, C1 03 87 and C1 03 31 in the clear through the session (App::ServerSession::SendNpcWindowPacket) and the
    // inventory services (the cursor item and the notices). Empty, closing one of them stops where it sends.
    std::function<Core::Expected<void, std::string>(Protocol::Outgoing)> sendNpcWindow{};
    const Items::InventoryServices* inventory = nullptr;
    // The current map's terrain, whose .att the events mark and Crywolf reloads. Without it, those stop.
    Terrain::WorldTerrain* terrain = nullptr;
};

// The sounds it requested, in order: 0x41ed10 (those that play) and 0x41ee00 (those that stop). The last one played
// also stays in WorldRuntime::lastSoundId, the CL10 hook.
struct EventPacketResult
{
    std::vector<Audio::SoundId> sounds;
    std::vector<Audio::SoundId> stoppedSounds;
    // Where and why MuRet dropped the rest of the packet: where the original reads or writes outside its tables and
    // goes on (the short 93 and 94, Q-C1324, Q-C1325, Q-C1328, Q-C1341 and Q-C1347, and the strcat of Chaos Castle's
    // 91, Q-C1109). What came before, with its rand(), stays done; the session logs it and goes on, as with the D3 and
    // the 18 of an unknown key (Q-C174, Q-C778).
    std::optional<std::string> discarded;
};

[[nodiscard]] Core::Expected<EventPacketResult, std::string>
ApplyEventPacket(const Protocol::Events::EventPacket& packet, const EventPacketContext& context);

// 0x40bab0, the text of the 30 s countdown to enter Devil Square, Blood Castle and Chaos Castle with +8 from 1 to 14:
// nothing with another value; 30 s past +0xc (the unsigned subtraction), +8 at 0 if `write` and nothing; otherwise the
// text with the seconds left (wsprintfA). Stops without Text.bmd.
[[nodiscard]] Core::Expected<std::optional<std::string>, std::string>
EventCountdownText(EventMapObject& event, std::uint32_t tickCount, const Tables::TextTable* texts, bool write);

// 0x42fdc0: the Crywolf interface globals to their initial value, the state and the stage at 0, the arrival's chosen
// NPC (0x6c8e54, Kernel::ChosenNpc) and the second of the chosen targets (0x6c8e2c) at −1. Called by the
// return to the selection (0x4c363a) and BD 00 with stage 6.
void ResetTheCrywolfInterface(Kernel::WorldRuntime& runtime);
} // namespace Client::Events
