#include "Scenes/KalimaObjectServices.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr float WaterPulseRate = std::bit_cast<float>(0x3b03126fu);
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);
constexpr float WaterPulseBase = std::bit_cast<float>(0x3e99999au);
constexpr float FlareRate = std::bit_cast<float>(0x3a83126fu);
constexpr float FlareAmplitude = std::bit_cast<float>(0x3e99999au);
constexpr float FlareBase = std::bit_cast<float>(0x3f333333u);
constexpr float FlareExtra = std::bit_cast<float>(0x3e4ccccdu);
constexpr float FlareChannel = std::bit_cast<float>(0x3f19999au);
constexpr std::uint32_t FirstAnimatedWaterTexture = 65;
constexpr std::uint32_t AnimatedWaterFrames = 32;

bool HasWaterOverlay(std::int16_t type)
{
    // 4394cd..43956e, comparaciones signed16 después del primer RenderBody.
    constexpr std::int16_t LastOverlayType = 66;
    constexpr std::array<std::int16_t, 18> Excluded{2,  4,  12, 14, 15, 18, 20, 21, 27,
                                                    29, 30, 31, 32, 41, 43, 52, 54, 55};
    if (type < 0 || type > LastOverlayType)
        return false;
    for (const auto excluded : Excluded)
        if (type == excluded)
            return false;
    return true;
}

Core::Expected<void, std::string> KalimaMeshPass(Render::ModelBatch& batch, const IModelLibrary& models,
                                                 const DrawnObject& drawn, Core::Duration frameClock, std::int16_t mesh,
                                                 std::uint32_t flags,
                                                 std::optional<Render::TextureSlot> texture = std::nullopt)
{
    if (!drawn.retainedMeshes.empty())
        return DrawItemMeshPass(batch, models, drawn, frameClock, drawn.retainedMeshes, mesh, flags, texture);
    FaunaDrawCarry local;
    if (auto posed = PublishFaunaPose(local, models, drawn, true); !posed.HasValue())
        return posed;
    return DrawItemMeshPass(batch, models, drawn, frameClock, local.meshes, mesh, flags, texture);
}

Core::Expected<void, std::string> WaterOverlay(Render::ModelBatch& batch, const IModelLibrary& models,
                                               DrawnObject& drawn, Core::Duration frameClock,
                                               const GameObjectPass& pass)
{
    // 439570: argumento sin spill binary32; el color sí tiene tres FSTP idénticos.
    const auto pulse = Render::Models::Detail::SineProductPC53(
        static_cast<double>(pass.worldClock) * static_cast<double>(WaterPulseRate), Tenth);
    if (!pulse)
        return Core::Unexpected(std::string("0x43957c: pulso de Kalima fuera del dominio matemático portado"));
    if (!pass.waterTextureFrame)
        return Core::Unexpected(std::string("0x439593: falta el productor compartido DAT_07d2d360"));
    if (*pass.waterTextureFrame >= AnimatedWaterFrames)
        return Core::Unexpected(std::string("0x439593: DAT_07d2d360 fuera del ciclo público0..31"));
    const auto selector = FirstAnimatedWaterTexture + *pass.waterTextureFrame;
    const auto texture = models.TextureSlotOf(selector);
    if (!texture)
        return Core::Unexpected(std::format("0x4395ba: falta la textura animada de agua{} de618e1d", selector));
    drawn.color = glm::vec3(static_cast<float>(*pulse + static_cast<double>(WaterPulseBase)));
    drawn.alpha = WaterPulseBase;
    return KalimaMeshPass(batch, models, drawn, frameClock, -1, 2, texture);
}

