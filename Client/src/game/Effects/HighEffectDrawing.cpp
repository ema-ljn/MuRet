#include "Effects/HighEffectDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skinning.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Effects/ModelEffects.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Checked against the PE c32bbd5f: 0x6002e0 (0x601e68..0x602044) and 0x438fe0 (0x43909e..0x439214).
namespace Client::Effects
{
namespace
{
using Math::D;
using Math::F32;

constexpr std::int16_t WaveType = 0x114, RingType = 0x11b, KundunSpiritType = 0x125, GuildMarkType = 0x140;
constexpr std::uint32_t ChromeFlags = 0x44; // 0x602039 and 0x60235c
constexpr std::int16_t NoHiddenMesh = -1;   // the `push 0xffffffff` of 0x601fc7 and 0x601ffe
constexpr float VisibleAlpha = 0.01f;       // 0x6b88d4, the one of 0x5ffdf0
// The bones of the pose's global buffer (0x68b181c, 0x30 bytes each).
constexpr std::size_t SecondBone = 2; // 0x68b187c
constexpr std::size_t ThirdBone = 3;  // 0x68b18ac
constexpr std::size_t FourthBone = 4; // 0x68b18dc
constexpr std::uint16_t GlowSprite = 0x5ac, FlareSprite = 0x5fd, SpiritSprite = 0x5ca;

// 0x4ec110(bone, (0, 0, 0), point, 0): without translating, the point is the translation of the bone 0x5ffdf0 left in
// the world. A bone the buffer does not have yet is the BSS at 0, as in the Crywolf missile's tail.
glm::vec3 BonePoint(std::span<const Math::BoneTransform> bones, std::size_t bone)
{
    const auto matrix = bone < bones.size() ? bones[bone] : Math::BoneTransform(0.0f);
    return Math::OriginalVectorTransform(glm::vec3(0.0f), Math::OriginalMatrixOf(matrix));
}

// The posed meshes of `object`: those the caller's pose published or, without them, those it poses here like 0x5ffdf0
// with the second argument at 0 (0x52da99 → 0x602e90(effect, 0, …): WorldScratchOf, the normal turned by R · s); none
// with the alpha under 0.01 or unordered (0x5ffe2a..0x5ffe3d), or without the model, its skeleton or the pose.
std::span<const Models::SkinnedMesh> MeshesOf(const Models::IModelLibrary& models,
                                              const Actors::Drawing::DrawnObject& object,
                                              std::vector<Models::SkinnedMesh>& posed)
{
    if (!object.retainedMeshes.empty())
        return object.retainedMeshes;
    if (!(object.alpha >= VisibleAlpha))
        return {};
    posed = Actors::Drawing::WorldScratchOf(models, object);
    return posed;
}

Core::Expected<std::span<const Math::BoneTransform>, std::string> BonesOf(const HighEffectDrawServices& services,
                                                                          const char* address)
{
    if (!services.posedBones)
        return Core::Unexpected(std::string(address) + ": faltan los huesos de la pose (0x68b181c)");
    return services.posedBones();
}

// 0x601e68..0x601faf: the 0x11b.
Core::Expected<bool, std::string> DrawTheRing(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                              Effects::ModelEffect& effect, Actors::Drawing::DrawnObject& object,
                                              Core::Duration frameClock, const HighEffectDrawServices& services)
{
    // 0x601e72..0x601e7c: (edi = 1, 0x3f19999a, 0x3e99999a).
    object.color = {1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e99999au)};
    Actors::Drawing::DrawObject(batch, models, object, frameClock); // 0x601ea4
    const auto bones = BonesOf(services, "0x601ecf");
    if (!bones.HasValue())
        return Core::Unexpected(bones.Error());
    effect.point40 = BonePoint(bones.Value(), SecondBone); // 0x601ecf
    effect.point4c = BonePoint(bones.Value(), ThirdBone);  // 0x601ee5
    if (services.sprites == nullptr)
        return Core::Unexpected(std::string("0x601f12: faltan los sprites del 0x11b"));
    // 0x601efd..0x601f2a: luz (1, 0, 0), escala edi = 1, giro 0.
    constexpr glm::vec3 Red{1.0f, 0.0f, 0.0f};
    static_cast<void>(services.sprites->Create(GlowSprite, effect.point40, 1.0f, Red, 0.0f, 0));
    static_cast<void>(services.sprites->Create(GlowSprite, effect.point4c, 1.0f, Red, 0.0f, 0));
    if (services.random == nullptr)
        return Core::Unexpected(std::string("0x601f49: falta el CRT del 0x11b"));
    // 0x601f32..0x601fa7: light 0.5, scale 0.5f and the turn rand() % 360 (fild and fstp), one per point.
    constexpr glm::vec3 Gray{0.5f, 0.5f, 0.5f};
    constexpr float FlareScale = 0.5f;
    constexpr int FullTurn = Math::FullTurn; // 0x168
    for (const glm::vec3& point : {effect.point40, effect.point4c})
    {
        const auto turn = static_cast<float>(services.random->Next() % FullTurn);
        static_cast<void>(services.sprites->Create(FlareSprite, point, FlareScale, Gray, turn, 0));
    }
    return true;
}

// 0x43909e..0x439214 (0x438fe0): the 0x125.
Core::Expected<bool, std::string> DrawTheKundunSpirit(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                      Actors::Drawing::DrawnObject& object, Core::Duration frameClock,
                                                      const HighEffectDrawServices& services)
{
    // 0x4390ab..0x4390b8: the color at 0.3 (0x3e99999a).
    object.color = glm::vec3(std::bit_cast<float>(0x3e99999au));
    Actors::Drawing::DrawObject(batch, models, object, frameClock); // 0x4390da
    if (!services.worldClock)
        return Core::Unexpected(std::string("0x4390df: falta el reloj del mundo (0x5767b88) del 0x125"));
    // 0x4390df..0x43911d: FSIN(T · 0.003f) · 0.2f + 0.8f on the x87, stored as float.
    constexpr double Rate = static_cast<double>(std::bit_cast<float>(0x3b449ba6u)); // 0x6b89cc, 0.003f
    constexpr float Swing = std::bit_cast<float>(0x3e4ccccdu);                      // 0x6b8854, 0.2f
    constexpr double Base = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38, 0.8f
    constexpr double Fifth = Math::FifthAsDouble;                                   // 0x6b8854, 0.2f
    constexpr double Tenth = Math::TenthAsDouble;                                   // 0x6b87b8, 0.1f
    const auto pulse = Math::Detail::SineProductPC53(D(*services.worldClock) * Rate, Swing);
    if (!pulse)
        return Core::Unexpected(std::string("0x4390f4: la luz del 0x125 no cabe en el perfil PC53"));
    const float luminosity = F32(*pulse + Base);
    // 0x439120..0x439143: (l, l · 0.2f, l · 0.1f), each with its fst.
    const glm::vec3 light{luminosity, F32(D(luminosity) * Fifth), F32(D(luminosity) * Tenth)};
    const auto bones = BonesOf(services, "0x439146");
    if (!bones.HasValue())
        return Core::Unexpected(bones.Error());
    if (services.sprites == nullptr)
        return Core::Unexpected(std::string("0x439162: faltan los sprites del 0x125"));
    constexpr float SpiritScale = std::bit_cast<float>(0x3e4ccccdu); // 0x439157, 0.2f
    constexpr float FlareScale = 0.5f;                               // 0x439182
    for (const std::size_t bone : {ThirdBone, FourthBone})
    {
        const glm::vec3 point = BonePoint(bones.Value(), bone);
        static_cast<void>(services.sprites->Create(SpiritSprite, point, SpiritScale, light, 0.0f, 0));
        static_cast<void>(services.sprites->Create(FlareSprite, point, FlareScale, light, 0.0f, 0));
    }
    return true;
}

// 0x602005..0x602044: the 0x140.
Core::Expected<bool, std::string> DrawTheGuildMark(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                   Actors::Drawing::DrawnObject& object, Core::Duration frameClock)
{
    // 0x60200b..0x602019: (0x3ecccccd, 0x3f19999a, edi = 1).
    object.color = {std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3f19999au), 1.0f};
    std::vector<Models::SkinnedMesh> posed;
    const auto meshes = MeshesOf(models, object, posed);
    if (meshes.empty())
        return true;
    // 0x60203d and 0x602360: two RenderBody(0x44) with the texture at −1, 0x4ecc30's chrome (Chrome01); the second with
    // the color the first left.
    for (int pass = 0; pass < 2; ++pass)
        if (auto drawn = Actors::Drawing::DrawItemMeshPass(batch, models, object, frameClock, meshes, -1, ChromeFlags,
                                                           std::nullopt, &object.color);
            !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
    return true;
}
} // namespace

