#include "Scenes/GameHanging.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Attachment.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorName.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "World/ChaosCastle.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/ScepterGlow.h"
#include "World/Sprites.h"
#include "World/WeaponSparkKernels.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Render::Models::BoneTransform;
using Result = Core::Expected<void, std::string>;

// Las texturas de los sprites (0x61ccb0): flare01, Impack03 de Object9, Spark02, Spark03, lightning2,
// Shiny02 y Shiny03.
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t Impact = 0x5ad;
constexpr std::uint16_t SecondSpark = 0x5c5;
constexpr std::uint16_t ThirdSpark = 0x5c6;
constexpr std::uint16_t Lightning = 0x5d5;
constexpr std::uint16_t Shiny = 0x5fd;
constexpr std::uint16_t ThirdShiny = 0x5fe;

constexpr std::int32_t NoItem = -1; // el 0xffff de un ítem, con signo (MOVSX)
constexpr std::int32_t Bolts = 0xa19;
constexpr std::int32_t Arrows = 0xa21;
constexpr std::int32_t Helper02 = 0x1c13;
constexpr std::int32_t Helper06 = 0x1c17;
constexpr std::uint8_t RedPk = 6;
constexpr std::uint32_t HiddenAttachmentFlag = 0x4000;
constexpr std::uint32_t FrozenAnimationFlag = 0x1000;
// Un rand() por cada byte de +0x38e al cerrarse (0x5098e5 y el lazo de 0x50e298).
constexpr std::uint16_t Byte38eCloseRandoms = 1;
constexpr std::uint8_t WingsBone = 0x2f;
// 0x5096ac: el modelo al que 0x509340 le corre el origen y le gira la z (BossHead01, del mapa 5).
constexpr std::int32_t CarriedHead = 0xef;
// 0x743a160..0x743a168: lo que 0x5096ac suma al origen del 0xef. Ningún código los escribe y están en la .bss (la
// imagen inicializada termina en 0x6e6000): 0 (*inferido*, P-C353).
constexpr glm::vec3 CarriedHeadShift{0.0f, 0.0f, 0.0f};
constexpr std::uint8_t PetBone = 0x22;
// 0x6b8a44: en la acción 0x3e la mano derecha no cuelga nada hasta pasar el frame 4.
constexpr float LateSwingFrame = 4.0f;
// 0x50de63: Blood Castle, donde +0x360 del 1 al 3 cuelga el arma del evento.
constexpr std::int32_t FirstBloodCastle = 11;
constexpr std::int32_t LastBloodCastle = 17;
// 0x6b88d4 (0,01f): el alpha más bajo que 0x414470 y 0x5ffdf0 dibujan.
constexpr float DrawnLayerAlpha = std::bit_cast<float>(0x3c23d70au);
// 0x413fe0: los GM llevan la capa 5; 0x41407c crea las del 5 al 13.
constexpr std::uint8_t GameMasterLayerKind = 5;
constexpr std::uint8_t LastLayerKind = 13;
constexpr std::uint32_t GameMasterFlag = 0x1000000;
constexpr std::uint8_t GameMasterControlCode = 0x20;
constexpr std::uint8_t SecondGameMasterControlCode = 8;
// 0x4140ab..0x4140c4: 0x414370(0x12a, 0x14, 1, 70, −5, 0, 0, 0, 45): Skill\MuSign.bmd en el hueso 20 del
// dueño, corrido (70, −5, 0), con los ángulos fijos (0, 0, 45) (+0x93 en 1: no copia los del dueño).
constexpr std::uint16_t MuSign = 0x12a;
constexpr std::uint8_t MuSignBone = 0x14;
constexpr glm::vec3 MuSignOffset{70.0f, -5.0f, 0.0f};
constexpr glm::vec3 MuSignAngles{0.0f, 0.0f, 45.0f};
// +0xdc: 0x414370 pone 0,5, lo que 0x414470 le pasa a 0x4ec200.
constexpr float MuSignSpeed = 0.5f;
// 0x6002e0 con el 0x12a: RenderBody(2) con la luz en 1 y RenderBody(0x44) con (1; 0,5; 0).
constexpr std::uint32_t BasePass = 2;
constexpr std::uint32_t ChromePass = 0x44;
constexpr glm::vec3 MuSignChromeLight{1.0f, 0.5f, 0.0f};

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// rand() % n como el IDIV del original: rand() no es negativo.
int Mod(Render::Models::CrtRandom& random, int n)
{
    return random.Next() % n;
}

// FSIN de `argument` por el float `factor`, un solo R53, y FSIN más `addend` (el FADD de un float), un
// solo R53. DrawGameHanging valida antes el reloj: con él, los dos dan siempre un valor.
double SineTimes(double argument, float factor)
{
    return Render::Models::Detail::SineProductPC53(argument, factor).value_or(0.0);
}

double SinePlus(double argument, double addend)
{
    return Render::Models::Detail::SineSumPC53(argument, addend).value_or(0.0);
}

// El estado del lugar que cuelga y de su modelo de un frame de 0x50ba30.
struct Frame
{
    ActorState& actor;
    const GameHangingPass& pass;
    // [ebp+0xc] de 0x50ba30: (rand() % 30 + 70) · 0,01 del rand de 0x50db57.
    float lum = 0.0f;
    // +0x68 del modelo del tipo del personaje, con que 0x4ec190 escala el desplazamiento: la escala con que se posó el
    // cuerpo, aunque quien llama a 0x509340 haya cambiado la del personaje (0x50c7dd).
    float bodyScale = 0.0f;

    // 0x4ec110 con el jugador (escala y posición del personaje) sobre el hueso `bone` de su pose propia.
    // DrawGameHanging comprueba antes que los huesos de las partes existen.
    [[nodiscard]] glm::vec3 BodyPoint(std::uint8_t bone, const glm::vec3& offset) const
    {
        return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(pass.body[bone]), offset,
                                                         actor.scale, actor.position);
    }

    // 0x4ec110 sobre el hueso `bone` del buffer global, con la escala del personaje y `origin`. Un hueso
    // que ninguna pose escribió todavía es el de la .bss: todo en 0.
    [[nodiscard]] glm::vec3 GlobalPoint(std::size_t bone, const glm::vec3& offset, const glm::vec3& origin) const
    {
        const BoneTransform matrix = bone < pass.carry->bones.size() ? pass.carry->bones[bone] : BoneTransform(0.0f);
        return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset, actor.scale,
                                                         origin);
    }

    // 0x5491e0: el sprite aditivo; el dueño no se guarda.
    void Sprite(std::uint16_t texture, const glm::vec3& point, float scale, const glm::vec3& light,
                float rotation = 0.0f) const
    {
        static_cast<void>(pass.sprites->Create(texture, point, scale, light, rotation, 0));
    }

    // 0x5092d0: el sprite en el hueso `bone` del buffer global, con la escala y la posición del personaje.
    void BoneSprite(std::uint16_t texture, std::size_t bone, float scale, const glm::vec3& light,
                    const glm::vec3& origin) const
    {
        Sprite(texture, GlobalPoint(bone, {}, origin), scale, light);
    }

    // 0x530860 en `address`, sobre los joints del frame. Con el pool lleno no crea ni saca rand().
    [[nodiscard]] Result Joint(const World::FishEffectRequest& request, std::string_view address) const
    {
        if (pass.joints.empty())
            return Core::Unexpected(std::format("{}: faltan los joints del frame (0x530860)", address));
        auto created = CreateFaunaJoint(pass.joints, request, pass.random);
        if (!created.HasValue())
            return Core::Unexpected(created.Error());
        return {};
    }

    // 0x53fe70 en `address` con el dueño en 0, sobre las partículas del frame y el reloj del mundo.
    [[nodiscard]] Result Particle(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                  const glm::vec3& light, std::int32_t subtype, float scale,
                                  std::string_view address) const
    {
        if (pass.particles == nullptr)
            return Core::Unexpected(std::format("{}: faltan las partículas del frame (0x53fe70)", address));
        const auto created =
            pass.particles->CreateChecked(type, position, angles, light, subtype, scale, pass.worldClock, pass.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{}: la partícula {:#x}/{} falla con el error {}", address, type,
                                                subtype, static_cast<int>(created.Error())));
        return {};
    }
};

