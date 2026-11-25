#include "Scenes/ItemDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/PlayerModels.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRetainedMath.h"
#include "Models/Skinning.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameItemSpecialDrawing.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr float DrawnAlpha = std::bit_cast<float>(0x3c23d70au); // 0.01f (0x6b88d4)
constexpr std::uint16_t KalimaGate = 0xc5;
constexpr std::uint32_t RiderFlag = 0x4000;
constexpr std::size_t HelmPart = 1;
constexpr std::uint8_t MaskedClass = 4;
// The records of 0x5769ad8 that 0x60dbe0 zeroes (0x60dc75).
constexpr std::int32_t ZeroedModelRecords = 0x449d;

Core::Unexpected<std::string> Cut(const char* address, const char* what, std::uint16_t type)
{
    return Core::Unexpected(std::format("{}: {} (type {:#x}) is not ported", address, what, type));
}

// 0x60cf9f..0x60cfc1
std::uint16_t LeveledModelOf(std::uint16_t model, std::uint8_t level)
{
    constexpr std::uint16_t LeveledModel = 0x1e1e;
    if (model == LeveledModel && level == 0)
        return 0x223a;
    if (model == LeveledModel && level == 2)
        return 0x223b;
    return model;
}

bool PutsAClothOnThePart(std::uint16_t model)
{
    return model == 0x1424 || model == 0x1425 || model == 0x1428;
}

// 0x50d44b..0x50d46d: sin([0x5767b88] · 0.001f) · 0.1f + 0.7f in the x87, stored as a float.
Core::Expected<float, std::string> MaskedClassLight(const ItemDrawContext& context)
{
    if (context.special == nullptr || !context.special->worldClock)
        return Core::Unexpected(std::string("0x50d44b: the class 4 helm needs the world clock (0x5767b88)"));
    const auto sine = Math::Detail::SineProductPC53(
        static_cast<double>(*context.special->worldClock) * static_cast<double>(0.001f), 0.1f);
    if (!sine)
        return Core::Unexpected(
            std::format("0x50d45f: FSIN outside its domain with the world clock {}", *context.special->worldClock));
    return static_cast<float>(*sine + static_cast<double>(0.7f));
}
} // namespace

Core::Expected<void, std::string> DrawItemPart(Render::ModelBatch& batch, const Render::Models::IModelLibrary& models,
                                               const ItemPartOwner& owner, const ItemPart& part,
                                               Core::Duration frameClock, const ItemDrawContext& context,
                                               const std::function<void(const glm::vec3&)>& onPartBox)
{
    if (!(owner.alpha > DrawnAlpha))
        return {};
    const auto level = static_cast<std::uint8_t>(part.level & 15);
    const std::uint16_t model = LeveledModelOf(part.model, level);
    if (models.Find(model) == nullptr)
    {
        if (onPartBox)
            onPartBox(owner.transform.position);
        return {};
    }
    DrawnObject object;
    object.model = model;
    object.transform = owner.transform;
    object.state = Render::Models::ObjectState{
        .blendMesh = owner.blendMesh,
        .light = owner.blendMeshLight != nullptr ? *owner.blendMeshLight : 1.0f,
        .uv = owner.uv,
        .hiddenMesh = owner.hiddenMesh,
    };
    object.color = owner.light;
    object.alpha = owner.alpha;
    object.vertexLight = owner.vertexLight;
    object.skin = part.skin;
    if (!owner.outline.empty() && (owner.stateFlags & RiderFlag) == 0)
    {
        if (owner.type == KalimaGate && owner.hiddenMesh != Render::Models::NoHiddenMesh)
            return Cut("0x60ce4e", "the +0x58 texture of the outline of a part of the 0xc5", owner.type);
        if (owner.type == KalimaGate)
            DrawObjectOutlineOfTheFirstMesh(batch, models, object, frameClock, owner.bones, owner.outline);
        else
            DrawObjectOutline(batch, models, object, frameClock, owner.bones, owner.outline);
    }
    object.state.hiddenMesh = Render::Models::NoHiddenMesh;
    const std::vector<Render::Models::SkinnedMesh> meshes =
        SkinObjectMeshes(models, object, owner.bones, owner.boneScale);
    if (onPartBox)
        onPartBox(owner.transform.position);
    if (PutsAClothOnThePart(model))
        return Cut("0x60d198", "the cloth of these pants (0x455e30)", model);
    ItemDrawContext resources = context;
    ItemSpecialDrawContext special;
    if (context.special != nullptr)
    {
        special = *context.special;
        special.objectLight = owner.storedLight;
        special.publishedObject = &object;
        resources.special = &special;
    }
    resources.poseBones = [bones = owner.bones]() { return bones; };
    auto drawn =
        DrawItemFromMeshes(batch, models, object, frameClock, meshes, level, part.excellent, part.ancient, resources);
    if (owner.blendMeshLight != nullptr)
        *owner.blendMeshLight = object.state.light;
    return drawn;
}

Core::Expected<void, std::string> DrawWornParts(Render::ModelBatch& batch, const Render::Models::IModelLibrary& models,
                                                const ItemPartOwner& owner, const WornParts& parts,
                                                std::uint8_t characterClass, Core::Duration frameClock,
                                                const ItemDrawContext& context,
                                                const std::function<void(const glm::vec3&)>& onPartBox)
{
    for (std::size_t k = parts.size(); k-- > 0;)
    {
        if (!parts[k])
            continue;
        ItemPart part = *parts[k];
        const auto model = static_cast<std::int16_t>(part.model);
        if (model < 0 || model >= ZeroedModelRecords)
            return Core::Unexpected(std::format("0x50d431: part {} of type {:#x} has the model {:#x}, outside the "
                                                "records 0x60dbe0 zeroes: the original writes +0x98 and 0x60cf70 "
                                                "reads and writes outside them (P-C352)",
                                                k, owner.type, part.model));
        part.skin = SkinIndexOf(characterClass);
        if ((characterClass & 7) == MaskedClass && k == HelmPart)
        {
            const auto light = MaskedClassLight(context);
            if (!light.HasValue())
                return Core::Unexpected(light.Error());
            if (owner.blendMeshLight != nullptr)
                *owner.blendMeshLight = light.Value();
            // 0x50d470..0x50d4a4
            if (std::ranges::find(Data::Models::HelmsWithMask, part.model) != Data::Models::HelmsWithMask.end())
                part.model = static_cast<std::uint16_t>(part.model + Data::Models::MaskHelmOffset);
        }
        if (auto drawn = DrawItemPart(batch, models, owner, part, frameClock, context, onPartBox); !drawn.HasValue())
            return drawn;
    }
    return {};
}
} // namespace Client::Scenes
