#pragma once

#include "Core/Expected.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

// CL11, lote magos: los efectos con modelo de las magias (Data\Skill\Ice, Fire, Poison, Circle, Magic, Storm, Laser,
// Saw, Bone y Snow, 0x615700) en 0x518cf0 (el alta), 0x5208a0 (el paso) y su cola común (0x52cb98). Los llaman los
// casos de habilidad de 0x4fd5f0 (Scenes/AttackSkillCases), los ataques de los monstruos (0x4f5b70, 0x439bd0,
// 0x439cd0, 0x43a7a0 y 0x429110, Scenes/MonsterAttackStep), 0x4f97b0 (Scenes/AttackFrameSkills), las banderas de
// 0x4e6380 (Scenes/ActorEffectFlags) y la muerte (0x4f5660, Scenes/ActorCombat). 06, "Los efectos de los magos".
namespace Client::World
{
struct FishEffectRequest;
struct ModelEffect;
struct ModelEffectArguments;
class ModelEffects;
class Particles;
class FrameSprites;

// Los tipos (0x615700 carga sus modelos; el 0xbd y el 0xc7 no tienen y 0x52d620 no dibuja el 0xbd).
inline constexpr std::int16_t ShadowRainEffect = 0xbd;     // 0x51bc43: diez joints 0x60f/0x10 que bajan.
inline constexpr std::int16_t IceEffect = 0xc1;            // Skill\Ice01
inline constexpr std::int16_t FireEffect = 0xc2;           // Skill\Fire01 (el meteoro y sus parientes)
inline constexpr std::int16_t PoisonEffect = 0xc3;         // Skill\Poison01
inline constexpr std::int16_t HellGateEffect = 0xc5;       // Skill\HellGate
inline constexpr std::int16_t PlainSkillEffect = 0xc7;     // 0x51abbf: vida 10, sin modelo.
inline constexpr std::int16_t InnerCircleEffect = 0xcb;    // Skill\Circle01
inline constexpr std::int16_t OuterCircleEffect = 0xcc;    // Skill\Circle02
inline constexpr std::int16_t FollowingMagicEffect = 0xcd; // Skill\Magic01
inline constexpr std::int16_t MagicEffect = 0xce;          // Skill\Magic02
inline constexpr std::int16_t StormEffect = 0xcf;          // Skill\Storm01
inline constexpr std::int16_t LaserEffect = 0xd0;          // Skill\Laser01
inline constexpr std::int16_t SawEffect = 0xd4;            // Skill\Saw01
inline constexpr std::int16_t FirstBoneEffect = 0xd5;      // Skill\Bone01
inline constexpr std::int16_t SecondBoneEffect = 0xd6;     // Skill\Bone02
inline constexpr std::int16_t SnowEffect = 0xd7;           // Skill\Snow01
inline constexpr std::int16_t FirstSnowStoneEffect = 0xd8; // Skill\Snow02, la piedrita del 0xd7
inline constexpr std::int16_t SecondSnowStoneEffect = 0xd9;

// Lo que leen del dueño (+0x108) el paso del 0xc1/1 y /2, del 0xc5, del 0xcd, del 0xbd, del 0xcb/1 y 0x518b60 (el
// 0xc2/1 y el 0xd7): el byte vivo (+0x0), el tipo (+0x2), la posición (+0x10), +0x78, la acción (+0x111), +0x16c y
// +0x1bc. Los dueños que crean los llamadores portados son personajes (Scenes::ActorState).
struct WizardOwnerView
{
    bool alive = false;
    std::uint16_t type = 0;
    glm::vec3 position{};
    std::uint32_t flags78 = 0;
    std::uint8_t action = 0;
    bool seen = false;
    std::uint8_t byte1bc = 0;
};

// Lo que las altas y los pasos piden afuera del efecto. Sin un servicio que su rama alcanza, el caso corta en su
// dirección.
struct WizardEffectServices
{
    // 0x53fe70, con el reloj del mundo (0x5767b88) que leen sus altas y el seno del 0xc1/1.
    Particles* particles = nullptr;
    float worldClock = 0.0f;
    // 0x5491e0.
    FrameSprites* sprites = nullptr;
    // 0x5f9780 sobre 0x7b4ccc4.
    Terrain::TerrainLighting* lighting = nullptr;
    // 0x518cf0 para los hijos que crean los pasos (las piedritas 0xc8/0xc9 y 0xd8/0xd9).
    ModelEffects* effects = nullptr;
    // 0x5f93c0.
    std::function<double(float, float)> terrainHeight{};
    // 0x7d0d348 (0x5f82a0 y 0x5f82c0).
    const Data::Terrain::TerrainAttributes* terrainAttributes = nullptr;
    // 0x6c9dd0.
    const std::int32_t* map = nullptr;
    // 0x7d325c8, el temblor de la cámara.
    float* cameraShake = nullptr;
    // 0x4294b0, el byte del asedio (0x7239ec).
    bool siegeActive = false;
    // 0x41ed10(id, 0, 0).
    std::function<void(Audio::SoundId)> sound{};
    // 0x530860.
    std::function<Core::Expected<void, std::string>(const FishEffectRequest&)> joint{};
    // El dueño (+0x108) leído en vivo; nada si no es un personaje.
    std::function<std::optional<WizardOwnerView>(const void*)> ownerOf{};
    // [0x743a170], el héroe publicado.
    const void* hero = nullptr;
    // 0x517a20(+0x89, la posición, `radius`, el byte +0x8c, +0x8a, 0) sobre el efecto (Scenes/SkillHits).
    std::function<Core::Expected<void, std::string>(const ModelEffect&, float)> skillHits{};
    // El registro propio (0x7681eb4): la cuenta de accesos abiertos y su +0xf04.
    std::uint8_t* ownRecordDepth = nullptr;
    std::uint8_t* ownRecordCount = nullptr;
    // 0x52cc21: la cola publica la acción del efecto en la tabla de modelos antes de animar.
    std::function<void(std::uint16_t, std::uint8_t)> publishAction{};
    // 0x518cf0 con un tipo de otro lote: el 0xf6/2 que suelta el 0xc2/6..8 al caer (0x524d38), con el meteoro de
    // dueño (+0x108). Scenes::WizardEffectServicesOf lo da con World::CreateBlastModelEffect.
    std::function<Core::Expected<void, std::string>(const ModelEffectArguments&)> otherEffect{};
    // 0x524a8d..0x524b21: el 0xc2/4 que toca el suelo en (x, y) empuja a los 400 personajes del pool (de a 0x438) que
    // están vivos, vistos (+0x16c), no son el héroe, no se mueren (+0x389 en 0) y quedan a 200 o menos (la raíz de
    // dy² + dx² en x87, con dx y dy guardados en float; `fcomp` y `test ah,0x41`): el 0x20a con Fenrir (+0x318 en
    // 0x1c37) va a 0x4e9cd0 (la acción de golpe por las manos), el 0x20a sin él a 0x4ea030 con 0xc2 y los demás con 5.
    // Sigue con todos y no saca rand(); después el fuego cae en el suelo igual.
    std::function<Core::Expected<void, std::string>(float, float)> pushAround{};
};

// Si 0x518cf0 tiene el caso de `type` en este lote.
[[nodiscard]] bool IsWizardModelEffect(std::int16_t type);

// 0x518cf0(arguments) con un tipo del lote: el prefijo (ModelEffects::Begin) y su caso, con los rand(), las
// partículas, la luz, los joints y el registro propio que pide. Con la tabla llena, nada y sin rand(). El 0xc1/1 y /2
// escriben (0,4f; 0,3f; 0,2f) en la luz del llamador (`callerLight`, el cuarto argumento, 0x51acd2). Un subtipo sin
// caso sale por 0x51db73 con el prefijo y lo que el caso escribió antes de mirarlo. Otro tipo, o una rama que pide
// algo que MuRet no tiene (CL11 de otro lote), corta con su dirección.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateWizardModelEffect(ModelEffects& effects, const ModelEffectArguments& arguments, Crt::CrtRandom& random,
                        const WizardEffectServices& services, glm::vec3* callerLight = nullptr);

// 0x5208a0 sobre un efecto del lote: el primer rand() de la luz local (0x5208c5), el caso del tipo y la cola común
// (0x52cb98): la animación, 0x51fc80 (sin girar con el 0xd4) y una vida menos; con 0 o menos, muere. La cola corre
// aunque el caso lo haya apagado. `skeleton` es el del modelo (o nullptr). Deja en previousPosition la posición de
// antes del paso.
[[nodiscard]] Core::Expected<void, std::string> StepWizardModelEffect(ModelEffect& effect,
                                                                      const Models::Skeleton* skeleton,
                                                                      Crt::CrtRandom& random,
                                                                      const WizardEffectServices& services);
} // namespace Client::World