// El lugar de lo que cuelga por el camino de la bandera (0x5093d2..0x5095e2). El 0 de los ángulos es el de 0x6b86a0.
Core::Expected<Render::Models::HangingPlacement, std::string> PlacementOf(const ActorState& actor, std::int32_t model)
{
    // 0x5093f3..0x50947c: con el +0x376 de 0x84 a 0x86 (las estatuas de Blood Castle, 0x50c83b), el 0xc1c y el 0x225
    // en (90, 0, 90) y (0, 80, 120), y el 0xa24 en (10, 0, 0) y (0, 110, 80). Con otro modelo (0x5095e0), la matriz
    // del lugar queda como estaba en la pila: 0x50ba30 solo les cuelga esos tres, y ningún alta le da ese +0x376 a un
    // 0x20a.
    if (actor.word376 >= 0x84 && actor.word376 <= 0x86)
    {
        if (model == 0xc1c || model == 0x225)
            return Render::Models::HangingPlacement{{90.0f, 0.0f, 90.0f}, {0.0f, 80.0f, 120.0f}};
        if (model == 0xa24)
            return Render::Models::HangingPlacement{{10.0f, 0.0f, 0.0f}, {0.0f, 110.0f, 80.0f}};
        return Core::Unexpected(std::format("0x5095e0: el lugar del modelo {:#x} con el +0x376 {:#x} es la matriz sin "
                                            "iniciar de la pila de 0x509340",
                                            model, actor.word376));
    }
    const bool crossbow = (model >= 0xa1a && model < Arrows) || model == 0xa22 || (model >= 0xa24 && model < 0xa26);
    if (crossbow)
        return Render::Models::HangingPlacement{{0.0f, 20.0f, 180.0f}, {-10.0f, 8.0f, 40.0f}};
    if (model == 0xc1b)
        return Render::Models::HangingPlacement{{110.0f, 180.0f, 90.0f}, {-10.0f, 5.0f, -10.0f}};
    if (model == 0xa26)
        return Render::Models::HangingPlacement{{-60.0f, 0.0f, -80.0f}, {-5.0f, 20.0f, 0.0f}};
    if ((model >= 0xa12 && model < 0xc12) || model == 0xc1a)
        return Render::Models::HangingPlacement{{70.0f, 0.0f, 90.0f}, {-10.0f, 5.0f, 10.0f}};
    return Render::Models::HangingPlacement{{70.0f, 0.0f, 90.0f}, {-20.0f, 5.0f, 40.0f}};
}

// 0x4ec190 y 0x50966b: el origen de lo que cuelga sin la bandera, con la escala `scale` del modelo del tipo.
glm::vec3 OriginOf(const BoneTransform& parent, const glm::vec3& offset, const ActorState& actor, float scale)
{
    const glm::vec3 turned = Render::Models::OriginalVectorRotate(offset, Render::Models::OriginalMatrixOf(parent));
    glm::vec3 origin{};
    for (int k = 0; k < 3; ++k)
        origin[k] = F32(D(F32(D(scale) * D(turned[k]))) + D(actor.position[k]));
    return origin;
}

