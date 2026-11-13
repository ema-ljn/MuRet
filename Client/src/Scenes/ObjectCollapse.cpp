#include "Scenes/ObjectCollapse.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/TerrainMarks.h"
#include "Scenes/WorldRuntime.h"
#include "World/ChaosCastle.h"
#include "World/Particles.h"
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
using Result = Core::Expected<void, std::string>;

float F32(double value)
{
    return static_cast<float>(value);
}

// 0x5ff9c2..0x5ffa1e: los objetos 9 y 10 (con cualquier tipo pedido) se abren con la cuenta en 0: +0x58 en −1 y +0x8a
// en 4.
constexpr std::int16_t FirstOpeningObject = 9;
constexpr std::int16_t SecondOpeningObject = 10;
constexpr std::int16_t OpenedMark = 4;
// 0x5ffa2c..0x5ffa4c: con la cuenta en 20, +0x1c en 35 y el sonido 0x89.
constexpr std::int32_t FallStartCounter = 0x14;
constexpr float FallStartAngle = 35.0f;
constexpr std::int32_t FallSound = 0x89;
// 0x6b8b94, 0x6b8948 y 0x42a00000: lo que suma la demora en cada objeto, el giro que termina y el giro del polvo.
constexpr double DelayStep = 1.5;
constexpr float FallenAngle = 90.0f;
constexpr std::uint32_t DustAngleBits = 0x42a00000u;
constexpr float DelayAfterTheFall = 2.0f;
// 0x5ffac4..0x5ffb2a: diez polvos 5f3/0 corridos en x por rand() % 300 − 150 (0x6b87ac) y atrás en y por rand() % 20
// + 600 (0x6b8c08), con los ángulos y la luz del objeto, escala 1 y sin dueño.
constexpr int DustCount = 10;
constexpr int DustSpreadX = 300;
constexpr double DustBackX = 150.0;
constexpr int DustSpreadY = 20;
constexpr double DustBackY = 600.0;
constexpr float DustScale = 1.0f;
// 0x5ffb48..0x5ffb54: 0x5f8750(13, 70, 3, 6, 8, 0), el paso de la puerta de Blood Castle sin el atributo 8.
constexpr TerrainArea GatePassage{.x = 0xd, .y = 0x46, .width = 3, .height = 6};
constexpr std::uint8_t GateAttribute = 8;

// 0x405630: la tabla de 0x40592c por el tipo (0 a 0x23) a los cuatro casos de 0x40591c: el área de 0x6c634c que
// esperan y la que dejan al terminar, y dónde cada uno crea la piedra y escribe el temblor; el 3 no hace nada.
struct ChaosCastleStep
{
    std::uint8_t expected = 0;
    std::uint8_t next = 0;
    std::string_view stoneAt;
    std::string_view shakeAt;
};
constexpr std::int16_t LastChaosCastleType = 0x23;
constexpr std::array<std::uint8_t, LastChaosCastleType + 1> ChaosCastleStepOfType{
    0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0, 3, 3, 3, 3, 3, 3, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2};
// 0x40581f, 0x40566b y 0x405745.
constexpr std::array<ChaosCastleStep, 3> ChaosCastleSteps{
    {{1, 2, "0x405899", "0x4058bf"}, {7, 8, "0x4056e5", "0x40570b"}, {4, 5, "0x4057bf", "0x4057e5"}}};
// 0x40567d..0x405740 (y sus copias): con la cuenta en 30 o más, la piedra; en 0 o menos, el final; si no, la z baja
// con la demora.
constexpr std::int32_t ChaosCastleStoneCounter = 0x1e;
constexpr int StoneSpread = 300;
constexpr double StoneBack = 150.0;
constexpr float StoneScale = 1.5f;
constexpr int ShakeSpan = 3;
constexpr double ShakeStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr double ChaosCastleDelayStep = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));

