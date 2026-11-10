#include "World/GroundEffectDrawing.h"

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/ModelEffects.h"
#include "World/KalimaSurfaceDecal.h"
#include "World/OriginalFtol.h"
#include "World/TerrainDecal.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::World
{
namespace
{
// Floats del build c32bbd5f: 0x6b87b8, 0x6b8854, 0x6b8a48, 0x6b8ae8 y 0x6ba2ac.
constexpr float Tenth = 0.1f;
constexpr float Fifth = 0.2f;
constexpr float SmallGrowth = 0.05f;
constexpr float ArrivalGrowth = 0.15f;
constexpr float SecondArrivalGrowth = 0.55f;
// 0x6b87b4, 0x6b8a20, 0x6b8a1c, 0x6b882c y 0x6ba26c.
constexpr float TwoFifths = 0.4f;
constexpr float ThreeFifths = 0.6f;
constexpr float NineTenths = 0.9f;
constexpr float Half = 0.5f;
constexpr float LightDecay = std::bit_cast<float>(0x3f68ba2eu);
constexpr std::int32_t FullArrivalLife = 20;
constexpr std::int32_t FadeLife = 5;
constexpr std::int32_t SecondArrivalHalfLife = 10;
constexpr std::int32_t PulseLife = 60;
constexpr std::int32_t ClockTurn = 3600;
constexpr std::int32_t SpawnInterval = 25;
constexpr std::int32_t ArrivalDecal = 0x61e;
constexpr std::int32_t ArrivalRing = 0x61f;
constexpr float DecalLift = 5.0f;
constexpr float RandomDecalSide = 2.0f;
constexpr int RandomLightCount = 4;
constexpr int RandomLightBase = 8;
// Argumentos de 0x52e224/0x52e24e y datos de 0x53d2c0 (0x6b8858, 0x6ba324, 0x6b8864).
constexpr float RingBottomRadius = 90.0f;
constexpr float RingTopRadius = 130.0f;
constexpr float RingHeight = 200.0f;
constexpr float RingAngleStep = 30.0f;
constexpr float RingUvStep = std::bit_cast<float>(0x3daaaaabu);
constexpr int RingSegments = 12;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// SUB de 32 bits, incluso en los bordes del entero con signo.
std::int32_t Difference(std::int32_t left, std::int32_t right)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(left) - std::bit_cast<std::uint32_t>(right));
}

void SetBlend(Render::ModelBatch& batch, Render::ModelBlend blend)
{
    Render::ModelDrawState state = batch.State();
    state.blend = blend;
    state.cullEnabled = false;
    state.depthWrite = blend == Render::ModelBlend::Alpha;
    state.alphaTest = blend == Render::ModelBlend::Alpha;
    state.texture2d = true;
    // El perfil es observable aun sin vértices. Estas operaciones no ligan una textura ajena.
    constexpr std::array Operations{Render::ModelStateOperation::Blend, Render::ModelStateOperation::Cull,
                                    Render::ModelStateOperation::DepthWrite, Render::ModelStateOperation::AlphaTest,
                                    Render::ModelStateOperation::Texture2d};
    for (const Render::ModelStateOperation operation : Operations)
    {
        batch.ApplyOperation(state, operation);
    }
}

Core::Expected<void, std::string> DrawDecal(Render::ModelBatch& batch, const ModelEffect& effect,
                                            const GroundEffectResources& resources, std::int32_t slot, float width,
                                            float height, const glm::vec3& color, float angle)
{
    return DrawTerrainDecal(batch, resources.heights,
                            TerrainDecal{.slot = slot,
                                         .x = effect.transform.position.x,
                                         .y = effect.transform.position.y,
                                         .width = width,
                                         .height = height,
                                         .color = color,
                                         .angle = angle,
                                         .alpha = 1.0f,
                                         .lift = DecalLift});
}

// 0x5fc9c0 guarda los mismos elementos de 0x5fc8f0 transpuestos, con los mismos redondeos.
Render::Models::OriginalMatrix RingRotation(float angle)
{
    const auto rotation = Render::Models::OriginalAngleMatrix(glm::vec3(0.0f, 0.0f, angle));
    Render::Models::OriginalMatrix inverse{};
    constexpr std::size_t Rows = 3;
    constexpr std::size_t Stride = 4;
    for (std::size_t row = 0; row < Rows; ++row)
    {
        for (std::size_t column = 0; column < Rows; ++column)
        {
            inverse[(row * Stride) + column] = rotation[(column * Stride) + row];
        }
    }
    return inverse;
}

