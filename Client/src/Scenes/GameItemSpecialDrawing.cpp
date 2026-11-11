#include "Scenes/GameItemSpecialDrawing.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameItemGlowEffects.h"
#include "World/Sprites.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <ratio>
#include <span>
#include <string>
namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;
constexpr std::int16_t Body = -1;
constexpr std::uint32_t Textured = 2;
constexpr float Slow = 0.0001f;
constexpr float Scroll = 0.0005f;
constexpr float PulseFrequency = 0.001f;
constexpr Render::TextureSlot ArmorChrome{0x5c1};
float F32(double value)
{
    return static_cast<float>(value);
}
// Los parámetros de cada pase no son stores. Sólo Store publica los MOV/FSTP de609640.
class ItemRenderer
{
public:
    ItemRenderer(Render::ModelBatch& batch, const IModelLibrary& models, const DrawnObject& input, float nativeClock,
                 std::span<const Render::Models::SkinnedMesh> meshes, const ItemSpecialDrawContext& context)
        : object(input), time(nativeClock), m_batch(batch), m_models(models),
          m_clock(
              std::chrono::duration_cast<Core::Duration>(std::chrono::duration<long double, std::milli>(nativeClock))),
          m_meshes(meshes), m_context(context)
    {
    }
    bool ModelHasMeshes() const
    {
        return !m_models.Find(object.model)->meshes.empty();
    }
    void Store()
    {
        if (m_context.publishedObject)
            *m_context.publishedObject = object;
    }
    void Color(glm::vec3 color)
    {
        object.color = color;
        Store();
    }
    void Unlit(std::int16_t mesh)
    {
        object.unlitMesh = mesh;
        Store();
    }
    float Product(float factor) const
    {
        return F32(static_cast<double>(time) * factor);
    }
    double WaveValue(float frequency, float amplitude, float bias = 0) const
    {
        const auto sine = Render::Models::Detail::SineProductPC53(static_cast<double>(time) * frequency, amplitude);
        return sine ? *sine + bias : std::numeric_limits<double>::quiet_NaN();
    }
    float Wave(float frequency, float amplitude, float bias = 0) const
    {
        return F32(WaveValue(frequency, amplitude, bias));
    }
    Result Pass(std::int16_t mesh, std::uint32_t flags, std::optional<Render::TextureSlot> texture = {})
    {
        const auto result =
            DrawItemMeshPass(m_batch, m_models, object, m_clock, m_meshes, mesh, flags, texture, &object.color);
        Store();
        return result;
    }
    Result Layer(std::int16_t mesh, std::uint32_t flags, std::int16_t blend, float light, glm::vec2 uv,
                 std::optional<Render::TextureSlot> texture = {}, std::optional<float> alpha = {})
    {
        auto layer = object;
        layer.state.blendMesh = blend;
        layer.state.light = light;
        layer.state.uv = uv;
        if (alpha)
            layer.alpha = *alpha;
        const auto result =
            DrawItemMeshPass(m_batch, m_models, layer, m_clock, m_meshes, mesh, flags, texture, &object.color);
        Store();
        return result;
    }
    Result Layer(std::int16_t mesh, std::uint32_t flags, std::int16_t blend,
                 std::optional<Render::TextureSlot> texture = {}, std::optional<float> alpha = {})
    {
        return Layer(mesh, flags, blend, object.state.light, object.state.uv, texture, alpha);
    }
    Result RandomMesh(std::int16_t mesh)
    {
        if (!m_context.random)
            return Core::Unexpected(std::string("609640: falta el CRT global de los UV"));
        // El primer rand es V: los pushes del original van de derecha a izquierda.
        const auto v = F32(static_cast<double>(m_context.random->Next() % 10) * 0.1f);
        const auto u = F32(static_cast<double>(m_context.random->Next() % 10) * 0.1f);
        return Layer(mesh, Textured, mesh, object.state.light, {u, v}, {}, 1.0f);
    }
    DrawnObject object;
    float time = 0;

private:
    Render::ModelBatch& m_batch;
    const IModelLibrary& m_models;
    Core::Duration m_clock;
    std::span<const Render::Models::SkinnedMesh> m_meshes;
    const ItemSpecialDrawContext& m_context;
};
Result ChromeWeapon(ItemRenderer& draw)
{
    const auto type = draw.object.model;
    if (auto result = draw.Layer(0, type == 0x61f ? 2u : 0xau, type == 0x61f ? -1 : 0); !result.HasValue())
        return result;
    auto uv = draw.object.state.uv;
    uv.y = draw.Product(type == 0x225 ? Slow : -Slow);
    const auto alpha = (type == 0xc1c || type == 0xa24) ? 1 : draw.object.alpha;
    if (auto result = draw.Layer(0, 0x12, 0, draw.object.state.light, uv, ChromeTextureSlot, alpha); !result.HasValue())
        return result;
    if (type != 0x61f)
        return draw.Layer(1, Textured, type == 0x225 ? 0 : -1);
    return {};
}
Result DrawArmorChrome(ItemRenderer& draw)
{
    const auto color = draw.object.color;
    switch (draw.object.model & 0x1ff)
    {
    case 0x26:
        draw.Color({F32(static_cast<double>(color.r) * 0.85f), F32(static_cast<double>(color.g) * 0.85f),
                    F32(static_cast<double>(color.b) * 1.2f)});
        break;
    case 0x29:
        draw.Color({F32(static_cast<double>(color.r) * 0.3f), F32(static_cast<double>(color.g) * 0.8f), color.g});
        break;
    default:
        break;
    }
    if (auto result = draw.Pass(Body, 6, ArmorChrome); !result.HasValue())
        return result;
    draw.Color(color);
    return draw.Pass(Body, Textured);
}
Result Staff(ItemRenderer& draw)
{
    auto& state = draw.object.state;
    switch (draw.object.model)
    {
    case 0x61a:
        state.light = draw.Wave(PulseFrequency, 0.6f, 0.4f);
        draw.Store();
        if (auto result = draw.Pass(Body, Textured); !result.HasValue())
            return result;
        return draw.Layer(0, Textured, 0);
    case 0x61b:
        if (auto result = draw.Pass(Body, Textured); !result.HasValue())
            return result;
        draw.Color({0.1f, 0.3f, 1});
        state.blendMesh = 0;
        state.light = draw.Wave(PulseFrequency, 0.6f, 0.4f);
        draw.Store();
        if (auto result = draw.Layer(0, Textured, 0); !result.HasValue())
            return result;
        draw.Color({0.6f, 0.8f, 1});
        state.blendMesh = 1;
        state.light = 1;
        draw.Store();
        return draw.Layer(1, Textured, 1, 1, {state.uv.x, draw.Product(0.0003f)}, ChromeTextureSlot);
    case 0x61c:
        state.light = 1;
        state.hiddenMesh = 2;
        state.blendMesh = 2;
        draw.Store();
        if (auto result = draw.Pass(Body, Textured); !result.HasValue())
            return result;
        return draw.Layer(2, Textured, 2, 1, glm::vec2(draw.Product(Scroll)));
    default:
        state.light = 1;
        state.uv.x = draw.Product(0.0008f);
        draw.Store();
        if (auto result = draw.Pass(Body, Textured); !result.HasValue())
            return result;
        state.light = draw.Wave(PulseFrequency, 0.3f, 0.7f);
        draw.Store();
        return draw.Layer(1, 0x44, 0, state.light, {0, state.uv.y});
    }
}
Result Sword(ItemRenderer& draw)
{
    if (auto result = draw.Pass(Body, Textured); !result.HasValue())
        return result;
    if (draw.object.model == 0x226)
    {
        if (auto result = draw.Layer(2, Textured, 2, draw.Wave(0.0008f, 0.7f, 0.5f), draw.object.state.uv);
            !result.HasValue())
            return result;
        if (auto result = draw.Layer(1, Textured, 1); !result.HasValue())
            return result;
        // glColor3f no escribe model+48; RenderMesh conserva su autoridad de color.
        return draw.Layer(3, 0x44, 3, draw.object.state.light, {draw.Product(Slow), draw.Product(Scroll)});
    }
    if (auto result = draw.Layer(0, Textured, 0, draw.object.state.light, glm::vec2(draw.Product(Scroll)), {}, 1.0f);
        !result.HasValue())
        return result;
    draw.object.state.hiddenMesh = 0;
    draw.Store();
    draw.Unlit(1);
    return draw.Layer(1, Textured, -1, draw.object.state.light, {draw.object.state.uv.x, draw.Product(Scroll)});
}
Result BrightGroup(ItemRenderer& draw)
{
    draw.Color(glm::vec3(0.7f));
    draw.Unlit(0);
    if (auto result = draw.Pass(0, Textured); !result.HasValue())
        return result;
    const auto type = draw.object.model;
    if (type == 0x228)
    {
        draw.Color({1, 0.7f, 0.4f});
        const auto offset = F32(static_cast<double>(ItemClockWord(draw.time) % 4000) * 0.0004f - 0.7f);
        if (auto result = draw.Layer(0, 0x42, draw.object.state.blendMesh, draw.object.state.light,
                                     {offset, draw.object.state.uv.y}, Render::TextureSlot{0x6b3});
            !result.HasValue())
            return result;
        draw.Color(glm::vec3(0.7f));
    }
    else
    {
        const auto period = type == 0xc1e ? 2000 : 4000;
        const auto offset = F32(static_cast<double>(ItemClockWord(draw.time) % period) * 0.0002f - 0.3f);
        if (auto result = draw.Layer(0, 0x42, draw.object.state.blendMesh, draw.object.state.light, glm::vec2(offset),
                                     Render::TextureSlot{static_cast<std::uint16_t>(type == 0xc1e ? 0x6b4 : 0x6b2)});
            !result.HasValue())
            return result;
    }
    draw.Unlit(-1);
    return {};
}
Result Bow(ItemRenderer& draw)
{
    const auto uv = draw.object.state.uv;
    const auto light = draw.object.state.light;
    if (draw.object.model == 0xa25)
    {
        const auto color = draw.object.color;
        draw.Color({F32(static_cast<double>(color.r) * 0.8f), 0, F32(static_cast<double>(color.b) * 0.8f)});
        if (auto result = draw.Layer(0, 0x12, 0, light, {draw.Product(-0.0002f), uv.y}, ChromeTextureSlot, 1.0f);
            !result.HasValue())
            return result;
        draw.Color(color);
        if (auto result = draw.Layer(0, 0x44, 0, ArmorChrome, 1.0f); !result.HasValue())
            return result;
        draw.Color(color);
        return draw.Pass(Body, Textured);
    }
    const auto pulse = draw.Wave(0.002f, 0.3f, 0.5f);
    for (const auto mesh : {std::int16_t{0}, std::int16_t{1}, std::int16_t{2}, std::int16_t{6}})
        if (auto result = draw.Layer(mesh, Textured, -1, {}, 1.0f); !result.HasValue())
            return result;
    if (auto result = draw.Layer(3, Textured, 3, pulse, uv, {}, 1.0f); !result.HasValue())
        return result;
    if (auto result = draw.Layer(4, Textured, 4, pulse, uv, {}, 1.0f); !result.HasValue())
        return result;
    return draw.Layer(5, Textured, -1, {}, 1.0f);
}
Result Orb(ItemRenderer& draw, const ItemSpecialDrawContext& context)
{
    const auto uv = draw.object.state.uv;
    draw.Unlit(1);
    if (auto result = draw.Layer(1, Textured, -1, draw.object.state.light, {uv.x, draw.Product(Scroll)}, {}, 1.0f);
        !result.HasValue())
        return result;
    draw.Unlit(-1);
    if (draw.object.model == 0x1e2e)
        return draw.Layer(0, Textured, -1, {}, 1.0f);
    draw.Color({1, 0.5f, 0});
    if (!context.pulse)
        return Core::Unexpected(std::string("60a444: falta el pulso global 6c9de4/6c9de8"));
    auto& pulse = *context.pulse;
    if (pulse.x > 1)
        pulse = {1, -0.01f};
    else if (!(pulse.x >= 0.01f))
        pulse = {0.01f, 0.01f};
    if (auto result = draw.Layer(2, Textured, 2, pulse.x, uv, {}, 1.0f); !result.HasValue())
        return result;
    pulse.x = F32(static_cast<double>(pulse.x) + pulse.y);
    draw.Color(glm::vec3(1));
    if (auto result = draw.Layer(0, Textured, -1, {}, 1.0f); !result.HasValue())
        return result;
    return draw.Layer(0, 0x44, -1, {}, 0.3f);
}
Result Shield(ItemRenderer& draw, std::uint32_t flags)
{
    if (auto result = draw.Pass(Body, flags); !result.HasValue())
        return result;
    if (draw.object.model == 0xc1d)
    {
        if (auto result = draw.Layer(1, 0x44, 1, 0.2f, draw.object.state.uv); !result.HasValue())
            return result;
        if (auto result = draw.Layer(0, 0x42, 0, draw.Wave(0.005f, 1), draw.object.state.uv); !result.HasValue())
            return result;
        return draw.Layer(0, 0x1040, 0);
    }
    if (auto result = draw.Layer(1, 0x42, 1, 1, draw.object.state.uv); !result.HasValue())
        return result;
    return draw.Layer(3, 0x42, 3, 1, draw.object.state.uv);
}
Result Wing(ItemRenderer& draw)
{
    const auto color = draw.object.color;
    const auto uv = draw.object.state.uv;
    const auto light = draw.object.state.light;
    if (draw.object.model == 0xe21)
    {
        draw.Color(color * 0.3f);
        if (auto result = draw.Layer(2, 1, -1, {}, 1.0f); !result.HasValue())
            return result;
        draw.Color(color);
        if (auto result = draw.Layer(2, 0x44, 2, light, {uv.x, draw.Product(0.01f)}, ChromeTextureSlot, 1.0f);
            !result.HasValue())
            return result;
        if (auto result = draw.Layer(0, Textured, -1, {}, 1.0f); !result.HasValue())
            return result;
        if (auto result = draw.RandomMesh(1); !result.HasValue())
            return result;
        draw.Color(glm::vec3(F32(draw.WaveValue(PulseFrequency, 0.4f, 0.6f) * color.r)));
        return draw.Layer(2, 0x42, 2, light, {draw.Product(Slow), draw.Product(Scroll)}, {}, 1.0f);
    }
    if (auto result = draw.Layer(1, Textured, -1, {}, 0.8f); !result.HasValue())
        return result;
    if (auto result = draw.Layer(3, Textured, -1, {}, 0.5f); !result.HasValue())
        return result;
    if (auto result = draw.Layer(0, Textured, -1, {}, 1.0f); !result.HasValue())
        return result;
    if (auto result = draw.Layer(2, Textured, 2, light, {draw.Product(Scroll), uv.y}, {}, 1.0f); !result.HasValue())
        return result;
    return draw.RandomMesh(3);
}
Result SpecialArmor(ItemRenderer& draw, const ItemSpecialDrawContext& context)
{
    if (draw.object.model == 0x121b)
    {
        if (!context.privateBoneOrigin)
            return Core::Unexpected(std::string("60acd9: falta la matriz privada del casco"));
        const auto position = context.privateBoneOrigin();
        if (!position.HasValue())
            return Core::Unexpected(position.Error());
        if (!context.objectLight)
            return Core::Unexpected(std::string("60ad23: falta la luz retenida del objeto"));
        *context.objectLight = {0.4f, 0.6f, 0.8f};
        if (!context.sprites)
            return Core::Unexpected(std::string("60ad3d: falta el pool de sprites del casco"));
        context.sprites->Create(0x5ac, position.Value(), 6, *context.objectLight, 0.5f, 0);
        const auto pulse = draw.WaveValue(0.05f, 0.4f, 0.9f);
        *context.objectLight = {F32(pulse * 0.3f), F32(pulse * 0.5f), F32(pulse * 0.8f)};
        context.sprites->Create(0x5ac, position.Value(), 2, *context.objectLight, 0, 0);
    }
    const auto color = draw.object.color;
    draw.Color({0.9f, 0.7f, 1});
    if (auto result = draw.Pass(0, Textured); !result.HasValue())
        return result;
    if (auto result = draw.Layer(0, 0x44, 1, Render::TextureSlot{0x68f}); !result.HasValue())
        return result;
    if (auto result = draw.Layer(0, 0x48, 1, Render::TextureSlot{0x68f}); !result.HasValue())
        return result;
    draw.Color(color);
    return {};
}
Result PurpleHelper(ItemRenderer& draw)
{
    draw.Color({0.8f, 0.6f, 1});
    if (auto result = draw.Layer(Body, 0x44, draw.object.state.blendMesh, 0.5f, draw.object.state.uv, ArmorChrome);
        !result.HasValue())
        return result;
    draw.Color(glm::vec3(1));
    return draw.Pass(Body, Textured);
}

