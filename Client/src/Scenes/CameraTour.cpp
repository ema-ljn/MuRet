#include "Scenes/CameraTour.h"

#include "Core/Expected.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorMotion.h"
#include "Scenes/ActorState.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Ftol.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;

// 0x4033d0 y 0x4033f0: la etapa del recorrido y la que lo deshace.
constexpr std::int32_t TourStage = 2;
constexpr std::int32_t UndoStage = 3;
// 0x403400: 0x404c90(5000).
constexpr std::int32_t TourDelay = 5000;
// 0x403d60 y +0x14c en 0x403300: la primera clave del guion.
constexpr std::int32_t FirstTourActor = 10000;
// 0x403912..0x403931 y 0x403770..0x403790: sin la clave entre los 400, queda el último registro mirado.
constexpr std::size_t LastRecord = 399;
// 0x6b86b4 y 0x6b86b0: el centro de una celda.
constexpr double CellSize = 100.0;
constexpr double CellCenter = 50.0;
// 0x6b86c0 (0,01f), con el que 0x403760 vuelve a la celda.
constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
// 0x6b86a8 (double), los grados de 0x4036d0.
constexpr double Degrees = std::bit_cast<double>(0x404ca5ddb05cc3e9ull);
// 0x6b869c, 0x6b86bc, 0x6b86b8 y 0x6b86c4.
constexpr double FullTurn = 360.0;
constexpr double HalfTurn = 180.0;
constexpr float TurnStep = 3.0f;
constexpr float LastActionFrame = 8.0f;
// 0x403a82..0x403a9c y 0x403d40: el tope de +0x150.
constexpr std::int32_t LastLevel = 4;
// 0x403ad0: el sonido del alta con las chispas.
constexpr std::int32_t ArrivalSound = 0x243;
// 0x4038b9, 0x403856 y 0x4039f0: las acciones de 0x4ea030 (quieto, caminar) y la que toma la velocidad.
constexpr std::uint8_t StandAction = 0;
constexpr std::uint8_t WalkAction = 2;
// Las velocidades de los tramos (0x43960000 y 0x42200000) y de las acciones (0xbf800000, 0x3e3851ec y 0x3e6872b0).
constexpr float FastLeg = 300.0f;
constexpr float SlowLeg = 40.0f;
constexpr float KeepSpeed = -1.0f;
constexpr float BarrierSpeed = std::bit_cast<float>(0x3e3851ecu);
constexpr float GuardSpeed = std::bit_cast<float>(0x3e6872b0u);

float F32(double value)
{
    return static_cast<float>(value);
}

float CellCenterOf(std::int32_t cell)
{
    return F32(static_cast<double>(cell) * CellSize + CellCenter);
}

// 0x404c90(período): con +0x37, +0x148 en ahora; vence con +0x148 + período <= ahora (con signo), y entonces +0x37 en
// 1 y +0x140 uno más.
bool TourTimerFires(CameraTour& tour, std::uint32_t now, std::int32_t period)
{
    if (tour.timerRestart)
    {
        tour.timerStart = now;
        tour.timerRestart = false;
    }
    const auto due = static_cast<std::int32_t>(tour.timerStart + static_cast<std::uint32_t>(period));
    if (due > static_cast<std::int32_t>(now))
        return false;
    tour.timerRestart = true;
    ++tour.step;
    return true;
}

// 0x5fc800: la suma de los cuadrados desde 0 en la x87 (PC53), su raíz y, si no es 0 (FCOM con C3, también NaN),
// cada componente dividido y guardado en float.
void Normalize(glm::vec3& vector)
{
    double sum = 0.0;
    for (int axis = 0; axis < 3; ++axis)
        sum += static_cast<double>(vector[axis]) * static_cast<double>(vector[axis]);
    const double length = std::sqrt(sum);
    if (length == 0.0 || std::isnan(length))
        return;
    for (int axis = 0; axis < 3; ++axis)
        vector[axis] = F32(static_cast<double>(vector[axis]) / length);
}

