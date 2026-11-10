#include "Scenes/GameItemGlowSpecialDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "World/ObjectModelStepState.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;
constexpr Render::TextureSlot ArmorChrome{0x5c1};
float F32(double value)
{
    return static_cast<float>(value);
}
class EarlyItemRenderer
{
public:
    EarlyItemRenderer(Render::ModelBatch& batch, const IModelLibrary& models, DrawnObject& input, Core::Duration clock,
                      std::span<const Render::Models::SkinnedMesh> meshes, const ItemDrawContext& drawContext)
        : object(input), context(drawContext), m_batch(batch), m_models(models), m_clock(clock), m_meshes(meshes)
    {
        time = drawContext.special && drawContext.special->worldClock
                   ? *drawContext.special->worldClock
                   : static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(clock).count());
    }
    void Store()
    {
        if (context.special && context.special->publishedObject)
            *context.special->publishedObject = object;
        if (context.modelState && object.unlitMesh)
            context.modelState->unlitMeshes[static_cast<std::int16_t>(object.model)] =
                static_cast<std::int8_t>(*object.unlitMesh);
    }
    void Color(const glm::vec3& value)
    {
        object.color = value;
        Store();
    }
    void Unlit(std::int16_t value)
    {
        object.unlitMesh = value;
        Store();
    }
    Result Pass(std::int16_t mesh, std::uint32_t flags, std::optional<Render::TextureSlot> texture = {},
                std::optional<float> alpha = {}, std::optional<std::int16_t> blend = {},
                std::optional<float> light = {})
    {
        auto layer = object;
        if (alpha)
            layer.alpha = *alpha;
        if (blend)
            layer.state.blendMesh = *blend;
        if (light)
            layer.state.light = *light;
        return DrawItemMeshPass(m_batch, m_models, layer, m_clock, m_meshes, mesh, flags, texture);
    }
    Core::Expected<double, std::string> Wave(float frequency, float multiplier, float offset)
    {
        const auto wave = Render::Models::Detail::SineProductPC53(static_cast<double>(time) * frequency, multiplier);
        if (!wave)
            return Core::Unexpected(std::string("60b3d0: fase del pase especial fuera del perfil portable"));
        return *wave + offset;
    }
    DrawnObject& object;
    const ItemDrawContext& context;
    float time = 0;