glm::vec3 RingPoint(const glm::vec3& position, float radius, float height,
                    const Render::Models::OriginalMatrix& rotation)
{
    const glm::vec3 rotated = Render::Models::OriginalVectorRotate(glm::vec3(0.0f, radius, height), rotation);
    return {F32(D(rotated.x) + D(position.x)), F32(D(rotated.y) + D(position.y)), F32(D(rotated.z) + D(position.z))};
}

// 0x53d2c0 con los argumentos de 0x52e224/0x52e24e: doce quads, blancos abajo y negros arriba.
// Los anillos no dependen de la vida; se atenúa solo el calco posterior.
void DrawArrivalRing(Render::ModelBatch& batch, const glm::vec3& position, float angle)
{
    Render::ModelDrawState state = batch.State();
    state.texture = Render::TextureSlot{static_cast<std::uint16_t>(ArrivalRing)};
    state.rawTextureName.reset();
    batch.SetState(state);
    constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
    constexpr Render::Color Black{0.0f, 0.0f, 0.0f, 1.0f};
    for (int segment = 0; segment < RingSegments; ++segment)
    {
        const double current = static_cast<double>(segment);
        const double next = current + 1.0;
        const float left = F32(current * D(RingUvStep));
        const float right = F32(next * D(RingUvStep));
        const auto firstRotation = RingRotation(F32((current * D(RingAngleStep)) + D(angle)));
        const auto nextRotation = RingRotation(F32((next * D(RingAngleStep)) + D(angle)));
        const std::array<Render::ModelVertex, 4> corners{
            Render::ModelVertex{RingPoint(position, RingBottomRadius, 0.0f, firstRotation), glm::vec2(left, 1.0f),
                                White},
            Render::ModelVertex{RingPoint(position, RingBottomRadius, 0.0f, nextRotation), glm::vec2(right, 1.0f),
                                White},
            Render::ModelVertex{RingPoint(position, RingTopRadius, RingHeight, nextRotation), glm::vec2(right, 0.0f),
                                Black},
            Render::ModelVertex{RingPoint(position, RingTopRadius, RingHeight, firstRotation), glm::vec2(left, 0.0f),
                                Black}};
        batch.Triangle({corners[0], corners[1], corners[2]});
        batch.Triangle({corners[0], corners[2], corners[3]});
    }
}

double ArrivalBrightness(std::int32_t life)
{
    return life < FadeLife ? 1.0 - (static_cast<double>(Difference(FadeLife, life)) * D(Fifth)) : 1.0;
}

float ArrivalSide(std::int32_t life)
{
    return F32(static_cast<double>(Difference(FullArrivalLife, life)) * D(ArrivalGrowth));
}