Core::Expected<void, std::string> Flare(const GameObjectPass& pass)
{
    // 438c53..438cd0: hueso20 del scratch global; un hueso aún no escrito conserva .bss cero.
    constexpr std::size_t Bone = 20;
    if (pass.retainedPose == nullptr)
        return Core::Unexpected(std::string("0x438c9e: falta el scratch global del hueso20 de Kalima"));
    glm::vec3 point(0.0f);
    if (Bone < pass.retainedPose->bones.size())
        point = Render::Models::OriginalVectorTransform(
            glm::vec3(0.0f), Render::Models::OriginalMatrixOf(pass.retainedPose->bones[Bone]));
    const float argument = static_cast<float>(static_cast<double>(pass.worldClock) * FlareRate);
    const auto pulse = Render::Models::Detail::SineProductPC53(argument, FlareAmplitude);
    if (!pulse)
        return Core::Unexpected(std::string("0x438ca6: pulso del sprite de Kalima fuera de dominio"));
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::string("0x438cd0: falta el pool de sprites de Kalima"));
    const float scale = static_cast<float>((*pulse + FlareBase) + FlareExtra);
    static_cast<void>(pass.sprites->Create(World::StreetLightGlowTexture, point, scale,
                                           glm::vec3(FlareChannel, FlareChannel, 1.0f), 0.0f, 0));
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawKalimaObjectTail(Render::ModelBatch& batch, const IModelLibrary& models,
                                                       const World::WorldObject& object, const DrawnObject& drawn,
                                                       Core::Duration frameClock, const GameObjectPass& pass)
{
    DrawnObject current = drawn;
    if (HasWaterOverlay(object.type))
    {
        if (auto overlay = WaterOverlay(batch, models, current, frameClock, pass); !overlay.HasValue())
            return overlay;
    }
    current.alpha = drawn.alpha;
    // 4395bf: el tipo34 tiene una malla extra, sólo si el contador signed16 supera1.
    if (object.type == 34 && models.Find(drawn.model) != nullptr && models.Find(drawn.model)->meshes.size() > 1)
    {
        if (!models.TextureSlotOf(ChromeTextureSlot.value))
            return Core::Unexpected(std::string("0x4395fc: falta la textura Chrome del objeto34 de Kalima"));
        if (auto chrome = KalimaMeshPass(batch, models, current, frameClock, 1, 0x44); !chrome.HasValue())
            return chrome;
    }
    if (object.type == 15 || object.type == 29 || object.type == 32)
    {
        current.color = glm::vec3(Tenth);
        return KalimaMeshPass(batch, models, current, frameClock, -1, 0x22);
    }
    return {};
}

Core::Expected<void, std::string> StepKalimaObjectFrame(World::WorldObject& object, const GameObjectPass& pass)
{
    // 6044ed→4059d0 retorna AL0;438c10 retorna AL1 aun en el default de438f90.
    switch (object.type)
    {
    case 12:
        return Flare(pass);
    case 15:
    case 29:
    case 32:
        return Core::Unexpected(std::string("0x438ad0: falta el movimiento del objeto con la acción del héroe"));
    case 35:
        return Core::Unexpected(std::string("0x438dde: falta la creación de partícula0x5ac/subtipo6"));
    case 36:
        return Core::Unexpected(std::string("0x438e23: falta la creación de partícula0x6ed/subtipo0"));
    case 37:
        return Core::Unexpected(std::string("0x438e6f: falta la creación de partícula0x6f4/subtipo0"));
    case 38:
        if (pass.random->Next() % 2 == 0)
            return Core::Unexpected(std::string("0x438ed5: falta la creación de partícula0x6f0/subtipo0"));
        return Core::Unexpected(std::string("0x438ee6: falta el servicio de sonido0x1d6 de Kalima"));
    case 39:
        static_cast<void>(pass.random->Next());
        return Core::Unexpected(std::string("0x438f81: falta la creación de partícula0x6f2/0x6f3"));
    case 40:
        if (pass.random->Next() % 4 == 0)
            return Core::Unexpected(std::string("0x438f81: falta la creación de partícula0x6f1/subtipo0"));
        object.transform.scale = 0.5f; // 438f89: también cuando el sorteo omite la partícula.
        return {};
    default:
        return {};
    }
}
} // namespace Client::Scenes
