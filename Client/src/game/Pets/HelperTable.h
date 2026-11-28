#pragma once

#include "Actors/HelperHooks.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Math/HalfLifeMath.h"
#include "Kernel/HelperTable.h"
#include "Kernel/SceneId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
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
struct SkinnedMesh;
} // namespace Client::Models

namespace Client::Terrain
{
struct TerrainLighting;
struct TerrainPolygon;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

// The game's helper table: the 10 slots of 0x1d4 bytes at 0x7d335d0 (.bss); 0x443fa0 → 0x443d70 creates, 0x443d40
// turns off, 0x445230 → 0x444010 steps and 0x4453b0 → 0x445260 draws. The original has one for scenes 2, 4 and 5; MuRet
// uses it in 4 and 5 and keeps the login's horse in LoginScene. Ports the Dark Horse (0xbf), the Uniria (0xc6), the
// Dinorant (0x110), the Fenrir (0x14a..0x14c) and the Guardian Angel (0x1c12). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "El caballo oscuro" and "Las mascotas".
namespace Client::Pets
{

// 0x443fa0(type, position, rider, 0, 0), as 0x5bc740 and the game's other setups call it: on maps 18 to 23, or with a
// rider that is not a player (0x20a) and a type other than 0x1c12, nothing. Otherwise 0x443d70 on the first slot
// without the live byte; the action and the previous one stay from the slot's last use (Q-C221). The 0x1c12 draws
// three position rand() and needs `random`. Returns the slot, or nullopt if none was created; other types cut before
// 0x443d70.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateHelper(Kernel::WorldState& world, std::uint16_t type, const glm::vec3& position, std::size_t rider,
             Crt::CrtRandom* random = nullptr);

// 0x443d70(type, position, rider, slot, 0, 0) on one slot: on maps 18 to 23 (`map`) or with the slot live, nothing
// (false). Otherwise it builds it as CreateHelper says and returns true. `riderSlot` is the rider's pool slot, or
// ActorPoolSize for an object outside the pool (the card portrait). Unported types cut before touching it.
[[nodiscard]] Core::Expected<bool, std::string> ConstructHelper(Kernel::HelperState& helper, std::uint16_t type,
                                                                const glm::vec3& position,
                                                                const Kernel::ActorState& rider, std::size_t riderSlot,
                                                                std::int32_t map, Crt::CrtRandom* random);

// From the world the step also uses the particles (0x743a198), the effects (0x7491fd8) and the previous draw's global
// pose buffer (0x68b181c, WorldState::PoseCarry).
struct HelperStepContext
{
    Kernel::WorldState& world;
    // 0x5769ad8 + type · 0xe0, for the actions 0x4ea030 and 0x4ec200 read: the `Skill\` models 0x615700 loads and
    // Player\Helper01.bmd (the 0x1c12) from 0x60ff50. nullptr, or without the type's: cuts with a live one.
    const Models::IModelLibrary* models = nullptr;
    Crt::CrtRandom& random;
    // 0x5767b88, in ms; 0x53fe70 turns the 5f2 with it when creating them.
    float worldClock = 0.0f;
    // DAT_006d5dc8: only 5 turns off the riderless helper (0x44402a), and 4 decides the animation's end with frame + 2
    // (0x4ec200, Q-C21).
    Kernel::SceneId scene = Kernel::SceneId::Game;
    // The attributes (0x7d0d348) of scene 4's load; without it, an empty terrain's.
    const Terrain::WorldTerrain* terrain = nullptr;
    // 0x7d3a0f0, the logic step count 0x4453e0 reads raw for the horse's action 3 wave 0x6eb/0 (P-C22 a: in the
    // original it is the encrypted count, Q-C125).
    std::int32_t logicSteps = 0;
    // 0x7d325c8, written by 0x4453e0 (0x445577). nullptr: cuts if the branch asks for it.
    float* cameraShake = nullptr;
    // 0x444010's second argument, not 0 from the card portrait (StepTheHelperOutsideTheTable): the rider's safe cell is
    // still read, but does not hide the helper (0x4440eb, 0x4447f9 and 0x444b25).
    bool ignoresTheSafeCell = false;
};

// 0x445230: 0x444010(slot, 0) on the 10, in scene 5 (0x6370f4) and 4 (0x6342f4). In 5, a dead rider or one with +0x88
// not 1 turns the slot off (0x44402a..0x444047, Q-C225); otherwise the alpha rises (0x4e9980), then the type's body and
// its action's advance (0x445006..0x445039). The bodies: the 0xbf (safe cell 0x4440b8..0x4440f8, Q-C222; dust
// 0x44418e..0x4444ef; breath 0x44453f..0x4445de; in action 3 the 0x52e470 ribbon and the 0x4453e0 earthquake; the
// twelve 60f/0 at 0x44469d..0x44474c), the Uniria and the Dinorant (0x4447c4..0x444ac4), the Fenrir
// (0x444af0..0x444f25) and the Guardian Angel (0x444f2a..0x444fff and 0x445052..0x445221). It cuts after the rand()
// the original draws first; what it did before the cut stays.
[[nodiscard]] Core::Expected<void, std::string> StepHelpers(const HelperStepContext& context);

// 0x444010(helper, 1) on a helper outside the table with its rider outside the pool: the card portrait's (0x495796,
// which always calls it). Without the live byte, nothing (0x444021); otherwise StepHelpers for one slot, with `rider`
// as rider and without the safe cell hiding it.
[[nodiscard]] Core::Expected<void, std::string>
StepTheHelperOutsideTheTable(Kernel::HelperState& helper, Kernel::ActorState& rider, const HelperStepContext& context);

struct HelperDrawContext
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& models;
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    Core::Duration frameClock{};
    // The fraction from the last step to the next, for the animation (D-C4).
    float stepAlpha = 0.0f;
    // D-C4: in-between draws project without publishing seen/frame or consuming the durable CRT.
    bool originalFrame = true;
    std::function<Core::Expected<void, std::string>(const Actors::Drawing::DrawnObject&)> onPose{};
    bool publishProjectedPose = false;
    std::function<std::span<const Models::SkinnedMesh>()> posedMeshes{};
    // DAT_006d5dc8, for the scale of 0x4452db..0x445309: 1.6 in 2 and 1 in 4; in 5, the slot's for the Fenrir and 1 for
    // the others.
    Kernel::SceneId scene = Kernel::SceneId::Game;
    // 0x5ffce0: the color of the helpers other than the horse.
    const Terrain::TerrainLighting* lighting = nullptr;
    // 0x5491e0: the 0x1c12's glow (0x445390) and the Fenrir's.
    Particles::FrameSprites* sprites = nullptr;
    // What the Fenrir draw (0x60277b) also reads and creates: the world clock (0x5767b88), the model effects
    // (0x518cf0), the particles (0x53fe70), timeGetTime() (0x6b8544, for the 0x14e decal) and the global pose buffer
    // (0x68b181c) after posing, the frame's or its projected copy.
    float worldClock = 0.0f;
    Effects::ModelEffects* effects = nullptr;
    Particles::Particles* particles = nullptr;
    std::function<std::uint32_t()> systemTime{};
    std::function<std::span<const Math::BoneTransform>()> globalPose{};
    // The global trapezoid (Camera::CameraState::trapezoid) that 0x5fba90 reads. nullptr: the .bss's zeros.
    const Terrain::TerrainPolygon* trapezoid = nullptr;
};

