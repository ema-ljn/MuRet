#include "Scenes/ObjectFrameSprites.h"

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/Ftol.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t GlowTexture = 0x5ac;
constexpr std::uint16_t MagicGroundTexture = 0x61e;
constexpr std::uint16_t LightningTexture = 0x5d5;
// 0x6b88d4 (0,01f), 0x6b87f8 (5), 0x6b8a20 (0,6f), 0x6b89dc (0,3f), 0x6b87b8 (0,1f), 0x6b8854 (0,2f),
// 0x6b87b4 (0,4f), 0x6b8a38 (0,8f) y 0x6b88f0 (260).
constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr double GlowSize = 5.0;
constexpr double EmberRed = static_cast<double>(std::bit_cast<float>(0x3f19999au));
constexpr double EmberGreen = static_cast<double>(std::bit_cast<float>(0x3e99999au));
constexpr double EmberBlue = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr double MagicGreen = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
constexpr double LightningRed = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));
constexpr double LightningGreen = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
constexpr double Lift = 260.0;
constexpr double TurnPerMs = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr std::int32_t FullTurn = 360;
// 0x3e99999a, 0x3fc00000 y 0x40200000.
constexpr float SmallSprite = std::bit_cast<float>(0x3e99999au);
constexpr float LargeSprite = 1.5f;
constexpr float BeamSprite = 2.5f;
// El nivel del try de 0x6002e0 (+0x60, en −1): leído como float, NaN; 0x5fccc0 lo propaga a los tres ejes.
constexpr float StackNaN = std::bit_cast<float>(0xffffffffu);

float F32(double value)
{
    return static_cast<float>(value);
}

// 0x602ef4..0x602f18: L, guardada en float.
float FrameLight(int commonRandom)
{
    constexpr int LightSpan = 30, LightBase = 70;
    return F32(static_cast<double>(commonRandom % LightSpan + LightBase) * Hundredth);
}

glm::vec3 EmberLight(float light)
{
    const auto value = static_cast<double>(light);
    return {F32(value * EmberRed), F32(value * EmberGreen), F32(value * EmberBlue)};
}

// La salida de 0x4ec110 con el desplazamiento de [ebp − 0x28] de 0x602ed0, que nadie escribió: con el objeto
// posado, 0x602e90 llamó a 0x6002e0 en el mismo lugar de la pila y ahí quedaron su [ebp − 8] (0x6b628b), su
// nivel del try (−1) y su ebp guardado. La y es NaN, y la salida también.
Core::Expected<glm::vec3, std::string> StackPoint(bool posed, std::string_view address)
{
    if (!posed)
        return Core::Unexpected(std::format("{}: el desplazamiento de 0x4ec110 es lo que dejó 0x5ffdf0 en la pila "
                                            "(el objeto no se posó), y no está leído",
                                            address));
    return glm::vec3(StackNaN);
}

Core::Expected<void, std::string> CreateSprite(const GameObjectPass& pass, std::uint16_t texture,
                                               const glm::vec3& position, float scale, const glm::vec3& light,
                                               float rotation, std::string_view address)
{
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de sprites (0x5491e0)", address));
    // 0x5491e0(textura, punto, escala, luz, objeto, giro, 0): MuRet no guarda el dueño del sprite (Q-C303).
    static_cast<void>(pass.sprites->Create(texture, position, scale, light, rotation, 0));
    return {};
}

// 0x6037bb/0x6038ba: ftol de reloj · 0,1f desde la pila, su resto firmado de 360 y fild a float.
float ClockTurn(float worldClock)
{
    return static_cast<float>(World::FtolLow(static_cast<double>(worldClock) * TurnPerMs) % FullTurn);
}

// 0x6038ed..0x6039c3: los pares de giro r y −r en los huesos 15, 19 y 21.
Core::Expected<void, std::string> BoneSprites(const GameObjectPass& pass, std::uint16_t texture, const glm::vec3& light)
{
    struct BonePair
    {
        std::size_t bone;
        float scale;
    };
    constexpr std::array<BonePair, 3> Pairs{{{15, SmallSprite}, {19, SmallSprite}, {21, LargeSprite}}};
    const float turn = ClockTurn(pass.worldClock);
    for (const BonePair& pair : Pairs)
    {
        const auto point = ObjectBonePoint(pass, pair.bone, glm::vec3(0.0f));
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        for (const float rotation : {turn, -turn})
        {
            if (auto created = CreateSprite(pass, texture, point.Value(), pair.scale, light, rotation, "0x603907");
                !created.HasValue())
                return created;
        }
    }
    return {};
}
} // namespace

