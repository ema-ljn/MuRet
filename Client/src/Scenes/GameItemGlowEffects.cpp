#include "Scenes/GameItemGlowEffects.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Shine.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "World/IFishStepServices.h"
#include "World/ObjectModelStepState.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float2.hpp>

#include <chrono>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ratio>
#include <span>
#include <string>

namespace Client::Scenes
{
// 69fd3c: FISTP qword con truncación; los callers conservan EAX, no una conversión int32 del host.
std::int32_t ItemClockWord(float time)
{
    constexpr double Limit = 0x1p63;
    constexpr double WordRange = 0x1p32;
    const double value = time;
    if (!std::isfinite(value) || value >= Limit || value < -Limit)
        return 0; // El patrón inválido64 enmascarado tiene la palabra baja en cero.
    double low = std::fmod(std::trunc(value), WordRange);
    if (low < 0)
        low += WordRange;
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(low));
}
namespace
{
using Result = Core::Expected<void, std::string>;
float F32(double value)
{
    return static_cast<float>(value);
}
float Time(Core::Duration clock, const ItemDrawContext& context)
{
    if (context.special && context.special->worldClock)
        return *context.special->worldClock;
    return static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(clock).count());
}
Core::Expected<double, std::string> Wave(float time, float frequency, float multiplier, float offset)
{
    const auto value = Render::Models::Detail::SineProductPC53(static_cast<double>(time) * frequency, multiplier);
    if (!value)
        return Core::Unexpected(std::string("60b3d0: fase del brillo fuera del perfil portable"));
    return *value + offset;
}
void Publish(const DrawnObject& object, const ItemDrawContext& context, std::optional<float> alpha)
{
    if (context.special && context.special->publishedObject)
    {
        *context.special->publishedObject = object;
        if (alpha)
            context.special->publishedObject->alpha = *alpha;
    }
    if (context.modelState && object.unlitMesh)
        context.modelState->unlitMeshes[static_cast<std::int16_t>(object.model)] =
            static_cast<std::int8_t>(*object.unlitMesh);
}
Core::Expected<glm::vec3, std::string> Point(const DrawnObject& object, const ItemDrawContext& context,
                                             std::size_t bone, const glm::vec3& offset, bool translated)
{
    if (!context.poseBones)
        return Core::Unexpected(std::string("4ec110: falta el préstamo de huesos retenidos del brillo"));
    const auto bones = context.poseBones();
    if (bone >= bones.size())
        return Core::Unexpected(std::string("4ec110: el hueso del brillo excede el scratch retenido"));
    const auto matrix = Render::Models::OriginalMatrixOf(bones[bone]);
    if (translated)
        return Render::Models::OriginalTransformPosition(matrix, offset, object.transform.scale,
                                                         object.transform.position);
    auto result = Render::Models::OriginalVectorTransform(offset, matrix);
    for (int component = 0; component < 3; ++component)
        result[component] = F32(static_cast<double>(result[component]) + object.transform.position[component]);
    return result;
}
Result Sprite(const ItemDrawContext& context, std::uint16_t texture, const glm::vec3& position, float scale,
              const glm::vec3& light)
{
    if (!context.special || !context.special->sprites)
        return Core::Unexpected(std::string("5491e0: falta el pool de sprites del brillo"));
    static_cast<void>(context.special->sprites->Create(texture, position, scale, light, 0, 0));
    return {};
}
Result SmallGlow(DrawnObject& object, float time, const ItemDrawContext& context)
{
    const bool second = object.model == 0x2247;
    if (second)
    {
        object.unlitMesh = std::int16_t{0}; // 60bf3e: model+88 antes de FSIN, object+70 y huesos/pool.
        Publish(object, context, {});
    }
    const auto wave = Wave(time, 0.002f, 0.35f, 0.65f);
    if (!wave.HasValue())
        return Core::Unexpected(wave.Error());
    const float pulse = F32(wave.Value()); // 60be4f/60bf68: el store precede a los productos del color.
    if (second)
    {
        const auto clockInteger = ItemClockWord(-time);
        object.state.uv.y = F32(static_cast<double>(clockInteger % 4000) * 0.00025f);
        Publish(object, context, {});
    }
    const auto point = Point(object, context, 0, second ? glm::vec3(0, -5, -15) : glm::vec3(0, 0, 15), false);
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    const float low = F32(static_cast<double>(pulse) * 0.32f);
    const float high = F32(static_cast<double>(pulse) * 2);
    return Sprite(context, 0x5c6, point.Value(), F32(static_cast<double>(pulse) * 0.8f + (second ? 2.5f : 2.0f)),
                  second ? glm::vec3(high, low, low) : glm::vec3(low, low, high));
}
Result PairedGlow(const DrawnObject& object, float time, const ItemDrawContext& context)
{
    const auto wave = Wave(time, 0.002f, 0.35f, 0.65f);
    if (!wave.HasValue())
        return Core::Unexpected(wave.Error());
    const auto pulse = wave.Value();
    const glm::vec3 light{F32(pulse * 2), F32(pulse * 0.32f), F32(pulse * 0.32f)};
    const std::size_t first = object.model == 0x1e24 ? 1 : 9;
    for (std::size_t bone = first; bone < first + 2; ++bone)
    {
        const auto point = Point(object, context, bone, {}, false);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        if (auto result = Sprite(context, 0x5c6, point.Value(), F32(pulse * 0.8f), light); !result.HasValue())
            return result;
    }
    return {};
}
Result Joint(const DrawnObject& object, const ItemDrawContext& context, glm::vec3& first, glm::vec3& second,
             std::int32_t type, std::int32_t subtype, float scale)
{
    if (!context.originalFrame)
        return {};
    if (!context.createJoint)
        return Core::Unexpected(std::string("530860: falta el creador de joints del brillo"));
    auto angles = object.transform.angles;
    const World::FishEffectRequest request{type,  first, second, angles, subtype, context.ownerContext,
                                           scale, -1,    0,      0,      -1};
    return context.createJoint(request);
}
Result BeamPair(const DrawnObject& object, const ItemDrawContext& context, std::size_t firstBone,
                std::size_t secondBone, float size)
{
    const auto a = Point(object, context, firstBone, {}, true);
    if (!a.HasValue())
        return Core::Unexpected(a.Error());
    const auto b = Point(object, context, secondBone, {}, true);
    if (!b.HasValue())
        return Core::Unexpected(b.Error());
    auto first = a.Value(), second = b.Value();
    if (auto result = Joint(object, context, first, second, 0x614, 14, size); !result.HasValue())
        return result;
    if (auto result = Joint(object, context, second, first, 0x613, 4, F32(static_cast<double>(size) + 5));
        !result.HasValue())
        return result;
    return Sprite(context, 0x62b, second, F32(static_cast<double>(size) * 0.035714287f), {0.6f, 0.3f, 0.8f});
}
Result HelperBeams(const DrawnObject& object, float time, const ItemDrawContext& context)
{
    const auto wave = Wave(time, 0.004f, 1, 1);
    if (!wave.HasValue())
        return Core::Unexpected(wave.Error());
    const float size = F32(wave.Value() * 0.3f * 10 + 20);
    for (std::size_t first = 30; first > 25; --first)
        if (auto result = BeamPair(object, context, first, first - 8, size); !result.HasValue())
            return result;
    for (std::size_t first = 7, second = 11; first > 2; --first, ++second)
        if (auto result = BeamPair(object, context, second, first, size); !result.HasValue())
            return result;
    return {};
}
Result StateLight(DrawnObject& object, float time, const ItemDrawContext& context)
{
    std::optional<Core::Expected<double, std::string>> light;
    if (object.model == 0x1a16)
        light = Wave(time, 0.001f, 1, 1.1f);
    else if (object.model == 0x1a17)
        light = Wave(time, 0.001f, 0.25f, 0.25f);
    else if (object.model == 0xc1d)
        light = Wave(time, 0.004f, 0.3f, 0.7f);
    else if (object.model == 0x102a || object.model == 0x142a)
        light = Wave(time, 0.001f, 0.4f, 0.6f);
    if (light)
    {
        if (!light->HasValue())
            return Core::Unexpected(light->Error());
        object.state.light = F32(light->Value());
        Publish(object, context, {});
    }
    if (object.model == 0x1025 || object.model == 0x1625 || object.model == 0x1825)
    {
        object.state.light = 1;
        Publish(object, context, {});
    }
    return {};
}
} // namespace
ItemShineClock ItemShineClockOf(Render::Models::ShinePass pass, Core::Duration clock)
{
    const float time = static_cast<float>(std::chrono::duration<double, std::milli>(clock).count());
    ItemShineClock shine;
    shine.word = ItemClockWord(time);
    shine.t = F32(static_cast<double>(shine.word % 10000) * 0.0001f);
    if (pass != Render::Models::ShinePass::Chrome4)
        return shine;
    // CoreDuration es finito y acotado: ambos argumentos están dentro de |a|<2^63.
    shine.chrome4Cosine = F32(*Render::Models::Detail::CosineProductPC53(static_cast<double>(time) * 0.001f, 1));
    shine.chrome4Sine = F32(*Render::Models::Detail::SineProductPC53(static_cast<double>(time) * 0.002f, 1));
    return shine;
}