Result PulsingSword(ItemRenderer& draw)
{
    if (auto result = draw.Layer(3, Textured, -1, {}, 1.0f); !result.HasValue())
        return result;
    if (auto result = draw.Layer(1, Textured, -1, {}, 1.0f); !result.HasValue())
        return result;
    if (auto result =
            draw.Layer(1, Textured, 1, draw.object.state.light, draw.object.state.uv, {}, draw.Wave(0.01f, 1));
        !result.HasValue())
        return result;
    if (auto result = draw.Layer(0, 6, 0, draw.object.state.light,
                                 {draw.object.state.uv.x, draw.Product(PulseFrequency)}, ChromeTextureSlot, 1.0f);
        !result.HasValue())
        return result;
    draw.Color(glm::vec3(F32(draw.WaveValue(PulseFrequency, 1, 1) * 0.5f * draw.object.color.r)));
    return draw.Layer(2, 0x42, 2, draw.object.state.light, {draw.Product(Slow), draw.Product(-Scroll)}, {}, 1.0f);
}

Result ScrollingSpear(ItemRenderer& draw, std::uint32_t flags)
{
    if (auto result = draw.Pass(Body, flags); !result.HasValue())
        return result;
    return draw.Layer(1, Textured, 1, draw.object.state.light, {draw.Product(Slow), draw.Product(Scroll)}, {}, 1.0f);
}

