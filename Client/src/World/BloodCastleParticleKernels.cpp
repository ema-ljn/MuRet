#include "World/BloodCastleParticleKernels.h"

#include "Render/Models/Cloth.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>

namespace Client::World
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

constexpr int FullTurn = 360;
constexpr int Tens = 10;
constexpr int Spread = 5;
// 0x6b8b90 (0,02f), 0x6b882c (0,5), 0x6b87b4 (0,4f), 0x6b8854 (0,2f), 0x6b87b8 (0,1f) y 0x6b87d4 (1).
constexpr double Fiftieth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
constexpr double Half = 0.5;
constexpr double TwoFifths = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));
constexpr double Fifth = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr double One = 1.0;
// 0x6ba30c (0,95f), 0x6b89dc (0,3f), 0x6ba190 (0,07f), 0x6ba3ac (0,09f), 0x6b8a20 (0,6f), 0x6b8a38 (0,8f),
// 0x6b8a48 (0,05f), 0x6b8acc (0,04f) y 0x6b89e4 (0,02f).
constexpr double Drag = static_cast<double>(std::bit_cast<float>(0x3f733333u));
constexpr double SparkLift = static_cast<double>(std::bit_cast<float>(0x3e99999au));
constexpr double SparkGrowth = static_cast<double>(std::bit_cast<float>(0x3d8f5c29u));
constexpr double WideSparkGrowth = static_cast<double>(std::bit_cast<float>(0x3db851ecu));
constexpr double FlameLift = static_cast<double>(std::bit_cast<float>(0x3f19999au));
constexpr double FlameJitter = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
constexpr double FlameGrowth = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr double FlameLight = static_cast<double>(std::bit_cast<float>(0x3d23d70au));
constexpr double SmokeLight = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
// 0x6ba348 (0,08f): la luz de la 6ac/2, a la que 0x547a01 le resta el 1 de 0x6b87d4.
constexpr double BrightFlameLight = static_cast<double>(std::bit_cast<float>(0x3da3d70au));

// 0x543a7b, 0x5432a1: las escalas fijas de la 6ac y del 622/6.
constexpr float FlameScale = 0.5f;
constexpr float SmokeScale = std::bit_cast<float>(0x3e4ccccdu);
// 0x543b82: la vida que deja el caso del 6ec con un subtipo que no es 7.
constexpr std::int32_t SixEcLife = 24;

// fild del entero y fmul del float de la tabla, guardado en float.
float Scaled(int value, double factor)
{
    return F32(static_cast<double>(value) * factor);
}

// 0x543a9f..0x543ae0 y 0x5432b3..0x5432f9: la x (EBX, +0x54) y la z, con la y en 0.
void ForwardSpeed(Particle& p, Render::Models::CrtRandom& random)
{
    const int forward = random.Next() % Tens + Spread;
    p.velocity.y = 0.0f;
    p.velocity.x = Scaled(forward, TwoFifths);
    p.velocity.z = Scaled(random.Next() % Tens + Spread, Fifth);
}

void CreateSpark(Particle& p, Render::Models::CrtRandom& random)
{
    constexpr int LifeBase = 20;
    p.life = random.Next() % Spread + LifeBase;
    p.rotation = 0.0f;
    const bool narrow = p.subtype == 0;
    const double scaleStep = narrow ? Fiftieth : Tenth;
    const double scaleBase = narrow ? Half : One;
    const double sideStep = narrow ? TwoFifths : Fifth;
    const double riseStep = narrow ? Fifth : Tenth;
    p.scale = F32(static_cast<double>(random.Next() % Tens) * scaleStep + scaleBase);
    p.velocity.x = Scaled(random.Next() % Tens + Spread, sideStep);
    p.velocity.y = Scaled(random.Next() % Tens - Spread, sideStep);
    p.velocity.z = Scaled(random.Next() % Tens + Spread, riseStep);
}

void CreateFlame(Particle& p, Render::Models::CrtRandom& random)
{
    constexpr int LifeBase = 25;
    p.life = random.Next() % Spread + LifeBase;
    const int turn = random.Next() % FullTurn;
    p.scale = FlameScale;
    p.rotation = static_cast<float>(turn);
    ForwardSpeed(p, random);
    // 0x543ae3: el caso del 6ec y del 6ed, con un subtipo que no es 7.
    p.life = SixEcLife;
    const int side = random.Next() % Tens - Spread;
    p.velocity.y = 0.0f;
    p.velocity.x = Scaled(side, TwoFifths);
    p.velocity.z = Scaled(random.Next() % Tens + Spread, Fifth);
    // 0x543e7d: el inicio vuelve a ser la posición.
    p.start = p.position;
}

