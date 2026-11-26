#include "Scenes/HighEffectDrawing.h"

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
#include "Scenes/CharacterDrawing.h"
#include "World/ModelEffects.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Cotejado con el PE c32bbd5f: 0x6002e0 (0x601e68..0x602044) y 0x438fe0 (0x43909e..0x439214).
namespace Client::Scenes
{
namespace
{
using Math::D;
using Math::F32;

constexpr std::int16_t WaveType = 0x114, RingType = 0x11b, KundunSpiritType = 0x125, GuildMarkType = 0x140;
constexpr std::uint32_t ChromeFlags = 0x44; // 0x602039 y 0x60235c
constexpr std::int16_t NoHiddenMesh = -1;   // el `push 0xffffffff` de 0x601fc7 y 0x601ffe
constexpr float VisibleAlpha = 0.01f;       // 0x6b88d4, el de 0x5ffdf0
// Los huesos del buffer global de la pose (0x68b181c, 0x30 bytes cada uno).
constexpr std::size_t SecondBone = 2; // 0x68b187c
constexpr std::size_t ThirdBone = 3;  // 0x68b18ac
constexpr std::size_t FourthBone = 4; // 0x68b18dc
constexpr std::uint16_t GlowSprite = 0x5ac, FlareSprite = 0x5fd, SpiritSprite = 0x5ca;

// 0x4ec110(hueso, (0, 0, 0), punto, 0): sin trasladar, el punto es la traslación del hueso que 0x5ffdf0 dejó en el
// mundo. Un hueso que el buffer todavía no tiene es la BSS en 0, como en la cola del misil de Crywolf.
glm::vec3 BonePoint(std::span<const Math::BoneTransform> bones, std::size_t bone)
{
    const auto matrix = bone < bones.size() ? bones[bone] : Math::BoneTransform(0.0f);
    return Math::OriginalVectorTransform(glm::vec3(0.0f), Math::OriginalMatrixOf(matrix));
}

// Las mallas posadas de `object`: las que publicó la pose del caller o, sin ellas, las que posa acá como 0x5ffdf0 con
// el segundo argumento en 0 (0x52da99 → 0x602e90(efecto, 0, …): WorldScratchOf, la normal girada por R · s); ninguna
// con el alpha por debajo de 0,01 o sin orden (0x5ffe2a..0x5ffe3d), o sin el modelo, su esqueleto o la pose.
std::span<const Render::Models::SkinnedMesh> MeshesOf(const Render::Models::IModelLibrary& models,
                                                      const DrawnObject& object,
                                                      std::vector<Render::Models::SkinnedMesh>& posed)
{
    if (!object.retainedMeshes.empty())
        return object.retainedMeshes;
    if (!(object.alpha >= VisibleAlpha))
        return {};
    posed = WorldScratchOf(models, object);
    return posed;
}

Core::Expected<std::span<const Math::BoneTransform>, std::string> BonesOf(const HighEffectDrawServices& services,
                                                                          const char* address)
{
    if (!services.posedBones)
        return Core::Unexpected(std::string(address) + ": faltan los huesos de la pose (0x68b181c)");
    return services.posedBones();
}

// 0x601e68..0x601faf: el 0x11b.
Core::Expected<bool, std::string> DrawTheRing(Render::ModelBatch& batch, const Render::Models::IModelLibrary& models,
                                              World::ModelEffect& effect, DrawnObject& object,
                                              Core::Duration frameClock, const HighEffectDrawServices& services)
{
    // 0x601e72..0x601e7c: (edi = 1, 0x3f19999a, 0x3e99999a).
    object.color = {1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e99999au)};
    DrawObject(batch, models, object, frameClock); // 0x601ea4
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
    // 0x601f32..0x601fa7: luz 0,5, escala 0,5f y el giro rand() % 360 (fild y fstp), uno por punto.
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

// 0x43909e..0x439214 (0x438fe0): el 0x125.
Core::Expected<bool, std::string> DrawTheKundunSpirit(Render::ModelBatch& batch,
                                                      const Render::Models::IModelLibrary& models, DrawnObject& object,
                                                      Core::Duration frameClock, const HighEffectDrawServices& services)
{
    // 0x4390ab..0x4390b8: el color en 0,3 (0x3e99999a).
    object.color = glm::vec3(std::bit_cast<float>(0x3e99999au));
    DrawObject(batch, models, object, frameClock); // 0x4390da
    if (!services.worldClock)
        return Core::Unexpected(std::string("0x4390df: falta el reloj del mundo (0x5767b88) del 0x125"));
    // 0x4390df..0x43911d: FSIN(T · 0,003f) · 0,2f + 0,8f en la x87, guardado en float.
    constexpr double Rate = static_cast<double>(std::bit_cast<float>(0x3b449ba6u)); // 0x6b89cc, 0,003f
    constexpr float Swing = std::bit_cast<float>(0x3e4ccccdu);                      // 0x6b8854, 0,2f
    constexpr double Base = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38, 0,8f
    constexpr double Fifth = Math::FifthAsDouble;                                   // 0x6b8854, 0,2f
    constexpr double Tenth = Math::TenthAsDouble;                                   // 0x6b87b8, 0,1f
    const auto pulse = Math::Detail::SineProductPC53(D(*services.worldClock) * Rate, Swing);
    if (!pulse)
        return Core::Unexpected(std::string("0x4390f4: la luz del 0x125 no cabe en el perfil PC53"));
    const float luminosity = F32(*pulse + Base);
    // 0x439120..0x439143: (l, l · 0,2f, l · 0,1f), cada uno con su fst.
    const glm::vec3 light{luminosity, F32(D(luminosity) * Fifth), F32(D(luminosity) * Tenth)};
    const auto bones = BonesOf(services, "0x439146");
    if (!bones.HasValue())
        return Core::Unexpected(bones.Error());
    if (services.sprites == nullptr)
        return Core::Unexpected(std::string("0x439162: faltan los sprites del 0x125"));
    constexpr float SpiritScale = std::bit_cast<float>(0x3e4ccccdu); // 0x439157, 0,2f
    constexpr float FlareScale = 0.5f;                               // 0x439182
    for (const std::size_t bone : {ThirdBone, FourthBone})
    {
        const glm::vec3 point = BonePoint(bones.Value(), bone);
        static_cast<void>(services.sprites->Create(SpiritSprite, point, SpiritScale, light, 0.0f, 0));
        static_cast<void>(services.sprites->Create(FlareSprite, point, FlareScale, light, 0.0f, 0));
    }
    return true;
}

// 0x602005..0x602044: el 0x140.
Core::Expected<bool, std::string> DrawTheGuildMark(Render::ModelBatch& batch,
                                                   const Render::Models::IModelLibrary& models, DrawnObject& object,
                                                   Core::Duration frameClock)
{
    // 0x60200b..0x602019: (0x3ecccccd, 0x3f19999a, edi = 1).
    object.color = {std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3f19999au), 1.0f};
    std::vector<Render::Models::SkinnedMesh> posed;
    const auto meshes = MeshesOf(models, object, posed);
    if (meshes.empty())
        return true;
    // 0x60203d y 0x602360: dos RenderBody(0x44) con la textura en −1, el cromado de 0x4ecc30 (Chrome01); el segundo,
    // con el color que dejó el primero.
    for (int pass = 0; pass < 2; ++pass)
        if (auto drawn = DrawItemMeshPass(batch, models, object, frameClock, meshes, -1, ChromeFlags, std::nullopt,
                                          &object.color);
            !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
    return true;
}
} // namespace

bool HasHighEffectBody(const World::ModelEffect& effect)
{
    if (effect.type == WaveType)
        return effect.subtype == 3 || effect.subtype == 4 || effect.subtype == 5;
    return effect.type == RingType || effect.type == KundunSpiritType || effect.type == GuildMarkType;
}

Core::Expected<bool, std::string> DrawTheHighEffectBody(Render::ModelBatch& batch,
                                                        const Render::Models::IModelLibrary& models,
                                                        World::ModelEffect& effect, DrawnObject& object,
                                                        Core::Duration frameClock,
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
    // 0x601fc2..0x601ffe y 0x60221c: el 0x114/3, /4 y /5, RenderBody(2, …, −1, 0x6ef) con el color que dejó 0x5ffdf0.
    constexpr Render::TextureSlot PinkWave{0x6ef}; // Skill\pinkWave.jpg (0x61dd71)
    object.state.hiddenMesh = NoHiddenMesh;
    DrawObjectWithTexture(batch, models, object, frameClock, PinkWave);
    return true;
}
} // namespace Client::Scenes