// 0x509a92..0x50b801: los sprites por el modelo, después de dibujarlo, con su pose en el buffer global.
Result ItemSprites(const Frame& frame, std::int32_t model, const glm::vec3& origin)
{
    ActorState& actor = frame.actor;
    Render::Models::CrtRandom& random = frame.pass.random;
    const double clock = D(frame.pass.worldClock);
    // [ebp+0x28]: (rand() % 30 + 70) · 0,005, aun en los modelos sin caso.
    const float l = F32(D(static_cast<float>(Mod(random, 30) + 70)) * D(0.005f));
    const auto at = [&](std::size_t bone, const glm::vec3& offset) { return frame.GlobalPoint(bone, offset, origin); };
    switch (model)
    {
    case 0x228:
    {
        // 0x509ad9: dos Impack03 a los costados de la guarda.
        const double x = SineTimes(clock * D(0.4f), 0.25f) + D(0.7f);
        const glm::vec3 light{F32(x), F32(x - D(0.5f)), F32(x - D(0.5f))};
        const glm::vec3 left = at(0, glm::vec3(5.0f, -22.0f, -10.0f));
        frame.Sprite(Impact, left, 0.75f, light);
        const glm::vec3 right = at(0, glm::vec3(-5.0f, -22.0f, -10.0f));
        frame.Sprite(Impact, right, 0.75f, light);
        return {};
    }
    case 0x229:
    {
        const float p = F32(D(static_cast<float>(Mod(random, 60))) * D(0.05f) - D(1.5f));
        const float s = F32(D(static_cast<float>(Mod(random, 30))) * D(0.05f) + D(1.5f));
        const float rotation = F32(clock * D(0.0006f) * D(360.0f));
        const glm::vec3 light{0.2f, 0.2f, F32(SineTimes(clock * D(0.7f), 0.2f) + D(0.5f))};
        const glm::vec3 tip = at(4, glm::vec3(0.0f, p, p));
        frame.Sprite(Shiny, tip, s, light, rotation);
        frame.Sprite(Shiny, tip, F32(D(s) - D(0.4f)), light, F32(D(rotation) + D(90.0f)));
        frame.Sprite(Flare, tip, 2.3f, light);
        const glm::vec3 guard = at(4, glm::vec3(30.0f, 0.0f, 0.0f));
        frame.Sprite(Flare, guard, 2.0f, light);
        const glm::vec3 sixth = at(6, glm::vec3{});
        frame.Sprite(Flare, sixth, 2.0f, light);
        const glm::vec3 seventh = at(7, glm::vec3{});
        frame.Sprite(Flare, seventh, 2.0f, light);
        const glm::vec3 eighth = at(8, glm::vec3{});
        frame.Sprite(Flare, eighth, 1.0f, light);
        return {};
    }
    case 0x225:
    {
        const glm::vec3 light{l, l * 0.3f, l * 0.1f};
        for (std::size_t bone = 2; bone <= 8; ++bone)
        {
            const glm::vec3 point = at(bone, glm::vec3{});
            if (Mod(random, 3) == 0)
                frame.Sprite(Shiny, point, 0.6f, glm::vec3(0.4f), static_cast<float>(Mod(random, 360)));
            frame.Sprite(Flare, point, 2.0f, light);
        }
        // 0x509ea8: fuera de las acciones 0x0d a 0x21 y 0x38 a 0x3c, un rand() y dos puntos que nadie usa.
        const std::uint8_t action = actor.animation.action;
        if (!(action >= 0x0d && action <= 0x21) && !(action >= 0x38 && action <= 0x3c))
            static_cast<void>(random.Next());
        return {};
    }
    case 0x226:
    {
        // 0x509f17..0x509ff8: EDI de 0x68b184c a 0x68b18ac, los huesos 1 y 2; en cada uno, los dos flare01 en el
        // mismo punto, el del latido y el gris (0,3).
        for (std::size_t bone = 1; bone <= 2; ++bone)
        {
            const double x = SineTimes(clock * D(0.002f), 0.35f) + D(0.65f);
            const glm::vec3 light{F32(x * D(0.43f)), F32(x * D(0.14f)), F32(x * D(0.6f))};
            const glm::vec3 point = at(bone, glm::vec3{});
            frame.Sprite(Flare, point, F32(x * D(0.9f)), light);
            frame.Sprite(Flare, point, F32(SineTimes(clock * D(0.002f), 0.5f) + D(0.4f)), glm::vec3(0.3f));
        }
        return {};
    }
    case 0x227:
    case 0x231:
    {
        // 0x50a005..0x50a138: en la acción 0x8c, fuera del mapa 10 y con la mitad de los rand(), el hueso 1 corrido
        // (rand() % 30 − 15; rand() % 30 − 15; +20) suelta cuatro veces el joint 0x617/0 (escala 10, sin dueño) y
        // la partícula 0x5c5/0 con la luz blanca, cada par con los ángulos (rand() % 60 + 150; 0; z del personaje).
        if (actor.animation.action != 0x8c || frame.pass.map == 10 || (random.Next() & 1) != 0)
            return {};
        glm::vec3 point = at(1, glm::vec3{});
        point.x = F32(D(static_cast<float>(Mod(random, 30))) - D(15.0f) + D(point.x));
        point.y = F32(D(static_cast<float>(Mod(random, 30))) - D(15.0f) + D(point.y));
        point.z = F32(D(point.z) + D(20.0f));
        const glm::vec3 white(1.0f);
        for (int spark = 0; spark < 4; ++spark)
        {
            glm::vec3 angles{static_cast<float>(Mod(random, 60) + 150), 0.0f, actor.angles.z};
            if (auto created = frame.Joint({0x617, point, point, angles, 0, nullptr, 10.0f, -1, 0, 0, -1}, "0x50a110");
                !created.HasValue())
                return created;
            if (auto created = frame.Particle(SecondSpark, point, angles, white, 0, 1.0f, "0x50a12f");
                !created.HasValue())
                return created;
        }
        return {};
    }
    case 0x619:
    {
        const glm::vec3 point = at(1, glm::vec3{});
        frame.Sprite(Flare, point, 2.0f, {l, l * 0.9f, 0.0f});
        frame.Sprite(Flare, point, F32(SinePlus(clock * D(0.002f), D(0.5f))), glm::vec3(0.5f));
        return {};
    }
    case 0x61a:
    {
        const float scale = F32(SinePlus(clock * D(0.001f), D(1.0f)));
        const glm::vec3 light{l * 0.2f, l * 0.1f, l * 3.0f};
        const glm::vec3 back = at(1, glm::vec3(-15.0f, 0.0f, 0.0f));
        frame.Sprite(Flare, back, scale, light);
        const glm::vec3 front = at(1, glm::vec3(10.0f, 0.0f, 0.0f));
        frame.Sprite(Flare, front, scale, light);
        const float rotation = F32(SineTimes(clock * D(0.01f), 360.0f));
        const glm::vec3 white(F32(SinePlus(clock * D(0.001f), D(1.0f)) * D(0.5f)));
        frame.Sprite(Shiny, front, 0.6f, white, F32(D(360.0f) - D(rotation)));
        frame.Sprite(Shiny, front, 0.6f, white, rotation);
        return {};
    }
    case 0x61b:
    {
        // 0x50a683..0x50a75f: ESI de 1 a 4 sobre 0x68b184c + ESI · 0x30, los huesos 2 a 5; con ESI en 3 (el
        // hueso 4), el Shiny02.
        for (std::size_t bone = 2; bone <= 5; ++bone)
        {
            const double x = SineTimes(clock * D(0.002f), 0.35f) + D(0.65f);
            const glm::vec3 point = at(bone, glm::vec3(-10.0f, 0.0f, 0.0f));
            frame.Sprite(Flare, point, 1.0f, {F32(x * D(0.6f)), F32(x * D(0.8f)), F32(x)});
            if (bone == 4)
                frame.Sprite(Shiny, point, 0.6f, glm::vec3(0.5f), static_cast<float>(Mod(random, 360)));
        }
        return {};
    }
    case 0x61c:
    {
        const float v = F32(SineTimes(clock * D(0.001f), 0.5f) + D(0.7f));
        const glm::vec3 head = at(2, glm::vec3{});
        const glm::vec3 light{v, v * 0.8f, v * 0.6f};
        frame.Sprite(Flare, head, 1.5f, light);
        frame.Sprite(Shiny, head, v * 0.5f, light);
        const float x = v * -10.0f;
        const float rotation = static_cast<float>(Mod(random, 360));
        const glm::vec3 tip = at(1, glm::vec3(x, 0.0f, 0.0f));
        const glm::vec3 white{0.6f, 0.8f, 1.0f};
        frame.Sprite(Flare, tip, 0.7f, white);
        frame.Sprite(Shiny, tip, 0.5f, white, rotation);
        frame.Sprite(Shiny, tip, 0.4f, white, F32(D(360.0f) - D(rotation)));
        return {};
    }
    case 0x61d:
    {
        const glm::vec3 light{1.0f, 0.6f, 0.3f};
        const glm::vec3 first = at(1, glm::vec3{});
        frame.Sprite(Flare, first, 1.3f, light);
        for (int i = 0; i < 3; ++i)
        {
            const glm::vec3 point = at(2, glm::vec3(static_cast<float>(i * 15 - 10), 0.0f, 0.0f));
            frame.Sprite(Flare, point, 1.3f, light);
            frame.Sprite(Shiny, point, 0.4f, light, static_cast<float>(Mod(random, 360)));
        }
        return {};
    }
    case 0x61e:
    {
        const float rotation = static_cast<float>(Mod(random, 360));
        const float x = F32(SineTimes(clock * D(0.002f), 0.3f) + D(0.7f));
        const glm::vec3 point = at(1, glm::vec3{});
        frame.Sprite(Flare, point, 2.0f, {0.0f, x * 0.5f, x});
        frame.Sprite(Shiny, point, 1.0f, glm::vec3(0.5f), rotation);
        frame.Sprite(Shiny, point, 1.0f, glm::vec3(0.5f), F32(D(360.0f) - D(rotation)));
        return {};
    }
    case 0x61f:
    {
        const glm::vec3 light{l, l * 0.3f, l * 0.1f};
        const glm::vec3 point = at(0, glm::vec3{});
        if (Mod(random, 3) == 0)
            frame.Sprite(Shiny, point, 0.6f, glm::vec3(0.4f), static_cast<float>(Mod(random, 360)));
        frame.Sprite(Flare, point, 2.0f, light);
        return {};
    }
    case World::HdkMaceModel:
    {
        if (frame.pass.carry->bones.empty())
            return Core::Unexpected(std::string("0x50a169: el cetro no dejó huesos en el buffer global"));
        World::CreateScepterGlow(*frame.pass.sprites, Render::Models::OriginalMatrixOf(frame.pass.carry->bones[0]),
                                 actor.scale, origin, random);
        return {};
    }
    case 0x81c:
    {
        const glm::vec3 light{l * 0.2f, l * 0.1f, l * 0.8f};
        for (std::size_t bone = 1; bone <= 8; ++bone)
        {
            const glm::vec3 point = at(bone, glm::vec3{});
            frame.Sprite(Flare, point, 1.3f, light);
        }
        return {};
    }
    case 0xa16:
    case 0xa17:
    {
        const glm::vec3 light{l, l * 0.6f, l * 0.2f};
        for (const std::size_t bone : {std::size_t{2}, std::size_t{6}})
            frame.BoneSprite(Shiny, bone, 1.0f, light, origin);
        return {};
    }
    case 0xa18:
    case 0xa23:
    {
        const glm::vec3 light =
            model == 0xa18 ? glm::vec3{l * 0.6f, l, l * 0.8f} : glm::vec3{l * 0.5f, l * 0.5f, l * 0.8f};
        for (std::size_t bone = 13; bone <= 18; ++bone)
            frame.BoneSprite(Shiny, bone, 1.0f, light, origin);
        if (model == 0xa23)
            for (std::size_t bone = 5; bone <= 8; ++bone)
                frame.BoneSprite(Shiny, bone, 1.0f, light, origin);
        return {};
    }
    case 0xa25:
    {
        for (std::size_t bone = 1; bone <= 5; ++bone)
        {
            const glm::vec3 point = at(bone, glm::vec3(0.0f, 0.0f, 10.0f));
            glm::vec3 light{l * 0.5f, l * 0.5f, l * 0.8f};
            frame.Sprite(Shiny, point, 1.0f, light);
            if (bone == 5)
                light = glm::vec3(1.0f);
            frame.Sprite(Flare, point, 2.0f, light);
        }
        return {};
    }
    case 0xa26:
    {
        const float scale = F32(D(F32(SinePlus(clock * D(0.001f), D(1.0f)))) * D(0.8f));
        const glm::vec3 light{l * 3.0f, l, l};
        const glm::vec3 sixth = at(6, glm::vec3{});
        frame.Sprite(Flare, sixth, scale, light);
        const glm::vec3 second = at(2, glm::vec3{});
        frame.Sprite(Flare, second, scale, light);
        const float rotation = F32(SineTimes(clock * D(0.01f), 360.0f));
        const double x = SineTimes(clock * D(0.001f), 1.0f) * 0.3 + 0.30000001192092896;
        if (actor.safeZone != 0)
            return {};
        const glm::vec3 nock = at(9, glm::vec3(10.0f, 0.0f, 0.0f));
        frame.Sprite(Flare, nock, scale, glm::vec3(F32(x)));
        const glm::vec3 shine{F32(D(F32(x)) * D(3.0f)), F32(x), F32(x)};
        frame.Sprite(Shiny, nock, 0.8f, shine, F32(D(360.0f) - D(rotation)));
        frame.Sprite(Shiny, nock, 0.8f, shine, rotation);
        return {};
    }
    case 0xa27:
    {
        // 0x50af03..0x50afac: con la mitad de los rand(), la chispa 0x5c6/0xb de 0,8 en el hueso 4 y en el 12, con
        // la luz (0,8; 0,8; 0,2) y los ángulos del personaje.
        if ((random.Next() & 1) == 1)
        {
            const glm::vec3 light{0.8f, 0.8f, 0.2f};
            const glm::vec3 fourth = at(4, glm::vec3{});
            if (auto created = frame.Particle(ThirdSpark, fourth, actor.angles, light, World::WeaponSparkSubtype, 0.8f,
                                              "0x50af72");
                !created.HasValue())
                return created;
            const glm::vec3 twelfth = at(12, glm::vec3{});
            if (auto created = frame.Particle(ThirdSpark, twelfth, actor.angles, light, World::WeaponSparkSubtype, 0.8f,
                                              "0x50afa7");
                !created.HasValue())
                return created;
        }
        const glm::vec3 seventh = at(7, glm::vec3{});
        frame.Sprite(Flare, seventh, 2.0f, {0.5f, 0.5f, 0.1f});
        const glm::vec3 fifteenth = at(15, glm::vec3{});
        frame.Sprite(Flare, fifteenth, 2.0f, {0.5f, 0.5f, 0.1f});
        const glm::vec3 string = at(0, glm::vec3(20.0f, 0.0f, 0.0f));
        for (const float scale : {0.8f, 1.3f, 2.0f})
            frame.Sprite(Flare, string, scale, {1.0f, 1.0f, 0.4f});
        return {};
    }
    case 0xc1c:
    {
        const glm::vec3 light{l, l * 0.3f, l * 0.1f};
        for (int i = 0; i < 10; ++i)
        {
            const glm::vec3 point = at(0, glm::vec3(static_cast<float>(i * 30 - 180), -40.0f, 0.0f));
            if (Mod(random, 3) == 0)
                frame.Sprite(Shiny, point, 0.6f, glm::vec3(0.4f), static_cast<float>(Mod(random, 360)));
            frame.Sprite(Flare, point, 2.0f, light);
        }
        return {};
    }
    case 0xc1d:
    {
        const double x = SineTimes(clock * D(0.002f), 0.3f) + D(0.7f);
        const float rotation = static_cast<float>(Mod(random, 360));
        const float opposite = F32(D(360.0f) - D(rotation));
        constexpr std::array<std::pair<std::size_t, std::array<float, 2>>, 3> Bones{
            {{5, {1.0f, 0.5f}}, {6, {1.0f, 0.5f}}, {8, {2.0f, 1.0f}}}};
        for (const auto& [bone, scales] : Bones)
        {
            const glm::vec3 point = at(bone, glm::vec3{});
            frame.Sprite(Flare, point, scales[0], {0.0f, F32(x * D(0.5f)), F32(x)});
            frame.Sprite(Shiny, point, scales[1], glm::vec3(0.5f), rotation);
            frame.Sprite(Shiny, point, scales[1], glm::vec3(0.5f), opposite);
        }
        return {};
    }
    case 0xc1e:
    {
        // 0x50b411..0x50b4e7: Spark03 en (0; p − 170; p) del hueso 0, con p = rand() % 60 · 0,05f − 1,5f y la escala
        // rand() % 15 · 0,05f + 1,8f. El punto queda en +0x40 del personaje, y el joint 0x618/0x11 (escala 30, con el
        // personaje como dueño) nace ahí.
        const float p = F32(D(static_cast<float>(Mod(random, 60))) * D(0.05f) - D(1.5f));
        const float s = F32(D(static_cast<float>(Mod(random, 15))) * D(0.05f) + D(1.8f));
        glm::vec3 point = at(0, glm::vec3(0.0f, F32(D(p) - D(170.0f)), p));
        frame.Sprite(ThirdSpark, point, s, glm::vec3(0.4f));
        actor.effectOrigin = point;
        glm::vec3 angles = actor.angles;
        if (auto created = frame.Joint({0x618, point, point, angles, 0x11, &actor, 30.0f, -1, 0, 0, -1}, "0x50b4e7");
            !created.HasValue())
            return created;
        // 0x50b4ef..0x50b5d8: otra p (Q-C362), que corre el desplazamiento sin volver a 0x4ec110, y la escala t =
        // rand() % 15 · 0,05f + 1. En el mismo punto, con la luz (1; 0,4; 1): un destello de t, Shiny02 de t y de t −
        // 0,3f girado 90 y la chispa 0x5c6/0xb de 2 con los ángulos del personaje.
        static_cast<void>(Mod(random, 60));
        const float t = F32(D(static_cast<float>(Mod(random, 15))) * D(0.05f) + D(1.0f));
        const glm::vec3 violet{1.0f, 0.4f, 1.0f};
        frame.Sprite(Flare, point, t, violet);
        frame.Sprite(Shiny, point, t, violet);
        frame.Sprite(Shiny, point, F32(D(t) - D(0.3f)), violet, 90.0f);
        if (auto created =
                frame.Particle(ThirdSpark, point, actor.angles, violet, World::WeaponSparkSubtype, 2.0f, "0x50b5d8");
            !created.HasValue())
            return created;
        // 0x50b5dd..0x50b763: con la luz (a; 0,1; a), a = sin(reloj · 0,7f) · 0,2f + 0,5f − 0,1f, y el giro g =
        // reloj · 0,0006f · 360: Shiny02 de 1,5 y 1,2 en (0; 10; 0) del hueso 2, de 1 y 0,7 en (−40; −10; 0) del hueso
        // 1 y de 1,2 y 1 en (−160; −5; 0) del hueso 1, girados g y g + 90.
        const float a = F32(SineTimes(clock * D(0.7f), 0.2f) + D(0.5f) - D(0.1f));
        const glm::vec3 light{a, 0.1f, a};
        const float rotation = F32(clock * D(0.0006f) * D(360.0f));
        const float turned = F32(D(rotation) + D(90.0f));
        constexpr std::array<std::tuple<std::size_t, glm::vec3, float, float>, 3> Shines{{
            {2, {0.0f, 10.0f, 0.0f}, 1.5f, 1.2f},
            {1, {-40.0f, -10.0f, 0.0f}, 1.0f, 0.7f},
            {1, {-160.0f, -5.0f, 0.0f}, 1.2f, 1.0f},
        }};
        for (const auto& [bone, offset, first, second] : Shines)
        {
            point = at(bone, offset);
            frame.Sprite(Shiny, point, first, light, rotation);
            frame.Sprite(Shiny, point, second, light, turned);
        }
        return {};
    }
    case 0xe21:
    {
        const glm::vec3 point = at(1, glm::vec3(15.0f, -15.0f, 0.0f));
        const glm::vec3 light{l * 0.6f, l * 0.6f, l + l};
        frame.Sprite(Shiny, point, 1.5f, light);
        frame.Sprite(Flare, point, F32(D(l) + D(1.5f)), light);
        return {};
    }
    default:
        return {};
    }
}