Core::Expected<glm::vec3, std::string> ObjectBonePoint(const GameObjectPass& pass, std::size_t bone,
                                                       const glm::vec3& offset)
{
    if (pass.retainedPose == nullptr)
    {
        return Core::Unexpected(std::format("0x4ec110: falta la pose global del hueso {} de los objetos del {}", bone,
                                            static_cast<int>(pass.map)));
    }
    // La .bss nativa empieza en cero; un hueso aún no publicado conserva esa matriz.
    if (bone >= pass.retainedPose->bones.size())
    {
        return Render::Models::OriginalVectorTransform(offset, Render::Models::OriginalMatrix{});
    }
    // Translate=0: el hueso del buffer ya está escalado y trasladado al mundo.
    return Render::Models::OriginalVectorTransform(offset,
                                                   Render::Models::OriginalMatrixOf(pass.retainedPose->bones[bone]));
}

Core::Expected<void, std::string> StepMerchantAnimalFrame(const World::WorldObject& object, int commonRandom,
                                                          const GameObjectPass& pass, bool posed)
{
    static_cast<void>(object);
    const float light = FrameLight(commonRandom);
    const float scale = F32(static_cast<double>(light) * GlowSize);
    // 0x60318a y 0x6031be: los huesos 48 y 57, con la misma pila.
    for (int sprite = 0; sprite < 2; ++sprite)
    {
        const auto point = StackPoint(posed, "0x60318a");
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        if (auto created = CreateSprite(pass, GlowTexture, point.Value(), scale, EmberLight(light), 0.0f, "0x6031a5");
            !created.HasValue())
            return created;
    }
    return {};
}

Core::Expected<void, std::string> StepLostTowerObjectFrame(const World::WorldObject& object, int commonRandom,
                                                           const GameObjectPass& pass, bool posed)
{
    const float light = FrameLight(commonRandom);
    const auto value = static_cast<double>(light);
    if (object.type == 0x13)
    {
        if (auto drawn = BoneSprites(pass, MagicGroundTexture, glm::vec3(light, F32(value * MagicGreen), 0.0f));
            !drawn.HasValue())
            return drawn;
    }
    else if (object.type == 0x14)
    {
        if (auto drawn = BoneSprites(pass, LightningTexture,
                                     glm::vec3(F32(value * LightningRed), F32(value * LightningGreen), light));
            !drawn.HasValue())
            return drawn;
    }
    else if (object.type == 0x28)
    {
        // 0x6037bb..0x603864: sobre el objeto, la z 260 más arriba guardada en float.
        const float turn = ClockTurn(pass.worldClock);
        const glm::vec3 point{object.transform.position.x, object.transform.position.y,
                              F32(static_cast<double>(object.transform.position.z) + Lift)};
        for (const float rotation : {turn, -turn})
        {
            if (auto created =
                    CreateSprite(pass, LightningTexture, point, BeamSprite, glm::vec3(light), rotation, "0x60383e");
                !created.HasValue())
                return created;
        }
    }
    return StepArenaObjectFrame(object, commonRandom, pass, posed);
}

Core::Expected<void, std::string> StepArenaObjectFrame(const World::WorldObject& object, int commonRandom,
                                                       const GameObjectPass& pass, bool posed)
{
    constexpr std::int16_t GlowingObject = 9;
    if (object.type != GlowingObject)
        return {};
    const float light = FrameLight(commonRandom);
    // 0x603a0b: el hueso 1 (0x68b184c), con la misma pila sin escribir.
    const auto point = StackPoint(posed, "0x603a0b");
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    return CreateSprite(pass, GlowTexture, point.Value(), F32(static_cast<double>(light) * GlowSize), EmberLight(light),
                        0.0f, "0x603a2f");
}
} // namespace Client::Scenes
