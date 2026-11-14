#include "Scenes/MonsterBodyPasses.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"

#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;

// La textura de 0x4ef0c0 y 0x4ecc30: −1 deja la de cada malla; otra negativa no tiene un slot.
Core::Expected<std::optional<Render::TextureSlot>, std::string> TextureOf(std::int32_t texture)
{
    if (texture == -1)
        return std::optional<Render::TextureSlot>{};
    if (texture < 0 || texture > 0xffff)
        return Core::Unexpected(std::format("0x4ecc30: la textura {} no es un slot", texture));
    return std::optional<Render::TextureSlot>{Render::TextureSlot{static_cast<std::uint16_t>(texture)}};
}

DrawnObject PassObject(const MonsterBodyModel& model, const BodyPassArgs& args)
{
    DrawnObject object = model.object;
    object.alpha = args.alpha;
    object.state.blendMesh = static_cast<std::int16_t>(args.blendMesh);
    object.state.light = args.light;
    object.state.uv = args.uv;
    object.state.hiddenMesh = static_cast<std::int16_t>(args.hiddenMesh);
    return object;
}
} // namespace

BodyPassArgs ActorPassArgs(const ActorState& actor)
{
    return {.alpha = actor.alpha,
            .blendMesh = actor.dword64,
            .light = actor.blendMeshLight,
            .uv = {actor.textureU, 0.0f},
            .hiddenMesh = actor.dword58,
            .texture = -1};
}

Result DrawBodyPass(MonsterBodyModel& model, std::uint32_t flags, const BodyPassArgs& args)
{
    const auto texture = TextureOf(args.texture);
    if (!texture.HasValue())
        return Core::Unexpected(texture.Error());
    const DrawnObject object = PassObject(model, args);
    if (flags == 2 && !texture.Value())
    {
        DrawObjectFromMeshes(model.batch, model.models, object, model.frameClock, model.meshes);
        return {};
    }
    return DrawItemMeshPass(model.batch, model.models, object, model.frameClock, model.meshes, -1, flags,
                            texture.Value(), &model.object.color);
}

Result DrawMeshPass(MonsterBodyModel& model, std::int16_t mesh, std::uint32_t flags, const BodyPassArgs& args)
{
    const auto texture = TextureOf(args.texture);
    if (!texture.HasValue())
        return Core::Unexpected(texture.Error());
    if (mesh < 0)
        return Core::Unexpected(std::format("0x4ecc30: la malla {} es negativa", mesh));
    BodyPassArgs meshArgs = args;
    meshArgs.hiddenMesh = -1;
    const DrawnObject object = PassObject(model, meshArgs);
    return DrawItemMeshPass(model.batch, model.models, object, model.frameClock, model.meshes, mesh, flags,
                            texture.Value(), &model.object.color);
}

void RequestBodySound(MonsterBodyFrame& frame, std::int32_t sound)
{
    // 0x41ed10 (CL10): el sonido queda anotado; entre frames del original no se pide.
    if (frame.originalFrame && frame.lastSoundId != nullptr)
        *frame.lastSoundId = sound;
}

std::int32_t ReadLogicSteps(MonsterBodyFrame& frame)
{
    const std::int32_t steps = frame.cloth.logicSteps;
    if (frame.cloth.openCounterAccesses == 0)
    {
        for (int index = 0; index < 4; ++index)
            static_cast<void>(frame.random.Next());
    }
    return steps;
}
} // namespace Client::Scenes
