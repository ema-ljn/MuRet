#pragma once

#include "Core/Expected.h"
#include "Kernel/HelperTable.h"
#include "Core/PixelSize.h"
#include "Kernel/CharacterAppearance.h"
#include "Math/HalfLifeMath.h"
#include "Render/Pass3D.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Pets/HelperTable.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindows.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
}

namespace Client::Models
{
class IModelLibrary;
struct SkinnedMesh;
} // namespace Client::Models

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The letters' portrait character (vtable 0x6ba008, 0x495e50): the object that the read window (+0xb0) and the
// writing one (+0xb4) carry, with its own character (+0x7c), its own helper (+0x4b4) and what builds it, dresses it,
// copies it from the hero and changes its action. The window's part (the place, the mouse, the angle, the zoom and the
// action at +0x6a4) belongs to Ui (Ui::OldPortraitPart). Contract: Client/docs/research/06-render-escenas-y-ui.md,
// "El retrato de las cartas". All *static*, from the ASM.
namespace Client::Messenger
{

struct LetterPortrait
{
    // +0x7c: a 0x438-byte record like the pool's, outside it. It is never the published hero.
    Kernel::ActorState actor{};
    // +0x4b4: a 0x1d4-byte slot like the helper table's (0x7d335d0), outside it. The rider (+0x108) is the character
    // above: in MuRet, `rider` at ActorPoolSize.
    Kernel::HelperState helper{};
    // +0x688: the helper's scale, times the character's (0x497810 and 0x496530's tail) and the zoom (0x495760); the
    // drawing (0x445260 with its second argument at 1) overwrites it with 0x3f3ae148 (Q-C1010).
    float helperScale = 0.0f;
    // +0x698: the drawing's idle action (0x497c10); 0x497910 sets it to 1.
    std::int32_t idle = 0;
    // +0x6a0: the last action that 0x495a20 showed.
    std::int32_t shownAction = 0;
    // +0x6a8: the integer frame that the drawing stores (0x497c10); 0x497910 sets it to 0.
    std::int32_t frameMark = 0;
    // The character's cloths (object + 0x18c/+0x190), which 0x4fb1a0 releases and the drawing builds (0x50ba30).
    Actors::Drawing::CharacterCloth cloth{};
};

// 0x4961d0 (the vtable + 0x30): the helper zeroed; the character's cell entries (+0x41c and +0x420) open while it
// clears the character and closed, X and then Y (4 rand() each if they stay at 0); the published hero's logical cell,
// Y and then X (another 4 and 4); 0x511290(character, 0x20a, X, Y, 0), with its 28 rand(); the place at (−300, −300,
// −300); and +0x68c at 1 (0x496415: in MuRet, OldPortraitPart::built, which the hook sets). From zero, 44 rand().
[[nodiscard]] Core::Expected<void, std::string> BuildThePortrait(LetterPortrait& portrait,
                                                                 const Scenes::WorldEntryResources& resources);

// 0x4977d0 (the vtable + 0x34), built: the class (+0x1d4) and 0x513100.
[[nodiscard]] Core::Expected<void, std::string> SetThePortraitClass(LetterPortrait& portrait,
                                                                    std::uint8_t characterClass,
                                                                    const Scenes::WorldEntryResources& resources);

// 0x497810 (the vtable + 0x38), built: 0x5139f0(0, equipment, character, helper) (ApplyCharacterSetTo), +0x688 with
// the helper's scale times 0.7f (0x6b8a5c) divided by the published hero's ([0x743a170] + 0xc), and the safe zone
// (+0x3e2) at 1 with wings and the action `action` (+0x6a4) past 0x82, at 0 otherwise.
[[nodiscard]] Core::Expected<void, std::string> DressThePortrait(LetterPortrait& portrait, Kernel::Equipment equipment,
                                                                 std::int32_t action,
                                                                 const Scenes::WorldEntryResources& resources);

// 0x496530 (the vtable + 0x3c), built: the published hero's class (with 0x513100 if it changes) and, by groups (the
// head with the body, the hands, the wings and the pet), what differs: from the hero as is with +0x3e3 at 0, or from
// the own record with +0x3e3 other than 0 (with its access and the 3848 rand() of its close when comparing and in each
// group it copies). With the pet different, 0x495a20(0x7a, 0) and `action` (+0x6a4) at 0x7a; otherwise, 0x495a20 with
// the one it showed. With the pet or the hands different, the helper again. Returns whether something differed.
[[nodiscard]] Core::Expected<bool, std::string> CopyTheHeroToThePortrait(LetterPortrait& portrait, std::int32_t& action,
                                                                         const Scenes::WorldEntryResources& resources);

// 0x495a20(action, offset): the list's action (the ridden helper's if there is one, the gestures' otherwise) that is
// `delta` from `action`, set on the character (0x4f2250, 0x4f4a10 or 0x4f37c0 with the world at 0, or 0x4ea030 with
// the gestures table), and 0x4fd5f0 and 0x505de0 with the current world. Returns the action and leaves it at +0x6a0.
[[nodiscard]] Core::Expected<std::int32_t, std::string>
ShowThePortraitAction(LetterPortrait& portrait, std::int32_t action, std::int32_t delta,
                      const Scenes::WorldEntryResources& resources);

// 0x497890's character part (the vtable + 0x40), built: the angles (+0x1c..+0x24) at (−20, 5, 60).
void TurnThePortrait(LetterPortrait& portrait);

// 0x495760's portrait view (0x4957a4..0x4958c6) on a `screen`-pixel screen: 0x620d10's viewport with `base`'s place
// and width and 141 (0x6ba004) high, each times W / 640 or H / 480 in float (0x5762ea8 and 0x5762eac) and truncated;
// gluPerspective(1, the width over the height untruncated, 2000, 20000) (0x6203c0, without 0x620db0's × 1.4); and
// glRotatef(−90) on X, glRotatef(−90) on Z, glTranslatef(−10000, 0, −75) and glTranslatef(−X, −Y, −Z), with 50 less on
// Z with the dark horse (0x1c16) at +0x318.
[[nodiscard]] Render::Pass3D PortraitPass(Core::PixelSize screen, const Ui::OldWindowBase& base,
                                          const Kernel::ActorState& actor);

// 0x4957a4..0x4959bf, 0x495760's part after the step: PortraitPass's pass, the angles at (0, 0, +0x6b0), the scale by
// the zoom (+0x6bc · 0.7 and the helper's +0x688 · +0x6bc, which 0x445260 overwrites: Q-C1010), the lights at 1,
// +0x3e4 at 1 and the safe zone (+0x3e2) with wings and +0x6a4 past 0x82. No rand().
[[nodiscard]] Render::Pass3D PlaceThePortrait(LetterPortrait& portrait, const Ui::OldPortraitPart& part,
                                              Core::PixelSize screen);

// 0x497c10 (the vtable + 0x14) without the 2D part, with `part` not blank (+0x6c0): the frame (+0x114) below +0x6a8
// chooses the next action (idle, 1 in 4 with rand() % 4 the one at +0x6a4; otherwise, 0x7a if +0x6a4 is not between
// 0x7a and 0x82) and otherwise +0x6a8 follows the frame; with +0x360 below 4, 0x414340; and 0x495760 up to its
// drawing: 0x4fd5f0, 0x505de0 and 0x444010(helper, 1) (StepTheHelperOutsideTheTable) with the world at 0, and
// PlaceThePortrait. Returns its pass; the drawing of the helper (0x445260) and the character (0x50ba30) in it is
// DrawThePortraitModel.
[[nodiscard]] Core::Expected<Render::Pass3D, std::string>
PrepareThePortraitDraw(LetterPortrait& portrait, Ui::OldPortraitPart& part, Core::PixelSize screen,
                       const Scenes::WorldEntryResources& resources);

// What the portrait's drawing uses from the game's drawing (0x637673's 2D, after the HUD).
struct LetterPortraitDraw
{
    // The 2D batch, the texts, Text.bmd and the tooltip box (0x5af090); the portrait does not use its rand().
    Ui::GameHudContext& hud;
    // The model batch and the two batches' ends (SceneCanvas): the portrait's pass closes the earlier 2D and stays
    // under the later one.
    Render::ModelBatch& batch;
    std::function<void()> flushSprites{};
    std::function<void()> flushModels{};
    // What 0x50ba30 reads (DrawCharacterOutsideThePool), with the frame's rand() in `clothWorld`.
    Actors::Drawing::GameCharacterPass characters{};
    // The helpers' models (the world's, like DrawHelpers) and what 0x445260 reads: the pose to the global buffer
    // (0x68b181c), its posed meshes, timeGetTime() and the pose's global buffer.
    const Models::IModelLibrary* helperModels = nullptr;
    std::function<Core::Expected<void, std::string>(const Actors::Drawing::DrawnObject&)> onHelperPose{};
    std::function<std::span<const Models::SkinnedMesh>()> posedMeshes{};
    std::function<std::uint32_t()> systemTime{};
    std::function<std::span<const Math::BoneTransform>()> globalPose{};
    // An original frame: the step and the drawing over the portrait. An intermediate MuRet draw draws a copy, without
    // the step (the next original frame does it), and with `characters`' rand(), which is its copy.
    bool originalFrame = true;
};

// 0x4958e2..0x495a0c over PrepareThePortraitDraw's pass: closes the 2D, starts the pass with 0x620db0's material and
// the depth cleared (glDisable(GL_ALPHA_TEST), glEnable(GL_TEXTURE_2D), 0x620850, 0x6208d0, 0x620890, GL_LEQUAL,
// glAlphaFunc(GL_GREATER, 0.25), no fog and glClear(GL_DEPTH_BUFFER_BIT)), 0x445260(helper, 1)
// (DrawTheHelperOutsideTheTable) and 0x50ba30(character, character, 0) (DrawCharacterOutsideThePool), and closes the
// pass even if it cuts. The 2D batch sets the matrices and the screen viewport back (0x4959dd..0x495a0c).
[[nodiscard]] Core::Expected<void, std::string> DrawThePortraitModel(LetterPortrait& portrait,
                                                                     const Render::Pass3D& pass,
                                                                     const LetterPortraitDraw& draw,
                                                                     const Scenes::WorldEntryResources& resources);

// The whole 0x497c10 (the vtable + 0x14, from 0x49a1d0 and 0x498800): blank (+0x6c0 at 1), the 119 × 141 black square
// and Local\Webzenlogo.jpg (0x653) of 80 × 62 at (x + 0x14; y + 0x26) with v at 0.76171875; otherwise,
// PrepareThePortraitDraw and DrawThePortraitModel, and with +0x4c & 1 the help icon (0x651): 16 × 16 at (x + 1;
// y + height − 0x11), or with the help on view (+0x690) gray (0.6) 15 × 15 at (x + 2; y + height − 0x10) with u and v
// at 0.9375, and texts 997, 998 and 999 of Text.bmd in 0x5af090's box (mode 1, framed), centered at x + width / 2 and
// raised (cy("Z") / ry + 2) · 3 from the bottom. Without `part`'s character, it cuts.
[[nodiscard]] Core::Expected<void, std::string> DrawTheLetterPortrait(Ui::OldPortraitPart& part,
                                                                      const LetterPortraitDraw& draw,
                                                                      const Scenes::WorldEntryResources& resources);

// The hook of Ui::OldWindowStepView::portrait with the game of `resources`, which outlives it: the first request
// builds the LetterPortrait of Ui::OldPortraitPart::actor. Build (0x4961d0) sets +0x68c to 1; SetClass, SetEquipment,
// CopyTheHero and SetAngle do nothing unbuilt (0x4977d9, 0x49781e, 0x496548, 0x49789b); SetAction (0x4978f0) and
// TurnAction (0x497910: the action that 0x495a20 gives at +0x6a4, +0x6a8 at 0 and +0x698 at 1) do not check it; Step
// copies the hero's +0x360 (0x4979c0).
[[nodiscard]] Ui::OldPortraitHook LetterPortraitHook(const Scenes::WorldEntryResources& resources);
} // namespace Client::Messenger
