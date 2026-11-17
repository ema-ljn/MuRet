#include "Scenes/DarkSpirit.h"

#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"
#include "World/Particles.h"
#include "World/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Client::Scenes
{
namespace
{
// 0x4167b0: los rand() de los cierres de las celdas protegidas (0x4128e7, ocho) y del contador (0x40b9a1,
// cuatro), y la altura sobre el terreno (0x6b8900).
constexpr int CellSize = 100;
constexpr int CellCenter = 50;
constexpr int CellMask = 0xff;
constexpr double BirthHeight = 300.0;

constexpr std::uint32_t OwnerStops = 0x1000;
constexpr std::uint32_t OwnerHidesSparks = 0x4000;
// 0x416480: el avance de la animación, y la acción de cada estado al dar la vuelta (0x417f0c).
constexpr float FrameStep = 0.4f;
constexpr std::array<std::uint8_t, 6> ActionOfState{1, 0, 3, 2, 1, 3};
// 0x417b87: la chispa, de un hueso de los 66 primeros, con su luz, su subtipo y su escala.
constexpr int SparkBones = 66;
constexpr glm::vec3 SparkLight{std::bit_cast<float>(0x3E99999Au), std::bit_cast<float>(0x3ECCCCCDu),
                               std::bit_cast<float>(0x3F333333u)};
constexpr float SparkScale = std::bit_cast<float>(0x3F4CCCCDu);
// 150² (0x6b8938): más cerca no gira hacia el dueño y el sorteo es el de cerca; 640², lejos.
constexpr double NearSquared = 22500.0;
constexpr float NearSquaredFloat = 22500.0f;
constexpr double FarSquared = 409600.0;
constexpr int TurnSpan = 15;
constexpr double TurnBase = 5.0;
// 0x417c95: la altura de vuelo sobre el dueño, 350 en el mapa 0x37 y 250 en los demás (Q-C131), y el
// margen de arriba.
constexpr std::uint8_t LoginMap = 0x37;
// 0x417c95: compara el word de la mascota con 0x1a04, el índice del caballo oscuro (13, 4) y no su modelo
// (0x1c16): con el equipo de 0x512a30 no da nunca (Q-C413).
constexpr std::uint16_t HorseIndex = 0x1a04;
constexpr double LoginFlight = 350.0;
constexpr double Flight = 250.0;
constexpr double FlightBand = 100.0;
constexpr double PitchStep = 2.0;
constexpr float PitchLimit = 15.0f;
constexpr int LowDraw = 10;
constexpr int HighDraw = 20;
constexpr int CruiseDraw = 28;
constexpr int NearSpan = 8;
constexpr int NearBase = 32;
constexpr int NearTurnSpan = 60;
constexpr int FarSpan = 64;
constexpr int FarBase = 128;
constexpr int ClimbSpan = 64;
constexpr int ClimbCenter = 32;
constexpr double Tenth = static_cast<double>(0.1f);
constexpr double MinusTenth = static_cast<double>(-0.1f);
constexpr double Half = 0.5;
constexpr float FastBelow = -12.0f;
constexpr int ShoutSpan = 100;
constexpr int ShoutCounter = 60;
constexpr double Below = 200.0;
constexpr int FarSteps = 90;
constexpr double JumpHeight = 250.0;
// 0x418301: el trapecio con su lugar por 0,01f (0x6b88d4) y −20 (0xc1a00000); sin pasarlo, la velocidad
// en 3 (0x40400000) salvo en el estado 4.
constexpr double CellsPerUnit = static_cast<double>(0.01f);
constexpr double ViewMargin = -20.0;
constexpr float UnseenSpeed = 3.0f;
constexpr std::uint8_t FastState = 0;
constexpr std::uint8_t SlowState = 1;
constexpr std::uint8_t PerchedState = 3;
constexpr std::uint8_t LandingState = 4;
// 0x4176f0: la zona segura es +0x3e2 en 1 justo; la velocidad al empezar a bajar (0x40400000).
constexpr std::uint8_t InSafeZone = 1;
constexpr float LandingSpeed = 3.0f;
// 0x4178a6: posado, el hueso 37 (0x6f0 / 0x30) del dueño con (−10, 0, 10) (0xc1200000, 0x41200000), y el
// rumbo del dueño menos 120 (0x6b86c8).
constexpr std::size_t PerchBone = 37;
constexpr glm::vec3 PerchOffset{-10.0f, 0.0f, 10.0f};
constexpr double PerchTurn = 120.0;
// 0x417974: bajando, hacia el hueso 42 (0x7e0 / 0x30) del dueño; acelera 1 (0x6b87d4) y se posa a menos
// de 50 (0x6b86b0).
constexpr std::size_t LandingBone = 42;
constexpr double LandingAcceleration = 1.0;
constexpr double PerchReach = 50.0;
// Los estados del ataque (CL9 hito 4): el 2 vuelve, el 5 ataca y el 6 grita (0x416ae3, 0x417833).
constexpr std::uint8_t ReturningState = 2;
constexpr std::uint8_t AttackingState = 5;
constexpr std::uint8_t ShoutingState = 6;
// 0x416b1d: +0x3b0 en 0xffff después del aviso; el 6 no gira con él (0x41784e).
constexpr std::uint16_t NoAttackTarget = 0xffff;
// 0x417a59: el destino del ataque, 50 (0x6b86b0) más arriba; a menos de 20 (0x6b86d0) o con la lejanía en más de 20
// vuelve (0x417a84), con el cabeceo en −45 (0xc2340000); la orden 3 deja +0x1ad (0x417a9d).
constexpr double AttackLift = 50.0;
constexpr double AttackReach = 20.0;
constexpr std::int32_t AttackSteps = 20;
constexpr float ReturnPitch = -45.0f;
constexpr std::uint8_t FollowTargetOrder = 3;
// 0x417acf: +0xf0 sube 0,2 (0x6b8854) por paso; 0x417b17: volviendo frena 1 (0x6b87d4); a 250² (0x6b893c) del
// destino, el estado 0.
constexpr double AttackAccelerationStep = static_cast<double>(0.2f);
constexpr double ReturnBrake = 1.0;
constexpr double ReturnedSquared = 62500.0;
constexpr std::uint8_t AttackAction = 3;
// 0x41788b: el 6 gira 40 (0x42200000) hacia el personaje de +0x3b0.
constexpr float ShoutTurn = 40.0f;
// 0x417e33: +0x38d sube en los estados 5 y 6 hasta 15.
constexpr std::uint8_t AttackCounterTop = 15;

float F32(double value)
{
    return static_cast<float>(value);
}

// 0x4164f0: el estado y el contador de lejanía en 0, sin mirar el de antes.
void SetState(DarkSpirit& spirit, std::uint8_t state)
{
    spirit.state = state;
    spirit.far = 0;
}

void SetStateOf(DarkSpirit& spirit, std::uint8_t state)
{
    if (spirit.state != state)
    {
        SetState(spirit, state);
    }
}

// 0x4ec110(modelo del dueño, huesos propios + hueso · 0x30, offset, punto, 1): el hueso por la escala y
// más el origen que tiene el modelo compartido. Sin huesos (antes del primer dibujo), la matriz en cero,
// como OwnBonePoint (Q-C318).
glm::vec3 OwnerBonePoint(const DarkSpiritOwner& owner, std::size_t bone, const glm::vec3& offset)
{
    const Render::Models::BoneTransform matrix =
        bone < owner.pose.size() ? owner.pose[bone] : Render::Models::BoneTransform(0.0f);
    return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset, owner.modelScale,
                                                     owner.modelOrigin);
}

// 0x4176f0..0x417775: dentro de la zona segura, sin estar posado ni bajando, 0x4164f0(3) y después 0x4164f0(4)
// con la velocidad en 3: la comparación con 50 de la distancia al dueño lee el estado ya en 3 y no decide
// nada (Q-C414). Fuera, posado o bajando, 0x4164f0(0).
void FollowTheSafeZone(DarkSpirit& spirit, const DarkSpiritOwner& owner)
{
    const bool resting = spirit.state == PerchedState || spirit.state == LandingState;
    if (owner.safeZone == InSafeZone)
    {
        if (!resting)
        {
            SetState(spirit, LandingState);
            spirit.speed = LandingSpeed;
        }
        return;
    }
    if (resting)
    {
        SetState(spirit, FastState);
    }
}

// 0x4178a6..0x4178ec: en el hombro del dueño, con sus ángulos y el rumbo 120 grados menos.
void Perch(DarkSpirit& spirit, const DarkSpiritOwner& owner)
{
    spirit.position = OwnerBonePoint(owner, PerchBone, PerchOffset);
    spirit.angles.x = owner.angles.x;
    spirit.angles.y = owner.angles.y;
    spirit.angles.z = F32(static_cast<double>(owner.angles.z) - PerchTurn);
}

// 0x4178fc..0x4179ea: la matriz de sus ángulos antes de girar; con velocidad (fcomp con 0 y C3: ni 0 ni
// unordered), avanza (0, −velocidad, 0) girado; gira hacia el hueso 42 con 0x4e9810 y acelera; con la
// distancia de la x87 menor que 50 (o unordered), 0x4164f0(3).
void Land(DarkSpirit& spirit, const DarkSpiritOwner& owner)
{
    const Render::Models::OriginalMatrix matrix = Render::Models::OriginalAngleMatrix(spirit.angles);
    if (!(spirit.speed == 0.0f || std::isnan(spirit.speed)))
    {
        const glm::vec3 move = Render::Models::OriginalVectorRotate(glm::vec3(0.0f, -spirit.speed, 0.0f), matrix);
        spirit.position.x = F32(static_cast<double>(move.x) + static_cast<double>(spirit.position.x));
        spirit.position.y = F32(static_cast<double>(move.y) + static_cast<double>(spirit.position.y));
        spirit.position.z = F32(static_cast<double>(move.z) + static_cast<double>(spirit.position.z));
    }
    const glm::vec3 shoulder = OwnerBonePoint(owner, LandingBone, glm::vec3(0.0f));
    const double distance = Render::Models::MoveToward(spirit.position, spirit.angles, shoulder, spirit.speed);
    spirit.speed = F32(static_cast<double>(spirit.speed) + LandingAcceleration);
    if (!(distance >= PerchReach))
    {
        SetState(spirit, PerchedState);
    }
}

// 0x417b33..0x417e10: los estados 0 y 1, el vuelo libre hacia `destination` (el dueño o, con +0x1ad en 1, el blanco
// de +8). +0x1ad en 0; la chispa mira las banderas del dueño y la altura, su mascota.
void Fly(DarkSpirit& spirit, const DarkSpiritOwner& owner, const glm::vec3& destination, std::uint8_t map,
         World::Particles& particles, float worldClock, Render::Models::CrtRandom& random)
{
    spirit.byte1ad = 0;
    const glm::vec3 start = spirit.position;
    const float dx = F32(static_cast<double>(destination.x) - static_cast<double>(start.x));
    const float dy = F32(static_cast<double>(destination.y) - static_cast<double>(start.y));
    // 0x417b75: la chispa, desde la traslación de un hueso de la pose del último dibujo (0x4ec110 sin
    // trasladar). Antes del primero, el original lee memoria sin iniciar: MuRet, la posición (Q-C133).
    if ((owner.flags & OwnerHidesSparks) == 0)
    {
        const auto bone = static_cast<std::size_t>(random.Next() % SparkBones);
        const glm::vec3 from = bone < spirit.pose.size()
                                   ? Render::Models::OriginalVectorTransform(glm::vec3(0.0f), spirit.pose[bone])
                                   : spirit.position;
        (void)particles.Create(World::SparkParticle, from, spirit.angles, SparkLight, World::RavenSparkSubtype,
                               SparkScale, worldClock, random);
    }
    // 0x417bcb: la distancia² en la x87 (fst la guarda en float y compara la de 53 bits).
    const double distance =
        (static_cast<double>(dy) * static_cast<double>(dy)) + (static_cast<double>(dx) * static_cast<double>(dx));
    const float distanceFloat = F32(distance);
    if (distance >= NearSquared)
    {
        const float target = F32(Render::Models::CreateAngle(start.x, start.y, destination.x, destination.y));
        const int turn = random.Next() % TurnSpan;
        spirit.angles.z =
            F32(Render::Models::TurnAngle(spirit.angles.z, target, F32(static_cast<double>(turn) + TurnBase)));
    }
    const glm::vec3 move =
        Render::Models::OriginalVectorRotate(spirit.direction, Render::Models::OriginalAngleMatrix(spirit.angles));
    spirit.position.x = F32(static_cast<double>(move.x) + static_cast<double>(spirit.position.x));
    spirit.position.y = F32(static_cast<double>(move.y) + static_cast<double>(spirit.position.y));
    const double z = static_cast<double>(move.z) + static_cast<double>(spirit.position.z);
    spirit.position.z = F32(z);
    const double flight =
        ((owner.pet == HorseIndex || map == LoginMap) ? LoginFlight : Flight) + static_cast<double>(destination.z);
    int draw = CruiseDraw;
    if (z < flight)
    {
        spirit.angles.x = F32(static_cast<double>(spirit.angles.x) - PitchStep);
        draw = LowDraw;
        if (spirit.angles.x < -PitchLimit)
        {
            spirit.angles.x = -PitchLimit;
        }
    }
    else if (z > flight + FlightBand)
    {
        spirit.angles.x = F32(static_cast<double>(spirit.angles.x) + PitchStep);
        draw = HighDraw;
        if (spirit.angles.x > PitchLimit)
        {
            spirit.angles.x = PitchLimit;
        }
    }
    // 0x417d25: el sorteo de la dirección. Cerca (con la distancia² en float), el empujón de rand() & 7 y
    // un giro de rand() % 60; lejos, el de rand() & 63, sin redondear.
    if (random.Next() % draw == 0)
    {
        double push = 0.0;
        if (distanceFloat < NearSquaredFloat)
        {
            push = static_cast<double>(F32(static_cast<double>((random.Next() % NearSpan) + NearBase) * MinusTenth));
            spirit.angles.z =
                F32(static_cast<double>(random.Next() % NearTurnSpan) + static_cast<double>(spirit.angles.z));
        }
        else
        {
            push = static_cast<double>((random.Next() % FarSpan) + FarBase) * MinusTenth;
        }
        spirit.direction.x = 0.0f;
        spirit.direction.y = F32((push + static_cast<double>(spirit.direction.y)) * Half);
        spirit.direction.z = F32(static_cast<double>((random.Next() % ClimbSpan) - ClimbCenter) * Tenth);
    }
    SetStateOf(spirit, spirit.direction.y < FastBelow ? FastState : SlowState);
}

// 0x416a88..0x4176e6: con el blanco de +0x3b0 entre los 400 y con 0x4000 (0x416ab6), o con el dueño con 0x4000
// (0x416ac5, 0x4170d9), en los estados 5, 2 y 6, +0x3b0 en 0xffff y el A7 (las dos copias, 0x416b05 y 0x417113, arman
// el mismo). Después 0x4164f0(3) y, a más de 50 del dueño (0x4170a9, 0x4176bf) o con el estado ya en 3, que no es 4
// ni pasa de 3, 0x4164f0(4) con la velocidad en 3: siempre el 4 (Q-C1206). La distancia no saca rand().
bool StopTheHiddenAttack(DarkSpirit& spirit, const DarkSpiritOwner& owner, const DarkSpiritTargets& targets)
{
    const bool targetHides = targets.attackInPool && (targets.attackFlags & OwnerHidesSparks) == OwnerHidesSparks;
    const bool ownerHides = (owner.flags & OwnerHidesSparks) == OwnerHidesSparks;
    const bool attacking =
        spirit.state == AttackingState || spirit.state == ReturningState || spirit.state == ShoutingState;
    if (!(targetHides || ownerHides) || !attacking)
    {
        return false;
    }
    spirit.target3b0 = NoAttackTarget;
    SetState(spirit, PerchedState);
    SetState(spirit, LandingState);
    spirit.speed = LandingSpeed;
    return true;
}

// 0x4179ef..0x417b2e: el 5 y el 2. La matriz de los ángulos (0x5fc8f0) y el avance de (0, −velocidad, 0) girado, sin
// mirar si la velocidad es 0. `dx` y `dy` son los del destino antes de avanzar (0x417814).
void Charge(DarkSpirit& spirit, const Render::Models::Skeleton& skeleton, glm::vec3 destination, float dx, float dy)
{
    const Render::Models::OriginalMatrix matrix = Render::Models::OriginalAngleMatrix(spirit.angles);
    const glm::vec3 move = Render::Models::OriginalVectorRotate(glm::vec3(0.0f, -spirit.speed, 0.0f), matrix);
    spirit.position.x = F32(static_cast<double>(move.x) + static_cast<double>(spirit.position.x));
    spirit.position.y = F32(static_cast<double>(move.y) + static_cast<double>(spirit.position.y));
    spirit.position.z = F32(static_cast<double>(move.z) + static_cast<double>(spirit.position.z));
    if (spirit.state == AttackingState)
    {
        // 0x417a56..0x417aa6: fcomp con 20 y C0 (menor o unordered).
        destination.z = F32(static_cast<double>(destination.z) + AttackLift);
        const double distance = Render::Models::MoveToward(spirit.position, spirit.angles, destination, spirit.speed);
        if (!(distance >= AttackReach) || spirit.far > AttackSteps)
        {
            SetState(spirit, ReturningState);
            spirit.angles.x = ReturnPitch;
            if (spirit.order != FollowTargetOrder)
            {
                spirit.byte1ad = 0;
            }
        }
        // 0x417aad..0x417ad5: la velocidad suma el +0xf0 de antes.
        ++spirit.far;
        spirit.speed = F32(static_cast<double>(spirit.speed) + static_cast<double>(spirit.floatF0));
        spirit.floatF0 = F32(static_cast<double>(spirit.floatF0) + AttackAccelerationStep);
    }
    else
    {
        // 0x417aec..0x417b1d: fcomp con 250² y C0: con la suma en 250² o más (no unordered), 0x4164f0(0).
        const double squared =
            (static_cast<double>(dy) * static_cast<double>(dy)) + (static_cast<double>(dx) * static_cast<double>(dx));
        if (!(squared < ReturnedSquared || std::isnan(squared)))
        {
            SetState(spirit, FastState);
        }
        spirit.speed = F32(static_cast<double>(spirit.speed) - ReturnBrake);
    }
    // 0x417adb y 0x417b26: 0x4ea030(3).
    Render::Models::SetAction(spirit.animation, skeleton, AttackAction);
}

// 0x417847..0x41789d: el 6, con +0x3b0 distinto de 0xffff, gira 40 hacia ese personaje (0x4e93f0 desde su posición
// de antes del paso y 0x4e9560).
void Shout(DarkSpirit& spirit, const DarkSpiritTargets& targets)
{
    if (spirit.target3b0 == NoAttackTarget)
    {
        return;
    }
    const float angle = F32(Render::Models::CreateAngle(spirit.position.x, spirit.position.y, targets.attackPosition.x,
                                                        targets.attackPosition.y));
    spirit.angles.z = F32(Render::Models::TurnAngle(spirit.angles.z, angle, ShoutTurn));
}
} // namespace