// 0x403d90(x, y, z, velocidad): el tramo siguiente, al centro de la celda; +0x140 en 0 y la etapa del guion una más.
void HeadTo(CameraTour& tour, std::int32_t x, std::int32_t y, std::int32_t z, float speed)
{
    tour.step = 0;
    tour.moving = true;
    tour.begin = true;
    tour.speed = speed;
    ++tour.scriptStage;
    tour.target = {CellCenterOf(x), CellCenterOf(y), CellCenterOf(z)};
}

// 0x403430: con +0x34, en la etapa 0 del guion sale del héroe ([0x743a170] + 0x10) y pasa a la 1; el rumbo es destino −
// salida (en float) y el largo su norma ((x² + y²) + z², en la x87).
void MeasureTheLeg(CameraTour& tour, const ActorState& hero)
{
    if (!tour.begin)
        return;
    if (tour.scriptStage == 0)
    {
        tour.start = hero.position;
        tour.scriptStage = 1;
    }
    tour.direction = {F32(static_cast<double>(tour.target.x) - static_cast<double>(tour.start.x)),
                      F32(static_cast<double>(tour.target.y) - static_cast<double>(tour.start.y)),
                      F32(static_cast<double>(tour.target.z) - static_cast<double>(tour.start.z))};
    const auto x = static_cast<double>(tour.direction.x);
    const auto y = static_cast<double>(tour.direction.y);
    const auto z = static_cast<double>(tour.direction.z);
    tour.distance = F32(std::sqrt((x * x + y * y) + z * z));
}

// 0x4034e0: con la cámara en camino, 0x403430, el rumbo normalizado y la posición en lo andado · rumbo + salida; lo
// andado suma +0x74. Con la norma de lo andado (de antes) en el largo o más, llega: la posición y la salida en el
// destino, +0x36 en 1, +0x35 y lo andado en 0. Devuelve si la cámara estaba en camino (el guion espera).
bool MoveTheCamera(CameraTour& tour, const ActorState& hero)
{
    if (!tour.moving)
        return false;
    MeasureTheLeg(tour, hero);
    tour.begin = false;
    Normalize(tour.direction);
    const auto travelled = static_cast<double>(tour.travelled);
    const double x = travelled * static_cast<double>(tour.direction.x);
    const double y = travelled * static_cast<double>(tour.direction.y);
    const double z = travelled * static_cast<double>(tour.direction.z);
    tour.position = {F32(x + static_cast<double>(tour.start.x)), F32(y + static_cast<double>(tour.start.y)),
                     F32(z + static_cast<double>(tour.start.z))};
    tour.travelled = F32(static_cast<double>(tour.speed) + travelled);
    // 0x403539..0x403559: (z² + y²) + x², y FCOMP con C0 (menor o NaN) sigue en camino.
    const double length = std::sqrt((z * z + y * y) + x * x);
    if (!(length >= static_cast<double>(tour.distance)))
        return true;
    tour.position = tour.target;
    tour.arrived = true;
    tour.moving = false;
    tour.travelled = 0.0f;
    tour.start = tour.target;
    return true;
}

// El lugar de una clave del guion en +0x38 y +0x78 (clave − 10000). Las claves de los guiones van de 10000 a 0x272b.
Core::Expected<std::size_t, std::string> SlotOf(std::int32_t key, const char* address)
{
    constexpr std::int32_t ScriptSlots = 50;
    const std::int32_t slot = key - FirstTourActor;
    if (slot < 0 || slot >= ScriptSlots)
        return Core::Unexpected(std::format("{}: la clave {} cae fuera de +0x38 y +0x78 de CCameraMove", address, key));
    return static_cast<std::size_t>(slot);
}

// 0x403912..0x403931 y 0x403770..0x403790: el primero vivo con la clave (MOVSX); sin él, el registro 399 (Q-C1349).
ActorState& ScriptActor(WorldState& world, std::int32_t key)
{
    auto actors = world.Actors();
    for (std::size_t index = 0; index <= LastRecord; ++index)
    {
        if (actors[index].alive && static_cast<std::int16_t>(actors[index].key) == key)
            return actors[index];
    }
    return actors[LastRecord];
}