Core::Expected<void, std::string> DrawArrival(Render::ModelBatch& batch, const ModelEffect& effect,
                                              const GroundEffectResources& resources)
{
    SetBlend(batch, Render::ModelBlend::Additive); // 0x52e1c2, antes del ftol del reloj.
    const std::optional<std::int32_t> clock = OriginalFtolLow(D(resources.worldClock));
    if (!clock)
    {
        return Core::Unexpected(std::string("0x52e1cd: el ftol del reloj no da un entero de 64 bits"));
    }
    const float angle = F32(static_cast<double>(*clock % ClockTurn) * D(Tenth));
    double brightness = 1.0;
    float side = 0.0f;
    if (effect.subtype == 2)
    {
        // 0x52e2ba no escribe el lado (ebp-4): conserva basura o el lado de otro efecto.
        if (effect.life <= SecondArrivalHalfLife)
        {
            return Core::Unexpected(std::string("0x52e312: el 0x61f/2 lee un lado local previo sin reconstruir"));
        }
        side = F32(static_cast<double>(Difference(FullArrivalLife, effect.life)) * D(SecondArrivalGrowth));
    }
    else
    {
        DrawArrivalRing(batch, effect.transform.position, angle);
        DrawArrivalRing(batch, effect.transform.position, -angle);
        brightness = ArrivalBrightness(effect.life);
        side = ArrivalSide(effect.life);
    }
    // 0x52e2d5..0x52e303: R se guarda sin sacar el valor extendido usado por G y B.
    const glm::vec3 color(F32(brightness), F32(brightness * D(TwoFifths)), F32(brightness * D(Fifth)));
    return DrawDecal(batch, effect, resources, ArrivalDecal, side, side, color, -effect.transform.angles.z);
}
Core::Expected<void, std::string> DrawFadingDecal(Render::ModelBatch& batch, ModelEffect& effect,
                                                  const GroundEffectResources& resources)
{
    if (effect.subtype != 1)
    {
        return {};
    }
    const float side = effect.transform.scale;
    if (auto drawn =
            DrawDecal(batch, effect, resources, effect.type, side, side, effect.light, -effect.transform.angles.z);
        !drawn.HasValue())
    {
        return drawn;
    }
    // 0x52dccf..0x52dcfe: el dibujo ocurre antes de estas cuatro escrituras persistentes.
    effect.light.x = F32(D(effect.light.x) * D(LightDecay));
    effect.light.y = F32(D(effect.light.y) * D(LightDecay));
    effect.light.z = F32(D(effect.light.z) * D(LightDecay));
    effect.transform.scale = F32(D(effect.transform.scale) + D(SmallGrowth));
    return {};
}

Core::Expected<void, std::string> DrawRandomDecal(Render::ModelBatch& batch, const ModelEffect& effect,
                                                  const GroundEffectResources& resources)
{
    if (effect.subtype == 3)
    {
        return {};
    }
    const int sample = (resources.random.Next() % RandomLightCount) + RandomLightBase;
    const float light = F32(static_cast<double>(sample) * D(Tenth));
    return DrawDecal(batch, effect, resources, effect.type, RandomDecalSide, RandomDecalSide, glm::vec3(light),
                     -effect.transform.angles.z);
}

Core::Expected<void, std::string> DrawGrowingDecal(Render::ModelBatch& batch, ModelEffect& effect,
                                                   const GroundEffectResources& resources)
{
    // 0x52dd7b..0x52dd98: el lado cambia antes de entrar en 0x5fb2f0.
    effect.transform.scale = F32(D(effect.transform.scale) + D(Fifth));
    const float light = F32(static_cast<double>(effect.life) * D(Tenth));
    return DrawDecal(batch, effect, resources, effect.type, effect.transform.scale, effect.transform.scale,
                     glm::vec3(light), -effect.transform.angles.z);
}

Core::Expected<glm::vec3, std::string> ColoredDecalLight(std::int32_t subtype, double brightness)
{
    switch (subtype)
    {
    case 0:
    case 1:
        return glm::vec3(F32(brightness * D(TwoFifths)), F32(brightness * D(ThreeFifths)), F32(brightness));
    case 2:
        return glm::vec3(F32(brightness * D(TwoFifths)), F32(brightness), F32(brightness * D(ThreeFifths)));
    case 3:
        return glm::vec3(F32(brightness), F32(brightness * D(ThreeFifths)), F32(brightness * D(TwoFifths)));
    case 4:
        return glm::vec3(F32(brightness), F32(brightness * D(Half)), F32(brightness * D(Tenth)));
    case 9:
        return glm::vec3(F32(brightness * D(NineTenths)), F32(brightness * D(TwoFifths)), F32(brightness * D(Fifth)));
    case 10:
        return glm::vec3(F32(brightness * D(Tenth)), F32(brightness * D(Half)), F32(brightness));
    default:
        return Core::Unexpected(std::string("0x52dfdc: el 0x61e lee un color local previo sin reconstruir"));
    }
}

