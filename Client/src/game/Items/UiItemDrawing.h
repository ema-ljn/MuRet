#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/PixelSize.h"
#include "Tables/ItemTable.h"
#include "Render/Pass3D.h"
#include "Scenes/GameProjection.h"
#include "Ui/Widget.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// The UI's 3D item (06 § El inventario, "El ítem de la UI"; CL8 milestone 4): 0x5e1480 picks a point of the item's
// box, takes it in front of the UI camera (0x620480) and picks the model; 0x5e0720 brings it close to the camera,
// gives it its family's angles, scale and root height, poses it in the global buffer (0x4eb860) and draws it like a
// floor item (0x607b60 and 0x60cf70). All in 0x5ac120's 3D pass: the 1° projection of the whole screen with the
// camera at the origin.
namespace Client::Kernel
{
struct GameDrawTailState;
struct PoseBuffer;
} // namespace Client::Kernel

namespace Client::Render
{
class ModelBatch;
}

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Actors::Drawing
{
struct ItemDrawContext;
} // namespace Client::Actors::Drawing

namespace Client::Items
{

// A UI item's box in 640 × 480: the four floats 0x5e1480 and 0x5e2d00 receive.
struct UiItemBox
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// What 0x5e1480 passes to 0x5e0720: the model and the level, the item's +4, or −1 with the 0x1a13's models (0xc1c,
// 0x225 and 0xa24) and with 0x2246 and 0x2247.
struct UiItemCall
{
    std::int32_t model = 0;
    std::int32_t level = 0;
};

// 0x5e1d6c..0x5e2267: the model and the level by the type and +4 (`level`, in 32 bits: the >> 3 signed and
// unmasked, the & ~7 unsigned). Without a value, 0x5e1480 returns without calling 0x5e0720 (0x1a13 with the step
// outside 0 to 2, 0x1c17 and 0x1c18 outside 0 to 1 and 0x1a14 outside 0 to 3).
[[nodiscard]] std::optional<UiItemCall> UiItemCallOf(std::int32_t type, std::uint32_t level);

// 0x5e1486..0x5e14e5: the item is under the mouse (the integers at 0x7d3682c and 0x7d36828, in 640 × 480) if nobody
// has an item in the cursor (0x7879fbc at 0) or with `always`, and x ≤ mx < x + w and y ≤ my < y + h on the x87.
[[nodiscard]] bool UiItemHovered(const UiItemBox& box, Ui::Point mouse, std::int32_t cursorItemCount, bool always);

// 0x5e14e9..0x5e1d69: the box point 0x5e1480 passes to 0x620480: x + w · kx and y + h · ky on the x87 with the
// factors of the type's table (and the level's in some), truncated with _ftol. With a step the table does not have
// (0x1a13, 0x1a14, 0x1a0b and 0x1c15), the corner.
[[nodiscard]] Core::Expected<glm::ivec2, std::string> UiItemAnchor(std::int32_t type, std::uint32_t level,
                                                                   const UiItemBox& box);

// What 0x5e0720 builds before the pose.
struct UiItemPlacement
{
    // The drawing one (the local object's +2, ESI): the call's, or 0x223a and 0x223b for 0x1e1e with step 0 and 2.
    std::uint16_t model = 0;
    // 0x78aadb2: the posing one, 0x20a (Player) with the armors ([0x1012, 0x1a12)).
    std::uint16_t poseModel = 0;
    // The point brought close to the camera (0x5fc790) with some models' shifts.
    glm::vec3 position{};
    // 0x78aadcc..0x78aadd4 after this item.
    glm::vec3 angles{};
    // The local object's +0xc.
    float scale = 0.0f;
    // +0x84 of the posing model: the root's extra z with lock_positions.
    float rootHeight = 0.0f;
    // The level 0x60cf70 receives: the call's, or 0 with 0x225, 0xc1c and 0xa24 with a negative one.
    std::int32_t level = 0;
};

// 0x5e0720 up to the pose: `anchor` is 0x620480's output and its camera (0x7d36834); `hovered` turns the item with
// the world clock (0x5767b88, in ms). `angles` are the globals 0x78aadcc..0x78aadd4: 0x1e1e with a step other than
// 0, 1 or 2 does not write them and keeps the previous item's. With the models from 0x212 to 0xe11 it reads the byte
// +0x1e (two hands) of its Item.bmd record; without it, it stops.
[[nodiscard]] Core::Expected<UiItemPlacement, std::string>
PlaceUiItem(const UiItemCall& call, const Scenes::UnprojectedPoint& anchor, bool hovered, bool always, float worldClock,
            const Tables::ItemTable* items, glm::vec3& angles);

// 0x5ac120's 3D pass: the 1° projection, W / H, 20 and 2000 on the whole screen (0x620d10 and 0x6203c0) and the view
// at identity (0x620370 on MODELVIEW at identity), with 0x6203c0's cache. 0x633110's is the same but with the near and
// far of [0x6d5d40] (20) and [0x6d5d44] (this frame's world far, which 0x635ae0 writes): `farPlane`. 0x6203c0's
// cache does not depend on the far.
struct UiItemPass
{
    Render::Pass3D pass{};
    Scenes::GameProjection projection{};
};

[[nodiscard]] Core::Expected<UiItemPass, std::string> MakeUiItemPass(Core::PixelSize screen, float farPlane = 2000.0f);

// What the frame's owner lends the UI's 3D stretch to draw its items: the models' batch and the ends of the two
// batches (SceneCanvas), the models, the pose's global buffer (0x68b181c), 0x60b3d0's contexts and the pass. The
// first item drawn closes the 2D batch and starts the UI pass, and the end of 0x5ac120 closes the models' one: the
// items end up above the earlier 2D and below the later one. Without items, nothing.
struct UiItemResources
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& models;
    Kernel::PoseBuffer& poseCarry;
    const Actors::Drawing::ItemDrawContext& itemContext;
    Core::Duration frameClock{};
    std::function<void()> flushSprites{};
    std::function<void()> flushModels{};
    UiItemPass pass{};
    // Whether the models' batch already runs with the UI pass.
    bool open = false;
};

