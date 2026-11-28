#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/ItemTable.h"
#include "Tables/GateTable.h"
#include "Tables/SkillTable.h"
#include "Tables/TextTable.h"
#include "Pathfinding/PathCell.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Outgoing.h"
#include "Actors/ActionSend.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

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
} // namespace Client::Render::Text

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Terrain
{
struct TerrainLighting;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

namespace Client::Movement
{
class IHeroMovementHooks;

// +0x379 at 2: the arrival talks to the NPC of 0x6c8e54.
inline constexpr std::uint8_t NpcTalkMovement = 2;
// 0x59daaf..0x59dabe: with this bit of +0x7c the click does not set +0x379 to 2 (the one there stays).
inline constexpr std::uint32_t NpcTalkBlockedFlag = 0x10000000;

struct HeroMovementInput
{
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    bool leftPressed = false;
    bool leftDown = false;
    bool shiftDown = false;
    float worldTime = 0.0f;
    std::uint32_t ticksMs = 0;
    // GetAsyncKeyState(0x11), which 0x558bb0 checks at 0x558e7f to attack another player from the click (CL9 milestone
    // 3).
    bool controlDown = false;
};
struct HeroMovementContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    const Models::Skeleton* playerSkeleton = nullptr;
    const Tables::ItemTable* items = nullptr;
    const Tables::SkillTable* skills = nullptr;
    const Tables::GateTable* gates = nullptr;
    HeroMovementInput input{};
    // The service uses the previous draw's ray, range and trapezoid.
    std::function<Core::Expected<std::optional<Pathfinding::PathCell>, std::string>()> pickTarget{};
    std::function<Core::Expected<void, std::string>(std::span<const Protocol::Movement::Cell>, Protocol::Movement::Cell,
                                                    float)>
        sendWalk{};
    std::function<void(std::uint8_t)> requestGate{};
    float fixedHeight = 1200.0f;
    std::function<Core::Expected<void, std::string>(std::uint8_t)> sendFacing{};
    // CL9 milestone 11, SendMove with 0x78abb00 (0x56b4db..0x56b78f): the C1 03 97 in clear (without the service, it
    // stops), and SetCursorPos ([0x6b84f4]) with the place the original computes on the `screen` screen ([0x6d5d60] and
    // [0x6d5d64]), which stays requested in GameMovementState::cursorWarpRequest.
    std::function<Core::Expected<void, std::string>()> sendEventWindowLeave{};
    // The D7 for hitting on arrival (0x5650c0, CL9 milestone 3): the target's key, the action and the heading. Empty,
    // it sends nothing.
    std::function<Core::Expected<void, std::string>(std::uint16_t, std::uint8_t, std::uint8_t)> sendAttack{};
    // The C1 05 DF X Y of the elf that walks with a ranged weapon and clicks an attackable (0x59cf88, CL9 milestone 3).
    // Empty, it sends nothing.
    std::function<Core::Expected<void, std::string>(std::uint8_t x, std::uint8_t y)> sendElfCell{};
    // The hero's C1 18 for sitting (0x4f2250, 0x4f2416..0x4f2bea). Empty: sitting stops with an error.
    Actors::ActionSender sendAction{};
    // Dressing the hero and sending the 24, to move the left weapon to the right (0x59bd3e). nullptr: it stops where it
    // asks for them.
    const Items::InventoryServices* inventory = nullptr;
    // 0x419900: the C1 03 31 on closing the quests, which walking closes (0x56b397..), and the one for closing the
    // event NPC (0x56b4d6 → 0x65cae0(0x1000), 0x658a18; CL9 milestone 11). Without it, closing them stops.
    std::function<void()> sendWindowClose{};
    // The C1 03 3D that walking sends on closing the trade (0x56b3b2 → 0x65cae0(0x20) with 0, 0x657fe1). Without it,
    // walking with the trade open stops.
    std::function<void(Protocol::Outgoing)> sendGamePacket{};
    // What walking sends on closing the NPC windows (0x56b3d2, 0x56b41e and 0x56b48a → 0x65cae0 → 0x657ab0): the
    // warehouse's C1 03 82, the chaos box's C1 03 87 and the shop's C1 03 31, in clear through the session
    // (App::ServerSession::SendNpcWindowPacket). Empty, the close stops where it sends.
    std::function<Core::Expected<void, std::string>(Protocol::Outgoing)> sendNpcWindow{};
    // The arrival at an NPC (0x5674c8, Npc/NpcTalk.h): the C3 A0 without the quest class (0x5676a6) and the C3 30
    // (0x567c2d..). The step draws the two rand() of each C3. Without them, the arrival stops where it sends them.
    std::function<void()> requestQuestList{};
    std::function<void(std::int16_t)> sendTalk{};
    // Text.bmd: the texts of the NPC arrival notices (663 of 0xee, 0x567618, and 1830 of 0x101, 0x567ad3). nullptr:
    // those notices stop.
    const Tables::TextTable* texts = nullptr;
    // 0x57ceea: the terrain point under the mouse for a skill without a chosen one (0x5fc260(1) and 0x5fac90). Empty:
    // it stops.
    std::function<Core::Expected<std::optional<glm::vec3>, std::string>()> pickTerrainPoint{};
    // What 0x57cfc0 sends (the skills, the raven and the mana potion). Empty, nothing goes out.
    std::function<void(Protocol::Outgoing)> sendSkill{};
    // The player key of the F1 00 ([0x57678bc]), which the skills send in the C1 19 (CL9 milestone 4, phase 2c).
    std::uint16_t playerKey = 0;
    // 0x555190, for those notices: the text measure and the screen. Without a measure, they stop. The screen is also
    // the one for SendMove's SetCursorPos with 0x78abb00 (CL9 milestone 11).
    const Render::Text::ITextMeasure* measure = nullptr;
    Core::PixelSize screen{640, 480};
    // The base library for the actions of transformed characters that are not in AssetsState::fauna.
    const Models::IModelLibrary* models = nullptr;
    // 0x599ef0 → 0x61c210/0x61c170: options and macros before entering Castle Siege or Land of Trial.
    std::function<Core::Expected<void, std::string>()> saveOptionsAndMacros{};
    // The current map's terrain (Terrain::TerrainState::current). Without it, an empty one: walking stops where it
    // reads the TerrainWall.
    const Terrain::WorldTerrain* terrain = nullptr;
    // The dynamic light (Terrain::TerrainState::dynamicLight) the skills' effects add to; nullptr: adding one stops.
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna). nullptr: readers find no model, writers stop.
    Assets::FaunaModels* faunaModels = nullptr;
    // What the step asks of other modules (Session::HeroMovementHooks). nullptr: the step does not start.
    const IHeroMovementHooks* hooks = nullptr;
};
[[nodiscard]] Core::Expected<void, std::string> StepHeroMovement(const HeroMovementContext& context);
} // namespace Client::Movement