Result PulsingStaff(ItemRenderer& draw, std::uint32_t flags)
{
    {
        const auto red = draw.object.color.r;
        if (auto result = draw.Pass(Body, flags); !result.HasValue())
            return result;
        draw.Color(glm::vec3(F32(draw.WaveValue(PulseFrequency, 0.5f, 0.3f) * red)));
        return draw.Layer(2, Textured, 2, draw.object.state.light,
                          {draw.Product(PulseFrequency), draw.Product(-Scroll)}, {}, 1.0f);
    }
}

Result PetChrome(ItemRenderer& draw, std::uint32_t flags)
{
    const auto type = draw.object.model;
    if (auto result = draw.Pass(Body, flags); !result.HasValue())
        return result;
    draw.Color(type == 0x1c16 ? glm::vec3{0.8f, 0.4f, 0.1f} : glm::vec3{0.3f, 0.8f, 1});
    return draw.Pass(type == 0x1c16 ? Body : 0, 0x44);
}

Result TranslucentSword(ItemRenderer& draw)
{
    draw.Color(glm::vec3(1));
    if (auto result = draw.Pass(Body, Textured); !result.HasValue())
        return result;
    if (auto result = draw.Layer(0, Textured, 0, draw.object.state.light, glm::vec2(draw.object.state.uv.x));
        !result.HasValue())
        return result;
    {
        const auto pulse = draw.WaveValue(0.0015f, 0.03f, 0.3f);
        draw.Color({F32(pulse), F32(pulse), F32(pulse + 0.1f)});
    }
    if (auto result = draw.Layer(2, Textured, -2, draw.object.state.light, glm::vec2(draw.object.state.uv.x));
        !result.HasValue())
        return result;
    draw.object.alpha = 0.5f;
    draw.Store();
    draw.Color({0.4f, 0.4f, 0.8f});
    return draw.Layer(1, Textured, -2, draw.object.state.light, glm::vec2(draw.object.state.uv.x));
}

