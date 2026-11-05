#include "Scenes/DarkSpirit.h"

#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
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
constexpr int ConstructorRandoms = 12;
constexpr int CellSize = 100;
constexpr int CellCenter = 50;
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
// 0x4e93f0.
constexpr float Tiny = 0.0001f;
constexpr double Degrees = static_cast<double>(57.2957763671875f);
constexpr double Quarter = 90.0;
constexpr double HalfTurn = 180.0;
constexpr double ThreeQuarters = 270.0;
constexpr double Turn = 360.0;
// 0x418301: el trapecio con su lugar por 0,01f (0x6b88d4) y −20 (0xc1a00000); sin pasarlo, la velocidad
// en 3 (0x40400000) salvo en el estado 4.
constexpr double CellsPerUnit = static_cast<double>(0.01f);
constexpr double ViewMargin = -20.0;
constexpr float UnseenSpeed = 3.0f;
constexpr std::uint8_t LandingState = 4;

float F32(double value)
{
    return static_cast<float>(value);
}

// 0x4e93f0: el rumbo de (x1, y1) a (x2, y2), en grados, como lo deja en la pila de la x87.
double CreateAngle(float x1, float y1, float x2, float y2)
{
    const float nx = F32(static_cast<double>(x2) - static_cast<double>(x1));
    const float ny = F32(static_cast<double>(y2) - static_cast<double>(y1));
    if (std::fabs(nx) < Tiny)
    {
        return ny < 0.0f ? 0.0 : HalfTurn;
    }
    if (std::fabs(ny) < Tiny)
    {
        return nx < 0.0f ? ThreeQuarters : Quarter;
    }
    double angle = (std::atan(static_cast<double>(ny) / static_cast<double>(nx)) * Degrees) + Quarter;
    if (nx < 0.0f)
    {
        angle += HalfTurn;
    }
    return angle;
}

// 0x4e9560: `a` hacia `b` a lo sumo `d` grados, como lo deja en la pila de la x87 (con sus ramas, que no
// son simétricas).
double TurnAngle(float from, float to, float step)
{
    double a = static_cast<double>(from);
    double b = static_cast<double>(to);
    const double d = static_cast<double>(step);
    if (a < 0.0)
    {
        a = static_cast<double>(F32(a + Turn));
    }
    if (b < 0.0)
    {
        b = static_cast<double>(F32(b + Turn));
    }
    if (a < HalfTurn)
    {
        const auto low = static_cast<double>(F32(a - d));
        if (!(b < a + d) && a + HalfTurn > b)
        {
            return a + d;
        }
        if (!(low < 0.0))
        {
            if (a + HalfTurn <= b || b < low)
            {
                return low;
            }
            return b;
        }
        if (a + HalfTurn > b || low + Turn <= b)
        {
            return b;
        }
        return low + Turn;
    }
    const auto high = static_cast<double>(F32(a + d));
    if (b < a - d && a - HalfTurn <= b)
    {
        return a - d;
    }
    if (high < Turn)
    {
        if (a - HalfTurn > b)
        {
            return high;
        }
        if (b < high)
        {
            return b;
        }
        return high;
    }
    if (a - HalfTurn <= b || high - Turn > b)
    {
        return b;
    }
    return high - Turn;
}

void SetStateOf(DarkSpirit& spirit, std::uint8_t state)
{
    if (spirit.state != state)
    {
        spirit.state = state;
        spirit.far = 0;
    }
}
} // namespace

DarkSpirit CreateDarkSpirit(int cellX, int cellY, double groundHeight, Render::Models::CrtRandom& random)
{
    for (int k = 0; k < ConstructorRandoms; ++k)
    {
        (void)random.Next();
    }
    DarkSpirit spirit;
    spirit.position = glm::vec3(static_cast<float>((cellX * CellSize) + CellCenter),
                                static_cast<float>((cellY * CellSize) + CellCenter), F32(groundHeight + BirthHeight));
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
    if (spirit.animation.action < skeleton.actions.size() &&
        Render::Models::AdvanceAnimation(spirit.animation, skeleton.actions[spirit.animation.action], FrameStep,
                                         Render::Models::AnimationScene::Other) &&
        spirit.state < ActionOfState.size())
    {
        Render::Models::SetAction(spirit.animation, skeleton, ActionOfState[spirit.state]);
    }
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
        const float target = F32(CreateAngle(start.x, start.y, owner.position.x, owner.position.y));
        const int turn = random.Next() % TurnSpan;
        spirit.angles.z = F32(TurnAngle(spirit.angles.z, target, F32(static_cast<double>(turn) + TurnBase)));
    }
    const glm::vec3 move =
        Render::Models::OriginalVectorRotate(spirit.direction, Render::Models::OriginalAngleMatrix(spirit.angles));
    spirit.position.x = F32(static_cast<double>(move.x) + static_cast<double>(spirit.position.x));
    spirit.position.y = F32(static_cast<double>(move.y) + static_cast<double>(spirit.position.y));
    const double z = static_cast<double>(move.z) + static_cast<double>(spirit.position.z);
    spirit.position.z = F32(z);
    const double flight = (map == LoginMap ? LoginFlight : Flight) + static_cast<double>(owner.position.z);
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
    SetStateOf(spirit, spirit.direction.y < FastBelow ? 0 : 1);
    // 0x417e3e: el grito, con rand() % 100 y el contador que lee de 0x7d3a0f0, con signo.
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