void CreateSmoke(Particle& p, Render::Models::CrtRandom& random)
{
    constexpr int LifeBase = 25;
    p.life = random.Next() % Spread + LifeBase;
    const int turn = random.Next() % FullTurn;
    p.scale = SmokeScale;
    p.rotation = static_cast<float>(turn);
    ForwardSpeed(p, random);
}

// fld de la velocidad o de la posición, fadd de la otra, guardado en float; después la x y la y frenan.
void Drift(Particle& p)
{
    p.position.x = F32(D(p.velocity.x) + D(p.position.x));
    p.position.y = F32(D(p.position.y) + D(p.velocity.y));
    p.position.z = F32(D(p.position.z) + D(p.velocity.z));
    p.velocity.x = F32(D(p.velocity.x) * Drag);
    p.velocity.y = F32(D(p.velocity.y) * Drag);
}

void SetLight(Particle& p, float level)
{
    p.light = glm::vec3(level);
}

// rand() & 0x80000003 con el ajuste del signo, menos 2.
int Jitter(Render::Models::CrtRandom& random)
{
    constexpr int Mask = 3;
    constexpr int Center = 2;
    return (random.Next() & Mask) - Center;
}

void StepSpark(Particle& p)
{
    Drift(p);
    SetLight(p, F32(static_cast<double>(p.life) * Tenth));
    p.velocity.z = F32(D(p.velocity.z) + SparkLift);
    p.scale = F32(D(p.scale) + (p.subtype == 0 ? SparkGrowth : WideSparkGrowth));
}

// 0x5479f6..0x547a0f: fild de la vida y fmul; la 6ac/2 (0x5479db, el subtipo leído antes del producto) resta 1 y
// puede quedar negativa, que la función fija recorta a 0 en el glColor. 0x54748a..0x54756d: el 622/6.
float FlameOrSmokeLight(const Particle& p)
{
    const double life = static_cast<double>(p.life);
    if (p.type == CloudParticle)
        return F32(life * SmokeLight);
    if (p.subtype == BrightFlameSubtype)
        return F32(life * BrightFlameLight - One);
    return F32(life * FlameLight);
}

// 0x54793f..0x547a38 y 0x54748a..0x54756d: lo mismo salvo la luz.
void StepFlameOrSmoke(Particle& p, Render::Models::CrtRandom& random)
{
    Drift(p);
    p.velocity.z = F32(D(p.velocity.z) + FlameLift);
    p.position.x = F32(static_cast<double>(Jitter(random)) + D(p.position.x));
    p.position.y = F32(static_cast<double>(Jitter(random)) + D(p.position.y));
    p.position.z = F32(static_cast<double>(Jitter(random)) * FlameJitter + D(p.position.z));
    p.scale = F32(D(p.scale) + FlameGrowth);
    SetLight(p, FlameOrSmokeLight(p));
    p.rotation = F32((static_cast<double>(random.Next() & 1) + D(p.rotation)) + One);
}
} // namespace

bool IsBloodCastleParticle(std::int32_t type, std::int32_t subtype)
{
    // 0x543a5d: la 6ac/2 se crea como la /0; de los subtipos de la llama, solo el 1 (la escala, 0x543a84), el 5 y
    // el 6 (la vida, 0x543bc1) y el 7 (0x543ae3) cambian algo.
    return (type == TorchSparkParticle && (subtype == 0 || subtype == 1)) ||
           (type == TorchFlameParticle && (subtype == 0 || subtype == BrightFlameSubtype)) ||
           (type == CloudParticle && subtype == TorchSmokeSubtype);
}

void CreateBloodCastleParticle(Particle& particle, Render::Models::CrtRandom& random)
{
    if (particle.type == TorchSparkParticle)
        CreateSpark(particle, random);
    else if (particle.type == TorchFlameParticle)
        CreateFlame(particle, random);
    else
        CreateSmoke(particle, random);
}

void StepBloodCastleParticle(Particle& particle, Render::Models::CrtRandom& random)
{
    if (particle.type == TorchSparkParticle)
        StepSpark(particle);
    else
        StepFlameOrSmoke(particle, random);
}
} // namespace Client::World
