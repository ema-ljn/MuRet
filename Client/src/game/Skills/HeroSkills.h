#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Protocol/Outgoing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
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

namespace Client::Kernel
{
class WorldState;
struct ActorState;
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

// Using a skill from the game (0x57cfc0). Contract: Client/docs/research/06-render-escenas-y-ui.md, "Usar una skill".
// MoveHero calls it at the end of the click (0x59e255), except when the attack click leaves without arrows
// (MoveHeroEnd::Leave, 0x59d666).
namespace Client::Skills
{

struct HeroSkillContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    // Skill.bmd (0x76bd1d8). nullptr: 0x5526f0 corta.
    const Tables::SkillTable* skills = nullptr;
    // Player.bmd's actions, which 0x4ea030 gives a player's skills, and Item.bmd, which 0x4f4a10 reads with another.
    // nullptr: the one that asks for it cuts.
    const Models::Skeleton* playerSkeleton = nullptr;
    const Tables::ItemTable* items = nullptr;
    // The skeletons of the other models, which 0x4ea030 reads in the 0x4f4a10 of a hero that is not a player (with a
    // transformation ring; the arrow of 0x564f2f). nullptr: that 0x4f4a10 cuts (0x4ea030).
    const Models::IModelLibrary* models = nullptr;
    // GetTickCount (0x6b8144), read by the 300 ms brake of the sends (0x576791c, PassesTheActionBrake). Empty: the send
    // that checks it cuts.
    std::function<std::uint32_t()> tickCount{};
    // What 0x561c50 (CheckTheArrows) asks for to equip the inventory's arrows. nullptr: no services.
    const Items::InventoryServices* inventory = nullptr;
    // [0x5767b88], the world clock in ms.
    float worldTime = 0.0f;
    // GetAsyncKeyState(0x11), which 0x558bb0 checks to attack another player.
    bool controlDown = false;
    // 0x57ceea: with no chosen target, 0x5fc260(1) and 0x5fac90 over the mouse; the terrain point
    // (0x7d366e0..0x7d366e8) if it hits. Empty: that path cuts.
    std::function<Core::Expected<std::optional<glm::vec3>, std::string>()> pickTerrainPoint{};
    // What the skills send by send() (the inline XOR32; the C3s with their padding and serial already drawn). Empty,
    // without the session: it is not sent, like the original with the socket at −1.
    std::function<void(Protocol::Outgoing)> send{};
    // The player key the F1 00 gave ([0x57678bc]), which goes in the branches' C1 19.
    std::uint16_t playerKey = 0;
    // The current map's terrain (Terrain::TerrainState::current); without it, an empty one, and the skills stop where
    // they read it.
    const Terrain::WorldTerrain* terrain = nullptr;
    // The dynamic light (Terrain::TerrainState::dynamicLight) the skills' effects add to; nullptr: adding one stops.
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna). nullptr: readers find no model, writers stop.
    Assets::FaunaModels* faunaModels = nullptr;
};

// 0x57cfc0(hero): with the editor mode (0x77f3b18), the UI mouse (0x775857c) or 0x1000 in +0x78, nothing. Otherwise:
// the fade of +0x80; the skill of slot +0x425 (+0x6c of the own record) and its reach (0x5526f0); the right button
// (0x7d36880, 0x7d3685c, the release 0x7d36840 and the frames 0x7d368a4, with 0x28 charging as 0x3a) or the auto
// attack with 0x6c8e38 at 2 and a chosen target (0x57d1d9) turn the use on, and the clock of 0x77f3b4c/0x77f3b50 cuts
// it at 3600 s (with the movement locked, 0x77f3bac, the left click). Then: 0x6c8e50 at −1, the raven's orders
// (0x420f70), the siege skills (0x5a5540), Skill.bmd +0x31 at 2 with 0x200, +0x389, the safe zone (only in Blood
// Castle and Chaos Castle, and the listed skills), 0x6c8e38 by class, the hero's action and, except for the Wizard, the
// target (0x57ce80), the line of sight (0x558800) and each hand's skill (0x559890 or 0x561f60). The branch of classes
// 1, 3 and 4 (0x581894) is in HeroSkillsKnight.h, the elf's (0x57d790) in HeroSkillsElf.h and the Wizard's (0x5897b7,
// also the Magic Gladiator's after its own) is UseTheWizardSkill (HeroSkillsWizard.h); the tail of skills 0x43..0x48
// (0x5808da), for all classes, is UseTheSharedTail (HeroSkillsTail.h).
[[nodiscard]] Core::Expected<void, std::string> UseTheHeroSkill(Kernel::ActorState& hero,
                                                                const HeroSkillContext& context);
} // namespace Client::Skills
