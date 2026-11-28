#include "Effects/CastleWeaponBodies.h"

#include "Actors/Drawing/CharacterDrawing.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Models/IModelLibrary.h"
#include "Models/ModelId.h"
#include "Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
// The stones of the siege (0x51cfa4 and 0x51d1f0) and the fragments 0x4ee770 sheds from each on impact.
constexpr std::int16_t SiegeStone = 0x12b, SiegeIceStone = 0x12c;
constexpr std::int16_t StoneFragments = 0x12d, IceFragments = 0x12e;
// +0x58 at 99 on a stone already drawn: its impact.
constexpr std::int32_t ImpactMesh = 99;
// The two with an additive pass on white; 0xee is Crywolf's shot (0x51c06c).
constexpr std::int16_t FirstAdditiveBody = 0xed, CrywolfShotBody = 0xee;
} // namespace

Core::Expected<void, std::string> DrawCastleWeaponBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                       World::ModelEffect& effect,
                                                       const Actors::Drawing::DrawnObject& input,
                                                       Core::Duration frameClock, const VertexEffectEmitter& emit)
{
    // 0x602e90 → 0x5ffdf0: C0 also drops a NaN alpha before the pose and before changing the object.
    if (!(effect.alpha >= .01f) || input.state.hiddenMesh == -2)
        return {};
    const bool stone = effect.type == SiegeStone;
    const bool projectile = stone || effect.type == SiegeIceStone;
    const bool impact = projectile && effect.objectState.hiddenMesh == ImpactMesh && effect.seen;
    const auto* model = models.Find(Models::ModelId{input.model});
    if (model == nullptr || model->meshes.empty())
    {
        if (impact)
            effect.objectState.hiddenMesh = -2;
        return {};
    }
    std::vector<Models::SkinnedMesh> temporary;
    const auto meshes = Actors::Drawing::PosedMeshesOf(models, input, temporary);
    Actors::Drawing::DrawnObject object = input;
    const auto draw = [&](std::uint32_t flags, std::optional<Render::TextureSlot> texture = std::nullopt)
    {
        return Actors::Drawing::DrawItemMeshPass(batch, models, object, frameClock, meshes, -1, flags, texture,
                                                 &object.color);
    };
    if (impact)
    {
        effect.objectState.hiddenMesh = -2;
        object.state.hiddenMesh = -2;
        if (!emit)
            return Core::Unexpected(std::string("0x601c90: no 0x4ee770 for the impact of the weapon"));
        if (auto emitted = emit(stone ? StoneFragments : IceFragments, object, meshes); !emitted.HasValue())
            return emitted;
    }
    else if (effect.type == World::FirstDebrisEffect || effect.type == World::DebrisEffect)
    {
        if (effect.subtype == 2 || effect.subtype == 3)
        {
            object.color = effect.subtype == 2 ? glm::vec3{.1f, .3f, .6f} : glm::vec3{.1f, .6f, .3f};
            if (auto chrome = draw(0x44, Actors::Drawing::ChromeTextureSlot); !chrome.HasValue())
                return chrome;
        }
        else
        {
            if (auto base = draw(2); !base.HasValue())
                return base;
            object.color = glm::vec3(1);
            if (auto chrome = draw(0x44, Actors::Drawing::ChromeTextureSlot); !chrome.HasValue())
                return chrome;
            object.color = {.3f, .3f, 1};
            if (auto shiny = draw(0x48, Actors::Drawing::ChromeTextureSlot); !shiny.HasValue())
                return shiny;
        }
    }
    else
    {
        const bool fragment = effect.type == StoneFragments || effect.type == IceFragments;
        const bool fragmentChrome = fragment && effect.subtype == 1;
        const bool fragmentGreen = fragment && (effect.subtype == 2 || effect.subtype == 3);
        if (stone || fragmentChrome)
            object.color = glm::vec3(1);
        else if (fragmentGreen)
            object.color = {.5f, 1, .3f};
        if (auto base = draw(2); !base.HasValue())
            return base;
        if ((stone && effect.subtype <= 1) || fragmentChrome)
        {
            object.color = {1, .2f, .1f};
            object.state.light = 1;
            if (auto chrome = draw(0x44); !chrome.HasValue())
                return chrome;
        }
        if (fragmentChrome || fragmentGreen)
            object.color = glm::vec3(1);
    }
    // 0x602365: uses the last color, even the red of 0x12b; 0x12d/0x12e restore white explicitly.
    if ((effect.flags78 & 2) != 0)
    {
        object.state.blendMesh = -2;
        object.state.light = 1;
        if (auto tail = draw(2); !tail.HasValue())
            return tail;
    }
    if (effect.type == FirstAdditiveBody || effect.type == CrywolfShotBody)
    {
        object.color = glm::vec3(1);
        object.state.blendMesh = -2;
        object.state.light = 1;
        if (auto additive = draw(0x42); !additive.HasValue())
            return additive;
    }
    return {};
}
} // namespace Client::Scenes
