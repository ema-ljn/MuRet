#include "World/EventMapObjectStep.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>

namespace Client::World
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

// 0x604ad5..0x604ae2: ftol(reloj) % 4.000 (0xfa0) contra 1.000 (0x3e8), con signo.
constexpr std::int32_t FlashPeriod = 4000;
constexpr std::int32_t FlashWindow = 1000;
// 0x604aee: rand() % 100.
constexpr int FlashOdds = 100;
// 0x604b03..0x604b10: rand() % 12 + 4, por 0,1f (0x6b87b8).
constexpr int FlashSpan = 12;
constexpr int FlashBase = 4;
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
// 0x6b8854 (0,2f), 0x6b89dc (0,3f) y 0x6b882c (0,5).
constexpr double FlashRed = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
constexpr double FlashGreen = static_cast<double>(std::bit_cast<float>(0x3e99999au));
constexpr double FlashBlue = 0.5;
// 0x604b48 y 0x604b6e: rand() % 1.200 (0x4b0), menos 600 (0x6b8c08).
constexpr int FlashSpread = 1200;
constexpr double FlashCenter = 600.0;
// 0x604b0f: el radio, el 12 que quedó en ecx del idiv.
constexpr std::int32_t FlashRadius = 12;
// 0x604b96..0x604b9b: 0x41ed10(1, 0, 1).
constexpr std::int32_t DevilSquareAmbient = 1;

// 0x606328..0x60638e: rand() % 900 (0x384) − 300 (0x12c) en x e y, rand() % 50 (0x32) + 250 (0x6b8920) en z.
constexpr int OrbSpread = 900;
constexpr int OrbBack = 300;
constexpr int OrbRise = 50;
constexpr double OrbHeight = 250.0;
// 0x606396: rand() & 0x80000003 con el ajuste del signo, el resto de dividir por 4.
constexpr int OrbOdds = 4;
// 0x6063a4..0x6063b9: 0x53fe70(0x60f, punto, ángulos en 0, luz (1; 1; 1), 3, 0x3e428f5c, 0).
constexpr std::int32_t OrbSubtype = 3;
constexpr float OrbScale = std::bit_cast<float>(0x3e428f5cu);

constexpr std::int16_t FirstGate = 9;
constexpr std::int16_t LastGate = 10;
constexpr std::int16_t OpenGate = 4;

// fild del entero, fadd del float del héroe y fsub de 600, guardado en float.
float FlashCoordinate(int spread, float hero)
{
    return F32((static_cast<double>(spread) + static_cast<double>(hero)) - FlashCenter);
}
} // namespace

Core::Expected<void, std::string> StepDevilSquarePrelude(Render::Models::CrtRandom& random,
                                                         const ObjectStepGlobals& globals)
{
    const std::optional<std::int32_t> clock = OriginalFtolLow(static_cast<double>(globals.worldClock));
    if (!clock)
        return Core::Unexpected(std::string("0x604acf: el reloj del mundo no tiene un ftol acreditado"));
    if (*clock % FlashPeriod < FlashWindow && random.Next() % FlashOdds == 0)
    {
        const double level = static_cast<double>(random.Next() % FlashSpan + FlashBase) * Tenth;
        const glm::vec3 color{F32(level * FlashRed), F32(level * FlashGreen), F32(level * FlashBlue)};
        const int spreadY = random.Next() % FlashSpread;
        if (globals.heroPosition == nullptr)
            return Core::Unexpected(std::string("0x604b53: falta el héroe (0x743a170) para la luz del mapa 9"));
        const float y = FlashCoordinate(spreadY, globals.heroPosition->y);
        const int spreadX = random.Next() % FlashSpread;
        const float x = FlashCoordinate(spreadX, globals.heroPosition->x);
        if (auto lit = AddDynamicLight(globals.dynamicLighting, glm::vec3(x, y, 0.0f), color, FlashRadius, "0x604b8e");
            !lit.HasValue())
            return lit;
    }
    if (!globals.loopedSound)
        return Core::Unexpected(std::string("0x604b9b: falta el sonido en bucle (0x41ed10(1, 0, 1)) del mapa 9"));
    globals.loopedSound(DevilSquareAmbient);
    return {};
}

Core::Expected<void, std::string> StepBloodCastlePrelude(Render::Models::CrtRandom& random, const glm::vec3& hero,
                                                         Particles* particles, const float& worldClock)
{
    // fild del entero y fadd del float del héroe; la z suma después los 250. Todo cabe exacto en la x87.
    const int spreadX = random.Next() % OrbSpread - OrbBack;
    const float x = F32(static_cast<double>(spreadX) + static_cast<double>(hero.x));
    const int spreadY = random.Next() % OrbSpread - OrbBack;
    const float y = F32(static_cast<double>(spreadY) + static_cast<double>(hero.y));
    const int rise = random.Next() % OrbRise;
    const float z = F32((static_cast<double>(rise) + static_cast<double>(hero.z)) + OrbHeight);
    if (random.Next() % OrbOdds != 0)
        return {};
    if (particles == nullptr)
        return Core::Unexpected(std::string("0x6063be: falta el pool de partículas (0x53fe70) para la órbita "
                                            "0x60f/3 de Blood Castle"));
    PortableParticleTrigonometry trigonometry;
    const auto created = particles->CreateChecked(OrbitParticle, glm::vec3(x, y, z), glm::vec3(0.0f), glm::vec3(1.0f),
                                                  OrbSubtype, OrbScale, std::cref(worldClock), random, 0, trigonometry);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x6063be: la órbita 0x60f/3 de Blood Castle falla con error {}",
                                            static_cast<int>(created.Error())));
    return {};
}

void StepBloodCastleObject(WorldObject& object)
{
    if (object.type >= FirstGate && object.type <= LastGate && object.field8a != OpenGate)
        object.state.hiddenMesh = HiddenObject;
}
} // namespace Client::World