DarkSpirit CreateDarkSpirit(int cellX, int cellY, double groundHeight, Render::Models::CrtRandom& random,
                            DarkSpiritBirthAccesses accesses)
{
    // 0x4167b0 lee Y y X del dueño, y 0x415be0 el contador: solo gastan si cierran en cero.
    const std::array closes{accesses.cellY == 0, accesses.cellX == 0, accesses.counter == 0};
    for (const bool closesInZero : closes)
    {
        if (!closesInZero)
        {
            continue;
        }
        for (std::uint16_t k = 0; k < Render::Models::SmallLogicalCloseRandoms; ++k)
        {
            static_cast<void>(random.Next());
        }
    }
    DarkSpirit spirit;
    // 0x415c25: las celdas con `and 0xff`; 0x415ea7 guarda la altura del terreno en float y 0x4169ec le
    // suma 300 desde ahí.
    const int x = cellX & CellMask;
    const int y = cellY & CellMask;
    spirit.position =
        glm::vec3(static_cast<float>((x * CellSize) + CellCenter), static_cast<float>((y * CellSize) + CellCenter),
                  F32(static_cast<double>(F32(groundHeight)) + BirthHeight));
    spirit.previousPosition = spirit.position;
    return spirit;
}

bool DarkSpiritSeen(const DarkSpirit& spirit, const World::TerrainPolygon& polygon)
{
    const auto cells = [](float coordinate)
    { return static_cast<double>(F32(static_cast<double>(coordinate) * CellsPerUnit)); };
    return World::PolygonContains(polygon, cells(spirit.position.x), cells(spirit.position.y), ViewMargin);
}