// 0x403d10, la tabla del modelo de 0x4039f0 por tipo − 0x154; los demás tipos toman como modelo el dword de la bandera
// de las chispas (0x403a93, [EBP + 0x1c]). Ningún guion los usa.
std::uint16_t ScriptModelOf(std::int32_t type, bool sparks)
{
    switch (type)
    {
    case 0x154:
        return 0x1ae;
    case 0x155:
        return 0x1b0;
    case 0x158:
        return 0x1ad;
    case 0x159:
        return 0x1af;
    case 0x15c:
        return 0x1b5;
    case 0x15d:
        return 0x1ab;
    default:
        return sparks ? 1 : 0;
    }
}

class TourScript
{
public:
    TourScript(const CameraTourContext& context, CameraTour& tour) : m_context(context), m_tour(tour) {}

    // 0x4035f0: con la cámara en camino (0x4034e0) nada más; si no, la etapa del guion (+0x144, la tabla 0x403644).
    Result Step()
    {
        WorldState& world = m_context.world;
        if (MoveTheCamera(m_tour, world.Actors()[world.Runtime().hero]))
            return {};
        switch (m_tour.scriptStage)
        {
        case 0:
            // 0x403e00: el primer tramo sale del héroe (0x403430 con la etapa 0).
            HeadTo(m_tour, 0x71, 0xe8, 0, FastLeg);
            --m_tour.scriptStage;
            return {};
        case 1:
            return TheBarrier();
        case 2:
            return TheDefenders();
        case 3:
            return TheAltars();
        case 4:
            // 0x404b70.
            if (!m_tour.arrived)
                HeadTo(m_tour, 0x79, 0x57, 0, SlowLeg);
            else if (m_tour.step == 0)
                Wait(3000);
            else
                m_tour.arrived = false;
            return {};
        case 5:
            return TheEnd();
        default:
            return {};
        }
    }

private:
    void Wait(std::int32_t period)
    {
        static_cast<void>(TourTimerFires(m_tour, m_context.now, period));
    }

