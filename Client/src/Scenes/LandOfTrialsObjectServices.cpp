#include "Scenes/LandOfTrialsObjectServices.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/ObjectFrameSprites.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/KalimaParticleKernels.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TrialsParticleKernels.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr int EmitChance = 3;
// 0x68b18ac: el hueso 3 del buffer global de la pose (0x68b181c + 3 · 0x30).
constexpr std::size_t SpriteBone = 3;
constexpr std::uint16_t FlareTexture = 0x5ac; // Effect\flare01.jpg.
constexpr std::uint16_t ShinyTexture = 0x5fd; // Effect\Shiny02.jpg.
constexpr float SecondFlareScale = 0.5f;      // 0x4402f1.
constexpr float ShinyScale = std::bit_cast<float>(0x3f99999au);
constexpr float ShinyGrey = std::bit_cast<float>(0x3e99999au);
constexpr int FullTurn = 360;

Core::Expected<void, std::string> Emit(const GameObjectPass& pass, const World::WorldObject& object, std::int32_t type,
                                       std::int32_t subtype, std::string_view address)
{
    if (pass.particles == nullptr)
    {
        return Core::Unexpected(
            std::format("{}: falta el pool de partículas 0x743a198 para {:#x}/{}", address, type, subtype));
    }
    const auto created = pass.particles->CreateOwnedChecked(type, object.transform.position, object.transform.angles,
                                                            glm::vec3(1.0f), subtype, object.transform.scale,
                                                            pass.worldClock, *pass.random, pass.scene, {});
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    }
    return {};
}

// 0x440263..0x440346.
Core::Expected<void, std::string> Sprites(const GameObjectPass& pass)
{
    // 0x4ec110(0x68b18ac, (0, −10, 0), salida, 0).
    const auto point = ObjectBonePoint(pass, SpriteBone, glm::vec3(0.0f, -10.0f, 0.0f));
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    // 0x44028f..0x4402d7: el reloj por 0,002f (0x6b89bc), FSIN, por 0,35f (0x6b8bc4), más 0,65f (0x6b8bc0),
    // por 1,2f (0x6b8bb0) y más 0,3f (0x6b89dc), en la x87 hasta el fstp de la escala.
    constexpr double Rate = static_cast<double>(std::bit_cast<float>(0x3b03126fu));
    constexpr float Swing = std::bit_cast<float>(0x3eb33333u);
    constexpr double Base = static_cast<double>(std::bit_cast<float>(0x3f266666u));
    constexpr double Stretch = static_cast<double>(std::bit_cast<float>(0x3f99999au));
    constexpr double Lift = static_cast<double>(std::bit_cast<float>(0x3e99999au));
    const auto pulse = Render::Models::Detail::SineProductPC53(static_cast<double>(pass.worldClock) * Rate, Swing);
    if (!pulse)
        return Core::Unexpected(std::string("0x4402a3: el pulso del 0x31 del 31 no cabe en el perfil PC53"));
    const float scale = static_cast<float>(((*pulse + Base) * Stretch) + Lift);
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::string("0x4402e0: falta el pool de sprites de los objetos del 31"));
    // 0x5491e0(textura, punto, escala, luz, dueño, giro, 0): MuRet no guarda el dueño del sprite (Q-C303).
    const glm::vec3 red(1.0f, 0.0f, 0.0f);
    static_cast<void>(pass.sprites->Create(FlareTexture, point.Value(), scale, red, 0.0f, 0));
    static_cast<void>(pass.sprites->Create(FlareTexture, point.Value(), SecondFlareScale, red, 0.0f, 0));
    // 0x44031b: el giro sale después de los dos primeros, sin dueño.
    const float rotation = static_cast<float>(pass.random->Next() % FullTurn);
    static_cast<void>(pass.sprites->Create(ShinyTexture, point.Value(), ShinyScale, glm::vec3(ShinyGrey), rotation, 0));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepLandOfTrialsObjectFrame(const World::WorldObject& object,
                                                              const GameObjectPass& pass)
{
    if (pass.random == nullptr)
    {
        return Core::Unexpected(std::string("0x4401e0: falta el rand() de la CRT"));
    }
    switch (object.type)
    {
    case 3:
        // 0x440355: con rand() % 3 en 0, 0x53fe70(0x6f2, …, 3, escala, 0); el subtipo es el divisor que quedó
        // en ECX.
        if (pass.random->Next() % EmitChance != 0)
            return {};
        return Emit(pass, object, World::WaterfallSplashFirst, World::TrialsSplashSubtype, "0x440392");
    case 0x35:
        if (pass.random->Next() % EmitChance != 0)
            return {};
        return Emit(pass, object, World::BreathParticle, World::TrialsSmokeSubtype, "0x440254");
    case 0x31:
        return Sprites(pass);
    default:
        return {};
    }
}

Core::Expected<std::optional<TrialsBodyDraw>, std::string> LandOfTrialsBody(const World::WorldObject& object,
                                                                            const glm::vec3& color, float worldClock)
{
    // 0x6b8ac4 (0,0012f) para el 0x1b y el 0x36, 0x6b8bc8 (0,0009f) para el 0x34; 0x6b882c (0,5f) y
    // 0x6b8a1c (0,9f) para los dos.
    constexpr double FastRate = static_cast<double>(std::bit_cast<float>(0x3a9d4952u));
    constexpr double SlowRate = static_cast<double>(std::bit_cast<float>(0x3a6bedfau));
    constexpr float Swing = 0.5f;
    constexpr double Base = static_cast<double>(std::bit_cast<float>(0x3f666666u));
    double rate = FastRate;
    bool twice = false;
    std::string_view address = "0x4404c5";
    switch (object.type)
    {
    case 0x1b:
    case 0x36:
        break;
    case 0x34:
        rate = SlowRate;
        twice = true;
        address = "0x440446";
        break;
    case 0xa:
        // 0x4403ed: sin restaurarlo; el próximo objeto lo recibe de 0x5ffce0.
        return std::optional(
            TrialsBodyDraw{glm::vec3(std::bit_cast<float>(0x3f0f5c29u), std::bit_cast<float>(0x3f4ccccdu),
                                     std::bit_cast<float>(0x3f4f5c29u)),
                           false});
    default:
        return std::optional<TrialsBodyDraw>();
    }
    const double argument = static_cast<double>(worldClock) * rate + static_cast<double>(object.field84);
    const auto pulse = Render::Models::Detail::SineProductPC53(argument, Swing);
    if (!pulse)
        return Core::Unexpected(std::format("{}: el pulso del {:#x} del 31 no cabe en el perfil PC53", address,
                                            static_cast<int>(object.type)));
    // La suma queda en la x87 y cada canal se guarda de su producto.
    const double level = *pulse + Base;
    TrialsBodyDraw body;
    body.twice = twice;
    for (int axis = 0; axis < 3; ++axis)
        body.color[axis] = static_cast<float>(level * static_cast<double>(color[axis]));
    return std::optional(body);
}
} // namespace Client::Scenes