private:
    Render::ModelBatch& m_batch;
    const IModelLibrary& m_models;
    Core::Duration m_clock;
    std::span<const Render::Models::SkinnedMesh> m_meshes;
};
Result Fenrir(EarlyItemRenderer& draw, int level)
{
    draw.Color(glm::vec3(1));
    draw.Unlit(0);
    if (auto result = draw.Pass(0, 2, {}, {}, -1); !result.HasValue())
        return result;
    if (level == 2 || level == 3)
    {
        if (auto result = draw.Pass(1, 2, {}, {}, -1); !result.HasValue())
            return result;
        if (level == 3)
            if (auto result = draw.Pass(2, 2, {}, {}, -1); !result.HasValue())
                return result;
        draw.Color({0.75f, 0.65f, 0.5f});
        if (auto result = draw.Pass(1, 0x44, ChromeTextureSlot, {}, -1); !result.HasValue())
            return result;
        if (level == 3)
            if (auto result = draw.Pass(2, 0x44, ChromeTextureSlot, {}, -1); !result.HasValue())
                return result;
    }
    draw.Unlit(-1);
    return {};
}
Result Scroll(EarlyItemRenderer& draw)
{
    draw.object.state.light = 1;
    draw.Store();
    const auto color = draw.object.color;
    draw.object.color = draw.object.vertexLight ? color : glm::vec3(1);
    if (auto result = draw.Pass(0, 2, {}, {}, -1); !result.HasValue())
        return result;
    draw.object.color = color;
    const auto wave = draw.Wave(0.001f, 0.5f, 0.5f);
    if (!wave.HasValue())
        return Core::Unexpected(wave.Error());
    draw.object.state.light = F32(wave.Value());
    draw.Store();
    if (auto result = draw.Pass(1, 0x42, {}, {}, 1); !result.HasValue())
        return result;
    const auto result = draw.Pass(2, 0x42, {}, {}, 2, F32(1.0 - draw.object.state.light));
    draw.object.color = color;
    return result;
}
Result EventItem(EarlyItemRenderer& draw, std::uint8_t packedLevel)
{
    const bool second = packedLevel == 15;
    draw.Color(second ? glm::vec3(0.5f, 0.3f, 0.2f) : glm::vec3(0.2f, 0.3f, 0.5f));
    if (auto result = draw.Pass(-1, 2); !result.HasValue())
        return result;
    draw.Color(second ? glm::vec3(1, 0.3f, 0.1f) : glm::vec3(0.1f, 0.3f, 1));
    if (auto result = draw.Pass(-1, 0x44); !result.HasValue())
        return result;
    return draw.Pass(-1, 0x48);
}
Result QuestColor(EarlyItemRenderer& draw, int level)
{
    if (level == 0)
        draw.Color({0, 0.5f, 1});
    else if (level == 1)
        draw.Color({1, 0.2f, 0});
    else if (level == 2)
        draw.Color({1, 0.8f, 0});
    else if (level == 3)
        draw.Color({0.6f, 0.8f, 0.4f});
    if (auto result = draw.Pass(-1, 8, ArmorChrome); !result.HasValue())
        return result;
    return draw.Pass(-1, 0x44, ArmorChrome);
}
Result RedBlueItem(EarlyItemRenderer& draw)
{
    draw.Color({0.9f, 0.1f, 0.1f});
    const auto wave = Render::Models::Detail::SineProductPC53(static_cast<double>(draw.context.map) * 0.0001f, 1);
    if (!wave)
        return Core::Unexpected(std::string("60bd0f: fase de mapa inválida"));
    draw.object.state.uv = {F32(*wave), F32(static_cast<double>(draw.time) * -0.0005f)};
    draw.Unlit(0);
    if (auto result = draw.Pass(-1, 2, ChromeTextureSlot); !result.HasValue())
        return result;
    draw.Unlit(-1);
    draw.Color(glm::vec3(0.9f));
    return draw.Pass(-1, 2);
}
Result EarlyCase(EarlyItemRenderer& draw, int level, std::uint8_t packedLevel)
{
    switch (draw.object.model)
    {
    case 0x1e2d:
        return Fenrir(draw, level);
    case 0x1a27:
    case 0x1a28:
    case 0x1a29:
    case 0x1a2a:
        return Scroll(draw);
    case 0x1c21:
        return QuestColor(draw, level);
    case 0x2245:
        draw.Color(glm::vec3(0.9f));
        if (auto result = draw.Pass(-1, 2); !result.HasValue())
            return result;
        return draw.Pass(-1, 0x44, ArmorChrome, 0.5f);
    case 0x2248:
        draw.Color({0.3f, 0.8f, 1});
        if (auto result = draw.Pass(-1, 2, {}, {}, -1); !result.HasValue())
            return result;
        draw.Color({1, 0.8f, 0.3f});
        return draw.Pass(-1, 0x44, ArmorChrome, {}, -1);
    case 0x2240:
        draw.Color({0.4f, 0.6f, 1});
        if (auto result = draw.Pass(-1, 1, {}, 1, 0, 1); !result.HasValue())
            return result;
        return draw.Pass(-1, 0x44, {}, 1, 0, 1);
    case 0x223f:
        return EventItem(draw, packedLevel);
    case 0x1c23:
        return RedBlueItem(draw);
    case 0x1c24:
    {
        draw.Color(glm::vec3(0.8f));
        const auto wave = draw.Wave(0.002f, 0.3f, 0.7f);
        if (!wave.HasValue())
            return Core::Unexpected(wave.Error());
        return draw.Pass(-1, 0x42, {}, 1, 0, F32(wave.Value()));
    }
    case 0x1c31:
        if (auto result = draw.Pass(-1, 2); !result.HasValue())
            return result;
        if (level == 0 || level == 1)
        {
            if (level == 1)
                draw.Color({0.3f, 0.8f, 1});
            return draw.Pass(0, 0x42, {}, {}, 0);
        }
        return {};
    default:
        return {};
    }
}
} // namespace
Core::Expected<bool, std::string> DrawEarlyItemGlow(Render::ModelBatch& batch, const IModelLibrary& models,
                                                    DrawnObject& object, Core::Duration clock,
                                                    std::span<const Render::Models::SkinnedMesh> meshes, int level,
                                                    std::uint8_t packedLevel, const ItemDrawContext& context)
{
    const auto type = object.model;
    const bool handled = type == 0x1e2d || (type >= 0x1a27 && type <= 0x1a2a) || type == 0x1c21 || type == 0x2245 ||
                         (type == 0x2248 && level == 9) || (type == 0x2240 && level == 13) ||
                         (type == 0x223f && (packedLevel == 14 || packedLevel == 15)) || type == 0x1c23 ||
                         type == 0x1c24 || type == 0x1c31;
    if (!handled)
        return false;
    EarlyItemRenderer draw(batch, models, object, clock, meshes, context);
    const auto result = EarlyCase(draw, level, packedLevel);
    if (!result.HasValue())
        return Core::Unexpected(result.Error());
    return true;
}
} // namespace Client::Scenes