    // 0x4039f0(tipo, x, y, ángulo, avanza, chispas, velocidad): el alta 0x5141e0 con la clave +0x14c, que sube uno; la
    // clave en +0x1f4 (word), las manos (+0x2b8 y +0x2d8) en 0xffff, el ángulo en +0x24 y +0x174 en 0. Con la
    // velocidad en 0 o más (FCOMP sin C0), la de la acción 2 del modelo de 0x403d10 (+0x30 → +0x24). Con las chispas,
    // el sonido 0x243 y los joints y las partículas, que cortan. Con avanza, +0x140 uno más.
    Result Create(std::int32_t type, std::int32_t x, std::int32_t y, float angle, bool advance, bool sparks,
                  float speed)
    {
        const std::int32_t key = m_tour.nextActorKey;
        if (m_context.resources == nullptr)
            return Core::Unexpected(
                std::format("0x403a11: el alta 0x5141e0 del personaje {} del recorrido (tipo {:#x}) "
                            "sin los recursos de la entrada",
                            key, type));
        auto created =
            CreateMonster(type, static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), key, *m_context.resources);
        if (!created.HasValue())
            return Core::Unexpected(std::move(created).Error());
        auto actors = m_context.world.Actors();
        if (created.Value() >= actors.size())
            return Core::Unexpected(std::format("0x403a25: con los 400 personajes vivos, 0x5141e0 devuelve el registro "
                                                "400 y 0x4039f0 escribe la clave {} fuera del pool",
                                                key));
        ActorState& actor = actors[created.Value()];
        actor.key = static_cast<std::uint16_t>(key);
        ++m_tour.nextActorKey;
        constexpr std::uint16_t NoItem = 0xffff;
        actor.hands[0].model = NoItem;
        actor.hands[1].model = NoItem;
        actor.angles.z = angle;
        actor.alpha = 0.0f;
        if (speed >= 0.0f)
        {
            auto set = m_context.world.FaunaResources().SetActionSpeed(ScriptModelOf(type, sparks), WalkAction, speed,
                                                                       "0x403abe");
            if (!set.HasValue())
                return set;
        }
        if (sparks)
        {
            // 0x403ad5: 0x41ed10(0x243, 0, 0) (CL10).
            m_context.world.Runtime().lastSoundId = ArrivalSound;
            return Core::Unexpected(std::format(
                "0x403b09: las chispas del alta del personaje {} del recorrido (cuatro joints 0x615/7 con rand() % 10, "
                "tres partículas 0x5f6/1 y cuatro joints 0x614/0x11, de 0x530860 y 0x53fe70) no están portadas",
                key));
        }
        if (advance)
            ++m_tour.step;
        return {};
    }

    // 0x403760(clave, x, y, ángulo, velocidad): en la celda (ftol(ftol(posición) · 0,01f)), el centro de la celda,
    // +0x38 en 1, 0x4ea030(0) y verdadero. Si no, mientras gira (+0x38), el ángulo a la celda (0x4036d0) contra +0x24,
    // los dos truncados; con 3 o menos de diferencia, deja de girar. Con +0x38a, +0x386 con la velocidad,
    // 0x4ea030(2) y 0x508a80; si no, +0x24 ± 3 y 0x4ea030(0). Falso.
    Core::Expected<bool, std::string> Walk(std::int32_t key, std::int32_t x, std::int32_t y, float angle,
                                           std::uint16_t speed)
    {
        const auto slot = SlotOf(key, "0x4037d7");
        if (!slot.HasValue())
            return Core::Unexpected(slot.Error());
        ActorState& actor = ScriptActor(m_context.world, key);
        const auto cellOf = [](float position)
        { return World::FtolLow(static_cast<double>(World::FtolLow(static_cast<double>(position))) * Hundredth); };
        if (cellOf(actor.position.x) == x && cellOf(actor.position.y) == y)
        {
            // 0x4038b3..0x4038ed.
            actor.position.x = CellCenterOf(x);
            actor.position.y = CellCenterOf(y);
            m_tour.turning[slot.Value()] = true;
            if (auto stood = SetAction(actor, StandAction, 0x4038e5); !stood.HasValue())
                return Core::Unexpected(stood.Error());
            return true;
        }
        std::int32_t difference = 0;
        if (m_tour.turning[slot.Value()])
        {
            std::int32_t target = World::FtolLow(AngleTo(actor, x, y));
            const std::int32_t current = World::FtolLow(static_cast<double>(actor.angles.z));
            // 0x403808..0x40381d: FILD del ángulo truncado − el pedido, FCOMP 180 sin C0 ni C3.
            if (static_cast<double>(target) - static_cast<double>(angle) > HalfTurn)
                target -= static_cast<std::int32_t>(FullTurn);
            difference = target - current;
            actor.byte38a = 0;
            if (difference <= 3 && difference >= -3)
            {
                actor.byte38a = 1;
                m_tour.turning[slot.Value()] = false;
            }
        }
        else
        {
            // 0x403836: sin girar, camina.
            actor.byte38a = 1;
            m_tour.turning[slot.Value()] = false;
        }
        if (actor.byte38a != 0)
        {
            actor.baseMovementSpeed = speed;
            if (auto walked = SetAction(actor, WalkAction, 0x403860); !walked.HasValue())
                return Core::Unexpected(walked.Error());
            if (!m_context.height)
                return Core::Unexpected(std::format("0x403866: 0x508a80 mueve al personaje {} sin la altura", key));
            if (auto moved = UpdateActorPosition(actor, ActorMotionContext{.runtime = m_context.world.Runtime(),
                                                                           .random = m_context.random,
                                                                           .height = m_context.height});
                !moved.HasValue())
                return Core::Unexpected(moved.Error());
            return false;
        }
        const bool left = difference > 3 && difference <= static_cast<std::int32_t>(HalfTurn);
        actor.angles.z = F32(static_cast<double>(actor.angles.z) + static_cast<double>(left ? TurnStep : -TurnStep));
        if (auto stood = SetAction(actor, StandAction, 0x4038a0); !stood.HasValue())
            return Core::Unexpected(stood.Error());
        return false;
    }

    // 0x4036d0(personaje, x, y, ángulo): (posición − centro de la celda), con la x del centro sin redondear (queda en
    // la x87) y la y en float, z 0; normalizado (0x5fc800), acos de la y (0x69fc70) en grados y, con la x mayor que 0,
    // 360 − eso. El ángulo pedido no se lee.
    static double AngleTo(const ActorState& actor, std::int32_t x, std::int32_t y)
    {
        const double centerX = static_cast<double>(x) * CellSize + CellCenter;
        const float centerY = CellCenterOf(y);
        glm::vec3 toward{F32(static_cast<double>(actor.position.x) - centerX),
                         F32(static_cast<double>(actor.position.y) - static_cast<double>(centerY)), 0.0f};
        Normalize(toward);
        const double angle = std::acos(static_cast<double>(toward.y)) * Degrees;
        return toward.x > 0.0f ? FullTurn - angle : angle;
    }

    // 0x403900(clave, acción, veces, efecto, avanza): mientras +0x78 no llega a las veces, con otra acción en +0x111,
    // la pone (+0x114 en 0) y cuenta una vez; con el efecto, el 0xed (0x518cf0), que corta. Falso. Con las veces,
    // avanza pone +0x78 en 0 y +0x140 uno más; verdadero con +0x114 en 8 o más.
    Core::Expected<bool, std::string> Act(std::int32_t key, std::uint8_t action, std::int32_t times, bool effect,
                                          bool advance)
    {
        const auto slot = SlotOf(key, "0x403933");
        if (!slot.HasValue())
            return Core::Unexpected(slot.Error());
        ActorState& actor = ScriptActor(m_context.world, key);
        std::int32_t& count = m_tour.actionCounts[slot.Value()];
        if (count != times)
        {
            if (actor.animation.action != action)
            {
                actor.animation.frame = 0.0f;
                actor.animation.action = action;
                ++count;
                if (effect)
                    return Core::Unexpected(std::format("0x4039a0: el efecto 0xed (0x518cf0) del personaje {} del "
                                                        "recorrido es de la CL11",
                                                        key));
            }
            return false;
        }
        if (advance)
        {
            count = 0;
            ++m_tour.step;
        }
        return actor.animation.frame >= LastActionFrame;
    }

    Result SetAction(ActorState& actor, std::uint8_t action, std::uint32_t address)
    {
        if (m_context.resources == nullptr)
            return Core::Unexpected(std::format("{:#x}: 0x4ea030 sin los modelos de la entrada", address));
        return SetMonsterAction(actor, action, address, *m_context.resources);
    }

    // 0x403d40: +0x150 uno más, hasta 4.
    void RaiseTheLevel()
    {
        if (m_tour.level < LastLevel)
            ++m_tour.level;
    }

    // 0x403e30: la barrera y los guardias en (0x72, 0xee) y la fila de la 0xe3; después, al (0x72, 0xdc).
    Result TheBarrier()
    {
        if (!m_tour.arrived)
        {
            HeadTo(m_tour, 0x72, 0xdc, 0, SlowLeg);
            return {};
        }
        switch (m_tour.step)
        {
        case 0:
            return Create(0x15d, 0x72, 0xee, 0, true, true, BarrierSpeed);
        case 1:
            return Create(0x154, 0x71, 0xee, 0, true, true, GuardSpeed);
        case 2:
            return Create(0x154, 0x73, 0xee, 0, true, true, GuardSpeed);
        case 3:
        case 5:
        case 0xd:
        case 0x15:
        case 0x1d:
            Wait(1000);
            return {};
        case 4:
        {
            // 0x403ec0..0x403f0b: los tres caminan antes de mirar si llegaron.
            const auto first = Walk(10000, 0x72, 0xea, 0, 9);
            if (!first.HasValue())
                return Core::Unexpected(first.Error());
            const auto second = Walk(0x2711, 0x70, 0xe8, 0, 0xc);
            if (!second.HasValue())
                return Core::Unexpected(second.Error());
            const auto third = Walk(0x2712, 0x74, 0xe8, 0, 0xc);
            if (!third.HasValue())
                return Core::Unexpected(third.Error());
            if (first.Value() && second.Value() && third.Value())
                ++m_tour.step;
            return {};
        }
        case 6:
        case 0xe:
        {
            const bool first = m_tour.step == 6;
            const auto done = Act(first ? 0x2711 : 0x2712, 8, 1, false, false);
            if (!done.HasValue())
                return Core::Unexpected(done.Error());
            if (!done.Value())
                return {};
            return first ? Create(0x159, 0x6d, 0xe5, 0, true, true, KeepSpeed)
                         : Create(0x158, 0x72, 0xe5, 0, true, true, KeepSpeed);
        }
        case 7:
        case 9:
        case 0xb:
        case 0xf:
        case 0x11:
        case 0x13:
        case 0x17:
        case 0x19:
        case 0x1b:
            Wait(0x294);
            return {};
        case 8:
            return Create(0x159, 0x6e, 0xe6, 0, true, true, KeepSpeed);
        case 0xa:
            return Create(0x159, 0x70, 0xe6, 0, true, true, KeepSpeed);
        case 0xc:
            return Create(0x159, 0x71, 0xe5, 0, true, true, KeepSpeed);
        case 0x10:
            return Create(0x158, 0x73, 0xe6, 0, true, true, KeepSpeed);
        case 0x12:
            return Create(0x158, 0x75, 0xe6, 0, true, true, KeepSpeed);
        case 0x14:
            return Create(0x158, 0x76, 0xe5, 0, true, true, KeepSpeed);
        case 0x16:
        case 0x18:
        case 0x1a:
        case 0x1c:
        {
            // 0x404240..0x40426d: la fila de la 0xe3 en 0x6e, 0x70, 0x72 y 0x74, y 0x403d40.
            const std::int32_t x = 0x6e + (m_tour.step - 0x16);
            if (auto created = Create(0x155, x, 0xe3, 0, true, true, KeepSpeed); !created.HasValue())
                return created;
            RaiseTheLevel();
            return {};
        }
        case 0x1e:
            return Discard(Act(10000, 8, 1, false, true));
        case 0x1f:
            Wait(2000);
            return {};
        default:
            m_tour.arrived = false;
            return {};
        }
    }

    // 0x4042c0: los defensores en las filas 0xf0..0xf4, que caminan a las de 0xd9..0xe7; después, al (0x72, 0xa0).
    Result TheDefenders()
    {
        if (!m_tour.arrived)
        {
            HeadTo(m_tour, 0x72, 0xa0, 0, FastLeg);
            return {};
        }
        switch (m_tour.step)
        {
        case 0:
        {
            struct Spawn
            {
                std::int32_t type;
                std::int32_t x;
                std::int32_t y;
            };
            constexpr std::array<Spawn, 13> Spawns{{{0x15c, 0x6e, 0xf0},
                                                    {0x15c, 0x72, 0xf0},
                                                    {0x15c, 0x76, 0xf0},
                                                    {0x155, 0x6e, 0xf2},
                                                    {0x155, 0x70, 0xf2},
                                                    {0x155, 0x72, 0xf2},
                                                    {0x155, 0x74, 0xf2},
                                                    {0x155, 0x76, 0xf2},
                                                    {0x155, 0x6e, 0xf4},
                                                    {0x155, 0x70, 0xf4},
                                                    {0x155, 0x72, 0xf4},
                                                    {0x155, 0x74, 0xf4},
                                                    {0x155, 0x76, 0xf4}}};
            for (std::size_t index = 0; index < Spawns.size(); ++index)
            {
                // El último avanza y lleva las chispas.
                const bool last = index + 1 == Spawns.size();
                const Spawn& spawn = Spawns[index];
                if (auto created = Create(spawn.type, spawn.x, spawn.y, 0, last, last, KeepSpeed); !created.HasValue())
                    return created;
            }
            return {};
        }
        case 1:
        case 3:
            Wait(1000);
            return {};
        case 2:
        {
            struct Goal
            {
                std::int32_t key;
                std::int32_t x;
                std::int32_t y;
                std::uint16_t speed;
            };
            constexpr std::array<Goal, 28> Goals{
                {{10000, 0x72, 0xde, 9},    {0x2711, 0x6e, 0xde, 0xc}, {0x2712, 0x76, 0xde, 0xc},
                 {0x2713, 0x6b, 0xdb, 0xc}, {0x2714, 0x6c, 0xdc, 0xc}, {0x2715, 0x6e, 0xdc, 0xc},
                 {0x2716, 0x6f, 0xdb, 0xc}, {0x2717, 0x74, 0xdb, 0xc}, {0x2718, 0x75, 0xdc, 0xc},
                 {0x2719, 0x77, 0xdc, 0xc}, {0x271a, 0x78, 0xdb, 0xc}, {0x271b, 0x6e, 0xd9, 0xc},
                 {0x271c, 0x70, 0xd9, 0xc}, {0x271d, 0x72, 0xd9, 0xc}, {0x271e, 0x74, 0xd9, 0xc},
                 {0x271f, 0x6e, 0xe3, 0xc}, {0x2720, 0x72, 0xe3, 0xc}, {0x2721, 0x76, 0xe3, 0xc},
                 {0x2722, 0x6e, 0xe5, 0xc}, {0x2723, 0x70, 0xe5, 0xc}, {0x2724, 0x72, 0xe5, 0xc},
                 {0x2725, 0x74, 0xe5, 0xc}, {0x2726, 0x76, 0xe5, 0xc}, {0x2727, 0x6e, 0xe7, 0xc},
                 {0x2728, 0x70, 0xe7, 0xc}, {0x2729, 0x72, 0xe7, 0xc}, {0x272a, 0x74, 0xe7, 0xc},
                 {0x272b, 0x76, 0xe7, 0xc}}};
            // Los 28 caminan antes de mirar si llegaron todos.
            bool all = true;
            for (const Goal& goal : Goals)
            {
                const auto arrived = Walk(goal.key, goal.x, goal.y, 0, goal.speed);
                if (!arrived.HasValue())
                    return Core::Unexpected(arrived.Error());
                all = all && arrived.Value();
            }
            if (all)
                ++m_tour.step;
            return {};
        }
        case 4:
            return Discard(Act(10000, 8, 1, false, true));
        case 5:
            Wait(0xa8c);
            return {};
        case 6:
            return Discard(Act(10000, 9, 1, false, true));
        case 7:
            Wait(3000);
            return {};
        default:
            m_tour.arrived = false;
            return {};
        }
    }

    // 0x404830: borra los del guion (0x403d60) y vuelve a la clave 10000; los 27 de los altares, sin chispas; después,
    // al (0x79, 0x4b). Dos de las altas avanzan: +0x140 pasa de 1 a 3.
    Result TheAltars()
    {
        if (!m_tour.arrived)
        {
            HeadTo(m_tour, 0x79, 0x4b, 0, FastLeg);
            return {};
        }
        if (m_tour.step == 0)
        {
            RemoveTheCameraTourActors(m_context.world, m_tour);
            m_tour.nextActorKey = FirstTourActor;
            ++m_tour.step;
            return {};
        }
        if (m_tour.step != 1)
        {
            m_tour.arrived = false;
            return {};
        }
        struct Spawn
        {
            std::int32_t type;
            std::int32_t x;
            std::int32_t y;
            bool advance;
        };
        constexpr std::array<Spawn, 27> Spawns{
            {{0x154, 0x6e, 0x4d, false}, {0x154, 0x7d, 0x4d, false}, {0x155, 0x5a, 0x25, false},
             {0x155, 0x6c, 0x49, false}, {0x155, 0x6d, 0x4b, false}, {0x155, 0x6e, 0x49, false},
             {0x155, 0x6f, 0x4b, false}, {0x155, 0x70, 0x49, false}, {0x155, 0x7b, 0x49, false},
             {0x155, 0x7c, 0x4b, false}, {0x155, 0x7d, 0x49, false}, {0x155, 0x7e, 0x4b, false},
             {0x155, 0x7f, 0x49, false}, {0x155, 0xb0, 0x14, false}, {0x155, 0x75, 0x4d, false},
             {0x155, 0x77, 0x4d, false}, {0x155, 0x79, 0x4d, true},  {0x154, 0x77, 0x53, false},
             {0x158, 0x76, 0x4f, false}, {0x158, 0x78, 0x4f, false}, {0x158, 0x77, 0x5a, false},
             {0x159, 0x74, 0x51, false}, {0x159, 0x77, 0x51, false}, {0x159, 0x7a, 0x51, false},
             {0x154, 0x77, 0x57, false}, {0x15c, 0x74, 0x5a, false}, {0x15c, 0x7a, 0x5a, true}}};
        for (const Spawn& spawn : Spawns)
        {
            if (auto created = Create(spawn.type, spawn.x, spawn.y, 0, spawn.advance, false, KeepSpeed);
                !created.HasValue())
                return created;
        }
        return {};
    }

    // 0x404bb0: los dos últimos de los altares cambian a la acción 3 con el 0xed; después, el fin: 0x403d60 y +0x158
    // en 1.
    Result TheEnd()
    {
        if (!m_tour.arrived)
        {
            RemoveTheCameraTourActors(m_context.world, m_tour);
            m_tour.skipped = true;
            return {};
        }
        switch (m_tour.step)
        {
        case 0:
            Wait(2000);
            return {};
        case 1:
            if (auto acted = Discard(Act(0x2729, 3, 1, true, false)); !acted.HasValue())
                return acted;
            return Discard(Act(0x272a, 3, 1, true, true));
        case 2:
            Wait(4000);
            return {};
        default:
            m_tour.arrived = false;
            return {};
        }
    }

    static Result Discard(const Core::Expected<bool, std::string>& done)
    {
        if (!done.HasValue())
            return Core::Unexpected(done.Error());
        return {};
    }

    const CameraTourContext& m_context;
    CameraTour& m_tour;
};
} // namespace