bool HasHighEffectBody(const Effects::ModelEffect& effect)
{
    if (effect.type == WaveType)
        return effect.subtype == 3 || effect.subtype == 4 || effect.subtype == 5;
    return effect.type == RingType || effect.type == KundunSpiritType || effect.type == GuildMarkType;
}

Core::Expected<bool, std::string> DrawTheHighEffectBody(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                        Effects::ModelEffect& effect,
                                                        Actors::Drawing::DrawnObject& object, Core::Duration frameClock,
                                                        const HighEffectDrawServices& services)
{
    if (!HasHighEffectBody(effect))
        return false;
    switch (effect.type)
    {
    case RingType:
        return DrawTheRing(batch, models, effect, object, frameClock, services);
    case KundunSpiritType:
        return DrawTheKundunSpirit(batch, models, object, frameClock, services);
    case GuildMarkType:
        return DrawTheGuildMark(batch, models, object, frameClock);
    default:
        break;
    }
    // 0x601fc2..0x601ffe and 0x60221c: the 0x114/3, /4 and /5, RenderBody(2, …, −1, 0x6ef) with the color 0x5ffdf0
    // left.
    constexpr Render::TextureSlot PinkWave{0x6ef}; // Skill\pinkWave.jpg (0x61dd71)
    object.state.hiddenMesh = NoHiddenMesh;
    Actors::Drawing::DrawObjectWithTexture(batch, models, object, frameClock, PinkWave);
    return true;
}
} // namespace Client::Effects
