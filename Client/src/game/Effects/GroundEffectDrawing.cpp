#include "Effects/GroundEffectDrawing.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/OriginalRotation.h"
#include "Math/RoundedMath.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Effects/ColoredDecals.h"
#include "Math/Ftol.h"
#include "Effects/ModelEffects.h"
#include "MapScripts/Kalima/KalimaSurfaceDecal.h"
#include "Terrain/TerrainDecal.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Terrain
{
namespace
{
// Floats of the c32bbd5f build: 0x6b87b8, 0x6b8854, 0x6b8a48, 0x6b8ae8 and 0x6ba2ac.
using Math::Fifth;
using Math::Tenth;
constexpr float SmallGrowth = 0.05f;
constexpr float ArrivalGrowth = 0.15f;
constexpr float SecondArrivalGrowth = 0.55f;
// 0x6b87b4, 0x6b8a20, 0x6b8a1c, 0x6b882c and 0x6ba26c.
constexpr float TwoFifths = 0.4f;
constexpr float ThreeFifths = 0.6f;
constexpr float NineTenths = 0.9f;
using Math::Half;
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
// Arguments of 0x52e224/0x52e24e and data of 0x53d2c0 (0x6b8858, 0x6ba324, 0x6b8864).
constexpr float RingBottomRadius = 90.0f;
constexpr float RingTopRadius = 130.0f;
constexpr float RingHeight = 200.0f;
constexpr float RingAngleStep = 30.0f;
constexpr float RingUvStep = std::bit_cast<float>(0x3daaaaabu);
constexpr int RingSegments = 12;

using Math::D;
using Math::F32;

// A 32-bit SUB, even at the edges of the signed integer.
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
    // The profile is observable even without vertices. These operations do not bind another texture.
    constexpr std::array Operations{Render::ModelStateOperation::Blend, Render::ModelStateOperation::Cull,
                                    Render::ModelStateOperation::DepthWrite, Render::ModelStateOperation::AlphaTest,
                                    Render::ModelStateOperation::Texture2d};
    for (const Render::ModelStateOperation operation : Operations)
    {
        batch.ApplyOperation(state, operation);
    }
}

Core::Expected<void, std::string> DrawDecal(Render::ModelBatch& batch, const World::ModelEffect& effect,
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

// 0x5fc9c0 stores the same elements of 0x5fc8f0 transposed, with the same roundings.
Math::OriginalMatrix RingRotation(float angle)
{
    const auto rotation = Math::OriginalAngleMatrix(glm::vec3(0.0f, 0.0f, angle));
    Math::OriginalMatrix inverse{};
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

glm::vec3 RingPoint(const glm::vec3& position, float radius, float height, const Math::OriginalMatrix& rotation)
{
    const glm::vec3 rotated = Math::OriginalVectorRotate(glm::vec3(0.0f, radius, height), rotation);
    return {F32(D(rotated.x) + D(position.x)), F32(D(rotated.y) + D(position.y)), F32(D(rotated.z) + D(position.z))};
}

// 0x53d2c0 with the arguments of 0x52e224/0x52e24e: twelve quads, white below and black above. The rings do not depend
// on the life; only the later decal fades.
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

Core::Expected<void, std::string> DrawArrival(Render::ModelBatch& batch, const World::ModelEffect& effect,
                                              const GroundEffectResources& resources)
{
    SetBlend(batch, Render::ModelBlend::Additive); // 0x52e1c2, before the clock's ftol.
    const std::optional<std::int32_t> clock = Math::OriginalFtolLow(D(resources.worldClock));
    if (!clock)
    {
        return Core::Unexpected(std::string("0x52e1cd: el ftol del reloj no da un entero de 64 bits"));
    }
    const float angle = F32(static_cast<double>(*clock % ClockTurn) * D(Tenth));
    double brightness = 1.0;
    float side = 0.0f;
    if (effect.subtype == 2)
    {
        // 0x52e2ba does not write the side (ebp-4): it keeps garbage or another effect's side.
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
    // 0x52e2d5..0x52e303: R is stored without popping the extended value G and B use.
    const glm::vec3 color(F32(brightness), F32(brightness * D(TwoFifths)), F32(brightness * D(Fifth)));
    return DrawDecal(batch, effect, resources, ArrivalDecal, side, side, color, -effect.transform.angles.z);
}
Core::Expected<void, std::string> DrawFadingDecal(Render::ModelBatch& batch, World::ModelEffect& effect,
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
    // 0x52dccf..0x52dcfe: the drawing happens before these four persistent writes.
    effect.light.x = F32(D(effect.light.x) * D(LightDecay));
    effect.light.y = F32(D(effect.light.y) * D(LightDecay));
    effect.light.z = F32(D(effect.light.z) * D(LightDecay));
    effect.transform.scale = F32(D(effect.transform.scale) + D(SmallGrowth));
    return {};
}

Core::Expected<void, std::string> DrawRandomDecal(Render::ModelBatch& batch, const World::ModelEffect& effect,
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

Core::Expected<void, std::string> DrawGrowingDecal(Render::ModelBatch& batch, World::ModelEffect& effect,
                                                   const GroundEffectResources& resources)
{
    // 0x52dd7b..0x52dd98: the side changes before entering 0x5fb2f0.
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
    case 7:
    case 9:
        return glm::vec3(F32(brightness * D(NineTenths)), F32(brightness * D(TwoFifths)), F32(brightness * D(Fifth)));
    case 10:
        return glm::vec3(F32(brightness * D(Tenth)), F32(brightness * D(Half)), F32(brightness));
    default:
        return Core::Unexpected(std::string("0x52dfdc: el 0x61e lee un color local previo sin reconstruir"));
    }
}

// 0x52e010..0x52e0a2: /6 every 25 lives and /8 on even lives shed a 0x61e/7 or /9 into the pass's table, with their
// position, angles, light and owner (0x518cf0 with key −1; /7 draws its rand() % 360).
void SpawnColoredDecal(const World::ModelEffect& effect, const GroundEffectResources& resources, std::int32_t subtype)
{
    const Kernel::ModelEffectArguments child{.type = ArrivalDecal,
                                             .position = effect.transform.position,
                                             .angles = effect.transform.angles,
                                             .light = effect.light,
                                             .subtype = subtype,
                                             .owner = effect.ownerContext};
    static_cast<void>(CreateColoredDecal(resources.table, child, resources.random));
}

Core::Expected<void, std::string> DrawColoredDecal(Render::ModelBatch& batch, World::ModelEffect& effect,
                                                   const GroundEffectResources& resources)
{
    // 0x52dddc..0x52de2a: light 1, that of the last five lives and, in /7 above 30, (40 − life) · 0.1f.
    constexpr std::int32_t ChildFadeInLife = 30, ChildFullLife = 40;
    double brightness = ArrivalBrightness(effect.life);
    if (effect.life >= FadeLife && effect.subtype == BindingChildSubtype && effect.life > ChildFadeInLife)
        brightness = static_cast<double>(Difference(ChildFullLife, effect.life)) * D(Tenth);
    // 0x52de30..0x52de96: the side. /4 and /0xa, the scale (and, with the light at 1, the pulse); /7, life · 0.07f; /9,
    // the scale; the rest, (20 − life) · 0.15f (/6 computes its own and is not drawn).
    constexpr float ChildGrowth = std::bit_cast<float>(0x3d8f5c29u); // 0x6ba190, 0.07f
    const bool pulse = effect.subtype == PulsingDecalSubtype || effect.subtype == CoolPulsingDecalSubtype;
    float side = ArrivalSide(effect.life);
    if (pulse || effect.subtype == GrowingDecalSubtype)
        side = effect.transform.scale;
    else if (effect.subtype == BindingChildSubtype)
        side = F32(static_cast<double>(effect.life) * D(ChildGrowth));
    if (pulse && brightness == 1.0)
    {
        brightness =
            Math::RoundedSin(static_cast<double>(Difference(PulseLife, effect.life)) * D(SmallGrowth)) + D(Half);
    }
    if (effect.subtype == BindingChildSubtype)
    {
        // 0x52ded0..0x52df00: the owner's position (+0x108 → +0x10), read on each drawing, and the z angle 10 more.
        constexpr double ChildTurn = 10.0; // 0x6b87a8
        const auto owner = resources.ownerPosition ? resources.ownerPosition(effect.ownerContext) : std::nullopt;
        if (!owner.has_value())
            return Core::Unexpected(std::string("0x52ded0: el 0x61e/7 lee la posición de un dueño que no es un "
                                                "personaje"));
        effect.transform.position = *owner;
        effect.transform.angles.z = F32(D(effect.transform.angles.z) + ChildTurn);
    }
    if (effect.subtype == 5 || effect.subtype == BindingDecalSubtype || effect.subtype == SpawningDecalSubtype)
    {
        if (effect.subtype == BindingDecalSubtype && effect.life % SpawnInterval == 0)
            SpawnColoredDecal(effect, resources, BindingChildSubtype);
        else if (effect.subtype == SpawningDecalSubtype && effect.life % 2 == 0)
            SpawnColoredDecal(effect, resources, GrowingDecalSubtype);
        return {};
    }
    const auto color = ColoredDecalLight(effect.subtype, brightness);
    if (!color.HasValue())
    {
        return Core::Unexpected(color.Error());
    }
    return DrawDecal(batch, effect, resources, ArrivalDecal, side, side, color.Value(), -effect.transform.angles.z);
}

Core::Expected<void, std::string> DrawMapDecal(Render::ModelBatch& batch, const World::ModelEffect& effect,
                                               const GroundEffectResources& resources)
{
    constexpr std::uint8_t FirstSpecialMap = 24;
    constexpr std::uint8_t LastSpecialMap = 29;
    constexpr std::uint8_t LastMap = 36;
    const bool specialMap =
        (resources.map >= FirstSpecialMap && resources.map <= LastSpecialMap) || resources.map == LastMap;
    if (specialMap && effect.subtype != 6)
    {
        // 0x52e0f0: 0x6208b0 turns off depth writes before the 0x437940 wrapper.
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
            if (const auto drawn = World::DrawKalimaSurfaceDecal(batch, *resources.kalima, decal); !drawn.HasValue())
                return drawn;
        }
        // 0x52e137 → 0x620890: writes it as 1 even when 0x437940 found no owner.
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

Core::Expected<void, std::string> DrawGroundEffectCase(Render::ModelBatch& batch, World::ModelEffect& effect,
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
    case World::ArrivalEffect:
        return DrawArrival(batch, effect, resources);
    case 0x636:
        SetBlend(batch, Render::ModelBlend::Alpha);
        // +0x17c/+0x180: the two sides, where the dragon keeps its start position.
        return DrawDecal(batch, effect, resources, effect.type, effect.start.x, effect.start.y, effect.light, 0.0f);
    case 0x6eb:
        return DrawMapDecal(batch, effect, resources);
    default:
        return {};
    }
}
} // namespace Client::Terrain