Result EffectChrome(ItemRenderer& draw, std::uint32_t flags)
{
    // 60a98d: un modelo cargado con cero mallas recorre el default, sin los stores RGB.
    if (!draw.ModelHasMeshes())
        return draw.Pass(Body, flags);
    if (auto result = draw.Pass(Body, Textured); !result.HasValue())
        return result;
    if ((flags & 4) == 0)
        return {};
    draw.Color({0.75f, 0.55f, 0.5f});
    if (auto result = draw.Pass(0, 0x1040); !result.HasValue())
        return result;
    draw.Color(glm::vec3(1));
    return draw.Pass(0, 0x46);
}

Result SelectedHelper(ItemRenderer& draw)
{
    const auto type = draw.object.model;
    {
        const auto hidden = draw.object.state.hiddenMesh;
        if (hidden != 0 && hidden != 1)
            return {};
        const std::int16_t mesh = hidden == 1 ? 0 : 1;
        draw.Color(glm::vec3(1));
        if (auto result = draw.Layer(mesh, Textured, draw.object.state.blendMesh, {}, 1.0f); !result.HasValue())
            return result;
        if (type == 0x1c19)
            return {};
        draw.Color({0.1f, 0.5f, 1});
        if (auto result = draw.Layer(mesh, 0x48, draw.object.state.blendMesh, {}, 1.0f); !result.HasValue())
            return result;
        return draw.Layer(mesh, 0x44, draw.object.state.blendMesh, {}, 1.0f);
    }
}