// 0x5e13c5..0x5e146d: poses with `placement` in the global buffer, builds the local object (0x607b60, with its
// rand()) and draws it with 0x60cf70(object, model, 0, (1, 1, 1), 1, level, excellent, set, 1, 1, 1, 0, 2): the
// global pose, with no owner and no per-vertex light. Opens the UI pass if it is not open. Without the model or the
// posing one's skeleton, it stops.
[[nodiscard]] Core::Expected<void, std::string> DrawUiItemModel(UiItemResources& resources, Crt::CrtRandom& random,
                                                                const UiItemPlacement& placement,
                                                                std::uint8_t excellent, std::uint8_t set);

// 0x5ac2a2..0x5ac2bb: the end of 0x5ac120. If an item opened the pass, the models' batch is drawn.
void CloseUiItemPass(UiItemResources& resources);

// 0x5e0720's [ebp − 0x45c]: the local object 0x5e0720 builds on the stack for each UI item and passes to 0x607b60 and
// 0x60cf70. 0x60b3d0 gives it as owner to the 0x1a18's joints (0x60c482..0x60c555): not null, so 0x530860 does not
// write +0x58, but nobody dereferences it afterwards (07 Q-C1821). Its address is the same in each call; MuRet uses a
// durable token that is not read either.
[[nodiscard]] void* UiItemLocalObject();

// Those joints are born in the UI draw, after the joints' draw (0x637516 and 0x637592), and die in the next frame's
// first step (life at 0, 0x53c3ad..0x53c3bd), before the following draw: the original never draws them. MuRet's
// intermediate draws (GameScene::PrepareDrawProjection) turn them off in their copy of the tail; the real table keeps
// them for the step, with their rand().
void HideUiItemJoints(Kernel::GameDrawTailState& tail);
} // namespace Client::Items