void MissTheView(DarkSpirit& spirit)
{
    spirit.speed = UnseenSpeed;
    if (spirit.state != LandingState)
    {
        spirit.state = 0;
    }
}

DrawnObject DrawnDarkSpirit(const DarkSpirit& spirit, const Render::Models::Skeleton* skeleton,
                            const glm::vec3& terrainLight, float stepAlpha)
{
    DrawnObject drawn;
    drawn.model = DarkSpiritModel;
    drawn.animation = spirit.animation;
    if (skeleton != nullptr && spirit.animation.action < skeleton->actions.size())
    {
        drawn.animation = Render::Models::Interpolated(spirit.animation, skeleton->actions[spirit.animation.action],
                                                       FrameStep, stepAlpha);
    }
    drawn.transform.position = spirit.position + ((spirit.position - spirit.previousPosition) * stepAlpha);
    drawn.transform.angles = spirit.angles;
    drawn.transform.scale = spirit.scale;
    drawn.color = terrainLight + spirit.light;
    drawn.alpha = spirit.alpha;
    drawn.vertexLight = true;
    return drawn;
}

DarkSpiritStepped StepDarkSpirit(DarkSpirit& spirit, const Render::Models::Skeleton& skeleton,
                                 const DarkSpiritOwner& owner, std::uint8_t map, World::Particles& particles,
                                 float worldClock, Render::Models::CrtRandom& random, std::int32_t counter,
                                 const DarkSpiritTargets& targets)
{
    spirit.previousPosition = spirit.position;
    DarkSpiritStepped stepped;
    if ((owner.flags & OwnerStops) != 0)
    {
        return stepped;
    }
    // 0x416a82..0x416a8b: el byte +0x1d6 del CHARACTER del cuervo va a +0x8e; MuRet no tiene ninguno de los dos.
    stepped.stopsTheAttack = StopTheHiddenAttack(spirit, owner, targets);
    FollowTheSafeZone(spirit, owner);
    if (spirit.animation.action < skeleton.actions.size() &&
        Render::Models::AdvanceAnimation(spirit.animation, skeleton.actions[spirit.animation.action], FrameStep,
                                         Render::Models::AnimationScene::Other))
    {
        // 0x417791: más allá de la tabla, la 0.
        Render::Models::SetAction(spirit.animation, skeleton,
                                  spirit.state < ActionOfState.size() ? ActionOfState[spirit.state] : 0);
    }
    // 0x4177b1..0x417822: el destino, el blanco de +8 con +0x1ad en 1 o el dueño, y la distancia en float antes de
    // moverse.
    const glm::vec3 destination = spirit.byte1ad == 1 ? targets.target : owner.position;
    const float dx = F32(static_cast<double>(destination.x) - static_cast<double>(spirit.position.x));
    const float dy = F32(static_cast<double>(destination.y) - static_cast<double>(spirit.position.y));
    if (spirit.state == FastState || spirit.state == SlowState)
    {
        Fly(spirit, owner, destination, map, particles, worldClock, random);
    }
    else if (spirit.state == AttackingState || spirit.state == ReturningState)
    {
        Charge(spirit, skeleton, destination, dx, dy);
    }
    else if (spirit.state == ShoutingState)
    {
        Shout(spirit, targets);
    }
    else if (spirit.state == PerchedState)
    {
        Perch(spirit, owner);
    }
    else if (spirit.state == LandingState)
    {
        Land(spirit, owner);
    }
    // 0x417e15..0x417e37: en el 5 y el 6, +0x38d sube (con la vuelta del byte) y se queda en 15.
    if (spirit.state == AttackingState || spirit.state == ShoutingState)
    {
        ++spirit.byte38d;
        if (spirit.byte38d >= AttackCounterTop)
        {
            spirit.byte38d = AttackCounterTop;
        }
    }
    // 0x417e3e: el grito, con rand() % 100 y el contador que lee de 0x7d3a0f0, con signo.
    stepped.shouts = random.Next() % ShoutSpan == 0 && counter % ShoutCounter == 0;
    const double farX = static_cast<double>(owner.position.x) - static_cast<double>(spirit.position.x);
    const double farY = static_cast<double>(owner.position.y) - static_cast<double>(spirit.position.y);
    if (static_cast<double>(owner.position.z) - Below > static_cast<double>(spirit.position.z) ||
        (farY * farY) + (farX * farX) > FarSquared)
    {
        ++spirit.far;
    }
    if (spirit.far > FarSteps)
    {
        spirit.position =
            glm::vec3(owner.position.x, owner.position.y, F32(static_cast<double>(owner.position.z) + JumpHeight));
        spirit.previousPosition = spirit.position;
        spirit.far = 0;
    }
    return stepped;
}
} // namespace Client::Scenes