Result BlueHelper(ItemRenderer& draw)
{
    draw.Color(glm::vec3(1));
    if (auto result = draw.Layer(0, Textured, draw.object.state.blendMesh, {}, 1.0f); !result.HasValue())
        return result;
    draw.Color({0, 0.5f, 1});
    return draw.Layer(1, 0x44, draw.object.state.blendMesh, {}, 1.0f);
}
Result Dispatch(ItemRenderer& draw, const ItemSpecialDrawContext& context)
{
    const auto type = draw.object.model;
    const auto flags = context.flags;
    switch (type)
    {
    case 0x1221:
    case 0x1421:
    case 0x1621:
    case 0x1821:
    case 0x1226:
    case 0x1426:
    case 0x1626:
    case 0x1826:
    case 0x1229:
    case 0x1429:
    case 0x1629:
    case 0x1829:
        return DrawArmorChrome(draw);
    case 0x225:
    case 0xc1c:
    case 0xa24:
    case 0x61f:
        return ChromeWeapon(draw);
    case 0x61a:
    case 0x61b:
    case 0x61c:
    case 0x61d:
        return Staff(draw);
    case 0x226:
    case 0x227:
        return Sword(draw);
    case 0xa27:
    case 0xc1e:
    case 0x620:
    case 0x228:
        return BrightGroup(draw);
    case 0xa25:
    case 0xa26:
        return Bow(draw);
    case 0x1e2e:
    case 0x1e2f:
        if (type == 0x1e2f)
            draw.Color(glm::vec3(1));
        return Orb(draw, context);
    case 0xc1d:
    case 0x61e:
        return Shield(draw, flags);
    case 0xe21:
    case 0xe22:
        return Wing(draw);
    case 0x1a18:
        return PurpleHelper(draw);
    case 0x231:
        return PulsingSword(draw);
    case 0x81c:
        return ScrollingSpear(draw, flags);
    case 0x619:
        return PulsingStaff(draw, flags);
    case 0x1c16:
    case 0x1c17:
        return PetChrome(draw, flags);
    case 0x229:
        return TranslucentSword(draw);
    case 0xa15:
        if ((flags & 0x100) != 0)
        {
            draw.Color(glm::vec3(0.1f));
            return draw.Pass(Body, flags - 0x100);
        }
        break;
    case 0x1f7:
        return EffectChrome(draw, flags);
    case 0x1e19:
    case 0x1c19:
        return SelectedHelper(draw);
    case 0x1c1d:
        return BlueHelper(draw);
    default:
        break;
    }
    if (context.objectFlags == 0x102 &&
        (type == 0x101b || type == 0x121b || type == 0x141b || type == 0x161b || type == 0x181b))
        return SpecialArmor(draw, context);
    return draw.Pass(Body, flags);
}
} // namespace
Core::Expected<void, std::string> DrawSpecialItemMeshes(Render::ModelBatch& batch, const IModelLibrary& models,
                                                        const DrawnObject& object, Core::Duration frameClock,
                                                        std::span<const Render::Models::SkinnedMesh> meshes,
                                                        const ItemSpecialDrawContext& context)
{
    const auto* model = models.Find(object.model);
    if (!model)
        return Core::Unexpected(std::format("609640: falta el modelo {:#x}", object.model));
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(frameClock).count();
    const float nativeClock = context.worldClock.value_or(static_cast<float>(milliseconds));
    using Milliseconds = std::chrono::duration<long double, std::milli>;
    constexpr auto LowestClock = std::chrono::duration_cast<Milliseconds>(Core::Duration::min()).count();
    constexpr auto HighestClock = std::chrono::duration_cast<Milliseconds>(Core::Duration::max()).count();
    if (!std::isfinite(nativeClock) || nativeClock < LowestClock || nativeClock > HighestClock)
        return Core::Unexpected(std::string("609640: reloj fuera del perfil portable"));
    ItemRenderer draw(batch, models, object, nativeClock, meshes, context);
    return Dispatch(draw, context);
}
} // namespace Client::Scenes