bool CameraTourRunning(const CameraTour& tour)
{
    return !tour.skipped && !tour.waiting && tour.stage == TourStage;
}

void ResetTheCameraTour(CameraTour& tour)
{
    tour = CameraTour{};
}

void RemoveTheCameraTourActors(WorldState& world, const CameraTour& tour)
{
    for (std::int32_t key = FirstTourActor; key < tour.nextActorKey; ++key)
        RemoveActor(world, key);
}

Core::Expected<void, std::string> StepTheCameraTour(const CameraTourContext& context)
{
    CameraTour& tour = context.world.Runtime().events.cameraTour;
    // 0x4033f0.
    if (tour.stage == TourStage && tour.waiting && TourTimerFires(tour, context.now, TourDelay))
        tour.waiting = false;
    if (tour.stage == UndoStage)
    {
        RemoveTheCameraTourActors(context.world, tour);
        ResetTheCameraTour(tour);
    }
    if (!CameraTourRunning(tour))
        return {};
    // 0x4035a3..0x4035da: la etapa es 2 (0x4033d0 lo pide), así que la rama de la 5 (0x403660) y la de otra
    // (0x403d60 y 0x403300) no se alcanzan. Primero 0x403390.
    if (!context.releaseTheUi)
        return Core::Unexpected(std::string("0x4035d3: 0x403390 (0x5be150 y 0x657880(0x1b)) sin la UI del juego"));
    if (auto released = context.releaseTheUi(); !released.HasValue())
        return released;
    return TourScript(context, tour).Step();
}
} // namespace Client::Scenes
