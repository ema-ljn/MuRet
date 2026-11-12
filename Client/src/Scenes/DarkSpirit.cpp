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

// 0x4177eb..0x417e10: los estados 0 y 1, el vuelo libre.
void Fly(DarkSpirit& spirit, const DarkSpiritOwner& owner, std::uint8_t map, World::Particles& particles,
         float worldClock, Render::Models::CrtRandom& random)
{
    const glm::vec3 start = spirit.position;
    const float dx = F32(static_cast<double>(owner.position.x) - static_cast<double>(start.x));
    const float dy = F32(static_cast<double>(owner.position.y) - static_cast<double>(start.y));
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
        const float target = F32(Render::Models::CreateAngle(start.x, start.y, owner.position.x, owner.position.y));
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
        ((owner.pet == HorseIndex || map == LoginMap) ? LoginFlight : Flight) + static_cast<double>(owner.position.z);
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

bool StepDarkSpirit(DarkSpirit& spirit, const Render::Models::Skeleton& skeleton, const DarkSpiritOwner& owner,
                    std::uint8_t map, World::Particles& particles, float worldClock, Render::Models::CrtRandom& random,
                    std::int32_t counter)
{
    spirit.previousPosition = spirit.position;
    if ((owner.flags & OwnerStops) != 0)
    {
        return false;
    }
    FollowTheSafeZone(spirit, owner);
    if (spirit.animation.action < skeleton.actions.size() &&
        Render::Models::AdvanceAnimation(spirit.animation, skeleton.actions[spirit.animation.action], FrameStep,
                                         Render::Models::AnimationScene::Other))
    {
        // 0x417791: más allá de la tabla, la 0.
        Render::Models::SetAction(spirit.animation, skeleton,
                                  spirit.state < ActionOfState.size() ? ActionOfState[spirit.state] : 0);
    }
    if (spirit.state == PerchedState)
    {
        Perch(spirit, owner);
    }
    else if (spirit.state == LandingState)
    {
        Land(spirit, owner);
    }
    else if (spirit.state == FastState || spirit.state == SlowState)
    {
        Fly(spirit, owner, map, particles, worldClock, random);
    }
    // 0x417e15: el grito, con rand() % 100 y el contador que lee de 0x7d3a0f0, con signo.
    const bool shouts = random.Next() % ShoutSpan == 0 && counter % ShoutCounter == 0;
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
    return shouts;
}
} // namespace Client::Scenes
