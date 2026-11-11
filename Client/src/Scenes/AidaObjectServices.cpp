#include "Scenes/AidaObjectServices.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/KalimaParticleKernels.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/SelectionParticleKernels.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x5c6 (Effect\Spark03.jpg de la tabla de texturas del juego): los destellos del 0x1e y del 0x47.
constexpr std::uint16_t AidaFlareTexture = 0x5c6;
constexpr std::int32_t SelectionSceneId = 4;
constexpr std::int32_t MistCount = 20;
constexpr std::uint32_t TimeMask = 0x3ff;
constexpr std::uint32_t ButterflyWindow = 10;
constexpr std::uint32_t AidaEffectWindow = 30;
constexpr std::int32_t WaterfallSubtype = 2;
constexpr std::int32_t SplashSubtype = 4;
constexpr std::int32_t ButterflySubtype = 3;

// Un destello del par: el hueso (en el buffer global 0x68b181c + hueso · 0x30) con su desplazamiento, y
// las escalas de los dos sprites; la luz del primero es por tipo, la del segundo es fija.
struct FlarePair
{
    std::size_t bone;
    glm::vec3 offset;
    float firstScale;
    float firstRed;
};

// 0x425298..0x4255ee (0x1e): luz (0,1; 0,1; 0,3) y (0,15; 0,15; 0,15).
constexpr std::array<FlarePair, 6> BlueFlares{{
    {6, {0.0f, -3.0f, 1.0f}, 5.5f, 0.0f},
    {7, {0.0f, -3.0f, 0.0f}, 4.5f, 0.0f},
    {8, {0.0f, -3.0f, 0.0f}, 4.0f, 0.0f},
    {12, {-3.0f, -1.0f, 0.0f}, 4.5f, 0.0f},
    {13, {-3.0f, -3.0f, 0.0f}, 4.0f, 0.0f},
    {17, {0.0f, 2.0f, -8.0f}, 4.0f, 0.0f},
}};
// 0x4259bf..0x425d1a (0x47): el primero (0,5; 0; 0) en el hueso 6 y (0,25; 0; 0) en los demás; el segundo
// (0,15; 0; 0).
constexpr std::array<FlarePair, 6> RedFlares{{
    {6, {0.0f, -3.0f, 1.0f}, 6.0f, 0.5f},
    {7, {0.0f, -3.0f, 0.0f}, 5.5f, 0.25f},
    {8, {0.0f, -3.0f, 0.0f}, 5.0f, 0.25f},
    {12, {-3.0f, -1.0f, 0.0f}, 5.5f, 0.25f},
    {13, {-3.0f, -3.0f, 0.0f}, 5.0f, 0.25f},
    {17, {0.0f, 2.0f, -8.0f}, 5.0f, 0.25f},
}};
constexpr float SecondFlareScale = 3.0f;
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);
constexpr float Fifteenth = std::bit_cast<float>(0x3e19999au);
constexpr float BlueGlow = std::bit_cast<float>(0x3e99999au);

Core::Expected<glm::vec3, std::string> BonePoint(const GameObjectPass& pass, std::size_t bone, const glm::vec3& offset)
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

Core::Expected<void, std::string> Flares(const GameObjectPass& pass, std::span<const FlarePair> flares, bool blue)
{
    const glm::vec3 secondLight = blue ? glm::vec3(Fifteenth) : glm::vec3(Fifteenth, 0.0f, 0.0f);
    for (const FlarePair& flare : flares)
    {
        const auto position = BonePoint(pass, flare.bone, flare.offset);
        if (!position.HasValue())
        {
            return Core::Unexpected(position.Error());
        }
        if (pass.sprites == nullptr)
        {
            return Core::Unexpected(std::string("0x5491e0: falta el pool de sprites de los objetos del 54/33"));
        }
        // 0x5491e0(0x5c6, punto, escala, luz, objeto, 0, 0): MuRet no guarda el dueño del sprite (Q-C303).
        const glm::vec3 firstLight = blue ? glm::vec3(Tenth, Tenth, BlueGlow) : glm::vec3(flare.firstRed, 0.0f, 0.0f);
        static_cast<void>(
            pass.sprites->Create(AidaFlareTexture, position.Value(), flare.firstScale, firstLight, 0.0f, 0));
        static_cast<void>(
            pass.sprites->Create(AidaFlareTexture, position.Value(), SecondFlareScale, secondLight, 0.0f, 0));
    }
    return {};
}

Core::Expected<void, std::string> Emit(const GameObjectPass& pass, const World::WorldObject& object, std::int32_t type,
                                       std::int32_t subtype, const glm::vec3& light, World::ParticleOwner owner,
                                       std::string_view address)
{
    if (pass.particles == nullptr || pass.random == nullptr)
    {
        return Core::Unexpected(
            std::format("{}: falta el pool de partículas 0x743a198 para {:#x}/{}", address, type, subtype));
    }
    const auto created =
        pass.particles->CreateOwnedChecked(type, object.transform.position, object.transform.angles, light, subtype,
                                           object.transform.scale, pass.worldClock, *pass.random, pass.scene, owner);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    }
    return {};
}

