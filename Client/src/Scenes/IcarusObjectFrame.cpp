#include "Scenes/IcarusObjectFrame.h"

#include "Core/Expected.h"
#include "Scenes/ObjectFrameSprites.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/KalimaParticleKernels.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

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
// 0x603f09..0x6040ac: la luz de las nubes, 0x3dcccccd en las tres componentes.
constexpr float CloudLight = std::bit_cast<float>(0x3dcccccdu);
constexpr int ManyClouds = 20;
constexpr int FewClouds = 10;
constexpr std::int16_t LastManyClouds = 2;
constexpr std::int16_t LastCloud = 5;
constexpr std::int16_t Flare = 10;
// 0x6040f3: 0x68b18ac, el hueso 3 desde 0x68b181c, de a 0x30.
constexpr std::size_t FlareBone = 3;

Core::Expected<void, std::string> Emit(const GameObjectPass& pass, std::int32_t type, const glm::vec3& position,
                                       const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                                       float scale, World::ParticleOwner owner, std::string_view address)
{
    if (pass.particles == nullptr || pass.random == nullptr)
        return Core::Unexpected(
            std::format("{}: falta el pool de partículas 0x743a198 para {:#x}/{}", address, type, subtype));
    const auto created = pass.particles->CreateOwnedChecked(type, position, angles, light, subtype, scale,
                                                            pass.worldClock, *pass.random, pass.scene, owner);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepIcarusObjectFrame(World::WorldObject& object, const GameObjectPass& pass)
{
    if (object.type >= 0 && object.type <= LastCloud)
    {
        // 0x603f03..0x6040db: con +0x58 en −2 no emite; después lo deja en −2 igual.
        if (object.state.hiddenMesh != World::HiddenObject)
        {
            const int count = object.type <= LastManyClouds ? ManyClouds : FewClouds;
            const World::ParticleOwner owner{World::ParticleOwnerKind::WorldObject, object.identity};
            for (int cloud = 0; cloud < count; ++cloud)
            {
                if (auto emitted = Emit(pass, World::CloudParticle, object.transform.position, object.transform.angles,
                                        glm::vec3(CloudLight), object.type, object.transform.scale, owner, "0x603f3b");
                    !emitted.HasValue())
                    return emitted;
            }
        }
        object.state.hiddenMesh = World::HiddenObject;
        return {};
    }
    if (object.type != Flare)
        return {};
    // 0x6040e9..0x604143: 0x4ec110(hueso 3, cero, salida, 0) y 0x53fe70(0x5ac, punto, ángulos, 1, 0, 1, 0).
    const auto point = ObjectBonePoint(pass, FlareBone, glm::vec3(0.0f));
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    return Emit(pass, World::KalimaFlare, point.Value(), object.transform.angles, glm::vec3(1.0f), 0, 1.0f, {},
                "0x604143");
}
} // namespace Client::Scenes