// 0x509340: lo que cuelga de `part`, con el modelo `model`, desde su hueso del cuerpo.
Result HangItem(const Frame& frame, const glm::vec3& offset, ActorItemPart& part, std::int32_t model,
                std::uint8_t level, std::uint8_t excellent, bool placed)
{
    ActorState& actor = frame.actor;
    const GameHangingPass& pass = frame.pass;
    if (!Render::Models::HangsAsAModel(static_cast<std::uint16_t>(model)) ||
        (actor.flags78 & HiddenAttachmentFlag) == HiddenAttachmentFlag)
        return {};
    const auto type = static_cast<std::uint16_t>(model);
    // 0x5093bb: 0x607b60 arma el objeto con el tipo del ítem.
    Render::Models::ObjectRandoms randoms{};
    for (std::size_t index = 0; index < HangingRandomCount(type); ++index)
        randoms[index] = pass.random.Next();
    const Render::Models::ObjectState state = Render::Models::NativeItemObjectState(type, pass.frameClock, randoms);
    if (part.bone >= pass.body.size())
        return Core::Unexpected(
            std::format("0x509340: el hueso {:#x} del cuerpo no existe", static_cast<int>(part.bone)));
    const BoneTransform& parent = pass.body[part.bone];
    BoneTransform root = parent;
    glm::vec3 origin = actor.position;
    if (placed)
    {
        const auto placement = PlacementOf(actor, model);
        if (!placement.HasValue())
            return Core::Unexpected(placement.Error());
        root = Render::Models::PlacedRoot(parent, placement.Value());
    }
    else
    {
        origin = OriginOf(parent, offset, actor, frame.bodyScale);
    }
    // 0x50968c..0x5096f1: los ángulos del objeto en 0; el 0xef (solo lo cuelga el +0x376 0x2a, 0x50c79a) suma
    // 0x743a160..0x743a168 al origen, pone en 0 el hueso de la cabeza de su modelo (+0x54) y lleva a la z los bits del
    // reloj del mundo (0x5767b88). Los ángulos van a +0x78 del modelo (0x4eb860), y la pose no los lee: su raíz es la
    // de 0x68c4a1c. La cabeza resta los ángulos x e y del objeto, que son 0, y deja el cuaternión de la key: sin
    // efecto, el +0x54 no se guarda.
    glm::vec3 angles{};
    if (model == CarriedHead)
    {
        for (int k = 0; k < 3; ++k)
            origin[k] = F32(D(CarriedHeadShift[k]) + D(origin[k]));
        angles.z = pass.worldClock;
    }
    std::uint8_t value = 0;
    {
        const Render::Models::LogicalAccess access(actor.byte38eAccesses, pass.random, Byte38eCloseRandoms);
        value = actor.byte38e;
    }
    // 0x509926: con '4', un jugador entre las acciones 0x22 y 0x97 y del frame 5 al 10.
    if (value == 0x34 && actor.type == Data::Models::PlayerSkeletonModel && actor.animation.action >= 0x22 &&
        actor.animation.action <= 0x97 && actor.animation.frame >= 5.0f && actor.animation.frame <= 10.0f)
    {
        part.animation.previousKey = 2;
        part.animation.frame = 3.2f;
    }
    // Q-C102: un modelo que el original no carga es una entrada de la tabla en cero, y 0x509340 no la mira:
    // 0x4ec200 y 0x4eb860 no hacen nada sin acciones (+0x26), 0x4ebc80 no tiene mallas que recorrer (+0x24) y
    // 0x60b3d0 tampoco (su rand() es del set, que 0x509340 pasa en 0). La luz de 0x509a92 y el caso del modelo
    // corren igual. Así, la mano de un tipo que no es un ítem (0xff0 da 0x1202) no dibuja nada. Sin portar:
    // los efectos por nivel de 0x60b3d0 (0x530860, 0x5491e0) sobre un modelo así.
    const Render::Models::Skeleton* skeleton = pass.models.SkeletonOf(type);
    if (pass.models.Find(type) == nullptr || skeleton == nullptr)
        return ItemSprites(frame, model, origin);
    // 0x5093a3 y 0x50997e: la acción del modelo es la de la parte, salvo 0x1a18 en la zona segura (con el
    // modelo de la parte, no con el que dibuja).
    const std::uint8_t action = part.model == 0x1a18 && actor.safeZone != 0 ? 1 : part.animation.action;
    // 0x5099a2..0x5099ed: 0x4ec200 avanza la animación salvo con la bandera en un arma de 0xa12 a 0xc11 o con
    // +0x78 & 0x1000, y sin acciones (+0x26) no hace nada. Lee la acción de +0xa0 sin mirar cuántas tiene el modelo:
    // con una de más, lee el heap después de su arreglo (Q-C1119), y corta. 0x4eb860 la lleva a la 0 (ChooseKeys).
    const bool still = placed && model >= 0xa12 && model < 0xc12;
    const bool advances =
        !still && (actor.flags78 & FrozenAnimationFlag) != FrozenAnimationFlag && !skeleton->actions.empty();
    if (advances && action >= skeleton->actions.size())
        return Core::Unexpected(std::format("0x4ec200: el modelo {:#x} no tiene la acción {}, que lee fuera de su "
                                            "arreglo (Q-C1119)",
                                            model, static_cast<int>(action)));
    Render::Models::AnimationState animation = part.animation;
    animation.action = action;
    if (pass.originalFrame && advances)
    {
        static_cast<void>(Render::Models::AdvanceAnimation(animation, skeleton->actions[action], part.speed,
                                                           Render::Models::AnimationScene::Other));
        part.animation.frame = animation.frame;
        part.animation.previousAction = animation.previousAction;
        part.animation.previousKey = animation.previousKey;
    }
    // 0x509a1a: 0x4eb860 en el buffer global con la raíz de 0x68c4a1c; los dummy quedan como estaban.
    FaunaDrawCarry& carry = *pass.carry;
    if (carry.bones.size() < skeleton->bones.size())
        carry.bones.resize(skeleton->bones.size(), BoneTransform(0.0f));
    const auto bones = std::span(carry.bones).first(skeleton->bones.size());
    if (!Render::Models::PoseAttached(*skeleton, animation, root, bones))
        return Core::Unexpected(std::format("0x4eb860: la pose del modelo {:#x} que cuelga no es válida", model));
    DrawnObject object;
    object.model = type;
    object.animation = animation;
    object.transform = {.position = origin, .angles = angles, .scale = actor.scale};
    object.state = state;
    object.color = actor.drawnLight;
    object.alpha = actor.alpha;
    if (auto published = PublishFaunaPoseRaw(carry, pass.models, type, bones, object.transform, false, true);
        !published.HasValue())
        return published;
    // 0x509a8a: 0x60b3d0 con el nivel · 8, el excelente, sin set, y 2 (0x102 con +0x376 en 0x43 o 0x89).
    ItemDrawContext resources = pass.itemContext != nullptr ? *pass.itemContext : ItemDrawContext{};
    ItemSpecialDrawContext special = resources.special != nullptr ? *resources.special : ItemSpecialDrawContext{};
    glm::vec3 objectLight{};
    special.worldClock = pass.worldClock;
    special.random = &pass.random;
    special.sprites = pass.sprites;
    special.objectFlags = actor.word376 == 0x43 || actor.word376 == 0x89 ? 0x102u : 2u;
    special.objectLight = &objectLight;
    special.publishedObject = &object;
    resources.special = &special;
    resources.ownerContext = &actor;
    if (!resources.poseBones)
        resources.poseBones = [&carry]() { return std::span<const BoneTransform>(carry.bones); };
    if (auto drawn = DrawItemFromMeshes(pass.batch, pass.models, object, pass.frameClock, carry.meshes,
                                        static_cast<std::uint8_t>(level & 15), excellent, 0, 4, resources);
        !drawn.HasValue())
        return drawn;
    return ItemSprites(frame, model, origin);
}