// 0x4453b0 in scene 5 (0x637508) and 4 (0x63486a): 0x445260(slot, 0) on the 10, through 0x602e90 → 0x5ffdf0 (alpha
// under 0.01 is not drawn; frame 0 with the rider's +0x78 & 0x1000, 0x5fff0d, Q-C224; on maps 18 to 23 the stored
// alpha drops 0.15f per original frame) and 0x6002e0 (Dinorant 0x600b35, Uniria and Guardian Angel 0x602360, the
// Fenrir 0x60277b). The rand() % 30 + 70 of 0x445336 is drawn even when nothing is drawn (Q-C223). With the rider's
// +0x78 & 0x4000, 0x445310 passes 10 to 0x602e90 and 0x6002e0 takes the 0x60033c path (0x600343..0x600399): another
// player's helper in the siege (0x4294b0) with +0x370 not 1 is not drawn; the rest is DrawChromeShell, without the
// Fenrir's rand().
[[nodiscard]] Core::Expected<void, std::string> DrawHelpers(const HelperDrawContext& context);

// 0x445260(helper, 1) on a helper outside the table with its rider outside the pool: the card portrait's (0x4959cc).
// Without the live byte, nothing (0x44526b); otherwise DrawHelpers for one slot, with `rider` as rider, seen without
// the trapezoid (0x4452a4) and with scale 0x3f3ae148 (0x4452d2, Q-C1010). Unlike DrawHelpers it makes no copy: the
// caller draws a copy between frames.
[[nodiscard]] Core::Expected<void, std::string> DrawTheHelperOutsideTheTable(Kernel::HelperState& helper,
                                                                             const Kernel::ActorState& rider,
                                                                             const HelperDrawContext& context);

// The helpers' table of `world` as a character's pets reach it: 0x443d40 and 0x443fa0 with `random` (CreateHelper).
class TableHelpers final : public Actors::IHelperHooks
{
public:
    TableHelpers(Kernel::WorldState& world, Crt::CrtRandom* random);

    void TurnOffTheHelpersOf(std::size_t rider) const override;
    [[nodiscard]] Core::Expected<void, std::string> CreateTheHelper(std::uint16_t type, const glm::vec3& position,
                                                                    std::size_t rider) const override;

private:
    Kernel::WorldState& m_world;
    Crt::CrtRandom* m_random;
};

// The one helper of `rider`, a character outside the pool (0x5139f0 with its fourth argument, the letters' portrait):
// its live byte at 0 instead of 0x443d40 (0x513b39), and 0x443d70 on it instead of 0x443fa0 (ConstructHelper), on the
// map of `world`.
class OwnHelper final : public Actors::IHelperHooks
{
public:
    OwnHelper(Kernel::HelperState& helper, const Kernel::ActorState& rider, const Kernel::WorldState& world,
              Crt::CrtRandom* random);

    void TurnOffTheHelpersOf(std::size_t rider) const override;
    [[nodiscard]] Core::Expected<void, std::string> CreateTheHelper(std::uint16_t type, const glm::vec3& position,
                                                                    std::size_t rider) const override;

private:
    Kernel::HelperState& m_helper;
    const Kernel::ActorState& m_rider;
    const Kernel::WorldState& m_world;
    Crt::CrtRandom* m_random;
};
} // namespace Client::Pets
