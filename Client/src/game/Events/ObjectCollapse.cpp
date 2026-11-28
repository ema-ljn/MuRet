#include "Events/ObjectCollapse.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Maps/MapId.h"
#include "Math/X87.h"
#include "Models/ObjectState.h"
#include "Events/EventGlobals.h"
#include "Terrain/TerrainMarks.h"
#include "Kernel/WorldRuntime.h"
#include "Events/ChaosCastle/ChaosCastle.h"
#include "Particles/Particles.h"
#include "Kernel/WorldObject.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Events
{
namespace
{
using Result = Core::Expected<void, std::string>;

using Math::F32;

// 0x5ff9c2..0x5ffa1e: objects 9 and 10 (with any requested type) open with the count at 0: +0x58 at −1 and +0x8a at 4.
constexpr std::int16_t FirstOpeningObject = 9;
constexpr std::int16_t SecondOpeningObject = 10;
constexpr std::int16_t OpenedMark = 4;
// 0x5ffa2c..0x5ffa4c: with the count at 20, +0x1c at 35 and sound 0x89.
constexpr std::int32_t FallStartCounter = 0x14;
constexpr float FallStartAngle = 35.0f;
constexpr Audio::SoundId FallSound{0x89};
// 0x6b8b94, 0x6b8948 and 0x42a00000: what the delay adds to each object, the turn that ends it and the dust's turn.
constexpr double DelayStep = 1.5;
constexpr float FallenAngle = 90.0f;
constexpr std::uint32_t DustAngleBits = 0x42a00000u;
constexpr float DelayAfterTheFall = 2.0f;
// 0x5ffac4..0x5ffb2a: ten 0x5f3/0 dusts shifted in x by rand() % 300 − 150 (0x6b87ac) and back in y by rand() % 20 +
// 600 (0x6b8c08), with the object's angles and light, scale 1 and no owner.
constexpr int DustCount = 10;
constexpr int DustSpreadX = 300;
constexpr double DustBackX = 150.0;
constexpr int DustSpreadY = 20;
constexpr double DustBackY = 600.0;
constexpr float DustScale = 1.0f;
// 0x5ffb48..0x5ffb54: 0x5f8750(13, 70, 3, 6, 8, 0), the Blood Castle gate passage without attribute 8.
constexpr Terrain::TerrainArea GatePassage{.x = 0xd, .y = 0x46, .width = 3, .height = 6};
constexpr std::uint8_t GateAttribute = 8;

// 0x405630: the table at 0x40592c by type (0 to 0x23) to the four cases of 0x40591c: the area of 0x6c634c they expect
// and the one they leave when done, and where each creates the stone and writes the shake; 3 does nothing.
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
// 0x40581f, 0x40566b and 0x405745.
constexpr std::array<ChaosCastleStep, 3> ChaosCastleSteps{
    {{1, 2, "0x405899", "0x4058bf"}, {7, 8, "0x4056e5", "0x40570b"}, {4, 5, "0x4057bf", "0x4057e5"}}};
// 0x40567d..0x405740 (and its copies): with the count at 30 or more, the stone; at 0 or less, the end; otherwise z
// drops with the delay.
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

// 0x405630 in maps 18 to 23: always returns 1, so 0x5ff960 goes on with no object.
Result StepChaosCastleCollapse(Kernel::WorldObject& object, const ObjectCollapseContext& context)
{
    Kernel::WorldRuntime& runtime = context.runtime;
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
        // 0x40569b..0x40570b (case 1): the stone at (x + rand() % 300 − 150, y, hero z), with the object's angles,
        // light 1, scale 1.5 and no owner, and the shake (rand() % 3 − 3) · 0.1f.
        const int spread = context.random.Next() % StoneSpread;
        const glm::vec3 stone{
            F32((static_cast<double>(spread) - StoneBack) + static_cast<double>(object.transform.position.x)),
            object.transform.position.y, context.heroZ};
        if (auto created = CreateParticle(context, Events::ChaosCastle::ChaosCastleFallingStone, stone,
                                          object.transform.angles, glm::vec3(1.0f), StoneScale, step.stoneAt);
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
        // 0x405715..0x40572c: the object hidden, the next area and the request cleared.
        object.state.hiddenMesh = Kernel::HiddenObject;
        runtime.chaosCastleArea = step.next;
        ResetTheObjectCollapse(collapse);
        return {};
    }
    // 0x405733..0x405740 and 0x4058f4..0x40590f: z = +0x184 − (30 − count) · delay, and the delay rises 0.4f.
    const std::int32_t fallen = ChaosCastleStoneCounter - counter;
    object.transform.position.z = F32(static_cast<double>(object.basePosition.z) -
                                      (static_cast<double>(fallen) * static_cast<double>(collapse.delay)));
    collapse.delay = F32(static_cast<double>(collapse.delay) + ChaosCastleDelayStep);
    return {};
}

// 0x5ffa24..0x5ffb59: the object of the requested type (the Blood Castle gate, 0x24).
Result FellTheObject(Kernel::WorldObject& object, const ObjectCollapseContext& context)
{
    ObjectCollapse& collapse = context.runtime.events.objectCollapse;
    float& angle = object.transform.angles.x;
    if (collapse.counter == FallStartCounter)
    {
        angle = FallStartAngle;
        object.state.hiddenMesh = Models::NoHiddenMesh;
        // 0x41ed10(0x89, 0, 0): RequestSound dispatches the effect.
        context.runtime.RequestSound(FallSound);
    }
    if (collapse.counter < 0)
        return {};
    angle = F32(static_cast<double>(collapse.delay) + static_cast<double>(angle));
    collapse.delay = F32(static_cast<double>(collapse.delay) + DelayStep);
    // 0x5ffa81..0x5ffa8e: FCOMP and C0; a NaN does not enter.
    if (angle >= FallenAngle)
    {
        angle = F32(static_cast<double>(angle) - static_cast<double>(collapse.counter));
        collapse.delay = DelayAfterTheFall;
        if (std::bit_cast<std::uint32_t>(angle) == DustAngleBits)
        {
            // 0x5ffaa8..0x5ffabe: the object's position, with new x and y on each turn and the same z.
            glm::vec3 dust = object.transform.position;
            for (int count = 0; count < DustCount; ++count)
            {
                const int spreadX = context.random.Next() % DustSpreadX;
                dust.x =
                    F32((static_cast<double>(spreadX) - DustBackX) + static_cast<double>(object.transform.position.x));
                const int spreadY = context.random.Next() % DustSpreadY;
                dust.y =
                    F32(static_cast<double>(object.transform.position.y) - (static_cast<double>(spreadY) + DustBackY));
                if (auto created = CreateParticle(context, Particles::DustParticle, dust, object.transform.angles,
                                                  object.light, DustScale, "0x5ffb21");
                    !created.HasValue())
                    return created;
            }
        }
    }
    if (collapse.counter != 0)
        return {};
    // 0x5ffb36..0x5ffb54: fallen, the request cleared and the passage open.
    object.state.hiddenMesh = Kernel::HiddenObject;
    angle = FallenAngle;
    ResetTheObjectCollapse(collapse);
    return Terrain::MarkTheTerrainArea(context.attributes, GatePassage, GateAttribute, false, "0x5ffb54");
}
} // namespace

bool ObjectCollapseActive(const Kernel::WorldRuntime& runtime)
{
    const ObjectCollapse& collapse = runtime.events.objectCollapse;
    return collapse.map >= 0 && collapse.type >= 0 && collapse.counter >= 0 && runtime.map == collapse.map;
}

void ResetTheObjectCollapse(ObjectCollapse& collapse)
{
    collapse = ObjectCollapse{};
}

void CountDownTheObjectCollapse(Kernel::WorldRuntime& runtime)
{
    std::int32_t& counter = runtime.events.objectCollapse.counter;
    counter = static_cast<std::int32_t>(static_cast<std::uint32_t>(counter) - 1U);
}

Result CollapseTheObject(Kernel::WorldObject& object, const ObjectCollapseContext& context)
{
    if (!ObjectCollapseActive(context.runtime))
        return {};
    if (Maps::IsChaosCastle(context.runtime.map))
        return StepChaosCastleCollapse(object, context);
    ObjectCollapse& collapse = context.runtime.events.objectCollapse;
    if (object.type == FirstOpeningObject || object.type == SecondOpeningObject)
    {
        if (collapse.counter != 0)
            return {};
        object.state.hiddenMesh = Models::NoHiddenMesh;
        object.field8a = OpenedMark;
        return {};
    }
    if (static_cast<std::int32_t>(object.type) != collapse.type)
        return {};
    return FellTheObject(object, context);
}
} // namespace Client::Events