// 0x50dcda..0x50dd70: con la espalda, el arma de BackHandOf; sin ella, las saetas de la izquierda o las
// flechas de la derecha.
struct BackItem
{
    bool selected = false;
    std::size_t hand = 0;
};

BackItem BackItemOf(const ActorState& actor, bool back)
{
    const std::int32_t left = static_cast<std::int16_t>(actor.hands[1].model);
    const std::int32_t right = static_cast<std::int16_t>(actor.hands[0].model);
    if (back)
    {
        if ((left >= 0xa12 && left < Bolts) || left == 0xa23 || left == 0xa26 || left == 0xa27)
            return {true, 1};
        if ((right >= 0xa1a && right < Arrows) || (right >= 0xa22 && right < 0xa23) ||
            (right >= 0xa24 && right < 0xc12))
            return {true, 0};
        return {true, left == Bolts ? std::size_t{1} : std::size_t{0}};
    }
    if (left == Bolts)
        return {true, 1};
    return {right == Arrows, 0};
}

float PlayerActionSpeed(const GameHangingPass& pass, std::size_t action)
{
    const Render::Models::Skeleton* player = pass.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    return player != nullptr && action < player->actions.size() ? player->actions[action].speed : 0.0f;
}

// 0x50ddde..0x50de60: el arma o el carcaj a la espalda, del lugar de las alas.
Result HangTheBack(Frame& frame, bool back, std::uint8_t& petExcellent)
{
    ActorState& actor = frame.actor;
    const BackItem selected = BackItemOf(actor, back);
    const ActorItemPart& hand = actor.hands[selected.hand];
    std::int32_t model = static_cast<std::int16_t>(hand.model);
    std::uint8_t level = hand.level;
    petExcellent = hand.flags1b;
    // 0x50dd8f: con +0x88 en 4 en Lorencia, los subtipos 0xd1 a 0xd3 llevan Bow+8 (0xa1b).
    const bool guard = actor.byte88 == 4 && frame.pass.map == 0 && actor.subtype >= 0xd1 && actor.subtype <= 0xd3;
    if (guard)
    {
        model = 0xa1b;
        level = 8;
    }
    else if (!selected.selected || model == NoItem)
    {
        return {};
    }
    ActorItemPart& wings = actor.wings;
    const float savedFrame = wings.animation.frame;
    wings.bone = WingsBone;
    wings.speed = Render::Models::WingKeysPerOriginalFrame(actor.animation.action);
    const bool placed = model >= 0x212 && model < 0x1012;
    if (placed)
        wings.speed = PlayerActionSpeed(frame.pass, 1);
    if (auto hung = HangItem(frame, {0.0f, 0.0f, 15.0f}, wings, model, level, hand.flags1b, placed); !hung.HasValue())
        return hung;
    // 0x50de5a: vuelve solo el frame; la acción y la key anteriores quedan como las dejó el arma (Q-C296).
    wings.animation.frame = savedFrame;
    return {};
}

// 0x50df22..0x50e05d: las alas y Helper02 con su chispa.
Result HangWingsAndPet(Frame& frame, std::uint8_t petExcellent)
{
    ActorState& actor = frame.actor;
    ActorItemPart& wings = actor.wings;
    if (wings.model != NoItemModel)
    {
        wings.bone = WingsBone;
        wings.speed = Render::Models::WingKeysPerOriginalFrame(actor.animation.action);
        if (auto hung = HangItem(frame, {0.0f, 0.0f, 15.0f}, wings, static_cast<std::int16_t>(wings.model), wings.level,
                                 wings.flags1b, false);
            !hung.HasValue())
            return hung;
    }
    ActorItemPart& pet = actor.pet;
    if (static_cast<std::int16_t>(pet.model) != Helper02)
        return {};
    pet.bone = PetBone;
    pet.speed = Render::Models::PetKeysPerOriginalFrame;
    // 0x50dfa9: el excelente es el del arma de la espalda (Q-C297).
    if (auto hung = HangItem(frame, {20.0f, 0.0f, 0.0f}, pet, Helper02, pet.level, petExcellent, false);
        !hung.HasValue())
        return hung;
    const glm::vec3 point = frame.BodyPoint(pet.bone, {20.0f, 0.0f, 15.0f});
    frame.Sprite(Flare, point, 1.5f, {frame.lum * 0.5f, 0.0f, 0.0f});
    return {};
}