Core::Expected<void, std::string> DrawColoredDecal(Render::ModelBatch& batch, const ModelEffect& effect,
                                                   const GroundEffectResources& resources)
{
    if (effect.subtype == 7)
    {
        return Core::Unexpected(std::string("0x52ded0: el 0x61e/7 necesita la posición de su dueño"));
    }
    if (effect.subtype == 6 && effect.life % SpawnInterval == 0)
    {
        return Core::Unexpected(std::string("0x52e04c: falta crear el 0x61e/7 desde el dibujo del 0x61e/6"));
    }
    if (effect.subtype == 8 && effect.life % 2 == 0)
    {
        return Core::Unexpected(std::string("0x52e0a2: falta crear el 0x61e/9 desde el dibujo del 0x61e/8"));
    }
    if (effect.subtype == 5 || effect.subtype == 6 || effect.subtype == 8)
    {
        return {};
    }
    double brightness = ArrivalBrightness(effect.life);
    const bool pulse = effect.subtype == 4 || effect.subtype == 10;
    const float side = pulse || effect.subtype == 9 ? effect.transform.scale : ArrivalSide(effect.life);
    if (pulse && brightness == 1.0)
    {
        brightness = std::sin(static_cast<double>(Difference(PulseLife, effect.life)) * D(SmallGrowth)) + D(Half);
    }
    const auto color = ColoredDecalLight(effect.subtype, brightness);
    if (!color.HasValue())
    {
        return Core::Unexpected(color.Error());
    }
    return DrawDecal(batch, effect, resources, ArrivalDecal, side, side, color.Value(), -effect.transform.angles.z);
}

Core::Expected<void, std::string> DrawMapDecal(Render::ModelBatch& batch, const ModelEffect& effect,
                                               const GroundEffectResources& resources)
{
    constexpr std::uint8_t FirstSpecialMap = 24;
    constexpr std::uint8_t LastSpecialMap = 29;
    constexpr std::uint8_t LastMap = 36;
    const bool specialMap =
        (resources.map >= FirstSpecialMap && resources.map <= LastSpecialMap) || resources.map == LastMap;
    if (specialMap && effect.subtype != 6)
    {
        // 0x52e0f0: 0x6208b0 apaga la escritura de profundidad antes del wrapper437940.
        Render::ModelDrawState state = batch.State();
        state.depthWrite = false;
        batch.ApplyOperation(state, Render::ModelStateOperation::DepthWrite);
        if (resources.kalima)
        {
            const TerrainDecal decal{.slot = effect.type,
                                     .x = effect.transform.position.x,
                                     .y = effect.transform.position.y,
                                     .width = effect.transform.scale,
                                     .height = effect.transform.scale,
                                     .color = effect.light,
                                     .angle = -effect.transform.angles.z,
                                     .alpha = 1,
                                     .lift = 0};
            if (const auto drawn = DrawKalimaSurfaceDecal(batch, *resources.kalima, decal); !drawn.HasValue())
                return drawn;
        }
        // 52e137→620890: lo escribe en1 aun cuando437940 no encontró dueño.
        state = batch.State();
        state.depthWrite = true;
        batch.ApplyOperation(state, Render::ModelStateOperation::DepthWrite);
        return {};
    }
    return DrawDecal(batch, effect, resources, effect.type, effect.transform.scale, effect.transform.scale,
                     effect.light, -effect.transform.angles.z);
}
} // namespace

void ApplyGroundEffectAdditive(Render::ModelBatch& batch)
{
    SetBlend(batch, Render::ModelBlend::Additive);
}

Core::Expected<void, std::string> DrawGroundEffectCase(Render::ModelBatch& batch, ModelEffect& effect,
                                                       const GroundEffectResources& resources)
{
    switch (effect.type)
    {
    case 0x61d:
        return DrawFadingDecal(batch, effect, resources);
    case 0x5de:
        return DrawRandomDecal(batch, effect, resources);
    case 0x5d5:
        return DrawGrowingDecal(batch, effect, resources);
    case 0x61e:
        return DrawColoredDecal(batch, effect, resources);
    case ArrivalEffect:
        return DrawArrival(batch, effect, resources);
    case 0x636:
        SetBlend(batch, Render::ModelBlend::Alpha);
        // +0x17c/+0x180: los dos lados, donde el dragón guarda su posición de partida.
        return DrawDecal(batch, effect, resources, effect.type, effect.start.x, effect.start.y, effect.light, 0.0f);
    case 0x6eb:
        return DrawMapDecal(batch, effect, resources);
    default:
        return {};
    }
}
} // namespace Client::World