Core::Expected<void, std::string> Mist(const GameObjectPass& pass, const World::WorldObject& object,
                                       const glm::vec3& light, std::string_view address)
{
    // 0x425740/0x4257ed/0x42583d: con +0x58 en −2 (el paso de 0x4250f0) no emite.
    if (object.state.hiddenMesh == World::HiddenObject)
    {
        return {};
    }
    const World::ParticleOwner owner{World::ParticleOwnerKind::WorldObject, object.identity};
    for (int count = 0; count < MistCount; ++count)
    {
        auto emitted = Emit(pass, object, World::CloudParticle, World::OwnedMistSubtype, light, owner, address);
        if (!emitted.HasValue())
        {
            return emitted;
        }
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepAidaObjectFrame(World::WorldObject& object, const GameObjectPass& pass)
{
    if (pass.random == nullptr)
    {
        return Core::Unexpected(std::string("0x425260: falta el rand() de la CRT"));
    }
    switch (object.type)
    {
    case 0x1e:
        return Flares(pass, BlueFlares, true);
    case 0x47:
        return Flares(pass, RedFlares, false);
    case 0x29:
        return Core::Unexpected(std::string("0x4255f3: el 0x29 de Aida vuelve a dibujar su malla 0 con 0x4ecc30 y la "
                                            "V del reloj, y no está portado"));
    case 0x41:
    case 0x42:
        return Core::Unexpected(std::string("0x42588d: el 0x41/0x42 de Aida vuelve a dibujar su malla 2 con 0x4ecc30 "
                                            "y la V del reloj, y no está portado"));
    case 0x38:
        // 0x42566d: rand() & 1 (con la corrección del signo, que con rand() no corre); con 1, nada.
        if ((pass.random->Next() & 1) != 0)
        {
            return {};
        }
        return Emit(pass, object, World::KalimaWaterfallFirst, WaterfallSubtype, glm::vec3(1.0f), {}, "0x42569d");
    case 0x39:
        return Emit(pass, object, World::WaterfallSplashFirst, SplashSubtype, glm::vec3(1.0f), {}, "0x4256dc");
    case 0x3a:
        if (pass.random->Next() % 3 != 0)
        {
            return {};
        }
        return Emit(pass, object, World::KalimaWaterfallMist, WaterfallSubtype, glm::vec3(1.0f), {}, "0x42572f");
    case 0x3b:
        return Mist(pass, object,
                    glm::vec3(std::bit_cast<float>(0x3c23d70au), std::bit_cast<float>(0x3cf5c28fu),
                              std::bit_cast<float>(0x3d4ccccdu)),
                    "0x42577c");
    case 0x3e:
        return Mist(pass, object, glm::vec3(std::bit_cast<float>(0x3d4ccccdu)), "0x425829");
    case 0x3f:
        return Mist(pass, object,
                    glm::vec3(std::bit_cast<float>(0x3d4ccccdu), std::bit_cast<float>(0x3ca3d70au),
                              std::bit_cast<float>(0x3ca3d70au)),
                    "0x425879");
    case 0x3c:
    {
        // 0x425790: timeGetTime() (0x6b8544) & 0x3ff; el and deja un valor no negativo.
        if (!pass.systemTime)
        {
            return Core::Unexpected(std::string("0x425790: falta timeGetTime() para la mariposa del 0x3c"));
        }
        if ((pass.systemTime() & TimeMask) < ButterflyWindow)
        {
            if (pass.effects == nullptr)
            {
                return Core::Unexpected(std::string("0x4257d5: falta el pool de efectos de modelo para la 0xae"));
            }
            static_cast<void>(pass.effects->CreateButterflyEffect(
                object.transform.position, object.transform.angles, glm::vec3(1.0f), ButterflySubtype,
                object.transform.scale, pass.scene == SelectionSceneId, *pass.random));
        }
        // 0x4257dd: siempre, aunque no haya salido la mariposa.
        object.state.hiddenMesh = World::HiddenObject;
        return {};
    }
    case 0x43:
        // 0x4258ff: la luz del objeto (+0xf4..+0xfc) en 0,3 antes de prestarla a la llama.
        object.light = glm::vec3(BlueGlow);
        return Emit(pass, object, World::FlameParticle, World::RisingFlameSubtype, object.light, {}, "0x42592e");
    case 0x46:
    {
        if (!pass.systemTime)
        {
            return Core::Unexpected(std::string("0x42593f: falta timeGetTime() para el 0x46 de Aida"));
        }
        const std::uint32_t time = pass.systemTime() & TimeMask;
        if (pass.random->Next() % 5 == 1 && time < AidaEffectWindow)
        {
            return Core::Unexpected(std::string("0x4259ae: el efecto 0xb3 del 0x46 de Aida no está portado"));
        }
        return {};
    }
    default:
        return {};
    }
}
} // namespace Client::Scenes