// 0x50e76e..0x50e86a: la acción y lo que avanza el arma de la mano, por la acción del personaje.
void SetTheHandPace(const Frame& frame, ActorItemPart& part, std::int32_t model)
{
    const std::uint8_t action = frame.actor.animation.action;
    const float one = PlayerActionSpeed(frame.pass, 1);
    if (action >= 0x2e && action <= 0x31)
    {
        part.animation.action = 0;
        part.speed = PlayerActionSpeed(frame.pass, 0x2e);
    }
    else if (model == 0x614)
    {
        const bool swing = action >= 0x23 && action <= 0x24;
        part.animation.action = swing ? 2 : 1;
        part.speed = swing ? PlayerActionSpeed(frame.pass, 0x23) : one;
    }
    else if (model == 0x617)
    {
        part.animation.action = 0;
        part.speed = F32(D(one) + D(one));
    }
    else if (model == 0xc18)
    {
        part.animation.action = 0;
        part.speed = F32(D(one) * D(15.0f));
    }
    else if (model >= 0x212 && model < 0x412)
    {
        part.animation.action = 0;
        part.speed = one;
    }
    else
    {
        part.animation.action = 0;
        part.speed = 0.0f;
        part.animation.frame = 0.0f;
        part.animation.previousKey = 0;
    }
}

// 0x50e896..0x50e908: la luz de los sprites del arma por su nivel.
glm::vec3 LevelLight(float lum, std::uint8_t level)
{
    if (level >= 7)
        return {lum * 0.5f, lum * 0.4f, lum * 0.3f};
    if (level >= 5)
        return {lum * 0.3f, lum * 0.3f, lum * 0.5f};
    if (level >= 3)
        return {lum * 0.5f, lum * 0.3f, lum * 0.3f};
    return glm::vec3(lum * 0.3f);
}

// 0x50e921..0x50f828: los sprites de las armas en la mano, con el cuerpo y el buffer global.
Result HandSprites(const Frame& frame, const ActorItemPart& part, glm::vec3 light)
{
    const double clock = D(frame.pass.worldClock);
    const float lum = frame.lum;
    Render::Models::CrtRandom& random = frame.pass.random;
    const glm::vec3& position = frame.actor.position;
    const auto at = [&](const glm::vec3& offset) { return frame.BodyPoint(part.bone, offset); };
    const auto blade = [&](const glm::vec3& offset) -> Result
    {
        const glm::vec3 point = at(offset);
        frame.Sprite(Flare, point, 1.4f, light);
        return {};
    };
    switch (static_cast<std::int16_t>(part.model))
    {
    case 0x216:
    case 0x217:
    case 0x21f:
        return blade({0.0f, -110.0f, 5.0f});
    case 0x219:
    case 0x21c:
        return blade({0.0f, -110.0f, 0.0f});
    case 0x21a:
    case 0x21b:
        return blade({0.0f, -110.0f, -5.0f});
    case 0x21d:
    case 0x220:
        return blade({0.0f, -150.0f, 0.0f});
    case 0x21e:
        light = glm::vec3(lum);
        return blade({0.0f, -160.0f, 0.0f});
    case 0x223:
    {
        // 0x50eb43..0x50ecf0: el ancho sin(reloj · 0,004f) · 10 + 20 y dos joints 0x610/4 con el personaje como dueño
        // y sus ángulos, de (0; −20; −40) a (0; −160; −10) y de (0; −10; 28) a (0; −145; 18) de la mano. Sin sprites.
        const float width = F32(SineTimes(clock * D(0.004f), 10.0f) + D(20.0f));
        glm::vec3 angles = frame.actor.angles;
        constexpr std::array<std::tuple<glm::vec3, glm::vec3, std::string_view>, 2> Edges{{
            {{0.0f, -20.0f, -40.0f}, {0.0f, -160.0f, -10.0f}, "0x50ec26"},
            {{0.0f, -10.0f, 28.0f}, {0.0f, -145.0f, 18.0f}, "0x50ecf0"},
        }};
        for (const auto& [from, to, address] : Edges)
        {
            glm::vec3 first = at(from);
            glm::vec3 second = at(to);
            if (auto created =
                    frame.Joint({0x610, first, second, angles, 4, &frame.actor, width, -1, 0, 0, -1}, address);
                !created.HasValue())
                return created;
        }
        return {};
    }
    case 0x224:
    {
        const glm::vec3 point = at(glm::vec3(0.0f, -20.0f, 15.0f));
        const double x = SinePlus(clock * D(0.004f), D(1.0f)) * D(0.3f);
        const float v = F32(x);
        frame.Sprite(Shiny, point, F32(D(v) + D(1.0f)), {F32(x * D(0.2f)), F32(x * D(0.2f)), v});
        // 0x50eda0..0x50ee6e: tres joints 0x614/10 sin dueño desde ese punto hasta (0; −133; 7) de la mano, con los
        // ángulos del personaje y el ancho v · 20 + 20.
        glm::vec3 first = point;
        glm::vec3 second = at(glm::vec3(0.0f, -133.0f, 7.0f));
        glm::vec3 angles = frame.actor.angles;
        const float width = F32(D(v) * D(20.0f) + D(20.0f));
        for (const std::string_view address : {"0x50ee26", "0x50ee46", "0x50ee69"})
        {
            if (auto created = frame.Joint({0x614, first, second, angles, 10, nullptr, width, -1, 0, 0, -1}, address);
                !created.HasValue())
                return created;
        }
        return {};
    }
    case 0x617:
    {
        light = {lum, lum * 0.6f, lum * 0.4f};
        for (int y = 0; y > -160; y -= 20)
        {
            if (Mod(random, 4) >= 3)
                continue;
            const glm::vec3 point = at(glm::vec3(0.0f, F32(D(static_cast<float>(y)) - D(30.0f)), 0.0f));
            frame.Sprite(Flare, point, 1.0f, light);
        }
        return {};
    }
    case 0x618:
    {
        const glm::vec3 head = at(glm::vec3(0.0f, -84.0f, 0.0f));
        const float v = F32(SineTimes(clock * D(0.004f), 0.3f) + D(0.7f));
        light = {v, v * 0.2f, v * 0.1f};
        frame.Sprite(Shiny, head, F32(D(v) + D(1.5f)), light);
        for (int i = 0; i > -5; --i)
        {
            const glm::vec3 point = at(glm::vec3(0.0f, F32(D(static_cast<float>(i)) * D(20.0f) - D(10.0f)), 0.0f));
            frame.Sprite(Shiny, point, 1.0f, light);
        }
        light = {v * 0.5f, v * 0.1f, v * 0.05f};
        for (const std::size_t bone : {std::size_t{2}, std::size_t{6}})
            frame.BoneSprite(Shiny, bone, 1.0f, light, position);
        return {};
    }
    case 0xa1f:
    case 0xa20:
    {
        light = part.model == 0xa1f ? glm::vec3{lum * 0.2f, lum * 0.4f, lum * 0.6f}
                                    : glm::vec3{lum * 0.6f, lum * 0.4f, lum * 0.2f};
        for (int z = 0; z > -120; z -= 20)
        {
            const glm::vec3 point = at(glm::vec3(0.0f, -20.0f, static_cast<float>(z)));
            frame.Sprite(Flare, point, 2.0f, light);
        }
        return {};
    }
    case 0xa22:
    {
        light = {lum * 0.4f, lum * 0.6f, lum};
        for (int i = 0; i > -6; --i)
        {
            const glm::vec3 point = at(glm::vec3(0.0f, -10.0f, F32(D(static_cast<float>(i)) * D(20.0f))));
            frame.Sprite(Flare, point, 2.0f, light);
        }
        return {};
    }
    case 0xc16:
    {
        const glm::vec3 point = at(glm::vec3(0.0f, -90.0f, 0.0f));
        frame.Sprite(Shiny, point, 2.0f, {lum * 0.4f, lum * 0.8f, lum * 0.6f});
        return {};
    }
    case 0xc17:
    {
        const glm::vec3 point = at(glm::vec3(0.0f, -145.0f, 0.0f));
        light = {lum * 0.4f, lum * 0.6f, lum};
        frame.Sprite(Shiny, point, 1.5f, light);
        frame.Sprite(Lightning, point, 0.3f, light);
        return {};
    }
    case 0xc18:
    {
        const glm::vec3 point = at(glm::vec3(0.0f, -145.0f, 0.0f));
        light = {lum, lum * 0.6f, lum * 0.4f};
        frame.Sprite(SecondSpark, point, 3.0f, light);
        frame.Sprite(ThirdShiny, point, 1.5f, light);
        // 0x50f4fd..0x50f5b1: cuatro chispas 0x5c5/1 de la mano, corridas (rand() % 20 − 10; rand() % 20 − 10 − 90;
        // rand() % 20 − 10), con los ángulos del personaje y la misma luz.
        for (int spark = 0; spark < 4; ++spark)
        {
            const auto x = static_cast<float>(Mod(random, 20) - 10);
            const float y = F32(D(static_cast<float>(Mod(random, 20) - 10)) - D(90.0f));
            const auto z = static_cast<float>(Mod(random, 20) - 10);
            const glm::vec3 offset = at(glm::vec3(x, y, z));
            if (auto created = frame.Particle(SecondSpark, offset, frame.actor.angles, light, 1, 1.0f, "0x50f5a8");
                !created.HasValue())
                return created;
        }
        // 0x50f5b7..0x50f676: de 0 a −180, de a 20, con tres de cada cuatro rand() un destello de 1 en (0; y + 60; 0)
        // con la luz (l; l · 0,2; l · 0,1).
        light = {lum, lum * 0.2f, lum * 0.1f};
        for (int y = 0; y > -200; y -= 20)
        {
            if ((random.Next() & 3) >= 3)
                continue;
            const glm::vec3 flare = at(glm::vec3(0.0f, F32(D(static_cast<float>(y)) + D(60.0f)), 0.0f));
            frame.Sprite(Flare, flare, 1.0f, light);
        }
        return {};
    }
    case 0xc19:
    {
        light = {lum * 0.4f, lum * 0.6f, lum};
        frame.BoneSprite(Shiny, 0x1b, 2.0f, light, position);
        for (std::size_t bone = 0x1c; bone <= 0x25; ++bone)
            frame.BoneSprite(Flare, bone, 1.5f, light, position);
        return {};
    }
    case 0xc1b:
    {
        light = {lum * 0.6f, lum * 0.6f, lum + lum};
        const glm::vec3 head = at(glm::vec3(0.0f, -120.0f, 5.0f));
        frame.Sprite(Shiny, head, 1.5f, light);
        frame.Sprite(Flare, head, F32(D(lum) + D(1.0f)), light);
        const glm::vec3 foot = at(glm::vec3(0.0f, 100.0f, 10.0f));
        frame.Sprite(Flare, foot, F32(D(lum) + D(1.0f)), light);
        return {};
    }
    case 0xe20:
    {
        const glm::vec3 point = at(glm::vec3(20.0f, 0.0f, 0.0f));
        frame.Sprite(Shiny, point, 1.5f, {lum * 0.4f, lum * 0.6f, F32(D(lum) * D(1.5f))});
        return {};
    }
    default:
        return {};
    }
}

