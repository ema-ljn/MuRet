#pragma once

#include "Kernel/GameContext.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Scenes/HelperTable.h"
#include "Player/ItemSetState.h"
#include "Actors/PlayerActionSpeeds.h"
#include "Scenes/FaunaState.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "World/Leaves.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <memory>

namespace Client::Crt
{
class CrtRandom;
}

// What WinMain zeroes and lasts from one scene to another: the pool of 401 character records (REP STOSD at
// 0x4bafa6..0x4bafb3, with the hero in the first, 0x4bafdf), the own record ([0x7681eb0], 0x4bafbc..0x4bafc2), the own
// inventory's grids (0x78a9bb0 and 0x78ab0b8, in the .bss), the 10 helpers of 0x7d335d0 (also in the .bss:
// HelperTable.h) and the plain globals (WorldRuntime). Also what 0x550d10 leaves in the sets' object ([0x6eed3c], the
// static of 0x6e5930), which starts at zero and nobody clears again. main is its only owner and lends it: it is
// neither copied nor moved, so there is no other copy of the history.
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los personajes de la lista".
namespace Client::Kernel
{
// 0x4c20a0 puts each list character in chars[place], with the place from 0 to 4.
inline constexpr std::size_t SelectionActorCount = 5;

// +0x68 and +0x6c..+0x74 of a shared model (0x5769ad8 + type · 0xe0): the scale and the position of the last object
// of that type that copied them. 0x4ec110 with Translate at 1 adds them to the bones it is passed, even if they belong
// to another object of the same type.
struct SharedModelPlacement
{
    float scale = 0.0f;
    glm::vec3 origin{};
    // BYTE +0xa0: published before the body; the actor's action can change afterwards.
    std::uint8_t action = 0;
};

// 0x6b88d4 (0.01f): with less alpha, 0x6001b0 leaves without copying anything to the model.
inline constexpr float PlayerModelAlpha = 0.01f;

class WorldState
{
public:
    WorldState() = default;
    ~WorldState() = default;
    WorldState(const WorldState&) = delete;
    WorldState& operator=(const WorldState&) = delete;
    WorldState(WorldState&&) = delete;
    WorldState& operator=(WorldState&&) = delete;