Result CreateParticle(const ObjectCollapseContext& context, std::int32_t type, const glm::vec3& position,
                      const glm::vec3& angles, const glm::vec3& light, float scale, std::string_view where)
{
    if (context.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de partículas (0x53fe70) para {:#x}/0", where, type));
    const auto created = context.particles->CreateChecked(type, position, angles, light, 0, scale, context.worldClock,
                                                          context.random, 0);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: el alta de {:#x}/0 (0x53fe70) falla con error {}", where, type,
                                            static_cast<unsigned>(created.Error())));
    return {};
}

// 0x405630 en los mapas 18 a 23: devuelve 1 siempre, así que 0x5ff960 no sigue con ningún objeto.
Result StepChaosCastleCollapse(World::WorldObject& object, const ObjectCollapseContext& context)
{
    WorldRuntime& runtime = context.runtime;
    ObjectCollapse& collapse = runtime.events.objectCollapse;
    if (object.type < 0 || object.type > LastChaosCastleType)
        return {};
    const std::uint8_t kind = ChaosCastleStepOfType[static_cast<std::size_t>(object.type)];
    if (kind >= ChaosCastleSteps.size())
        return {};
    const ChaosCastleStep& step = ChaosCastleSteps[kind];
    if (runtime.chaosCastleArea != step.expected)
        return {};
    const std::int32_t counter = collapse.counter;
    if (counter >= ChaosCastleStoneCounter)
    {
        // 0x40569b..0x40570b (el caso 1): la piedra en (x + rand() % 300 − 150, y, z del héroe), con los ángulos del
        // objeto, luz 1, escala 1,5 y sin dueño, y el temblor (rand() % 3 − 3) · 0,1f.
        const int spread = context.random.Next() % StoneSpread;
        const glm::vec3 stone{
            F32((static_cast<double>(spread) - StoneBack) + static_cast<double>(object.transform.position.x)),
            object.transform.position.y, context.heroZ};
        if (auto created = CreateParticle(context, World::ChaosCastleFallingStone, stone, object.transform.angles,
                                          glm::vec3(1.0f), StoneScale, step.stoneAt);
            !created.HasValue())
            return created;
        const int shake = context.random.Next() % ShakeSpan - ShakeSpan;
        if (context.cameraShake == nullptr)
            return Core::Unexpected(std::format("{}: falta el temblor de la cámara (0x7d325c8)", step.shakeAt));
        *context.cameraShake = F32(static_cast<double>(shake) * ShakeStep);
        return {};
    }
    if (counter <= 0)
    {
        // 0x405715..0x40572c: el objeto oculto, el área siguiente y el pedido borrado.
        object.state.hiddenMesh = World::HiddenObject;
        runtime.chaosCastleArea = step.next;
        ResetTheObjectCollapse(collapse);
        return {};
    }
    // 0x405733..0x405740 y 0x4058f4..0x40590f: z = +0x184 − (30 − cuenta) · demora, y la demora sube 0,4f.
    const std::int32_t fallen = ChaosCastleStoneCounter - counter;
    object.transform.position.z = F32(static_cast<double>(object.basePosition.z) -
                                      (static_cast<double>(fallen) * static_cast<double>(collapse.delay)));
    collapse.delay = F32(static_cast<double>(collapse.delay) + ChaosCastleDelayStep);
    return {};
}

// 0x5ffa24..0x5ffb59: el objeto del tipo pedido (la puerta de Blood Castle, 0x24).
Result FellTheObject(World::WorldObject& object, const ObjectCollapseContext& context)
{
    ObjectCollapse& collapse = context.runtime.events.objectCollapse;
    float& angle = object.transform.angles.x;
    if (collapse.counter == FallStartCounter)
    {
        angle = FallStartAngle;
        object.state.hiddenMesh = Render::Models::NoHiddenMesh;
        // 0x41ed10(0x89, 0, 0): MuRet todavía no tiene audio (CL10).
        context.runtime.lastSoundId = FallSound;
    }
    if (collapse.counter < 0)
        return {};
    angle = F32(static_cast<double>(collapse.delay) + static_cast<double>(angle));
    collapse.delay = F32(static_cast<double>(collapse.delay) + DelayStep);
    // 0x5ffa81..0x5ffa8e: FCOMP y C0; un NaN no entra.
    if (angle >= FallenAngle)
    {
        angle = F32(static_cast<double>(angle) - static_cast<double>(collapse.counter));
        collapse.delay = DelayAfterTheFall;
        if (std::bit_cast<std::uint32_t>(angle) == DustAngleBits)
        {
            // 0x5ffaa8..0x5ffabe: la posición del objeto, con x e y nuevas en cada vuelta y la z de siempre.
            glm::vec3 dust = object.transform.position;
            for (int count = 0; count < DustCount; ++count)
            {
                const int spreadX = context.random.Next() % DustSpreadX;
                dust.x =
                    F32((static_cast<double>(spreadX) - DustBackX) + static_cast<double>(object.transform.position.x));
                const int spreadY = context.random.Next() % DustSpreadY;
                dust.y =
                    F32(static_cast<double>(object.transform.position.y) - (static_cast<double>(spreadY) + DustBackY));
                if (auto created = CreateParticle(context, World::DustParticle, dust, object.transform.angles,
                                                  object.light, DustScale, "0x5ffb21");
                    !created.HasValue())
                    return created;
            }
        }
    }
    if (collapse.counter != 0)
        return {};
    // 0x5ffb36..0x5ffb54: caída, el pedido borrado y el paso abierto.
    object.state.hiddenMesh = World::HiddenObject;
    angle = FallenAngle;
    ResetTheObjectCollapse(collapse);
    return MarkTheTerrainArea(context.attributes, GatePassage, GateAttribute, false, "0x5ffb54");
}
} // namespace

bool ObjectCollapseActive(const WorldRuntime& runtime)
{
    const ObjectCollapse& collapse = runtime.events.objectCollapse;
    return collapse.map >= 0 && collapse.type >= 0 && collapse.counter >= 0 && runtime.map == collapse.map;
}

void ResetTheObjectCollapse(ObjectCollapse& collapse)
{
    collapse = ObjectCollapse{};
}

void CountDownTheObjectCollapse(WorldRuntime& runtime)
{
    std::int32_t& counter = runtime.events.objectCollapse.counter;
    counter = static_cast<std::int32_t>(static_cast<std::uint32_t>(counter) - 1U);
}

Result CollapseTheObject(World::WorldObject& object, const ObjectCollapseContext& context)
{
    if (!ObjectCollapseActive(context.runtime))
        return {};
    if (World::IsChaosCastleMap(context.runtime.map))
        return StepChaosCastleCollapse(object, context);
    ObjectCollapse& collapse = context.runtime.events.objectCollapse;
    if (object.type == FirstOpeningObject || object.type == SecondOpeningObject)
    {
        if (collapse.counter != 0)
            return {};
        object.state.hiddenMesh = Render::Models::NoHiddenMesh;
        object.field8a = OpenedMark;
        return {};
    }
    if (static_cast<std::int32_t>(object.type) != collapse.type)
        return {};
    return FellTheObject(object, context);
}
} // namespace Client::Scenes