// 0x50e298..0x50e9ea: una mano, con +0x38e abierto y cerrado antes de mirarla.
Result HangTheHand(Frame& frame, std::size_t index)
{
    ActorState& actor = frame.actor;
    std::uint8_t value = 0;
    {
        const Render::Models::LogicalAccess access(actor.byte38eAccesses, frame.pass.random, Byte38eCloseRandoms);
        value = actor.byte38e;
    }
    const std::uint8_t action = actor.animation.action;
    const bool right = index == 0;
    if (action == 0x3e && right && !(actor.animation.frame > LateSwingFrame))
        return {};
    if (value == ')' && action == 0x3d && right)
        return {};
    ActorItemPart& part = actor.hands[index];
    const std::int32_t model = static_cast<std::int16_t>(part.model);
    if (model == NoItem || model == Bolts || model == Arrows || model == Helper06)
        return {};
    SetTheHandPace(frame, part, model);
    if (auto hung = HangItem(frame, {}, part, model, part.level, part.flags1b, false); !hung.HasValue())
        return hung;
    const glm::vec3 light = LevelLight(frame.lum, part.level);
    // 0x50e902: los rojos y el nivel 4 del personaje (+0x1d6) no llevan los sprites.
    if (actor.pk >= RedPk || actor.level == 4)
        return {};
    return HandSprites(frame, part, light);
}

bool SomethingHangs(const ActorState& actor)
{
    return actor.wings.model != NoItemModel || static_cast<std::int16_t>(actor.pet.model) == Helper02 ||
           std::ranges::any_of(actor.hands, [](const ActorItemPart& hand) { return hand.model != NoItemModel; });
}

bool WeaponsOnTheBack(const ActorState& actor, std::int32_t map)
{
    constexpr std::uint8_t FirstBackAction = 0x99;
    constexpr std::uint8_t LastBackAction = 0xb8;
    const bool event = map >= 11 && map <= 23;
    const bool restingInFlight = (map == 7 || (map >= 24 && map <= 29) || map == 36) &&
                                 (actor.animation.action == 0x15 || actor.animation.action == 0x1d);
    return !event && (actor.safeZone != 0 || restingInFlight ||
                      (actor.animation.action >= FirstBackAction && actor.animation.action <= LastBackAction));
}

// 0x413fe0: 0x576960 busca "webzen" (0x6c64ac) en el nombre de +0x1d9.
bool NamedLikeAGameMaster(const ActorState& actor)
{
    return ActorNameContains(actor, "webzen");
}

// 0x413fe0: un GM ("webzen" en el nombre, +0x78 & 0x1000000 o el CtlCode en 0x20 u 8) suelta una capa que
// no sea la 5 y pone +0x360 en 5. Después, sin capa y con +0x360 del 5 al 13, la crea: con +0x360 en 5, la
// de MuSign (0x414370); las del 6 al 13 (0x4147c0 con el 0x637) no están portadas. Del 6 al 13 lo escriben solo
// los paquetes: el 0x12 (0x4c89e1, ViewportPlayers.h) y 0x40c140 (CL9.5) copian un byte, y 0x42ab40
// (ApplyCastleSiegeFlags), 0x42ad50 y 0x42ae60 (CL9.5) lo eligen por las banderas 0x40000, 0x80000, 0x400000 y
// 0x800000 de +0x78 (0x42ab40, en el mapa 30); el dibujo de esas capas (0x414820) lee +0x360 y +0x368. +0x360 no lo
// vuelve a 0 nadie más que 0x414340: el 5 queda en el registro.
Result StepTheLayer(ActorState& actor)
{
    const bool gameMaster = NamedLikeAGameMaster(actor) || (actor.flags78 & GameMasterFlag) == GameMasterFlag ||
                            actor.controlCode == GameMasterControlCode ||
                            actor.controlCode == SecondGameMasterControlCode;
    if (gameMaster)
    {
        if (actor.layer.has_value() && actor.byte360 != GameMasterLayerKind)
            ReleaseTheLayer(actor);
        actor.byte360 = GameMasterLayerKind;
    }
    if (actor.layer.has_value() || actor.byte360 < GameMasterLayerKind || actor.byte360 > LastLayerKind)
        return {};
    if (actor.byte360 != GameMasterLayerKind)
        return Core::Unexpected(std::format("0x4140d5: la capa {} (0x4147c0 con el modelo 0x637) no está portada",
                                            static_cast<int>(actor.byte360)));
    // 0x414370: la acción, la anterior, el frame y la key anterior en 0.
    actor.layer = GameMasterLayer{};
    return {};
}