    // The shared core, which the accessors below forward to.
    [[nodiscard]] Kernel::GameContext& Context();
    [[nodiscard]] const Kernel::GameContext& Context() const;
    [[nodiscard]] std::span<ActorState, ActorPoolSize> Actors();
    [[nodiscard]] std::span<const ActorState, ActorPoolSize> Actors() const;
    // The hero's actor, as GameContext::Hero reads it.
    [[nodiscard]] ActorState& Hero();
    [[nodiscard]] const ActorState& Hero() const;
    [[nodiscard]] std::span<Actors::Drawing::CharacterCloth, ActorPoolSize> ActorCloths();
    [[nodiscard]] std::span<const Actors::Drawing::CharacterCloth, ActorPoolSize> ActorCloths() const;
    [[nodiscard]] OwnCharacterRecord& OwnCharacter();
    [[nodiscard]] const OwnCharacterRecord& OwnCharacter() const;
    // What the F3 10 fills outside the equipment (06, "El F3 10, el inventario").
    [[nodiscard]] OwnInventory& Inventory();
    [[nodiscard]] const OwnInventory& Inventory() const;
    [[nodiscard]] WorldRuntime& Runtime();
    [[nodiscard]] const WorldRuntime& Runtime() const;
    // What the last map load (0x61a560) left on the CPU.
    [[nodiscard]] WorldMap& Map();
    [[nodiscard]] const WorldMap& Map() const;
    [[nodiscard]] Player::ItemSetState& ItemSets();
    [[nodiscard]] const Player::ItemSetState& ItemSets() const;
    // The game's helpers (0x7d335d0): each rider's dark horse.
    [[nodiscard]] Scenes::HelperTable& Helpers();
    [[nodiscard]] const Scenes::HelperTable& Helpers() const;
    // 0x743a198 and its wind: one pool for the login, the selection and the game, on the heap because of its size.
    [[nodiscard]] World::Particles& Particles();
    [[nodiscard]] const World::Particles& Particles() const;
    [[nodiscard]] Actors::PlayerActionSpeeds& ActionSpeeds();
    [[nodiscard]] const Actors::PlayerActionSpeeds& ActionSpeeds() const;
    [[nodiscard]] Scenes::FaunaState& Fauna();
    [[nodiscard]] const Scenes::FaunaState& Fauna() const;
    [[nodiscard]] Scenes::FaunaDrawCarry& PoseCarry();
    [[nodiscard]] const Scenes::FaunaDrawCarry& PoseCarry() const;
    [[nodiscard]] Scenes::GameDrawTailState& DrawTail();
    [[nodiscard]] const Scenes::GameDrawTailState& DrawTail() const;
    [[nodiscard]] World::Leaves& Leaves();
    [[nodiscard]] const World::Leaves& Leaves() const;
    // Player.bmd (0x20a), shared by all players: each one's step (0x505ec6) and its body's pose (0x6001b0, with alpha
    // from 0.01) copy it, in the game, the login and the selection. The raven reads it in the safe zone (0x4178d2 and
    // 0x4179a5), before its owner's 0x505ec6 (Q-C412). It starts at zero: before the first copy, MuRet adds nothing.
    [[nodiscard]] SharedModelPlacement& PlayerModel();
    [[nodiscard]] const SharedModelPlacement& PlayerModel() const;
    // What 0x505ec6 and 0x6001b0 copy from `actor` if it is a 0x20a; with another type, to another model.
    void PlacePlayerModel(const ActorState& actor);
    // The same with origin `origin`: 0x6001b0's in 0x50ba30 is the position before Blood Castle's shadow floor
    // (0x600247..0x600256 runs at 0x50bbf7, before 0x50bcaf).
    void PlacePlayerModel(const ActorState& actor, const glm::vec3& origin);
    // What 0x60cf70 copies from `actor` to the 0x20b (Player\Shadow01) when 0x50ba30 draws its shadow (0x50bcec): the
    // scale (+0x68, 0x60cff1) and the origin (+0x6c, 0x60d009); the action (+0xa0) stays as it was. Joint 0x632/0 and
    // /1 read it on reaching the ground (0x53c1eb).
    void PlaceShadowModel(const ActorState& actor);
    // 0x505ec6 and 0x4ec110: copies the scale and the origin to its type's shared model (the 0x20a, to PlayerModel).
    void PlaceModel(const ActorState& actor);
    void PlaceModel(std::uint16_t type, const SharedModelPlacement& placement);
    // The scale and the position model `type` holds (0x5769ad8 + type · 0xe0, +0x68 and +0x6c). Zero until the first
    // copy, like Player.bmd's. 0x4ec110 publishes it when it draws the body in a frame of the original (P-C241 g).
    [[nodiscard]] SharedModelPlacement ModelPlacement(std::uint16_t type) const;

    // What the selection does when it goes to scene 3 with `chosen` (06, "La lógica" and "La entrada al mundo", step
    // 1; the three paths: Enter 0x6343e2..0x634470, double click 0x634546..0x634638 and 0x633c10's button, up to
    // 0x633f05): it opens the own record, copies the level, the class, the skin and the name into it, closes it with
    // its 3,848 rand() calls of `random` if no access is left (0x40f370 or 0x40b4d0) and clears loadingOnceFlag. The
    // scene frees its own in its Leave (0x61cc50).
    void PrepareSelectedActor(const Protocol::FrontEnd::CharacterEntry& chosen, Crt::CrtRandom& random);

private:
    Kernel::GameContext m_context;
    // On the heap for the same reason as the actor pool (GameContext.h).
    std::unique_ptr<std::array<Actors::Drawing::CharacterCloth, ActorPoolSize>> m_actorCloths =
        std::make_unique<std::array<Actors::Drawing::CharacterCloth, ActorPoolSize>>();
    Player::ItemSetState m_itemSets;
    Scenes::HelperTable m_helpers{};
    std::unique_ptr<World::Particles> m_particles = std::make_unique<World::Particles>();
    Actors::PlayerActionSpeeds m_actionSpeeds;
    Scenes::FaunaState m_fauna;
    Scenes::FaunaDrawCarry m_poseCarry;
    std::unique_ptr<Scenes::GameDrawTailState> m_drawTail = std::make_unique<Scenes::GameDrawTailState>();
    std::unique_ptr<World::Leaves> m_leaves = std::make_unique<World::Leaves>();
    SharedModelPlacement m_playerModel;
    std::map<std::uint16_t, SharedModelPlacement> m_modelPlacements;
};
} // namespace Client::Kernel