glm::vec2 ItemShineTexCoords(Render::Models::ShinePass pass, const glm::vec3& normal, const ItemShineClock& clock)
{
    const std::int32_t word = clock.word;
    const float t = clock.t;
    const double nx = normal.x, ny = normal.y, nz = normal.z;
    using Render::Models::ShinePass;
    switch (pass)
    {
    case ShinePass::Shiny:
        return {F32(nz * 0.5f + 0.2f), F32((ny + 1) * 0.5f)};
    case ShinePass::Chrome:
        return {F32(nz * 0.5f + t), F32(ny * 0.5f + static_cast<double>(t) * 2)};
    case ShinePass::Chrome2:
    {
        const float f = F32(static_cast<double>(word % 5000) * 0.00024f - 0.4f);
        return {F32((nz + nx) * 0.8f + static_cast<double>(f) * 2), F32(nx + ny + static_cast<double>(f) * 3)};
    }
    case ShinePass::Chrome3:
    {
        const double dot = nz * -0.8f + nx * 0 + ny * -0.1f;
        return {F32(dot), F32(1 - dot)};
    }
    case ShinePass::Chrome4:
    {
        const float c = clock.chrome4Cosine;
        const float sine = clock.chrome4Sine;
        const double sum = static_cast<double>(c) * nx + static_cast<double>(sine) * ny + nz;
        const float u = F32(sum); // 4ed1fb: spill que el último FADD vuelve a leer.
        return {F32(static_cast<double>(sine) * 3 + ny * 0.5f + u),
                F32((1 - sum) - (nz * 0.5f + static_cast<double>(t) * 3))};
    }
    }
    return {};
}