// 0x414470, el dibujo de la capa 5: MuSign en el hueso 20 del dueño, con 0x4ec200 a 0,5 y 0x602e90(objeto, 1,
// 0, 0). Con el alpha de la capa (+0x178) por debajo de 0,01 (o sin orden) no hace nada (0x414496); 0x414370 lo
// deja en 1 y solo 0x5ffdf0 lo baja, en Chaos Castle con +0x1a1 del héroe.
Result DrawMuSign(const Frame& frame, GameMasterLayer& layer)
{
    const GameHangingPass& pass = frame.pass;
    if (!(layer.alpha >= DrawnLayerAlpha))
        return {};
    if (pass.carry == nullptr)
        return Core::Unexpected(std::string("0x5ffdf0: falta el buffer global de la pose (0x68b181c)"));
    if (MuSignBone >= pass.body.size())
        return Core::Unexpected(
            std::format("0x4144d6: el hueso {:#x} del cuerpo no existe", static_cast<int>(MuSignBone)));
    const Render::Models::Skeleton* skeleton = pass.models.SkeletonOf(MuSign);
    if (pass.models.Find(MuSign) == nullptr || skeleton == nullptr)
        return Core::Unexpected(std::string("0x414470: falta Skill\\MuSign.bmd (0x12a) de la capa de los GM"));
    DrawnObject object;
    object.model = MuSign;
    // 0x4144d6: 0x4ec110 con el modelo del dueño sobre su hueso 20, trasladado; +0x93 en 1 deja los ángulos.
    object.transform = {.position = frame.BodyPoint(MuSignBone, MuSignOffset), .angles = MuSignAngles, .scale = 1.0f};
    // 0x414555: 0x4ec200 sobre la acción de +0x115 (0). Sin acciones o con una key no hace nada; entre frames
    // del original, la dibuja como quedó.
    if (pass.originalFrame && layer.animation.action < skeleton->actions.size())
        static_cast<void>(Render::Models::AdvanceAnimation(layer.animation, skeleton->actions[layer.animation.action],
                                                           MuSignSpeed, Render::Models::AnimationScene::Other));
    object.animation = layer.animation;
    // 0x414370: el blendMesh (+0x68) y la malla oculta (+0x5c) en −1, la luz del blendMesh en 1, la luz por
    // vértice (+0xec) prendida y el alpha en 1. 0x5ffce0 suma la luz del terreno, que 0x6002e0 pisa. La U, la V
    // y el +0x78 que 0x6002e0 compara con 0x80 quedan sin inicializar (Q-C302): acá, en 0 y distinto de 0x80.
    object.state = {};
    object.color = glm::vec3(1.0f);
    object.vertexLight = true;
    // 0x5ffdff..0x5ffe3b: en los mapas 18 a 23, con +0x1a1 del héroe, el alpha baja 0,15f y queda guardado (una vez
    // por frame del original); por debajo de 0,01, 0x5ffdf0 sale sin posar y 0x602e90 no llama a 0x6002e0.
    if (pass.originalFrame)
        layer.alpha = World::ChaosCastlePoseAlpha(pass.map, pass.heroByte1a1, layer.alpha);
    if (!(layer.alpha >= DrawnLayerAlpha))
        return {};
    object.alpha = layer.alpha;
    // 0x5ffdf0 con el segundo argumento en 1: la pose en el buffer global, sin llevar los huesos al mundo.
    FaunaDrawCarry& carry = *pass.carry;
    if (auto posed = PublishFaunaPose(carry, pass.models, object, false); !posed.HasValue())
        return posed;
    if (auto drawn = DrawItemMeshPass(pass.batch, pass.models, object, pass.frameClock, carry.meshes, -1, BasePass);
        !drawn.HasValue())
        return drawn;
    DrawnObject chrome = object;
    chrome.color = MuSignChromeLight;
    return DrawItemMeshPass(pass.batch, pass.models, chrome, pass.frameClock, carry.meshes, -1, ChromePass);
}

// 0x4142f0: con capa y sin +0x78 & 0x4000, la dibuja; con +0x360 del 6 al 13, solo si 0x4294b0 (el byte del
// asedio): sin él no dibuja nada.
Result DrawTheLayer(const Frame& frame)
{
    ActorState& actor = frame.actor;
    if (!actor.layer.has_value() || (actor.flags78 & HiddenAttachmentFlag) == HiddenAttachmentFlag)
        return {};
    if (actor.byte360 > GameMasterLayerKind && actor.byte360 <= LastLayerKind)
    {
        if (!frame.pass.siegeActive)
            return {};
        return Core::Unexpected(std::format("0x414330: la capa {} del asedio (el método de +0x364) no está portada",
                                            static_cast<int>(actor.byte360)));
    }
    return DrawMuSign(frame, *actor.layer);
}

// 0x50dcce..0x50e05d, solo con un 0x20a: la espalda, el arma del evento de Blood Castle, la capa de los GM y, fuera de
// Chaos Castle, las alas y Helper02.
Result HangThePlayer(Frame& frame, bool back)
{
    ActorState& actor = frame.actor;
    const GameHangingPass& pass = frame.pass;
    std::uint8_t petExcellent = 0;
    if (auto hung = HangTheBack(frame, back, petExcellent); !hung.HasValue())
        return hung;
    // 0x50de63: en Blood Castle (11 a 17) con +0x360, el arma del evento (del 1 al 3: 0xc1c, 0x225 y 0xa24;
    // con otro valor, el modelo que quedó en [ebp-0x18]). MuRet solo escribe el 5 de los GM (0x413fe0), y lo
    // deja en el registro desde su primer dibujo.
    if (pass.map >= FirstBloodCastle && pass.map <= LastBloodCastle && actor.byte360 != 0)
        return Core::Unexpected(std::string("0x50de84: el arma del evento de Blood Castle (+0x360) no está portada"));
    // 0x50df01 y 0x50df07: la capa de los GM, antes de las alas.
    if (auto layer = StepTheLayer(actor); !layer.HasValue())
        return layer;
    if (auto drawn = DrawTheLayer(frame); !drawn.HasValue())
        return drawn;
    if (!(pass.map >= 18 && pass.map <= 23))
    {
        if (auto hung = HangWingsAndPet(frame, petExcellent); !hung.HasValue())
            return hung;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawGameHanging(ActorState& actor, const GameHangingPass& pass)
{
    // 0x50db57: el rand() de la luz de lo que cuelga, aun sin ningún ítem.
    Frame frame{actor, pass, F32(D(static_cast<float>(Mod(pass.random, 30) + 70)) * D(0.01f)), actor.scale};
    // 0x50dcc2: lo que no es un jugador 0x20a salta a 0x50e060, sin la espalda, el arma del evento, la capa ni las
    // alas.
    const bool player = actor.type == Data::Models::PlayerSkeletonModel;
    if (SomethingHangs(actor) && (pass.carry == nullptr || pass.sprites == nullptr))
        return Core::Unexpected<std::string>(
            "0x509340: faltan el buffer global de la pose (0x68b181c) o la tabla de sprites (0x5491e0)");
    if (!std::isfinite(pass.worldClock) || std::abs(pass.worldClock) >= 0x1p40f)
        return Core::Unexpected<std::string>("0x5767b88: el reloj del mundo está fuera del dominio de FSIN");
    // Los huesos del cuerpo de donde cuelgan las partes y salen sus sprites (BodyPoint no los mira).
    if (SomethingHangs(actor))
    {
        // Lo que no es un 0x20a no cuelga alas ni mascota: solo miran sus manos.
        const std::array<std::uint8_t, 4> bones{actor.hands[0].bone, actor.hands[1].bone,
                                                player ? WingsBone : actor.hands[0].bone,
                                                player ? PetBone : actor.hands[0].bone};
        for (const std::uint8_t bone : bones)
        {
            if (bone >= pass.body.size())
                return Core::Unexpected(
                    std::format("0x509340: el hueso {:#x} del cuerpo no existe", static_cast<int>(bone)));
        }
    }
    const bool back = WeaponsOnTheBack(actor, pass.map);
    if (player)
    {
        if (auto hung = HangThePlayer(frame, back); !hung.HasValue())
            return hung;
    }
    if (back)
        return {};
    // 0x50e060..0x50e0c0: antes de las manos, salvo espalda y +0x4000, un efecto de un 0x20a cada 30 pasos.
    if (player && pass.originalFrame && (actor.flags78 & 0x4800u) == 0x800u && actor.byte88 == 1 &&
        pass.logicSteps % 30 == 0)
        return Core::Unexpected<std::string>(
            "0x50e0c0: el efecto periódico de la bandera +0x78 & 0x800 no está portado");
    for (std::size_t index = 0; index < actor.hands.size(); ++index)
    {
        if (auto hung = HangTheHand(frame, index); !hung.HasValue())
            return hung;
    }
    return {};
}

Core::Expected<void, std::string> HangFromTheWingsSlot(ActorState& actor, const GameHangingPass& pass,
                                                       const glm::vec3& offset, bool placed, float bodyScale)
{
    if (pass.carry == nullptr || pass.sprites == nullptr)
        return Core::Unexpected<std::string>(
            "0x509340: faltan el buffer global de la pose (0x68b181c) o la tabla de sprites (0x5491e0)");
    if (!std::isfinite(pass.worldClock) || std::abs(pass.worldClock) >= 0x1p40f)
        return Core::Unexpected<std::string>("0x5767b88: el reloj del mundo está fuera del dominio de FSIN");
    const Frame frame{actor, pass, 0.0f, bodyScale};
    // 0x50c7e4 y 0x50c8a2: el modelo es el del lugar, con signo (MOVSX); 0x509340 con el nivel, el excelente y el
    // undécimo argumento en 0 y el décimo en 1, como en las otras llamadas de 0x50ba30.
    return HangItem(frame, offset, actor.wings, static_cast<std::int16_t>(actor.wings.model), 0, 0, placed);
}
} // namespace Client::Scenes
