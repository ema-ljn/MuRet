#include "Terrain/GroundMarks.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Terrain/TerrainDecal.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Terrain
{
namespace
{
// The types 0x548fe0 and 0x549090 check.
constexpr std::int32_t ShrinkingType = 7;
constexpr std::int32_t BloodType = 0x5e3;
constexpr std::int32_t SecondFadingType = 0x5e4;
constexpr std::int32_t ThirdFadingType = 0x606;
// 0x548fe7: the life the search for the place to turn off starts from, 0x270f.
constexpr std::int32_t SearchLife = 9999;
// 0x54906e..0x54907f: a new mark's life, rand() % 32 + 50.
constexpr int LifeRandomRange = 32;
constexpr std::int32_t FirstLife = 50;
// 0x5490ef: how much the side of 0x5e3 grows on each step (0x6b89e8).
constexpr float BloodGrowth = 0.004f;
static_assert(std::bit_cast<std::uint32_t>(BloodGrowth) == 0x3B83126F, "la constante de 0x6b89e8");
// 0x5490f9: the red of those that fade (0x3dcccccd).
constexpr float FadingRed = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(FadingRed) == 0x3DCCCCCD, "la constante de 0x5490f9");
// 0x54910b..0x549113: below 50 life, the alpha is life · 0.02 (0x6b8b90).
constexpr std::int32_t FadingLife = 50;
constexpr float AlphaPerLife = 0.02f;
static_assert(std::bit_cast<std::uint32_t>(AlphaPerLife) == 0x3CA3D70A, "la constante de 0x6b8b90");
// 0x5490c5..0x5490cd: how much 7 shrinks on each step (0x6b8a48) and the side below which it turns off (0x6b87b8).
constexpr float ShrinkStep = 0.05f;
static_assert(std::bit_cast<std::uint32_t>(ShrinkStep) == 0x3D4CCCCD, "la constante de 0x6b8a48");
constexpr float SmallestSize = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(SmallestSize) == 0x3DCCCCCD, "la constante de 0x6b87b8");
// 0x549152: with a 3-component texture (+0x28 of the slot), the additive blend.
constexpr std::uint8_t AdditiveComponents = 3;
// 0x549175: how much the decal rises above the terrain (0x40a00000).
constexpr float MarkLift = 5.0f;

using Math::F32;

// 0x620a90 and 0x620a00(1) set the blend with its whole profile (culling, depth mask, alpha test and texture) and do
// not touch the depth test or the bound texture.
Render::ModelDrawState Blended(Render::ModelDrawState state, Render::ModelBlend blend)
{
    state.blend = blend;
    state.cullEnabled.reset();
    state.depthWrite.reset();
    state.alphaTest.reset();
    state.texture2d.reset();
    return state;
}

// The 32-bit dec, which wraps at the minimum.
std::int32_t Decremented(std::int32_t life)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(life) - 1u);
}

bool Fades(std::int32_t type)
{
    return type == BloodType || type == SecondFadingType || type == ThirdFadingType;
}

// 0x5490a1..0x549119: a live mark, in one step.
void StepMark(GroundMark& mark)
{
    mark.life = Decremented(mark.life);
    if (mark.type == ShrinkingType)
    {
        // 0x5490c3..0x5490da: fsub, fst and fcomp: stores the side and compares the register's, unrounded; below 0.1,
        // or NaN, it turns off.
        const double shrunk = static_cast<double>(mark.size) - static_cast<double>(ShrinkStep);
        mark.size = F32(shrunk);
        if (!(shrunk >= static_cast<double>(SmallestSize)))
        {
            mark.live = false;
        }
        return;
    }
    if (!Fades(mark.type))
    {
        return;
    }
    if (mark.type == BloodType)
    {
        mark.size = F32(static_cast<double>(mark.size) + static_cast<double>(BloodGrowth));
    }
    // 0x5490f7..0x549119.
    mark.color = glm::vec3(FadingRed, 0.0f, 0.0f);
    if (mark.life <= 0)
    {
        mark.live = false;
    }
    if (mark.life < FadingLife)
    {
        mark.alpha = F32(static_cast<double>(mark.life) * static_cast<double>(AlphaPerLife));
    }
}
} // namespace

Core::Expected<void, std::string> AddGroundMark(GroundMarkTable& marks, const GroundMarkRequest& request,
                                                Crt::CrtRandom& random)
{
    GroundMark* weakest = nullptr;
    std::int32_t weakestLife = SearchLife;
    for (GroundMark& mark : marks)
    {
        if (!mark.live)
        {
            // 0x549012..0x549059: the mark, with the alpha at 1.
            mark = GroundMark{.live = true,
                              .type = request.type,
                              .size = request.size,
                              .position = request.position,
                              .angle = request.angle,
                              .color = request.color,
                              .alpha = 1.0f,
                              .life = mark.life};
            // 0x549020 and 0x54905c..0x54906c: the life, only with the types that fade.
            if (Fades(request.type))
            {
                mark.life = (random.Next() % LifeRandomRange) + FirstLife;
            }
            return {};
        }
        // 0x548ff6..0x548fff: cmp and jle, signed: it keeps the first one with the least life.
        if (mark.life < weakestLife)
        {
            weakestLife = mark.life;
            weakest = &mark;
        }
    }
    if (weakest == nullptr)
    {
        return Core::Unexpected(std::format("0x54900c: sin lugar libre en las marcas del suelo y sin ninguna vida "
                                            "debajo de 9999, 0x548fe0 escribe en la dirección {:#x}, el tipo",
                                            request.type));
    }
    // 0x54900c: turns that one off and does not place the new one.
    weakest->live = false;
    return {};
}

void StepGroundMarks(GroundMarkTable& marks)
{
    for (GroundMark& mark : marks)
    {
        if (mark.live)
        {
            StepMark(mark);
        }
    }
}

Core::Expected<void, std::string> DrawGroundMarks(Render::ModelBatch& batch, const GroundMarkTable& marks,
                                                  const Data::Terrain::TerrainHeights& heights,
                                                  const SlotComponents& components)
{
    // 0x549135: the additive blend, even if there is none.
    batch.SetState(Blended(batch.State(), Render::ModelBlend::Additive));
    for (const GroundMark& mark : marks)
    {
        if (!mark.live)
        {
            continue;
        }
        // 0x549146..0x54916a: byte +0x28 of the type's slot.
        const std::optional<std::uint8_t> slotComponents = components(mark.type);
        if (!slotComponents)
        {
            return Core::Unexpected(std::format("0x549152: una marca del suelo con el slot {:#x}, que MuRet no subió "
                                                "y del que no sabe los componentes (+0x28)",
                                                mark.type));
        }
        const Render::ModelBlend blend =
            *slotComponents == AdditiveComponents ? Render::ModelBlend::Additive : Render::ModelBlend::Alpha;
        batch.SetState(Blended(batch.State(), blend));
        // 0x54916d..0x549198: 0x5fb2f0 with the side on both sides and the turn negated (fchs).
        const TerrainDecal decal{.slot = mark.type,
                                 .x = mark.position.x,
                                 .y = mark.position.y,
                                 .width = mark.size,
                                 .height = mark.size,
                                 .color = mark.color,
                                 .angle = -mark.angle,
                                 .alpha = mark.alpha,
                                 .lift = MarkLift};
        if (auto drawn = DrawTerrainDecal(batch, heights, decal); !drawn.HasValue())
        {
            return drawn;
        }
    }
    return {};
}
} // namespace Client::Terrain