Core::Expected<void, std::string> PrepareItemGlowEffects(DrawnObject& object, Core::Duration clock, std::uint8_t level,
                                                         const ItemDrawContext& context,
                                                         std::optional<float>& storedAlpha)
{
    const float time = Time(clock, context);
    if (object.model == 0x2246 || object.model == 0x2247)
        return SmallGlow(object, time, context);
    if (object.model == 0x1e24 || object.model == 0x1e25)
        return PairedGlow(object, time, context);
    if (object.model == 0x1e27)
    {
        const auto wave = Wave(time, 0.002f, 0.25f, 0.75f);
        if (!wave.HasValue())
            return Core::Unexpected(wave.Error());
        return Sprite(context, 0x5c6, object.transform.position, 2.5f,
                      {F32(wave.Value()), F32(wave.Value() * 0.5f), 0});
    }
    if (object.model == 0x1a18)
        return HelperBeams(object, time, context);
    if (object.model == 0x1e23)
    {
        const auto wave = Wave(time, 0.002f, 10, 15.65f);
        if (!wave.HasValue())
            return Core::Unexpected(wave.Error());
        object.state.blendMesh = 1;
        object.state.light = F32(wave.Value());
        storedAlpha = 2.0f;
        object.state.uv.y = F32(static_cast<double>(ItemClockWord(time) % 2000) * 0.0005f);
        if (context.special && context.special->objectLight)
            *context.special->objectLight = glm::vec3(F32(wave.Value() * 0.2f));
        Publish(object, context, storedAlpha);
    }
    if (object.model == 0x1e19 || object.model == 0x1c19)
    {
        if (level == 0)
            object.state.hiddenMesh = 1;
        else if (level == 1)
            object.state.hiddenMesh = 0;
        Publish(object, context, storedAlpha);
    }
    if (object.model == 0x1c1d)
    {
        object.state.hiddenMesh = 1;
        Publish(object, context, storedAlpha);
    }
    if (object.model == 0x224c)
    {
        object.state.blendMesh = 1;
        Publish(object, context, storedAlpha);
    }
    return StateLight(object, time, context);
}
} // namespace Client::Scenes
